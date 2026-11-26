/**
 * @file   MultiplayerLobbyDetailPage.cpp
 * @brief  联机大厅房间详情页实现
 * @author BlockBox Team
 * @date   2026-11-13
 *
 * 版式、卡片与文字颜色全部通过 @TOKEN@ 令牌解析，跟随主题色与亮 / 暗主题；
 * 窄窗口时把左右两栏改成单栏，并让「加入这个房间」和「正在游玩」排在介绍前面。
 * 右栏的「正在游玩」来自房间数据里的 players：房主固定第一，其余是进游戏后还在上报的玩家。
 * 加入卡里的「分享」会把房间名、邀请码和版本拼成一段文案，复制给朋友或直接发论坛私信。
 */
#include "MultiplayerLobbyDetailPage.h"

#include "components/AppMessageBox.h"
#include "components/ForumKit.h"
#include "components/ForumPostBrowser.h"
#include "components/MasonryContentCard.h"
#include "components/PlayerCountChart.h"
#include "components/QuizDialog.h"
#include "components/ShareToFriendDialog.h"
#include "layouts/FlowLayout.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/ForumPlayerStatsService.h"
#include "utils/IconHelper.h"
#include "utils/Lobby/LobbyInstanceMatcher.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"
#include "utils/TouchScroll.h"
#include "utils/links/LinkRouter.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayoutItem>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QStyle>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace
{
/** 和论坛服务端一致：一条评论最多 300 字 */
constexpr int kCommentMaxLength = 300;

QString elide(const QString &text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars - 1) + QStringLiteral("…");
}

QString roomTitle(const ForumLobbyRoom &room)
{
    const QString title = room.title.trimmed();
    return title.isEmpty() ? QObject::tr("未命名房间") : title;
}

QString hostName(const ForumLobbyRoom &room)
{
    const QString display = room.host.displayName.trimmed();
    return display.isEmpty() ? room.host.username.trimmed() : display;
}

/** 核心类型 → 封面 / 横幅上的单字徽记（与大厅卡片保持一致） */
QString coreGlyph(const ForumLobbyRoom &room)
{
    if (room.core == QLatin1String("terracotta"))
        return QStringLiteral("陶");
    if (room.core == QLatin1String("hongshi"))
        return QStringLiteral("红");
    if (room.core == QLatin1String("frp"))
        return QStringLiteral("穿");

    const QString label = room.coreLabel.trimmed().isEmpty()
                              ? ForumLobbyService::coreDisplayName(room.core)
                              : room.coreLabel.trimmed();
    return label.isEmpty() ? QStringLiteral("联") : label.left(1);
}

/** 横幅 / 徽记的同色系渐变：与大厅瀑布流卡同一套色板 */
void roomPalette(const ForumLobbyRoom &room, QColor &c1, QColor &c2)
{
    MasonryContentCard::paletteForSeed(
        QStringLiteral("lobby:%1:%2").arg(room.id).arg(room.core), c1, c2);
}

/** 房间状态文案：下架优先，其次满员 / 游戏中 / 等待加入 */
QString roomStatusText(const ForumLobbyRoom &room)
{
    if (!room.isOnline())
        return QObject::tr("已下架");
    if (room.status == QLatin1String("full"))
        return QObject::tr("已满");
    if (room.status == QLatin1String("playing"))
        return QObject::tr("游戏中");
    return QObject::tr("等待加入");
}

/** 状态胶囊的样式属性值 */
QString roomStatusKey(const ForumLobbyRoom &room)
{
    if (!room.isOnline())
        return QStringLiteral("offline");
    if (room.status == QLatin1String("full"))
        return QStringLiteral("full");
    if (room.status == QLatin1String("playing"))
        return QStringLiteral("playing");
    return QStringLiteral("waiting");
}

QString expiresText(int seconds)
{
    if (seconds <= 0)
        return QObject::tr("已过期，等房主重新发布");
    if (seconds < 60)
        return QObject::tr("不到 1 分钟后过期");
    if (seconds < 3600)
        return QObject::tr("%1 分钟后过期").arg(seconds / 60);
    return QObject::tr("%1 小时 %2 分钟后过期").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

/**
 * @brief 给房间挑本地实例，返回一句能直接显示的话
 * @param missing 输出：挑中实例缺少的必装模组文件名
 * @param matched 输出：本机有没有可用实例
 */
QString matchHeadline(const ForumLobbyRoom &room, QStringList *missing, bool *matched)
{
    const LobbyInstanceMatcher::Requirement requirement = LobbyInstanceMatcher::requirementFor(
        room.gameVersion, room.loader, room.requiredMods(), room.auxMods, room.resourcePacks,
        room.shaderPacks);
    const LobbyInstanceMatcher::JoinPlan plan = LobbyInstanceMatcher::makePlan(requirement);
    if (missing)
        *missing = plan.best.missingMods;
    if (matched)
        *matched = plan.found;

    if (!plan.found)
    {
        if (!plan.installHint.trimmed().isEmpty())
            return plan.installHint;
        return room.gameVersion.isEmpty()
                   ? QObject::tr("任意版本都能加入，会自动挑一个合适的实例。")
                   : QObject::tr("本机没有 %1 的实例，需要先在「资源」里装一个。").arg(room.gameVersion);
    }

    const QString name = plan.best.instance.id;
    if (plan.best.level == LobbyInstanceMatcher::MatchLevel::Mods)
        return QObject::tr("将用「%1」加入 · 必装模组都齐了。").arg(name);
    if (plan.best.level == LobbyInstanceMatcher::MatchLevel::Loader)
        return QObject::tr("将用「%1」加入 · 还缺 %2 个必装模组，加入时会问要不要下载。")
            .arg(name)
            .arg(plan.best.missingMods.size());
    return QObject::tr("将用「%1」加入 · 只有版本匹配，进房前最好补一下模组。").arg(name);
}

/** 详情页的小标题卡片：返回卡片，layoutOut 拿到卡片里的竖向布局 */
QFrame *makeSectionCard(QWidget *parent, const QString &title, QVBoxLayout **layoutOut)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("mlDetailCard"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);

    auto *accent = new QFrame(card);
    accent->setObjectName(QStringLiteral("mlDetailAccent"));
    accent->setFixedSize(3, 14);
    titleRow->addWidget(accent, 0, Qt::AlignVCenter);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("mlDetailCardTitle"));
    titleRow->addWidget(titleLabel, 1);
    layout->addLayout(titleRow);

    if (layoutOut)
        *layoutOut = layout;
    return card;
}

/** 房间信息里的一行「键 —— 值」 */
QWidget *makeInfoRow(QWidget *parent, const QString &key, QLabel **valueOut)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(10);

    auto *keyLabel = new QLabel(key, row);
    keyLabel->setObjectName(QStringLiteral("mlDetailKey"));
    keyLabel->setMinimumWidth(70);
    keyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(keyLabel, 0);

    auto *value = new QLabel(row);
    value->setObjectName(QStringLiteral("mlDetailValue"));
    value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(value, 1);

    if (valueOut)
        *valueOut = value;
    return row;
}

/** 信息行之间的细分隔线 */
QFrame *makeInfoDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("mlDetailDivider"));
    line->setFixedHeight(1);
    return line;
}

/** 正在游玩的一位玩家：头像 + 名字 + 「房主 / 你」的小标签 */
QFrame *makePlayerChip(QWidget *parent, const ForumLobbyPlayer &player, bool isSelf)
{
    auto *chip = new QFrame(parent);
    chip->setObjectName(QStringLiteral("mlDetailPlayerChip"));

    auto *layout = new QHBoxLayout(chip);
    layout->setContentsMargins(8, 4, 10, 4);
    layout->setSpacing(7);

    const QString name = player.display();
    QLabel *avatar = ForumKit::makeAvatar(chip, player.avatar, name, 22);
    layout->addWidget(avatar, 0, Qt::AlignVCenter);

    auto *nameLabel = new QLabel(elide(name, 14), chip);
    nameLabel->setObjectName(QStringLiteral("mlDetailPlayerName"));
    nameLabel->setToolTip(name);
    layout->addWidget(nameLabel, 0, Qt::AlignVCenter);

    auto addBadge = [chip, layout](const QString &text, const QString &kind) {
        auto *badge = new QLabel(text, chip);
        badge->setObjectName(QStringLiteral("mlDetailPlayerBadge"));
        badge->setProperty("kind", kind);
        layout->addWidget(badge, 0, Qt::AlignVCenter);
    };
    if (player.isHost)
        addBadge(QObject::tr("房主"), QStringLiteral("host"));
    if (isSelf)
        addBadge(QObject::tr("你"), QStringLiteral("self"));
    return chip;
}

/** 正在游玩的人变了才需要重画（定时刷新时避免整页重排） */
QString playersSignature(const ForumLobbyRoom &room)
{
    QStringList parts;
    for (const ForumLobbyPlayer &player : room.players)
        parts << QStringLiteral("%1@%2").arg(player.id).arg(player.joinedAt);
    return parts.join(QLatin1Char(','));
}

/**
 * @brief 一条房间评论：头像 + 名字（房主 / 你 标签）+ 时间 + 正文 + 删除
 * @param onDelete 非空时右侧显示「删除」（自己发的、或房主清理自己房间里的评论）
 */
QFrame *makeCommentRow(QWidget *parent, const ForumLobbyComment &comment,
                       const std::function<void()> &onDelete)
{
    auto *row = new QFrame(parent);
    row->setObjectName(QStringLiteral("mlDetailCommentRow"));

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 9, 10, 10);
    layout->setSpacing(9);

    const QString name = comment.display();
    QLabel *avatar = ForumKit::makeAvatar(row, comment.avatar, name, 28);
    layout->addWidget(avatar, 0, Qt::AlignTop);

    auto *body = new QVBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);

    auto *head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(6);

    auto *nameLabel = new QLabel(name, row);
    nameLabel->setObjectName(QStringLiteral("mlDetailCommentName"));
    nameLabel->setToolTip(name);
    head->addWidget(nameLabel, 0);

    auto addBadge = [row, head](const QString &text, const QString &kind) {
        auto *badge = new QLabel(text, row);
        badge->setObjectName(QStringLiteral("mlDetailCommentBadge"));
        badge->setProperty("kind", kind);
        head->addWidget(badge, 0);
    };
    if (comment.isHost)
        addBadge(QObject::tr("房主"), QStringLiteral("host"));
    if (comment.mine)
        addBadge(QObject::tr("你"), QStringLiteral("self"));

    auto *timeLabel = new QLabel(ForumKit::relativeTime(comment.createdAt), row);
    timeLabel->setObjectName(QStringLiteral("mlDetailCommentTime"));
    head->addWidget(timeLabel, 0);
    head->addStretch(1);

    if (onDelete)
    {
        auto *del = new QPushButton(QObject::tr("删除"), row);
        del->setObjectName(QStringLiteral("mlDetailCommentDelete"));
        del->setCursor(Qt::PointingHandCursor);
        del->setFixedHeight(22);
        QObject::connect(del, &QPushButton::clicked, row, [onDelete]() { onDelete(); });
        head->addWidget(del, 0);
    }
    body->addLayout(head);

    auto *content = new QLabel(comment.content, row);
    content->setObjectName(QStringLiteral("mlDetailCommentBody"));
    // 评论是纯文本：粘贴进来的 <b> 之类按原样显示，不当 HTML 解析
    content->setTextFormat(Qt::PlainText);
    content->setWordWrap(true);
    content->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(content);

    layout->addLayout(body, 1);
    return row;
}

