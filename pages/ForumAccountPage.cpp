/**
 * @file   ForumAccountPage.cpp
 * @brief  账户管理页（实验性·论坛账户版）实现
 * @author BlockBox Team
 * @date   2026-10-31
 */
#include "ForumAccountPage.h"

#include "pages/PlaytimeStatsPage.h"

#include "SkinStationPage.h"

#include <algorithm>

#include <QDateTime>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/ForumKit.h"
#include "components/ForumPostBrowser.h"
#include "components/MedalShareDialog.h"
#include "components/NotificationManager.h"
#include "components/QrLoginDialog.h"
#include "components/UserAgreementDialog.h"
#include "layouts/FlowLayout.h"
#include "layouts/MasonryLayout.h"
#include "utils/AssetUrls.h"
#include "utils/FeedbackService.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumChatService.h"
#include "utils/AuthManager.h"
#include "utils/FavoritesManager.h"
#include "utils/IconHelper.h"
#include "utils/GameAchievementCatalog.h"
#include "utils/LocalFavoritesManager.h"
#include "utils/MedalCatalog.h"
#include "utils/MedalShareCard.h"
#include "utils/SettingsManager.h"
#include "utils/SkinDownloader.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"
#include "utils/links/LinkRouter.h"

namespace
{

constexpr int kAvatarSize = 76;
/** 资料编辑页头像：圆形头像本体的尺寸（标签再加 1px 描边圈） */
constexpr int kSettingsAvatarSize = 56;
/** 资料卡顶部封面横幅：高度跟着卡片宽度走（3:1），和网页版、裁剪比例一致 */
/** 角色卡片封面统一高度（卡片等高，瀑布流按等宽等高排布） */
constexpr int kCharacterCoverHeight = 190;

/** 单行文本按控件宽度省略（论坛帖子列表的长标题） */
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) { setWordWrap(false); }

    void setFullText(const QString &text)
    {
        m_full = text;
        updateElidedText();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElidedText();
    }

private:
    void updateElidedText()
    {
        if (m_full.isEmpty())
        {
            QLabel::setText(QString());
            return;
        }
        const int available = qMax(60, width() - 2);
        QLabel::setText(fontMetrics().elidedText(m_full, Qt::ElideRight, available));
    }

    QString m_full;
};

/** 可点击的行（帖子 / 回复 / 收藏） */
class ClickableFrame : public QFrame
{
public:
    explicit ClickableFrame(QWidget *parent = nullptr) : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;
    std::function<void()> onDoubleClick;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // 只有真的按在这一行上，后面的松开才算点击。
        // 列表重新排版/滚动时，Qt 会把松开事件补发到光标下新出现的行上，
        // 这种没有按下事件配合的松开不能当成点击（否则点标签会直接跳进帖子）。
        m_pressed = event->button() == Qt::LeftButton && rect().contains(event->pos());
        QFrame::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool pressed = m_pressed;
        m_pressed = false;
        if (pressed && event->button() == Qt::LeftButton && rect().contains(event->pos()) && onClick)
            onClick();
        QFrame::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        m_pressed = false;
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onDoubleClick)
            onDoubleClick();
        QFrame::mouseDoubleClickEvent(event);
    }

private:
    bool m_pressed = false;
};

/** 只按当前页算尺寸的 QStackedWidget：避免「账号设置」这种长页面把别的页撑高 */
class AdaptiveStack : public QStackedWidget
{
public:
    explicit AdaptiveStack(QWidget *parent = nullptr) : QStackedWidget(parent)
    {
        connect(this, &QStackedWidget::currentChanged, this, [this](int) { updateGeometry(); });
    }

    QSize sizeHint() const override
    {
        if (QWidget *page = currentWidget())
            return page->sizeHint();
        return QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        if (QWidget *page = currentWidget())
            return page->minimumSizeHint();
        return QStackedWidget::minimumSizeHint();
    }
};

/** 圆形裁切头像 */
QPixmap roundPixmap(const QPixmap &source, int size)
{
    if (source.isNull() || size <= 0)
        return QPixmap();

    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(clip);

    const QPixmap scaled =
        source.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    painter.drawPixmap((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    return canvas;
}

/** 首字母头像（论坛默认头像同款：绿底白字） */
QPixmap letterAvatar(const QString &name, int size, const QColor &background)
{
    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(11, size / 2));
    painter.setFont(font);
    painter.setPen(Qt::white);

    const QString trimmed = name.trimmed();
    const QString letter = trimmed.isEmpty() ? QStringLiteral("?") : trimmed.left(1).toUpper();
    painter.drawText(canvas.rect(), Qt::AlignCenter, letter);
    return canvas;
}

/** 未登录头像：绿底 + 玩家剪影（纯手绘，不依赖图标资源） */
QPixmap guestAvatar(int size, const QColor &background)
{
    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);

    QPainterPath circle;
    circle.addEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));
    painter.drawPath(circle);

    painter.setClipPath(circle);
    const QColor silhouette(255, 255, 255, 235);
    painter.setBrush(silhouette);

    // 头
    const qreal headRadius = size * 0.135;
    painter.drawEllipse(QPointF(size / 2.0, size * 0.40), headRadius, headRadius);

    // 肩
    QPainterPath shoulders;
    shoulders.addRoundedRect(QRectF(size * 0.26, size * 0.56, size * 0.48, size * 0.40),
                             size * 0.24, size * 0.24);
    painter.drawPath(shoulders);

    return canvas;
}

/** 论坛网页同款相对时间 */
QString relativeTime(qint64 seconds)
{
    if (seconds <= 0)
        return QString();

    const qint64 diff = QDateTime::currentSecsSinceEpoch() - seconds;
    if (diff < 60)
        return QObject::tr("刚刚");
    if (diff < 3600)
        return QObject::tr("%1 分钟前").arg(diff / 60);
    if (diff < 86400)
        return QObject::tr("%1 小时前").arg(diff / 3600);
    if (diff < 86400 * 30)
        return QObject::tr("%1 天前").arg(diff / 86400);
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd"));
}

/** 通知列表的日期分组标题：今天 / 昨天 / 11月1日 */
QString notificationDayGroup(qint64 seconds)
{
    const QDateTime when = QDateTime::fromSecsSinceEpoch(seconds);
    if (!when.isValid() || seconds <= 0)
        return QObject::tr("更早");

    const QDate day = when.date();
    const QDate today = QDate::currentDate();
    if (day == today)
        return QObject::tr("今天");
    if (day == today.addDays(-1))
        return QObject::tr("昨天");
    if (day.year() == today.year())
        return day.toString(QObject::tr("M月d日"));
    return day.toString(QStringLiteral("yyyy-MM-dd"));
}

/**
 * 表单字段：标签在上，控件在下（对照论坛网页 .field）
 * @param headerExtra 可选：与标签同一行、靠右的小控件（如简介的「预览」开关）
 * @param outLabel    可选：把标签指针带出去，登录卡切「找回账号」时要把「密码」改成「新密码」
 */
QWidget *buildField(const QString &labelText, QWidget *editor, QWidget *parent,
                    QWidget *headerExtra = nullptr, QLabel **outLabel = nullptr)
{
    auto *field = new QWidget(parent);
    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *label = new QLabel(labelText, field);
    label->setObjectName(QStringLiteral("fbFieldLabel"));
    if (outLabel)
        *outLabel = label;
    if (headerExtra)
    {
        auto *headerRow = new QHBoxLayout();
        headerRow->setContentsMargins(0, 0, 0, 0);
        headerRow->setSpacing(8);
        headerRow->addWidget(label);
        headerRow->addStretch(1);
        headerRow->addWidget(headerExtra);
        layout->addLayout(headerRow);
    }
    else
    {
        layout->addWidget(label);
    }
    layout->addWidget(editor);
    return field;
}

QLabel *makeMutedLabel(const QString &text, QWidget *parent, const char *objectName)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setWordWrap(true);
    return label;
}

QFrame *makeDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("fbDivider"));
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    return line;
}

/** 编辑表单的小节标题：文字前加一段品牌色竖条，把「资料 / 密码 / 登录状态」分段衬出来 */
QWidget *makeSectionHeader(const QString &text, QWidget *parent, QLabel **outLabel = nullptr)
{
    auto *row = new QWidget(parent);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto *mark = new QFrame(row);
    mark->setObjectName(QStringLiteral("fbSectionMark"));
    mark->setFixedSize(4, 14);
    rowLayout->addWidget(mark, 0, Qt::AlignVCenter);

    auto *label = new QLabel(text, row);
    label->setObjectName(QStringLiteral("fbSectionTitle"));
    rowLayout->addWidget(label, 0, Qt::AlignVCenter);
    rowLayout->addStretch();
    if (outLabel)
        *outLabel = label;
    return row;
}

/**
 * 把一张皮肤纹理拼成 16×32 的正面角色图（像素风）：
 * 头 / 身体 / 双臂 / 双腿，modern（64×64）皮肤连同帽子、外套、袖子等覆盖层一起叠上去。
 */
QImage composeFrontModel(const QImage &skin)
{
    if (skin.isNull() || skin.width() < 64)
        return QImage();

    const bool modern = skin.height() >= 64;
    QImage model(16, 32, QImage::Format_ARGB32_Premultiplied);
    model.fill(Qt::transparent);

    QPainter painter(&model);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    auto blit = [&](int sx, int sy, int sw, int sh, int dx, int dy, bool mirror = false) {
        QImage part = skin.copy(sx, sy, sw, sh);
        if (part.isNull())
            return;
        if (mirror)
            part = part.flipped(Qt::Horizontal);
        painter.drawImage(QRect(dx, dy, sw, sh), part);
    };

    // 头（+ 帽子 / 头发层）
    blit(8, 8, 8, 8, 4, 0);
    if (modern)
        blit(40, 8, 8, 8, 4, 0);

    // 身体（+ 外套层）
    blit(20, 20, 8, 12, 4, 8);
    if (modern)
        blit(20, 36, 8, 12, 4, 8);

    // 右臂 / 左臂
    blit(44, 20, 4, 12, 0, 8);
    if (modern)
    {
        blit(44, 36, 4, 12, 0, 8);
        blit(36, 52, 4, 12, 12, 8);
        blit(52, 52, 4, 12, 12, 8);
    }
    else
    {
        blit(44, 20, 4, 12, 12, 8, true);
    }

    // 右腿 / 左腿
    blit(4, 20, 4, 12, 4, 20);
    if (modern)
    {
        blit(4, 36, 4, 12, 4, 20);
        blit(20, 52, 4, 12, 8, 20);
        blit(4, 52, 4, 12, 8, 20);
    }
    else
    {
        blit(4, 20, 4, 12, 8, 20, true);
    }

    return model;
}

/** 账户类型短文案（离线 / 微软正版 / 第三方服务器名） */
QString accountTypeText(const AccountInfo &account)
{
    const QString type = account.type;
    if (type.contains(QStringLiteral("微软")) || type.contains(QStringLiteral("Microsoft"),
                                                             Qt::CaseInsensitive))
        return QObject::tr("微软正版");
    if (type.contains(QObject::tr("离线")))
        return QObject::tr("离线");
    return type.isEmpty() ? QObject::tr("第三方") : type;
}

/**
 * 账户管理页「首次进入」的聚光灯引导：
 * 整页压暗，只在要介绍的区域抠出一块亮的，旁边挂一张气泡说明。
 *   第一步：高亮「角色」页签 —— 这里放游戏账户，启动游戏必须选一个；
 *   第二步：高亮资料卡   —— 这是方块盒子账户，发帖 / 加好友 / 联机都用它。
 * 点遮罩任意处或「下一步」推进，Esc /「跳过」结束。
 */
class AccountTourOverlay : public QWidget
{
public:
    struct Step
    {
        QString title;
        QString text;
        QPointer<QWidget> target;
    };

    explicit AccountTourOverlay(QWidget *host) : QWidget(host)
    {
        setObjectName(QStringLiteral("fbTourOverlay"));
        setFocusPolicy(Qt::StrongFocus);
        host->installEventFilter(this);
        buildBubble();
    }

    /** 走完 / 跳过后的回调（宿主用来记「已看过」并销毁本控件） */
    std::function<void()> onFinished;
    /** 把目标滚进可视区（目标在下半屏时用），可为空 */
    std::function<void(QWidget *)> ensureVisible;

    void setSteps(const QList<Step> &steps) { m_steps = steps; }

    void setThemeColors(const QColor &brand, const QColor &card, const QColor &line)
    {
        m_brandColor = brand;
        m_cardColor = card;
        m_lineColor = line;
        update();
    }

    bool isRunning() const { return m_index >= 0; }

    void start()
    {
        if (m_steps.isEmpty())
        {
            finish();
            return;
        }
        setGeometry(parentWidget()->rect());
        raise();
        show();
        setFocus(Qt::OtherFocusReason);
        m_index = 0;
        enterStep();
    }

    /** 目标位置变了（滚动 / 窗口尺寸）重新抠洞、重新摆气泡 */
    void refresh()
    {
        if (!isRunning())
            return;
        const QRect hole = holeRect();
        positionBubble(hole);
        if (m_bubble)
            m_bubble->show();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect hole = holeRect();
        const QRectF holeRectF(hole.adjusted(-1, -1, 1, 1));

        // 遮罩：整页压暗，把目标区域抠成圆角亮块（深色主题本来就暗，压得更重一点才看得出层次）
        QPainterPath scrim;
        scrim.addRect(QRectF(rect()));
        if (!hole.isEmpty())
        {
            QPainterPath cut;
            cut.addRoundedRect(holeRectF, 17, 17);
            scrim = scrim.subtracted(cut);
        }
        painter.fillPath(scrim, QColor(0, 0, 0, m_cardColor.lightness() < 110 ? 176 : 142));

        if (!hole.isEmpty())
        {
            // 外圈柔光 + 品牌色描边：深色卡片上也看得清高亮框
            QColor glow = m_brandColor;
            glow.setAlpha(52);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(glow, 8.0));
            painter.drawRoundedRect(holeRectF, 17, 17);
            painter.setPen(QPen(m_brandColor, 2.0));
            painter.drawRoundedRect(holeRectF, 17, 17);
        }

        paintTail(painter, hole);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            advance();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
        {
            finish();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Space)
        {
            advance();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget())
        {
            if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            {
                setGeometry(parentWidget()->rect());
                refresh();
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    static QString text(const char *source)
    {
        return QCoreApplication::translate("ForumAccountPage", source);
    }

    void buildBubble()
    {
        m_bubble = new QFrame(this);
        m_bubble->setObjectName(QStringLiteral("fbTourCard"));
        auto *bubbleLayout = new QVBoxLayout(m_bubble);
        bubbleLayout->setContentsMargins(18, 14, 18, 14);
        bubbleLayout->setSpacing(6);

        auto *topRow = new QHBoxLayout();
        topRow->setContentsMargins(0, 0, 0, 0);
        topRow->setSpacing(8);
        m_stepLabel = new QLabel(m_bubble);
        m_stepLabel->setObjectName(QStringLiteral("fbTourStep"));
        topRow->addWidget(m_stepLabel);
        topRow->addStretch();

        auto *skipBtn = new QPushButton(text("跳过"), m_bubble);
        skipBtn->setObjectName(QStringLiteral("fbTourSkip"));
        skipBtn->setCursor(Qt::PointingHandCursor);
        skipBtn->setFlat(true);
        connect(skipBtn, &QPushButton::clicked, this, [this]() { finish(); });
        topRow->addWidget(skipBtn);
        bubbleLayout->addLayout(topRow);

        m_titleLabel = new QLabel(m_bubble);
        m_titleLabel->setObjectName(QStringLiteral("fbTourTitle"));
        m_titleLabel->setWordWrap(true);
        bubbleLayout->addWidget(m_titleLabel);

        m_textLabel = new QLabel(m_bubble);
        m_textLabel->setObjectName(QStringLiteral("fbTourText"));
        m_textLabel->setWordWrap(true);
        m_textLabel->setTextFormat(Qt::PlainText);
        bubbleLayout->addWidget(m_textLabel);

        auto *buttonRow = new QHBoxLayout();
        buttonRow->setContentsMargins(0, 8, 0, 0);
        buttonRow->setSpacing(8);
        buttonRow->addStretch();

        m_nextBtn = new QPushButton(text("下一步"), m_bubble);
        m_nextBtn->setObjectName(QStringLiteral("fbTourNext"));
        m_nextBtn->setFixedHeight(30);
        m_nextBtn->setMinimumWidth(88);
        m_nextBtn->setCursor(Qt::PointingHandCursor);
        connect(m_nextBtn, &QPushButton::clicked, this, [this]() { advance(); });
        buttonRow->addWidget(m_nextBtn);
        bubbleLayout->addLayout(buttonRow);

        m_bubble->hide();
    }

    /** 当前步骤要抠亮的区域（页面坐标）；目标不可见时返回空 */
    QRect holeRect() const
    {
        if (m_index < 0 || m_index >= m_steps.size())
            return QRect();
        QWidget *target = m_steps.at(m_index).target;
        QWidget *host = parentWidget();
        if (!host || !target || !target->isVisible() || target->width() <= 0
            || target->height() <= 0)
        {
            return QRect();
        }

        // 目标在滚动区里面，和遮罩是两棵分支：先映射到宿主页，再扣掉遮罩自身的偏移
        const QPoint topLeft = target->mapTo(host, QPoint(0, 0)) - mapTo(host, QPoint(0, 0));
        const QRect mapped(topLeft, target->size());
        // 目标比页面还高（窗口很小）：只抠露出来的那截，别把边框画到页面外面去
        const QRect visible = mapped.intersected(rect().adjusted(3, 3, -3, -3));
        return visible;
    }

    void enterStep()
    {
        if (!isRunning())
            return;
        const Step &step = m_steps.at(m_index);
        m_stepLabel->setText(QStringLiteral("%1 / %2").arg(m_index + 1).arg(m_steps.size()));
        m_titleLabel->setText(step.title);
        m_textLabel->setText(step.text);
        m_nextBtn->setText(m_index + 1 >= m_steps.size() ? text("我知道了") : text("下一步"));

        // 先把气泡收起来，等目标滚进可视区、布局落定后再定位，免得看到气泡乱跳
        if (m_bubble)
            m_bubble->hide();
        if (step.target && ensureVisible)
            ensureVisible(step.target);

        QTimer::singleShot(90, this, [this]() {
            if (!isRunning())
                return;
            refresh();
        });
    }

    void advance()
    {
        if (!isRunning())
            return;
        if (m_index + 1 >= m_steps.size())
        {
            finish();
            return;
        }
        ++m_index;
        enterStep();
    }

    void finish()
    {
        if (!isRunning())
            return;
        m_index = -1;
        if (m_bubble)
            m_bubble->hide();
        hide();
        // 回调里宿主会 deleteLater 掉本控件，之后别再碰成员
        auto callback = onFinished;
        if (callback)
            callback();
    }

    /** 气泡贴着高亮区域摆：优先下方，其次上方，再退到左右，最后兜底不压目标 */
    void positionBubble(const QRect &hole)
    {
        if (!m_bubble)
            return;

        const int margin = 18;
        const int gap = 18;
        const int bubbleWidth = qMin(340, qMax(220, width() - 2 * margin));
        m_bubble->setFixedWidth(bubbleWidth);
        m_bubble->layout()->activate();
        int bubbleHeight = m_bubble->heightForWidth(bubbleWidth);
        if (bubbleHeight <= 0)
            bubbleHeight = m_bubble->sizeHint().height();

        const QRect area = rect().adjusted(margin, margin, -margin, -margin);
        bubbleHeight = qMin(bubbleHeight, qMax(80, area.height()));

        int x = area.left();
        int y = area.top();
        if (hole.isEmpty())
        {
            x = area.center().x() - bubbleWidth / 2;
            y = area.center().y() - bubbleHeight / 2;
        }
        else if (area.bottom() - hole.bottom() >= bubbleHeight + gap)
        {
            x = hole.center().x() - bubbleWidth / 2;
            y = hole.bottom() + gap;
        }
        else if (hole.top() - area.top() >= bubbleHeight + gap)
        {
            x = hole.center().x() - bubbleWidth / 2;
            y = hole.top() - gap - bubbleHeight;
        }
        else if (area.right() - hole.right() >= bubbleWidth + gap)
        {
            x = hole.right() + gap;
            y = hole.center().y() - bubbleHeight / 2;
        }
        else if (hole.left() - area.left() >= bubbleWidth + gap)
        {
            x = hole.left() - gap - bubbleWidth;
            y = hole.center().y() - bubbleHeight / 2;
        }
        else
        {
            // 四面都挤：压在页面底部，尽量别盖住目标本身
            x = area.center().x() - bubbleWidth / 2;
            y = area.bottom() - bubbleHeight;
            if (QRect(x, y, bubbleWidth, bubbleHeight).intersects(hole))
                y = qMax(area.top(), hole.top() - gap - bubbleHeight);
        }

        const int maxX = area.right() - bubbleWidth;
        const int maxY = area.bottom() - bubbleHeight;
        x = (maxX <= area.left()) ? area.left() : qBound(area.left(), x, maxX);
        y = (maxY <= area.top()) ? area.top() : qBound(area.top(), y, maxY);

        m_bubble->setGeometry(x, y, bubbleWidth, bubbleHeight);
        m_bubble->show();
        m_bubble->raise();
    }

    /** 从气泡贴向高亮区域的小箭头（画在遮罩上，所以只能由本控件自绘） */
    void paintTail(QPainter &painter, const QRect &hole)
    {
        if (!m_bubble || !m_bubble->isVisible())
            return;

        const QRect bubble = m_bubble->geometry();
        // 气泡压在亮块上（窗口太矮时的兜底摆法）：不画箭头，免得在卡片里多出一截白尖
        if (bubble.intersects(hole))
            return;
        const QPoint aim = hole.isEmpty() ? QPoint(width() / 2, height() / 2) : hole.center();
        const int half = 9;
        const int length = 11;

        QPolygonF tail;
        const bool vertical = aim.y() > bubble.bottom() + 6 || aim.y() < bubble.top() - 6;
        if (vertical)
        {
            const int x = qBound(bubble.left() + 22, aim.x(), bubble.right() - 22);
            if (aim.y() > bubble.bottom())
                tail << QPointF(x - half, bubble.bottom()) << QPointF(x + half, bubble.bottom())
                     << QPointF(x, bubble.bottom() + length);
            else
                tail << QPointF(x - half, bubble.top()) << QPointF(x + half, bubble.top())
                     << QPointF(x, bubble.top() - length);
        }
        else
        {
            const int y = qBound(bubble.top() + 20, aim.y(), bubble.bottom() - 20);
            if (aim.x() > bubble.right())
                tail << QPointF(bubble.right(), y - half) << QPointF(bubble.right(), y + half)
                     << QPointF(bubble.right() + length, y);
            else
                tail << QPointF(bubble.left(), y - half) << QPointF(bubble.left(), y + half)
                     << QPointF(bubble.left() - length, y);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(m_cardColor);
        painter.drawPolygon(tail);
        painter.setPen(QPen(m_lineColor, 1.0));
        painter.drawLine(tail.at(0), tail.at(2));
        painter.drawLine(tail.at(2), tail.at(1));
    }

    QList<Step> m_steps;
    QFrame *m_bubble = nullptr;
    QLabel *m_stepLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QPushButton *m_nextBtn = nullptr;
    int m_index = -1;
    QColor m_brandColor = QColor(0x2e, 0x7d, 0x32);
    QColor m_cardColor = QColor(Qt::white);
    QColor m_lineColor = QColor(0xe4, 0xec, 0xe3);
};

} // namespace

// ════════════════════════════════════════════════════════════════ 生命周期

ForumAccountPage::ForumAccountPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("fbRoot"));
    m_service = ForumAccountService::instance();
    m_network = new QNetworkAccessManager(this);

    buildUi();
    applyTheme();
    renderProfile();
    renderContentArea();

    // 本地收藏增删（任意资源页点星标 / 账户页移除）：
    // 资料卡「收藏」统计与「我的收藏 → 本地收藏」列表实时刷新
    connect(LocalFavoritesManager::instance(), &LocalFavoritesManager::favoritesChanged, this,
            [this]() {
                if (m_statBookmarks && m_service->isLoggedIn())
                    m_statBookmarks->setText(
                        QString::number(m_bookmarkTotal + m_contentFavoriteTotal
                                        + LocalFavoritesManager::instance()->count()));
                if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkLocalTab)
                    renderLocalFavorites();
            });

    // 游戏账户增删（添加角色、皮肤站同步等）时重拉角色卡片和账户计数
    if (SettingsManager::instance())
    {
        connect(SettingsManager::instance(), &SettingsManager::accountsChanged, this,
                &ForumAccountPage::reloadCharacters);
    }

    // 论坛账号状态
    connect(m_service, &ForumAccountService::userChanged, this, [this](const ForumUser &user) {
        Q_UNUSED(user);
        m_busy = false;
        if (m_authSubmitBtn)
        {
            m_authSubmitBtn->setEnabled(true);
            setAuthMode(m_authMode);
        }
        if (m_service->isLoggedIn())
            showOverview();
        renderProfile();
        renderContentArea();
        if (m_service->isLoggedIn())
        {
            m_threadsLoaded = m_repliesLoaded = m_bookmarksLoaded = false;
            m_myFeedbacksLoaded = false;
            m_pointsLoaded = false;
            m_pointsShopLoaded = false;
            m_myFeedbacks.clear();
            m_contentFavorites.clear();
            m_contentFavoritesLoaded = false;
            m_contentFavoriteTotal = 0;
            refreshBookmarkStat();
            loadTabData();
        }
    });

    connect(m_service, &ForumAccountService::sessionExpired, this, [this]() {
        m_threads = QJsonArray();
        m_replies = QJsonArray();
        m_bookmarks = QJsonArray();
        m_threadsLoaded = m_repliesLoaded = m_bookmarksLoaded = false;
        m_myFeedbacksLoaded = false;
        m_pointsLoaded = false;
        m_pointsShopLoaded = false;
        stopPayPolling();
        if (m_pointsShop)
            m_pointsShop->hide();
        m_myFeedbacks.clear();
        m_threadTotal = m_replyTotal = m_bookmarkTotal = 0;
        m_contentFavorites.clear();
        m_contentFavoritesLoaded = false;
        m_contentFavoriteTotal = 0;
        showOverview();
        renderProfile();
        renderContentArea();
        NotificationManager::showInfo(this, tr("论坛登录状态已失效，请重新登录"));
    });

    connect(m_service, &ForumAccountService::requestFailed, this,
            [this](const QString &message, int status) {
                m_busy = false;
                if (m_authSubmitBtn)
                {
                    m_authSubmitBtn->setEnabled(true);
                    setAuthMode(m_authMode);
                }
                if (status != 0 && m_viewStack && m_viewStack->currentIndex() == OverviewView)
                    NotificationManager::showError(this, message);
                else if (m_authError && m_viewStack && m_viewStack->currentIndex() == AuthView)
                {
                    m_authError->setText(message);
                    m_authError->show();
                }
                else
                    NotificationManager::showError(this, message);
            });

    connect(m_service, &ForumAccountService::requestSucceeded, this, [this](const QString &message) {
        m_busy = false;
        if (m_authError)
            m_authError->clear();
        NotificationManager::showSuccess(this, message);
    });

    // 邮箱验证码：发码成功后进入 60 秒重发冷却；成败都会刷新验证区按钮
    connect(m_service, &ForumAccountService::emailCodeSent, this, [this](int) {
        m_emailBusy = false;
        m_codeCooldown = 60;
        if (m_codeTimer)
            m_codeTimer->start();
        updateEmailVerifyUi();
        if (m_emailNote)
            m_emailNote->setText(tr("验证码已发送，10 分钟内有效；没收到的话看看垃圾邮件箱"));
    });
    connect(m_service, &ForumAccountService::requestFailed, this, [this](const QString &, int) {
        m_emailBusy = false;
        updateEmailVerifyUi();
    });

    // 注册 / 找回卡上的邮箱验证码：和账号设置里的那套互不干扰，各走各的倒计时
    connect(m_service, &ForumAccountService::registerEmailCodeSent, this, [this](int) {
        m_regEmailBusy = false;
        m_regCodeCooldown = 60;
        if (m_regCodeTimer)
            m_regCodeTimer->start();
        updateRegisterCodeUi();
        if (m_regCodeNote)
            m_regCodeNote->setText(tr("验证码已发送，10 分钟内有效；没收到就看看垃圾邮件箱"));
        if (m_regCodeEdit)
            m_regCodeEdit->setFocus();
    });
    // 找回账号发码成功：顺便告诉用户这个邮箱对应哪个账号（忘了用户名的也能对上号）
    connect(m_service, &ForumAccountService::recoverEmailCodeSent, this,
            [this](int, const QString &username) {
                m_regEmailBusy = false;
                m_regCodeCooldown = 60;
                if (m_regCodeTimer)
                    m_regCodeTimer->start();
                updateRegisterCodeUi();
                if (m_regCodeNote)
                {
                    m_regCodeNote->setText(
                        username.isEmpty()
                            ? tr("验证码已发送，10 分钟内有效；没收到就看看垃圾邮件箱")
                            : tr("验证码已发送，这个邮箱对应的账号是「%1」；10 分钟内有效。")
                                  .arg(username));
                }
                if (m_regCodeEdit)
                    m_regCodeEdit->setFocus();
            });
    connect(m_service, &ForumAccountService::requestFailed, this, [this](const QString &, int) {
        m_regEmailBusy = false;
        updateRegisterCodeUi();
    });
    // 论坛的注册规矩（要不要邮箱验证）拿到后，正在注册卡上就直接按新规矩换
    connect(m_service, &ForumAccountService::registerPolicyChanged, this,
            [this](bool emailRequired, bool) {
                m_registerEmailRequired = emailRequired;
                if (m_viewStack && m_viewStack->currentIndex() == AuthView)
                    setAuthMode(m_authMode);
            });
    connect(m_service, &ForumAccountService::userChanged, this, [this](const ForumUser &) {
        m_emailBusy = false;
        if (m_service->user().emailVerified && m_emailCodeEdit && !m_emailCodeEdit->text().isEmpty())
            m_emailCodeEdit->clear();
        updateEmailVerifyUi();
        updatePasswordUi();
        setOAuthWaiting(false);
        if (m_emailNote)
            m_emailNote->setText(tr("验证码发到上面填的邮箱，10 分钟内有效；没收到的话看看垃圾邮件箱"));
    });

    // 第三方登录：按钮是否显示、等待浏览器时的提示、失败原因都回到登录卡上
    connect(m_service, &ForumAccountService::oauthProvidersChanged, this,
            [this](const QJsonArray &providers) { renderOAuthProviders(providers); });
    connect(m_service, &ForumAccountService::oauthLoginStarted, this,
            [this](const QString &provider, const QString &url, const QString &, int) {
                m_oauthVerificationUrl = url;
                m_oauthProviderName =
                    provider == QLatin1String("littleskin") ? tr("LittleSkin") : tr("GitHub");
                setOAuthWaiting(true);
                if (!QDesktopServices::openUrl(QUrl(url)))
                {
                    if (m_oauthStatus)
                        m_oauthStatus->setText(
                            tr("没能自动打开浏览器，请点「重新打开浏览器」完成授权。"));
                }
            });
    connect(m_service, &ForumAccountService::oauthLoginSucceeded, this,
            [this](const ForumUser &user) {
                setOAuthWaiting(false);
                NotificationManager::showSuccess(
                    this, tr("已通过 %1 登录方块盒子论坛")
                              .arg(m_oauthProviderName.isEmpty() ? tr("第三方账号") : m_oauthProviderName));
                Q_UNUSED(user);
            });
    connect(m_service, &ForumAccountService::oauthLoginFailed, this,
            [this](const QString &message) {
                setOAuthWaiting(false);
                if (m_viewStack && m_viewStack->currentIndex() != AuthView)
                    return;
                if (m_authError)
                {
                    m_authError->setText(message);
                    m_authError->show();
                    syncAuthViewHeight();
                }
            });

    // 论坛地址 / 主题变化
    connect(SettingsManager::instance(), &SettingsManager::forumBaseUrlChanged, this,
            [this](const QString &) {
                if (m_service->hasSession())
                    refreshAccount();
            });

    if (ThemeManager::instance())
    {
        connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
                [this](ThemeManager::ThemeType) { applyTheme(); });
        connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this,
                [this](const QString &) { applyTheme(); });
    }
}

ForumAccountPage::~ForumAccountPage() = default;

QString ForumAccountPage::currentAccountName() const
{
    return m_service->isLoggedIn() ? m_service->user().displayName : tr("未登录");
}

void ForumAccountPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 每次进入页面都重新同步一次（论坛上的主题数、昵称可能已经变了）
    QTimer::singleShot(0, this, [this]() { refreshAccount(); });
    // 第一次进来先讲讲「角色 = 游戏账户」和「方块盒子账户」的区别
    scheduleAccountTour();
    // 之前在等充值到账、中途离开过：回到这一页继续轮询
    if (m_payPollTimer && !m_payOrderNo.isEmpty() && m_activeTab == PointsTab)
        m_payPollTimer->start();
}

void ForumAccountPage::hideEvent(QHideEvent *event)
{
    // 走到别的页面：引导遮罩跟着收掉（没看完的下次进来重讲）
    cancelAccountTour();
    // 离开账户页就别再等第三方授权了，免得用户在别的页面时这里突然登录成功
    m_service->cancelOAuthLogin();
    setOAuthWaiting(false);
    // 等充值的轮询也先停，回到这一页再续上
    if (m_payPollTimer)
        m_payPollTimer->stop();
    QWidget::hideEvent(event);
}

bool ForumAccountPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_detailSkinWidget && event->type() == QEvent::Resize)
        fitDetailCamera();

    // 登录卡内容变了（切到注册多一栏、冒出错误提示）：等这一轮布局走完，
    // 再按新高度收紧一次，避免卡片被压扁、输入框被裁。
    if (m_viewStack && watched == m_viewStack->widget(AuthView)
        && event->type() == QEvent::LayoutRequest)
    {
        QTimer::singleShot(0, this, &ForumAccountPage::syncAuthViewHeight);
    }
    return QWidget::eventFilter(watched, event);
}

void ForumAccountPage::fitDetailCamera()
{
    if (!m_detailSkinWidget || m_detailSkinWidget->height() <= 0)
        return;

    // 与首页皮肤面板同一套算法：fovY 40°，模型高 2.0、臂展 1.0（1 像素 = 1/16 单位）
    const qreal aspect = qreal(m_detailSkinWidget->width()) / qreal(m_detailSkinWidget->height());
    constexpr float kFovTan = 0.36397f;   // tan(20°)
    constexpr float kMargin = 1.10f;      // 模型外扩余量（与经典账户页观感一致）
    const float fitHeight = (2.0f * kMargin) / (2.0f * kFovTan);
    const float fitWidth = (1.0f * kMargin / float(aspect > 0 ? aspect : 1.0)) / (2.0f * kFovTan);
    m_detailSkinWidget->resetView(qMax(fitHeight, fitWidth) * 1.50f);
    // 稍微压低俯视角，正面感更强（默认 28.6° 俯视略陡）
    m_detailSkinWidget->rotateView(0.0f, -0.15f);
}

// ════════════════════════════════════════════════════════════════ 构建界面