/** 清空 FlowLayout / QBoxLayout 里现有的控件（立即摘掉，下一帧删除） */
void clearLayoutWidgets(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

QString signatureFor(const ForumLobbyRoom &room)
{
    return QStringList{
               QString::number(room.id),
               room.title,
               room.description,
               room.md,
               room.core,
               room.coreLabel,
               room.inviteCode,
               room.address,
               room.gameVersion,
               room.loader,
               room.mods.join(QLatin1Char('\n')),
               room.auxMods.join(QLatin1Char('\n')),
               room.resourcePacks.join(QLatin1Char('\n')),
               room.shaderPacks.join(QLatin1Char('\n')),
               room.cover,
               room.modpackMode,
               room.modpackName,
               room.modpackUrl,
               room.modpackMeta,
               QString::number(room.maxPlayers),
               room.status,
               QString::number(room.joinCount),
               room.host.username,
               room.host.displayName,
               room.host.avatar,
               QString::number(room.createdAt),
               QString::number(room.expiresIn / 60),
               playersSignature(room),
               room.mine ? QStringLiteral("1") : QStringLiteral("0")}
        .join(QLatin1Char('|'));
}
} // namespace

/**
 * @brief 详情页顶部横幅：封面 / 主题渐变 + 底部压暗 + 大号房徽 + 内容叠层
 *
 * 没有封面时不再是一整块纯色：用房间专属色板画斜向渐变、光晕和半透明大徽记，
 * 底部再压一层暗色渐变，保证叠在上面的白色标题、房主和标签都看得清。
 * 内容通过 contentLayout() 交给详情页塞进来，高度由详情页按宽 / 窄屏指定。
 */
class LobbyHeroBanner : public QWidget
{
public:
    explicit LobbyHeroBanner(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("mlDetailHero"));
        setAttribute(Qt::WA_StyledBackground, false);
        setCursor(Qt::ArrowCursor);

        m_contentLayout = new QVBoxLayout(this);
        m_contentLayout->setContentsMargins(26, 22, 26, 20);
        m_contentLayout->setSpacing(8);
        // 内容整体压在底部，上方留出封面 / 渐变的空间
        m_contentLayout->addStretch(1);
    }

    QVBoxLayout *contentLayout() const { return m_contentLayout; }

    /** 主题切换时只重算渐变配色（封面 / 文字不用动） */
    void refreshPalette(const ForumLobbyRoom &room)
    {
        if (m_roomId == 0)
            return;
        roomPalette(room, m_colorFrom, m_colorTo);
        update();
    }

    void setRoom(const ForumLobbyRoom &room)
    {
        const bool same = m_roomId == room.id && m_coverPath == room.cover
                          && m_title == room.title && m_core == room.core;
        if (same)
            return;

        m_roomId = room.id;
        m_coverPath = room.cover;
        m_title = room.title;
        m_core = room.core;
        m_glyph = coreGlyph(room);
        roomPalette(room, m_colorFrom, m_colorTo);
        m_image = QImage();
        update();

        const QString path = room.cover.trimmed();
        if (path.isEmpty())
            return;
        const QString url = ForumAccountService::instance()->absoluteUrl(path);
        if (url.isEmpty())
            return;

        QPointer<LobbyHeroBanner> guard(this);
        ForumImageStore::instance()->requestImage(url, [guard](const QImage &image) {
            if (guard.isNull() || image.isNull())
                return;
            guard->m_image = image;
            guard->update();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (width() <= 2 || height() <= 2)
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr qreal radius = 16.0;

        QPainterPath clip;
        clip.addRoundedRect(box, radius, radius);
        painter.setClipPath(clip);

        if (!m_image.isNull())
        {
            const QImage scaled = m_image.scaled(box.size().toSize(),
                                                 Qt::KeepAspectRatioByExpanding,
                                                 Qt::SmoothTransformation);
            const int offsetX = qMax(0, (scaled.width() - int(box.width())) / 2);
            const int offsetY = qMax(0, (scaled.height() - int(box.height())) / 2);
            painter.drawImage(box, scaled, QRect(offsetX, offsetY, int(box.width()),
                                                 int(box.height())));
        }
        else
        {
            // 房间专属色板：斜向渐变 + 左上光晕 + 右侧大徽记 + 淡点缀
            QLinearGradient gradient(box.topLeft(), box.bottomRight());
            gradient.setColorAt(0.0, m_colorFrom);
            gradient.setColorAt(0.55, m_colorTo);
            gradient.setColorAt(1.0, m_colorTo.darker(118));
            painter.fillRect(box, gradient);

            QRadialGradient glow(QPointF(box.left() + box.width() * 0.24,
                                         box.top() + box.height() * 0.26),
                                 box.width() * 0.72);
            glow.setColorAt(0.0, QColor(255, 255, 255, 42));
            glow.setColorAt(1.0, QColor(255, 255, 255, 0));
            painter.fillRect(box, glow);

            // 右侧半透明大徽记（陶 / 红 / 穿…），给纯色封面一点识别度
            QFont glyphFont = font();
            glyphFont.setBold(true);
            glyphFont.setPixelSize(qMax(48, qRound(height() * 0.58)));
            painter.setFont(glyphFont);
            painter.setPen(QColor(255, 255, 255, 34));
            painter.drawText(box.adjusted(0, 0, -40, 0),
                             Qt::AlignRight | Qt::AlignVCenter, m_glyph);

            // 细点阵装饰
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 16));
            const int step = 30;
            for (int y = 18; y < height() - 18; y += step)
            {
                for (int x = 20 + ((y / step) % 2) * (step / 2); x < width() * 0.65; x += step)
                    painter.drawEllipse(QPointF(x, y), 1.4, 1.4);
            }
        }

        // 底部压暗：白色标题 / 房主 / 标签叠在上面时的可读性保障
        QLinearGradient scrim(box.topLeft(), QPointF(box.left(), box.bottom()));
        scrim.setColorAt(0.30, QColor(0, 0, 0, 0));
        scrim.setColorAt(0.72, QColor(0, 0, 0, 95));
        scrim.setColorAt(1.0, QColor(0, 0, 0, 172));
        painter.fillRect(box, scrim);

        painter.setClipping(false);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(ThemeManager::instance()->currentBorderColor()), 1));
        painter.drawRoundedRect(box, radius, radius);
    }

private:
    QVBoxLayout *m_contentLayout = nullptr;
    QImage m_image;
    QString m_coverPath;
    QString m_title;
    QString m_core;
    QString m_glyph;
    QColor m_colorFrom;
    QColor m_colorTo;
    int m_roomId = 0;
};

MultiplayerLobbyDetailPage::MultiplayerLobbyDetailPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("multiplayerLobbyDetail"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &MultiplayerLobbyDetailPage::applyStyle);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](ThemeManager::ThemeType) { applyStyle(); });

    // 登录 / 换账号 / 答题通过后，评论输入区和「删除」按钮都要跟着变
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            [this](const ForumUser &) {
                m_commentComposerState.clear();
                updateCommentComposer();
                if (m_room.id > 0 && isVisible() && !m_commentsLoading)
                    requestComments();
            });

    // 评论跟着大厅一起每 30 秒安静地刷新一次（只在详情页开着、房间还在时）
    m_commentsTimer = new QTimer(this);
    m_commentsTimer->setInterval(30000);
    connect(m_commentsTimer, &QTimer::timeout, this, [this]() {
        if (isVisible() && m_room.id > 0 && m_room.isOnline() && !m_commentsLoading)
            requestComments();
    });
    m_commentsTimer->start();

    // 人数曲线走慢一点的时钟：定期补一次历史 + 上报一次当前人数。
    // 房间数据每 30 秒推一次只顺带触发上报（有节流），曲线不会跟着每 30 秒重排
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(90000);
    connect(m_statsTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible() || m_room.id <= 0)
            return;
        if (m_room.isOnline())
            reportStatsSample(false);
        requestStatsHistory(false);
    });
    m_statsTimer->start();

    applyStyle();
    updateContent();
}

MultiplayerLobbyDetailPage::~MultiplayerLobbyDetailPage() = default;

void MultiplayerLobbyDetailPage::buildUi()
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(24, 18, 24, 16);
    m_rootLayout->setSpacing(12);

    // ---- 顶部：返回大厅 + 面包屑 ----
    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(10);

    m_backButton = new QPushButton(tr("返回大厅"), this);
    m_backButton->setObjectName(QStringLiteral("mlDetailBack"));
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setFixedHeight(32);
    m_backButton->setMinimumWidth(104);
    connect(m_backButton, &QPushButton::clicked, this, &MultiplayerLobbyDetailPage::backRequested);
    headerRow->addWidget(m_backButton);

    auto *crumb = new QLabel(tr("联机大厅 · 房间详情"), this);
    crumb->setObjectName(QStringLiteral("mlDetailCrumb"));
    headerRow->addWidget(crumb);
    headerRow->addStretch(1);
    m_rootLayout->addLayout(headerRow);

    // ---- 滚动区 ----
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("mlDetailScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    TouchScroll::enableDragScroll(m_scroll);

    m_body = new QWidget(m_scroll);
    m_body->setObjectName(QStringLiteral("mlDetailBody"));
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(0, 0, 2, 8);
    m_bodyLayout->setSpacing(12);
    m_scroll->setWidget(m_body);
    m_rootLayout->addWidget(m_scroll, 1);

    // ---- 顶部横幅：封面 / 渐变 + 标题 + 房主 + 标签 ----
    m_heroBanner = new LobbyHeroBanner(m_body);
    m_heroBanner->setFixedHeight(300);
    QVBoxLayout *heroContent = m_heroBanner->contentLayout();

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);

    m_titleLabel = new QLabel(m_heroBanner);
    m_titleLabel->setObjectName(QStringLiteral("mlHeroTitle"));
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    {
        // 白色标题压在封面 / 渐变上，加一点投影更清楚
        auto *shadow = new QGraphicsDropShadowEffect(m_titleLabel);
        shadow->setBlurRadius(10);
        shadow->setOffset(0, 2);
        shadow->setColor(QColor(0, 0, 0, 150));
        m_titleLabel->setGraphicsEffect(shadow);
    }
    titleRow->addWidget(m_titleLabel, 1);

    m_mineChip = new QLabel(tr("我的房间"), m_heroBanner);
    m_mineChip->setObjectName(QStringLiteral("mlHeroMine"));
    titleRow->addWidget(m_mineChip, 0, Qt::AlignVCenter);

    m_statusChip = new QLabel(m_heroBanner);
    m_statusChip->setObjectName(QStringLiteral("mlHeroStatus"));
    titleRow->addWidget(m_statusChip, 0, Qt::AlignVCenter);
    heroContent->addLayout(titleRow);

    m_hostHost = new QWidget(m_heroBanner);
    auto *hostLayout = new QHBoxLayout(m_hostHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(8);
    heroContent->addWidget(m_hostHost);

    m_metaHost = new QWidget(m_heroBanner);
    new FlowLayout(m_metaHost, 0, 6, 6);
    heroContent->addWidget(m_metaHost);

    m_bodyLayout->addWidget(m_heroBanner);
    // ---- 左右两栏 ----
    m_columns = new QBoxLayout(QBoxLayout::LeftToRight);
    m_columns->setContentsMargins(0, 0, 0, 0);
    m_columns->setSpacing(12);

    m_leftColumn = new QVBoxLayout();
    m_leftColumn->setContentsMargins(0, 0, 0, 0);
    m_leftColumn->setSpacing(12);

    m_rightColumn = new QVBoxLayout();
    m_rightColumn->setContentsMargins(0, 0, 0, 0);
    m_rightColumn->setSpacing(12);

    m_columns->addLayout(m_leftColumn, 3);
    m_columns->addLayout(m_rightColumn, 2);
    m_bodyLayout->addLayout(m_columns);

    // ---- 左栏：房间介绍 / 模组 / 前置整合包 ----
    QVBoxLayout *introLayout = nullptr;
    m_introCard = makeSectionCard(m_body, tr("房间介绍"), &introLayout);

    m_descLabel = new QLabel(m_introCard);
    m_descLabel->setObjectName(QStringLiteral("mlDetailDesc"));
    m_descLabel->setWordWrap(true);
    introLayout->addWidget(m_descLabel);

    m_introBrowser = new ForumPostBrowser(m_introCard);
    m_introBrowser->setObjectName(QStringLiteral("mlDetailIntroBrowser"));
    // 紧凑模式：正文里的小标题不会大过卡片标题，段落间距也更贴合卡片
    m_introBrowser->setCompactMode(true);
    connect(m_introBrowser, &ForumPostBrowser::imageClicked, this,
            [this](const QString &path) { ForumPostBrowser::showImagePreview(path, this); });
    connect(m_introBrowser, &ForumPostBrowser::linkClicked, this, [this](const QUrl &url) {
        LinkRouter::instance()->open(url, this);
    });
    introLayout->addWidget(m_introBrowser);

    m_introHint = new QLabel(m_introCard);
    m_introHint->setObjectName(QStringLiteral("mlDetailMuted"));
    m_introHint->setWordWrap(true);
    introLayout->addWidget(m_introHint);

    m_leftColumn->addWidget(m_introCard);

    // ---- 左栏：房间评论（对着房间留言，房间下架后一起收起） ----
    QVBoxLayout *commentsLayout = nullptr;
    m_commentsCard = makeSectionCard(m_body, tr("房间评论"), &commentsLayout);
    m_commentsCard->hide();

    auto *commentsHead = new QHBoxLayout();
    commentsHead->setContentsMargins(0, 0, 0, 0);
    commentsHead->setSpacing(8);

    m_commentsHint = new QLabel(m_commentsCard);
    m_commentsHint->setObjectName(QStringLiteral("mlDetailMuted"));
    m_commentsHint->setWordWrap(true);
    commentsHead->addWidget(m_commentsHint, 1);

    m_commentsRefresh = new QPushButton(tr("刷新"), m_commentsCard);
    m_commentsRefresh->setObjectName(QStringLiteral("mlDetailCommentsRefresh"));
    m_commentsRefresh->setCursor(Qt::PointingHandCursor);
    m_commentsRefresh->setFixedHeight(26);
    m_commentsRefresh->setToolTip(tr("重新拉取最新评论"));
    connect(m_commentsRefresh, &QPushButton::clicked, this,
            &MultiplayerLobbyDetailPage::requestComments);
    commentsHead->addWidget(m_commentsRefresh, 0, Qt::AlignTop);
    commentsLayout->addLayout(commentsHead);

    m_commentsHost = new QWidget(m_commentsCard);
    auto *commentsHostLayout = new QVBoxLayout(m_commentsHost);
    commentsHostLayout->setContentsMargins(0, 0, 0, 0);
    commentsHostLayout->setSpacing(8);
    commentsLayout->addWidget(m_commentsHost);

    commentsLayout->addWidget(makeInfoDivider(m_commentsCard));

    // 输入区按登录 / 答题状态重建，放在固定的宿主里
    m_commentComposerHost = new QWidget(m_commentsCard);
    m_commentComposerLayout = new QVBoxLayout(m_commentComposerHost);
    m_commentComposerLayout->setContentsMargins(0, 0, 0, 0);
    m_commentComposerLayout->setSpacing(8);
    commentsLayout->addWidget(m_commentComposerHost);

    m_commentStatus = new QLabel(m_commentsCard);
    m_commentStatus->setObjectName(QStringLiteral("mlDetailHint"));
    m_commentStatus->setWordWrap(true);
    m_commentStatus->hide();
    commentsLayout->addWidget(m_commentStatus);

    m_leftColumn->addWidget(m_commentsCard);

    QVBoxLayout *modsLayout = nullptr;
    m_modsCard = makeSectionCard(m_body, tr("房主使用的模组"), &modsLayout);

    m_modsHint = new QLabel(m_modsCard);
    m_modsHint->setObjectName(QStringLiteral("mlDetailMuted"));
    m_modsHint->setWordWrap(true);
    modsLayout->addWidget(m_modsHint);

    m_modsHost = new QWidget(m_modsCard);
    new FlowLayout(m_modsHost, 0, 6, 6);
    modsLayout->addWidget(m_modsHost);

    // 辅助模组（带「辅助」标签）：加入时可勾选补装，不装也能进房
    m_auxHint = new QLabel(m_modsCard);
    m_auxHint->setObjectName(QStringLiteral("mlDetailMuted"));
    m_auxHint->setWordWrap(true);
    m_auxHint->hide();
    modsLayout->addWidget(m_auxHint);

    m_auxHost = new QWidget(m_modsCard);
    new FlowLayout(m_auxHost, 0, 6, 6);
    m_auxHost->hide();
    modsLayout->addWidget(m_auxHost);

    // 资源包 / 光影包：房主用的那套，加入时可以一起勾选安装
    m_extrasHint = new QLabel(m_modsCard);
    m_extrasHint->setObjectName(QStringLiteral("mlDetailMuted"));
    m_extrasHint->setWordWrap(true);
    m_extrasHint->hide();
    modsLayout->addWidget(m_extrasHint);

    m_extrasHost = new QWidget(m_modsCard);
    new FlowLayout(m_extrasHost, 0, 6, 6);
    m_extrasHost->hide();
    modsLayout->addWidget(m_extrasHost);

    m_leftColumn->addWidget(m_modsCard);

    QVBoxLayout *packLayout = nullptr;
    m_modpackCard = makeSectionCard(m_body, tr("前置整合包"), &packLayout);

    m_packNameLabel = new QLabel(m_modpackCard);
    m_packNameLabel->setObjectName(QStringLiteral("mlDetailPackName"));
    m_packNameLabel->setWordWrap(true);
    m_packNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    packLayout->addWidget(m_packNameLabel);

    m_packMetaLabel = new QLabel(m_modpackCard);
    m_packMetaLabel->setObjectName(QStringLiteral("mlDetailMuted"));
    m_packMetaLabel->setWordWrap(true);
    packLayout->addWidget(m_packMetaLabel);

    m_packHintLabel = new QLabel(m_modpackCard);
    m_packHintLabel->setObjectName(QStringLiteral("mlDetailMuted"));
    m_packHintLabel->setWordWrap(true);
    packLayout->addWidget(m_packHintLabel);

    m_packButton = new QPushButton(tr("打开下载地址"), m_modpackCard);
    m_packButton->setObjectName(QStringLiteral("mlDetailGhost"));
    m_packButton->setCursor(Qt::PointingHandCursor);
    m_packButton->setFixedHeight(32);
    connect(m_packButton, &QPushButton::clicked, this, [this]() {
        const QString url = m_room.modpackUrl.trimmed();
        if (!url.isEmpty())
            LinkRouter::instance()->open(QUrl(url, QUrl::TolerantMode), this);
    });
    packLayout->addWidget(m_packButton, 0, Qt::AlignLeft);
    m_leftColumn->addWidget(m_modpackCard);

    m_leftColumn->addStretch(1);

    // ---- 右栏：加入卡片 / 房间信息 ----
    QVBoxLayout *joinLayout = nullptr;
    m_joinCard = makeSectionCard(m_body, tr("加入这个房间"), &joinLayout);

    auto *codeBox = new QFrame(m_joinCard);
    codeBox->setObjectName(QStringLiteral("mlDetailCodeBox"));
    auto *codeLayout = new QHBoxLayout(codeBox);
    codeLayout->setContentsMargins(14, 10, 10, 12);
    codeLayout->setSpacing(8);

    auto *codeColumn = new QVBoxLayout();
    codeColumn->setContentsMargins(0, 0, 0, 0);
    codeColumn->setSpacing(4);

    m_codeCaption = new QLabel(codeBox);
    m_codeCaption->setObjectName(QStringLiteral("mlDetailCodeCaption"));
    codeColumn->addWidget(m_codeCaption);

    m_codeLabel = new QLabel(codeBox);
    m_codeLabel->setObjectName(QStringLiteral("mlDetailCode"));
    m_codeLabel->setWordWrap(true);
    m_codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeColumn->addWidget(m_codeLabel);
    codeLayout->addLayout(codeColumn, 1);

    // 复制做成码框里的图标按钮：加入按钮才是这张卡唯一的大按钮
    m_copyButton = new QPushButton(codeBox);
    m_copyButton->setObjectName(QStringLiteral("mlDetailCopyIcon"));
    m_copyButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->setFixedSize(32, 32);
    m_copyButton->setIconSize(QSize(16, 16));
    m_copyButton->setToolTip(tr("复制邀请码 / 连接地址"));
    connect(m_copyButton, &QPushButton::clicked, this, &MultiplayerLobbyDetailPage::copyTarget);
    codeLayout->addWidget(m_copyButton, 0, Qt::AlignVCenter);
    joinLayout->addWidget(codeBox);

    m_joinButton = new QPushButton(tr("一键加入"), m_joinCard);
    m_joinButton->setObjectName(QStringLiteral("mlDetailPrimary"));
    m_joinButton->setCursor(Qt::PointingHandCursor);
    m_joinButton->setFixedHeight(40);
    connect(m_joinButton, &QPushButton::clicked, this, [this]() {
        if (m_room.isOnline())
            emit joinRequested(m_room);
    });

    // 分享做成「一键加入」右边的小按钮：复制一段带邀请码的文案，或直接发论坛私信
    m_shareButton = new QPushButton(tr("分享"), m_joinCard);
    m_shareButton->setObjectName(QStringLiteral("mlDetailShare"));
    m_shareButton->setCursor(Qt::PointingHandCursor);
    m_shareButton->setFixedHeight(40);
    m_shareButton->setMinimumWidth(88);
    m_shareButton->setToolTip(tr("把这个房间分享给朋友"));
    connect(m_shareButton, &QPushButton::clicked, this, &MultiplayerLobbyDetailPage::shareRoom);

    auto *joinRow = new QHBoxLayout();
    joinRow->setContentsMargins(0, 0, 0, 0);
    joinRow->setSpacing(8);
    joinRow->addWidget(m_joinButton, 1);
    joinRow->addWidget(m_shareButton, 0);
    joinLayout->addLayout(joinRow);

    auto *matchBox = new QFrame(m_joinCard);
    matchBox->setObjectName(QStringLiteral("mlDetailMatchBox"));
    auto *matchLayout = new QHBoxLayout(matchBox);
    matchLayout->setContentsMargins(10, 8, 10, 8);
    matchLayout->setSpacing(6);

    m_matchLabel = new QLabel(matchBox);
    m_matchLabel->setObjectName(QStringLiteral("mlDetailMatch"));
    m_matchLabel->setWordWrap(true);
    matchLayout->addWidget(m_matchLabel, 1);
    joinLayout->addWidget(matchBox);

    m_actionHint = new QLabel(m_joinCard);
    m_actionHint->setObjectName(QStringLiteral("mlDetailHint"));
    m_actionHint->setWordWrap(true);
    m_actionHint->hide();
    joinLayout->addWidget(m_actionHint);

    auto *joinNote = new QLabel(
        tr("加入时会自动挑最合适的本地实例；缺模组、资源包或光影包时，会先问你要不要一起下载，"
           "也可以在列表里逐项勾选。"),
        m_joinCard);
    joinNote->setObjectName(QStringLiteral("mlDetailMuted"));
    joinNote->setWordWrap(true);
    joinLayout->addWidget(joinNote);

    m_rightColumn->addWidget(m_joinCard);

    // ---- 右栏：正在游玩的人（房主 + 已进游戏的玩家） ----
    QVBoxLayout *playersLayout = nullptr;
    m_playersCard = makeSectionCard(m_body, tr("正在游玩"), &playersLayout);

    m_playersHint = new QLabel(m_playersCard);
    m_playersHint->setObjectName(QStringLiteral("mlDetailMuted"));
    m_playersHint->setWordWrap(true);
    playersLayout->addWidget(m_playersHint);

    m_playersHost = new QWidget(m_playersCard);
    new FlowLayout(m_playersHost, 0, 6, 6);
    playersLayout->addWidget(m_playersHost);

    m_rightColumn->addWidget(m_playersCard);

    // ---- 右栏：人数统计（房间里在线人数的折线图，数据和所有打开过详情页的人共用） ----
    QVBoxLayout *statsLayout = nullptr;
    m_statsCard = makeSectionCard(m_body, tr("人数统计"), &statsLayout);

    m_statsValue = new QLabel(m_statsCard);
    m_statsValue->setObjectName(QStringLiteral("mlDetailValue"));
    statsLayout->addWidget(m_statsValue);

    m_statsHint = new QLabel(m_statsCard);
    m_statsHint->setObjectName(QStringLiteral("mlDetailMuted"));
    m_statsHint->setWordWrap(true);
    statsLayout->addWidget(m_statsHint);

    m_statsChart = new PlayerCountChart(m_statsCard);
    m_statsChart->setMinimumHeight(150);
    m_statsChart->setEmptyText(tr("还没有人数记录，过几分钟再来看看"));
    statsLayout->addWidget(m_statsChart);

    m_rightColumn->addWidget(m_statsCard);

    QVBoxLayout *infoLayout = nullptr;
    m_infoCard = makeSectionCard(m_body, tr("房间信息"), &infoLayout);
    auto addInfoRow = [&](const QString &key, QLabel **valueOut) {
        if (infoLayout->count() > 1)
            infoLayout->addWidget(makeInfoDivider(m_infoCard));
        infoLayout->addWidget(makeInfoRow(m_infoCard, key, valueOut));
    };
    addInfoRow(tr("游戏版本"), &m_versionValue);
    addInfoRow(tr("加载器"), &m_loaderValue);
    addInfoRow(tr("联机核心"), &m_coreValue);
    addInfoRow(tr("房间状态"), &m_statusValue);
    addInfoRow(tr("人数上限"), &m_playersValue);
    addInfoRow(tr("已加入"), &m_joinCountValue);
    addInfoRow(tr("发布时间"), &m_publishedValue);
    addInfoRow(tr("有效期"), &m_expireValue);
    m_rightColumn->addWidget(m_infoCard);
    m_rightColumn->addStretch(1);

    updateResponsiveLayout();
}