void ForumAccountPage::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("fbScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:none;}"
                                           "QScrollArea > QWidget > QWidget{background:transparent;}"));
    rootLayout->addWidget(m_scroll);

    auto *content = new QWidget(m_scroll);
    content->setObjectName(QStringLiteral("fbContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 14, 24, 20);
    contentLayout->setSpacing(12);

    m_viewStack = new AdaptiveStack(content);
    m_viewStack->setObjectName(QStringLiteral("fbViewStack"));

    // 登录 / 注册视图上方的弹性留白：平时隐藏（不占高度），只在登录卡显示时
    // 与下方留白平分剩余空间，让卡片始终竖直居中。
    m_authTopSpacer = new QWidget(content);
    m_authTopSpacer->setObjectName(QStringLiteral("fbAuthTopSpacer"));
    contentLayout->addWidget(m_authTopSpacer, 1);
    m_authTopSpacer->hide();

    m_viewStack->addWidget(buildOverview(m_viewStack));
    QWidget *authView = buildAuthCard(m_viewStack);
    authView->installEventFilter(this);
    m_viewStack->addWidget(authView);
    m_viewStack->addWidget(buildAccountDetailView(m_viewStack));
    // 卡片按内容高度排布（和论坛网页一致），窗口更高时留白在下方，不把卡片拉长
    contentLayout->addWidget(m_viewStack, 0);
    contentLayout->addStretch(1);

    m_scroll->setWidget(content);
}

QWidget *ForumAccountPage::buildOverview(QWidget *parent)
{
    auto *overview = new QWidget(parent);
    auto *layout = new QVBoxLayout(overview);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    layout->addWidget(buildProfileCard(overview));
    layout->addWidget(buildTabsCard(overview), 0);

    return overview;
}

QWidget *ForumAccountPage::buildProfileCard(QWidget *parent)
{
    m_profileCard = new QFrame(parent);
    m_profileCard->setObjectName(QStringLiteral("fbProfileCard"));
    auto *outer = new QVBoxLayout(m_profileCard);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 封面横幅：贴在卡片顶部，顶部两角跟着卡片圆角一起裁
    m_coverBanner = new ForumKit::CoverBanner(m_profileCard);
    m_coverBanner->setAspectRatio(3.0);
    m_coverBanner->setRadius(16);
    outer->addWidget(m_coverBanner);

    auto *body = new QWidget(m_profileCard);
    auto *layout = new QHBoxLayout(body);
    layout->setContentsMargins(22, 18, 22, 20);
    layout->setSpacing(18);

    // 头像
    m_avatarLabel = new QLabel(body);
    m_avatarLabel->setFixedSize(kAvatarSize, kAvatarSize);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_avatarLabel, 0, Qt::AlignTop);

    // 资料
    auto *infoColumn = new QVBoxLayout();
    infoColumn->setContentsMargins(0, 0, 0, 0);
    infoColumn->setSpacing(2);

    auto *nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(8);
    m_nameLabel = new QLabel(tr("未登录"), body);
    m_nameLabel->setObjectName(QStringLiteral("fbName"));
    m_nameLabel->setWordWrap(true);
    nameRow->addWidget(m_nameLabel);

    m_roleChip = new QLabel(body);
    m_roleChip->setObjectName(QStringLiteral("fbChip"));
    m_roleChip->hide();
    nameRow->addWidget(m_roleChip);
    nameRow->addStretch();
    infoColumn->addLayout(nameRow);

    m_usernameLabel = new QLabel(body);
    m_usernameLabel->setObjectName(QStringLiteral("fbUsername"));
    m_usernameLabel->setWordWrap(true);
    infoColumn->addWidget(m_usernameLabel);

    m_bioBrowser = new ForumPostBrowser(body);
    m_bioBrowser->setObjectName(QStringLiteral("fbBioBrowser"));
    m_bioBrowser->setCompactMode(true);
    m_bioBrowser->hide();
    // 简介里点站内帖子 / 图片：内链在启动器里打开，图片弹大图
    connect(m_bioBrowser, &ForumPostBrowser::linkClicked, this,
            &ForumAccountPage::handleBioLink);
    connect(m_bioBrowser, &ForumPostBrowser::imageClicked, this,
            [](const QString &path) { ForumPostBrowser::showImagePreview(path); });
    infoColumn->addWidget(m_bioBrowser);

    infoColumn->addSpacing(10);

    // 数据行：主题 / 回复 / 收藏 / 加入 / 游戏账户
    auto *statRow = new QHBoxLayout();
    statRow->setContentsMargins(0, 0, 0, 0);
    statRow->setSpacing(22);

    const QStringList statLabels = {tr("主题"), tr("回复"), tr("收藏"), tr("方块"), tr("加入"),
                                    tr("游戏账户")};
    QList<QLabel **> statTargets = {&m_statThreads, &m_statPosts, &m_statBookmarks, &m_statPoints,
                                    &m_statJoined, &m_statGameAccounts};
    for (int i = 0; i < statLabels.size(); ++i)
    {
        auto *statWidget = new QWidget(body);
        auto *statLayout = new QVBoxLayout(statWidget);
        statLayout->setContentsMargins(0, 0, 0, 0);
        statLayout->setSpacing(0);

        auto *value = new QLabel(QStringLiteral("0"), statWidget);
        value->setObjectName(QStringLiteral("fbStatValue"));
        auto *caption = new QLabel(statLabels.at(i), statWidget);
        caption->setObjectName(QStringLiteral("fbStatLabel"));
        statLayout->addWidget(value);
        statLayout->addWidget(caption);

        *statTargets.at(i) = value;
        statRow->addWidget(statWidget);
    }
    statRow->addStretch();
    infoColumn->addLayout(statRow);

    layout->addLayout(infoColumn, 1);

    // 右侧操作
    auto *actionColumn = new QVBoxLayout();
    actionColumn->setContentsMargins(0, 0, 0, 0);
    actionColumn->setSpacing(8);
    actionColumn->setAlignment(Qt::AlignTop);

    m_profileAuthActions = new QWidget(body);
    auto *authActionsLayout = new QHBoxLayout(m_profileAuthActions);
    authActionsLayout->setContentsMargins(0, 0, 0, 0);
    authActionsLayout->setSpacing(8);

    auto *loginBtn = new QPushButton(tr("登录"), m_profileAuthActions);
    loginBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    loginBtn->setFixedHeight(36);
    loginBtn->setMinimumWidth(78);
    loginBtn->setCursor(Qt::PointingHandCursor);
    connect(loginBtn, &QPushButton::clicked, this, [this]() { openAuth(LoginMode); });
    authActionsLayout->addWidget(loginBtn);

    auto *registerBtn = new QPushButton(tr("注册"), m_profileAuthActions);
    registerBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
    registerBtn->setFixedHeight(36);
    registerBtn->setMinimumWidth(78);
    registerBtn->setCursor(Qt::PointingHandCursor);
    connect(registerBtn, &QPushButton::clicked, this, [this]() { openAuth(RegisterMode); });
    authActionsLayout->addWidget(registerBtn);

    actionColumn->addWidget(m_profileAuthActions, 0, Qt::AlignRight);

    m_profileUserActions = new QWidget(body);
    auto *userActionsLayout = new QHBoxLayout(m_profileUserActions);
    userActionsLayout->setContentsMargins(0, 0, 0, 0);
    userActionsLayout->setSpacing(8);

    // 皮肤站入口放在「编辑资料」左侧：论坛账号和皮肤站是同一个账号，
    // 点一下就在软件里整页打开，看衣柜、预览皮肤、一键穿上。
    auto *skinSiteBtn = new QPushButton(tr("皮肤站"), m_profileUserActions);
    skinSiteBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    skinSiteBtn->setFixedHeight(36);
    skinSiteBtn->setMinimumWidth(78);
    skinSiteBtn->setCursor(Qt::PointingHandCursor);
    connect(skinSiteBtn, &QPushButton::clicked, this, [this]() { emit skinStationRequested(); });
    userActionsLayout->addWidget(skinSiteBtn);

    auto *editBtn = new QPushButton(tr("编辑资料"), m_profileUserActions);
    editBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    editBtn->setFixedHeight(36);
    editBtn->setMinimumWidth(78);
    editBtn->setCursor(Qt::PointingHandCursor);
    connect(editBtn, &QPushButton::clicked, this, [this]() { setActiveTab(SettingsTab); });
    userActionsLayout->addWidget(editBtn);

    auto *creationBtn = new QPushButton(tr("创作中心"), m_profileUserActions);
    creationBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    creationBtn->setFixedHeight(36);
    creationBtn->setMinimumWidth(78);
    creationBtn->setCursor(Qt::PointingHandCursor);
    connect(creationBtn, &QPushButton::clicked, this, &ForumAccountPage::creationCenterRequested);
    userActionsLayout->addWidget(creationBtn);

    auto *logoutBtn = new QPushButton(tr("退出登录"), m_profileUserActions);
    logoutBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    logoutBtn->setFixedHeight(36);
    logoutBtn->setCursor(Qt::PointingHandCursor);
    connect(logoutBtn, &QPushButton::clicked, this, &ForumAccountPage::confirmLogout);
    userActionsLayout->addWidget(logoutBtn);

    m_profileUserActions->hide();
    actionColumn->addWidget(m_profileUserActions, 0, Qt::AlignRight);

    layout->addLayout(actionColumn);

    outer->addWidget(body);

    return m_profileCard;
}

QWidget *ForumAccountPage::buildTabsCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("fbCard"));
    m_tabsCard = card;
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 工具条：分段页签 + 提示（刷新统一走顶栏的刷新按钮）
    auto *toolbar = new QWidget(card);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(14, 12, 14, 12);
    toolbarLayout->setSpacing(10);

    auto *tabsTrack = new QWidget(toolbar);
    tabsTrack->setObjectName(QStringLiteral("fbTabsTrack"));
    auto *tabsLayout = new QHBoxLayout(tabsTrack);
    tabsLayout->setContentsMargins(4, 4, 4, 4);
    tabsLayout->setSpacing(4);

    const QStringList tabLabels = {tr("角色"), tr("我的主题"), tr("我的回复"), tr("我的收藏"),
                                   tr("通知"), tr("我的反馈"), tr("积分"), tr("勋章"),
                                   tr("游玩"), tr("账号设置")};
    for (int i = 0; i < tabLabels.size(); ++i)
    {
        auto *tab = new QPushButton(tabLabels.at(i), tabsTrack);
        tab->setObjectName(QStringLiteral("fbTab"));
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setChecked(i == 0);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFixedHeight(30);
        connect(tab, &QPushButton::clicked, this, [this, i]() { setActiveTab(TabIndex(i)); });
        tabsLayout->addWidget(tab);
        m_tabButtons.append(tab);
    }
    toolbarLayout->addWidget(tabsTrack);

    m_tabHint = new QLabel(toolbar);
    m_tabHint->setObjectName(QStringLiteral("fbNote"));
    toolbarLayout->addWidget(m_tabHint);
    toolbarLayout->addStretch();

    layout->addWidget(toolbar);
    layout->addWidget(makeDivider(card));

    // 内容页：手工换页，避免 QStackedWidget 用最长页面把卡片撑高
    m_contentHost = new QWidget(card);
    m_contentHost->setObjectName(QStringLiteral("fbContentHost"));
    m_contentLayout = new QVBoxLayout(m_contentHost);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);

    m_contentPages.append(buildCharactersContent(m_contentHost));                   // CharactersContent
    m_contentPages.append(buildListContent(m_contentHost, &m_threadListLayout));    // ThreadsContent
    m_contentPages.append(buildListContent(m_contentHost, &m_replyListLayout));     // RepliesContent
    m_contentPages.append(buildBookmarksContent(m_contentHost));                    // BookmarksContent
    m_contentPages.append(buildNotificationsContent(m_contentHost));                // NotificationsContent
    m_contentPages.append(buildFeedbackContent(m_contentHost));                     // FeedbackContent
    m_contentPages.append(buildPointsContent(m_contentHost));                       // PointsContent
    m_contentPages.append(buildMedalsContent(m_contentHost));                       // MedalsContent
    m_contentPages.append(buildPlaytimeContent(m_contentHost));                     // PlaytimeContent
    m_contentPages.append(buildSettingsContent(m_contentHost));                     // SettingsContent
    m_contentPages.append(buildLoggedOutContent(m_contentHost));                    // LoggedOutContent
    for (QWidget *page : m_contentPages)
        page->hide();

    layout->addWidget(m_contentHost, 0);

    return card;
}

QWidget *ForumAccountPage::buildListContent(QWidget *parent, QVBoxLayout **outLayout)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 12);
    layout->setSpacing(2);
    if (outLayout)
        *outLayout = layout;
    return page;
}

QWidget *ForumAccountPage::buildBookmarksContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 12);
    layout->setSpacing(2);

    // 子页切换：资源收藏（在线资源同步）/ 帖子收藏（论坛主题）
    auto *subTrack = new QWidget(page);
    subTrack->setObjectName(QStringLiteral("fbTabsTrack"));
    auto *subLayout = new QHBoxLayout(subTrack);
    subLayout->setContentsMargins(0, 0, 0, 6);
    subLayout->setSpacing(4);
    subLayout->addStretch();  // 让切换胶囊靠右，与上方页签条错开层次

    const QStringList subLabels = {tr("资源收藏"), tr("帖子收藏"), tr("本地收藏")};
    for (int i = 0; i < subLabels.size(); ++i)
    {
        auto *sub = new QPushButton(subLabels.at(i), subTrack);
        sub->setObjectName(QStringLiteral("fbTab"));
        sub->setCheckable(true);
        sub->setAutoExclusive(true);
        sub->setChecked(i == m_bookmarkSubTab);
        sub->setCursor(Qt::PointingHandCursor);
        sub->setFixedHeight(28);
        connect(sub, &QPushButton::clicked, this, [this, i]() { setBookmarkSubTab(i); });
        subLayout->addWidget(sub);
        m_bookmarkSubButtons.append(sub);
    }
    layout->addWidget(subTrack, 0, Qt::AlignRight);

    auto *listHost = new QWidget(page);
    m_bookmarkListLayout = new QVBoxLayout(listHost);
    m_bookmarkListLayout->setContentsMargins(0, 0, 0, 0);
    m_bookmarkListLayout->setSpacing(2);
    layout->addWidget(listHost);

    return page;
}

QWidget *ForumAccountPage::buildCharactersContent(QWidget *parent)
{
    m_charactersPage = new QWidget(parent);
    auto *layout = new QVBoxLayout(m_charactersPage);
    layout->setContentsMargins(14, 14, 14, 16);
    layout->setSpacing(10);

    // 顶部提示 + 「制作皮肤」/「添加角色」：账户的增删和皮肤都在这一页完成
    // 两个按钮做成纯文字按钮（无外框），和右侧页签条的轻量感保持一致
    auto *header = new QWidget(m_charactersPage);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);
    // 不用 makeMutedLabel：那个默认自动换行，配合右侧按钮会被压成两行
    auto *hint = new QLabel(tr("点卡片切换当前账户，双击看账户详情"), header);
    hint->setObjectName(QStringLiteral("fbNote"));
    headerLayout->addWidget(hint);
    headerLayout->addStretch();

    auto *skinBtn = new QPushButton(tr("制作皮肤"), header);
    skinBtn->setObjectName(QStringLiteral("fbBtnText"));
    skinBtn->setFixedHeight(30);
    skinBtn->setCursor(Qt::PointingHandCursor);
    skinBtn->setToolTip(tr("为当前角色制作或更换皮肤"));
    connect(skinBtn, &QPushButton::clicked, this, [this]() {
        QString accountName;
        QString accountType;
        if (SettingsManager::instance())
        {
            const QList<AccountInfo> accounts = SettingsManager::instance()->getAccounts();
            const AccountInfo *current = nullptr;
            for (const AccountInfo &account : accounts)
            {
                if (account.isDefault)
                {
                    current = &account;
                    break;
                }
            }
            if (!current && !accounts.isEmpty())
                current = &accounts.first();
            if (current)
            {
                accountName = current->username;
                accountType = accountTypeText(*current);
            }
        }
        emit skinEditorRequested(accountName, accountType);
    });
    headerLayout->addWidget(skinBtn);

    auto *addBtn = new QPushButton(tr("添加角色"), header);
    addBtn->setObjectName(QStringLiteral("fbBtnText"));
    addBtn->setFixedHeight(30);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setToolTip(tr("添加离线、微软正版或第三方账户"));
    connect(addBtn, &QPushButton::clicked, this, [this]() { emit addGameAccountRequested(); });
    headerLayout->addWidget(addBtn);

    layout->addWidget(header);

    // 瀑布流布局直接装在容器控件上（与内容列表页的用法一致）
    auto *container = new QWidget(m_charactersPage);
    container->setObjectName(QStringLiteral("fbCharContainer"));
    m_charactersLayout = new MasonryLayout(container, 0, 12, 12);
    layout->addWidget(container);
    layout->addStretch(1);

    return m_charactersPage;
}

QWidget *ForumAccountPage::buildLoggedOutContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    page->setMinimumHeight(220);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 24, 20, 24);
    layout->setSpacing(8);

    layout->addStretch();

    auto *title = new QLabel(tr("还没有登录方块盒子论坛"), page);
    title->setObjectName(QStringLiteral("fbSectionTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_loggedOutHint = makeMutedLabel(
        tr("登录后，你在论坛发布的主题、回复和收藏都会显示在这里，也能直接在启动器里改资料、换头像。"),
        page, "fbEmpty");
    m_loggedOutHint->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_loggedOutHint);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 8, 0, 0);
    buttonRow->setSpacing(10);
    buttonRow->addStretch();

    m_loggedOutLoginBtn = new QPushButton(tr("登录"), page);
    m_loggedOutLoginBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    m_loggedOutLoginBtn->setFixedHeight(36);
    m_loggedOutLoginBtn->setMinimumWidth(96);
    m_loggedOutLoginBtn->setCursor(Qt::PointingHandCursor);
    connect(m_loggedOutLoginBtn, &QPushButton::clicked, this, [this]() { openAuth(LoginMode); });
    buttonRow->addWidget(m_loggedOutLoginBtn);

    m_loggedOutRegisterBtn = new QPushButton(tr("注册新账号"), page);
    m_loggedOutRegisterBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
    m_loggedOutRegisterBtn->setFixedHeight(36);
    m_loggedOutRegisterBtn->setCursor(Qt::PointingHandCursor);
    connect(m_loggedOutRegisterBtn, &QPushButton::clicked, this, [this]() { openAuth(RegisterMode); });
    buttonRow->addWidget(m_loggedOutRegisterBtn);

    // 忘了密码 / 忘了用户名：从这里直接进找回账号卡，不用先去登录卡再翻页签
    auto *recoverLink = new QPushButton(tr("忘了密码？找回账号"), page);
    recoverLink->setObjectName(QStringLiteral("fbLink"));
    recoverLink->setCursor(Qt::PointingHandCursor);
    connect(recoverLink, &QPushButton::clicked, this, [this]() { openAuth(RecoverMode); });
    buttonRow->addWidget(recoverLink);

    buttonRow->addStretch();
    layout->addLayout(buttonRow);
    layout->addStretch();

    return page;
}

QWidget *ForumAccountPage::buildPlaytimeContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 嵌的是统计页的「摊平」形态：滚动交给账户页统一处理，不会出现两层滚动条
    m_playtimePage = new PlaytimeStatsPage(page, /*embedded=*/true);
    // 「登录并绑定」直接打开本页自带的登录表单，登录完回到「游玩」页
    connect(m_playtimePage, &PlaytimeStatsPage::loginRequested, this, [this]() {
        openAuth(LoginMode);
    });
    // 年度报告卡片：交给主窗口打开整页（报告是独立整页，返回落点是本页签）
    connect(m_playtimePage, &PlaytimeStatsPage::annualReportRequested, this,
            &ForumAccountPage::annualReportRequested);

    layout->addWidget(m_playtimePage);
    return page;
}
QWidget *ForumAccountPage::buildSettingsContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 14, 18, 18);
    layout->setSpacing(0);

    // ── 主页封面 ──
    auto *coverRow = new QHBoxLayout();
    coverRow->setContentsMargins(0, 0, 0, 0);
    coverRow->setSpacing(12);

    m_settingsCover = new ForumKit::CoverBanner(page);
    m_settingsCover->setFixedSize(150, 50);
    m_settingsCover->setRadius(8);
    coverRow->addWidget(m_settingsCover, 0, Qt::AlignVCenter);

    auto *coverColumn = new QVBoxLayout();
    coverColumn->setContentsMargins(0, 0, 0, 0);
    coverColumn->setSpacing(6);

    auto *coverHint = new QLabel(tr("主页封面：建议用 3:1 的横图，会显示在个人主页顶部；超过 3 MB 会自动压缩"), page);
    coverHint->setObjectName(QStringLiteral("fbNote"));
    coverHint->setWordWrap(true);
    coverColumn->addWidget(coverHint);

    auto *coverButtons = new QHBoxLayout();
    coverButtons->setContentsMargins(0, 0, 0, 0);
    coverButtons->setSpacing(8);

    auto *uploadCoverBtn = new QPushButton(tr("上传封面"), page);
    uploadCoverBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    uploadCoverBtn->setFixedHeight(28);
    uploadCoverBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    uploadCoverBtn->setCursor(Qt::PointingHandCursor);
    connect(uploadCoverBtn, &QPushButton::clicked, this, &ForumAccountPage::pickCoverFile);
    coverButtons->addWidget(uploadCoverBtn);

    auto *removeCoverBtn = new QPushButton(tr("删除封面"), page);
    removeCoverBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    removeCoverBtn->setFixedHeight(28);
    removeCoverBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    removeCoverBtn->setCursor(Qt::PointingHandCursor);
    connect(removeCoverBtn, &QPushButton::clicked, this, [this]() { m_service->removeCover(); });
    coverButtons->addWidget(removeCoverBtn);
    coverButtons->addStretch();

    coverColumn->addLayout(coverButtons);
    coverRow->addLayout(coverColumn, 1);

    layout->addLayout(coverRow);
    layout->addSpacing(14);

    // ── 头像 ──（结构与封面行一致：预览在左，提示在上、按钮在下）
    auto *avatarRow = new QHBoxLayout();
    avatarRow->setContentsMargins(0, 0, 0, 0);
    avatarRow->setSpacing(12);

    m_settingsAvatar = new QLabel(page);
    m_settingsAvatar->setObjectName(QStringLiteral("fbSettingsAvatar"));
    // 描边圈两侧各占 1px，头像本体仍是 kSettingsAvatarSize 的正方形
    m_settingsAvatar->setFixedSize(kSettingsAvatarSize + 2, kSettingsAvatarSize + 2);
    m_settingsAvatar->setAlignment(Qt::AlignCenter);
    avatarRow->addWidget(m_settingsAvatar, 0, Qt::AlignVCenter);

    auto *avatarColumn = new QVBoxLayout();
    avatarColumn->setContentsMargins(0, 0, 0, 0);
    avatarColumn->setSpacing(6);

    auto *avatarHint = new QLabel(tr("建议用正方形图片，超过 512 KB 会自动压缩后再上传"), page);
    avatarHint->setObjectName(QStringLiteral("fbNote"));
    avatarHint->setWordWrap(true);
    avatarColumn->addWidget(avatarHint);

    auto *avatarButtons = new QHBoxLayout();
    avatarButtons->setContentsMargins(0, 0, 0, 0);
    avatarButtons->setSpacing(8);

    auto *uploadBtn = new QPushButton(tr("上传新头像"), page);
    uploadBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    uploadBtn->setFixedHeight(28);
    uploadBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    uploadBtn->setCursor(Qt::PointingHandCursor);
    connect(uploadBtn, &QPushButton::clicked, this, &ForumAccountPage::pickAvatarFile);
    avatarButtons->addWidget(uploadBtn);

    auto *removeAvatarBtn = new QPushButton(tr("删除头像"), page);
    removeAvatarBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    removeAvatarBtn->setFixedHeight(28);
    removeAvatarBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    removeAvatarBtn->setCursor(Qt::PointingHandCursor);
    connect(removeAvatarBtn, &QPushButton::clicked, this,
            [this]() { m_service->removeAvatar(); });
    avatarButtons->addWidget(removeAvatarBtn);
    avatarButtons->addStretch();

    avatarColumn->addLayout(avatarButtons);
    avatarRow->addLayout(avatarColumn, 1);
    layout->addLayout(avatarRow);
    layout->addSpacing(14);

    // ── 资料 ──
    layout->addWidget(makeSectionHeader(tr("账号资料"), page));
    layout->addSpacing(10);

    m_displayNameEdit = new QLineEdit(page);
    m_displayNameEdit->setObjectName(QStringLiteral("fbInput"));
    m_displayNameEdit->setPlaceholderText(tr("别人看到的名字，最多 24 个字"));
    m_displayNameEdit->setMaxLength(24);
    layout->addWidget(buildField(tr("昵称"), m_displayNameEdit, page));
    layout->addSpacing(12);

    m_emailEdit = new QLineEdit(page);
    m_emailEdit->setObjectName(QStringLiteral("fbInput"));
    m_emailEdit->setPlaceholderText(tr("选填，方便找回账号"));
    m_emailVerifiedChip = new QLabel(page);
    m_emailVerifiedChip->setObjectName(QStringLiteral("fbChipMuted"));
    m_emailVerifiedChip->hide();
    layout->addWidget(buildField(tr("邮箱"), m_emailEdit, page, m_emailVerifiedChip));
    layout->addSpacing(12);

    // ── 邮箱验证码：填邮箱 → 发码 → 填码 → 验证，通过后绑定并标记已验证 ──
    m_emailCodeEdit = new QLineEdit(page);
    m_emailCodeEdit->setObjectName(QStringLiteral("fbInput"));
    m_emailCodeEdit->setPlaceholderText(tr("邮箱收到的 6 位验证码"));
    m_emailCodeEdit->setMaxLength(6);
    m_emailCodeEdit->setFixedHeight(36);   // 与右侧「发送 / 验证」按钮等高

    m_sendCodeButton = new QPushButton(tr("发送验证码"), page);
    m_sendCodeButton->setObjectName(QStringLiteral("fbBtnGhost"));
    m_sendCodeButton->setFixedHeight(36);
    m_sendCodeButton->setCursor(Qt::PointingHandCursor);
    connect(m_sendCodeButton, &QPushButton::clicked, this, &ForumAccountPage::sendEmailCode);

    m_verifyCodeButton = new QPushButton(tr("验证邮箱"), page);
    m_verifyCodeButton->setObjectName(QStringLiteral("fbBtnPrimary"));
    m_verifyCodeButton->setFixedHeight(36);
    m_verifyCodeButton->setCursor(Qt::PointingHandCursor);
    connect(m_verifyCodeButton, &QPushButton::clicked, this, &ForumAccountPage::verifyEmailCode);

    m_emailNote = new QLabel(tr("验证码发到上面填的邮箱，10 分钟内有效；没收到的话看看垃圾邮件箱"), page);
    m_emailNote->setObjectName(QStringLiteral("fbNote"));
    m_emailNote->setWordWrap(true);

    auto *codeRow = new QHBoxLayout();
    codeRow->setContentsMargins(0, 0, 0, 0);
    codeRow->setSpacing(8);
    codeRow->addWidget(m_emailCodeEdit, 1);
    codeRow->addWidget(m_sendCodeButton);
    codeRow->addWidget(m_verifyCodeButton);

    auto *codeHost = new QWidget(page);
    auto *codeHostLayout = new QVBoxLayout(codeHost);
    codeHostLayout->setContentsMargins(0, 0, 0, 0);
    codeHostLayout->setSpacing(6);
    codeHostLayout->addLayout(codeRow);
    codeHostLayout->addWidget(m_emailNote);

    layout->addWidget(buildField(tr("邮箱验证"), codeHost, page));
    layout->addSpacing(12);

    m_codeTimer = new QTimer(codeHost);
    m_codeTimer->setInterval(1000);
    connect(m_codeTimer, &QTimer::timeout, this, &ForumAccountPage::tickCodeCooldown);

    m_bioEdit = new QPlainTextEdit(page);
    m_bioEdit->setObjectName(QStringLiteral("fbInput"));
    m_bioEdit->setPlaceholderText(tr("介绍一下自己，最多 300 个字，支持 Markdown"));
    m_bioEdit->setFixedHeight(88);

    // 简介预览：和资料卡上的排版完全一致，边写边看
    m_bioPreview = new ForumPostBrowser(page);
    m_bioPreview->setObjectName(QStringLiteral("fbBioPreview"));
    m_bioPreview->setCompactMode(true);
    m_bioPreview->setMinimumHeight(88);
    connect(m_bioPreview, &ForumPostBrowser::linkClicked, this,
            &ForumAccountPage::handleBioLink);
    connect(m_bioPreview, &ForumPostBrowser::imageClicked, this,
            [](const QString &path) { ForumPostBrowser::showImagePreview(path); });
    m_bioPreview->hide();

    // 编辑框和预览叠在同一个位置，只显示其中一个（隐藏的那个不占高度）
    m_bioHost = new QWidget(page);
    m_bioHost->setObjectName(QStringLiteral("fbBioHost"));
    auto *bioHostLayout = new QVBoxLayout(m_bioHost);
    bioHostLayout->setContentsMargins(0, 0, 0, 0);
    bioHostLayout->setSpacing(0);
    bioHostLayout->addWidget(m_bioEdit);
    bioHostLayout->addWidget(m_bioPreview);

    m_bioPreviewButton = new QPushButton(tr("预览"), page);
    m_bioPreviewButton->setObjectName(QStringLiteral("fbBtnGhost"));
    m_bioPreviewButton->setCheckable(true);
    m_bioPreviewButton->setFixedHeight(26);
    m_bioPreviewButton->setCursor(Qt::PointingHandCursor);
    connect(m_bioPreviewButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked && m_bioPreview)
            m_bioPreview->setPostMarkdown(m_bioEdit->toPlainText().trimmed());
        m_bioPreviewButton->setText(checked ? tr("继续编辑") : tr("预览"));
        if (m_bioEdit)
            m_bioEdit->setVisible(!checked);
        if (m_bioPreview)
            m_bioPreview->setVisible(checked);
    });

    layout->addWidget(buildField(tr("个人简介"), m_bioHost, page, m_bioPreviewButton));

    auto *bioHint = new QLabel(
        tr("简介支持 Markdown：**加粗**、*斜体*、`代码`、- 列表、> 引用、[文字](网址)，"
           "换行原样保留"),
        page);
    bioHint->setObjectName(QStringLiteral("fbNote"));
    bioHint->setWordWrap(true);
    layout->addSpacing(4);
    layout->addWidget(bioHint);
    layout->addSpacing(14);

    auto *saveProfileBtn = new QPushButton(tr("保存资料"), page);
    saveProfileBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
    saveProfileBtn->setFixedHeight(36);
    saveProfileBtn->setMinimumWidth(108);
    saveProfileBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    saveProfileBtn->setCursor(Qt::PointingHandCursor);
    connect(saveProfileBtn, &QPushButton::clicked, this, &ForumAccountPage::saveProfileForm);

    auto *saveProfileRow = new QHBoxLayout();
    saveProfileRow->setContentsMargins(0, 0, 0, 0);
    saveProfileRow->addWidget(saveProfileBtn);
    saveProfileRow->addStretch();
    layout->addLayout(saveProfileRow);

    layout->addSpacing(18);
    layout->addWidget(makeDivider(page));
    layout->addSpacing(16);

    // ── 密码 ──
    layout->addWidget(makeSectionHeader(tr("修改密码"), page, &m_passwordSectionTitle));
    layout->addSpacing(10);

    m_currentPasswordEdit = new QLineEdit(page);
    m_currentPasswordEdit->setObjectName(QStringLiteral("fbInput"));
    m_currentPasswordEdit->setEchoMode(QLineEdit::Password);
    m_currentPasswordEdit->setPlaceholderText(tr("输入当前密码"));
    m_currentPasswordField = buildField(tr("当前密码"), m_currentPasswordEdit, page);
    layout->addWidget(m_currentPasswordField);
    layout->addSpacing(12);

    m_newPasswordEdit = new QLineEdit(page);
    m_newPasswordEdit->setObjectName(QStringLiteral("fbInput"));
    m_newPasswordEdit->setEchoMode(QLineEdit::Password);
    m_newPasswordEdit->setPlaceholderText(tr("至少 6 位"));
    layout->addWidget(buildField(tr("新密码"), m_newPasswordEdit, page));
    layout->addSpacing(12);

    m_confirmPasswordEdit = new QLineEdit(page);
    m_confirmPasswordEdit->setObjectName(QStringLiteral("fbInput"));
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit->setPlaceholderText(tr("再输一次新密码"));
    layout->addWidget(buildField(tr("确认新密码"), m_confirmPasswordEdit, page));
    layout->addSpacing(14);

    m_savePasswordBtn = new QPushButton(tr("修改密码"), page);
    m_savePasswordBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
    m_savePasswordBtn->setFixedHeight(36);
    m_savePasswordBtn->setMinimumWidth(108);
    m_savePasswordBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_savePasswordBtn->setCursor(Qt::PointingHandCursor);
    connect(m_savePasswordBtn, &QPushButton::clicked, this, &ForumAccountPage::savePasswordForm);

    auto *savePasswordRow = new QHBoxLayout();
    savePasswordRow->setContentsMargins(0, 0, 0, 0);
    savePasswordRow->addWidget(m_savePasswordBtn);
    savePasswordRow->addStretch();
    layout->addLayout(savePasswordRow);

    layout->addSpacing(18);
    layout->addWidget(makeDivider(page));
    layout->addSpacing(16);

    // ── 会话 ──
    layout->addWidget(makeSectionHeader(tr("登录状态"), page));
    layout->addSpacing(10);

    m_settingsAccountLine = new QLabel(page);
    m_settingsAccountLine->setObjectName(QStringLiteral("fbNote"));
    m_settingsAccountLine->setWordWrap(true);
    layout->addWidget(m_settingsAccountLine);
    layout->addSpacing(12);

    auto *logoutBtn = new QPushButton(tr("退出登录"), page);
    logoutBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    logoutBtn->setFixedHeight(36);
    logoutBtn->setMinimumWidth(108);
    logoutBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    logoutBtn->setCursor(Qt::PointingHandCursor);
    connect(logoutBtn, &QPushButton::clicked, this, &ForumAccountPage::confirmLogout);

    auto *logoutRow = new QHBoxLayout();
    logoutRow->setContentsMargins(0, 0, 0, 0);
    logoutRow->addWidget(logoutBtn);
    logoutRow->addStretch();
    layout->addLayout(logoutRow);

    layout->addSpacing(18);

    return page;
}

QWidget *ForumAccountPage::buildAuthCard(QWidget *parent)
{
    auto *view = new QWidget(parent);
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    outer->addStretch();

    auto *card = new QFrame(view);
    card->setObjectName(QStringLiteral("fbAuthCard"));
    card->setFixedWidth(430);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(26, 24, 26, 22);
    layout->setSpacing(0);

    m_authTitle = new QLabel(tr("登录方块盒子论坛"), card);
    m_authTitle->setObjectName(QStringLiteral("fbAuthTitle"));
    m_authTitle->setWordWrap(true);
    layout->addWidget(m_authTitle);

    layout->addSpacing(4);
    m_authSub = new QLabel(tr("用你注册的账号继续参与讨论。"), card);
    m_authSub->setObjectName(QStringLiteral("fbAuthSub"));
    m_authSub->setWordWrap(true);
    layout->addWidget(m_authSub);
    layout->addSpacing(18);

    // 登录 / 注册 切换
    auto *tabsTrack = new QWidget(card);
    tabsTrack->setObjectName(QStringLiteral("fbAuthTabs"));
    auto *tabsLayout = new QHBoxLayout(tabsTrack);
    tabsLayout->setContentsMargins(4, 4, 4, 4);
    tabsLayout->setSpacing(4);

    const QStringList authLabels = {tr("登录"), tr("注册新账号"), tr("找回账号")};
    for (int i = 0; i < authLabels.size(); ++i)
    {
        auto *tab = new QPushButton(authLabels.at(i), tabsTrack);
        tab->setObjectName(QStringLiteral("fbAuthTab"));
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setChecked(i == 0);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFixedHeight(34);
        connect(tab, &QPushButton::clicked, this, [this, i]() { setAuthMode(AuthMode(i)); });
        tabsLayout->addWidget(tab, 1);
        m_authTabButtons.append(tab);
    }
    layout->addWidget(tabsTrack);
    layout->addSpacing(16);

    m_authError = new QLabel(card);
    m_authError->setObjectName(QStringLiteral("fbError"));
    m_authError->setWordWrap(true);
    m_authError->hide();
    layout->addWidget(m_authError);
    layout->addSpacing(12);

    m_authNameEdit = new QLineEdit(card);
    m_authNameEdit->setObjectName(QStringLiteral("fbInput"));
    m_authNameEdit->setPlaceholderText(tr("中文、字母、数字，2–20 位"));
    m_authNameEdit->setMaxLength(20);
    // 用户名整栏包一层：找回账号只认邮箱，这一栏要能整块收起来
    m_authNameField = new QWidget(card);
    auto *nameLayout = new QVBoxLayout(m_authNameField);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(0);
    nameLayout->addWidget(buildField(tr("用户名"), m_authNameEdit, m_authNameField));
    layout->addWidget(m_authNameField);
    layout->addSpacing(14);

    m_authPasswordEdit = new QLineEdit(card);
    m_authPasswordEdit->setObjectName(QStringLiteral("fbInput"));
    m_authPasswordEdit->setEchoMode(QLineEdit::Password);
    m_authPasswordEdit->setPlaceholderText(tr("至少 6 位"));
    // 论坛服务端最多收 72 位，这里先挡住，免得到提交时才被打回
    m_authPasswordEdit->setMaxLength(72);
    layout->addWidget(
        buildField(tr("密码"), m_authPasswordEdit, card, nullptr, &m_authPasswordLabel));

    // ── 密码强度（只在注册时显示）──
    // 四段小条 + 右侧等级文字 + 一句建议，跟着密码框内容实时刷新
    m_pwStrengthField = new QWidget(card);
    {
        auto *strengthLayout = new QVBoxLayout(m_pwStrengthField);
        // 上边距放在自己身上：登录时整块隐藏，不会多留一段空白
        strengthLayout->setContentsMargins(0, 8, 0, 0);
        strengthLayout->setSpacing(6);

        auto *meterRow = new QHBoxLayout();
        meterRow->setContentsMargins(0, 0, 0, 0);
        meterRow->setSpacing(6);
        for (int i = 0; i < 4; ++i)
        {
            auto *seg = new QFrame(m_pwStrengthField);
            seg->setObjectName(QStringLiteral("fbPwSeg"));
            seg->setFixedHeight(5);
            seg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            seg->setProperty("on", false);
            seg->setProperty("tone", QStringLiteral("weak"));
            meterRow->addWidget(seg, 1);
            m_pwStrengthSegs.append(seg);
        }
        m_pwStrengthLabel = new QLabel(m_pwStrengthField);
        m_pwStrengthLabel->setObjectName(QStringLiteral("fbPwStrengthLabel"));
        m_pwStrengthLabel->setMinimumWidth(48);
        m_pwStrengthLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        meterRow->addWidget(m_pwStrengthLabel, 0);
        strengthLayout->addLayout(meterRow);

        m_pwStrengthNote = new QLabel(m_pwStrengthField);
        m_pwStrengthNote->setObjectName(QStringLiteral("fbPwStrengthNote"));
        m_pwStrengthNote->setWordWrap(true);
        strengthLayout->addWidget(m_pwStrengthNote);

        m_pwStrengthField->hide();
        layout->addWidget(m_pwStrengthField);
    }
    connect(m_authPasswordEdit, &QLineEdit::textChanged, this,
            &ForumAccountPage::updatePasswordStrengthUi);
    // 用户名也算进强度判断（拿用户名当密码会降一级），所以用户名变了也要刷新
    connect(m_authNameEdit, &QLineEdit::textChanged, this,
            &ForumAccountPage::updatePasswordStrengthUi);
    updatePasswordStrengthUi();
    layout->addSpacing(14);

    m_confirmField = new QWidget(card);
    auto *confirmLayout = new QVBoxLayout(m_confirmField);
    confirmLayout->setContentsMargins(0, 0, 0, 0);
    confirmLayout->setSpacing(0);
    m_authConfirmEdit = new QLineEdit(m_confirmField);
    m_authConfirmEdit->setObjectName(QStringLiteral("fbInput"));
    m_authConfirmEdit->setEchoMode(QLineEdit::Password);
    m_authConfirmEdit->setPlaceholderText(tr("再输一次"));
    confirmLayout->addWidget(buildField(tr("确认密码"), m_authConfirmEdit, m_confirmField));
    m_confirmField->hide();
    layout->addWidget(m_confirmField);
    layout->addSpacing(18);

    // ── 注册 / 找回共用：邮箱 + 邮箱验证码 ──
    // 注册时论坛要求邮箱验证，两栏只在「注册新账号」时露出来；找回账号时也复用这两栏
    m_regEmailField = new QWidget(card);
    auto *regEmailLayout = new QVBoxLayout(m_regEmailField);
    regEmailLayout->setContentsMargins(0, 0, 0, 0);
    regEmailLayout->setSpacing(0);
    m_regEmailEdit = new QLineEdit(m_regEmailField);
    m_regEmailEdit->setObjectName(QStringLiteral("fbInput"));
    m_regEmailEdit->setPlaceholderText(tr("要收验证码，请填常用邮箱"));
    m_regEmailEdit->setMaxLength(120);
    regEmailLayout->addWidget(buildField(tr("邮箱"), m_regEmailEdit, m_regEmailField));
    m_regEmailField->hide();
    layout->addWidget(m_regEmailField);
    layout->addSpacing(14);

    m_regCodeField = new QWidget(card);
    auto *regCodeLayout = new QVBoxLayout(m_regCodeField);
    regCodeLayout->setContentsMargins(0, 0, 0, 0);
    regCodeLayout->setSpacing(0);

    m_regCodeEdit = new QLineEdit(m_regCodeField);
    m_regCodeEdit->setObjectName(QStringLiteral("fbInput"));
    m_regCodeEdit->setPlaceholderText(tr("邮箱收到的 6 位验证码"));
    m_regCodeEdit->setMaxLength(6);
    m_regCodeEdit->setFixedHeight(36);   // 与右侧「发送验证码」按钮等高

    m_regSendCodeBtn = new QPushButton(tr("发送验证码"), m_regCodeField);
    m_regSendCodeBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    m_regSendCodeBtn->setFixedHeight(36);
    m_regSendCodeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_regSendCodeBtn, &QPushButton::clicked, this,
            &ForumAccountPage::sendAuthEmailCode);

    m_regCodeNote = new QLabel(tr("验证码 10 分钟内有效；没收到就看看垃圾邮件箱"), m_regCodeField);
    m_regCodeNote->setObjectName(QStringLiteral("fbNote"));
    m_regCodeNote->setWordWrap(true);

    auto *regCodeRow = new QHBoxLayout();
    regCodeRow->setContentsMargins(0, 0, 0, 0);
    regCodeRow->setSpacing(8);
    regCodeRow->addWidget(m_regCodeEdit, 1);
    regCodeRow->addWidget(m_regSendCodeBtn);

    auto *regCodeHost = new QWidget(m_regCodeField);
    auto *regCodeHostLayout = new QVBoxLayout(regCodeHost);
    regCodeHostLayout->setContentsMargins(0, 0, 0, 0);
    regCodeHostLayout->setSpacing(6);
    regCodeHostLayout->addLayout(regCodeRow);
    regCodeHostLayout->addWidget(m_regCodeNote);

    regCodeLayout->addWidget(buildField(tr("邮箱验证码"), regCodeHost, m_regCodeField));
    m_regCodeField->hide();
    layout->addWidget(m_regCodeField);

    // 找回账号时多一句说明：忘了用户名也能找回，验证码邮件里会写账号名
    m_recoverNote = new QLabel(tr("忘了用户名也不要紧：验证码邮件里会写明这个邮箱对应的账号。"),
                              card);
    m_recoverNote->setObjectName(QStringLiteral("fbNote"));
    m_recoverNote->setWordWrap(true);
    m_recoverNote->hide();
    layout->addWidget(m_recoverNote);
    layout->addSpacing(18);

    m_regCodeTimer = new QTimer(card);
    m_regCodeTimer->setInterval(1000);
    connect(m_regCodeTimer, &QTimer::timeout, this, &ForumAccountPage::tickRegisterCodeCooldown);

    m_authSubmitBtn = new QPushButton(tr("登录"), card);
    m_authSubmitBtn->setObjectName(QStringLiteral("fbPrimaryBlock"));
    m_authSubmitBtn->setFixedHeight(44);
    m_authSubmitBtn->setCursor(Qt::PointingHandCursor);
    connect(m_authSubmitBtn, &QPushButton::clicked, this, &ForumAccountPage::submitAuth);
    layout->addWidget(m_authSubmitBtn);

    // ── 手机扫码登录 ──
    // 放在密码登录按钮下面、第三方登录上面：同属「换个方式登录」，但只对
    // 已经在手机上登录过的账号有意义，所以只在登录模式露出来（见 setAuthMode）。
    m_qrLoginSection = new QWidget(card);
    {
        auto *qrLayout = new QVBoxLayout(m_qrLoginSection);
        // 上边距放在分区自己身上：注册 / 找回时整块隐藏，不会多留一段空白
        qrLayout->setContentsMargins(0, 14, 0, 0);
        qrLayout->setSpacing(8);

        m_qrLoginButton = new QPushButton(tr("手机扫码登录"), m_qrLoginSection);
        m_qrLoginButton->setObjectName(QStringLiteral("fbBtnGhost"));
        m_qrLoginButton->setFixedHeight(40);
        m_qrLoginButton->setCursor(Qt::PointingHandCursor);
        connect(m_qrLoginButton, &QPushButton::clicked, this,
                &ForumAccountPage::openQrLoginDialog);
        qrLayout->addWidget(m_qrLoginButton);

        m_qrLoginHint = new QLabel(tr("用手机端方块盒子扫一扫"), m_qrLoginSection);
        m_qrLoginHint->setObjectName(QStringLiteral("fbNote"));
        m_qrLoginHint->setAlignment(Qt::AlignCenter);
        m_qrLoginHint->setWordWrap(true);
        qrLayout->addWidget(m_qrLoginHint);
    }
    m_qrLoginSection->hide();
    layout->addWidget(m_qrLoginSection);

    // ── 第三方登录：GitHub / LittleSkin ──
    // 服务端配好对应 OAuth 应用后才会显示，没配就和以前完全一样。
    m_oauthSection = new QWidget(card);
    auto *oauthLayout = new QVBoxLayout(m_oauthSection);
    // 上边距放在分区自己身上：没有第三方登录时整块隐藏，不会留下多余空白
    oauthLayout->setContentsMargins(0, 14, 0, 0);
    oauthLayout->setSpacing(10);

    auto *oauthOrRow = new QHBoxLayout();
    oauthOrRow->setContentsMargins(0, 0, 0, 0);
    oauthOrRow->setSpacing(10);
    auto *oauthLine1 = new QFrame(m_oauthSection);
    oauthLine1->setObjectName(QStringLiteral("fbDivider"));
    oauthLine1->setFixedHeight(1);
    oauthLine1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *oauthOrLabel = new QLabel(tr("或使用第三方账号"), m_oauthSection);
    oauthOrLabel->setObjectName(QStringLiteral("fbNote"));
    auto *oauthLine2 = new QFrame(m_oauthSection);
    oauthLine2->setObjectName(QStringLiteral("fbDivider"));
    oauthLine2->setFixedHeight(1);
    oauthLine2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    oauthOrRow->addWidget(oauthLine1, 1);
    oauthOrRow->addWidget(oauthOrLabel, 0);
    oauthOrRow->addWidget(oauthLine2, 1);
    oauthLayout->addLayout(oauthOrRow);

    m_oauthGithubBtn = new QPushButton(tr("使用 GitHub 登录"), m_oauthSection);
    m_oauthGithubBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    m_oauthGithubBtn->setFixedHeight(40);
    m_oauthGithubBtn->setCursor(Qt::PointingHandCursor);
    connect(m_oauthGithubBtn, &QPushButton::clicked, this,
            [this]() { startOAuthLogin(QStringLiteral("github")); });
    oauthLayout->addWidget(m_oauthGithubBtn);

    m_oauthLittleSkinBtn = new QPushButton(tr("使用 LittleSkin 登录"), m_oauthSection);
    m_oauthLittleSkinBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    m_oauthLittleSkinBtn->setFixedHeight(40);
    m_oauthLittleSkinBtn->setCursor(Qt::PointingHandCursor);
    connect(m_oauthLittleSkinBtn, &QPushButton::clicked, this,
            [this]() { startOAuthLogin(QStringLiteral("littleskin")); });
    oauthLayout->addWidget(m_oauthLittleSkinBtn);

    auto *oauthHint = new QLabel(tr("无需手动注册：首次使用会自动创建方块盒子账号，已登录时会直接绑定。"),
                                 m_oauthSection);
    oauthHint->setObjectName(QStringLiteral("fbNote"));
    oauthHint->setWordWrap(true);
    oauthHint->setAlignment(Qt::AlignCenter);
    oauthLayout->addWidget(oauthHint);

    // 等待浏览器授权：先藏起来，点了第三方按钮才显示
    m_oauthWaiting = new QWidget(m_oauthSection);
    auto *waitLayout = new QVBoxLayout(m_oauthWaiting);
    waitLayout->setContentsMargins(0, 2, 0, 0);
    waitLayout->setSpacing(8);
    m_oauthStatus = new QLabel(m_oauthWaiting);
    m_oauthStatus->setObjectName(QStringLiteral("fbNote"));
    m_oauthStatus->setWordWrap(true);
    m_oauthStatus->setAlignment(Qt::AlignCenter);
    waitLayout->addWidget(m_oauthStatus);

    auto *waitRow = new QHBoxLayout();
    waitRow->setContentsMargins(0, 0, 0, 0);
    waitRow->addStretch();
    m_oauthReopenBtn = new QPushButton(tr("重新打开浏览器"), m_oauthWaiting);
    m_oauthReopenBtn->setObjectName(QStringLiteral("fbLink"));
    m_oauthReopenBtn->setCursor(Qt::PointingHandCursor);
    connect(m_oauthReopenBtn, &QPushButton::clicked, this, [this]() {
        if (!m_oauthVerificationUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(m_oauthVerificationUrl));
    });
    m_oauthCancelBtn = new QPushButton(tr("取消"), m_oauthWaiting);
    m_oauthCancelBtn->setObjectName(QStringLiteral("fbLink"));
    m_oauthCancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_oauthCancelBtn, &QPushButton::clicked, this, [this]() {
        m_service->cancelOAuthLogin();
        setOAuthWaiting(false);
    });
    waitRow->addWidget(m_oauthReopenBtn);
    waitRow->addWidget(m_oauthCancelBtn);
    waitRow->addStretch();
    waitLayout->addLayout(waitRow);
    m_oauthWaiting->hide();
    oauthLayout->addWidget(m_oauthWaiting);

    m_oauthSection->hide();
    layout->addWidget(m_oauthSection);

    layout->addSpacing(16);
    // 提交前需同意协议；这里只做提示与全文入口，真正提交前还会再弹窗确认一次
    m_authLegalNote = new QLabel(card);
    m_authLegalNote->setObjectName(QStringLiteral("fbNote"));
    m_authLegalNote->setWordWrap(true);
    m_authLegalNote->setAlignment(Qt::AlignCenter);
    m_authLegalNote->setTextFormat(Qt::RichText);
    m_authLegalNote->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_authLegalNote->setOpenExternalLinks(false);
    connect(m_authLegalNote, &QLabel::linkActivated, this, [](const QString &href) {
        if (href == QStringLiteral("terms"))
            UserAgreementDialog::showDocument(UserAgreementDialog::TermsOfUse);
        else if (href == QStringLiteral("privacy"))
            UserAgreementDialog::showDocument(UserAgreementDialog::PrivacyPolicy);
    });
    layout->addWidget(m_authLegalNote);

    layout->addSpacing(10);
    auto *backBtn = new QPushButton(tr("← 返回账户管理"), card);
    backBtn->setObjectName(QStringLiteral("fbLink"));
    backBtn->setCursor(Qt::PointingHandCursor);
    connect(backBtn, &QPushButton::clicked, this, &ForumAccountPage::onBackToAccountList);

    auto *backRow = new QHBoxLayout();
    backRow->setContentsMargins(0, 0, 0, 0);
    backRow->addStretch();
    backRow->addWidget(backBtn);
    backRow->addStretch();
    layout->addLayout(backRow);

    auto *cardRow = new QHBoxLayout();
    cardRow->setContentsMargins(0, 0, 0, 0);
    cardRow->addStretch();
    cardRow->addWidget(card);
    cardRow->addStretch();
    outer->addLayout(cardRow);
    outer->addStretch();

    return view;
}