void MultiplayerLobbyDetailPage::applyStyle()
{
    if (m_backButton)
    {
        m_backButton->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/back.svg"),
            QColor(ThemeManager::instance()->currentThemeColor()), 14));
        m_backButton->setIconSize(QSize(14, 14));
    }

    setStyleSheet(StyleKit::resolve(QStringLiteral(
        /* 页面与滚动区：不刷底色，跟随大厅层透出软件背景 */
        "QWidget#multiplayerLobbyDetail { background: transparent; }"
        "QScrollArea#mlDetailScroll, QWidget#mlDetailBody {"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlDetailCrumb {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"

        /* 返回按钮 */
        "QPushButton#mlDetailBack {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_SECONDARY@; font-size: 13px; font-weight: 600; padding: 0 12px;"
        "}"
        "QPushButton#mlDetailBack:hover {"
        "  color: @TEXT_PRIMARY@; border-color: @PRIMARY@; background: @BG_HOVER@;"
        "}"
        "QPushButton#mlDetailBack:pressed { background: @BG_PRESSED@; }"

        /* 顶部横幅：内容叠在封面 / 渐变上，统一白字 */
        "QWidget#mlDetailHero { background: transparent; border: none; }"
        "QLabel#mlHeroTitle {"
        "  color: #ffffff; font-size: 25px; font-weight: 800;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlHeroMine {"
        "  color: #ffffff; background: rgba(255, 255, 255, 0.18);"
        "  border: 1px solid rgba(255, 255, 255, 0.40); border-radius: 9px;"
        "  padding: 2px 9px; font-size: 11px; font-weight: 700;"
        "}"
        "QLabel#mlHeroStatus {"
        "  color: #ffffff; background: rgba(0, 0, 0, 0.30);"
        "  border: 1px solid rgba(255, 255, 255, 0.30); border-radius: 9px;"
        "  padding: 2px 10px; font-size: 11px; font-weight: 700;"
        "}"
        "QLabel#mlHeroStatus[roomState=\"waiting\"] {"
        "  background: rgba(16, 185, 129, 0.44); border-color: rgba(255, 255, 255, 0.40);"
        "}"
        "QLabel#mlHeroStatus[roomState=\"playing\"] {"
        "  background: rgba(245, 158, 11, 0.48); border-color: rgba(255, 255, 255, 0.40);"
        "}"
        "QLabel#mlHeroStatus[roomState=\"full\"],"
        "QLabel#mlHeroStatus[roomState=\"offline\"] {"
        "  background: rgba(0, 0, 0, 0.40); border-color: rgba(255, 255, 255, 0.26);"
        "}"
        "QLabel#mlHeroHost {"
        "  color: rgba(255, 255, 255, 0.96); font-size: 13px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlHeroTime {"
        "  color: rgba(255, 255, 255, 0.72); font-size: 12px;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlHeroTag {"
        "  color: rgba(255, 255, 255, 0.96); background: rgba(255, 255, 255, 0.16);"
        "  border: 1px solid rgba(255, 255, 255, 0.28); border-radius: 9px;"
        "  padding: 2px 9px; font-size: 11px; font-weight: 600;"
        "}"

        /* 通用卡片 */
        "QFrame#mlDetailCard {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 14px;"
        "}"
        "QFrame#mlDetailAccent {"
        "  background: @PRIMARY@; border: none; border-radius: 2px;"
        "}"
        "QFrame#mlDetailDivider {"
        "  background: @BORDER_LIGHT@; border: none;"
        "}"
        "QLabel#mlDetailCardTitle {"
        "  color: @TEXT_PRIMARY@; font-size: 14px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlDetailDesc {"
        "  color: @TEXT_SECONDARY@; font-size: 13px; background: transparent; border: none;"
        "}"
        "QLabel#mlDetailMuted {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#mlDetailPackName {"
        "  color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlDetailKey {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#mlDetailValue {"
        "  color: @TEXT_PRIMARY@; font-size: 12px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QTextBrowser#mlDetailIntroBrowser {"
        "  background: transparent; border: none; padding: 0;"
        "  color: @TEXT_SECONDARY@; font-size: 13px;"
        "}"

        /* 模组胶囊：缺的用暖色提示 */
        "QLabel#mlDetailModChip {"
        "  background: @PRIMARY_BG@; color: @TEXT_ACCENT@;"
        "  border: 1px solid transparent; border-radius: 9px;"
        "  padding: 3px 10px; font-size: 11px; font-weight: 600;"
        "}"
        "QLabel#mlDetailModChip[missing=\"true\"] {"
        "  background: @WARNING_BG@; color: @WARNING_TEXT@;"
        "}"

        /* 正在游玩：一位玩家一枚圆角胶囊 */
        "QFrame#mlDetailPlayerChip {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER_LIGHT@; border-radius: 13px;"
        "}"
        "QLabel#mlDetailPlayerName {"
        "  color: @TEXT_PRIMARY@; font-size: 12px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlDetailPlayerBadge {"
        "  color: @TEXT_ACCENT@; background: @PRIMARY_BG@; border-radius: 7px;"
        "  padding: 0 6px; font-size: 10px; font-weight: 700;"
        "}"
        "QLabel#mlDetailPlayerBadge[kind=\"self\"] {"
        "  color: #ffffff; background: @PRIMARY@;"
        "}"

        /* 加入卡片 */
        "QFrame#mlDetailCodeBox {"
        "  background: @BG_MUTED@; border: 1px dashed @BORDER_DASHED@; border-radius: 12px;"
        "}"
        "QLabel#mlDetailCodeCaption {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlDetailCode {"
        "  color: @TEXT_PRIMARY@;"
        "  font-family: Consolas, \"Cascadia Mono\", \"Microsoft YaHei\";"
        "  font-size: 19px; font-weight: 800; letter-spacing: 1px;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlDetailCode[empty=\"true\"] {"
        "  color: @TEXT_TERTIARY@; font-size: 13px; font-weight: 500; letter-spacing: 0;"
        "}"
        "QPushButton#mlDetailPrimary {"
        "  background: @PRIMARY@; color: @TEXT_ON_PRIMARY@; border: none; border-radius: 10px;"
        "  padding: 0 18px; font-size: 14px; font-weight: 700;"
        "}"
        "QPushButton#mlDetailPrimary:hover { background: @PRIMARY_HOVER@; }"
        "QPushButton#mlDetailPrimary:pressed { background: @PRIMARY_PRESSED@; }"
        "QPushButton#mlDetailPrimary:disabled {"
        "  background: @BG_DISABLED@; color: @TEXT_DISABLED@;"
        "}"
        "QPushButton#mlDetailShare {"
        "  background: transparent; border: 1px solid @PRIMARY_RGBA_38@; border-radius: 10px;"
        "  color: @TEXT_ACCENT_DARK@; padding: 0 14px; font-size: 13px; font-weight: 700;"
        "}"
        "QPushButton#mlDetailShare:hover {"
        "  background: @PRIMARY_RGBA_10@; border-color: @PRIMARY@;"
        "}"
        "QPushButton#mlDetailShare:pressed { background: @PRIMARY_RGBA_20@; }"
        "QPushButton#mlDetailCopyIcon {"
        "  background: transparent; border: none; border-radius: 8px;"
        "}"
        "QPushButton#mlDetailCopyIcon:hover { background: @BG_HOVER@; }"
        "QPushButton#mlDetailCopyIcon:pressed { background: @BG_PRESSED@; }"
        "QPushButton#mlDetailCopyIcon:disabled { background: transparent; }"
        "QFrame#mlDetailMatchBox {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER_LIGHT@; border-radius: 10px;"
        "}"
        "QPushButton#mlDetailGhost {"
        "  background: transparent; border: 1px solid @PRIMARY_RGBA_38@; border-radius: 9px;"
        "  color: @TEXT_ACCENT@; padding: 0 14px; font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton#mlDetailGhost:hover {"
        "  background: @PRIMARY_RGBA_10@; border-color: @PRIMARY@;"
        "}"
        "QPushButton#mlDetailGhost:pressed { background: @PRIMARY_RGBA_20@; }"
        "QLabel#mlDetailMatch {"
        "  color: @TEXT_SECONDARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#mlDetailHint {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QLabel#mlDetailHint[error=\"true\"] { color: @DANGER_TEXT@; }"

        /* 房间评论 */
        "QFrame#mlDetailCommentRow {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER_LIGHT@; border-radius: 12px;"
        "}"
        "QLabel#mlDetailCommentName {"
        "  color: @TEXT_PRIMARY@; font-size: 12px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlDetailCommentBadge {"
        "  color: @TEXT_ACCENT@; background: @PRIMARY_BG@; border-radius: 7px;"
        "  padding: 0 6px; font-size: 10px; font-weight: 700;"
        "}"
        "QLabel#mlDetailCommentBadge[kind=\"self\"] { color: #ffffff; background: @PRIMARY@; }"
        "QLabel#mlDetailCommentTime {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QLabel#mlDetailCommentBody {"
        "  color: @TEXT_SECONDARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QPushButton#mlDetailCommentDelete {"
        "  background: transparent; border: none; border-radius: 6px;"
        "  color: @TEXT_TERTIARY@; font-size: 11px; padding: 0 6px;"
        "}"
        "QPushButton#mlDetailCommentDelete:hover {"
        "  color: @DANGER_TEXT@; background: @DANGER_BG@;"
        "}"
        "QPushButton#mlDetailCommentsRefresh {"
        "  background: transparent; border: 1px solid @BORDER_LIGHT@; border-radius: 8px;"
        "  color: @TEXT_TERTIARY@; font-size: 11px; padding: 0 10px;"
        "}"
        "QPushButton#mlDetailCommentsRefresh:hover {"
        "  color: @TEXT_PRIMARY@; border-color: @PRIMARY@; background: @BG_HOVER@;"
        "}"
        "QPushButton#mlDetailCommentsRefresh:disabled {"
        "  color: @TEXT_DISABLED@; border-color: @BORDER_DISABLED@;"
        "}"
        "QPlainTextEdit#mlDetailCommentEdit {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 10px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 8px 10px;"
        "}"
        "QPlainTextEdit#mlDetailCommentEdit:focus { border-color: @PRIMARY@; }"
        "QLabel#mlDetailCommentCount {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QLabel#mlDetailCommentCount[over=\"true\"] { color: @DANGER_TEXT@; }"
        "QPushButton#mlDetailCommentSend {"
        "  background: @PRIMARY@; color: @TEXT_ON_PRIMARY@; border: none; border-radius: 9px;"
        "  padding: 0 16px; font-size: 12px; font-weight: 700;"
        "}"
        "QPushButton#mlDetailCommentSend:hover { background: @PRIMARY_HOVER@; }"
        "QPushButton#mlDetailCommentSend:pressed { background: @PRIMARY_PRESSED@; }"
        "QPushButton#mlDetailCommentSend:disabled {"
        "  background: @BG_DISABLED@; color: @TEXT_DISABLED@;"
        "}"
        "QLabel#mlDetailCommentIdentity {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
    )));

    if (m_copyButton)
    {
        m_copyButton->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"),
            QColor(ThemeManager::instance()->currentThemeColor()), 16));
    }
    if (m_shareButton)
    {
        m_shareButton->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/share.svg"),
            QColor(StyleKit::resolve(QStringLiteral("@TEXT_ACCENT_DARK@"))), 15));
        m_shareButton->setIconSize(QSize(15, 15));
    }

    // 主题切换后动态属性驱动的胶囊 / 状态色要重新 polish 才会换色
    if (m_statusChip)
    {
        m_statusChip->style()->unpolish(m_statusChip);
        m_statusChip->style()->polish(m_statusChip);
    }
    if (m_codeLabel)
    {
        m_codeLabel->style()->unpolish(m_codeLabel);
        m_codeLabel->style()->polish(m_codeLabel);
    }
    if (m_actionHint)
    {
        m_actionHint->style()->unpolish(m_actionHint);
        m_actionHint->style()->polish(m_actionHint);
    }
    if (m_commentStatus)
    {
        m_commentStatus->style()->unpolish(m_commentStatus);
        m_commentStatus->style()->polish(m_commentStatus);
    }
    if (m_heroBanner)
    {
        m_heroBanner->refreshPalette(m_room);
        m_heroBanner->update();
    }
}

void MultiplayerLobbyDetailPage::setRoom(const ForumLobbyRoom &room)
{
    const bool roomChanged = (room.id != m_room.id);
    const QString signature = signatureFor(room);
    m_room = room;
    // 房间数据到了就顺手维护人数统计卡（换房间清空重拉；同一房间变化时上报一次，有节流）
    updateStatsRoom(roomChanged);
    if (signature == m_contentSignature)
        return;
    m_contentSignature = signature;
    updateContent();
}

void MultiplayerLobbyDetailPage::setActionHint(const QString &text, bool error)
{
    if (!m_actionHint)
        return;
    m_actionHint->setText(text);
    m_actionHint->setVisible(!text.isEmpty());
    if (m_actionHint->property("error").toBool() != error)
    {
        m_actionHint->setProperty("error", error);
        m_actionHint->style()->unpolish(m_actionHint);
        m_actionHint->style()->polish(m_actionHint);
    }
}

QString MultiplayerLobbyDetailPage::targetText() const
{
    return m_room.joinTarget().trimmed();
}

void MultiplayerLobbyDetailPage::scrollToTop()
{
    if (m_scroll && m_scroll->verticalScrollBar())
        m_scroll->verticalScrollBar()->setValue(0);
}

void MultiplayerLobbyDetailPage::updateContent()
{
    if (m_titleLabel)
        m_titleLabel->setText(roomTitle(m_room));
    if (m_mineChip)
        m_mineChip->setVisible(m_room.mine);
    if (m_heroBanner)
        m_heroBanner->setRoom(m_room);

    updateStatusChip();
    updateHostRow();
    updateMetaChips();
    updateIntro();
    updateMatchText();
    updateMods();
    updateModpack();
    updatePlayers();
    updateInfoRows();
    updateComments();

    // 加入卡片
    const QString target = targetText();
    if (m_codeCaption)
        m_codeCaption->setText(m_room.isTerracotta() ? tr("陶瓦邀请码") : tr("连接地址"));
    if (m_codeLabel)
    {
        m_codeLabel->setText(target.isEmpty() ? tr("房主没有填写邀请码 / 地址") : target);
        const bool empty = target.isEmpty();
        if (m_codeLabel->property("empty").toBool() != empty)
        {
            m_codeLabel->setProperty("empty", empty);
            m_codeLabel->style()->unpolish(m_codeLabel);
            m_codeLabel->style()->polish(m_codeLabel);
        }
    }
    if (m_copyButton)
        m_copyButton->setEnabled(!target.isEmpty());
    if (m_joinButton)
    {
        const bool online = m_room.isOnline();
        m_joinButton->setEnabled(online);
        m_joinButton->setText(online ? tr("一键加入") : tr("已下架"));
        m_joinButton->setToolTip(online ? QString() : tr("这个房间已经下架，等房主重新发布后再来吧"));
    }

    if (m_actionHint && m_actionHint->text().isEmpty())
        m_actionHint->hide();
}

void MultiplayerLobbyDetailPage::updateStatusChip()
{
    if (!m_statusChip)
        return;
    m_statusChip->setText(QStringLiteral("● ") + roomStatusText(m_room));
    const QString state = roomStatusKey(m_room);
    if (m_statusChip->property("roomState").toString() != state)
    {
        m_statusChip->setProperty("roomState", state);
        m_statusChip->style()->unpolish(m_statusChip);
        m_statusChip->style()->polish(m_statusChip);
    }
}

void MultiplayerLobbyDetailPage::updateHostRow()
{
    if (!m_hostHost)
        return;

    clearLayoutWidgets(m_hostHost->layout());

    const QString name = hostName(m_room);
    const QDateTime published = m_room.createdAt > 0
                                    ? QDateTime::fromSecsSinceEpoch(m_room.createdAt)
                                    : QDateTime();

    if (name.isEmpty())
    {
        auto *muted = new QLabel(tr("房主信息不可用"), m_hostHost);
        muted->setObjectName(QStringLiteral("mlHeroTime"));
        m_hostHost->layout()->addWidget(muted);
    }
    else
    {
        QLabel *avatar = ForumKit::makeAvatar(m_hostHost, m_room.host.avatar, name, 26);
        m_hostHost->layout()->addWidget(avatar);

        auto *hostLabel = new QLabel(tr("房主 %1").arg(name), m_hostHost);
        hostLabel->setObjectName(QStringLiteral("mlHeroHost"));
        m_hostHost->layout()->addWidget(hostLabel);

        if (published.isValid())
        {
            auto *timeLabel = new QLabel(
                tr("· %1发布").arg(ForumKit::relativeTime(m_room.createdAt)), m_hostHost);
            timeLabel->setObjectName(QStringLiteral("mlHeroTime"));
            m_hostHost->layout()->addWidget(timeLabel);
        }
    }

    // 重新加弹簧（清空时被一起删掉了）
    m_hostHost->layout()->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));
}

void MultiplayerLobbyDetailPage::updateMetaChips()
{
    if (!m_metaHost || !m_metaHost->layout())
        return;

    clearLayoutWidgets(m_metaHost->layout());

    auto addChip = [this](const QString &text) {
        auto *chip = new QLabel(text, m_metaHost);
        chip->setObjectName(QStringLiteral("mlHeroTag"));
        m_metaHost->layout()->addWidget(chip);
    };

    const QString core = m_room.coreLabel.trimmed().isEmpty()
                             ? ForumLobbyService::coreDisplayName(m_room.core)
                             : m_room.coreLabel.trimmed();
    addChip(core);
    if (!m_room.gameVersion.isEmpty())
        addChip(tr("MC %1").arg(m_room.gameVersion));
    if (!m_room.loader.isEmpty())
        addChip(m_room.loader);
    addChip(tr("%1 人上限").arg(m_room.maxPlayers));
    if (m_room.joinCount > 0)
        addChip(tr("%1 次加入").arg(m_room.joinCount));
    if (m_room.hasModpack())
        addChip(tr("需要前置整合包"));
}

void MultiplayerLobbyDetailPage::updateIntro()
{
    const QString desc = m_room.description.trimmed();
    const bool hasMd = !m_room.md.trimmed().isEmpty();

    if (m_descLabel)
    {
        m_descLabel->setText(desc);
        m_descLabel->setVisible(!desc.isEmpty());
    }
    if (m_introBrowser)
    {
        m_introBrowser->setPostMarkdown(m_room.md);
        m_introBrowser->setVisible(hasMd);
    }
    if (m_introHint)
    {
        m_introHint->setVisible(!hasMd && desc.isEmpty());
        m_introHint->setText(tr("房主还没有写详细介绍，先看看右边的房间信息，或者直接进游戏看看。"));
    }
}

void MultiplayerLobbyDetailPage::updateMatchText()
{
    if (!m_matchLabel)
        return;

    // 已下架的房间别再算实例匹配，直接说清楚要等房主重新发布
    if (!m_room.isOnline())
    {
        m_missingMods.clear();
        m_missingAuxMods.clear();
        m_missingPacks.clear();
        m_missingShaders.clear();
        m_matchLabel->setText(tr("房间已下架，等房主重新发布后再来加入。"));
        m_matchLabel->setToolTip(tr("这个房间已经过期，刷新大厅后看看别的房间"));
        return;
    }

    const LobbyInstanceMatcher::Requirement requirement = LobbyInstanceMatcher::requirementFor(
        m_room.gameVersion, m_room.loader, m_room.requiredMods(), m_room.auxMods,
        m_room.resourcePacks, m_room.shaderPacks);
    const LobbyInstanceMatcher::JoinPlan plan = LobbyInstanceMatcher::makePlan(requirement);

    m_missingMods = plan.best.missingMods;
    m_missingAuxMods = plan.best.missingAuxMods;
    m_missingPacks = plan.best.missingResourcePacks;
    m_missingShaders = plan.best.missingShaderPacks;

    QString headline;
    if (!plan.found)
        headline = plan.installHint.trimmed().isEmpty()
                       ? tr("本机没有对应版本的实例，先去「资源」里装一个吧。")
                       : plan.installHint;
    else
        headline = matchHeadline(m_room, nullptr, nullptr);
    m_matchLabel->setText(headline);

    QStringList tips;
    if (!m_missingMods.isEmpty())
        tips << tr("缺必装模组：%1").arg(m_missingMods.join(QStringLiteral("、")));
    if (!m_missingAuxMods.isEmpty())
        tips << tr("缺辅助模组 %1 个（可选）").arg(m_missingAuxMods.size());
    if (!m_missingPacks.isEmpty())
        tips << tr("缺资源包 %1 个（可选）").arg(m_missingPacks.size());
    if (!m_missingShaders.isEmpty())
        tips << tr("缺光影包 %1 个（可选）").arg(m_missingShaders.size());

    if (!tips.isEmpty())
        m_matchLabel->setToolTip(tips.join(QLatin1Char('\n')));
    else if (!plan.found)
        m_matchLabel->setToolTip(tr("先在资源里装一个对应版本的实例再回来加入"));
    else
        m_matchLabel->setToolTip(tr("加入时会自动选这个实例"));
}