// ════════════════════════════════════════════════════════════════ 主题

ForumAccountPage::Palette ForumAccountPage::currentPalette() const
{
    Palette p;
    p.dark = ThemeManager::instance()
             && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;

    if (p.dark)
    {
        p.bg = QStringLiteral("#0e1411");
        p.bgTint = QStringLiteral("#131b16");
        p.card = QStringLiteral("#151d18");
        p.line = QStringLiteral("#24312a");
        p.line2 = QStringLiteral("#2f4038");
        p.ink = QStringLiteral("#e7f0e9");
        p.ink2 = QStringLiteral("#a7baac");
        p.ink3 = QStringLiteral("#7d9184");
        p.brand = QStringLiteral("#66bb6a");
        p.brandHover = QStringLiteral("#7cc47f");
        p.brandSoft = QStringLiteral("rgba(102, 187, 106, 14%)");
        p.brandLine = QStringLiteral("rgba(102, 187, 106, 34%)");
        p.onBrand = QStringLiteral("#08210b");
        p.danger = QStringLiteral("#ef7c74");
        p.dangerSoft = QStringLiteral("rgba(198, 40, 40, 16%)");
        p.warn = QStringLiteral("#e0a94a");
        p.warnSoft = QStringLiteral("rgba(178, 106, 0, 16%)");
    }
    else
    {
        p.bg = QStringLiteral("#f6faf6");
        p.bgTint = QStringLiteral("#eef6ed");
        p.card = QStringLiteral("#ffffff");
        p.line = QStringLiteral("#e4ece3");
        p.line2 = QStringLiteral("#d3e3d2");
        p.ink = QStringLiteral("#17251a");
        p.ink2 = QStringLiteral("#52665a");
        p.ink3 = QStringLiteral("#87998b");
        p.brand = QStringLiteral("#2e7d32");
        p.brandHover = QStringLiteral("#388e3c");
        p.brandSoft = QStringLiteral("rgba(46, 125, 50, 10%)");
        p.brandLine = QStringLiteral("rgba(46, 125, 50, 24%)");
        p.onBrand = QStringLiteral("#ffffff");
        p.danger = QStringLiteral("#c62828");
        p.dangerSoft = QStringLiteral("#fdecea");
        p.warn = QStringLiteral("#b26a00");
        p.warnSoft = QStringLiteral("#fff5e6");
    }
    return p;
}

void ForumAccountPage::applyTheme()
{
    const Palette p = currentPalette();

    QString qss = QStringLiteral(R"QSS(
QWidget#fbRoot, QWidget#fbContent, QWidget#fbViewStack { background: transparent; }
QWidget#fbContentStack { background: transparent; }

QPushButton#fbBtnGhost {
    background: %CARD%;
    color: %INK2%;
    border: 1px solid %LINE2%;
    border-radius: 17px;
    padding: 0 14px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbBtnGhost:hover { border-color: %BRAND_LINE%; color: %BRAND%; }
QPushButton#fbBtnGhost:disabled { background: transparent; color: %INK3%; border-color: %LINE%; }
/* 纯文字按钮：没有外框，悬停时只铺一层品牌色浅底（角色页的「制作皮肤 / 添加角色」） */
QPushButton#fbBtnText {
    background: transparent;
    color: %BRAND%;
    border: none;
    border-radius: 15px;
    padding: 0 10px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbBtnText:hover { background: %BRAND_SOFT%; }
QPushButton#fbBtnText:pressed { background: %BRAND_LINE%; }
QPushButton#fbBtnPrimary {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: 1px solid transparent;
    border-radius: 17px;
    padding: 0 16px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbBtnPrimary:hover { background: %BRAND_HOVER%; }
QPushButton#fbBtnPrimary:disabled { background: %LINE2%; color: %INK3%; }
QPushButton#fbDangerGhost {
    background: transparent;
    color: %INK2%;
    border: 1px solid %LINE2%;
    border-radius: 17px;
    padding: 0 14px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbDangerGhost:hover { background: %DANGER_SOFT%; color: %DANGER%; border-color: transparent; }

QFrame#fbCard, QFrame#fbProfileCard {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 16px;
}
QFrame#fbAuthCard {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 16px;
}
QFrame#fbDivider { background: %LINE%; border: none; }

QFrame#fbCharCard {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 14px;
}
QFrame#fbCharCard:hover { border-color: %BRAND_LINE%; }
QFrame#fbCharCard[inUse="true"] { border-color: %BRAND%; }
QLabel#fbCharName { color: %INK%; font-size: 14px; font-weight: 800; }
QFrame#fbCharEmpty {
    background: %CARD%;
    border: 1px dashed %LINE2%;
    border-radius: 14px;
}

/* ── 账户详情页 ── */
QFrame#fbDetailPreview { background: transparent; border: none; }
QFrame#fbDetailPanel { background: %CARD%; border: 1px solid %LINE%; border-radius: 16px; }
QLabel#fbDetailName { color: %INK%; font-size: 19px; font-weight: 800; }
QLabel#fbDetailMeta { color: %INK3%; font-size: 12px; }
QPushButton#fbPoseButton {
    background: transparent;
    color: %INK2%;
    border: 1px solid %LINE2%;
    border-radius: 10px;
    padding: 0 8px;
    font-size: 12.5px;
    font-weight: 600;
}
QPushButton#fbPoseButton:hover { border-color: %BRAND_LINE%; color: %BRAND%; }
QPushButton#fbPoseButton:checked {
    background: %BRAND_SOFT%;
    border-color: %BRAND%;
    color: %BRAND%;
}
QPushButton#fbDetailUse {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: 1px solid transparent;
    border-radius: 19px;
    font-size: 13.5px;
    font-weight: 700;
}
QPushButton#fbDetailUse:hover { background: %BRAND_HOVER%; }
QPushButton#fbDetailUse:disabled { background: %LINE2%; color: %INK3%; }
QComboBox#fbDetailCombo {
    background: %CARD%;
    color: %INK%;
    border: 1px solid %LINE2%;
    border-radius: 10px;
    padding: 5px 10px;
    font-size: 12.5px;
}
QComboBox#fbDetailCombo QAbstractItemView {
    background: %CARD%;
    color: %INK%;
    border: 1px solid %LINE2%;
    selection-background-color: %BRAND_SOFT%;
    selection-color: %BRAND%;
}

QLabel#fbName { color: %INK%; font-size: 21px; font-weight: 800; }
QLabel#fbUsername { color: %INK3%; font-size: 13px; }
/* 简介（Markdown 渲染）：去掉文本浏览框自带的底色、边框和内边距，和资料卡融为一体 */
QTextBrowser#fbBioBrowser, QTextBrowser#fbBioPreview, QWidget#fbBioHost {
    background: transparent;
    border: none;
    padding: 0;
    color: %INK2%;
    font-size: 13.5px;
}
QTextBrowser#fbBioBrowser:hover, QTextBrowser#fbBioBrowser:focus,
QTextBrowser#fbBioPreview:hover, QTextBrowser#fbBioPreview:focus {
    background: transparent;
    border: none;
    padding: 0;
}
QLabel#fbStatValue { color: %INK%; font-size: 15px; font-weight: 800; }
QLabel#fbStatLabel { color: %INK3%; font-size: 12px; }
QLabel#fbSectionTitle { color: %INK%; font-size: 15px; font-weight: 800; }
QLabel#fbChip {
    background: %BRAND_SOFT%;
    color: %BRAND%;
    border-radius: 11px;
    padding: 2px 10px;
    font-size: 12px;
    font-weight: 700;
}
QLabel#fbChipMuted {
    background: transparent;
    color: %INK3%;
    border: 1px solid %LINE2%;
    border-radius: 11px;
    padding: 1px 9px;
    font-size: 12px;
    font-weight: 700;
}
QLabel#fbNote { color: %INK3%; font-size: 12.5px; }
QLabel#fbEmpty { color: %INK3%; font-size: 13.5px; }
QLabel#fbError {
    background: %DANGER_SOFT%;
    color: %DANGER%;
    border-radius: 10px;
    padding: 8px 12px;
    font-size: 12.5px;
}

QWidget#fbTabsTrack, QWidget#fbAuthTabs {
    background: %BG_TINT%;
    border-radius: 15px;
}
QPushButton#fbTab {
    background: transparent;
    color: %INK3%;
    border: 1px solid transparent;
    border-radius: 12px;
    padding: 0 14px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbTab:hover { color: %INK2%; }
QPushButton#fbTab:checked { background: %CARD%; color: %BRAND%; border-color: %LINE2%; }
QPushButton#fbAuthTab {
    background: transparent;
    color: %INK3%;
    border: 1px solid transparent;
    border-radius: 10px;
    font-size: 13.5px;
    font-weight: 700;
}
QPushButton#fbAuthTab:checked { background: %CARD%; color: %BRAND%; border-color: %LINE2%; }

/* ── 注册卡的密码强度：四段小条 + 右侧等级文字 ── */
QFrame#fbPwSeg {
    background: %LINE2%;
    border: none;
    border-radius: 3px;
}
QFrame#fbPwSeg[on="true"][tone="weak"] { background: %DANGER%; }
QFrame#fbPwSeg[on="true"][tone="fair"] { background: %WARN%; }
QFrame#fbPwSeg[on="true"][tone="good"] { background: %BRAND%; }
QFrame#fbPwSeg[on="true"][tone="great"] { background: %BRAND%; }
QLabel#fbPwStrengthLabel { color: %INK3%; font-size: 12px; font-weight: 800; }
QLabel#fbPwStrengthLabel[tone="weak"] { color: %DANGER%; }
QLabel#fbPwStrengthLabel[tone="fair"] { color: %WARN%; }
QLabel#fbPwStrengthLabel[tone="good"], QLabel#fbPwStrengthLabel[tone="great"] { color: %BRAND%; }
QLabel#fbPwStrengthNote { color: %INK3%; font-size: 12px; }

QFrame#fbListRow {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 10px;
}
QFrame#fbListRow:hover { background: %BG_TINT%; border-color: %LINE%; }
QLabel#fbRowTitle { color: %INK%; font-size: 14px; font-weight: 600; }
QLabel#fbRowMeta { color: %INK3%; font-size: 12px; }
QLabel#fbRowChip {
    background: %BRAND_SOFT%;
    color: %BRAND%;
    border-radius: 10px;
    padding: 1px 8px;
    font-size: 11.5px;
    font-weight: 700;
}

/* ── 通知页 ── */
QLabel#fbNotifUnreadChip {
    background: %BRAND_SOFT%;
    color: %BRAND%;
    border-radius: 10px;
    padding: 3px 10px;
    font-size: 12px;
    font-weight: 800;
}
QLabel#fbNotifGroup {
    color: %INK3%;
    font-size: 12px;
    font-weight: 800;
    padding: 2px 0 0 2px;
}
QFrame#fbNotifRow {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 12px;
}
QFrame#fbNotifRow:hover {
    background: %BG_TINT%;
    border-color: %BRAND_LINE%;
}
QFrame#fbNotifRow[unread="true"] {
    background: %BRAND_SOFT%;
    border-color: %BRAND_LINE%;
}
QFrame#fbNotifRow[unread="true"]:hover { border-color: %BRAND%; }
QFrame#fbNotifAccent {
    background: %BRAND%;
    border: none;
    border-radius: 2px;
}
QLabel#fbNotifIcon {
    background: %BG_TINT%;
    border: 1px solid transparent;
    border-radius: 12px;
}
QLabel#fbNotifIcon[type="mention"] { background: %BRAND_SOFT%; }
QLabel#fbNotifIcon[type="system"] { background: %WARN_SOFT%; }
QLabel#fbNotifDot {
    background: %BRAND%;
    border-radius: 3px;
}
QLabel#fbNotifTitle { color: %INK%; font-size: 14px; font-weight: 700; }
QLabel#fbNotifSub { color: %INK2%; font-size: 12.5px; }
QFrame#fbNotifQuote {
    background: %QUOTE_BG%;
    border: none;
    border-radius: 8px;
}
QFrame#fbNotifRow[unread="true"] QFrame#fbNotifQuote { background: %QUOTE_BG_ALT%; }
QLabel#fbNotifQuoteText { color: %INK2%; font-size: 12.5px; background: transparent; }
QLabel#fbNotifGo { color: %INK3%; font-size: 17px; font-weight: 700; }
QFrame#fbNotifRow:hover QLabel#fbNotifGo { color: %BRAND%; }
QLabel#fbNotifEmptyIcon {
    background: %BG_TINT%;
    border: 1px dashed %LINE2%;
    border-radius: 27px;
}
QLabel#fbNotifEmptyTitle { color: %INK2%; font-size: 14.5px; font-weight: 800; }
QFrame#fbNotifAlert {
    background: %DANGER_SOFT%;
    border: none;
    border-radius: 12px;
}
QLabel#fbNotifAlertText { color: %DANGER%; font-size: 12.5px; background: transparent; }
QLabel#fbNotifAlertMark {
    color: %DANGER%;
    font-size: 11px;
    font-weight: 900;
    border: 1px solid %DANGER%;
    border-radius: 8px;
    background: transparent;
}
QLabel#fbRowExcerpt { color: %INK2%; font-size: 12.5px; }

QLabel#fbFieldLabel { color: %INK2%; font-size: 13px; font-weight: 700; }
QLineEdit#fbInput, QPlainTextEdit#fbInput {
    background: %CARD%;
    color: %INK%;
    border: 1px solid %LINE2%;
    border-radius: 10px;
    padding: 8px 12px;
    font-size: 13.5px;
    selection-background-color: %BRAND%;
    selection-color: %ON_BRAND%;
}
QLineEdit#fbInput:hover, QPlainTextEdit#fbInput:hover { border-color: %BRAND_LINE%; }
QLineEdit#fbInput:focus, QPlainTextEdit#fbInput:focus {
    border-color: %BRAND%;
    background: %BG_TINT%;
}
QLineEdit#fbInput:disabled { color: %INK3%; }
/* 编辑页小节标题前的品牌色竖条 */
QFrame#fbSectionMark { background: %BRAND%; border-radius: 2px; }
/* 编辑页头像描边圈：圆环贴着圆形头像，没头像时露出淡色底 */
QLabel#fbSettingsAvatar {
    background: %BG_TINT%;
    border: 1px solid %LINE2%;
    border-radius: 29px;
}

QLabel#fbAuthTitle { color: %INK%; font-size: 21px; font-weight: 800; }
QLabel#fbAuthSub { color: %INK3%; font-size: 13px; }
QPushButton#fbPrimaryBlock {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: 1px solid transparent;
    border-radius: 22px;
    font-size: 14.5px;
    font-weight: 700;
}
QPushButton#fbPrimaryBlock:hover { background: %BRAND_HOVER%; }
QPushButton#fbPrimaryBlock:disabled { background: %LINE2%; color: %INK3%; }
QPushButton#fbLink {
    background: transparent;
    color: %BRAND%;
    border: none;
    font-size: 13px;
    font-weight: 600;
    padding: 2px;
}
QPushButton#fbLink:hover { color: %BRAND_HOVER%; }

/* ── 积分页（方块 / 每日签到） ── */
QFrame#fbPointsHero {
    background: %BG_TINT%;
    border: 1px solid %LINE%;
    border-radius: 14px;
}
QLabel#fbPointsEyebrow { color: %BRAND%; font-size: 12.5px; font-weight: 800; }
QLabel#fbPointsBalance { color: %INK%; font-size: 34px; font-weight: 900; }
QLabel#fbPointsStreak { color: %INK2%; font-size: 13px; }
QLabel#fbPointsReward { color: %INK3%; font-size: 12.5px; }
QPushButton#fbPointsCheckin {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: 1px solid transparent;
    border-radius: 19px;
    padding: 0 18px;
    font-size: 13.5px;
    font-weight: 800;
}
QPushButton#fbPointsCheckin:hover { background: %BRAND_HOVER%; }
QPushButton#fbPointsCheckin:disabled { background: %LINE2%; color: %INK3%; }
QFrame#fbPointsGiftCard {
    background: %CARD%;
    border: 1px dashed %BRAND_LINE%;
    border-radius: 12px;
}
QLabel#fbPointsGiftIcon { font-size: 20px; }
QLabel#fbPointsGiftText { color: %INK3%; font-size: 12px; }
QFrame#fbPointsWeek {
    background: %BG_TINT%;
    border: 1px solid %LINE%;
    border-radius: 14px;
}
QFrame#fbPointsDay {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 10px;
}
QFrame#fbPointsDay[checked="true"] {
    background: %BRAND_SOFT%;
    border-color: %BRAND_LINE%;
}
QFrame#fbPointsDay[current="true"] { border-color: %BRAND%; }
QLabel#fbPointsDayLabel { color: %INK3%; font-size: 11px; }
QLabel#fbPointsDayReward { color: %INK%; font-size: 16px; font-weight: 800; }
QLabel#fbPointsDayState { color: %INK3%; font-size: 11px; font-weight: 700; }
QFrame#fbPointsDay[checked="true"] QLabel#fbPointsDayReward,
QFrame#fbPointsDay[checked="true"] QLabel#fbPointsDayState { color: %BRAND%; }
QFrame#fbPointsMakeup {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 14px;
}
QLabel#fbPointsMakeupIcon { font-size: 20px; }
QLabel#fbPointsMakeupCount { color: %BRAND%; font-size: 12.5px; font-weight: 800; }
QFrame#fbPointsHistoryRow {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 10px;
}
QFrame#fbPointsHistoryRow:hover { background: %BG_TINT%; border-color: %LINE%; }
QLabel#fbPointsHistoryDay { color: %INK%; font-size: 13px; font-weight: 700; }
QLabel#fbPointsHistoryAmount { color: %BRAND%; font-size: 13px; font-weight: 800; }

/* ── 积分页 · 购买方块 ── */
QFrame#fbPointsShop {
    background: %BG_TINT%;
    border: 1px solid %LINE%;
    border-radius: 14px;
}
QFrame#fbShopCard {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 12px;
}
QFrame#fbShopCard:hover { border-color: %BRAND_LINE%; }
QLabel#fbShopName { color: %INK3%; font-size: 11.5px; font-weight: 700; }
QLabel#fbShopPoints { color: %INK%; font-size: 19px; font-weight: 900; }
QLabel#fbShopUnit { color: %INK3%; font-size: 10.5px; font-weight: 700; }
QLabel#fbShopPrice { color: %BRAND%; font-size: 13.5px; font-weight: 800; }
QPushButton#fbShopBuy {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: 1px solid transparent;
    border-radius: 14px;
    font-size: 12.5px;
    font-weight: 800;
}
QPushButton#fbShopBuy:hover { background: %BRAND_HOVER%; }
QPushButton#fbShopBuy:disabled { background: %LINE2%; color: %INK3%; }
QWidget#fbPointsPayPanel {
    background: %BRAND_SOFT%;
    border: 1px solid %BRAND_LINE%;
    border-radius: 12px;
}
QLabel#fbPointsPayTitle { color: %INK%; font-size: 13px; font-weight: 800; }

/* ── 勋章页（和账号绑定的勋章墙） ── */
QFrame#fbMedalsHero {
    background: %BG_TINT%;
    border: 1px solid %LINE%;
    border-radius: 14px;
}
QLabel#fbMedalsEyebrow { color: %BRAND%; font-size: 12.5px; font-weight: 800; }
QLabel#fbMedalsCount { color: %INK%; font-size: 34px; font-weight: 900; }
QLabel#fbMedalsTotal { color: %INK3%; font-size: 14px; font-weight: 700; }
QLabel#fbMedalsSummary { color: %INK2%; font-size: 12.5px; }
QLabel#fbMedalsNotice { color: %INK3%; font-size: 12px; }
QProgressBar#fbMedalsWallBar {
    background: %LINE2%;
    border: none;
    border-radius: 4px;
}
QProgressBar#fbMedalsWallBar::chunk {
    background: %BRAND%;
    border-radius: 4px;
}
QFrame#fbMedalTile {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 14px;
}
QFrame#fbMedalTile:hover { border-color: %BRAND_LINE%; }
QFrame#fbMedalTile[earned="false"] {
    background: %BG_TINT%;
    border-color: %LINE%;
}
QFrame#fbMedalTile[rarity="legendary"] { border-color: rgba(217, 119, 6, 38%); }
QFrame#fbMedalTile[rarity="mythic"] { border-color: rgba(220, 38, 38, 45%); }
QFrame#fbMedalTile[rarity="game"] { border-color: rgba(8, 145, 178, 38%); }
QLabel#fbMedalName { color: %INK%; font-size: 14px; font-weight: 800; }
QLabel#fbMedalCondition { color: %INK3%; font-size: 11.5px; }
QLabel#fbMedalEarned { font-size: 11.5px; font-weight: 700; }
QLabel#fbMedalProgress { color: %INK3%; font-size: 11px; font-weight: 700; }

/* ── 首次进入的聚光灯引导气泡 ── */
QFrame#fbTourCard {
    background: %CARD%;
    border: 1px solid %BRAND_LINE%;
    border-radius: 14px;
}
QLabel#fbTourStep { color: %BRAND%; font-size: 11.5px; font-weight: 800; }
QLabel#fbTourTitle { color: %INK%; font-size: 16px; font-weight: 800; }
QLabel#fbTourText { color: %INK2%; font-size: 13px; }
QPushButton#fbTourSkip {
    background: transparent;
    color: %INK3%;
    border: none;
    padding: 0 4px;
    font-size: 12.5px;
}
QPushButton#fbTourSkip:hover { color: %INK2%; }
QPushButton#fbTourNext {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: none;
    border-radius: 15px;
    padding: 0 16px;
    font-size: 12.5px;
    font-weight: 800;
}
QPushButton#fbTourNext:hover { background: %BRAND_HOVER%; }
)QSS");

    auto put = [&qss](const char *token, const QString &value) {
        qss.replace(QString::fromLatin1(token), value);
    };
    put("%BRAND_HOVER%", p.brandHover);
    put("%BRAND_SOFT%", p.brandSoft);
    put("%BRAND_LINE%", p.brandLine);
    put("%BRAND%", p.brand);
    put("%ON_BRAND%", p.onBrand);
    put("%BG_TINT%", p.bgTint);
    // 引用块底色：浅色主题用浅底，深色主题用一层淡淡的高光，避免比卡片更黑显得像洞
    put("%QUOTE_BG%", p.dark ? QStringLiteral("rgba(255, 255, 255, 6%)") : p.bgTint);
    // 未读行里的引用块：浅色用白卡提亮，深色同样用更亮的高光，保持“浮起来”的关系
    put("%QUOTE_BG_ALT%", p.dark ? QStringLiteral("rgba(255, 255, 255, 10%)") : p.card);
    put("%WARN_SOFT%", p.warnSoft);
    put("%WARN%", p.warn);
    put("%DANGER_SOFT%", p.dangerSoft);
    put("%DANGER%", p.danger);
    put("%INK3%", p.ink3);
    put("%INK2%", p.ink2);
    put("%INK%", p.ink);
    put("%LINE2%", p.line2);
    put("%LINE%", p.line);
    put("%CARD%", p.card);

    setStyleSheet(qss);

    // 首次引导遮罩：高亮边框和气泡箭头用的是手绘颜色，换主题要同步
    if (auto *tour = static_cast<AccountTourOverlay *>(m_tourOverlay))
        tour->setThemeColors(QColor(p.brand), QColor(p.card), QColor(p.line));

    // 登录卡底部的协议提示：链接颜色跟随主题，换主题后重新拼一次
    if (m_authLegalNote)
    {
        m_authLegalNote->setText(
            tr("<div align=\"center\">登录或注册前需同意 <a href=\"terms\" style=\"color:%1;"
               "text-decoration:none;\">《方块盒子使用协议》</a>和 <a href=\"privacy\" "
               "style=\"color:%1;text-decoration:none;\">《方块盒子隐私政策》</a>。</div>")
                .arg(p.brand));
    }

    // 头像占位色跟随主题
    if (!m_service->isLoggedIn())
        setAvatarPlaceholder(QString());
    else
        renderProfile();

    // 详情页的 3D 预览背景 / 模型动作图标也跟着主题走
    updateDetailBackground();
    refreshDetailPoseIcons();

    // 没有封面时封面横幅画的是主题色渐变，换主题要重画
    if (m_coverBanner)
        m_coverBanner->update();
    if (m_settingsCover)
        m_settingsCover->update();

    // 通知行的类型图标颜色（未读主题色/已读灰）跟着主题走，正在看通知页时重画
    if (m_activeTab == NotificationsTab && m_notificationsLoaded)
        renderNotifications();
}

// ════════════════════════════════════════════════════════════════ 视图

void ForumAccountPage::showOverview()
{
    if (!m_viewStack)
        return;
    // 离开登录卡就别再等第三方授权了（浏览器那端完成后也不会突然登录）
    if (m_viewStack->currentIndex() == AuthView)
    {
        m_service->cancelOAuthLogin();
        setOAuthWaiting(false);
    }
    // 总览 / 详情恢复自适应高度，之前给登录卡收紧的尺寸约束要放开
    releaseAuthViewHeight();
    // 详情页会把容器撑高，回到总览时恢复自适应
    m_viewStack->setMinimumHeight(0);
    m_viewStack->setCurrentIndex(OverviewView);
}

void ForumAccountPage::releaseAuthViewHeight()
{
    if (m_authTopSpacer)
        m_authTopSpacer->hide();
    if (m_viewStack)
        m_viewStack->setMaximumHeight(QWIDGETSIZE_MAX);
}

void ForumAccountPage::syncAuthViewHeight()
{
    if (!m_viewStack || m_viewStack->currentIndex() != AuthView)
        return;

    // 登录 / 注册卡只有几百像素高，但视图栈此前被总览页撑得很高，
    // 外层滚动区会按那个高度算最小尺寸，窗口稍矮就冒出竖向滚动条。
    // 这里按登录卡的实际高度收紧视图栈，窗口再矮也不用上下滚动。
    QWidget *authPage = m_viewStack->currentWidget();
    if (!authPage)
        return;
    const int needed = authPage->layout() ? authPage->layout()->totalSizeHint().height()
                                          : authPage->sizeHint().height();
    if (needed > 0 && m_viewStack->maximumHeight() != needed)
        m_viewStack->setMaximumHeight(needed);
    if (m_authTopSpacer && m_authTopSpacer->isHidden())
        m_authTopSpacer->show();
    m_viewStack->updateGeometry();
}

void ForumAccountPage::openAuth(AuthMode mode)
{
    m_service->cancelOAuthLogin();
    setOAuthWaiting(false);
    m_service->fetchOAuthProviders();
    // 顺便问一下论坛的注册规矩：要邮箱验证才显示注册卡上那两栏
    m_service->fetchRegisterPolicy();
    if (m_authError)
    {
        m_authError->clear();
        m_authError->hide();
    }
    m_authNameEdit->clear();
    m_authPasswordEdit->clear();
    m_authConfirmEdit->clear();
    if (m_regEmailEdit)
        m_regEmailEdit->clear();
    if (m_regCodeEdit)
        m_regCodeEdit->clear();
    // 重新进登录卡就重新开始：上一轮的倒计时和「发送中」不要带过来
    m_regCodeCooldown = 0;
    m_regEmailBusy = false;
    if (m_regCodeTimer)
        m_regCodeTimer->stop();
    if (m_regCodeNote)
        m_regCodeNote->setText(tr("验证码 10 分钟内有效；没收到就看看垃圾邮件箱"));
    setAuthMode(mode);
    if (m_viewStack)
        m_viewStack->setCurrentIndex(AuthView);
    syncAuthViewHeight();
    scrollToTop();
    QTimer::singleShot(0, this, [this]() {
        // 找回账号卡上没有用户名栏，焦点直接落在邮箱上
        if (m_authMode == RecoverMode)
        {
            if (m_regEmailEdit)
                m_regEmailEdit->setFocus();
            return;
        }
        m_authNameEdit->setFocus();
    });
}

void ForumAccountPage::setAuthMode(AuthMode mode)
{
    m_authMode = mode;
    const bool registering = (mode == RegisterMode);
    const bool recovering = (mode == RecoverMode);

    for (int i = 0; i < m_authTabButtons.size(); ++i)
        m_authTabButtons.at(i)->setChecked(i == int(mode));

    if (m_authTitle)
    {
        m_authTitle->setText(registering ? tr("注册方块盒子论坛")
                                         : recovering ? tr("找回方块盒子账号")
                                                      : tr("登录方块盒子论坛"));
    }
    if (m_authSub)
    {
        m_authSub->setText(
            registering
                ? (m_registerEmailRequired ? tr("取个名字、填个邮箱验证一下，之后就能发帖和回复了。")
                                           : tr("取个名字，之后就能发帖和回复了。"))
                : recovering
                      ? tr("忘了密码或忘了用户名都不要紧：用注册时填的邮箱收验证码，重设密码后自动登录。")
                      : tr("用你注册的账号继续参与讨论。"));
    }
    // 找回账号只认邮箱：用户名整栏收起来，密码栏当作「新密码」
    if (m_authNameField)
        m_authNameField->setVisible(!recovering);
    if (m_authPasswordLabel)
        m_authPasswordLabel->setText(recovering ? tr("新密码") : tr("密码"));
    if (m_authPasswordEdit)
    {
        m_authPasswordEdit->setPlaceholderText(recovering ? tr("设置新密码，至少 6 位")
                                                          : tr("至少 6 位"));
    }
    if (m_confirmField)
        m_confirmField->setVisible(registering || recovering);
    // 注册要邮箱验证时多出「邮箱 / 邮箱验证码」两栏；论坛没这项要求就收起来。
    // 找回账号不管论坛开没开邮箱注册，都必须走邮箱验证码
    const bool needEmail = (registering && m_registerEmailRequired) || recovering;
    if (m_regEmailField)
        m_regEmailField->setVisible(needEmail);
    if (m_regCodeField)
        m_regCodeField->setVisible(needEmail);
    if (m_recoverNote)
        m_recoverNote->setVisible(recovering);
    // 密码强度：注册和重设密码时都值得看，切回登录就收起来
    if (m_pwStrengthField)
        m_pwStrengthField->setVisible(registering || recovering);
    // 手机扫码登录只对「登录」有意义：注册要走邮箱验证、找回要重设密码，
    // 这两个模式下这个入口只会让人以为能绕过去
    if (m_qrLoginSection)
        m_qrLoginSection->setVisible(!registering && !recovering);
    updatePasswordStrengthUi();
    updateRegisterCodeUi();
    if (m_authSubmitBtn)
    {
        m_authSubmitBtn->setText(
            registering ? (m_busy ? tr("注册中…") : tr("注册并登录"))
                        : recovering ? (m_busy ? tr("重设中…") : tr("重设密码并登录"))
                                     : (m_busy ? tr("登录中…") : tr("登录")));
        m_authSubmitBtn->setEnabled(!m_busy && !m_oauthWaitingActive);
    }
    if (m_authError && !m_authError->text().isEmpty())
        m_authError->hide();
    // 切到注册 / 找回时多出「确认密码」「邮箱」「验证码」几栏、错误提示出现 / 消失
    // 都会改变卡片高度，收紧后的高度要跟着卡片重新算一次
    syncAuthViewHeight();
}