void MultiplayerLobbyDetailPage::updateMods()
{
    if (!m_modsHint || !m_modsHost)
        return;

    clearLayoutWidgets(m_modsHost->layout());
    if (m_auxHost)
        clearLayoutWidgets(m_auxHost->layout());
    if (m_extrasHost)
        clearLayoutWidgets(m_extrasHost->layout());

    const QStringList required = m_room.requiredMods();
    const bool hasAnything = !required.isEmpty() || !m_room.auxMods.isEmpty()
                             || !m_room.resourcePacks.isEmpty() || !m_room.shaderPacks.isEmpty();

    if (!hasAnything)
    {
        m_modsHint->setText(tr("房主没有限定模组，原版客户端就能进。"));
        m_modsHost->hide();
        if (m_auxHint)
            m_auxHint->hide();
        if (m_auxHost)
            m_auxHost->hide();
        if (m_extrasHint)
            m_extrasHint->hide();
        if (m_extrasHost)
            m_extrasHost->hide();
        return;
    }

    m_modsHost->show();
    if (!required.isEmpty())
    {
        if (!m_room.isOnline())
            m_modsHint->setText(tr("房间已下架，下面列出的模组只作参考。"));
        else if (m_missingMods.isEmpty())
            m_modsHint->setText(tr("必装模组 %1 个，本机实例已经齐全。").arg(required.size()));
        else if (m_missingMods.size() >= required.size())
            m_modsHint->setText(
                tr("这 %1 个必装模组本机都没有；点「一键加入」会问你要不要先下载。")
                    .arg(required.size()));
        else
            m_modsHint->setText(
                tr("必装模组 %1 个，暖色标出的 %2 个本机缺；点「一键加入」会问你要不要补上。")
                    .arg(required.size())
                    .arg(m_missingMods.size()));
    }
    else
    {
        m_modsHint->setText(tr("房主没限定必装模组，原版客户端就能进。"));
    }

    for (const QString &mod : required)
    {
        auto *chip = new QLabel(elide(mod, 34), m_modsHost);
        chip->setObjectName(QStringLiteral("mlDetailModChip"));
        chip->setToolTip(mod);
        if (m_missingMods.contains(mod))
            chip->setProperty("missing", true);
        m_modsHost->layout()->addWidget(chip);
    }

    // 辅助模组：单独一段，标成可选
    if (m_auxHint && m_auxHost)
    {
        const bool hasAux = !m_room.auxMods.isEmpty();
        m_auxHint->setVisible(hasAux);
        m_auxHost->setVisible(hasAux);
        if (hasAux)
        {
            if (m_missingAuxMods.isEmpty())
                m_auxHint->setText(tr("另有 %1 个带「辅助」标签的模组，本机都有。")
                                       .arg(m_room.auxMods.size()));
            else
                m_auxHint->setText(
                    tr("另有 %1 个带「辅助」标签的模组（不装也能进），%2 个本机没有；"
                       "加入时可以勾选一起装。")
                        .arg(m_room.auxMods.size())
                        .arg(m_missingAuxMods.size()));
            for (const QString &mod : m_room.auxMods)
            {
                auto *chip = new QLabel(elide(mod, 30), m_auxHost);
                chip->setObjectName(QStringLiteral("mlDetailModChip"));
                chip->setToolTip(tr("辅助模组（可选）：%1").arg(mod));
                if (m_missingAuxMods.contains(mod))
                    chip->setProperty("missing", true);
                m_auxHost->layout()->addWidget(chip);
            }
        }
    }

    // 资源包 / 光影包：房主用的那套，加入时可以勾选安装
    if (m_extrasHint && m_extrasHost)
    {
        const bool hasExtras = !m_room.resourcePacks.isEmpty() || !m_room.shaderPacks.isEmpty();
        m_extrasHint->setVisible(hasExtras);
        m_extrasHost->setVisible(hasExtras);
        if (hasExtras)
        {
            QStringList parts;
            if (!m_room.resourcePacks.isEmpty())
                parts << tr("资源包 %1 个").arg(m_room.resourcePacks.size());
            if (!m_room.shaderPacks.isEmpty())
                parts << tr("光影包 %1 个").arg(m_room.shaderPacks.size());
            const int missingCount = m_missingPacks.size() + m_missingShaders.size();
            if (missingCount > 0)
                m_extrasHint->setText(tr("房主还用了%1，其中 %2 个本机没有；加入时可以勾选安装。")
                                          .arg(parts.join(tr("、")))
                                          .arg(missingCount));
            else
                m_extrasHint->setText(tr("房主还用了%1，本机都有。").arg(parts.join(tr("、"))));

            for (const QString &name : m_room.resourcePacks)
            {
                auto *chip = new QLabel(tr("资源包 · %1").arg(elide(name, 26)), m_extrasHost);
                chip->setObjectName(QStringLiteral("mlDetailModChip"));
                chip->setToolTip(tr("房主使用的资源包：%1").arg(name));
                if (m_missingPacks.contains(name))
                    chip->setProperty("missing", true);
                m_extrasHost->layout()->addWidget(chip);
            }
            for (const QString &name : m_room.shaderPacks)
            {
                auto *chip = new QLabel(tr("光影包 · %1").arg(elide(name, 26)), m_extrasHost);
                chip->setObjectName(QStringLiteral("mlDetailModChip"));
                chip->setToolTip(tr("房主使用的光影包：%1").arg(name));
                if (m_missingShaders.contains(name))
                    chip->setProperty("missing", true);
                m_extrasHost->layout()->addWidget(chip);
            }
        }
    }
}

void MultiplayerLobbyDetailPage::updateModpack()
{
    if (!m_modpackCard)
        return;

    if (!m_room.hasModpack())
    {
        m_modpackCard->hide();
        return;
    }
    m_modpackCard->show();

    if (m_packNameLabel)
        m_packNameLabel->setText(m_room.modpackName);

    // 在线整合包会带一份信息快照，挑几个关键字段显示
    QString metaText;
    const QJsonDocument doc = QJsonDocument::fromJson(m_room.modpackMeta.toUtf8());
    if (doc.isObject())
    {
        const QJsonObject meta = doc.object();
        QStringList parts;
        const QString platform = meta.value(QStringLiteral("platform")).toString();
        if (!platform.isEmpty())
            parts << platform;
        const QString version = meta.value(QStringLiteral("version")).toString();
        if (!version.isEmpty())
            parts << tr("版本 %1").arg(version);
        const QString author = meta.value(QStringLiteral("author")).toString();
        if (!author.isEmpty())
            parts << tr("作者 %1").arg(author);
        metaText = parts.join(QStringLiteral(" · "));
    }
    if (m_packMetaLabel)
    {
        m_packMetaLabel->setText(metaText);
        m_packMetaLabel->setVisible(!metaText.isEmpty());
    }

    const QString url = m_room.modpackUrl.trimmed();
    if (m_packHintLabel)
    {
        if (m_room.modpackMode == QLatin1String("online") && !url.isEmpty())
            m_packHintLabel->setText(tr("房主提供了在线整合包，装了它再加入最稳；也可以直接点下面打开下载页。"));
        else if (m_room.modpackMode == QLatin1String("link") && !url.isEmpty())
            m_packHintLabel->setText(tr("房主给的是网盘链接，需要自己下载安装后再加入。"));
        else
            m_packHintLabel->setText(tr("房主标了前置整合包，但没有留下下载地址，加入前问一下房主吧。"));
    }
    if (m_packButton)
    {
        m_packButton->setVisible(!url.isEmpty());
        m_packButton->setText(m_room.modpackMode == QLatin1String("online") ? tr("打开整合包下载页")
                                                                           : tr("打开下载地址"));
    }
}

void MultiplayerLobbyDetailPage::updatePlayers()
{
    if (!m_playersCard || !m_playersHost || !m_playersHint)
        return;

    clearLayoutWidgets(m_playersHost->layout());

    if (!m_room.isOnline())
    {
        m_playersHint->setText(tr("房间已下架，这里现在没有人玩。"));
        m_playersHost->hide();
        return;
    }

    if (m_room.players.isEmpty())
    {
        // 老服务端没有这份数据时也别空着，给一句话说清楚
        m_playersHint->setText(tr("暂时看不到谁在玩，进游戏后这里会显示房间里的人。"));
        m_playersHost->hide();
        return;
    }

    m_playersHost->show();

    const int count = m_room.players.size();
    const int limit = qMax(1, m_room.maxPlayers);
    if (count <= 1)
    {
        m_playersHint->setText(m_room.mine
                                   ? tr("这是你的房间，别人进游戏后会出现在这里。")
                                   : tr("只有房主在房间里等你，点「一键加入」就能一起玩。"));
    }
    else
    {
        m_playersHint->setText(tr("房间里 %1 人 · 上限 %2 人").arg(count).arg(limit));
    }

    const int selfId = ForumAccountService::instance()->user().id;
    for (const ForumLobbyPlayer &player : m_room.players)
    {
        const bool isSelf = selfId > 0 && player.id == selfId;
        m_playersHost->layout()->addWidget(
            makePlayerChip(m_playersHost, player, isSelf));
    }
}

void MultiplayerLobbyDetailPage::updateStatsRoom(bool roomChanged)
{
    if (!m_statsCard || !m_statsChart)
        return;

    if (roomChanged)
    {
        // 换房间：旧曲线立刻清掉，免得把上一个房间的人数画到新房间里
        m_statsRoomId = m_room.id;
        m_statsFetching = false;
        m_statsLastFetchMs = 0;
        m_statsLastReportMs = 0;
        m_statsChart->clear();
        m_statsChart->setCapacity(qMax(1, m_room.maxPlayers));
        if (m_statsValue)
            m_statsValue->setText(tr("正在读取人数记录…"));
        if (m_statsHint)
            m_statsHint->setText(
                tr("最近 6 小时的人数变化，由打开过这个房间详情的人共同记录。"));
        if (m_room.id > 0 && m_room.isOnline())
            requestStatsHistory(true);
    }

    if (!m_statsValue)
        return;
    if (m_room.id <= 0)
    {
        m_statsValue->setText(tr("还没有选中房间"));
        return;
    }
    if (!m_room.isOnline())
    {
        m_statsValue->setText(tr("房间已下架"));
        if (m_statsHint)
            m_statsHint->setText(tr("下面是房间还在线时留下的人数记录。"));
        return;
    }

    const int count = m_room.players.size();
    const int limit = qMax(1, m_room.maxPlayers);
    m_statsValue->setText(tr("现在 %1 / %2 人").arg(count).arg(limit));
    reportStatsSample(false);
}

void MultiplayerLobbyDetailPage::reportStatsSample(bool force)
{
    if (m_room.id <= 0 || !m_room.isOnline() || !isVisible())
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_statsLastReportMs > 0 && nowMs - m_statsLastReportMs < 60000)
        return;
    m_statsLastReportMs = nowMs;
    // 在线人数用「正在游玩」的人数：房主 + 已进游戏且还在上报的玩家
    ForumPlayerStatsService::instance()->reportLobby(m_room.id, m_room.players.size());
}

void MultiplayerLobbyDetailPage::requestStatsHistory(bool force)
{
    if (m_room.id <= 0 || !m_statsChart || m_statsFetching)
        return;
    if (!force && !isVisible())
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_statsLastFetchMs > 0 && nowMs - m_statsLastFetchMs < 120000)
        return;

    m_statsFetching = true;
    m_statsLastFetchMs = nowMs;
    const int roomId = m_room.id;
    ForumPlayerStatsService::instance()->fetchLobby(
        roomId, 6,
        [this, roomId, nowMs](const PlayerStatSeries &series) {
            m_statsFetching = false;
            if (m_room.id != roomId || !m_statsChart)
                return; // 已经换房间了，丢弃过期结果
            QVector<PlayerCountChart::Point> points;
            points.reserve(series.points.size());
            for (const PlayerStatPoint &point : series.points)
            {
                PlayerCountChart::Point item;
                item.t = point.t;
                item.online = point.online;
                item.maxPlayers = point.maxPlayers;
                points.append(item);
            }
            const qint64 nowSec = nowMs / 1000;
            // 房间一般只活一会儿：数据没铺满 6 小时时按实际范围铺开，别挤在右边一小条
            qint64 fromSec = nowSec - 6 * 3600;
            if (!points.isEmpty() && points.first().t > fromSec + 900)
                fromSec = qMax<qint64>(0, points.first().t - 300);
            m_statsChart->setPoints(points, fromSec, nowSec);
            m_statsChart->setCapacity(qMax(1, m_room.maxPlayers));
            if (!m_statsHint)
                return;
            if (points.isEmpty())
                m_statsHint->setText(tr("还没有人记录过这个房间的人数，过几分钟再来看看。"));
            else
                m_statsHint->setText(tr("最近 6 小时人数变化 · 悬停看每个时刻"));
        },
        [this, roomId]() {
            m_statsFetching = false;
            if (m_room.id != roomId || !m_statsHint)
                return;
            if (m_statsChart && !m_statsChart->hasData())
                m_statsHint->setText(tr("读取人数记录失败，稍后会自动重试。"));
        });
}

void MultiplayerLobbyDetailPage::updateInfoRows()
{
    auto setValue = [](QLabel *label, const QString &text) {
        if (label)
            label->setText(text);
    };
    auto orUnset = [](const QString &text, const QString &fallback) {
        return text.trimmed().isEmpty() ? fallback : text.trimmed();
    };

    const QString core = m_room.coreLabel.trimmed().isEmpty()
                             ? ForumLobbyService::coreDisplayName(m_room.core)
                             : m_room.coreLabel.trimmed();

    setValue(m_versionValue, orUnset(m_room.gameVersion, tr("不限")));
    setValue(m_loaderValue, orUnset(m_room.loader, tr("原版 / 不限")));
    setValue(m_coreValue, core);
    setValue(m_statusValue, roomStatusText(m_room));
    setValue(m_playersValue, tr("%1 人").arg(m_room.maxPlayers));
    setValue(m_joinCountValue, tr("%1 次").arg(m_room.joinCount));

    const QDateTime published = m_room.createdAt > 0
                                    ? QDateTime::fromSecsSinceEpoch(m_room.createdAt)
                                    : QDateTime();
    setValue(m_publishedValue,
             published.isValid()
                 ? published.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                 : tr("刚发布"));
    // 红石源房间没有论坛那套「有效期」：挂着就一直有效，不显示下架倒计时
    setValue(m_expireValue, m_room.isHongshiSource() ? tr("联机期间一直有效")
                                                     : expiresText(m_room.expiresIn));
}

// ────────────────────────────────────────────── 房间评论

void MultiplayerLobbyDetailPage::updateComments()
{
    if (!m_commentsCard)
        return;

    // 分享链接进来的房间没有服务端 id，没有评论区
    if (m_room.id <= 0)
    {
        m_commentsCard->hide();
        return;
    }
    m_commentsCard->show();

    if (m_commentsRoomId != m_room.id)
    {
        // 换了一个房间：旧评论全部丢掉，重新拉一批
        m_commentsRoomId = m_room.id;
        m_comments.clear();
        m_commentsTotal = 0;
        m_commentsFailed = false;
        m_commentsUnavailable = false;
        m_commentDraft.clear();
        if (m_commentEdit)
            m_commentEdit->clear();
        if (m_commentStatus)
            m_commentStatus->hide();
        requestComments();
    }

    rerenderCommentRows();
    updateCommentComposer();
}

void MultiplayerLobbyDetailPage::requestComments()
{
    if (!m_commentsCard || m_room.id <= 0)
        return;
    if (m_commentsLoading)
    {
        // 正在拉取时又要求刷新（刚发完评论 / 刚删完）：等这一轮回来再补一次，
        // 免得新评论被还在路上的旧列表盖掉
        m_commentsRefreshQueued = true;
        return;
    }

    m_commentsLoading = true;
    m_commentsFailed = false;
    m_commentsUnavailable = false;
    if (m_commentsRefresh)
        m_commentsRefresh->setEnabled(false);
    if (m_commentsHint && m_comments.isEmpty())
        m_commentsHint->setText(tr("正在加载评论…"));

    QPointer<MultiplayerLobbyDetailPage> guard(this);
    const int roomId = m_room.id;
    ForumLobbyService::instance()->fetchComments(
        roomId,
        [guard, roomId](const QVector<ForumLobbyComment> &comments, int total) {
            if (guard.isNull())
                return;
            guard->m_commentsLoading = false;
            if (guard->m_room.id != roomId)
            {
                // 等结果的时候已经换房间了：把排队的新一轮接上
                if (guard->m_commentsRefreshQueued)
                {
                    guard->m_commentsRefreshQueued = false;
                    guard->requestComments();
                }
                return;
            }
            guard->m_comments = comments;
            guard->m_commentsTotal = total;
            guard->m_commentsFailed = false;
            guard->m_commentsUnavailable = false;
            if (guard->m_commentsRefresh)
                guard->m_commentsRefresh->setEnabled(true);
            guard->rerenderCommentRows();
            guard->updateCommentComposer();
            if (guard->m_commentsRefreshQueued)
            {
                guard->m_commentsRefreshQueued = false;
                guard->requestComments();
            }
        },
        [guard, roomId](const QString &message, int status) {
            if (guard.isNull())
                return;
            guard->m_commentsLoading = false;
            if (guard->m_room.id != roomId)
            {
                if (guard->m_commentsRefreshQueued)
                {
                    guard->m_commentsRefreshQueued = false;
                    guard->requestComments();
                }
                return;
            }
            if (guard->m_commentsRefresh)
                guard->m_commentsRefresh->setEnabled(true);
            // 404 = 房间刚好过期 / 被房主下架：评论区锁住，等下次刷新
            guard->m_commentsUnavailable = status == 404;
            guard->m_commentsFailed = status != 404;
            guard->rerenderCommentRows();
            guard->updateCommentComposer();
            if (status != 404)
                guard->setCommentStatus(message, true);
            if (guard->m_commentsRefreshQueued)
            {
                guard->m_commentsRefreshQueued = false;
                guard->requestComments();
            }
        });
}

void MultiplayerLobbyDetailPage::rerenderCommentRows()
{
    if (!m_commentsHint || !m_commentsHost)
        return;

    if (m_commentsUnavailable)
        m_commentsHint->setText(tr("房间已关闭，评论暂时看不了。"));
    else if (m_commentsFailed && m_comments.isEmpty())
        m_commentsHint->setText(tr("评论加载失败，点「刷新」再试一次。"));
    else if (m_commentsLoading && m_comments.isEmpty())
        m_commentsHint->setText(tr("正在加载评论…"));
    else if (!m_room.isOnline())
        m_commentsHint->setText(tr("房间已下架，评论暂停更新。"));
    else if (m_comments.isEmpty())
        m_commentsHint->setText(tr("还没有人评论，来聊聊这个房间吧。"));
    else if (m_commentsTotal > m_comments.size())
        m_commentsHint->setText(tr("共 %1 条评论 · 显示最近 %2 条")
                                    .arg(m_commentsTotal)
                                    .arg(m_comments.size()));
    else
        m_commentsHint->setText(tr("共 %1 条评论").arg(m_commentsTotal));

    m_commentsHost->setVisible(!m_comments.isEmpty());

    // 评论内容没变就不重画（定时刷新 / 主题切换时别打断正在选中的文字）
    QStringList parts;
    parts.reserve(m_comments.size() + 1);
    parts << (m_room.mine ? QStringLiteral("1") : QStringLiteral("0"));
    for (const ForumLobbyComment &comment : m_comments)
    {
        parts << QStringLiteral("%1@%2@%3@%4@%5@%6")
                     .arg(comment.id)
                     .arg(comment.userId)
                     .arg(comment.createdAt)
                     .arg(comment.isHost ? 1 : 0)
                     .arg(comment.mine ? 1 : 0)
                     .arg(comment.content);
    }
    const QString signature = parts.join(QLatin1Char('|'));
    if (signature == m_commentsRowsSignature)
        return;
    m_commentsRowsSignature = signature;

    clearLayoutWidgets(m_commentsHost->layout());

    QPointer<MultiplayerLobbyDetailPage> guard(this);
    for (const ForumLobbyComment &comment : m_comments)
    {
        // 自己发的能删；房主可以清理自己房间里的留言
        std::function<void()> onDelete;
        if (comment.mine || m_room.mine)
        {
            onDelete = [guard, comment]() {
                if (!guard.isNull())
                    guard->deleteComment(comment);
            };
        }
        m_commentsHost->layout()->addWidget(makeCommentRow(m_commentsHost, comment, onDelete));
    }
}

void MultiplayerLobbyDetailPage::updateCommentComposer()
{
    if (!m_commentComposerHost || !m_commentComposerLayout)
        return;

    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool quizPassed = account->hasPassedQuiz();
    const QString state = QStringLiteral("%1|%2|%3|%4")
                              .arg(m_room.isOnline() ? 1 : 0)
                              .arg(m_commentsUnavailable ? 1 : 0)
                              .arg(loggedIn ? 1 : 0)
                              .arg(quizPassed ? 1 : 0);
    if (state == m_commentComposerState)
    {
        // 状态没变：别重建输入框，只把字数 / 按钮同步一下
        updateCommentCount();
        return;
    }
    m_commentComposerState = state;

    // 重建前先把已经写了一半的评论收起来，重新挂上后原样放回
    if (m_commentEdit)
        m_commentDraft = m_commentEdit->toPlainText();

    clearLayoutWidgets(m_commentComposerLayout);
    m_commentEdit = nullptr;
    m_commentCount = nullptr;
    m_commentSend = nullptr;
    m_commentComposerHost->show();

    auto addHint = [this](const QString &text) {
        auto *hint = new QLabel(text, m_commentComposerHost);
        hint->setObjectName(QStringLiteral("mlDetailMuted"));
        hint->setWordWrap(true);
        m_commentComposerLayout->addWidget(hint);
    };
    auto addAction = [this](const QString &text, const std::function<void()> &onClick) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        auto *button = new QPushButton(text, m_commentComposerHost);
        button->setObjectName(QStringLiteral("mlDetailGhost"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(30);
        connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
        row->addWidget(button);
        row->addStretch(1);
        m_commentComposerLayout->addLayout(row);
    };

    if (m_commentsUnavailable || !m_room.isOnline())
    {
        addHint(tr("房间已下架，等房主重新发布后再来评论。"));
        return;
    }
    if (!loggedIn)
    {
        addHint(tr("登录论坛账号后，就能在这里给房主留言：问版本、约时间、提醒缺哪个模组。"));
        addAction(tr("登录 / 注册"), [this]() { emit loginRequested(); });
        return;
    }
    if (!quizPassed)
    {
        addHint(tr("评论前需要先通过入站答题，成绩会记在你的账号上，网页论坛同样生效。"));
        addAction(tr("去答题"), [this]() { openQuizDialog(); });
        return;
    }

    const ForumUser me = account->user();
    QString name = me.displayName.trimmed();
    if (name.isEmpty())
        name = me.username.trimmed();
    if (name.isEmpty())
        name = tr("方块盒子玩家");

    auto *identityRow = new QHBoxLayout();
    identityRow->setContentsMargins(0, 0, 0, 0);
    identityRow->setSpacing(8);
    identityRow->addWidget(ForumKit::makeAvatar(m_commentComposerHost, me.avatar, name, 24), 0,
                           Qt::AlignVCenter);
    auto *identity =
        new QLabel(tr("以 %1 的身份评论 · Ctrl + Enter 发送").arg(name), m_commentComposerHost);
    identity->setObjectName(QStringLiteral("mlDetailCommentIdentity"));
    identity->setWordWrap(true);
    identityRow->addWidget(identity, 1);
    m_commentComposerLayout->addLayout(identityRow);

    m_commentEdit = new QPlainTextEdit(m_commentComposerHost);
    m_commentEdit->setObjectName(QStringLiteral("mlDetailCommentEdit"));
    m_commentEdit->setPlaceholderText(tr("说点什么…比如还缺哪个模组、什么时候方便一起玩"));
    m_commentEdit->setFixedHeight(78);
    m_commentEdit->setTabChangesFocus(true);
    connect(m_commentEdit, &QPlainTextEdit::textChanged, this,
            &MultiplayerLobbyDetailPage::updateCommentCount);
    m_commentComposerLayout->addWidget(m_commentEdit);

    // 回车换行，Ctrl + Enter 直接发表（只在输入框里生效，别抢别的页面快捷键）
    for (const int key : {Qt::Key_Return, Qt::Key_Enter})
    {
        auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | key), m_commentEdit);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this,
                &MultiplayerLobbyDetailPage::submitComment);
    }

    auto *foot = new QHBoxLayout();
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(8);
    m_commentCount = new QLabel(m_commentComposerHost);
    m_commentCount->setObjectName(QStringLiteral("mlDetailCommentCount"));
    foot->addWidget(m_commentCount);
    foot->addStretch(1);
    m_commentSend = new QPushButton(tr("发表评论"), m_commentComposerHost);
    m_commentSend->setObjectName(QStringLiteral("mlDetailCommentSend"));
    m_commentSend->setCursor(Qt::PointingHandCursor);
    m_commentSend->setFixedHeight(32);
    connect(m_commentSend, &QPushButton::clicked, this,
            &MultiplayerLobbyDetailPage::submitComment);
    foot->addWidget(m_commentSend);
    m_commentComposerLayout->addLayout(foot);

    if (!m_commentDraft.isEmpty())
    {
        m_commentEdit->setPlainText(m_commentDraft);
        m_commentEdit->moveCursor(QTextCursor::End);
    }
    updateCommentCount();
}