void ForumAccountPage::showAuthError(const QString &message)
{
    if (!m_authError)
        return;
    m_authError->setText(message);
    m_authError->show();
    // 错误提示会撑高卡片：同步放宽视图栈，别把提示或标签裁掉
    syncAuthViewHeight();
}

// ── 注册卡：密码强度 ──
// 只做「粗算」，目的不是评分而是给玩家一个直观反馈：
// 长度打底、字符种类加成，太短 / 单字符重复 / 连续递增 / 常见弱口令直接判弱。

int ForumAccountPage::passwordStrengthScore(const QString &password) const
{
    if (password.isEmpty())
        return 0;

    const int length = password.size();
    int lower = 0;
    int upper = 0;
    int digit = 0;
    int symbol = 0;
    for (const QChar ch : password)
    {
        if (ch.isLower())
            ++lower;
        else if (ch.isUpper())
            ++upper;
        else if (ch.isDigit())
            ++digit;
        else
            ++symbol;
    }
    const int classes = (lower > 0 ? 1 : 0) + (upper > 0 ? 1 : 0) + (digit > 0 ? 1 : 0)
                        + (symbol > 0 ? 1 : 0);

    // 同一个字符复制到底（aaaaaa）或整段等步长（123456 / abcdef）：再长也是白搭
    if (length < 6)
        return 1;
    bool repeated = true;
    for (int i = 1; i < length; ++i)
    {
        if (password.at(i) != password.at(0))
        {
            repeated = false;
            break;
        }
    }
    bool uniformStep = true;
    const int step = int(password.at(1).unicode()) - int(password.at(0).unicode());
    for (int i = 2; i < length; ++i)
    {
        if (int(password.at(i).unicode()) - int(password.at(i - 1).unicode()) != step)
        {
            uniformStep = false;
            break;
        }
    }
    if (repeated || uniformStep)
        return 1;

    // 论坛上最常被撞库的那些
    static const QSet<QString> weakPasswords = {
        QStringLiteral("123456"),    QStringLiteral("1234567"),    QStringLiteral("12345678"),
        QStringLiteral("123456789"), QStringLiteral("111111"),     QStringLiteral("000000"),
        QStringLiteral("666666"),    QStringLiteral("888888"),     QStringLiteral("123123"),
        QStringLiteral("abc123"),    QStringLiteral("abc123456"),  QStringLiteral("a123456"),
        QStringLiteral("qwerty"),    QStringLiteral("qwertyuiop"), QStringLiteral("password"),
        QStringLiteral("passw0rd"),  QStringLiteral("admin"),      QStringLiteral("admin123"),
        QStringLiteral("iloveyou"),  QStringLiteral("woaini"),     QStringLiteral("woaini1314"),
        QStringLiteral("5201314"),   QStringLiteral("!@#$%^&*"),
    };
    if (weakPasswords.contains(password.toLower()))
        return 1;

    // 长度打底，字符种类往上加
    int score = 1;
    if (length >= 8)
        score = 2;
    if (length >= 12)
        score = 3;
    if (length >= 16)
        score = 4;
    if (classes >= 3)
        ++score;
    if (classes >= 4)
        ++score;
    // 只用一种字符（纯数字 / 纯小写）最多算一般
    if (classes == 1)
        score = qMin(score, 2);
    // 拿用户名当密码、或在密码里带着用户名：降一级
    const QString name = m_authNameEdit ? m_authNameEdit->text().trimmed().toLower() : QString();
    if (name.size() >= 2 && password.toLower().contains(name))
        score = qMin(score, 2);

    return qBound(1, score, 4);
}

void ForumAccountPage::updatePasswordStrengthUi()
{
    if (!m_pwStrengthField || !m_pwStrengthLabel || !m_pwStrengthNote)
        return;

    const QString password = m_authPasswordEdit ? m_authPasswordEdit->text() : QString();
    const int score = passwordStrengthScore(password);

    static const char *const tones[] = {"weak", "fair", "good", "great"};
    const QString tone = score > 0 ? QString::fromLatin1(tones[score - 1])
                                   : QStringLiteral("empty");

    for (int i = 0; i < m_pwStrengthSegs.size(); ++i)
    {
        QFrame *seg = m_pwStrengthSegs.at(i);
        const bool on = (score > 0) && (i < score);
        if (seg->property("on").toBool() == on && seg->property("tone").toString() == tone)
            continue;
        seg->setProperty("on", on);
        seg->setProperty("tone", tone);
        // 动态属性变化不会自动重套样式表，这里手动刷一下，颜色才跟着换
        seg->style()->unpolish(seg);
        seg->style()->polish(seg);
        seg->update();
    }

    QString levelText;
    QString noteText;
    switch (score)
    {
    case 1:
        levelText = tr("弱");
        noteText = password.size() < 6
                       ? tr("还差 %1 位到 6 位底线，先加长一点。").arg(6 - password.size())
                       : tr("太简单了：再长一点，混上大写字母、数字和符号更安全。");
        break;
    case 2:
        levelText = tr("一般");
        noteText = tr("能用了，但不算牢靠：加到 12 位以上或加个符号会好很多。");
        break;
    case 3:
        levelText = tr("强");
        noteText = tr("已经不错了，再长一点就是很牢靠的密码。");
        break;
    case 4:
        levelText = tr("很强");
        noteText = tr("这个密码很牢靠，记得自己收好。");
        break;
    default:
        levelText = tr("强度");
        noteText = tr("长度至少 6 位，字母、数字和符号混着用更安全。");
        break;
    }

    const bool textChanged = m_pwStrengthLabel->text() != levelText
                             || m_pwStrengthNote->text() != noteText;
    m_pwStrengthLabel->setText(levelText);
    m_pwStrengthNote->setText(noteText);
    if (m_pwStrengthLabel->property("tone").toString() != tone)
    {
        m_pwStrengthLabel->setProperty("tone", tone);
        m_pwStrengthLabel->style()->unpolish(m_pwStrengthLabel);
        m_pwStrengthLabel->style()->polish(m_pwStrengthLabel);
        m_pwStrengthLabel->update();
    }
    // 换语言后提示可能变成两行：等这一轮布局走完，再把卡片高度重新收紧一次
    if (textChanged)
        QTimer::singleShot(0, this, [this]() { syncAuthViewHeight(); });
}

void ForumAccountPage::submitAuth()
{
    if (m_busy)
        return;

    const bool recovering = (m_authMode == RecoverMode);
    const QString username = m_authNameEdit->text().trimmed();
    const QString password = m_authPasswordEdit->text();

    auto showError = [this](const QString &message) {
        m_authError->setText(message);
        m_authError->show();
        // 错误提示会撑高卡片：同步放宽视图栈，别把提示或按钮裁掉
        syncAuthViewHeight();
    };

    // 找回账号只认邮箱，不用填用户名
    if (!recovering && username.isEmpty())
    {
        showError(tr("请输入用户名"));
        return;
    }
    if (password.length() < 6)
    {
        showError(tr("密码至少 6 位"));
        return;
    }
    if ((m_authMode == RegisterMode || recovering) && password != m_authConfirmEdit->text())
    {
        showError(tr("两次输入的密码不一样"));
        return;
    }

    // 注册必须把邮箱验证过，找回账号则完全靠邮箱：格式和 6 位验证码先在本地挡一道，少跑一趟服务器
    QString email;
    QString code;
    if ((m_authMode == RegisterMode && m_registerEmailRequired) || recovering)
    {
        static const QRegularExpression mailPattern(
            QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$"));
        email = m_regEmailEdit ? m_regEmailEdit->text().trimmed() : QString();
        code = m_regCodeEdit ? m_regCodeEdit->text().trimmed() : QString();
        if (!mailPattern.match(email).hasMatch())
        {
            showError(tr("请填写正确的邮箱地址"));
            return;
        }
        bool digits = code.length() == 6;
        for (const QChar ch : code)
        {
            if (!ch.isDigit())
            {
                digits = false;
                break;
            }
        }
        if (!digits)
        {
            showError(tr("请填邮箱收到的 6 位数字验证码，点「发送验证码」会发到上面填的邮箱"));
            return;
        }
    }

    // 登录 / 注册 / 找回前先确认使用协议与隐私政策；拒绝就停在这一步，不提交账号密码
    if (!UserAgreementDialog::ensureAccepted(UserAgreementDialog::ForumLogin, this))
    {
        showError(tr("需要同意《方块盒子使用协议》和《方块盒子隐私政策》才能登录论坛"));
        return;
    }

    m_authError->hide();
    m_busy = true;
    setAuthMode(m_authMode);

    if (m_authMode == RegisterMode)
        m_service->registerAccount(username, password, email, code);
    else if (m_authMode == RecoverMode)
        m_service->recoverAccount(email, code, password);
    else
        m_service->login(username, password);
}

// ── 登录 / 注册 / 找回卡：给邮箱发验证码 ──

void ForumAccountPage::sendAuthEmailCode()
{
    if (m_regEmailBusy || m_regCodeCooldown > 0 || !m_regSendCodeBtn)
        return;

    const QString email = m_regEmailEdit ? m_regEmailEdit->text().trimmed() : QString();
    static const QRegularExpression mailPattern(
        QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$"));
    if (!mailPattern.match(email).hasMatch())
    {
        showAuthError(tr("请先在邮箱那栏填一个正确的地址"));
        return;
    }

    if (m_authError)
        m_authError->hide();
    m_regEmailBusy = true;
    updateRegisterCodeUi();
    // 找回账号走「按邮箱找号」的发码接口，注册走注册专用的；两边共用一个倒计时
    if (m_authMode == RecoverMode)
        m_service->sendRecoverEmailCode(email);
    else
        m_service->sendRegisterEmailCode(email);
}

void ForumAccountPage::tickRegisterCodeCooldown()
{
    if (m_regCodeCooldown > 0)
        --m_regCodeCooldown;
    if (m_regCodeCooldown <= 0 && m_regCodeTimer)
        m_regCodeTimer->stop();
    updateRegisterCodeUi();
}

void ForumAccountPage::updateRegisterCodeUi()
{
    if (!m_regSendCodeBtn)
        return;

    if (m_regCodeEdit)
        m_regCodeEdit->setEnabled(!m_regEmailBusy);
    m_regSendCodeBtn->setEnabled(!m_regEmailBusy && m_regCodeCooldown <= 0);
    if (m_regEmailBusy)
        m_regSendCodeBtn->setText(tr("发送中…"));
    else if (m_regCodeCooldown > 0)
        m_regSendCodeBtn->setText(tr("重新发送(%1s)").arg(m_regCodeCooldown));
    else
        m_regSendCodeBtn->setText(tr("发送验证码"));
}

// ── 第三方登录（GitHub / LittleSkin） ──

void ForumAccountPage::renderOAuthProviders(const QJsonArray &providers)
{
    if (!m_oauthSection)
        return;

    bool github = false;
    bool littleskin = false;
    for (const auto &value : providers)
    {
        const QJsonObject item = value.toObject();
        if (!item.value(QStringLiteral("enabled")).toBool())
            continue;
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id == QLatin1String("github"))
            github = true;
        else if (id == QLatin1String("littleskin"))
            littleskin = true;
    }

    m_oauthGithubEnabled = github;
    m_oauthLittleSkinEnabled = littleskin;
    m_oauthHasProviders = github || littleskin;
    if (m_oauthGithubBtn)
        m_oauthGithubBtn->setVisible(github);
    if (m_oauthLittleSkinBtn)
        m_oauthLittleSkinBtn->setVisible(littleskin);

    if (!m_oauthHasProviders)
        m_service->cancelOAuthLogin();
    m_oauthSection->setVisible(m_oauthHasProviders && !m_service->oauthLoginBusy());
    if (!m_oauthHasProviders)
        setOAuthWaiting(false);
    syncAuthViewHeight();
}

void ForumAccountPage::startOAuthLogin(const QString &provider)
{
    if (m_busy || m_service->oauthLoginBusy())
        return;

    // 与账号密码登录共用同一份协议确认：拒绝就停在这一步，不打开浏览器
    if (!UserAgreementDialog::ensureAccepted(UserAgreementDialog::ForumLogin, this))
    {
        if (m_authError)
        {
            m_authError->setText(
                tr("需要同意《方块盒子使用协议》和《方块盒子隐私政策》才能登录论坛"));
            m_authError->show();
            syncAuthViewHeight();
        }
        return;
    }

    if (m_authError)
    {
        m_authError->clear();
        m_authError->hide();
    }

    m_oauthProviderName = provider == QLatin1String("littleskin") ? tr("LittleSkin") : tr("GitHub");
    m_oauthVerificationUrl.clear();

    // 先切到「正在打开浏览器」，设备码到手后 oauthLoginStarted 会补上地址
    setOAuthWaiting(true);
    if (m_oauthStatus)
        m_oauthStatus->setText(tr("正在打开浏览器…"));
    if (m_oauthReopenBtn)
        m_oauthReopenBtn->setEnabled(false);

    m_service->startOAuthLogin(provider);
}

void ForumAccountPage::setOAuthWaiting(bool waiting)
{
    if (!m_oauthSection)
        return;

    m_oauthWaitingActive = waiting;
    const bool busy = waiting;
    if (m_oauthGithubBtn)
        m_oauthGithubBtn->setEnabled(!busy);
    if (m_oauthLittleSkinBtn)
        m_oauthLittleSkinBtn->setEnabled(!busy);
    if (m_authSubmitBtn)
        m_authSubmitBtn->setEnabled(!busy && !m_busy);
    for (QPushButton *tab : m_authTabButtons)
        tab->setEnabled(!busy);
    if (m_authNameEdit)
        m_authNameEdit->setEnabled(!busy);
    if (m_authPasswordEdit)
        m_authPasswordEdit->setEnabled(!busy);
    if (m_authConfirmEdit)
        m_authConfirmEdit->setEnabled(!busy);

    if (!waiting)
    {
        if (m_oauthWaiting)
            m_oauthWaiting->hide();
        if (m_oauthGithubBtn)
            m_oauthGithubBtn->setVisible(m_oauthGithubEnabled);
        if (m_oauthLittleSkinBtn)
            m_oauthLittleSkinBtn->setVisible(m_oauthLittleSkinEnabled);
        m_oauthSection->setVisible(m_oauthHasProviders);
        syncAuthViewHeight();
        return;
    }

    m_oauthSection->setVisible(m_oauthHasProviders);
    if (m_oauthGithubBtn)
        m_oauthGithubBtn->hide();
    if (m_oauthLittleSkinBtn)
        m_oauthLittleSkinBtn->hide();
    if (m_oauthWaiting)
        m_oauthWaiting->show();
    if (m_oauthReopenBtn)
        m_oauthReopenBtn->setEnabled(!m_oauthVerificationUrl.isEmpty());
    if (m_oauthStatus && !m_oauthVerificationUrl.isEmpty())
    {
        m_oauthStatus->setText(
            tr("已在浏览器打开 %1 授权页。完成授权后会回到这里，请稍等一下；"
               "浏览器没有自动打开就点「重新打开浏览器」。")
                .arg(m_oauthProviderName.isEmpty() ? tr("第三方账号") : m_oauthProviderName));
    }
    syncAuthViewHeight();
}

// ── 手机扫码登录 ──

void ForumAccountPage::openQrLoginDialog()
{
    // 扫码登录同样是论坛登录：和密码 / 第三方登录共用同一份协议确认，
    // 没同意就停在错误提示上，不弹二维码
    if (!UserAgreementDialog::ensureAccepted(UserAgreementDialog::ForumLogin, this))
    {
        if (m_authError)
        {
            m_authError->setText(
                tr("需要同意《方块盒子使用协议》和《方块盒子隐私政策》才能登录论坛"));
            m_authError->show();
            syncAuthViewHeight();
        }
        return;
    }

    if (m_authError)
    {
        m_authError->clear();
        m_authError->hide();
    }

    // 用 exec() 模态：二维码的整个生命周期就是这一行，
    // 弹窗关掉后本页不需要再维护任何等待态（第三方登录的等待态也不受影响）
    QrLoginDialog dialog(this);
    dialog.exec();
}

void ForumAccountPage::updatePasswordUi()
{
    const bool hasPassword =
        !m_service->isLoggedIn() || m_service->user().passwordSet;
    if (m_passwordSectionTitle)
        m_passwordSectionTitle->setText(hasPassword ? tr("修改密码") : tr("设置密码"));
    if (m_currentPasswordField)
        m_currentPasswordField->setVisible(hasPassword);
    if (m_savePasswordBtn)
        m_savePasswordBtn->setText(hasPassword ? tr("修改密码") : tr("设置密码"));
    if (m_currentPasswordEdit)
    {
        m_currentPasswordEdit->setPlaceholderText(
            hasPassword ? tr("输入当前密码") : QString());
        if (!hasPassword)
            m_currentPasswordEdit->clear();
    }
}

void ForumAccountPage::setActiveTab(TabIndex tab)
{
    m_activeTab = tab;
    for (int i = 0; i < m_tabButtons.size(); ++i)
        m_tabButtons.at(i)->setChecked(i == int(tab));

    // 充值轮询只跟着「积分」页走：切走先停，切回来再续
    if (m_payPollTimer)
    {
        if (tab == PointsTab && !m_payOrderNo.isEmpty())
            m_payPollTimer->start();
        else
            m_payPollTimer->stop();
    }

    showOverview();
    renderContentArea();
    if (m_service->isLoggedIn())
        loadTabData();
}

bool ForumAccountPage::returnToOverview()
{
    if (!m_viewStack)
        return false;

    // 账户详情页：退回「角色」页
    if (m_viewStack->currentIndex() == DetailView)
    {
        setDetailPose(SkinPose::Idle);
        m_activeTab = CharactersTab;
        showOverview();
        renderContentArea();
        scrollToTop();
        emit accountManagePageOpened();
        return true;
    }

    if (m_viewStack->currentIndex() != AuthView)
        return false;

    showOverview();
    m_authPasswordEdit->clear();
    m_authConfirmEdit->clear();
    if (m_authError)
    {
        m_authError->clear();
        m_authError->hide();
    }
    scrollToTop();
    emit accountManagePageOpened();
    return true;
}

void ForumAccountPage::onBackToAccountList()
{
    if (returnToOverview())
        return;

    showOverview();
    renderContentArea();
    scrollToTop();
    emit accountManagePageOpened();
}

void ForumAccountPage::openNotificationsTab()
{
    // setActiveTab 内部会回到总览视图并触发内容区与数据加载
    setActiveTab(NotificationsTab);
    scrollToTop();
}

void ForumAccountPage::openPlaytimeTab()
{
    // 年度报告页返回、首页入口都走这里：切到「游玩」页签并回到页顶
    setActiveTab(PlaytimeTab);
    scrollToTop();
}

void ForumAccountPage::openLoginForm()
{
    showOverview();
    openAuth(LoginMode);
    scrollToTop();
}

void ForumAccountPage::refreshAccount()
{
    m_threadsLoaded = m_repliesLoaded = m_bookmarksLoaded = false;
    m_myFeedbacksLoaded = false;
    m_pointsLoaded = false;
    m_pointsState = QJsonObject();
    // 充值档位也跟着重拉一次（换了账号或重登后档位可能变了）
    m_pointsShopLoaded = false;
    stopPayPolling();
    if (m_pointsShopPayPanel)
        m_pointsShopPayPanel->hide();
    m_myFeedbacks.clear();
    m_threads = QJsonArray();
    m_replies = QJsonArray();
    m_bookmarks = QJsonArray();
    m_threadTotal = m_replyTotal = m_bookmarkTotal = 0;
    m_contentFavorites.clear();
    m_contentFavoritesLoaded = false;
    m_contentFavoriteTotal = 0;
    m_medals = QJsonArray();
    m_medalEarned = 0;
    m_medalTotal = 0;
    m_medalsLoaded = false;
    m_medalsBusy = false;
    m_gameAchievements = QJsonArray();
    m_gameAchievementTotal = 0;
    m_loadedAvatarPath.clear();

    m_service->refreshProfile();
    renderProfile();
    renderContentArea();

    if (m_service->isLoggedIn())
    {
        refreshBookmarkStat();
        loadTabData();
    }
}

void ForumAccountPage::scrollToTop()
{
    if (m_scroll && m_scroll->verticalScrollBar())
        m_scroll->verticalScrollBar()->setValue(0);
}

// ════════════════════════════════════════════════════════════════ 资料

void ForumAccountPage::renderProfile()
{
    const bool loggedIn = m_service->isLoggedIn();
    const ForumUser user = m_service->user();

    if (loggedIn)
    {
        m_nameLabel->setText(user.displayName);
        m_usernameLabel->setText(QStringLiteral("@") + user.username);

        const QString bio = user.bio.trimmed();
        if (bio.isEmpty())
        {
            m_bioBrowser->hide();
        }
        else
        {
            m_bioBrowser->setPostMarkdown(bio);
            m_bioBrowser->show();
        }

        QString roleText = tr("论坛成员");
        if (user.role == QStringLiteral("admin"))
            roleText = tr("管理员");
        else if (user.status == QStringLiteral("muted"))
            roleText = tr("已禁言");
        else if (user.status == QStringLiteral("banned"))
            roleText = tr("已封禁");
        m_roleChip->setText(roleText);
        m_roleChip->show();

        m_statThreads->setText(QString::number(user.threadCount));
        m_statPosts->setText(QString::number(user.postCount));
        m_statPoints->setText(QString::number(user.points));
        m_statJoined->setText(user.createdAt > 0
                                  ? QDateTime::fromSecsSinceEpoch(user.createdAt)
                                        .toString(QStringLiteral("yyyy-MM-dd"))
                                  : QStringLiteral("—"));

        m_profileAuthActions->hide();
        m_profileUserActions->show();

        if (m_coverBanner)
            m_coverBanner->setCover(user.cover);
        if (m_settingsCover)
            m_settingsCover->setCover(user.cover);
        loadRemoteAvatar(user.avatar);
    }
    else
    {
        m_nameLabel->setText(tr("未登录"));
        m_usernameLabel->setText(tr("登录方块盒子论坛账号，发帖、回复、收藏都会同步到这里"));
        m_bioBrowser->hide();
        m_roleChip->hide();

        m_statThreads->setText(QStringLiteral("0"));
        m_statPosts->setText(QStringLiteral("0"));
        m_statPoints->setText(QStringLiteral("0"));
        m_statJoined->setText(QStringLiteral("—"));

        m_profileAuthActions->show();
        m_profileUserActions->hide();

        if (m_coverBanner)
            m_coverBanner->setCover(QString());
        if (m_settingsCover)
            m_settingsCover->setCover(QString());
        setAvatarPlaceholder(QString());
        m_loadedAvatarPath.clear();
    }

    m_statBookmarks->setText(QString::number(m_bookmarkTotal));

    // 本地游戏账户数量（论坛账号之外的 Minecraft 账户在「角色」页管理）
    if (m_statGameAccounts && SettingsManager::instance())
    {
        const QList<AccountInfo> accounts = SettingsManager::instance()->getAccounts();
        m_statGameAccounts->setText(QString::number(accounts.count()));
    }

    if (m_settingsAvatar && m_loadedAvatarPath.isEmpty())
    {
        if (loggedIn)
            m_settingsAvatar->setPixmap(letterAvatar(user.displayName, kSettingsAvatarSize,
                                                     QColor(currentPalette().brand)));
        else
            m_settingsAvatar->setPixmap(guestAvatar(kSettingsAvatarSize,
                                                    QColor(currentPalette().brand)));
    }

    if (m_settingsAccountLine)
    {
        if (loggedIn)
        {
            QString roleText = (user.role == QStringLiteral("admin")) ? tr("管理员") : tr("普通成员");
            m_settingsAccountLine->setText(
                tr("当前登录：%1（@%2） · %3 · 注册于 %4")
                    .arg(user.displayName, user.username, roleText,
                         user.createdAt > 0
                             ? QDateTime::fromSecsSinceEpoch(user.createdAt)
                                   .toString(QStringLiteral("yyyy-MM-dd"))
                             : tr("未知")));
        }
        else
        {
            m_settingsAccountLine->setText(tr("当前未登录论坛账号。"));
        }
    }

    updatePasswordUi();
}

void ForumAccountPage::handleBioLink(const QUrl &url)
{
    // 简介里的站内帖子链接在启动器内打开；CF / Modrinth 资源页也留在软件里，
    // 其余链接交给系统浏览器
    auto *service = ForumAccountService::instance();
    QString text = url.toString();
    const QString base = service ? service->baseUrl() : QString();
    if (!base.isEmpty() && text.startsWith(base))
        text = text.mid(base.size());

    if (text.contains(QStringLiteral("thread.html")))
    {
        bool ok = false;
        const int id = QUrlQuery(QUrl(text)).queryItemValue(QStringLiteral("id")).toInt(&ok);
        if (ok && id > 0)
        {
            emit threadOpenRequested(id);
            return;
        }
    }
    LinkRouter::instance()->open(url, this);
}

void ForumAccountPage::setAvatarPlaceholder(const QString &name)
{
    if (!m_avatarLabel)
        return;

    // 没有真实头像时分享图也按昵称现画一个，别把上一张头像带过去
    m_avatarRaw = QPixmap();

    const QColor brand = QColor(currentPalette().brand);
    if (name.trimmed().isEmpty())
        m_avatarLabel->setPixmap(guestAvatar(kAvatarSize, brand));
    else
        m_avatarLabel->setPixmap(letterAvatar(name, kAvatarSize, brand));
}

void ForumAccountPage::setAvatarPixmap(const QPixmap &pixmap)
{
    if (!m_avatarLabel)
        return;

    const QPixmap rounded = roundPixmap(pixmap, kAvatarSize);
    if (rounded.isNull())
    {
        setAvatarPlaceholder(m_service->user().displayName);
        return;
    }
    m_avatarLabel->setPixmap(rounded);
    // 头像原图留一份：分享图按原图裁圆，比把 76px 小图放大清楚
    m_avatarRaw = pixmap;

    if (m_settingsAvatar)
        m_settingsAvatar->setPixmap(roundPixmap(pixmap, kSettingsAvatarSize));
}

void ForumAccountPage::loadRemoteAvatar(const QString &avatarPath)
{
    const QString displayName = m_service->user().displayName;

    if (avatarPath.isEmpty())
    {
        setAvatarPlaceholder(displayName);
        m_loadedAvatarPath.clear();
        if (m_settingsAvatar && m_service->isLoggedIn())
            m_settingsAvatar->setPixmap(letterAvatar(displayName, kSettingsAvatarSize,
                                                     QColor(currentPalette().brand)));
        return;
    }

    if (m_loadedAvatarPath == avatarPath)
        return;
    m_loadedAvatarPath = avatarPath;

    QNetworkRequest request{QUrl(m_service->absoluteUrl(avatarPath))};
    request.setTransferTimeout(12000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, avatarPath]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            if (m_loadedAvatarPath == avatarPath)
                m_loadedAvatarPath.clear();
            setAvatarPlaceholder(m_service->user().displayName);
            return;
        }

        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll()))
        {
            setAvatarPlaceholder(m_service->user().displayName);
            return;
        }
        setAvatarPixmap(pixmap);
    });
}

// ════════════════════════════════════════════════════════════════ 角色页（游戏账户）

QPixmap ForumAccountPage::renderCharacterCover(const QImage &skin, int width, int height)
{
    if (width <= 0 || height <= 0)
        return QPixmap();

    const Palette p = currentPalette();

    QPixmap cover(width, height);
    cover.fill(Qt::transparent);

    QPainter painter(&cover);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 顶部两角跟随卡片圆角，底部直角
    const qreal radius = 14.0;
    QPainterPath clip;
    clip.moveTo(0, height);
    clip.lineTo(0, radius);
    clip.quadTo(0, 0, radius, 0);
    clip.lineTo(width - radius, 0);
    clip.quadTo(width, 0, width, radius);
    clip.lineTo(width, height);
    clip.closeSubpath();
    painter.setClipPath(clip);

    // 品牌绿渐变底
    const QColor brand(p.brand);
    QLinearGradient bg(0, 0, width * 0.35, height);
    bg.setColorAt(0.0, brand.lighter(p.dark ? 118 : 174));
    bg.setColorAt(1.0, brand.darker(p.dark ? 158 : 110));
    painter.fillRect(QRectF(0, 0, width, height), bg);

    // 柔和光斑
    QRadialGradient glow(QPointF(width * 0.5, height * 0.26), width * 0.9);
    glow.setColorAt(0.0, QColor(255, 255, 255, p.dark ? 26 : 78));
    glow.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(QRectF(0, 0, width, height), glow);

    // 正面皮肤模型
    const QImage model = composeFrontModel(skin);
    if (!model.isNull())
    {
        const int maxWidth = qMax(16, int(width * 0.60));
        const int maxHeight = qMax(32, int(height * 0.82));
        const int scale = qMax(1, qMin(maxWidth / 16, maxHeight / 32));
        const QSize target(16 * scale, 32 * scale);

        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(QRect((width - target.width()) / 2, (height - target.height()) / 2,
                                 target.width(), target.height()),
                           QPixmap::fromImage(model));
    }

    // 底部轻微压暗，方便叠信息
    QLinearGradient shade(0, height * 0.70, 0, height);
    shade.setColorAt(0.0, QColor(0, 0, 0, 0));
    shade.setColorAt(1.0, QColor(0, 0, 0, p.dark ? 88 : 46));
    painter.fillRect(QRectF(0, height * 0.70, width, height * 0.30), shade);

    return cover;
}

QImage ForumAccountPage::skinForAccount(const QString &accountName, const QString &uuid)
{
    const auto cached = m_skinImages.constFind(accountName);
    if (cached != m_skinImages.constEnd() && !cached->isNull())
        return cached.value();

    QImage skin = SkinDownloader::loadSkinForAccount(accountName, uuid);
    if (skin.isNull())
        skin = SkinDownloader::getDefaultSkinForUser(accountName);
    return skin;
}

void ForumAccountPage::requestRemoteSkin(const QString &accountName, const QString &uuid,
                                         const QString &skinUrl)
{
    // 本地已有离线皮肤 / 下载缓存就不用再拉
    if (QFileInfo::exists(SkinDownloader::getOfflineSkinPath(accountName)))
        return;

    QString normalizedUuid = uuid;
    normalizedUuid.remove(QLatin1Char('-'));
    if (!normalizedUuid.isEmpty()
        && QFileInfo::exists(SkinDownloader::getCachePath(normalizedUuid)))
        return;

    QString url = skinUrl;
    if (url.isEmpty() && !normalizedUuid.isEmpty())
        url = QStringLiteral("https://crafatar.com/skins/") + normalizedUuid;
    if (url.isEmpty() || m_pendingSkinRequests.contains(accountName))
        return;

    m_pendingSkinRequests.insert(accountName);

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, accountName]() {
        reply->deleteLater();
        m_pendingSkinRequests.remove(accountName);

        if (reply->error() != QNetworkReply::NoError)
            return;

        QImage image;
        if (!image.loadFromData(reply->readAll()))
            return;

        m_skinImages.insert(accountName, image);

        QLabel *cover = m_characterCovers.value(accountName);
        const int height = m_characterCoverHeights.value(accountName, 0);
        if (cover && height > 0)
            cover->setPixmap(renderCharacterCover(image, qMax(1, cover->width()), height));
    });
}

void ForumAccountPage::reloadCharacters()
{
    if (!m_charactersLayout)
        return;

    // 清空旧卡片
    while (QLayoutItem *item = m_charactersLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_characterCovers.clear();
    m_characterCoverHeights.clear();

    if (!SettingsManager::instance())
        return;

    const QList<AccountInfo> accounts = SettingsManager::instance()->getAccounts();
    QWidget *host = m_charactersLayout->parentWidget();

    // 资料卡上的「游戏账户」数量一起刷新
    if (m_statGameAccounts)
        m_statGameAccounts->setText(QString::number(accounts.size()));

    if (accounts.isEmpty())
    {
        auto *empty = new QFrame(host);
        empty->setObjectName(QStringLiteral("fbCharEmpty"));
        empty->setMinimumWidth(360);
        auto *emptyLayout = new QVBoxLayout(empty);
        emptyLayout->setContentsMargins(26, 24, 26, 24);
        emptyLayout->setSpacing(8);

        auto *title = new QLabel(tr("还没有游戏账户"), empty);
        title->setObjectName(QStringLiteral("fbSectionTitle"));
        emptyLayout->addWidget(title);

        auto *hint = makeMutedLabel(
            tr("离线、微软正版和第三方账户都可以直接加进来；加好之后这里就能像卡片一样切换。"),
            empty, "fbEmpty");
        emptyLayout->addWidget(hint);

        auto *addBtn = new QPushButton(tr("添加角色"), empty);
        addBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
        addBtn->setFixedHeight(34);
        addBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        addBtn->setCursor(Qt::PointingHandCursor);
        connect(addBtn, &QPushButton::clicked, this,
                [this]() { emit addGameAccountRequested(); });
        emptyLayout->addWidget(addBtn);

        m_charactersLayout->addWidget(empty);
        setTabHint(tr("还没有游戏账户"));
        return;
    }

    // 当前账户：默认账户；没有默认就取第一个
    QString currentName;
    for (const AccountInfo &account : accounts)
    {
        if (account.isDefault)
        {
            currentName = account.username;
            break;
        }
    }
    if (currentName.isEmpty())
        currentName = accounts.first().username;

    for (const AccountInfo &account : accounts)
    {
        const bool inUse = (account.username == currentName);

        auto *card = new ClickableFrame(host);
        card->setObjectName(QStringLiteral("fbCharCard"));
        card->setFixedWidth(214);
        card->setProperty("inUse", inUse);

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);

        // 封面：所有卡片统一高度，整排对齐
        const int coverHeight = kCharacterCoverHeight;
        auto *cover = new QLabel(card);
        cover->setFixedHeight(coverHeight);
        cover->setAlignment(Qt::AlignCenter);
        cover->setPixmap(renderCharacterCover(skinForAccount(account.username, account.uuid), 214,
                                              coverHeight));
        cardLayout->addWidget(cover);
        m_characterCovers.insert(account.username, cover);
        m_characterCoverHeights.insert(account.username, coverHeight);

        // 信息区
        auto *body = new QWidget(card);
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(12, 10, 12, 12);
        bodyLayout->setSpacing(6);

        auto *nameLabel = new QLabel(account.username, body);
        nameLabel->setObjectName(QStringLiteral("fbCharName"));
        bodyLayout->addWidget(nameLabel);

        auto *metaRow = new QHBoxLayout();
        metaRow->setContentsMargins(0, 0, 0, 0);
        metaRow->setSpacing(6);

        auto *typeChip = new QLabel(accountTypeText(account), body);
        typeChip->setObjectName(QStringLiteral("fbRowChip"));
        metaRow->addWidget(typeChip);
        metaRow->addStretch();

        auto *stateLabel = new QLabel(inUse ? tr("使用中") : tr("点击切换"), body);
        stateLabel->setObjectName(inUse ? QStringLiteral("fbChip") : QStringLiteral("fbNote"));
        metaRow->addWidget(stateLabel);
        bodyLayout->addLayout(metaRow);

        cardLayout->addWidget(body);

        const QString accountName = account.username;
        card->onClick = [this, accountName]() { switchGameAccount(accountName); };
        card->onDoubleClick = [this, accountName]() { openAccountDetail(accountName); };
        card->setToolTip(tr("%1\n单击切换为该账户，双击查看账户详情").arg(accountName));

        m_charactersLayout->addWidget(card);

        // 本地没有皮肤就异步拉一张，刷新这张卡片
        requestRemoteSkin(account.username, account.uuid, account.skinUrl);
    }

    setTabHint(tr("共 %1 个游戏账户").arg(accounts.size()));
}

void ForumAccountPage::switchGameAccount(const QString &accountName)
{
    if (!SettingsManager::instance() || accountName.isEmpty())
        return;

    QString uuid;
    for (const AccountInfo &account : SettingsManager::instance()->getAccounts())
    {
        if (account.username == accountName)
        {
            uuid = account.uuid;
            break;
        }
    }

    SettingsManager::instance()->setDefaultAccount(accountName);
    reloadCharacters();

    const QImage skin = skinForAccount(accountName, uuid);
    QPixmap avatar;
    if (!skin.isNull())
        avatar = QPixmap::fromImage(SkinDownloader::cropHeadPortrait(skin, 64));

    emit gameAccountSelected(accountName, avatar);
    NotificationManager::showSuccess(this, tr("已切换到游戏账户：%1").arg(accountName));
}

// ════════════════════════════════════════════════════════════════ 账户详情页