void MultiplayerLobbyDetailPage::updateCommentCount()
{
    if (!m_commentEdit || !m_commentCount || !m_commentSend)
        return;

    const int length = m_commentEdit->toPlainText().size();
    m_commentCount->setText(tr("%1 / %2 字").arg(length).arg(kCommentMaxLength));
    const bool over = length > kCommentMaxLength;
    if (m_commentCount->property("over").toBool() != over)
    {
        m_commentCount->setProperty("over", over);
        m_commentCount->style()->unpolish(m_commentCount);
        m_commentCount->style()->polish(m_commentCount);
    }
    if (!m_commentSubmitting)
        m_commentSend->setText(tr("发表评论"));
    m_commentSend->setEnabled(!m_commentSubmitting && length > 0 && !over);
}

void MultiplayerLobbyDetailPage::submitComment()
{
    if (m_commentSubmitting || !m_commentEdit)
        return;

    auto *account = ForumAccountService::instance();
    if (!account->isLoggedIn())
    {
        emit loginRequested();
        return;
    }
    if (!account->hasPassedQuiz())
    {
        openQuizDialog();
        return;
    }

    const QString content = m_commentEdit->toPlainText().trimmed();
    if (content.isEmpty())
    {
        setCommentStatus(tr("先写点什么再发表吧。"), true);
        return;
    }
    if (content.size() > kCommentMaxLength)
    {
        setCommentStatus(tr("评论最多 %1 个字，删掉一些再发。").arg(kCommentMaxLength), true);
        return;
    }

    m_commentSubmitting = true;
    m_commentSend->setEnabled(false);
    m_commentSend->setText(tr("发表中…"));

    QPointer<MultiplayerLobbyDetailPage> guard(this);
    ForumLobbyService::instance()->postComment(
        m_room.id, content,
        [guard](const ForumLobbyComment &) {
            if (guard.isNull())
                return;
            guard->m_commentSubmitting = false;
            guard->m_commentDraft.clear();
            if (guard->m_commentEdit)
                guard->m_commentEdit->clear();
            guard->updateCommentCount();
            guard->setCommentStatus(tr("评论已发表。"), false);
            guard->requestComments();
        },
        [guard](const QString &message, int) {
            if (guard.isNull())
                return;
            guard->m_commentSubmitting = false;
            guard->updateCommentCount();
            guard->setCommentStatus(message, true);
        });
}

void MultiplayerLobbyDetailPage::deleteComment(const ForumLobbyComment &comment)
{
    if (comment.id <= 0)
        return;

    const auto reply = AppMessageBox::question(
        this, tr("删除评论"),
        comment.mine ? tr("删除后这条评论就没了，确定删除吗？")
                     : tr("删除后这位玩家的留言就没了，确定删除吗？"),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    QPointer<MultiplayerLobbyDetailPage> guard(this);
    ForumLobbyService::instance()->deleteComment(
        comment.id,
        [guard]() {
            if (guard.isNull())
                return;
            guard->setCommentStatus(tr("评论已删除。"), false);
            guard->requestComments();
        },
        [guard](const QString &message, int) {
            if (guard.isNull())
                return;
            guard->setCommentStatus(message, true);
        });
}

void MultiplayerLobbyDetailPage::setCommentStatus(const QString &text, bool error)
{
    if (!m_commentStatus)
        return;
    m_commentStatus->setText(text);
    m_commentStatus->setVisible(!text.isEmpty());
    if (m_commentStatus->property("error").toBool() != error)
    {
        m_commentStatus->setProperty("error", error);
        m_commentStatus->style()->unpolish(m_commentStatus);
        m_commentStatus->style()->polish(m_commentStatus);
    }
}

void MultiplayerLobbyDetailPage::openQuizDialog()
{
    QWidget *host = window();
    QuizDialog dialog(host);
    connect(&dialog, &QuizDialog::loginRequested, this,
            &MultiplayerLobbyDetailPage::loginRequested);
    dialog.exec();
    // 通过后 userChanged 会带动输入区重建；这里再兜一次
    m_commentComposerState.clear();
    updateCommentComposer();
    if (m_room.id > 0)
        requestComments();
}

void MultiplayerLobbyDetailPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 从列表再次点进同一个房间时也刷新一次，不然看到的是上次的评论
    if (m_room.id > 0 && m_room.isOnline() && !m_commentsLoading)
        requestComments();
    // 人数曲线：进来先报一次当前人数、拉一次历史，离开再回来也能看到刚发生的变化
    if (m_room.id > 0)
    {
        m_statsLastReportMs = 0;
        reportStatsSample(false);
        requestStatsHistory(true);
    }
}

void MultiplayerLobbyDetailPage::copyTarget()
{
    const QString target = targetText();
    if (target.isEmpty())
    {
        setActionHint(tr("这个房间没有可复制的邀请码 / 地址。"), true);
        return;
    }
    QApplication::clipboard()->setText(target);
    setActionHint(m_room.isTerracotta() ? tr("已复制邀请码：%1").arg(elide(target, 28))
                                        : tr("已复制连接地址：%1").arg(elide(target, 28)));
}

QString MultiplayerLobbyDetailPage::shareText(bool withHeader) const
{
    const QString title = roomTitle(m_room);
    const QString target = targetText();

    QStringList lines;
    if (withHeader)
        lines << tr("【方块盒子 · 联机大厅】%1").arg(title);

    if (!target.isEmpty())
    {
        lines << (m_room.isTerracotta() ? tr("陶瓦邀请码：%1").arg(target)
                                        : tr("连接地址：%1").arg(target));
    }

    QStringList meta;
    if (!m_room.gameVersion.trimmed().isEmpty())
        meta << m_room.gameVersion.trimmed();
    if (!m_room.loader.trimmed().isEmpty())
        meta << m_room.loader.trimmed();
    const QString core = m_room.coreLabel.trimmed().isEmpty()
                             ? ForumLobbyService::coreDisplayName(m_room.core)
                             : m_room.coreLabel.trimmed();
    if (!core.isEmpty())
        meta << core;
    if (!meta.isEmpty())
        lines << meta.join(QStringLiteral(" · "));

    if (target.isEmpty())
        lines << tr("房主还没填邀请码 / 连接地址，先进大厅看看吧。");
    else if (!m_room.isOnline())
        lines << tr("这个房间已经下架了，等房主重新发布再来。");
    else if (m_room.isTerracotta())
        lines << tr("打开方块盒子 →「联机大厅」搜「%1」，就能一键加入。").arg(title);
    else
        lines << tr("打开方块盒子 →「联机大厅」搜「%1」一键加入，也可以在游戏里添加上面的地址。").arg(title);

    return lines.join(QLatin1Char('\n'));
}

void MultiplayerLobbyDetailPage::shareRoom()
{
    const QString fullText = shareText(true);
    const QString bodyText = shareText(false);
    const QString title = roomTitle(m_room);

    QMenu menu(this);
    menu.addAction(tr("复制分享文案"), this, [this, fullText]() {
        QApplication::clipboard()->setText(fullText);
        setActionHint(tr("分享文案已复制，去聊天窗口粘贴给朋友吧"));
    });

    if (!targetText().isEmpty())
    {
        menu.addAction(m_room.isTerracotta() ? tr("只复制邀请码") : tr("只复制连接地址"), this,
                       &MultiplayerLobbyDetailPage::copyTarget);
    }

    if (ForumAccountService::instance()->isLoggedIn())
    {
        const QString summary = tr("联机大厅 · %1").arg(
            m_room.coreLabel.trimmed().isEmpty()
                ? ForumLobbyService::coreDisplayName(m_room.core)
                : m_room.coreLabel.trimmed());
        menu.addAction(tr("分享给好友"), this, [this, title, bodyText, summary]() {
            ShareToFriendDialog::share(this, title, bodyText, summary);
        });
    }

    const QPoint pos = m_shareButton
                           ? m_shareButton->mapToGlobal(QPoint(0, m_shareButton->height() + 4))
                           : QCursor::pos();
    menu.exec(pos);
}

bool MultiplayerLobbyDetailPage::isCompactWidth() const
{
    return width() > 0 && width() < 920;
}

void MultiplayerLobbyDetailPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void MultiplayerLobbyDetailPage::updateResponsiveLayout()
{
    if (!m_columns || !m_leftColumn || !m_rightColumn || !m_joinCard || !m_heroBanner)
        return;

    const bool compact = isCompactWidth();
    if (compact == m_compact)
        return;
    m_compact = compact;

    if (compact)
    {
        m_rootLayout->setContentsMargins(12, 12, 12, 10);
        m_bodyLayout->setSpacing(10);
        m_columns->setDirection(QBoxLayout::TopToBottom);
        m_columns->setSpacing(10);
        m_columns->setStretch(0, 0);
        m_columns->setStretch(1, 0);
        // 单栏时「加入这个房间」要排在介绍前面，玩家不用滑到底才能进房；
        // 「正在游玩」紧跟在它后面，先看有谁在玩再决定要不要进
        if (m_leftColumn->indexOf(m_joinCard) < 0)
        {
            m_rightColumn->removeWidget(m_joinCard);
            m_leftColumn->insertWidget(0, m_joinCard);
            if (m_playersCard)
            {
                m_rightColumn->removeWidget(m_playersCard);
                m_leftColumn->insertWidget(1, m_playersCard);
            }
            // 人数统计紧跟在「正在游玩」后面：先看谁在玩，再看最近的人数走势
            if (m_statsCard)
            {
                m_rightColumn->removeWidget(m_statsCard);
                m_leftColumn->insertWidget(2, m_statsCard);
            }
        }
        m_heroBanner->setFixedHeight(232);
        if (auto *content = m_heroBanner->contentLayout())
            content->setContentsMargins(18, 18, 18, 16);
    }
    else
    {
        m_rootLayout->setContentsMargins(24, 18, 24, 16);
        m_bodyLayout->setSpacing(12);
        m_columns->setDirection(QBoxLayout::LeftToRight);
        m_columns->setSpacing(12);
        m_columns->setStretch(0, 3);
        m_columns->setStretch(1, 2);
        if (m_rightColumn->indexOf(m_joinCard) < 0)
        {
            m_leftColumn->removeWidget(m_joinCard);
            m_rightColumn->insertWidget(0, m_joinCard);
            if (m_playersCard)
            {
                m_leftColumn->removeWidget(m_playersCard);
                m_rightColumn->insertWidget(1, m_playersCard);
            }
            if (m_statsCard)
            {
                m_leftColumn->removeWidget(m_statsCard);
                m_rightColumn->insertWidget(2, m_statsCard);
            }
        }
        m_heroBanner->setFixedHeight(300);
        if (auto *content = m_heroBanner->contentLayout())
            content->setContentsMargins(26, 22, 26, 20);
    }
}