QWidget *ForumAccountPage::buildAccountDetailView(QWidget *parent)
{
    m_detailPage = new QWidget(parent);
    m_detailPage->setFixedHeight(UiMetrics::fitToScreen(this, QSize(0, 700)).height());

    auto *outer = new QVBoxLayout(m_detailPage);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(10);

    // 返回按钮不再放这里：顶栏已经有返回键，重复一个反而乱

    auto *bodyRow = new QHBoxLayout();
    bodyRow->setContentsMargins(0, 0, 0, 0);
    bodyRow->setSpacing(12);

    // 左：3D 皮肤预览（不画底色，模型直接浮在页面背景上）
    auto *previewCard = new QWidget(m_detailPage);
    previewCard->setObjectName(QStringLiteral("fbDetailPreview"));
    previewCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);

    m_detailSkinWidget = new Skin3DWidget(previewCard);
    m_detailSkinWidget->setObjectName(QStringLiteral("fbDetailSkin"));
    m_detailSkinWidget->setMinimumSize(280, 430);
    m_detailSkinWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QSurfaceFormat glFormat = m_detailSkinWidget->format();
    glFormat.setAlphaBufferSize(8);
    m_detailSkinWidget->setFormat(glFormat);
    m_detailSkinWidget->installEventFilter(this);
    previewLayout->addWidget(m_detailSkinWidget);
    bodyRow->addWidget(previewCard, 1);

    // 右：账户资料 + 模型动作 + 操作（与经典账户管理页右侧一致）
    auto *panelCard = new QFrame(m_detailPage);
    panelCard->setObjectName(QStringLiteral("fbDetailPanel"));
    auto *panelLayout = new QVBoxLayout(panelCard);
    panelLayout->setContentsMargins(16, 16, 16, 16);
    panelLayout->setSpacing(10);

    m_detailName = new QLabel(panelCard);
    m_detailName->setObjectName(QStringLiteral("fbDetailName"));
    m_detailName->setWordWrap(true);
    panelLayout->addWidget(m_detailName);

    auto *typeRow = new QHBoxLayout();
    typeRow->setContentsMargins(0, 0, 0, 0);
    typeRow->setSpacing(6);

    m_detailTypeDot = new QLabel(panelCard);
    m_detailTypeDot->setFixedSize(8, 8);
    typeRow->addWidget(m_detailTypeDot, 0, Qt::AlignVCenter);

    m_detailTypeLabel = new QLabel(panelCard);
    m_detailTypeLabel->setObjectName(QStringLiteral("fbNote"));
    typeRow->addWidget(m_detailTypeLabel);

    m_detailDefaultChip = new QLabel(tr("默认账户"), panelCard);
    m_detailDefaultChip->setObjectName(QStringLiteral("fbChip"));
    m_detailDefaultChip->hide();
    typeRow->addWidget(m_detailDefaultChip);
    typeRow->addStretch();
    panelLayout->addLayout(typeRow);

    m_detailMeta = new QLabel(panelCard);
    m_detailMeta->setObjectName(QStringLiteral("fbDetailMeta"));
    m_detailMeta->setWordWrap(true);
    panelLayout->addWidget(m_detailMeta);

    panelLayout->addSpacing(4);
    auto *modelTitle = new QLabel(tr("模型动作"), panelCard);
    modelTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    panelLayout->addWidget(modelTitle);

    auto *poseGrid = new QGridLayout();
    poseGrid->setContentsMargins(0, 0, 0, 0);
    poseGrid->setHorizontalSpacing(6);
    poseGrid->setVerticalSpacing(6);

    const struct
    {
        QString text;
        const char *icon;
        const char *data;
    } poses[] = {
        {tr("待机"), ":/Images/Icons/pose_idle.svg", "pose_idle"},
        {tr("挥手"), ":/Images/Icons/pose_wave.svg", "pose_wave"},
        {tr("走路"), ":/Images/Icons/pose_walk.svg", "pose_walk"},
        {tr("跑步"), ":/Images/Icons/pose_run.svg", "pose_run"},
        {tr("潜行"), ":/Images/Icons/pose_sneak.svg", "pose_sneak"},
        {tr("游泳"), ":/Images/Icons/pose_swim.svg", "pose_swim"},
        {tr("睡觉"), ":/Images/Icons/pose_sleep.svg", "pose_sleep"},
        {tr("攻击"), ":/Images/Icons/pose_attack.svg", "pose_attack"},
        {tr("舞蹈"), ":/Images/Icons/pose_dance.svg", "pose_dance"},
        {tr("欢呼"), ":/Images/Icons/pose_cheer.svg", "pose_cheer"},
        {tr("骑马坐姿"), ":/Images/Icons/pose_ride.svg", "pose_ride"},
    };
    constexpr int kPoseCount = int(sizeof(poses) / sizeof(poses[0]));
    constexpr int kSpanIndex = kPoseCount - 1;

    for (int i = 0; i < kPoseCount; ++i)
    {
        auto *poseBtn = new QPushButton(poses[i].text, panelCard);
        poseBtn->setObjectName(QStringLiteral("fbPoseButton"));
        poseBtn->setProperty("iconPath", poses[i].icon);
        poseBtn->setProperty("poseData", poses[i].data);
        poseBtn->setIcon(IconHelper::loadColoredIcon(poses[i].icon, detailPoseIconColor(false), 16));
        poseBtn->setIconSize(QSize(16, 16));
        poseBtn->setCheckable(true);
        poseBtn->setChecked(i == 0);
        poseBtn->setFixedHeight(30);
        poseBtn->setCursor(Qt::PointingHandCursor);
        connect(poseBtn, &QPushButton::clicked, this, [this, poseBtn]() {
            const QString data = poseBtn->property("poseData").toString();
            SkinPose pose = SkinPose::Idle;
            if (data == QStringLiteral("pose_wave"))
                pose = SkinPose::Wave;
            else if (data == QStringLiteral("pose_walk"))
                pose = SkinPose::Walk;
            else if (data == QStringLiteral("pose_run"))
                pose = SkinPose::Run;
            else if (data == QStringLiteral("pose_sneak"))
                pose = SkinPose::Sneak;
            else if (data == QStringLiteral("pose_swim"))
                pose = SkinPose::Swim;
            else if (data == QStringLiteral("pose_sleep"))
                pose = SkinPose::Sleep;
            else if (data == QStringLiteral("pose_attack"))
                pose = SkinPose::Attack;
            else if (data == QStringLiteral("pose_dance"))
                pose = SkinPose::Dance;
            else if (data == QStringLiteral("pose_cheer"))
                pose = SkinPose::Cheer;
            else if (data == QStringLiteral("pose_ride"))
                pose = SkinPose::Ride;
            setDetailPose(pose);
        });
        m_detailPoseButtons.append(poseBtn);

        if (i == kSpanIndex)
            poseGrid->addWidget(poseBtn, i / 2, 0, 1, 2);
        else
            poseGrid->addWidget(poseBtn, i / 2, i % 2);
    }
    poseGrid->setColumnStretch(0, 1);
    poseGrid->setColumnStretch(1, 1);
    panelLayout->addLayout(poseGrid);

    // ── 皮肤与披风：离线账户选本地图片，皮肤站角色上传到皮肤站并立刻穿上 ──
    m_detailOutfitSection = new QWidget(panelCard);
    auto *outfitLayout = new QVBoxLayout(m_detailOutfitSection);
    outfitLayout->setContentsMargins(0, 0, 0, 0);
    outfitLayout->setSpacing(6);

    auto *outfitTitle = new QLabel(tr("皮肤与披风"), m_detailOutfitSection);
    outfitTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    outfitLayout->addWidget(outfitTitle);

    auto makeOutfitRow = [this](QWidget *host, const QString &name, QLabel **stateOut,
                                QPushButton **clearOut, QPushButton **pickOut,
                                const QString &pickTip,
                                void (ForumAccountPage::*pickSlot)(),
                                void (ForumAccountPage::*clearSlot)())
    {
        auto *row = new QWidget(host);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto *nameLabel = new QLabel(name, row);
        nameLabel->setObjectName(QStringLiteral("fbNote"));
        nameLabel->setFixedWidth(30);
        rowLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);

        auto *state = new QLabel(row);
        state->setObjectName(QStringLiteral("fbNote"));
        rowLayout->addWidget(state, 1, Qt::AlignVCenter);

        auto *pickBtn = new QPushButton(tr("选择图片"), row);
        pickBtn->setObjectName(QStringLiteral("fbBtnGhost"));
        pickBtn->setFixedHeight(26);
        pickBtn->setToolTip(pickTip);
        pickBtn->setCursor(Qt::PointingHandCursor);
        connect(pickBtn, &QPushButton::clicked, this, pickSlot);
        rowLayout->addWidget(pickBtn, 0);

        auto *clearBtn = new QPushButton(tr("清除"), row);
        clearBtn->setObjectName(QStringLiteral("fbBtnText"));
        clearBtn->setFixedHeight(26);
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->hide();
        connect(clearBtn, &QPushButton::clicked, this, clearSlot);
        rowLayout->addWidget(clearBtn, 0);

        *stateOut = state;
        *clearOut = clearBtn;
        *pickOut = pickBtn;
        return row;
    };

    outfitLayout->addWidget(makeOutfitRow(
        m_detailOutfitSection, tr("皮肤"), &m_detailSkinState, &m_detailSkinClearBtn, &m_detailSkinPickBtn,
        tr("选一张 64×32 或 64×64 的 PNG 皮肤图，只对这个离线账户生效"),
        &ForumAccountPage::pickDetailSkin, &ForumAccountPage::clearDetailSkin));
    outfitLayout->addWidget(makeOutfitRow(
        m_detailOutfitSection, tr("披风"), &m_detailCapeState, &m_detailCapeClearBtn, &m_detailCapePickBtn,
        tr("选一张 64×32 的 PNG 披风图，挂在角色背后"),
        &ForumAccountPage::pickDetailCape, &ForumAccountPage::clearDetailCape));

    m_detailOutfitSection->hide();
    panelLayout->addWidget(m_detailOutfitSection);

    panelLayout->addStretch();

    m_detailUseBtn = new QPushButton(tr("设为当前账户"), panelCard);
    m_detailUseBtn->setObjectName(QStringLiteral("fbDetailUse"));
    m_detailUseBtn->setFixedHeight(38);
    m_detailUseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailUseBtn, &QPushButton::clicked, this, &ForumAccountPage::useDetailAccountAsCurrent);
    panelLayout->addWidget(m_detailUseBtn);

    m_detailDeleteBtn = new QPushButton(tr("删除账户"), panelCard);
    m_detailDeleteBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    m_detailDeleteBtn->setFixedHeight(34);
    m_detailDeleteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailDeleteBtn, &QPushButton::clicked, this, &ForumAccountPage::deleteDetailAccount);
    panelLayout->addWidget(m_detailDeleteBtn);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(6);

    m_detailActionCombo = new QComboBox(panelCard);
    m_detailActionCombo->setObjectName(QStringLiteral("fbDetailCombo"));
    m_detailActionCombo->addItem(tr("复制用户名"), "copy_username");
    m_detailActionCombo->addItem(tr("复制 UUID"), "copy_uuid");
    m_detailActionCombo->addItem(tr("刷新皮肤"), "refresh_skin");
    m_detailActionCombo->addItem(tr("打开皮肤编辑器"), "open_skin_editor");
    m_detailActionCombo->setCursor(Qt::PointingHandCursor);
    actionRow->addWidget(m_detailActionCombo, 1);

    m_detailExecuteBtn = new QPushButton(tr("执行"), panelCard);
    m_detailExecuteBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    m_detailExecuteBtn->setFixedHeight(28);
    m_detailExecuteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailExecuteBtn, &QPushButton::clicked, this, &ForumAccountPage::executeDetailAction);
    actionRow->addWidget(m_detailExecuteBtn);
    panelLayout->addLayout(actionRow);

    // 右侧白卡直接铺满整列高度：不再用滚动容器，卡片下方不会露出页面底色
    panelCard->setFixedWidth(318);
    panelCard->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    bodyRow->addWidget(panelCard, 0);

    outer->addLayout(bodyRow, 1);

    return m_detailPage;
}

void ForumAccountPage::openAccountDetail(const QString &accountName)
{
    if (accountName.isEmpty())
        return;

    m_detailAccountName = accountName;
    // 换了个账户：皮肤站档案状态作废，等 refreshAccountDetail 里重新拉
    m_detailStationLoaded = false;
    m_detailStationMatch = false;
    m_detailStationSkinUrl.clear();
    m_detailStationCapeUrl.clear();
    refreshAccountDetail();
    // 详情页有独立的高度要求，先放开登录卡留下的收紧约束
    releaseAuthViewHeight();
    if (m_viewStack)
        // 详情页比总览高：先把容器撑到详情页高度，避免被上一页的尺寸裁掉
    m_viewStack->setMinimumHeight(m_detailPage ? m_detailPage->height() : 700);
    m_viewStack->setCurrentIndex(DetailView);
    setDetailPose(SkinPose::Idle);
    // 切换视图后尺寸才就绪，下一轮事件循环再按视口调整相机距离
    QTimer::singleShot(220, this, [this]() {
        if (m_viewStack && m_detailPage)
            m_viewStack->setMinimumHeight(m_detailPage->height());
        fitDetailCamera();
        if (m_viewStack)
            m_viewStack->updateGeometry();
    });
    scrollToTop();
    emit accountManagePageOpened();
}

void ForumAccountPage::refreshAccountDetail()
{
    if (!SettingsManager::instance() || m_detailAccountName.isEmpty())
        return;

    AccountInfo info;
    bool found = false;
    for (const AccountInfo &account : SettingsManager::instance()->getAccounts())
    {
        if (account.username == m_detailAccountName)
        {
            info = account;
            found = true;
            break;
        }
    }

    if (!found)
    {
        // 账户已经不存在（被删掉了）：退回角色页
        setActiveTab(CharactersTab);
        return;
    }

    m_detailAccountType = info.type;
    m_detailAccountUuid = info.uuid;
    m_detailAccountSkinUrl = info.skinUrl;
    m_detailAccountServerUrl = info.serverUrl;
    m_detailIsDefault = info.isDefault;

    if (m_detailName)
        m_detailName->setText(info.username);

    const QString typeText = accountTypeText(info);
    if (m_detailTypeLabel)
        m_detailTypeLabel->setText(typeText);
    if (m_detailTypeDot)
    {
        QColor dotColor(QStringLiteral("#F59E0B"));
        if (typeText.contains(tr("微软")))
            dotColor = QColor(QStringLiteral("#00A4EF"));
        else if (typeText.contains(tr("离线")))
            dotColor = QColor(QStringLiteral("#F59E0B"));
        else
            dotColor = QColor(QStringLiteral("#8B5CF6"));
        m_detailTypeDot->setStyleSheet(
            QStringLiteral("background-color:%1;border-radius:4px;").arg(dotColor.name()));
    }
    if (m_detailDefaultChip)
        m_detailDefaultChip->setVisible(info.isDefault);
    if (m_detailMeta)
    {
        QStringList parts;
        if (!info.uuid.isEmpty())
            parts << tr("UUID：%1").arg(info.uuid);
        if (!info.serverUrl.isEmpty())
            parts << tr("服务器：%1").arg(info.serverUrl);
        m_detailMeta->setText(parts.join(QStringLiteral("　")));
        m_detailMeta->setVisible(!parts.isEmpty());
    }
    if (m_detailUseBtn)
        m_detailUseBtn->setEnabled(!info.isDefault);

    // 皮肤：本地 / 缓存优先，缺了再异步拉
    QImage skin = SkinDownloader::loadSkinForAccount(info.username, info.uuid);
    if (skin.isNull())
        skin = SkinDownloader::getDefaultSkinForUser(info.username);
    if (m_detailSkinWidget && !skin.isNull())
        m_detailSkinWidget->setSkin(skin);
    requestDetailSkin(info.username, info.uuid, info.skinUrl);

    // 披风：离线账户的自定义披风（没有就保持无披风）；皮肤站角色等档案拉回来再挂
    if (m_detailSkinWidget)
    {
        QImage cape;
        if (detailAccountIsOffline()
            && SkinDownloader::loadOfflineCape(info.username, cape) && !cape.isNull())
            m_detailSkinWidget->setCape(cape);
        else
            m_detailSkinWidget->clearCape();
    }

    // 皮肤站角色：拉一次当前档案，同步状态文字和披风预览（皮肤变了也会对齐）
    if (detailAccountIsSkinStation())
        refreshStationProfile();

    updateDetailBackground();
    refreshDetailPoseIcons();
    refreshDetailOutfit();
}

void ForumAccountPage::requestDetailSkin(const QString &accountName, const QString &uuid,
                                         const QString &skinUrl)
{
    if (QFileInfo::exists(SkinDownloader::getOfflineSkinPath(accountName)))
        return;

    QString normalizedUuid = uuid;
    normalizedUuid.remove(QLatin1Char('-'));
    if (!normalizedUuid.isEmpty() && QFileInfo::exists(SkinDownloader::getCachePath(normalizedUuid)))
        return;

    QString url = skinUrl;
    if (url.isEmpty() && !normalizedUuid.isEmpty())
        url = QStringLiteral("https://crafatar.com/skins/") + normalizedUuid;
    if (url.isEmpty() || m_pendingSkinRequests.contains(QStringLiteral("detail:") + accountName))
        return;

    const QString key = QStringLiteral("detail:") + accountName;
    m_pendingSkinRequests.insert(key);

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, accountName, key]() {
        reply->deleteLater();
        m_pendingSkinRequests.remove(key);
        if (reply->error() != QNetworkReply::NoError)
            return;
        QImage image;
        if (!image.loadFromData(reply->readAll()))
            return;

        m_skinImages.insert(accountName, image);
        if (accountName == m_detailAccountName && m_detailSkinWidget)
            m_detailSkinWidget->setSkin(image);
        if (QLabel *cover = m_characterCovers.value(accountName))
        {
            const int height = m_characterCoverHeights.value(accountName, 0);
            if (height > 0)
                cover->setPixmap(renderCharacterCover(image, qMax(1, cover->width()), height));
        }
    });
}

void ForumAccountPage::setDetailPose(SkinPose pose, const QString &tip)
{
    m_detailPose = pose;
    if (m_detailSkinWidget)
        m_detailSkinWidget->setPose(pose);

    QString poseData;
    switch (pose)
    {
    case SkinPose::Idle:   poseData = "pose_idle";   break;
    case SkinPose::Wave:   poseData = "pose_wave";   break;
    case SkinPose::Walk:   poseData = "pose_walk";   break;
    case SkinPose::Run:    poseData = "pose_run";    break;
    case SkinPose::Sneak:  poseData = "pose_sneak";  break;
    case SkinPose::Swim:   poseData = "pose_swim";   break;
    case SkinPose::Sleep:  poseData = "pose_sleep";  break;
    case SkinPose::Attack: poseData = "pose_attack"; break;
    case SkinPose::Dance:  poseData = "pose_dance";  break;
    case SkinPose::Cheer:  poseData = "pose_cheer";  break;
    case SkinPose::Ride:   poseData = "pose_ride";   break;
    }

    for (QPushButton *btn : m_detailPoseButtons)
    {
        const bool checked = (btn->property("poseData").toString() == poseData);
        if (btn->isChecked() != checked)
            btn->setChecked(checked);
        btn->setIcon(IconHelper::loadColoredIcon(btn->property("iconPath").toString(),
                                        detailPoseIconColor(checked)));
    }

    if (!tip.isEmpty())
        NotificationManager::showSuccess(this, tip);
}

void ForumAccountPage::refreshDetailPoseIcons()
{
    for (QPushButton *btn : m_detailPoseButtons)
    {
        const QString iconPath = btn->property("iconPath").toString();
        if (!iconPath.isEmpty())
            btn->setIcon(IconHelper::loadColoredIcon(iconPath, detailPoseIconColor(btn->isChecked())));
    }
}

QColor ForumAccountPage::detailPoseIconColor(bool checked) const
{
    if (checked)
        return QColor(currentPalette().brand);
    return currentPalette().dark ? QColor(QStringLiteral("#9CA3AF"))
                                 : QColor(QStringLiteral("#666666"));
}

void ForumAccountPage::useDetailAccountAsCurrent()
{
    if (m_detailAccountName.isEmpty())
        return;

    switchGameAccount(m_detailAccountName);
    refreshAccountDetail();
}

void ForumAccountPage::deleteDetailAccount()
{
    if (m_detailAccountName.isEmpty() || !SettingsManager::instance())
        return;

    const QString name = m_detailAccountName;
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("确认删除"), tr("确定要删除游戏账户「%1」吗？").arg(name),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    SettingsManager::instance()->removeAccount(name);
    m_detailAccountName.clear();
    setDetailPose(SkinPose::Idle);
    setActiveTab(CharactersTab);
    NotificationManager::showSuccess(this, tr("已删除游戏账户：%1").arg(name));
}

void ForumAccountPage::executeDetailAction()
{
    if (!m_detailActionCombo || m_detailAccountName.isEmpty())
        return;

    const QString action = m_detailActionCombo->currentData().toString();

    if (action == QStringLiteral("copy_username"))
    {
        QApplication::clipboard()->setText(m_detailAccountName);
        NotificationManager::showSuccess(this, tr("已复制用户名：%1").arg(m_detailAccountName));
    }
    else if (action == QStringLiteral("copy_uuid"))
    {
        if (m_detailAccountUuid.isEmpty())
        {
            NotificationManager::showError(this, tr("当前账户没有 UUID"));
            return;
        }
        QApplication::clipboard()->setText(m_detailAccountUuid);
        NotificationManager::showSuccess(this, tr("已复制 UUID：%1").arg(m_detailAccountUuid));
    }
    else if (action == QStringLiteral("refresh_skin"))
    {
        m_skinImages.remove(m_detailAccountName);
        QImage skin = SkinDownloader::loadSkinForAccount(m_detailAccountName, m_detailAccountUuid);
        if (skin.isNull())
            skin = SkinDownloader::getDefaultSkinForUser(m_detailAccountName);
        if (m_detailSkinWidget && !skin.isNull())
            m_detailSkinWidget->setSkin(skin);
        requestDetailSkin(m_detailAccountName, m_detailAccountUuid, m_detailAccountSkinUrl);
        NotificationManager::showSuccess(this, tr("已刷新皮肤"));
    }
    else if (action == QStringLiteral("open_skin_editor"))
    {
        emit skinEditorRequested(m_detailAccountName, m_detailAccountType);
    }
}

bool ForumAccountPage::detailAccountIsOffline() const
{
    // 老数据可能没存类型，按离线账户处理
    if (m_detailAccountType.isEmpty())
        return true;
    return m_detailAccountType.contains(QStringLiteral("离线"))
           || m_detailAccountType.contains(QStringLiteral("offline"), Qt::CaseInsensitive)
           || m_detailAccountType.contains(tr("离线"));
}

bool ForumAccountPage::detailAccountIsSkinStation() const
{
    // 皮肤站在启动器里就是一个内置的第三方认证服务器：服务器地址对上就是
    if (m_detailAccountServerUrl.isEmpty())
        return false;
    const QString skinRoot = AuthManager::normalizeAuthServerUrl(
        SkinStationPage::skinSiteBaseUrl() + QStringLiteral("/api/yggdrasil/"));
    return !skinRoot.isEmpty()
           && AuthManager::normalizeAuthServerUrl(m_detailAccountServerUrl) == skinRoot;
}

void ForumAccountPage::refreshDetailOutfit()
{
    if (!m_detailOutfitSection)
        return;

    const bool offline = detailAccountIsOffline();
    // 皮肤站角色要和当前论坛账号对上：换装接口改的就是论坛账号自己的档案
    const bool stationReady = !offline && detailAccountIsSkinStation() && m_detailStationMatch;
    m_detailOutfitSection->setVisible(offline || stationReady);
    if (!offline && !stationReady)
        return;

    if (offline)
    {
        const bool customSkin =
            QFileInfo::exists(SkinDownloader::getOfflineSkinPath(m_detailAccountName));
        const bool customCape =
            QFileInfo::exists(SkinDownloader::getOfflineCapePath(m_detailAccountName));

        if (m_detailSkinState)
            m_detailSkinState->setText(customSkin ? tr("自定义") : tr("默认"));
        if (m_detailCapeState)
            m_detailCapeState->setText(customCape ? tr("自定义") : tr("未设置"));
        if (m_detailSkinClearBtn)
            m_detailSkinClearBtn->setVisible(customSkin);
        if (m_detailCapeClearBtn)
            m_detailCapeClearBtn->setVisible(customCape);
        // 详情页控件是所有账户共用的：换了个离线账户回来要把离线提示换回来
        if (m_detailSkinPickBtn)
            m_detailSkinPickBtn->setToolTip(tr("选一张 64×32 或 64×64 的 PNG 皮肤图，只对这个离线账户生效"));
        if (m_detailCapePickBtn)
            m_detailCapePickBtn->setToolTip(tr("选一张 64×32 的 PNG 披风图，挂在角色背后"));
        return;
    }

    // 皮肤站角色：状态跟着皮肤站档案走；皮肤站没有「脱下」接口，清除按钮不出现
    if (m_detailSkinState)
        m_detailSkinState->setText(!m_detailStationLoaded ? tr("读取中…")
                                   : m_detailStationSkinUrl.isEmpty() ? tr("默认")
                                                                      : tr("已穿"));
    if (m_detailCapeState)
        m_detailCapeState->setText(!m_detailStationLoaded ? tr("读取中…")
                                   : m_detailStationCapeUrl.isEmpty() ? tr("未设置")
                                                                      : tr("已穿"));
    if (m_detailSkinClearBtn)
        m_detailSkinClearBtn->hide();
    if (m_detailCapeClearBtn)
        m_detailCapeClearBtn->hide();
    if (m_detailSkinPickBtn)
        m_detailSkinPickBtn->setToolTip(tr("选一张 PNG 皮肤上传到方块皮肤站并立刻穿上"));
    if (m_detailCapePickBtn)
        m_detailCapePickBtn->setToolTip(tr("选一张 PNG 披风上传到方块皮肤站并立刻穿上"));
}

void ForumAccountPage::refreshDetailAppearance()
{
    if (m_detailAccountName.isEmpty())
        return;

    // 皮肤 / 披风文件变了：清掉内存里的旧皮肤，重读详情页的三维预览
    m_skinImages.remove(m_detailAccountName);
    refreshAccountDetail();

    // 角色卡片封面是正面皮肤小图，也跟着换一张
    if (QLabel *cover = m_characterCovers.value(m_detailAccountName))
    {
        const int height = m_characterCoverHeights.value(m_detailAccountName, 0);
        const QImage skin = SkinDownloader::loadSkinForAccount(
            m_detailAccountName, m_detailAccountUuid);
        if (!skin.isNull() && height > 0)
            cover->setPixmap(renderCharacterCover(skin, qMax(1, cover->width()), height));
    }
}

void ForumAccountPage::pickDetailSkin()
{
    if (m_detailAccountName.isEmpty())
        return;

    // 皮肤站角色：换装要走皮肤站接口（上传 + 立刻穿上），不是存本地文件
    if (detailAccountIsSkinStation())
    {
        uploadStationTexture(QStringLiteral("skin"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择皮肤图片"), QString(), tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    const QImage skin = SkinDownloader::normalizeSkinTexture(QImage(path));
    if (skin.isNull())
    {
        NotificationManager::showError(
            this, tr("这张图片不能用作皮肤：需要 64×32 或 64×64 的 PNG 皮肤图"));
        return;
    }
    if (!SkinDownloader::saveOfflineSkin(m_detailAccountName, skin))
    {
        NotificationManager::showError(
            this, tr("皮肤保存失败，请检查启动器所在文件夹的写入权限"));
        return;
    }

    refreshDetailAppearance();
    NotificationManager::showSuccess(this, tr("已设置自定义皮肤"));
}

void ForumAccountPage::pickDetailCape()
{
    if (m_detailAccountName.isEmpty())
        return;

    // 皮肤站角色：换装要走皮肤站接口（上传 + 立刻穿上），不是存本地文件
    if (detailAccountIsSkinStation())
    {
        uploadStationTexture(QStringLiteral("cape"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择披风图片"), QString(), tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    const QImage cape = SkinDownloader::normalizeCapeTexture(QImage(path));
    if (cape.isNull())
    {
        NotificationManager::showError(
            this, tr("这张图片不能用作披风：需要 64×32 的 PNG 披风图，且披风区域不能是空白"));
        return;
    }
    if (!SkinDownloader::saveOfflineCape(m_detailAccountName, cape))
    {
        NotificationManager::showError(
            this, tr("披风保存失败，请检查启动器所在文件夹的写入权限"));
        return;
    }

    refreshDetailAppearance();
    NotificationManager::showSuccess(this, tr("已设置自定义披风"));
}

void ForumAccountPage::clearDetailSkin()
{
    if (m_detailAccountName.isEmpty())
        return;
    if (!QFileInfo::exists(SkinDownloader::getOfflineSkinPath(m_detailAccountName)))
        return;

    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("恢复默认皮肤"),
        tr("确定清除「%1」的自定义皮肤吗？清除后换回默认皮肤。").arg(m_detailAccountName),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    if (!SkinDownloader::removeOfflineSkin(m_detailAccountName))
    {
        NotificationManager::showError(this, tr("清除失败，请检查皮肤文件是否被占用"));
        return;
    }

    refreshDetailAppearance();
    NotificationManager::showSuccess(this, tr("已恢复默认皮肤"));
}

void ForumAccountPage::clearDetailCape()
{
    if (m_detailAccountName.isEmpty())
        return;
    if (!SkinDownloader::removeOfflineCape(m_detailAccountName))
    {
        NotificationManager::showError(this, tr("清除失败，请检查披风文件是否被占用"));
        return;
    }

    refreshDetailAppearance();
    NotificationManager::showSuccess(this, tr("已清除自定义披风"));
}

// ════════════════════════════════════════════════ 方块皮肤站角色（详情页换装）

QNetworkRequest ForumAccountPage::stationRequestFor(const QString &path) const
{
    // 登录态直接复用论坛会话令牌（和皮肤站窗口同一套 Bearer 头）
    const QString base = SkinStationPage::skinSiteBaseUrl();
    QNetworkRequest request{QUrl(path.startsWith(QStringLiteral("http")) ? path : base + path)};
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    if (m_service && !m_service->token().isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_service->token().toUtf8());
    return request;
}

void ForumAccountPage::refreshStationProfile()
{
    // 论坛没登录就拿不到皮肤站档案：不弹错误打扰，状态文字停在「读取中…」
    if (!m_service || m_service->token().isEmpty() || !m_network)
        return;

    QNetworkReply *reply = m_network->get(stationRequestFor(QStringLiteral("/api/me")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 || reply->error() != QNetworkReply::NoError)
            return;
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        handleStationProfile(root.value(QStringLiteral("profile")).toObject());
    });
}

void ForumAccountPage::handleStationProfile(const QJsonObject &profile)
{
    if (profile.isEmpty())
        return;

    const QString playerName = profile.value(QStringLiteral("player_name")).toString();
    const QJsonObject skin = profile.value(QStringLiteral("skin")).toObject();
    const QJsonObject cape = profile.value(QStringLiteral("cape")).toObject();

    m_detailStationLoaded = true;
    // 换装接口改的是当前论坛账号自己的档案：详情页这个角色得就是它才给换
    m_detailStationMatch = !playerName.isEmpty()
                           && playerName.compare(m_detailAccountName, Qt::CaseInsensitive) == 0;
    // 纹理地址可能是站内相对地址，统一补全成完整网址
    const QString base = SkinStationPage::skinSiteBaseUrl();
    const auto fullUrl = [&base](const QJsonObject &texture) {
        const QString url = texture.value(QStringLiteral("url")).toString();
        if (url.isEmpty())
            return QString();
        return url.startsWith(QStringLiteral("http")) ? url : base + url;
    };
    m_detailStationSkinUrl = fullUrl(skin);
    m_detailStationCapeUrl = fullUrl(cape);

    // 披风预览：对得上号的才有服务端披风（有就挂上，没有就摘掉）
    if (m_detailSkinWidget && m_network && m_detailStationMatch)
    {
        if (m_detailStationCapeUrl.isEmpty())
        {
            m_detailSkinWidget->clearCape();
        }
        else if (!m_pendingStationCape.contains(m_detailStationCapeUrl))
        {
            m_pendingStationCape.insert(m_detailStationCapeUrl);
            const QString account = m_detailAccountName;
            QNetworkReply *capeReply = m_network->get(stationRequestFor(m_detailStationCapeUrl));
            connect(capeReply, &QNetworkReply::finished, this,
                    [this, capeReply, account, url = m_detailStationCapeUrl]() {
                        capeReply->deleteLater();
                        m_pendingStationCape.remove(url);
                        if (account != m_detailAccountName)
                            return;
                        QImage capeTexture;
                        if (capeTexture.loadFromData(capeReply->readAll())
                            && url == m_detailStationCapeUrl && m_detailSkinWidget)
                            m_detailSkinWidget->setCape(capeTexture);
                    });
        }
    }

    // 皮肤地址对齐服务器最新档案：换过皮肤后账户里存的还是旧地址，
    // 首页按旧地址缓存会一直显示旧皮肤，这里顺手一起更新掉
    if (m_detailStationMatch && !m_detailStationSkinUrl.isEmpty()
        && m_detailStationSkinUrl != m_detailAccountSkinUrl)
    {
        if (SettingsManager::instance())
            SettingsManager::instance()->updateAccountSkinUrl(m_detailAccountName,
                                                              m_detailStationSkinUrl);
        m_detailAccountSkinUrl = m_detailStationSkinUrl;
        refreshStationAppearance(m_detailStationSkinUrl);
    }

    refreshDetailOutfit();
}

void ForumAccountPage::uploadStationTexture(const QString &kind)
{
    if (m_detailAccountName.isEmpty() || !m_network)
        return;

    if (!m_service || m_service->token().isEmpty())
    {
        NotificationManager::showError(this, tr("先登录论坛账号才能设置皮肤站的皮肤和披风"));
        return;
    }

    const bool isCape = kind == QStringLiteral("cape");
    const QString path = QFileDialog::getOpenFileName(
        this, isCape ? tr("选择披风图片") : tr("选择皮肤图片"), QString(), tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    // 校验和离线账户同一套：尺寸不对直接打回
    const QImage texture = isCape ? SkinDownloader::normalizeCapeTexture(QImage(path))
                                  : SkinDownloader::normalizeSkinTexture(QImage(path));
    if (texture.isNull())
    {
        NotificationManager::showError(
            this, isCape
                      ? tr("这张图片不能用作披风：需要 64×32 的 PNG 披风图，且披风区域不能是空白")
                      : tr("这张图片不能用作皮肤：需要 64×32 或 64×64 的 PNG 皮肤图"));
        return;
    }

    // 瘦手臂模型看纹理里手臂第二列是否全透明：穿到身上胳膊才不会变粗
    QString model = QStringLiteral("classic");
    if (!isCape && texture.height() == 64)
    {
        if (texture.pixelColor(54, 20).alpha() == 0 && texture.pixelColor(55, 20).alpha() == 0)
            model = QStringLiteral("slim");
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("kind"), kind);
    query.addQueryItem(QStringLiteral("model"), model);
    query.addQueryItem(QStringLiteral("name"), QFileInfo(path).completeBaseName().left(40));
    query.addQueryItem(QStringLiteral("public"), QStringLiteral("0"));

    if (m_detailSkinState && !isCape)
        m_detailSkinState->setText(tr("正在上传…"));
    if (m_detailCapeState && isCape)
        m_detailCapeState->setText(tr("正在上传…"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        NotificationManager::showError(this, tr("读不了这个文件：%1").arg(path));
        refreshDetailOutfit();
        return;
    }
    const QByteArray png = file.readAll();
    file.close();

    // 第一步：传进皮肤站衣柜（私密）
    QNetworkRequest request = stationRequestFor(
        QStringLiteral("/api/textures?%1").arg(query.toString(QUrl::FullyEncoded)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
    QNetworkReply *reply = m_network->post(request, png);
    connect(reply, &QNetworkReply::finished, this, [this, reply, isCape]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        if (status != 200)
        {
            QString message = status > 0
                                  ? root.value(QStringLiteral("error")).toString(
                                        tr("皮肤站返回了错误（%1）").arg(status))
                                  : tr("连不上皮肤站（%1），检查网络后再试").arg(
                                        SkinStationPage::skinSiteBaseUrl());
            if (status == 401 || status == 403)
                message = tr("论坛登录已失效，请重新登录论坛账号后再试");
            NotificationManager::showError(this, message);
            refreshDetailOutfit();
            return;
        }
        const int textureId =
            root.value(QStringLiteral("texture")).toObject().value(QStringLiteral("id")).toInt(-1);
        if (textureId <= 0)
        {
            NotificationManager::showError(this, tr("皮肤站返回数据不完整，换装没有完成"));
            refreshDetailOutfit();
            return;
        }

        // 第二步：上传完立刻穿上
        QNetworkRequest applyRequest =
            stationRequestFor(QStringLiteral("/api/textures/%1/apply").arg(textureId));
        applyRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *applyReply = m_network->post(applyRequest, QByteArray("{}"));
        connect(applyReply, &QNetworkReply::finished, this, [this, applyReply, isCape]() {
            applyReply->deleteLater();
            const int applyStatus =
                applyReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject applyRoot = QJsonDocument::fromJson(applyReply->readAll()).object();
            if (applyStatus != 200)
            {
                const QString message = applyStatus > 0
                                            ? applyRoot.value(QStringLiteral("error")).toString(
                                                  tr("皮肤站返回了错误（%1）").arg(applyStatus))
                                            : tr("连不上皮肤站，检查网络后再试");
                NotificationManager::showError(this, message);
                refreshDetailOutfit();
                return;
            }
            handleStationProfile(applyRoot.value(QStringLiteral("profile")).toObject());
            NotificationManager::showSuccess(
                this, isCape ? tr("已换上新披风，进游戏就能看到")
                             : tr("已换上新皮肤，进游戏就能看到"));
        });
    });
}

void ForumAccountPage::refreshStationAppearance(const QString &skinUrl)
{
    if (skinUrl.isEmpty() || !m_network)
        return;

    // 旧皮肤缓存清掉（UUID / 用户名两个键都清），预览和首页才会重新下载
    QString normalizedUuid = m_detailAccountUuid;
    normalizedUuid.remove(QLatin1Char('-'));
    if (!normalizedUuid.isEmpty())
        QFile::remove(SkinDownloader::getCachePath(normalizedUuid));
    QFile::remove(SkinDownloader::getCachePath(m_detailAccountName));
    m_skinImages.remove(m_detailAccountName);
    m_pendingSkinRequests.remove(QStringLiteral("detail:") + m_detailAccountName);

    const QString account = m_detailAccountName;
    QNetworkRequest request{QUrl(skinUrl)};
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, account]() {
        reply->deleteLater();
        if (account != m_detailAccountName || reply->error() != QNetworkReply::NoError)
            return;
        QImage skin;
        if (!skin.loadFromData(reply->readAll()))
            return;
        skin = SkinDownloader::normalizeSkinTexture(skin);
        if (skin.isNull())
            return;

        // 存进皮肤缓存：详情页预览、角色卡片封面、首页共用这一份
        const QString cachePath = SkinDownloader::getCachePath(account);
        QDir().mkpath(QFileInfo(cachePath).absolutePath());
        skin.save(cachePath, "PNG");
        m_skinImages.insert(account, skin);
        if (m_detailSkinWidget && account == m_detailAccountName)
            m_detailSkinWidget->setSkin(skin);
        if (QLabel *cover = m_characterCovers.value(account))
        {
            const int height = m_characterCoverHeights.value(account, 0);
            if (height > 0)
                cover->setPixmap(renderCharacterCover(skin, qMax(1, cover->width()), height));
        }
    });
}

void ForumAccountPage::updateDetailBackground()
{
    if (!m_detailSkinWidget)
        return;

    const bool dark = ThemeManager::instance()
                      && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    const Palette p = currentPalette();
    m_detailSkinWidget->setThemeBackground(dark);
    // 左侧预览不再画灰底：背景直接用页面底色，模型看起来浮在页面上
    m_detailSkinWidget->setBackgroundColor(QColor(p.bg));
    m_detailSkinWidget->setForegroundColor(QColor(p.ink));
}

// ════════════════════════════════════════════════════════════════ 内容

void ForumAccountPage::showContentPage(int index)
{
    if (!m_contentLayout)
        return;
    if (index == m_contentPage)
        return;

    // 把当前页从布局里拿掉（控件仍归 host 所有，只隐藏）
    while (QLayoutItem *item = m_contentLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->hide();
        delete item;
    }

    m_contentPage = index;
    if (index < 0 || index >= m_contentPages.size())
        return;

    QWidget *page = m_contentPages.at(index);
    page->show();
    m_contentLayout->addWidget(page);
}

void ForumAccountPage::renderContentArea()
{
    if (!m_contentHost)
        return;

    // 「角色」页显示的是游戏账户，和论坛登录状态无关
    if (m_activeTab == CharactersTab)
    {
        showContentPage(CharactersContent);
        reloadCharacters();
        // 正停在某个账户的详情页时，顺便重读一次（皮肤编辑器里应用后预览跟着换）
        if (m_viewStack && m_viewStack->currentIndex() == DetailView
            && !m_detailAccountName.isEmpty())
            refreshAccountDetail();
        return;
    }

    // 「游玩」页和登录状态无关：没登录也照常显示本机统计，登录后自动切到账号云端
    if (m_activeTab == PlaytimeTab)
    {
        showContentPage(PlaytimeContent);
        setTabHint(tr("本机时长会跟着方块盒子账户走"));
        if (m_playtimePage)
            m_playtimePage->refresh();
        return;
    }

    const bool loggedIn = m_service->isLoggedIn();
    if (!loggedIn)
    {
        showContentPage(LoggedOutContent);
        setTabHint(tr("登录后可查看"));
        return;
    }

    switch (m_activeTab)
    {
    case CharactersTab:
        // 上面已经单独处理
        break;
    case ThreadsTab:
        showContentPage(ThreadsContent);
        renderThreads();
        break;
    case RepliesTab:
        showContentPage(RepliesContent);
        renderReplies();
        break;
    case BookmarksTab:
        showContentPage(BookmarksContent);
        renderBookmarks();
        break;
    case NotificationsTab:
        showContentPage(NotificationsContent);
        renderNotifications();
        break;
    case FeedbackTab:
        showContentPage(FeedbackContent);
        loadMyFeedbacks(false);
        break;
    case PointsTab:
        showContentPage(PointsContent);
        setTabHint(tr("签到或充值都能拿方块"));
        renderPoints();
        break;
    case PlaytimeTab:
        // 上面已单独处理（和登录状态无关）+
        break;
    case MedalsTab:
        showContentPage(MedalsContent);
        setTabHint(tr("论坛勋章和游戏成就都在这里"));
        renderMedals();
        break;
    case SettingsTab:
        showContentPage(SettingsContent);
        setTabHint(tr("改完记得保存"));
        fillSettingsForm();
        break;
    }
}

void ForumAccountPage::renderLoading(QVBoxLayout *layout)
{
    clearLayout(layout);
    auto *label = makeMutedLabel(tr("正在加载…"), layout->parentWidget(), "fbEmpty");
    label->setAlignment(Qt::AlignCenter);
    layout->addSpacing(24);
    layout->addWidget(label);
    layout->addSpacing(24);
    layout->addStretch();
}

void ForumAccountPage::renderThreads()
{
    clearLayout(m_threadListLayout);

    if (m_threads.isEmpty())
    {
        if (!m_threadsLoaded)
        {
            renderLoading(m_threadListLayout);
            return;
        }
        auto *empty = makeMutedLabel(tr("还没有发过主题，去论坛聊聊吧"), m_threadListLayout->parentWidget(),
                                     "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_threadListLayout->addSpacing(28);
        m_threadListLayout->addWidget(empty);
        m_threadListLayout->addStretch();
        setTabHint(tr("共 0 个主题"));
        return;
    }

    renderRows(m_threadListLayout, m_threads, 0);
    setTabHint(tr("共 %1 个主题").arg(m_threadTotal));
}

void ForumAccountPage::renderReplies()
{
    clearLayout(m_replyListLayout);

    if (m_replies.isEmpty())
    {
        if (!m_repliesLoaded)
        {
            renderLoading(m_replyListLayout);
            return;
        }
        auto *empty = makeMutedLabel(tr("还没有回复过帖子"), m_replyListLayout->parentWidget(), "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_replyListLayout->addSpacing(28);
        m_replyListLayout->addWidget(empty);
        m_replyListLayout->addStretch();
        setTabHint(tr("共 0 条回复"));
        return;
    }

    renderRows(m_replyListLayout, m_replies, 1);
    setTabHint(tr("共 %1 条回复").arg(m_replyTotal));
}

void ForumAccountPage::renderBookmarks()
{
    if (m_bookmarkSubTab == BookmarkResourceTab)
    {
        renderContentFavorites();
        return;
    }
    if (m_bookmarkSubTab == BookmarkLocalTab)
    {
        renderLocalFavorites();
        return;
    }

    clearLayout(m_bookmarkListLayout);

    if (m_bookmarks.isEmpty())
    {
        if (!m_bookmarksLoaded)
        {
            renderLoading(m_bookmarkListLayout);
            return;
        }
        auto *empty = makeMutedLabel(tr("还没有收藏任何主题"), m_bookmarkListLayout->parentWidget(),
                                     "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_bookmarkListLayout->addSpacing(28);
        m_bookmarkListLayout->addWidget(empty);
        m_bookmarkListLayout->addStretch();
        setTabHint(tr("共 %1 个帖子收藏").arg(m_bookmarkTotal));
        return;
    }

    renderRows(m_bookmarkListLayout, m_bookmarks, 2);
    setTabHint(tr("共 %1 个帖子收藏").arg(m_bookmarkTotal));
}

void ForumAccountPage::setBookmarkSubTab(int subTab)
{
    if (m_bookmarkSubTab == subTab)
        return;
    m_bookmarkSubTab = subTab;
    renderBookmarks();
    loadTabData();
}

void ForumAccountPage::loadContentFavorites()
{
    if (!m_service->isLoggedIn())
        return;

    renderLoading(m_bookmarkListLayout);
    ForumContentService::instance()->fetchContentFavorites(
        1, 20,
        [this](const QVector<FavoriteItem> &items, int total) {
            m_contentFavorites = items;
            m_contentFavoriteTotal = total;
            m_contentFavoritesLoaded = true;
            if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkResourceTab)
                renderContentFavorites();
            refreshBookmarkStat();
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            m_contentFavoritesLoaded = true;
            clearLayout(m_bookmarkListLayout);
            auto *error =
                makeMutedLabel(message, m_bookmarkListLayout->parentWidget(), "fbEmpty");
            error->setAlignment(Qt::AlignCenter);
            m_bookmarkListLayout->addSpacing(24);
            m_bookmarkListLayout->addWidget(error);
            m_bookmarkListLayout->addStretch();
        });
}

void ForumAccountPage::requestContentIcon(const QString &url, QLabel *label)
{
    QPointer<QLabel> guard(label);
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(12000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, guard]() {
        reply->deleteLater();
        if (!guard || reply->error() != QNetworkReply::NoError)
            return;

        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll()))
            return;

        m_contentIconCache.insert(url, pixmap.scaled(40, 40, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
        guard->setPixmap(m_contentIconCache.value(url));
    });
}

void ForumAccountPage::renderContentFavorites()
{
    clearLayout(m_bookmarkListLayout);
    QWidget *host = m_bookmarkListLayout->parentWidget();
    const Palette p = currentPalette();

    if (m_contentFavorites.isEmpty())
    {
        if (!m_contentFavoritesLoaded)
        {
            renderLoading(m_bookmarkListLayout);
            return;
        }
        auto *empty = makeMutedLabel(tr("还没有收藏的资源，去「资源」页点 ☆ 收藏吧"), host, "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_bookmarkListLayout->addSpacing(28);
        m_bookmarkListLayout->addWidget(empty);
        m_bookmarkListLayout->addStretch();
        setTabHint(tr("共 %1 个资源收藏").arg(m_contentFavoriteTotal));
        return;
    }

    for (const FavoriteItem &item : m_contentFavorites)
    {
        auto *row = new ClickableFrame(host);
        row->setObjectName(QStringLiteral("fbListRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 8);
        rowLayout->setSpacing(10);

        // 图标：缓存优先，未命中走异步下载，失败留字母头像
        auto *iconLabel = new QLabel(row);
        iconLabel->setObjectName(QStringLiteral("fbRowIcon"));
        iconLabel->setFixedSize(40, 40);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setScaledContents(false);
        if (!item.iconUrl.isEmpty() && m_contentIconCache.contains(item.iconUrl))
        {
            iconLabel->setPixmap(m_contentIconCache.value(item.iconUrl));
        }
        else if (!item.iconUrl.isEmpty())
        {
            iconLabel->setPixmap(letterAvatar(item.displayName(), 40, QColor(p.brand)));
            requestContentIcon(item.iconUrl, iconLabel);
        }
        else
        {
            iconLabel->setPixmap(letterAvatar(item.displayName(), 40, QColor(p.brand)));
        }
        rowLayout->addWidget(iconLabel);

        auto *contentLayout = new QVBoxLayout();
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(3);

        auto *titleLabel = new ElidedLabel(row);
        titleLabel->setObjectName(QStringLiteral("fbRowTitle"));
        const QString name = item.displayName();
        titleLabel->setFullText(name.isEmpty() ? tr("（未命名资源）") : name);
        titleLabel->setToolTip(name);
        contentLayout->addWidget(titleLabel);

        QString meta = item.displayType();
        if (item.source.compare(QStringLiteral("curseforge"), Qt::CaseInsensitive) == 0)
            meta += QStringLiteral(" · CurseForge");
        else if (item.source.compare(QStringLiteral("modrinth"), Qt::CaseInsensitive) == 0)
            meta += QStringLiteral(" · Modrinth");
        if (item.addedAt > 0)
            meta += QStringLiteral(" · ") + relativeTime(item.addedAt / 1000);
        auto *metaLabel = new QLabel(meta, row);
        metaLabel->setObjectName(QStringLiteral("fbRowMeta"));
        metaLabel->setWordWrap(true);
        contentLayout->addWidget(metaLabel);

        rowLayout->addLayout(contentLayout, 1);

        auto *removeBtn = new QPushButton(tr("移除"), row);
        removeBtn->setObjectName(QStringLiteral("fbBtnGhost"));
        removeBtn->setFixedHeight(28);
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setToolTip(tr("从账号收藏中移除（本地收藏夹同步删除）"));
        const FavoriteItem removed = item;
        connect(removeBtn, &QPushButton::clicked, this, [this, removed]() {
            removeAccountContentFavorite(removed);
        });
        rowLayout->addWidget(removeBtn);

        const ModInfo info = item.toModInfo();
        const int contentType = item.contentType;
        row->onClick = [this, info, contentType]() {
            emit contentFavoriteOpenRequested(info, static_cast<ContentType>(contentType));
        };

        m_bookmarkListLayout->addWidget(row);
    }

    auto *moreHint =
        makeMutedLabel(tr("只显示最近 20 条，更多收藏可在论坛网页版查看"), host, "fbNote");
    moreHint->setAlignment(Qt::AlignCenter);
    m_bookmarkListLayout->addSpacing(6);
    m_bookmarkListLayout->addWidget(moreHint);
    m_bookmarkListLayout->addStretch();
    setTabHint(tr("共 %1 个资源收藏").arg(m_contentFavoriteTotal));
}

void ForumAccountPage::removeAccountContentFavorite(const FavoriteItem &item)
{
    ForumContentService::instance()->removeContentFavorite(
        item.source, item.remoteId,
        [this, item]() {
            // 本地所有包含它的收藏夹一并移除，避免下次登录对账又传回云端
            FavoritesManager *favorites = FavoritesManager::instance();
            const QList<QString> folderIds = favorites->foldersContaining(item.id);
            for (const QString &folderId : folderIds)
                favorites->removeFavorite(folderId, item.id);

            // FavoriteItem 未定义 operator==，按全局唯一 id 移除
            for (int i = m_contentFavorites.size() - 1; i >= 0; --i)
            {
                if (m_contentFavorites.at(i).id == item.id)
                    m_contentFavorites.removeAt(i);
            }
            m_contentFavoriteTotal = qMax(0, m_contentFavoriteTotal - 1);
            if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkResourceTab)
                renderContentFavorites();
            refreshBookmarkStat();
            NotificationManager::showSuccess(this, tr("已从账号收藏中移除"));
        },
        [this](const QString &message, int) {
            NotificationManager::showError(this, message);
        });
}

void ForumAccountPage::renderLocalFavorites()
{
    clearLayout(m_bookmarkListLayout);
    QWidget *host = m_bookmarkListLayout->parentWidget();
    const Palette p = currentPalette();

    const QVector<LocalFavoriteEntry> favorites = LocalFavoritesManager::instance()->favorites();
    if (favorites.isEmpty())
    {
        auto *empty =
            makeMutedLabel(tr("还没有收藏的本地资源，去实例资源页点 ★ 收藏吧"), host, "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_bookmarkListLayout->addSpacing(28);
        m_bookmarkListLayout->addWidget(empty);
        m_bookmarkListLayout->addStretch();
        setTabHint(tr("共 0 个本地收藏"));
        return;
    }

    for (const LocalFavoriteEntry &entry : favorites)
    {
        auto *row = new ClickableFrame(host);
        row->setObjectName(QStringLiteral("fbListRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 8);
        rowLayout->setSpacing(10);

        const QString name = entry.displayName.isEmpty() ? entry.fileName : entry.displayName;

        // 图标：本地资源没有网络图，用首字母头像
        auto *iconLabel = new QLabel(row);
        iconLabel->setObjectName(QStringLiteral("fbRowIcon"));
        iconLabel->setFixedSize(40, 40);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setPixmap(letterAvatar(name, 40, QColor(p.brand)));
        rowLayout->addWidget(iconLabel);

        auto *contentLayout = new QVBoxLayout();
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(3);

        auto *titleLabel = new ElidedLabel(row);
        titleLabel->setObjectName(QStringLiteral("fbRowTitle"));
        titleLabel->setFullText(name);
        titleLabel->setToolTip(name);
        contentLayout->addWidget(titleLabel);

        // 元信息：类型 · 实例文件夹 · 收藏时间（文件被移动/删除后提示「文件丢失」）
        QString meta = LocalFavoritesManager::typeDisplayName(entry.resourceType);
        const QString instanceName = QFileInfo(entry.instancePath).fileName();
        if (!instanceName.isEmpty())
            meta += QStringLiteral(" · ") + instanceName;
        if (entry.addedAt > 0)
            meta += QStringLiteral(" · ") + relativeTime(entry.addedAt / 1000);
        if (!entry.filePath.isEmpty() && !QFileInfo::exists(entry.filePath))
            meta += QStringLiteral(" · ") + tr("文件丢失");
        auto *metaLabel = new QLabel(meta, row);
        metaLabel->setObjectName(QStringLiteral("fbRowMeta"));
        metaLabel->setWordWrap(true);
        contentLayout->addWidget(metaLabel);

        rowLayout->addLayout(contentLayout, 1);

        auto *removeBtn = new QPushButton(tr("移除"), row);
        removeBtn->setObjectName(QStringLiteral("fbBtnGhost"));
        removeBtn->setFixedHeight(28);
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setToolTip(tr("从本地收藏中移除"));
        const LocalFavoriteEntry removed = entry;
        connect(removeBtn, &QPushButton::clicked, this, [this, removed]() {
            LocalFavoritesManager::instance()->setFavorite(removed, false);
            NotificationManager::showSuccess(this, tr("已从本地收藏中移除"));
        });
        rowLayout->addWidget(removeBtn);

        const LocalFavoriteEntry openEntry = entry;
        row->onClick = [this, openEntry]() {
            emit localFavoriteOpenRequested(openEntry);
        };

        m_bookmarkListLayout->addWidget(row);
    }

    m_bookmarkListLayout->addStretch();
    setTabHint(tr("共 %1 个本地收藏").arg(favorites.size()));
}

void ForumAccountPage::refreshBookmarkStat()
{
    if (!m_service->isLoggedIn())
        return;

    // 「收藏」统计 = 论坛帖子收藏 + 资源收藏 + 本地收藏，前两个轻量请求各取 total
    const int localFavCount = LocalFavoritesManager::instance()->count();
    m_service->getJson(
        QStringLiteral("/api/me/bookmarks?per_page=1"),
        [this, localFavCount](const QJsonObject &root) {
            m_bookmarkTotal =
                root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("total")).toInt();
            if (m_statBookmarks)
                m_statBookmarks->setText(
                    QString::number(m_bookmarkTotal + m_contentFavoriteTotal + localFavCount));
            if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkThreadTab &&
                m_bookmarksLoaded)
                setTabHint(tr("共 %1 个帖子收藏").arg(m_bookmarkTotal));
        },
        nullptr);

    ForumContentService::instance()->fetchContentFavorites(
        1, 1,
        [this, localFavCount](const QVector<FavoriteItem> &, int total) {
            m_contentFavoriteTotal = total;
            if (m_statBookmarks)
                m_statBookmarks->setText(
                    QString::number(m_bookmarkTotal + m_contentFavoriteTotal + localFavCount));
            if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkResourceTab &&
                m_contentFavoritesLoaded)
                setTabHint(tr("共 %1 个资源收藏").arg(m_contentFavoriteTotal));
        },
        nullptr);
}

QWidget *ForumAccountPage::buildNotificationsContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 14);
    layout->setSpacing(10);

    // 页头：未读统计 + 说明 + 全部已读。常驻在页面布局里，刷新列表时不重建，
    // 免得 clearLayout 清掉按钮后又重新 new 一个，留下残影。
    auto *head = new QWidget(page);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(9);

    auto *chip = new QLabel(head);
    chip->setObjectName(QStringLiteral("fbNotifUnreadChip"));
    chip->hide();
    headLayout->addWidget(chip, 0, Qt::AlignVCenter);

    auto *hint = new QLabel(tr("有人回复、提到你，或论坛发布公告时会出现在这里"), head);
    hint->setObjectName(QStringLiteral("fbNote"));
    headLayout->addWidget(hint, 1);

    m_markAllReadButton = new QPushButton(tr("全部已读"), head);
    m_markAllReadButton->setObjectName(QStringLiteral("fbBtnGhost"));
    m_markAllReadButton->setCursor(Qt::PointingHandCursor);
    m_markAllReadButton->setFixedHeight(28);
    connect(m_markAllReadButton, &QPushButton::clicked, this,
            &ForumAccountPage::markAllNotificationsRead);
    headLayout->addWidget(m_markAllReadButton, 0, Qt::AlignVCenter);

    layout->addWidget(head);

    // 列表容器：刷新时只重建这层内容
    auto *listHost = new QWidget(page);
    auto *listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);
    m_notificationListLayout = listLayout;

    layout->addWidget(listHost);
    layout->addStretch();
    return page;
}

QWidget *ForumAccountPage::buildFeedbackContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 14);
    layout->setSpacing(10);

    // 页头：说明 + 刷新。常驻在页面布局里，刷新列表时不重建，免得按钮闪烁。
    auto *head = new QWidget(page);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(9);

    auto *hint = new QLabel(tr("你在启动器或网页里提交的反馈，处理进度和管理员回复都在这里"), head);
    hint->setObjectName(QStringLiteral("fbNote"));
    hint->setWordWrap(true);
    headLayout->addWidget(hint, 1);

    m_feedbackRefreshButton = new QPushButton(tr("刷新"), head);
    m_feedbackRefreshButton->setObjectName(QStringLiteral("fbBtnGhost"));
    m_feedbackRefreshButton->setCursor(Qt::PointingHandCursor);
    m_feedbackRefreshButton->setFixedHeight(28);
    connect(m_feedbackRefreshButton, &QPushButton::clicked, this,
            [this]() { loadMyFeedbacks(true); });
    headLayout->addWidget(m_feedbackRefreshButton, 0, Qt::AlignVCenter);

    layout->addWidget(head);

    // 列表容器：刷新时只重建这层内容
    auto *listHost = new QWidget(page);
    auto *listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);
    m_feedbackListLayout = listLayout;

    layout->addWidget(listHost);
    layout->addStretch();
    return page;
}

/** 账户页「积分」：方块余额 + 7 天签到 + 签到记录 */
QWidget *ForumAccountPage::buildPointsContent(QWidget *parent)
{
    m_pointsPage = new QWidget(parent);
    auto *layout = new QVBoxLayout(m_pointsPage);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(14);

    // ── 余额 / 签到 ──
    auto *hero = new QFrame(m_pointsPage);
    hero->setObjectName(QStringLiteral("fbPointsHero"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(20, 18, 20, 18);
    heroLayout->setSpacing(18);

    auto *heroMain = new QVBoxLayout();
    heroMain->setContentsMargins(0, 0, 0, 0);
    heroMain->setSpacing(3);

    auto *eyebrow = new QLabel(tr("我的方块"), hero);
    eyebrow->setObjectName(QStringLiteral("fbPointsEyebrow"));
    heroMain->addWidget(eyebrow);

    m_pointsBalance = new QLabel(QStringLiteral("0"), hero);
    m_pointsBalance->setObjectName(QStringLiteral("fbPointsBalance"));
    heroMain->addWidget(m_pointsBalance);

    m_pointsStreak = new QLabel(tr("正在加载签到进度…"), hero);
    m_pointsStreak->setObjectName(QStringLiteral("fbPointsStreak"));
    m_pointsStreak->setWordWrap(true);
    heroMain->addWidget(m_pointsStreak);

    m_pointsReward = new QLabel(tr("登录后每天签到都能领方块"), hero);
    m_pointsReward->setObjectName(QStringLiteral("fbPointsReward"));
    m_pointsReward->setWordWrap(true);
    heroMain->addWidget(m_pointsReward);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 8, 0, 0);
    actionRow->setSpacing(10);

    m_pointsCheckinButton = new QPushButton(tr("签到"), hero);
    m_pointsCheckinButton->setObjectName(QStringLiteral("fbPointsCheckin"));
    m_pointsCheckinButton->setCursor(Qt::PointingHandCursor);
    m_pointsCheckinButton->setFixedHeight(38);
    m_pointsCheckinButton->setMinimumWidth(156);
    connect(m_pointsCheckinButton, &QPushButton::clicked, this,
            &ForumAccountPage::checkInPoints);
    actionRow->addWidget(m_pointsCheckinButton);

    auto *webHint = new QLabel(tr("也可以在论坛网页的「我的方块」页签到、买方块"), hero);
    webHint->setObjectName(QStringLiteral("fbNote"));
    webHint->setWordWrap(true);
    actionRow->addWidget(webHint, 1, Qt::AlignVCenter);
    actionRow->addStretch();
    heroMain->addLayout(actionRow);

    heroLayout->addLayout(heroMain, 1);

    // 神秘礼包（占位：每周天签到先记下来，内容以后再开放）
    auto *gift = new QFrame(hero);
    gift->setObjectName(QStringLiteral("fbPointsGiftCard"));
    gift->setMaximumWidth(272);
    gift->setMinimumWidth(0);
    auto *giftLayout = new QHBoxLayout(gift);
    giftLayout->setContentsMargins(14, 14, 14, 14);
    giftLayout->setSpacing(10);

    auto *giftIcon = new QLabel(QStringLiteral("🎁"), gift);
    giftIcon->setObjectName(QStringLiteral("fbPointsGiftIcon"));
    giftLayout->addWidget(giftIcon, 0, Qt::AlignTop);

    m_pointsGift = new QLabel(tr("每周天签到都会收到一份神秘礼包，内容正在准备中。"), gift);
    m_pointsGift->setObjectName(QStringLiteral("fbPointsGiftText"));
    m_pointsGift->setWordWrap(true);
    giftLayout->addWidget(m_pointsGift, 1);

    heroLayout->addWidget(gift, 0, Qt::AlignVCenter);
    layout->addWidget(hero);

    // ── 购买方块（档位由论坛下发，充值通道没开通时整块隐藏） ──
    auto *shop = new QFrame(m_pointsPage);
    shop->setObjectName(QStringLiteral("fbPointsShop"));
    m_pointsShop = shop;
    auto *shopLayout = new QVBoxLayout(shop);
    shopLayout->setContentsMargins(16, 14, 16, 14);
    shopLayout->setSpacing(10);

    auto *shopHead = new QHBoxLayout();
    shopHead->setContentsMargins(0, 0, 0, 0);
    shopHead->setSpacing(8);
    auto *shopTitle = new QLabel(tr("购买方块"), shop);
    shopTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    shopHead->addWidget(shopTitle);
    shopHead->addStretch();
    m_pointsShopNote = new QLabel(tr("正在读取充值档位…"), shop);
    m_pointsShopNote->setObjectName(QStringLiteral("fbNote"));
    m_pointsShopNote->setWordWrap(true);
    shopHead->addWidget(m_pointsShopNote);
    shopLayout->addLayout(shopHead);

    auto *shopGridHost = new QWidget(shop);
    m_pointsShopGrid = new QHBoxLayout(shopGridHost);
    m_pointsShopGrid->setContentsMargins(0, 0, 0, 0);
    m_pointsShopGrid->setSpacing(8);
    shopLayout->addWidget(shopGridHost);

    // 支付面板：下单后出现，显示订单、打开支付页的入口和「我已完成支付」
    m_pointsShopPayPanel = new QWidget(shop);
    m_pointsShopPayPanel->setObjectName(QStringLiteral("fbPointsPayPanel"));
    // 自绘底色 / 边框：QWidget 默认不按 QSS 画背景
    m_pointsShopPayPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_pointsShopPayPanel->hide();
    auto *payRow = new QVBoxLayout(m_pointsShopPayPanel);
    payRow->setContentsMargins(12, 10, 12, 10);
    payRow->setSpacing(8);

    m_pointsShopPayTitle = new QLabel(tr("订单已创建"), m_pointsShopPayPanel);
    m_pointsShopPayTitle->setObjectName(QStringLiteral("fbPointsPayTitle"));
    payRow->addWidget(m_pointsShopPayTitle);
    m_pointsShopPayText = new QLabel(tr("在浏览器里完成支付，付完方块会自动到账。"),
                                     m_pointsShopPayPanel);
    m_pointsShopPayText->setObjectName(QStringLiteral("fbNote"));
    m_pointsShopPayText->setWordWrap(true);
    payRow->addWidget(m_pointsShopPayText);

    // 按钮单独一行：窄屏（手机）放不下一横排时就换行，不会把面板挤爆
    auto *payActions = new QHBoxLayout();
    payActions->setContentsMargins(0, 0, 0, 0);
    payActions->setSpacing(6);
    m_pointsShopPayOpen = new QPushButton(tr("打开支付页"), m_pointsShopPayPanel);
    m_pointsShopPayOpen->setObjectName(QStringLiteral("fbBtnGhost"));
    m_pointsShopPayOpen->setCursor(Qt::PointingHandCursor);
    m_pointsShopPayOpen->setFixedHeight(30);
    connect(m_pointsShopPayOpen, &QPushButton::clicked, this, [this]() {
        if (!m_payUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(m_payUrl));
    });
    payActions->addWidget(m_pointsShopPayOpen);

    m_pointsShopPayCheck = new QPushButton(tr("我已完成支付"), m_pointsShopPayPanel);
    m_pointsShopPayCheck->setObjectName(QStringLiteral("fbBtnPrimary"));
    m_pointsShopPayCheck->setCursor(Qt::PointingHandCursor);
    m_pointsShopPayCheck->setFixedHeight(30);
    connect(m_pointsShopPayCheck, &QPushButton::clicked, this,
            [this]() { checkPayOrder(true); });
    payActions->addWidget(m_pointsShopPayCheck);

    auto *payCancel = new QPushButton(tr("收起"), m_pointsShopPayPanel);
    payCancel->setObjectName(QStringLiteral("fbBtnGhost"));
    payCancel->setCursor(Qt::PointingHandCursor);
    payCancel->setFixedHeight(30);
    connect(payCancel, &QPushButton::clicked, this, [this]() {
        // 只收面板，不停轮询：用户可能收起来之后才去付款，付完照样弹到账提示
        if (m_pointsShopPayPanel)
            m_pointsShopPayPanel->hide();
        NotificationManager::showInfo(
            this, tr("已收起。付完款方块会自动到账，付完回到这一页也能看到余额。"));
    });
    payActions->addWidget(payCancel);
    payActions->addStretch();
    payRow->addLayout(payActions);

    shopLayout->addWidget(m_pointsShopPayPanel);

    auto *shopRule =
        new QLabel(tr("用支付宝或微信付款，付完方块自动到账，几秒内就能刷新余额。"), shop);
    shopRule->setObjectName(QStringLiteral("fbNote"));
    shopRule->setWordWrap(true);
    shopLayout->addWidget(shopRule);

    shop->hide();
    layout->addWidget(shop);

    // ── 7 天签到 ──
    auto *weekHost = new QFrame(m_pointsPage);
    weekHost->setObjectName(QStringLiteral("fbPointsWeek"));
    auto *weekLayout = new QVBoxLayout(weekHost);
    weekLayout->setContentsMargins(16, 14, 16, 14);
    weekLayout->setSpacing(10);

    auto *weekHead = new QHBoxLayout();
    weekHead->setContentsMargins(0, 0, 0, 0);
    weekHead->setSpacing(8);
    auto *weekTitle = new QLabel(tr("7 天签到"), weekHost);
    weekTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    weekHead->addWidget(weekTitle);
    weekHead->addStretch();
    auto *weekHint = new QLabel(tr("连续签到越久，每天领得越多"), weekHost);
    weekHint->setObjectName(QStringLiteral("fbNote"));
    weekHead->addWidget(weekHint);
    weekLayout->addLayout(weekHead);

    auto *daysRow = new QHBoxLayout();
    daysRow->setContentsMargins(0, 0, 0, 0);
    daysRow->setSpacing(6);
    for (int i = 0; i < 7; ++i)
    {
        auto *tile = new QFrame(weekHost);
        tile->setObjectName(QStringLiteral("fbPointsDay"));
        tile->setMinimumWidth(0);
        auto *tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(8, 9, 8, 9);
        tileLayout->setSpacing(1);

        auto *dayLabel = new QLabel(tr("第 %1 天").arg(i + 1), tile);
        dayLabel->setObjectName(QStringLiteral("fbPointsDayLabel"));
        dayLabel->setAlignment(Qt::AlignCenter);
        tileLayout->addWidget(dayLabel);

        auto *reward = new QLabel(QStringLiteral("—"), tile);
        reward->setObjectName(QStringLiteral("fbPointsDayReward"));
        reward->setAlignment(Qt::AlignCenter);
        tileLayout->addWidget(reward);

        auto *state = new QLabel(tr("待领"), tile);
        state->setObjectName(QStringLiteral("fbPointsDayState"));
        state->setAlignment(Qt::AlignCenter);
        tileLayout->addWidget(state);

        daysRow->addWidget(tile, 1);
        m_pointsDayCards.append(tile);
        m_pointsDayRewards.append(reward);
        m_pointsDayStates.append(state);
    }
    weekLayout->addLayout(daysRow);

    auto *rule = new QLabel(
        tr("第 1～7 天奖励逐天增加，第 8 天起每天 20 方块；断签会从第 1 天重新开始。"), weekHost);
    rule->setObjectName(QStringLiteral("fbNote"));
    rule->setWordWrap(true);
    weekLayout->addWidget(rule);
    layout->addWidget(weekHost);

    // ── 补签卡 ──
    auto *makeup = new QFrame(m_pointsPage);
    makeup->setObjectName(QStringLiteral("fbPointsMakeup"));
    auto *makeupLayout = new QHBoxLayout(makeup);
    makeupLayout->setContentsMargins(16, 14, 16, 14);
    makeupLayout->setSpacing(12);

    auto *makeupIcon = new QLabel(QStringLiteral("🎫"), makeup);
    makeupIcon->setObjectName(QStringLiteral("fbPointsMakeupIcon"));
    makeupLayout->addWidget(makeupIcon, 0, Qt::AlignTop);

    auto *makeupInfo = new QVBoxLayout();
    makeupInfo->setContentsMargins(0, 0, 0, 0);
    makeupInfo->setSpacing(3);

    auto *makeupTitleRow = new QHBoxLayout();
    makeupTitleRow->setContentsMargins(0, 0, 0, 0);
    makeupTitleRow->setSpacing(8);
    auto *makeupTitle = new QLabel(tr("补签卡"), makeup);
    makeupTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    makeupTitleRow->addWidget(makeupTitle);

    m_makeupCount = new QLabel(tr("0 张"), makeup);
    m_makeupCount->setObjectName(QStringLiteral("fbPointsMakeupCount"));
    makeupTitleRow->addWidget(m_makeupCount);
    makeupTitleRow->addStretch();
    makeupInfo->addLayout(makeupTitleRow);

    m_makeupDesc = new QLabel(tr("昨天漏签时用一张补上，连续签到接着算；每张 20 方块。"), makeup);
    m_makeupDesc->setObjectName(QStringLiteral("fbNote"));
    m_makeupDesc->setWordWrap(true);
    makeupInfo->addWidget(m_makeupDesc);
    makeupLayout->addLayout(makeupInfo, 1);

    auto *makeupActions = new QVBoxLayout();
    makeupActions->setContentsMargins(0, 0, 0, 0);
    makeupActions->setSpacing(6);

    m_buyMakeupButton = new QPushButton(tr("购买补签卡（20 方块）"), makeup);
    m_buyMakeupButton->setObjectName(QStringLiteral("fbBtnGhost"));
    m_buyMakeupButton->setCursor(Qt::PointingHandCursor);
    m_buyMakeupButton->setFixedHeight(30);
    connect(m_buyMakeupButton, &QPushButton::clicked, this, &ForumAccountPage::buyMakeupCard);
    makeupActions->addWidget(m_buyMakeupButton);

    m_useMakeupButton = new QPushButton(tr("补签昨天"), makeup);
    m_useMakeupButton->setObjectName(QStringLiteral("fbBtnPrimary"));
    m_useMakeupButton->setCursor(Qt::PointingHandCursor);
    m_useMakeupButton->setFixedHeight(30);
    connect(m_useMakeupButton, &QPushButton::clicked, this, &ForumAccountPage::useMakeupCard);
    makeupActions->addWidget(m_useMakeupButton);

    makeupLayout->addLayout(makeupActions);
    layout->addWidget(makeup);

    // ── 签到记录 ──
    auto *historyHead = new QHBoxLayout();
    historyHead->setContentsMargins(2, 2, 2, 0);
    auto *historyTitle = new QLabel(tr("签到记录"), m_pointsPage);
    historyTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    historyHead->addWidget(historyTitle);
    historyHead->addStretch();
    layout->addLayout(historyHead);

    auto *historyHost = new QWidget(m_pointsPage);
    m_pointsHistoryLayout = new QVBoxLayout(historyHost);
    m_pointsHistoryLayout->setContentsMargins(0, 0, 0, 0);
    m_pointsHistoryLayout->setSpacing(4);
    layout->addWidget(historyHost);

    layout->addStretch(1);
    return m_pointsPage;
}

/** 账户页「勋章」：和方块盒子账号绑定的勋章墙（服务端判定，本地只负责展示） */
QWidget *ForumAccountPage::buildMedalsContent(QWidget *parent)
{
    m_medalsPage = new QWidget(parent);
    auto *layout = new QVBoxLayout(m_medalsPage);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(14);

    // ── 顶部汇总：已点亮数量 + 整墙完成度 ──
    auto *hero = new QFrame(m_medalsPage);
    hero->setObjectName(QStringLiteral("fbMedalsHero"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(20, 18, 20, 18);
    heroLayout->setSpacing(18);

    auto *heroMain = new QVBoxLayout();
    heroMain->setContentsMargins(0, 0, 0, 0);
    heroMain->setSpacing(4);

    auto *eyebrow = new QLabel(tr("我的勋章墙"), hero);
    eyebrow->setObjectName(QStringLiteral("fbMedalsEyebrow"));
    heroMain->addWidget(eyebrow);

    auto *countRow = new QHBoxLayout();
    countRow->setContentsMargins(0, 0, 0, 0);
    countRow->setSpacing(6);
    m_medalsCount = new QLabel(QStringLiteral("0"), hero);
    m_medalsCount->setObjectName(QStringLiteral("fbMedalsCount"));
    countRow->addWidget(m_medalsCount, 0, Qt::AlignBottom);
    m_medalsTotal = new QLabel(tr("/ 0 枚"), hero);
    m_medalsTotal->setObjectName(QStringLiteral("fbMedalsTotal"));
    countRow->addWidget(m_medalsTotal, 0, Qt::AlignBottom);
    countRow->addStretch();
    heroMain->addLayout(countRow);

    m_medalsWallBar = new QProgressBar(hero);
    m_medalsWallBar->setObjectName(QStringLiteral("fbMedalsWallBar"));
    m_medalsWallBar->setRange(0, 100);
    m_medalsWallBar->setValue(0);
    m_medalsWallBar->setTextVisible(false);
    m_medalsWallBar->setFixedHeight(8);
    m_medalsWallBar->setMaximumWidth(320);
    heroMain->addWidget(m_medalsWallBar);

    m_medalsSummary = new QLabel(tr("勋章记在方块盒子账号上，换设备登录也不会丢。"), hero);
    m_medalsSummary->setObjectName(QStringLiteral("fbMedalsSummary"));
    m_medalsSummary->setWordWrap(true);
    heroMain->addWidget(m_medalsSummary);

    // 「生成分享图」：把整面勋章墙排成一张竖图，方便发群聊 / 社区
    auto *shareRow = new QHBoxLayout();
    shareRow->setContentsMargins(0, 6, 0, 0);
    shareRow->setSpacing(10);
    m_medalsShareButton = new QPushButton(tr("生成分享图"), hero);
    m_medalsShareButton->setObjectName(QStringLiteral("fbBtnPrimary"));
    m_medalsShareButton->setCursor(Qt::PointingHandCursor);
    m_medalsShareButton->setFixedHeight(30);
    connect(m_medalsShareButton, &QPushButton::clicked, this,
            &ForumAccountPage::openMedalShareImage);
    shareRow->addWidget(m_medalsShareButton);

    auto *shareHint =
        new QLabel(tr("把点亮的勋章做成一张图，可以直接发群聊或社区"), hero);
    shareHint->setObjectName(QStringLiteral("fbNote"));
    shareHint->setWordWrap(true);
    shareRow->addWidget(shareHint, 1);
    heroMain->addLayout(shareRow);

    heroLayout->addLayout(heroMain, 1);

    // 右侧展示「下一枚要解锁的勋章」，全部点亮时展示最后一枚
    m_medalsShowcase = new QLabel(hero);
    m_medalsShowcase->setFixedSize(72, 72);
    m_medalsShowcase->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(m_medalsShowcase, 0, Qt::AlignVCenter);

    layout->addWidget(hero);

    m_medalsNotice = new QLabel(m_medalsPage);
    m_medalsNotice->setObjectName(QStringLiteral("fbMedalsNotice"));
    m_medalsNotice->setWordWrap(true);
    m_medalsNotice->hide();
    layout->addWidget(m_medalsNotice);

    // ── 勋章卡片：论坛勋章和游戏成就放在同一个瀑布流里 ──
    m_medalsHost = new QWidget(m_medalsPage);
    m_medalFlow = new FlowLayout(m_medalsHost, 0, 12, 12);
    layout->addWidget(m_medalsHost);

    layout->addStretch(1);
    return m_medalsPage;
}

void ForumAccountPage::clearMedalTiles()
{
    if (!m_medalFlow)
        return;
    while (QLayoutItem *item = m_medalFlow->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

void ForumAccountPage::loadMedals(bool force)
{
    if (!m_service->isLoggedIn() || !m_medalsPage)
        return;
    if (m_medalsBusy || (m_medalsLoaded && !force))
        return;

    m_medalsBusy = true;
    // 记下这次请求属于哪个账号：请求回来前换了账号 / 退出登录时直接丢弃，
    // 免得把上一个账号的勋章画到当前账号头上。
    const int accountId = m_service->user().id;
    m_service->getJson(
        QStringLiteral("/api/medals"),
        [this, accountId](const QJsonObject &root) {
            m_medalsBusy = false;
            if (m_service->user().id != accountId)
                return;
            m_medalsLoaded = true;
            m_medals = root.value(QStringLiteral("medals")).toArray();
            m_medalEarned = root.value(QStringLiteral("earned_count")).toInt();
            m_medalTotal = root.value(QStringLiteral("total")).toInt();
            m_gameAchievements = root.value(QStringLiteral("game_achievements")).toArray();
            m_gameAchievementTotal = root.value(QStringLiteral("game_total")).toInt();
            if (m_medalsNotice)
                m_medalsNotice->hide();
            renderMedals();

            // 这次读取新点亮的勋章：合并成一条提示，别一次弹好几张卡片
            const QJsonArray fresh = root.value(QStringLiteral("new_medals")).toArray();
            if (!fresh.isEmpty())
            {
                QStringList names;
                for (const QJsonValue &value : fresh)
                {
                    const QString name =
                        value.toObject().value(QStringLiteral("name")).toString();
                    if (!name.isEmpty())
                        names.append(name);
                }
                if (!names.isEmpty())
                {
                    // 一次点亮很多枚时只报最稀有的几枚，避免提示条被名字撑满
                    const QStringList shownNames =
                        names.size() > 3 ? names.mid(names.size() - 3) : names;
                    QString shown = shownNames.join(tr("、"));
                    if (names.size() > 3)
                        shown = tr("%1 等 %2 枚").arg(shown).arg(names.size());
                    NotificationManager::showSuccess(
                        this, tr("获得新勋章：%1").arg(shown));
                }
            }

            // 进页面之前点了分享图：数据回来了再弹，不用让用户再点一次
            if (m_medalSharePending)
            {
                m_medalSharePending = false;
                openMedalShareImage();
            }
        },
        [this, accountId](const QString &message, int status) {
            Q_UNUSED(status);
            m_medalsBusy = false;
            if (m_service->user().id != accountId)
                return;
            if (m_medalsLoaded)
                return;
            m_medalsSummary->setText(tr("暂时读不到勋章墙，检查一下网络后再试。"));
            m_medalsCount->setText(QStringLiteral("—"));
            m_medalsTotal->setText(QString());
            m_medalsWallBar->setValue(0);
            if (m_medalsNotice)
            {
                m_medalsNotice->setText(message);
                m_medalsNotice->show();
            }
            clearMedalTiles();
            m_gameAchievements = QJsonArray();
            m_gameAchievementTotal = 0;

            if (m_medalSharePending)
            {
                m_medalSharePending = false;
                NotificationManager::showError(
                    this, tr("勋章墙读不出来，分享图暂时生成不了：%1").arg(message));
            }
        });
}

void ForumAccountPage::renderMedals()
{
    if (!m_medalsPage || !m_medalsCount || !m_medalsWallBar)
        return;

    if (!m_medalsLoaded)
    {
        m_medalsSummary->setText(tr("正在读取账号勋章…"));
        if (m_medalsNotice)
            m_medalsNotice->hide();
        clearMedalTiles();
        return;
    }

    const int staticTotal = qMax(m_medalTotal, m_medals.size());
    const int staticEarned = qBound(0, m_medalEarned, staticTotal);

    // 把固定里程碑和实际同步上来的成就合并：命中固定条目的用固定文案，
    // 没命中的模组 / 特殊成就保留动态卡片。
    QHash<QString, QJsonObject> rawById;
    QHash<QString, QJsonObject> rawByTitle;
    for (const QJsonValue &value : m_gameAchievements)
    {
        const QJsonObject raw = value.toObject();
        const QString id = raw.value(QStringLiteral("id")).toString();
        const QString title = raw.value(QStringLiteral("title")).toString();
        if (!id.isEmpty())
            rawById.insert(id.toLower(), raw);
        if (!title.isEmpty())
            rawByTitle.insert(title.toLower(), raw);
    }

    QVector<QJsonObject> gameCards;
    QSet<QString> matchedRaw;
    for (const GameAchievementDef &def : GameAchievementCatalog::all())
    {
        QJsonObject raw;
        bool earned = false;
        auto byId = rawById.constFind(def.id.toLower());
        if (byId != rawById.constEnd())
        {
            raw = byId.value();
            earned = true;
        }
        else
        {
            auto byTitle = rawByTitle.constFind(def.name.toLower());
            if (byTitle != rawByTitle.constEnd())
            {
                raw = byTitle.value();
                earned = true;
            }
        }

        if (!earned)
            continue; // 只展示已经获得的游戏成就，不把整份清单都铺出来

        QJsonObject card;
        card.insert(QStringLiteral("id"), def.id);
        card.insert(QStringLiteral("name"), def.name);
        card.insert(QStringLiteral("condition"), def.condition);
        card.insert(QStringLiteral("difficulty"), def.difficulty);
        card.insert(QStringLiteral("rarity"), QStringLiteral("game"));
        card.insert(QStringLiteral("earned"), true);
        const QString rawId = raw.value(QStringLiteral("id")).toString();
        if (!rawId.isEmpty())
            matchedRaw.insert(rawId.toLower());
        card.insert(QStringLiteral("world"), raw.value(QStringLiteral("world")));
        card.insert(QStringLiteral("awarded_at"), raw.value(QStringLiteral("earned_at")));
        gameCards.append(card);
    }

    for (const QJsonValue &value : m_gameAchievements)
    {
        const QJsonObject raw = value.toObject();
        const QString rawId = raw.value(QStringLiteral("id")).toString();
        if (matchedRaw.contains(rawId.toLower()))
            continue;

        QString title = raw.value(QStringLiteral("title")).toString();
        if (title.isEmpty())
            title = rawId;
        // 同一条成就可能既有真实 id 又有标题指纹 id，固定里程碑已经展示过就不再重复
        if (GameAchievementCatalog::find(title))
            continue;
        QString condition = raw.value(QStringLiteral("condition")).toString();
        if (condition.isEmpty())
            condition = tr("在游戏中达成该成就");

        QJsonObject card;
        card.insert(QStringLiteral("id"), rawId);
        card.insert(QStringLiteral("name"), title);
        card.insert(QStringLiteral("condition"), condition);
        card.insert(QStringLiteral("difficulty"),
                    GameAchievementCatalog::difficultyForId(rawId));
        card.insert(QStringLiteral("rarity"), QStringLiteral("game"));
        card.insert(QStringLiteral("earned"), true);
        card.insert(QStringLiteral("world"), raw.value(QStringLiteral("world")));
        card.insert(QStringLiteral("awarded_at"), raw.value(QStringLiteral("earned_at")));
        gameCards.append(card);
    }

    const int gameEarned = gameCards.size();

    const int total = staticTotal + gameEarned;
    const int earned = staticEarned + gameEarned;

    m_medalsCount->setText(QString::number(earned));
    m_medalsTotal->setText(tr("/ %1 枚").arg(total));
    m_medalsWallBar->setValue(total > 0 ? earned * 100 / total : 0);
    if (staticTotal > 0 && staticEarned >= staticTotal)
        m_medalsSummary->setText(
            tr("论坛勋章已全部点亮，另外记录了 %1 个游戏成就。勋章记在账号上，换设备也不会丢。")
                .arg(gameEarned));
    else
        m_medalsSummary->setText(
            tr("论坛勋章已点亮 %1 / %2 枚，游戏成就 %3 个；还有 %4 枚论坛勋章等你解锁。")
                .arg(staticEarned)
                .arg(staticTotal)
                .arg(gameEarned)
                .arg(qMax(0, staticTotal - staticEarned)));

    // 「下一枚目标」：按从易到难的顺序找第一枚未获得的论坛勋章
    QString showcaseId;
    QString showcaseName;
    for (const QJsonValue &value : m_medals)
    {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("earned")).toBool())
            continue;
        showcaseId = item.value(QStringLiteral("id")).toString();
        showcaseName = item.value(QStringLiteral("name")).toString();
        break;
    }
    if (showcaseId.isEmpty() && !m_medals.isEmpty())
    {
        const QJsonObject last = m_medals.last().toObject();
        showcaseId = last.value(QStringLiteral("id")).toString();
        showcaseName = last.value(QStringLiteral("name")).toString();
    }
    if (m_medalsShowcase)
    {
        if (showcaseId.isEmpty())
        {
            m_medalsShowcase->clear();
            m_medalsShowcase->setToolTip(QString());
        }
        else
        {
            m_medalsShowcase->setPixmap(MedalCatalog::pixmapFor(showcaseId, 64));
            m_medalsShowcase->setToolTip(
                staticEarned >= staticTotal ? tr("论坛勋章已全部点亮")
                                            : tr("下一枚目标：%1").arg(showcaseName));
        }
    }

    clearMedalTiles();

    const Palette p = currentPalette();

    // 论坛勋章和游戏成就共用同一张卡片：同样的宽度、间距、图标尺寸和底部日期行
    auto addCard = [this, &p](const QJsonObject &item, bool isGame) {
        const QString id = item.value(QStringLiteral("id")).toString();
        const QString name = item.value(QStringLiteral("name")).toString();
        const QString condition = item.value(QStringLiteral("condition")).toString();
        const QString description = item.value(QStringLiteral("description")).toString();
        const QString rarity = item.value(QStringLiteral("rarity")).toString();
        const bool isEarned = item.value(QStringLiteral("earned")).toBool();
        const int current = item.value(QStringLiteral("current")).toInt();
        const int target = qMax(1, item.value(QStringLiteral("target")).toInt());
        const int percent = item.value(QStringLiteral("percent")).toInt();
        const QString unit = item.value(QStringLiteral("unit")).toString();
        const qint64 awardedAt =
            static_cast<qint64>(item.value(QStringLiteral("awarded_at")).toDouble());

        auto *tile = new QFrame(m_medalsHost);
        tile->setObjectName(QStringLiteral("fbMedalTile"));
        tile->setProperty("earned", isEarned);
        tile->setProperty("rarity", rarity);
        tile->setFixedWidth(214);
        tile->setMinimumHeight(172);

        if (isGame)
        {
            QString tip = name;
            if (!condition.isEmpty())
                tip += QLatin1Char('\n') + condition;
            const QString world = item.value(QStringLiteral("world")).toString();
            if (awardedAt > 0)
                tip += QLatin1Char('\n')
                     + QDateTime::fromSecsSinceEpoch(awardedAt).toString(
                         QStringLiteral("yyyy-MM-dd HH:mm"));
            if (!world.isEmpty())
                tip += QLatin1Char('\n') + tr("存档：%1").arg(world);
            tip += QLatin1Char('\n') + tr("游戏内成就 · 生存模式 · 未开作弊");
            tile->setToolTip(tip);
        }
        else
        {
            tile->setToolTip(description.isEmpty() ? condition : description);
        }

        auto *tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(14, 14, 14, 14);
        tileLayout->setSpacing(6);

        auto *head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(10);

        auto *icon = new QLabel(tile);
        icon->setFixedSize(48, 48);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(isGame ? MedalCatalog::gamePixmap(id, 48)
                               : MedalCatalog::pixmapFor(id, 48));
        if (!isEarned)
        {
            // 未解锁的图标压暗，但仍看得清是什么主题
            auto *effect = new QGraphicsOpacityEffect(icon);
            effect->setOpacity(0.38);
            icon->setGraphicsEffect(effect);
        }
        head->addWidget(icon, 0, Qt::AlignTop);

        auto *titleColumn = new QVBoxLayout();
        titleColumn->setContentsMargins(0, 0, 0, 0);
        titleColumn->setSpacing(3);

        auto *nameLabel = new QLabel(name, tile);
        nameLabel->setObjectName(QStringLiteral("fbMedalName"));
        nameLabel->setWordWrap(true);
        titleColumn->addWidget(nameLabel);

        QString chipText = MedalCatalog::rarityText(rarity);
        QColor chipColor = MedalCatalog::rarityColor(rarity);
        if (isGame)
        {
            QString difficulty = item.value(QStringLiteral("difficulty")).toString();
            if (difficulty.isEmpty())
                difficulty = tr("普通");
            chipText = difficulty;
            chipColor = GameAchievementCatalog::difficultyColor(difficulty);
        }
        auto *rarityChip = new QLabel(chipText, tile);
        rarityChip->setStyleSheet(
            QStringLiteral("QLabel{color:%1;background:rgba(%2,%3,%4,0.12);"
                           "border:1px solid rgba(%2,%3,%4,0.35);border-radius:8px;"
                           "font-size:10px;font-weight:700;padding:1px 6px;}")
                .arg(chipColor.name())
                .arg(chipColor.red())
                .arg(chipColor.green())
                .arg(chipColor.blue()));
        rarityChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        titleColumn->addWidget(rarityChip, 0, Qt::AlignLeft);
        titleColumn->addStretch();
        head->addLayout(titleColumn, 1);

        tileLayout->addLayout(head);

        auto *conditionLabel = new QLabel(condition, tile);
        conditionLabel->setObjectName(QStringLiteral("fbMedalCondition"));
        conditionLabel->setWordWrap(true);
        conditionLabel->setMinimumHeight(30);
        tileLayout->addWidget(conditionLabel);
        tileLayout->addStretch();

        if (isEarned)
        {
            const QString date =
                awardedAt > 0
                    ? QDateTime::fromSecsSinceEpoch(awardedAt).toString(QStringLiteral("yyyy-MM-dd"))
                    : QString();
            auto *earnedLabel = new QLabel(
                date.isEmpty() ? tr("已点亮") : tr("已点亮 · %1").arg(date), tile);
            earnedLabel->setObjectName(QStringLiteral("fbMedalEarned"));
            QColor accent;
            if (isGame)
            {
                accent = GameAchievementCatalog::difficultyColor(
                    item.value(QStringLiteral("difficulty")).toString());
            }
            else
            {
                accent = MedalCatalog::lookFor(id).accent;
            }
            earnedLabel->setStyleSheet(QStringLiteral("color:%1;").arg(accent.name()));
            tileLayout->addWidget(earnedLabel);
        }
        else if (isGame)
        {
            // 游戏成就只展示已获得的，正常不会走到这里；保留兜底文案
            auto *pendingLabel = new QLabel(tr("游戏成就"), tile);
            pendingLabel->setObjectName(QStringLiteral("fbMedalProgress"));
            tileLayout->addWidget(pendingLabel);
        }
        else if (target > 1)
        {
            const QColor accent = MedalCatalog::lookFor(id).accent;
            auto *bar = new QProgressBar(tile);
            bar->setRange(0, 100);
            bar->setValue(qBound(0, percent, 100));
            bar->setTextVisible(false);
            bar->setFixedHeight(6);
            bar->setStyleSheet(
                QStringLiteral("QProgressBar{background:%1;border:none;border-radius:3px;}"
                               "QProgressBar::chunk{background:%2;border-radius:3px;}")
                    .arg(p.line2)
                    .arg(accent.name()));
            tileLayout->addWidget(bar);

            auto *progress = new QLabel(
                unit.isEmpty() ? tr("%1 / %2").arg(qMin(current, target)).arg(target)
                               : tr("%1 / %2 %3").arg(qMin(current, target)).arg(target).arg(unit),
                tile);
            progress->setObjectName(QStringLiteral("fbMedalProgress"));
            tileLayout->addWidget(progress);
        }
        else
        {
            auto *progress = new QLabel(tr("满足条件后自动点亮"), tile);
            progress->setObjectName(QStringLiteral("fbMedalProgress"));
            tileLayout->addWidget(progress);
        }

        m_medalFlow->addWidget(tile);
    };

    for (const QJsonValue &value : m_medals)
        addCard(value.toObject(), false);

    // 固定里程碑 + 动态游戏成就，全部用同样的卡片接在论坛勋章后面
    for (const QJsonObject &card : gameCards)
        addCard(card, true);
}

void ForumAccountPage::openMedalShareImage()
{
    if (!m_service->isLoggedIn())
    {
        NotificationManager::showInfo(this, tr("登录方块盒子账号后就能生成勋章分享图"));
        return;
    }

    // 勋章墙还没读回来：先拉一次，读完后在 loadMedals 的回包里自动弹图
    if (!m_medalsLoaded)
    {
        m_medalSharePending = true;
        loadMedals(true);
        NotificationManager::showInfo(this, tr("正在读取勋章墙，读完就给你出图"));
        return;
    }

    const int staticTotal = qMax(m_medalTotal, m_medals.size());
    const int staticEarned = qBound(0, m_medalEarned, staticTotal);

    MedalShareCard::Data data;
    const ForumUser user = m_service->user();
    data.displayName = user.displayName;
    data.username = user.username;
    data.avatar = m_avatarRaw;
    data.joinedAt = user.createdAt;
    data.forumEarned = staticEarned;
    data.forumTotal = staticTotal;
    data.gameEarned = m_gameAchievements.size();
    data.earned = staticEarned + data.gameEarned;
    data.total = staticTotal + data.gameEarned;
    data.brand = QColor(currentPalette().brand);
    // 底部写官网地址（不是论坛地址：论坛只在软件里用，分享图对外露出官网）
    data.siteUrl = AssetUrls::originBase();

    // 论坛勋章按服务端的从易到难顺序排；游戏成就接在后面
    for (const QJsonValue &value : m_medals)
    {
        const QJsonObject item = value.toObject();
        if (!item.value(QStringLiteral("earned")).toBool())
        {
            // 第一枚没点亮的当「下一枚目标」：没点亮几枚时分享图下半部分靠它撑起来
            if (data.next.name.isEmpty())
            {
                data.next.name = item.value(QStringLiteral("name")).toString();
                data.next.rarity = item.value(QStringLiteral("rarity")).toString();
                data.next.iconId = item.value(QStringLiteral("id")).toString();
                data.nextProgress = item.value(QStringLiteral("condition")).toString();

                const int target = qMax(1, item.value(QStringLiteral("target")).toInt());
                const int current = item.value(QStringLiteral("current")).toInt();
                const QString unit = item.value(QStringLiteral("unit")).toString();
                if (target > 1)
                {
                    const QString progress =
                        unit.isEmpty()
                            ? tr("%1 / %2").arg(qMin(current, target)).arg(target)
                            : tr("%1 / %2 %3").arg(qMin(current, target)).arg(target).arg(unit);
                    data.nextProgress = data.nextProgress.isEmpty()
                                            ? progress
                                            : data.nextProgress + QStringLiteral("  ·  ")
                                                  + progress;
                }
            }
            continue;
        }
        MedalShareCard::Item entry;
        entry.name = item.value(QStringLiteral("name")).toString();
        entry.rarity = item.value(QStringLiteral("rarity")).toString();
        entry.iconId = item.value(QStringLiteral("id")).toString();
        entry.game = false;
        if (entry.name.isEmpty() || entry.iconId.isEmpty())
            continue;
        data.items.append(entry);
    }

    for (const QJsonValue &value : m_gameAchievements)
    {
        const QJsonObject raw = value.toObject();
        MedalShareCard::Item entry;
        entry.iconId = raw.value(QStringLiteral("id")).toString();
        entry.name = raw.value(QStringLiteral("title")).toString();
        if (entry.name.isEmpty())
            entry.name = entry.iconId;
        if (entry.name.isEmpty())
            continue;
        entry.rarity = QStringLiteral("game");
        entry.game = true;
        data.items.append(entry);
    }

    const QImage poster = MedalShareCard::buildPoster(data);
    MedalShareDialog::show(this, poster, MedalShareCard::suggestedFileName());
}

/** 账户页「我的反馈」条目的状态徽章（沿用反馈页的绿 / 紫 / 灰配色） */
QLabel *makeFeedbackStateBadge(QWidget *parent, const QString &status)
{
    QLabel *badge = new QLabel(parent);
    badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (status == QStringLiteral("resolved"))
    {
        badge->setText(QLabel::tr("已处理"));
        badge->setStyleSheet(QStringLiteral(
            "QLabel{background:#8250df;color:#ffffff;border:none;border-radius:9px;"
            "font-size:11px;font-weight:700;padding:2px 9px;}"));
    }
    else if (status == QStringLiteral("ignored"))
    {
        badge->setText(QLabel::tr("已忽略"));
        badge->setStyleSheet(QStringLiteral(
            "QLabel{background:rgba(128,128,128,0.18);color:rgba(128,128,128,1);"
            "border:none;border-radius:9px;font-size:11px;font-weight:700;padding:2px 9px;}"));
    }
    else
    {
        badge->setText(QLabel::tr("待处理"));
        badge->setStyleSheet(QStringLiteral(
            "QLabel{background:#10b981;color:#ffffff;border:none;border-radius:9px;"
            "font-size:11px;font-weight:700;padding:2px 9px;}"));
    }
    return badge;
}

void ForumAccountPage::loadMyFeedbacks(bool force)
{
    if (!m_service->isLoggedIn() || !m_feedbackListLayout)
        return;

    if (m_myFeedbacksLoaded && !force)
    {
        renderMyFeedbacks();
        return;
    }

    if (m_feedbackRefreshButton)
        m_feedbackRefreshButton->setEnabled(false);
    clearLayout(m_feedbackListLayout);
    auto *loading = makeMutedLabel(tr("正在加载反馈…"), m_feedbackListLayout->parentWidget(), "fbEmpty");
    loading->setAlignment(Qt::AlignCenter);
    m_feedbackListLayout->addSpacing(24);
    m_feedbackListLayout->addWidget(loading);
    m_feedbackListLayout->addSpacing(24);
    m_feedbackListLayout->addStretch();

    FeedbackService::instance()->fetchMyFeedbacks(
        1,
        [this](const QVector<FeedbackItem> &items, const ForumPagination &) {
            if (m_feedbackRefreshButton)
                m_feedbackRefreshButton->setEnabled(true);
            m_myFeedbacks = items;
            m_myFeedbacksLoaded = true;
            if (m_feedbackListLayout && m_activeTab == FeedbackTab)
                renderMyFeedbacks();
        },
        [this](const QString &message, int) {
            if (m_feedbackRefreshButton)
                m_feedbackRefreshButton->setEnabled(true);
            if (!m_feedbackListLayout)
                return;
            clearLayout(m_feedbackListLayout);
            auto *error = makeMutedLabel(tr("加载失败：%1").arg(message),
                                         m_feedbackListLayout->parentWidget(), "fbEmpty");
            error->setAlignment(Qt::AlignCenter);
            m_feedbackListLayout->addSpacing(24);
            m_feedbackListLayout->addWidget(error);
            m_feedbackListLayout->addSpacing(24);
            m_feedbackListLayout->addStretch();
        });
}

/** 反馈正文里的 GitHub 模板标题（### xxx）转成加粗富文本 */
QString renderFeedbackContent(const QString &content)
{
    const QStringList lines = content.split(QLatin1Char('\n'));
    QStringList out;
    for (const QString &line : lines)
    {
        if (line.startsWith(QStringLiteral("### ")))
            out.append(QStringLiteral("<b>%1</b>").arg(line.mid(4).toHtmlEscaped()));
        else
            out.append(line.toHtmlEscaped());
    }
    return out.join(QLatin1String("<br>"));
}

void ForumAccountPage::renderMyFeedbacks()
{
    if (!m_feedbackListLayout)
        return;
    clearLayout(m_feedbackListLayout);

    if (m_myFeedbacks.isEmpty())
    {
        auto *empty = makeMutedLabel(tr("还没有提交过反馈。遇到软件问题，欢迎到左侧「反馈」里提交。"),
                                     m_feedbackListLayout->parentWidget(), "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_feedbackListLayout->addSpacing(24);
        m_feedbackListLayout->addWidget(empty);
        m_feedbackListLayout->addSpacing(24);
        m_feedbackListLayout->addStretch();
        return;
    }

    for (const FeedbackItem &item : m_myFeedbacks)
    {
        auto *card = new QFrame(m_feedbackListLayout->parentWidget());
        card->setObjectName(QStringLiteral("fbCard"));
        auto *rowLayout = new QVBoxLayout(card);
        rowLayout->setContentsMargins(14, 12, 14, 12);
        rowLayout->setSpacing(6);

        // 头行：类型徽章 + 状态徽章 + 来源 + 时间
        auto *headRow = new QHBoxLayout();
        headRow->setSpacing(8);
        auto *typeBadge = new QLabel(card);
        typeBadge->setText(item.type == QStringLiteral("feature") ? tr("功能建议") : tr("问题反馈"));
        typeBadge->setStyleSheet(QStringLiteral(
            "QLabel{background:rgba(128,128,128,0.18);color:rgba(128,128,128,1);border:none;"
            "border-radius:8px;font-size:11px;font-weight:600;padding:1px 8px;}"));
        headRow->addWidget(typeBadge);
        headRow->addWidget(makeFeedbackStateBadge(card, item.status));

        QStringList headParts;
        if (item.source == QStringLiteral("launcher"))
            headParts << (item.appVersion.isEmpty()
                              ? tr("来自启动器")
                              : tr("来自启动器 v%1").arg(item.appVersion));
        else
            headParts << tr("来自网页版");
        auto *sourceLabel = new QLabel(headParts.join(QStringLiteral(" ")), card);
        sourceLabel->setObjectName(QStringLiteral("fbNote"));
        headRow->addWidget(sourceLabel);
        headRow->addStretch(1);
        auto *timeLabel = new QLabel(ForumKit::relativeTime(item.createdAt), card);
        timeLabel->setObjectName(QStringLiteral("fbNote"));
        headRow->addWidget(timeLabel);
        rowLayout->addLayout(headRow);

        // 反馈正文（### 模板标题渲染成加粗）
        auto *contentLabel = new QLabel(renderFeedbackContent(item.content), card);
        contentLabel->setWordWrap(true);
        contentLabel->setTextFormat(Qt::RichText);
        contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rowLayout->addWidget(contentLabel);

        // 管理员回复
        if (!item.reply.isEmpty())
        {
            auto *replyLabel = new QLabel(tr("管理员回复：%1").arg(item.reply), card);
            replyLabel->setObjectName(QStringLiteral("fbNote"));
            replyLabel->setWordWrap(true);
            replyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            replyLabel->setStyleSheet(QStringLiteral(
                "QLabel{background:rgba(128,128,128,0.12);border:1px solid rgba(128,128,128,0.3);"
                "border-radius:10px;padding:9px 12px;}"));
            rowLayout->addWidget(replyLabel);
        }

        m_feedbackListLayout->addWidget(card);
    }
    m_feedbackListLayout->addStretch();
}

void ForumAccountPage::loadPoints(bool force)
{
    if (!m_service->isLoggedIn() || !m_pointsPage)
        return;

    if (m_pointsLoaded && !force)
    {
        renderPoints();
        return;
    }

    if (m_pointsCheckinButton)
        m_pointsCheckinButton->setEnabled(false);
    if (m_buyMakeupButton)
        m_buyMakeupButton->setEnabled(false);
    if (m_useMakeupButton)
        m_useMakeupButton->setEnabled(false);

    m_service->getJson(
        QStringLiteral("/api/points"),
        [this](const QJsonObject &root) {
            m_pointsState = root;
            m_pointsLoaded = true;
            renderPoints();
            if (m_statPoints)
                m_statPoints->setText(QString::number(root.value(QStringLiteral("points")).toInt()));
            // 充值档位跟着一起拉（只拉一次，之后靠支付面板自己刷新）
            loadPointsShop();
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            if (m_pointsBalance)
                m_pointsBalance->setText(QStringLiteral("—"));
            if (m_pointsStreak)
                m_pointsStreak->setText(message);
            if (m_pointsReward)
                m_pointsReward->clear();
            if (m_pointsCheckinButton)
            {
                m_pointsCheckinButton->setEnabled(false);
                m_pointsCheckinButton->setText(tr("签到"));
            }
            if (m_makeupCount)
                m_makeupCount->setText(tr("— 张"));
            if (m_buyMakeupButton)
                m_buyMakeupButton->setEnabled(false);
            if (m_useMakeupButton)
                m_useMakeupButton->setEnabled(false);
            if (m_pointsHistoryLayout)
            {
                clearLayout(m_pointsHistoryLayout);
                auto *error = makeMutedLabel(message, m_pointsHistoryLayout->parentWidget(), "fbEmpty");
                error->setAlignment(Qt::AlignCenter);
                m_pointsHistoryLayout->addSpacing(16);
                m_pointsHistoryLayout->addWidget(error);
                m_pointsHistoryLayout->addStretch();
            }
        });
}

void ForumAccountPage::refreshPointsDayCards()
{
    const QJsonArray week = m_pointsState.value(QStringLiteral("week")).toArray();
    for (int i = 0; i < m_pointsDayCards.size(); ++i)
    {
        QFrame *tile = m_pointsDayCards.at(i);
        const QJsonObject day = i < week.size() ? week.at(i).toObject() : QJsonObject();
        const int reward = day.value(QStringLiteral("reward")).toInt();
        const bool checked = day.value(QStringLiteral("checked")).toBool();
        const bool current = day.value(QStringLiteral("current")).toBool();

        if (i < m_pointsDayRewards.size())
            m_pointsDayRewards.at(i)->setText(reward > 0 ? QStringLiteral("+%1").arg(reward)
                                                          : QStringLiteral("—"));
        if (i < m_pointsDayStates.size())
            m_pointsDayStates.at(i)->setText(checked ? tr("已领") : current ? tr("今天") : tr("待领"));

        tile->setProperty("checked", checked);
        tile->setProperty("current", current);
        // 子标签的颜色也由父卡的 checked 属性决定，父卡和两个标签都要重新套样式
        const QList<QWidget *> styled = {tile,
                                         i < m_pointsDayRewards.size() ? m_pointsDayRewards.at(i) : nullptr,
                                         i < m_pointsDayStates.size() ? m_pointsDayStates.at(i) : nullptr};
        for (QWidget *widget : styled)
        {
            if (!widget)
                continue;
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
        }
    }
}

void ForumAccountPage::renderPoints()
{
    if (!m_pointsPage || !m_pointsBalance)
        return;

    if (!m_pointsLoaded)
    {
        m_pointsBalance->setText(QStringLiteral("—"));
        m_pointsStreak->setText(tr("正在加载签到进度…"));
        m_pointsReward->clear();
        if (m_pointsCheckinButton)
            m_pointsCheckinButton->setEnabled(false);
        if (m_makeupCount)
            m_makeupCount->setText(tr("— 张"));
        if (m_buyMakeupButton)
            m_buyMakeupButton->setEnabled(false);
        if (m_useMakeupButton)
            m_useMakeupButton->setEnabled(false);
        if (m_pointsHistoryLayout)
        {
            clearLayout(m_pointsHistoryLayout);
            auto *loading =
                makeMutedLabel(tr("正在加载签到记录…"), m_pointsHistoryLayout->parentWidget(), "fbEmpty");
            loading->setAlignment(Qt::AlignCenter);
            m_pointsHistoryLayout->addSpacing(16);
            m_pointsHistoryLayout->addWidget(loading);
            m_pointsHistoryLayout->addSpacing(16);
            m_pointsHistoryLayout->addStretch();
        }
        return;
    }

    const int points = m_pointsState.value(QStringLiteral("points")).toInt();
    const bool checked = m_pointsState.value(QStringLiteral("checked_in_today")).toBool();
    const int streak = m_pointsState.value(QStringLiteral("streak")).toInt();
    const int rewardToday = m_pointsState.value(QStringLiteral("reward_today")).toInt();
    const int rewardNext = m_pointsState.value(QStringLiteral("reward_next")).toInt();
    const QJsonObject sunday = m_pointsState.value(QStringLiteral("sunday")).toObject();
    const bool isSunday = sunday.value(QStringLiteral("is_sunday")).toBool();
    const int giftPending = sunday.value(QStringLiteral("gift_pending")).toInt();

    m_pointsBalance->setText(tr("%1 方块").arg(points));
    m_pointsStreak->setText(streak > 0 ? tr("连续签到 %1 天").arg(streak)
                                       : tr("今天签到，开始你的连续记录"));
    if (checked)
    {
        m_pointsReward->setText(
            tr("今天已领 %1 方块，明天可得 %2 方块").arg(rewardToday).arg(rewardNext));
        if (m_pointsCheckinButton)
        {
            m_pointsCheckinButton->setText(tr("今天已签到"));
            m_pointsCheckinButton->setEnabled(false);
        }
    }
    else
    {
        m_pointsReward->setText(tr("今天签到可得 %1 方块").arg(rewardToday));
        if (m_pointsCheckinButton)
        {
            m_pointsCheckinButton->setText(tr("签到领 %1 方块").arg(rewardToday));
            m_pointsCheckinButton->setEnabled(true);
        }
    }

    if (m_pointsGift)
    {
        if (giftPending > 0)
            m_pointsGift->setText(
                tr("已收到 %1 份神秘礼包，内容开放后可以开启。").arg(giftPending));
        else if (isSunday)
            m_pointsGift->setText(tr("今天是周日，签到后就能收到本周的神秘礼包（内容即将上线）。"));
        else
            m_pointsGift->setText(tr("每周天签到都会收到一份神秘礼包，内容正在准备中。"));
    }

    const QJsonObject makeup = m_pointsState.value(QStringLiteral("makeup")).toObject();
    const int makeupCards = m_pointsState.value(QStringLiteral("makeup_cards")).toInt();
    const bool yesterdayChecked = makeup.value(QStringLiteral("yesterday_checked")).toBool();
    const int makeupPrice = makeup.value(QStringLiteral("price")).toInt(20);
    if (m_makeupCount)
        m_makeupCount->setText(tr("%1 张").arg(makeupCards));
    if (m_makeupDesc)
    {
        if (yesterdayChecked)
            m_makeupDesc->setText(tr("昨天已经签到过了，不用补签。"));
        else if (makeupCards > 0)
            m_makeupDesc->setText(tr("用一张补签卡补上昨天的签到，连续记录接着算。"));
        else
            m_makeupDesc->setText(
                tr("昨天漏签时用一张补上，连续签到接着算；每张 %1 方块。").arg(makeupPrice));
    }
    if (m_buyMakeupButton)
    {
        m_buyMakeupButton->setText(tr("购买补签卡（%1 方块）").arg(makeupPrice));
        m_buyMakeupButton->setEnabled(makeup.value(QStringLiteral("can_buy")).toBool());
        m_buyMakeupButton->setToolTip(
            makeup.value(QStringLiteral("can_buy")).toBool()
                ? QString()
                : tr("方块不够，先签到攒一些吧"));
    }
    if (m_useMakeupButton)
    {
        m_useMakeupButton->setEnabled(makeup.value(QStringLiteral("can_use")).toBool());
        if (yesterdayChecked)
            m_useMakeupButton->setText(tr("昨天已签到"));
        else if (makeupCards < 1)
            m_useMakeupButton->setText(tr("先买一张补签卡"));
        else
            m_useMakeupButton->setText(tr("补签昨天"));
    }

    refreshPointsDayCards();

    if (!m_pointsHistoryLayout)
        return;
    clearLayout(m_pointsHistoryLayout);

    const QJsonArray history = m_pointsState.value(QStringLiteral("history")).toArray();
    if (history.isEmpty())
    {
        auto *empty = makeMutedLabel(tr("还没有签到记录，今天签到就能领第一笔方块。"),
                                     m_pointsHistoryLayout->parentWidget(), "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_pointsHistoryLayout->addSpacing(18);
        m_pointsHistoryLayout->addWidget(empty);
        m_pointsHistoryLayout->addSpacing(18);
        m_pointsHistoryLayout->addStretch();
        return;
    }

    for (const QJsonValue &value : history)
    {
        const QJsonObject row = value.toObject();
        auto *item = new QFrame(m_pointsHistoryLayout->parentWidget());
        item->setObjectName(QStringLiteral("fbPointsHistoryRow"));
        auto *rowLayout = new QHBoxLayout(item);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(10);

        const QString dayText = row.value(QStringLiteral("day")).toString();
        const QDate day = QDate::fromString(dayText, QStringLiteral("yyyy-MM-dd"));
        auto *dayLabel = new QLabel(
            day.isValid()
                ? (day.year() == QDate::currentDate().year()
                       ? day.toString(tr("M月d日"))
                       : day.toString(QStringLiteral("yyyy-MM-dd")))
                : dayText,
            item);
        dayLabel->setObjectName(QStringLiteral("fbPointsHistoryDay"));
        rowLayout->addWidget(dayLabel);

        auto *streakLabel =
            new QLabel(tr("连续第 %1 天").arg(row.value(QStringLiteral("streak_day")).toInt()), item);
        streakLabel->setObjectName(QStringLiteral("fbNote"));
        rowLayout->addWidget(streakLabel);

        if (row.value(QStringLiteral("sunday_gift")).toBool())
        {
            auto *giftChip = new QLabel(tr("神秘礼包"), item);
            giftChip->setObjectName(QStringLiteral("fbChip"));
            rowLayout->addWidget(giftChip);
        }

        rowLayout->addStretch();

        auto *amount = new QLabel(tr("+%1 方块").arg(row.value(QStringLiteral("points")).toInt()), item);
        amount->setObjectName(QStringLiteral("fbPointsHistoryAmount"));
        rowLayout->addWidget(amount);

        m_pointsHistoryLayout->addWidget(item);
    }
    m_pointsHistoryLayout->addStretch();
}

void ForumAccountPage::checkInPoints()
{
    if (m_pointsBusy || !m_service->isLoggedIn())
        return;

    m_pointsBusy = true;
    if (m_pointsCheckinButton)
    {
        m_pointsCheckinButton->setEnabled(false);
        m_pointsCheckinButton->setText(tr("签到中…"));
    }

    m_service->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/points/check-in"), QJsonObject(),
        [this](const QJsonObject &result) {
            m_pointsBusy = false;
            m_pointsLoaded = true;
            m_pointsState = result;
            renderPoints();
            if (m_statPoints)
                m_statPoints->setText(QString::number(result.value(QStringLiteral("points")).toInt()));
            const int awarded = result.value(QStringLiteral("awarded")).toInt();
            if (result.value(QStringLiteral("sunday_gift")).toBool())
                NotificationManager::showSuccess(
                    this, tr("签到成功：+%1 方块，神秘礼包已记到账号上").arg(awarded));
            else
                NotificationManager::showSuccess(
                    this, tr("签到成功：+%1 方块，连续签到第 %2 天")
                              .arg(awarded)
                              .arg(result.value(QStringLiteral("streak_day")).toInt()));
            m_service->refreshProfile();
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            m_pointsBusy = false;
            if (m_pointsCheckinButton)
                m_pointsCheckinButton->setEnabled(true);
            if (message.contains(QStringLiteral("已经签到")))
                loadPoints(true);
            else
                renderPoints();
            NotificationManager::showError(this, message);
        });
}

void ForumAccountPage::buyMakeupCard()
{
    if (m_pointsBusy || !m_service->isLoggedIn())
        return;

    m_pointsBusy = true;
    if (m_buyMakeupButton)
        m_buyMakeupButton->setEnabled(false);

    m_service->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/points/makeup-card/buy"), QJsonObject(),
        [this](const QJsonObject &result) {
            m_pointsBusy = false;
            m_pointsLoaded = true;
            m_pointsState = result;
            renderPoints();
            if (m_statPoints)
                m_statPoints->setText(QString::number(result.value(QStringLiteral("points")).toInt()));
            NotificationManager::showSuccess(this, tr("已经买到一张补签卡"));
            m_service->refreshProfile();
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            m_pointsBusy = false;
            renderPoints();
            NotificationManager::showError(this, message);
        });
}

void ForumAccountPage::useMakeupCard()
{
    if (m_pointsBusy || !m_service->isLoggedIn())
        return;

    const QJsonObject makeup = m_pointsState.value(QStringLiteral("makeup")).toObject();
    if (!makeup.value(QStringLiteral("can_use")).toBool())
        return;
    const QString day = makeup.value(QStringLiteral("yesterday")).toString();
    if (AppMessageBox::question(this, tr("使用补签卡"),
                                tr("用一张补签卡补签 %1？").arg(day),
                                AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No) !=
        AppMessageBox::Yes)
        return;

    m_pointsBusy = true;
    if (m_useMakeupButton)
        m_useMakeupButton->setEnabled(false);

    m_service->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/points/makeup"), QJsonObject(),
        [this](const QJsonObject &result) {
            m_pointsBusy = false;
            m_pointsLoaded = true;
            m_pointsState = result;
            renderPoints();
            if (m_statPoints)
                m_statPoints->setText(QString::number(result.value(QStringLiteral("points")).toInt()));
            QString message = tr("补签成功：+%1 方块")
                                  .arg(result.value(QStringLiteral("awarded")).toInt());
            const int todayBonus = result.value(QStringLiteral("today_bonus")).toInt();
            if (todayBonus > 0)
                message += tr("，今天连续奖励补差 +%1 方块").arg(todayBonus);
            if (result.value(QStringLiteral("sunday_gift")).toBool())
                message += tr("，神秘礼包已记到账号上");
            NotificationManager::showSuccess(this, message);
            m_service->refreshProfile();
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            m_pointsBusy = false;
            loadPoints(true);
            NotificationManager::showError(this, message);
        });
}

// ════════════════════════════════════════════════════════════════ 积分页 · 购买方块

void ForumAccountPage::loadPointsShop()
{
    if (!m_service->isLoggedIn() || !m_pointsShop)
        return;
    if (m_pointsShopLoaded)
        return;

    m_service->getJson(
        QStringLiteral("/api/points/shop"),
        [this](const QJsonObject &root) {
            m_pointsShopLoaded = true;
            renderPointsShop(root);
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            // 拉不到档位就先收起来，别挡住签到那边
            m_pointsShopLoaded = true;
            if (m_pointsShopNote)
                m_pointsShopNote->setText(message);
            if (m_pointsShop)
                m_pointsShop->hide();
        });
}

void ForumAccountPage::renderPointsShop(const QJsonObject &shop)
{
    if (!m_pointsShop || !m_pointsShopGrid)
        return;

    while (QLayoutItem *item = m_pointsShopGrid->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_pointsShopButtons.clear();

    const bool enabled = shop.value(QStringLiteral("enabled")).toBool();
    const QJsonArray packages = shop.value(QStringLiteral("packages")).toArray();
    if (!enabled || packages.isEmpty())
    {
        // 论坛没配商户号：整块藏起来，免得用户点了才发现买不了
        if (m_pointsShopNote)
        {
            const QString note = shop.value(QStringLiteral("note")).toString();
            m_pointsShopNote->setText(note.isEmpty() ? tr("充值通道还没开通，先签到领方块吧。") : note);
        }
        m_pointsShop->hide();
        return;
    }

    if (m_pointsShopNote)
        m_pointsShopNote->setText(tr("付款后自动到账"));

    QWidget *host = m_pointsShopGrid->parentWidget();
    for (const QJsonValue &value : packages)
    {
        const QJsonObject pkg = value.toObject();
        const QString id = pkg.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        auto *card = new QFrame(host);
        card->setObjectName(QStringLiteral("fbShopCard"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(10, 10, 10, 10);
        cardLayout->setSpacing(3);

        auto *name = new QLabel(pkg.value(QStringLiteral("name")).toString(), card);
        name->setObjectName(QStringLiteral("fbShopName"));
        name->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(name);

        auto *points = new QLabel(QStringLiteral("+%1").arg(pkg.value(QStringLiteral("points")).toInt()),
                                  card);
        points->setObjectName(QStringLiteral("fbShopPoints"));
        points->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(points);

        auto *unit = new QLabel(tr("方块"), card);
        unit->setObjectName(QStringLiteral("fbShopUnit"));
        unit->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(unit);

        auto *price = new QLabel(QStringLiteral("¥%1").arg(pkg.value(QStringLiteral("price")).toString()),
                                 card);
        price->setObjectName(QStringLiteral("fbShopPrice"));
        price->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(price);

        auto *buy = new QPushButton(tr("购买"), card);
        buy->setObjectName(QStringLiteral("fbShopBuy"));
        buy->setCursor(Qt::PointingHandCursor);
        buy->setFixedHeight(28);
        connect(buy, &QPushButton::clicked, this, [this, id, buy]() { buyPointsPackage(id, buy); });
        cardLayout->addWidget(buy);

        m_pointsShopGrid->addWidget(card, 1);
        m_pointsShopButtons.insert(id, buy);
    }
    m_pointsShopGrid->addStretch(0);

    m_pointsShop->show();
}

void ForumAccountPage::buyPointsPackage(const QString &packageId, QPushButton *button)
{
    if (m_pointsBusy || !m_service->isLoggedIn() || packageId.isEmpty())
        return;

    m_pointsBusy = true;
    if (button)
    {
        button->setEnabled(false);
        button->setText(tr("下单中…"));
    }

    QJsonObject body;
    body.insert(QStringLiteral("package"), packageId);

    m_service->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/points/shop/order"), body,
        [this, button](const QJsonObject &result) {
            m_pointsBusy = false;
            if (button)
            {
                button->setEnabled(true);
                button->setText(tr("购买"));
            }

            const QJsonObject order = result.value(QStringLiteral("order")).toObject();
            const QString orderNo = order.value(QStringLiteral("out_trade_no")).toString();
            const QString payUrl = result.value(QStringLiteral("pay_url")).toString();
            if (orderNo.isEmpty())
            {
                NotificationManager::showError(this, tr("下单失败：没有拿到订单号，请再试一次"));
                return;
            }

            const QString title = tr("%1 %2 方块 · ¥%3")
                                      .arg(order.value(QStringLiteral("package_name")).toString())
                                      .arg(order.value(QStringLiteral("points")).toInt())
                                      .arg(order.value(QStringLiteral("price")).toString());
            startPayPolling(orderNo, order.value(QStringLiteral("points")).toInt(), title, payUrl);

            // 浏览器打开收银台；打不开就提示用户点面板里的按钮
            if (payUrl.isEmpty() || !QDesktopServices::openUrl(QUrl(payUrl)))
            {
                NotificationManager::showInfo(
                    this, tr("没能自动打开浏览器，点下面的「打开支付页」继续付款"));
            }
        },
        [this, button](const QString &message, int status) {
            Q_UNUSED(status);
            m_pointsBusy = false;
            if (button)
            {
                button->setEnabled(true);
                button->setText(tr("购买"));
            }
            NotificationManager::showError(this, message);
            // 服务端说通道没开通：重新拉一次档位状态，入口该收起就收起
            if (message.contains(QStringLiteral("还没开通")))
            {
                m_pointsShopLoaded = false;
                loadPointsShop();
            }
        });
}

void ForumAccountPage::startPayPolling(const QString &outTradeNo, int points, const QString &title,
                                       const QString &payUrl)
{
    if (m_payPollTimer)
        m_payPollTimer->stop();

    m_payOrderNo = outTradeNo;
    m_payOrderPoints = points;
    m_payOrderTitle = title;
    m_payUrl = payUrl;
    m_payPollTries = 0;

    if (m_pointsShopPayPanel)
    {
        if (m_pointsShopPayTitle)
            m_pointsShopPayTitle->setText(tr("待支付：%1").arg(title));
        if (m_pointsShopPayText)
            m_pointsShopPayText->setText(
                tr("在浏览器里付完款，这里会自动刷新余额；没弹出浏览器就点「打开支付页」。"));
        if (m_pointsShopPayOpen)
            m_pointsShopPayOpen->setEnabled(!payUrl.isEmpty());
        if (m_pointsShopPayCheck)
            m_pointsShopPayCheck->setEnabled(true);
        m_pointsShopPayPanel->show();
    }

    if (!m_payPollTimer)
    {
        m_payPollTimer = new QTimer(this);
        m_payPollTimer->setInterval(3000);
        connect(m_payPollTimer, &QTimer::timeout, this, [this]() { checkPayOrder(false); });
    }
    m_payPollTimer->start();
}

void ForumAccountPage::stopPayPolling()
{
    if (m_payPollTimer)
        m_payPollTimer->stop();
    m_payOrderNo.clear();
    m_payPollTries = 0;
}

void ForumAccountPage::checkPayOrder(bool manual)
{
    if (m_payOrderNo.isEmpty() || !m_service->isLoggedIn())
    {
        if (manual)
            NotificationManager::showInfo(this, tr("现在没有正在等待的订单"));
        return;
    }

    if (manual && m_pointsShopPayCheck)
        m_pointsShopPayCheck->setEnabled(false);

    const QString orderNo = m_payOrderNo;
    m_payPollTries += 1;

    m_service->getJson(
        QStringLiteral("/api/points/shop/order/%1").arg(orderNo),
        [this, orderNo, manual](const QJsonObject &root) {
            if (m_pointsShopPayCheck)
                m_pointsShopPayCheck->setEnabled(true);
            if (m_payOrderNo != orderNo)
                return;

            const QJsonObject order = root.value(QStringLiteral("order")).toObject();
            const int points = root.value(QStringLiteral("points")).toInt();
            if (order.value(QStringLiteral("status")).toString() == QStringLiteral("paid"))
            {
                const int gained = order.value(QStringLiteral("points")).toInt();
                stopPayPolling();
                if (m_pointsShopPayPanel)
                    m_pointsShopPayPanel->hide();
                if (m_statPoints)
                    m_statPoints->setText(QString::number(points));
                NotificationManager::showSuccess(
                    this, tr("支付成功：+%1 方块已到账").arg(gained));
                loadPoints(true);
                m_service->refreshProfile();
                return;
            }

            if (m_payPollTries >= 100)
            {
                if (m_payPollTimer)
                    m_payPollTimer->stop();
                if (m_pointsShopPayText)
                    m_pointsShopPayText->setText(
                        tr("还没收到付款。支付页还开着就继续付，付完点「我已完成支付」。"));
            }
            else if (manual)
            {
                NotificationManager::showInfo(this, tr("还没收到付款，付完稍等一下再点一次"));
            }
        },
        [this, manual](const QString &message, int status) {
            Q_UNUSED(status);
            if (m_pointsShopPayCheck)
                m_pointsShopPayCheck->setEnabled(true);
            if (manual)
                NotificationManager::showError(this, message);
        });
}

void ForumAccountPage::loadNotifications()
{
    ForumContentService::instance()->fetchNotifications(
        1,
        [this](const QVector<ForumNotificationItem> &items, int unread, const ForumPagination &) {
            m_notifications = items;
            m_notificationsLoaded = true;
            Q_UNUSED(unread);
            if (m_activeTab == NotificationsTab)
                renderNotifications();
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            m_notificationsLoaded = true;
            if (!m_notificationListLayout)
                return;

            clearLayout(m_notificationListLayout);
            QWidget *host = m_notificationListLayout->parentWidget();

            if (auto *chip = findChild<QLabel *>(QStringLiteral("fbNotifUnreadChip")))
                chip->hide();
            if (m_markAllReadButton)
                m_markAllReadButton->setEnabled(false);
            setTabHint(tr("通知加载失败"));

            auto *error = new QLabel(message, host);
            error->setObjectName(QStringLiteral("fbNotifAlertText"));
            error->setWordWrap(true);
            error->setAlignment(Qt::AlignCenter);

            auto *retry = new QPushButton(tr("重新加载"), host);
            retry->setObjectName(QStringLiteral("fbBtnGhost"));
            retry->setCursor(Qt::PointingHandCursor);
            retry->setFixedHeight(30);
            connect(retry, &QPushButton::clicked, this, [this]() {
                m_notificationsLoaded = false;
                renderNotifications();
                loadNotifications();
            });

            auto *alert = new QFrame(host);
            alert->setObjectName(QStringLiteral("fbNotifAlert"));
            alert->setMaximumWidth(560);
            auto *alertLayout = new QHBoxLayout(alert);
            alertLayout->setContentsMargins(14, 12, 14, 12);
            alertLayout->setSpacing(10);

            auto *alertMark = new QLabel(QStringLiteral("!"), alert);
            alertMark->setObjectName(QStringLiteral("fbNotifAlertMark"));
            alertMark->setFixedSize(16, 16);
            alertMark->setAlignment(Qt::AlignCenter);
            alertLayout->addWidget(alertMark, 0, Qt::AlignTop);

            error->setParent(alert);
            alertLayout->addWidget(error, 1);
            retry->setParent(alert);
            alertLayout->addWidget(retry, 0, Qt::AlignVCenter);

            m_notificationListLayout->addSpacing(24);
            m_notificationListLayout->addWidget(alert, 0, Qt::AlignHCenter);
            m_notificationListLayout->addStretch();
        });
}

void ForumAccountPage::renderNotifications()
{
    if (!m_notificationListLayout)
        return;

    clearLayout(m_notificationListLayout);

    QWidget *host = m_notificationListLayout->parentWidget();
    const Palette pal = currentPalette();

    const int unreadRows = std::count_if(m_notifications.cbegin(), m_notifications.cend(),
                                         [](const ForumNotificationItem &item) {
                                             return item.unread;
                                         });

    // 页头统计与按钮状态：页头是常驻控件，这里只更新不重建
    if (auto *chip = findChild<QLabel *>(QStringLiteral("fbNotifUnreadChip")))
    {
        chip->setText(tr("%1 组未读").arg(unreadRows));
        chip->setVisible(unreadRows > 0);
    }
    if (m_markAllReadButton)
        m_markAllReadButton->setEnabled(unreadRows > 0);

    if (m_notifications.isEmpty())
    {
        if (!m_notificationsLoaded)
        {
            renderLoading(m_notificationListLayout);
            return;
        }

        // 空态：图标 + 标题 + 说明
        auto *box = new QWidget(host);
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->setSpacing(8);

        auto *icon = new QLabel(box);
        icon->setObjectName(QStringLiteral("fbNotifEmptyIcon"));
        icon->setFixedSize(54, 54);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/announce.svg"),
                                                    QColor(pal.ink3), 22)
                            .pixmap(22, 22));
        boxLayout->addWidget(icon, 0, Qt::AlignHCenter);

        auto *title = new QLabel(tr("还没有通知"), box);
        title->setObjectName(QStringLiteral("fbNotifEmptyTitle"));
        title->setAlignment(Qt::AlignCenter);
        boxLayout->addWidget(title);

        auto *desc = makeMutedLabel(tr("有人回复你的主题、提到你，或论坛发布公告时，消息会收进这里"),
                                    box, "fbEmpty");
        desc->setAlignment(Qt::AlignCenter);
        boxLayout->addWidget(desc);

        m_notificationListLayout->addSpacing(30);
        m_notificationListLayout->addWidget(box);
        m_notificationListLayout->addStretch();
        setTabHint(tr("暂无通知"));
        return;
    }

    QString currentGroup;
    for (const ForumNotificationItem &item : m_notifications)
    {
        // 按日期分组：今天 / 昨天 / 具体日期
        const QString group = notificationDayGroup(item.createdAt);
        if (group != currentGroup)
        {
            currentGroup = group;
            auto *groupLabel = new QLabel(group, host);
            groupLabel->setObjectName(QStringLiteral("fbNotifGroup"));
            if (m_notificationListLayout->count() > 0)
                m_notificationListLayout->addSpacing(6);
            m_notificationListLayout->addWidget(groupLabel);
        }

        auto *row = new ClickableFrame(host);
        row->setObjectName(QStringLiteral("fbNotifRow"));
        row->setProperty("unread", item.unread ? QStringLiteral("true") : QStringLiteral("false"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 10, 12, 10);
        rowLayout->setSpacing(11);

        // 未读左侧强调条
        auto *accent = new QFrame(row);
        accent->setObjectName(QStringLiteral("fbNotifAccent"));
        accent->setFixedWidth(3);
        accent->setVisible(item.unread);
        rowLayout->addWidget(accent);

        // 左侧类型图标：回复 / 提及 / 系统 / 好友，底色区分类型，未读用主题色
        QString iconPath;
        QString typeKey = QStringLiteral("reply");
        if (item.type == QStringLiteral("mention"))
        {
            iconPath = QStringLiteral(":/Images/Icons/sparkles.svg");
            typeKey = QStringLiteral("mention");
        }
        else if (item.type == QStringLiteral("system"))
        {
            iconPath = QStringLiteral(":/Images/Icons/announce.svg");
            typeKey = QStringLiteral("system");
        }
        else if (item.type == QStringLiteral("friend") || item.type == QStringLiteral("friend_accepted"))
        {
            iconPath = QStringLiteral(":/Images/Icons/topbar_notify.svg");
            typeKey = QStringLiteral("friend");
        }
        else
        {
            iconPath = QStringLiteral(":/Images/Icons/send.svg");
        }

        auto *typeIcon = new QLabel(row);
        typeIcon->setObjectName(QStringLiteral("fbNotifIcon"));
        typeIcon->setProperty("type", typeKey);
        typeIcon->setFixedSize(36, 36);
        typeIcon->setAlignment(Qt::AlignCenter);
        typeIcon->setPixmap(IconHelper::loadColoredIcon(
                                iconPath,
                                item.unread ? QColor(pal.brand) : QColor(pal.ink3), 17)
                                .pixmap(17, 17));
        rowLayout->addWidget(typeIcon, 0, Qt::AlignTop);

        auto *contentLayout = new QVBoxLayout();
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(5);

        // 标题行：未读圆点 + 主题标题
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(7);
        if (item.unread)
        {
            auto *dot = new QLabel(row);
            dot->setObjectName(QStringLiteral("fbNotifDot"));
            dot->setFixedSize(6, 6);
            titleRow->addWidget(dot, 0, Qt::AlignVCenter);
        }
        auto *title = new ElidedLabel(row);
        title->setObjectName(QStringLiteral("fbNotifTitle"));
        title->setFullText(item.threadTitle.isEmpty() ? tr("（无标题）") : item.threadTitle);
        title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        titleRow->addWidget(title, 1);
        contentLayout->addLayout(titleRow);

        // 副行：谁 + 干了什么，人名加粗
        QString actors = item.actorNames.isEmpty() ? QString() : item.actorNames.join(QStringLiteral("、"));
        if (item.actorNames.size() > 3)
            actors = item.actorNames.mid(0, 3).join(QStringLiteral("、")) +
                     tr(" 等 %1 人").arg(item.actorNames.size());

        QString action;
        if (item.type == QStringLiteral("mention"))
            action = tr("提到了你");
        else if (item.type == QStringLiteral("friend"))
            action = tr("申请加你为好友");
        else if (item.type == QStringLiteral("friend_accepted"))
            action = tr("同意了你的好友申请");
        else if (item.type == QStringLiteral("system"))
            action = item.targetKind == QStringLiteral("admin") ? tr("管理团队发来消息") : tr("系统公告");
        else if (item.targetKind == QStringLiteral("post"))
            action = tr("回复了你的楼层");
        else
            action = tr("回复了你的主题");

        QString subText;
        if (actors.isEmpty())
            subText = QStringLiteral("<span style='color:%1'>%2</span>")
                          .arg(pal.ink2, action.toHtmlEscaped());
        else
            subText = QStringLiteral("<span style='color:%1;font-weight:700'>%2</span>"
                                     "<span style='color:%3'> %4</span>")
                          .arg(pal.ink, actors.toHtmlEscaped(), pal.ink2, action.toHtmlEscaped());

        auto *sub = new QLabel(row);
        sub->setObjectName(QStringLiteral("fbNotifSub"));
        sub->setTextFormat(Qt::RichText);
        sub->setText(subText);
        contentLayout->addWidget(sub);

        // 引用块：通知摘要原文
        if (!item.excerpt.isEmpty())
        {
            auto *quote = new QFrame(row);
            quote->setObjectName(QStringLiteral("fbNotifQuote"));
            auto *quoteLayout = new QHBoxLayout(quote);
            quoteLayout->setContentsMargins(9, 6, 9, 6);
            quoteLayout->setSpacing(0);
            auto *quoteText = new ElidedLabel(quote);
            quoteText->setObjectName(QStringLiteral("fbNotifQuoteText"));
            quoteText->setFullText(item.excerpt.simplified());
            quoteLayout->addWidget(quoteText, 1);
            contentLayout->addWidget(quote);
        }

        QStringList metaParts;
        if (item.count > 1)
            metaParts << tr("共 %1 条").arg(item.count);
        metaParts << relativeTime(item.createdAt);
        auto *meta = new QLabel(metaParts.join(QStringLiteral(" · ")), row);
        meta->setObjectName(QStringLiteral("fbRowMeta"));
        contentLayout->addWidget(meta);

        rowLayout->addLayout(contentLayout, 1);

        const QVector<int> ids = item.ids;
        const int threadId = item.threadId;
        const int requestId = item.requestId;

        // 好友申请：通知行里直接同意 / 拒绝，处理完重拉这一页（顶栏角标也跟着掉）
        if (requestId > 0)
        {
            auto *actions = new QVBoxLayout();
            actions->setContentsMargins(0, 0, 0, 0);
            actions->setSpacing(6);

            auto *acceptButton = new QPushButton(tr("同意"), row);
            acceptButton->setObjectName(QStringLiteral("forumPrimaryButton"));
            acceptButton->setCursor(Qt::PointingHandCursor);
            acceptButton->setFixedHeight(26);
            auto *declineButton = new QPushButton(tr("拒绝"), row);
            declineButton->setObjectName(QStringLiteral("forumGhostButton"));
            declineButton->setCursor(Qt::PointingHandCursor);
            declineButton->setFixedHeight(26);
            actions->addWidget(acceptButton);
            actions->addWidget(declineButton);
            rowLayout->addLayout(actions);

            connect(acceptButton, &QPushButton::clicked, this, [this, requestId]() {
                ForumChatService::instance()->acceptFriendRequest(
                    requestId,
                    [this]() {
                        NotificationManager::showSuccess(this, tr("已同意好友申请"));
                        m_service->refreshProfile();
                        m_notificationsLoaded = false;
                        loadNotifications();
                    },
                    [this](const QString &message, int) {
                        NotificationManager::showError(this, message);
                    });
            });
            connect(declineButton, &QPushButton::clicked, this, [this, requestId]() {
                ForumChatService::instance()->declineFriendRequest(
                    requestId,
                    [this]() {
                        m_service->refreshProfile();
                        m_notificationsLoaded = false;
                        loadNotifications();
                    },
                    [this](const QString &message, int) {
                        NotificationManager::showError(this, message);
                    });
            });
        }
        else
        {
            auto *go = new QLabel(QStringLiteral("›"), row);
            go->setObjectName(QStringLiteral("fbNotifGo"));
            rowLayout->addWidget(go, 0, Qt::AlignVCenter);
        }

        row->onClick = [this, ids, threadId]() {
            // 标已读（组内全部）然后直达帖子；回来后列表按服务端结果重新渲染
            ForumContentService::instance()->markNotificationsRead(
                ids, [this](int) {
                    m_service->refreshProfile();
                    m_notificationsLoaded = false;
                    if (m_activeTab == NotificationsTab)
                        loadNotifications();
                });
            if (threadId > 0)
                emit threadOpenRequested(threadId);
        };

        m_notificationListLayout->addWidget(row);
    }

    setTabHint(unreadRows > 0 ? tr("共 %1 条通知").arg(m_notifications.size())
                              : tr("没有未读通知"));
    m_notificationListLayout->addStretch();
}

void ForumAccountPage::markAllNotificationsRead()
{
    if (m_markAllReadButton)
        m_markAllReadButton->setEnabled(false);
    ForumContentService::instance()->markNotificationsRead(
        {},
        [this](int) {
            if (m_markAllReadButton)
                m_markAllReadButton->setEnabled(true);
            m_notificationsLoaded = false;
            m_service->refreshProfile();
            renderNotifications();
            loadNotifications();
        },
        [this](const QString &message, int) {
            Q_UNUSED(message);
            if (m_markAllReadButton)
                m_markAllReadButton->setEnabled(true);
        });
}

void ForumAccountPage::renderRows(QVBoxLayout *layout, const QJsonArray &items, int kind)
{
    QWidget *host = layout->parentWidget();

    for (const QJsonValue &value : items)
    {
        const QJsonObject item = value.toObject();

        auto *row = new ClickableFrame(host);
        row->setObjectName(QStringLiteral("fbListRow"));
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 8);
        rowLayout->setSpacing(4);

        QString title;
        QString meta;
        int threadId = 0;

        if (kind == 1)
        {
            // 我的回复：帖子标题 + 板块 + 摘要
            threadId = item.value(QStringLiteral("thread_id")).toInt();
            title = item.value(QStringLiteral("thread_title")).toString();
            const QString board =
                item.value(QStringLiteral("board")).toObject().value(QStringLiteral("name")).toString();
            const QString snippet = item.value(QStringLiteral("snippet")).toString();
            meta = board;
            if (!snippet.isEmpty())
                meta += QStringLiteral(" · ") + snippet;
            const qint64 created =
                static_cast<qint64>(item.value(QStringLiteral("created_at")).toDouble());
            meta += QStringLiteral(" · ") + relativeTime(created);
        }
        else
        {
            // 我的主题 / 我的收藏：标题 + 板块 + 回复数（教程帖看评论数）+ 最近回复
            threadId = item.value(QStringLiteral("id")).toInt();
            title = item.value(QStringLiteral("title")).toString();
            const QString board =
                item.value(QStringLiteral("board")).toObject().value(QStringLiteral("name")).toString();
            const int replies = item.value(QStringLiteral("reply_count")).toInt();
            const bool tutorial = item.value(QStringLiteral("tutorial")).toBool();
            const int comments = item.value(QStringLiteral("comment_count")).toInt();
            const qint64 lastPost =
                static_cast<qint64>(item.value(QStringLiteral("last_post_at")).toDouble());
            meta = board;
            if (!meta.isEmpty())
                meta += QStringLiteral(" · ");
            meta += tutorial ? tr("%1 条评论").arg(comments) : tr("%1 条回复").arg(replies);
            if (lastPost > 0)
                meta += QStringLiteral(" · ") + relativeTime(lastPost);
        }

        auto *titleLabel = new ElidedLabel(row);
        titleLabel->setObjectName(QStringLiteral("fbRowTitle"));
        titleLabel->setFullText(title.isEmpty() ? tr("（无标题）") : title);
        titleLabel->setToolTip(title);
        rowLayout->addWidget(titleLabel);

        auto *metaLabel = new QLabel(meta, row);
        metaLabel->setObjectName(QStringLiteral("fbRowMeta"));
        metaLabel->setWordWrap(true);
        rowLayout->addWidget(metaLabel);

        // 深度集成：点击行时在启动器内直接打开帖子详情，不再跳浏览器
        row->onClick = [this, threadId]() {
            if (threadId > 0)
                emit threadOpenRequested(threadId);
        };

        layout->addWidget(row);
    }

    auto *moreHint = makeMutedLabel(tr("只显示最近 20 条，更多内容可在论坛网页版查看"), host, "fbNote");
    moreHint->setAlignment(Qt::AlignCenter);
    layout->addSpacing(6);
    layout->addWidget(moreHint);
    layout->addStretch();
}

void ForumAccountPage::loadTabData()
{
    if (!m_service->isLoggedIn())
        return;

    const int userId = m_service->user().id;

    if (m_activeTab == ThreadsTab && !m_threadsLoaded)
    {
        renderLoading(m_threadListLayout);
        m_service->getJson(
            QStringLiteral("/api/users/%1/threads?per_page=20").arg(userId),
            [this](const QJsonObject &root) {
                m_threads = root.value(QStringLiteral("threads")).toArray();
                m_threadTotal =
                    root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("total")).toInt();
                m_threadsLoaded = true;
                if (m_activeTab == ThreadsTab)
                    renderThreads();
            },
            [this](const QString &message, int status) {
                Q_UNUSED(status);
                clearLayout(m_threadListLayout);
                auto *error = makeMutedLabel(message, m_threadListLayout->parentWidget(), "fbEmpty");
                error->setAlignment(Qt::AlignCenter);
                m_threadListLayout->addSpacing(24);
                m_threadListLayout->addWidget(error);
                m_threadListLayout->addStretch();
            });
    }
    else if (m_activeTab == RepliesTab && !m_repliesLoaded)
    {
        renderLoading(m_replyListLayout);
        m_service->getJson(
            QStringLiteral("/api/users/%1/posts?per_page=20").arg(userId),
            [this](const QJsonObject &root) {
                m_replies = root.value(QStringLiteral("posts")).toArray();
                m_replyTotal =
                    root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("total")).toInt();
                m_repliesLoaded = true;
                if (m_activeTab == RepliesTab)
                    renderReplies();
            },
            [this](const QString &message, int status) {
                Q_UNUSED(status);
                clearLayout(m_replyListLayout);
                auto *error = makeMutedLabel(message, m_replyListLayout->parentWidget(), "fbEmpty");
                error->setAlignment(Qt::AlignCenter);
                m_replyListLayout->addSpacing(24);
                m_replyListLayout->addWidget(error);
                m_replyListLayout->addStretch();
            });
    }
    else if (m_activeTab == BookmarksTab)
    {
        if (m_bookmarkSubTab == BookmarkResourceTab)
        {
            if (!m_contentFavoritesLoaded)
                loadContentFavorites();
        }
        else if (m_bookmarkSubTab == BookmarkLocalTab)
        {
            // 本地收藏不走网络：直接从 LocalFavoritesManager 渲染
            renderLocalFavorites();
        }
        else if (!m_bookmarksLoaded)
        {
            renderLoading(m_bookmarkListLayout);
            m_service->getJson(
                QStringLiteral("/api/me/bookmarks?per_page=20"),
                [this](const QJsonObject &root) {
                    m_bookmarks = root.value(QStringLiteral("threads")).toArray();
                    m_bookmarkTotal =
                        root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("total")).toInt();
                    m_bookmarksLoaded = true;
                    if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkThreadTab)
                        renderBookmarks();
                    refreshBookmarkStat();
                },
                [this](const QString &message, int status) {
                    Q_UNUSED(status);
                    clearLayout(m_bookmarkListLayout);
                    auto *error = makeMutedLabel(message, m_bookmarkListLayout->parentWidget(), "fbEmpty");
                    error->setAlignment(Qt::AlignCenter);
                    m_bookmarkListLayout->addSpacing(24);
                    m_bookmarkListLayout->addWidget(error);
                    m_bookmarkListLayout->addStretch();
                });
        }
    }
    else if (m_activeTab == NotificationsTab && !m_notificationsLoaded)
    {
        renderLoading(m_notificationListLayout);
        loadNotifications();
    }
    else if (m_activeTab == FeedbackTab)
    {
        loadMyFeedbacks(false);
    }
    else if (m_activeTab == PointsTab)
    {
        loadPoints(false);
    }
    else if (m_activeTab == MedalsTab)
    {
        // 每次切到勋章页都重新结算一次：刚发帖、刚签到或刚攒够方块时，
        // 不用手动点刷新也能看到新点亮的勋章。
        loadMedals(true);
    }
}

void ForumAccountPage::fillSettingsForm()
{
    const ForumUser user = m_service->user();
    if (!user.isValid())
        return;

    if (m_displayNameEdit)
        m_displayNameEdit->setText(user.displayName);
    if (m_emailEdit)
        m_emailEdit->setText(user.email);
    if (m_bioEdit)
        m_bioEdit->setPlainText(user.bio);
    // 正在看预览时重新填表，预览同步成最新内容
    if (m_bioPreview && !m_bioPreview->isHidden())
        m_bioPreview->setPostMarkdown(user.bio.trimmed());
    updateEmailVerifyUi();
}

void ForumAccountPage::setTabHint(const QString &text)
{
    if (m_tabHint)
        m_tabHint->setText(text);
}

void ForumAccountPage::clearLayout(QVBoxLayout *layout)
{
    if (!layout)
        return;

    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

// ════════════════════════════════════════════════════════════════ 账号操作

void ForumAccountPage::saveProfileForm()
{
    if (!m_service->isLoggedIn())
        return;

    const QString displayName = m_displayNameEdit->text().trimmed();
    const QString email = m_emailEdit->text().trimmed();
    const QString bio = m_bioEdit->toPlainText().trimmed();

    if (bio.size() > 300)
    {
        NotificationManager::showError(this, tr("个人简介最多 300 个字"));
        return;
    }

    m_service->updateProfile(displayName, bio, email);
}

void ForumAccountPage::savePasswordForm()
{
    if (!m_service->isLoggedIn())
        return;

    const bool hasPassword = m_service->user().passwordSet;
    const QString current = m_currentPasswordEdit->text();
    const QString next = m_newPasswordEdit->text();
    const QString confirm = m_confirmPasswordEdit->text();

    if (hasPassword && current.isEmpty())
    {
        NotificationManager::showError(this, tr("请先输入当前密码"));
        return;
    }
    if (next.length() < 6)
    {
        NotificationManager::showError(this, tr("新密码至少 6 位"));
        return;
    }
    if (next != confirm)
    {
        NotificationManager::showError(this, tr("两次输入的新密码不一样"));
        return;
    }

    m_service->changePassword(current, next);
    m_currentPasswordEdit->clear();
    m_newPasswordEdit->clear();
    m_confirmPasswordEdit->clear();
}

// ════════════════════════════════════════════════════════════════ 邮箱验证

void ForumAccountPage::sendEmailCode()
{
    if (!m_service->isLoggedIn() || m_emailBusy || m_codeCooldown > 0)
        return;

    const QString email = m_emailEdit->text().trimmed();
    static const QRegularExpression mailPattern(
        QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$"));
    if (!mailPattern.match(email).hasMatch())
    {
        NotificationManager::showError(this, tr("请先在上面填写正确的邮箱地址"));
        return;
    }

    m_emailBusy = true;
    updateEmailVerifyUi();
    m_service->sendEmailCode(email);
}

void ForumAccountPage::verifyEmailCode()
{
    if (!m_service->isLoggedIn() || m_emailBusy)
        return;

    const QString email = m_emailEdit->text().trimmed();
    const QString code = m_emailCodeEdit->text().trimmed();
    if (email.isEmpty())
    {
        NotificationManager::showError(this, tr("请先在上面填写邮箱地址"));
        return;
    }
    if (code.length() != 6)
    {
        NotificationManager::showError(this, tr("请输入 6 位数字验证码"));
        return;
    }
    for (const QChar ch : code)
    {
        if (!ch.isDigit())
        {
            NotificationManager::showError(this, tr("验证码是 6 位数字，请检查一下"));
            return;
        }
    }

    m_emailBusy = true;
    updateEmailVerifyUi();
    m_service->verifyEmailCode(email, code);
}

void ForumAccountPage::tickCodeCooldown()
{
    if (m_codeCooldown > 0)
        --m_codeCooldown;
    if (m_codeCooldown <= 0 && m_codeTimer)
        m_codeTimer->stop();
    updateEmailVerifyUi();
}

void ForumAccountPage::updateEmailVerifyUi()
{
    if (!m_emailVerifiedChip || !m_sendCodeButton || !m_verifyCodeButton || !m_emailCodeEdit)
        return;

    // 状态徽标：绑定了邮箱才显示，已验证用品牌绿、未验证用灰
    const ForumUser user = m_service->user();
    if (!m_service->isLoggedIn() || user.email.isEmpty())
    {
        m_emailVerifiedChip->hide();
    }
    else
    {
        const QString name = user.emailVerified ? QStringLiteral("fbChip")
                                                : QStringLiteral("fbChipMuted");
        if (m_emailVerifiedChip->objectName() != name)
        {
            m_emailVerifiedChip->setObjectName(name);
            if (QStyle *chipStyle = m_emailVerifiedChip->style())
            {
                chipStyle->unpolish(m_emailVerifiedChip);
                chipStyle->polish(m_emailVerifiedChip);
            }
        }
        m_emailVerifiedChip->setText(user.emailVerified ? tr("已验证") : tr("未验证"));
        m_emailVerifiedChip->show();
    }

    m_emailCodeEdit->setEnabled(!m_emailBusy);
    m_verifyCodeButton->setEnabled(!m_emailBusy);
    m_verifyCodeButton->setText(m_emailBusy ? tr("验证中…") : tr("验证邮箱"));
    m_sendCodeButton->setEnabled(!m_emailBusy && m_codeCooldown <= 0);
    if (m_emailBusy)
        m_sendCodeButton->setText(tr("发送中…"));
    else if (m_codeCooldown > 0)
        m_sendCodeButton->setText(tr("重新发送(%1s)").arg(m_codeCooldown));
    else
        m_sendCodeButton->setText(tr("发送验证码"));
}

void ForumAccountPage::pickAvatarFile()
{
    // 系统文件选择器
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择头像"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    m_service->uploadAvatar(path);
}

void ForumAccountPage::pickCoverFile()
{
    // 系统文件选择器
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择封面图"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    m_service->uploadCover(path);
}

void ForumAccountPage::confirmLogout()
{
    if (!m_service->isLoggedIn())
        return;

    m_service->logout();
    m_threadsLoaded = m_repliesLoaded = m_bookmarksLoaded = false;
    m_threads = QJsonArray();
    m_replies = QJsonArray();
    m_bookmarks = QJsonArray();
    m_threadTotal = m_replyTotal = m_bookmarkTotal = 0;
    m_contentFavorites.clear();
    m_contentFavoritesLoaded = false;
    m_contentFavoriteTotal = 0;
    m_loadedAvatarPath.clear();

    setActiveTab(ThreadsTab);
    renderProfile();
    renderContentArea();
}

// ════════════════════════════════════════════════════════════════ 首次进入的引导

void ForumAccountPage::scheduleAccountTour()
{
    if (m_tourOverlay || m_tourScheduled)
        return;
    SettingsManager *settings = SettingsManager::instance();
    if (!settings
        || settings->getProperty(QStringLiteral("account_tour/shown"), false).toBool())
    {
        return;
    }

    m_tourScheduled = true;
    // 页面入场动画约 350ms，等它和卡片布局都落定再抠洞，否则高亮框会和卡片错位
    QTimer::singleShot(650, this, [this]() {
        m_tourScheduled = false;
        startAccountTourIfNeeded();
    });
}

void ForumAccountPage::startAccountTourIfNeeded()
{
    if (m_tourOverlay || !isVisible())
        return;
    SettingsManager *settings = SettingsManager::instance();
    if (!settings
        || settings->getProperty(QStringLiteral("account_tour/shown"), false).toBool())
    {
        return;
    }
    // 登录卡 / 账户详情里没有这两块区域，只停在总览时才讲
    if (!m_viewStack || m_viewStack->currentIndex() != OverviewView)
        return;
    if (!m_profileCard || !m_tabsCard)
        return;

    auto *tour = new AccountTourOverlay(this);
    const Palette p = currentPalette();
    tour->setThemeColors(QColor(p.brand), QColor(p.card), QColor(p.line));
    tour->setSteps({ { tr("游戏账户（角色）"),
                       tr("「角色」页签里放的是游戏账户——离线、微软正版或第三方账号。"
                          "启动游戏前，必须先在这里选好一个账户。"),
                       m_tabsCard },
                     { tr("方块盒子账户"),
                       tr("这块是方块盒子账户：发帖、回复、加好友、进联机大厅都用它。"
                          "第一次用先点「注册」，已经有账号就直接「登录」。"),
                       m_profileCard } });
    tour->ensureVisible = [this](QWidget *target) {
        // 只做竖向滚动：QScrollArea::ensureWidgetVisible 会把窄窗口里的宽卡片横向挪一下，
        // 切步骤时整页跟着平移，这里自己算滚动量更稳
        if (!m_scroll || !m_scroll->viewport() || !target)
            return;
        QWidget *content = m_scroll->widget();
        if (!content || !content->isAncestorOf(target))
            return;
        QScrollBar *bar = m_scroll->verticalScrollBar();
        const int margin = 24;
        const int top = target->mapTo(content, QPoint(0, 0)).y();
        const int viewport = m_scroll->viewport()->height();
        if (top < bar->value() + margin)
            bar->setValue(qMax(0, top - margin));
        else if (top > bar->value() + viewport - 80)
            bar->setValue(qBound(0, top - margin, bar->maximum()));
    };
    tour->onFinished = [this]() { finishAccountTour(); };

    m_tourOverlay = tour;
    tour->start();
}

void ForumAccountPage::finishAccountTour()
{
    // 记下「已经看过」，以后进账户页不再打扰
    if (SettingsManager::instance())
        SettingsManager::instance()->setProperty(QStringLiteral("account_tour/shown"), true);

    if (m_tourOverlay)
    {
        QWidget *overlay = m_tourOverlay;
        m_tourOverlay = nullptr;
        overlay->deleteLater();
    }
}

void ForumAccountPage::cancelAccountTour()
{
    // 中途切走：不记「已看过」，下次进来从头再讲
    m_tourScheduled = false;
    if (m_tourOverlay)
    {
        QWidget *overlay = m_tourOverlay;
        m_tourOverlay = nullptr;
        overlay->deleteLater();
    }
}
