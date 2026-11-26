/**
 * @file   ForumKit.cpp
 * @brief  论坛页面共用小部件实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumKit.h"

#include <QDateTime>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QImage>
#include <QLayoutItem>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>

#include "utils/CoverImageCache.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/IconHelper.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace {

// 头像的圆角 / 字号跟尺寸走，背景与文字色在 style.qss 的 QLabel#forumAvatar 里
// （全局 QSS 随主题即时刷新，内联样式只放必须按尺寸变的属性）
QString styledAvatar(int size)
{
    return StyleKit::resolve(QStringLiteral(
        "QLabel#forumAvatar{border-radius:%1px;font-size:%2px;}")
        .arg(size / 2)
        .arg(qMax(11, size * 2 / 5)));
}

} // namespace

namespace ForumKit {

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
    if (diff < 86400 * 7)
        return QObject::tr("%1 天前").arg(diff / 86400);
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd"));
}

QString humanFileSize(qint64 bytes)
{
    if (bytes <= 0)
        return QObject::tr("0 B");
    if (bytes < 1024)
        return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(qMax(1.0, bytes / 1024.0), 0, 'f', 0);
    return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
}

void clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        else if (QLayout *child = item->layout())
            clearLayout(child);
        delete item;
    }
}

QFrame *makeCard(QWidget *parent, const QString &objectName)
{
    auto *card = new QFrame(parent);
    card->setObjectName(objectName.isEmpty() ? QStringLiteral("forumCard") : objectName);
    return card;
}

QLabel *makeMuted(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("forumMuted"));
    label->setWordWrap(true);
    return label;
}

QLabel *makeSectionTitle(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("forumSectionTitle"));
    return label;
}

QLabel *makeAvatar(QWidget *parent, const QString &avatarPath, const QString &name, int size)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("forumAvatar"));
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);

    const QString letter = name.isEmpty() ? QStringLiteral("?")
                                          : QString::fromUtf8(name.left(1).toUtf8());
    label->setText(letter);
    label->setStyleSheet(styledAvatar(size));

    const QString path = avatarPath.trimmed();
    if (!path.isEmpty())
    {
        const QString absolute = ForumAccountService::instance()->absoluteUrl(path);
        QPointer<QLabel> guard(label);
        ForumImageStore::instance()->requestImage(absolute, [guard, size](const QImage &image) {
            if (guard.isNull() || image.isNull())
                return;
            guard->setText(QString());
            guard->setStyleSheet(QString());
            // 按 devicePixelRatio 放大绘制，保证高分屏下不糊
            QPixmap pixmap = QPixmap::fromImage(image);
            const qreal dpr = guard->devicePixelRatioF();
            guard->setPixmap(pixmap.scaled(int(size * dpr), int(size * dpr),
                                           Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            guard->setFixedSize(size, size);
        });
    }
    return label;
}

QLabel *makeGroupAvatar(QWidget *parent, const QString &icon, const QString &name, int size)
{
    const QString value = icon.trimmed();
    // 站内图片地址交给 makeAvatar 异步加载；空值也走它显示群名首字
    if (value.isEmpty() || value.startsWith(QLatin1Char('/')) || value.startsWith(QStringLiteral("http")))
        return makeAvatar(parent, value, name, size);

    // 内置表情图标：圆角底 + 大一号的字号，别用头像那边偏小的首字字号
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("forumAvatar"));
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setTextFormat(Qt::PlainText);
    label->setText(value);
    label->setStyleSheet(StyleKit::resolve(
        QStringLiteral("QLabel#forumAvatar{border-radius:%1px;font-size:%2px;}")
            .arg(size / 2)
            .arg(qMax(14, size * 3 / 5))));
    return label;
}

QLabel *makeOfficialAvatar(QWidget *parent, int size)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("forumAvatar"));
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(styledAvatar(size));
    // 官方身份统一用方块盒子立方体图标：完整缩进圆角底上（四周留边），不按铺满裁切
    QPixmap cube(QStringLiteral(":/Images/blockbox_cube.png"));
    if (!cube.isNull())
    {
        const qreal dpr = label->devicePixelRatioF();
        const int target = qMax(1, int(size * 0.78));
        QPixmap pixmap = cube.scaled(int(target * dpr), int(target * dpr),
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(dpr);
        label->setPixmap(pixmap);
    }
    return label;
}

QLabel *makeElided(QWidget *parent, const QString &text, int maxWidth)
{
    auto *label = new ElidedLabel(parent);
    label->setFullText(text);
    if (maxWidth > 0)
        label->setMaximumWidth(maxWidth);
    return label;
}

QPushButton *makeTagChip(QWidget *parent, const QString &tag)
{
    auto *button = new QPushButton(QStringLiteral("# ") + tag, parent);
    button->setObjectName(QStringLiteral("forumTagChip"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    button->setFixedHeight(26);
    return button;
}

QLabel *makeBadge(QWidget *parent, const QString &text, bool warn)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(warn ? QStringLiteral("forumBadgeWarn") : QStringLiteral("forumBadge"));
    // 徽章是胶囊，宽度只跟文字走，不能在竖排布局里被拉伸成通栏
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return label;
}

// ────────────────────────────────────────────────────────────── CoverBanner

CoverBanner::CoverBanner(QWidget *parent)
    : QWidget(parent)
{
    // 高度由 width / 3 推出来，交给布局按 heightForWidth 排
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void CoverBanner::setAspectRatio(qreal ratio)
{
    const qreal next = ratio > 0.01 ? ratio : 3.0;
    if (qFuzzyCompare(m_aspectRatio, next))
        return;
    m_aspectRatio = next;
    updateGeometry();
    update();
}

bool CoverBanner::hasHeightForWidth() const
{
    return true;
}

int CoverBanner::heightForWidth(int width) const
{
    return qMax(1, qRound(qMax(0, width) / m_aspectRatio));
}

void CoverBanner::setRadius(int radius)
{
    m_radius = qMax(0, radius);
    update();
}

void CoverBanner::setCover(const QString &coverPath)
{
    const QString path = coverPath.trimmed();
    if (path == m_path && (path.isEmpty() || !m_image.isNull()))
        return;

    m_path = path;
    m_image = QImage();
    update();
    if (path.isEmpty())
        return;

    const QString absolute = ForumAccountService::instance()->absoluteUrl(path);

    // 本机缓存里已经有这张封面就直接画出来：切页面、重开软件、断网时都不用等网络。
    // 站内封面地址每次上传都是新文件、内容不会再变，所以有缓存就不必再下一次。
    m_image = CoverImageCache::load(absolute);
    if (!m_image.isNull())
    {
        update();
        return;
    }

    QPointer<CoverBanner> guard(this);
    ForumImageStore::instance()->requestImage(absolute, [guard, absolute](const QImage &image) {
        if (guard.isNull() || image.isNull())
            return;
        CoverImageCache::save(absolute, image);
        guard->m_image = image;
        guard->update();
    });
}

void CoverBanner::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect area = rect();
    if (area.width() <= 0 || area.height() <= 0)
        return;

    // 顶部两角裁成卡片圆角，底部与资料区贴平
    const qreal radius = qMin<qreal>(m_radius, qMin(area.width(), area.height()) / 2.0);
    QPainterPath clip;
    clip.moveTo(area.left(), area.bottom() + 1);
    clip.lineTo(area.left(), area.top() + radius);
    clip.quadTo(area.left(), area.top(), area.left() + radius, area.top());
    clip.lineTo(area.right() - radius, area.top());
    clip.quadTo(area.right(), area.top(), area.right(), area.top() + radius);
    clip.lineTo(area.right(), area.bottom() + 1);
    clip.closeSubpath();
    painter.setClipPath(clip);

    if (m_image.isNull())
    {
        // 没有封面：主题色斜向渐变 + 一点高光，避免大面积纯色发闷
        auto token = [](const char *name) {
            return QColor(ThemeManager::instance()->resolveTokens(QString::fromLatin1(name)));
        };
        QColor from = token("@PRIMARY@");
        QColor to = token("@PRIMARY_DARK@");
        if (!from.isValid())
            from = QColor(QStringLiteral("#2fbf71"));
        if (!to.isValid())
            to = from.darker(150);

        QLinearGradient gradient(area.topLeft(), area.bottomRight());
        gradient.setColorAt(0.0, from.lighter(112));
        gradient.setColorAt(0.55, from);
        gradient.setColorAt(1.0, to);
        painter.fillRect(area, gradient);

        QRadialGradient glow(QPointF(area.left() + area.width() * 0.2,
                                     area.top() + area.height() * 0.35),
                             area.width() * 0.62);
        glow.setColorAt(0.0, QColor(255, 255, 255, 46));
        glow.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillRect(area, glow);
    }
    else
    {
        // 按比例铺满，再居中裁掉多出来的部分
        const QImage scaled = m_image.scaled(area.size(), Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation);
        const int offsetX = qMax(0, (scaled.width() - area.width()) / 2);
        const int offsetY = qMax(0, (scaled.height() - area.height()) / 2);
        painter.drawImage(area, scaled, QRect(offsetX, offsetY, area.width(), area.height()));
    }

    // 底部一条极淡的分隔线，图片和资料区分得开
    painter.setClipping(false);
    painter.setPen(QPen(QColor(0, 0, 0, 20), 1));
    painter.drawLine(QPointF(area.left(), area.bottom() - 0.5),
                     QPointF(area.right(), area.bottom() - 0.5));
}

// ────────────────────────────────────────────────────────────── ClickableCard

ClickableCard::ClickableCard(QWidget *parent)
    : QFrame(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ClickableCard::mousePressEvent(QMouseEvent *event)
{
    // 记录按下位置：只有真的按在卡片上的松开才算点击。
    // 列表重新排版/滚动时，Qt 会把松开事件补发到光标下新出现的卡片上，
    // 这种没有按下事件配合的松开不能当成点击。
    if (event->button() == Qt::LeftButton)
        setProperty("pressPos", event->pos());
    else
        setProperty("pressPos", QVariant());
    QFrame::mousePressEvent(event);
}

void ClickableCard::mouseReleaseEvent(QMouseEvent *event)
{
    const QVariant pressed = property("pressPos");
    setProperty("pressPos", QVariant());

    if (event->button() == Qt::LeftButton && onClick && pressed.isValid()
        && (event->pos() - pressed.toPoint()).manhattanLength() < 8
        && rect().contains(event->pos()))
        onClick();
    QFrame::mouseReleaseEvent(event);
}

// ────────────────────────────────────────────────────────────── ElidedLabel

ElidedLabel::ElidedLabel(QWidget *parent)
    : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMinimumHeight(0);
}

void ElidedLabel::setFullText(const QString &text)
{
    m_text = text;
    setToolTip(text);
    applyElide();
}

void ElidedLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    applyElide();
}

void ElidedLabel::applyElide()
{
    const QFontMetrics metrics(fontMetrics());
    const int available = qMax(10, width() - 2);
    const QString elided = metrics.elidedText(m_text, Qt::ElideRight, available);
    setText(elided.isEmpty() ? m_text : elided);
}

// ────────────────────────────────────────────────────────────── ForumPager

ForumPager::ForumPager(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addStretch(1);

    m_prevButton = new QPushButton(tr("上一页"), this);
    m_prevButton->setObjectName(QStringLiteral("forumPageButton"));
    m_prevButton->setCursor(Qt::PointingHandCursor);
    m_prevButton->setFixedHeight(28);
    layout->addWidget(m_prevButton);

    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("forumMuted"));
    layout->addWidget(m_label);

    m_nextButton = new QPushButton(tr("下一页"), this);
    m_nextButton->setObjectName(QStringLiteral("forumPageButton"));
    m_nextButton->setCursor(Qt::PointingHandCursor);
    m_nextButton->setFixedHeight(28);
    layout->addWidget(m_nextButton);

    layout->addStretch(1);

    connect(m_prevButton, &QPushButton::clicked, this, [this]() { emitPage(m_current - 1); });
    connect(m_nextButton, &QPushButton::clicked, this, [this]() { emitPage(m_current + 1); });
    setPage(1, 1);
}

void ForumPager::setPage(int current, int total)
{
    m_current = qMax(1, current);
    m_total = qMax(1, total);

    m_label->setText(tr("第 %1 / %2 页").arg(m_current).arg(m_total));
    m_prevButton->setEnabled(m_current > 1);
    m_nextButton->setEnabled(m_current < m_total);
    setVisible(m_total > 1);
}

void ForumPager::emitPage(int page)
{
    const int clamped = qBound(1, page, m_total);
    if (clamped == m_current)
        return;
    emit pageRequested(clamped);
}

// ────────────────────────────────────────────────────────────── 预设模板

QVector<MarkdownPreset> postPresets()
{
    return {
        {QObject::tr("求助"),
         QObject::tr("## 现象\n"
                     "简单说清发生了什么，什么时候开始的\n"
                     "\n"
                     "## 复现步骤\n"
                     "1. \n"
                     "2. \n"
                     "\n"
                     "## 环境\n"
                     "- 启动器版本：\n"
                     "- 游戏版本 / 加载器：\n"
                     "- 报错截图或日志：\n")},
        {QObject::tr("教程"),
         QObject::tr("## 准备\n"
                     "动手前需要准备的东西\n"
                     "\n"
                     "## 步骤\n"
                     "1. \n"
                     "2. \n"
                     "\n"
                     "## 常见问题\n"
                     "- \n")},
        {QObject::tr("作品分享"),
         QObject::tr("## 简介\n"
                     "一句话介绍你的作品\n"
                     "\n"
                     "## 特色\n"
                     "- \n"
                     "\n"
                     "## 截图\n"
                     "![](图片链接)\n"
                     "\n"
                     "## 下载\n"
                     "- \n")},
        {QObject::tr("插件介绍"),
         QObject::tr("## 功能\n"
                     "这个插件能做什么\n"
                     "\n"
                     "## 用法\n"
                     "- 安装后在左侧导航的「插件」里就能打开\n"
                     "\n"
                     "## 配置\n"
                     "- \n"
                     "\n"
                     "## 更新记录\n"
                     "- \n")},
        {QObject::tr("意见建议"),
         QObject::tr("## 想反馈的问题\n"
                     "简单描述一下你遇到的情况\n"
                     "\n"
                     "## 期望的做法\n"
                     "- \n"
                     "\n"
                     "## 补充说明\n"
                     "- \n")},
    };
}

/** 教程帖专用模板：发教程模式（ForumComposerPage::Mode::Tutorial）的预设菜单用 */
QVector<MarkdownPreset> tutorialPresets()
{
    return {
        {QObject::tr("图文教程"),
         QObject::tr("## 简介\n"
                     "这篇教程教你做什么、做完能达到什么效果\n"
                     "\n"
                     "## 准备\n"
                     "- \n"
                     "\n"
                     "## 步骤\n"
                     "### 第一步：\n"
                     "\n"
                     "### 第二步：\n"
                     "\n"
                     "## 常见问题\n"
                     "- \n"
                     "\n"
                     "> 提示：正文里的标题会自动生成目录，点击可跳转；\n"
                     "> 用 ::: fold 标题 … ::: 可以收起长段落；B 站视频链接直接贴进正文就行。\n")},
        {QObject::tr("视频教程"),
         QObject::tr("## 简介\n"
                     "一句话说明这个视频讲了什么\n"
                     "\n"
                     "## 视频\n"
                     "https://www.bilibili.com/video/BVxxxxxxxx\n"
                     "\n"
                     "## 图文补充\n"
                     "### 关键步骤\n"
                     "\n"
                     "## 常见问题\n"
                     "- \n")},
        {QObject::tr("攻略合集"),
         QObject::tr("## 简介\n"
                     "这份攻略覆盖哪些内容\n"
                     "\n"
                     "## 目录速览\n"
                     "```mindmap\n"
                     "- 攻略主题\n"
                     "  - 章节 1\n"
                     "  - 章节 2\n"
                     "```\n"
                     "\n"
                     "## 章节 1\n"
                     "\n"
                     "## 章节 2\n")},
    };
}

QVector<MarkdownPreset> lobbyIntroPresets()
{    return {
        {QObject::tr("生存服"),
         QObject::tr("## 玩法\n"
                     "- 模式：生存\n"
                     "- 难度：\n"
                     "- 版本：\n"
                     "\n"
                     "## 规则\n"
                     "- 禁止作弊、刷物品\n"
                     "\n"
                     "## 加入方式\n"
                     "点「一键加入」即可，需要的整合包会自动下载安装。\n")},
        {QObject::tr("建筑服"),
         QObject::tr("## 玩法\n"
                     "- 模式：创造\n"
                     "- 版本：\n"
                     "\n"
                     "## 规则\n"
                     "- 各自认领地皮，别拆别人的建筑\n"
                     "\n"
                     "## 加入方式\n"
                     "点「一键加入」即可。\n")},
        {QObject::tr("小游戏"),
         QObject::tr("## 玩法\n"
                     "- 小游戏：\n"
                     "- 人数：\n"
                     "- 版本：\n"
                     "\n"
                     "## 规则\n"
                     "- 听房主口令开局\n"
                     "\n"
                     "## 加入方式\n"
                     "点「一键加入」，人齐就开始。\n")},
        {QObject::tr("联机须知"),
         QObject::tr("## 前置要求\n"
                     "- 需要安装的整合包已填在上面的「前置整合包」里，加入时会提示\n"
                     "\n"
                     "## 注意事项\n"
                     "- 进服先看公告和群号\n")},
    };
}

void applyMarkdownPreset(QPlainTextEdit *edit, const QString &text)
{
    if (!edit || text.isEmpty())
        return;

    const QString body = edit->toPlainText();
    const bool empty = body.trimmed().isEmpty();

    // 选区先删掉再插，光标停在原选区开头
    QTextCursor cursor = edit->textCursor();
    int insertPos = 0;
    QString prefix;
    if (empty)
    {
        edit->selectAll();
        cursor = edit->textCursor();
    }
    else
    {
        insertPos = cursor.selectionStart();
        // 接在正文后面时另起一段，别和上一段黏在一起
        if (insertPos > 0)
            prefix = QStringLiteral("\n\n");
    }

    cursor.beginEditBlock();
    cursor.insertText(prefix + text);
    cursor.endEditBlock();

    const int blockStart = insertPos + prefix.size();
    const QString plain = edit->toPlainText();

    // 选中标题下面那行提示文字，用户直接打字就能替换
    int lineStart = plain.indexOf(QLatin1Char('\n'), blockStart);
    if (lineStart >= 0)
    {
        ++lineStart;
        int lineEnd = plain.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0)
            lineEnd = plain.size();
        if (lineEnd > lineStart)
        {
            QTextCursor select = edit->textCursor();
            select.setPosition(lineStart);
            select.setPosition(lineEnd, QTextCursor::KeepAnchor);
            edit->setTextCursor(select);
            edit->ensureCursorVisible();
            edit->setFocus();
            return;
        }
    }

    QTextCursor tail = edit->textCursor();
    tail.setPosition(qMin(plain.size(), blockStart + text.size()));
    edit->setTextCursor(tail);
    edit->ensureCursorVisible();
    edit->setFocus();
}

// ────────────────────────────────────────────────────────────── Markdown 编辑助手

namespace {

/** 选区覆盖的完整行范围：首尾不满一行的也扩成整行（行为与网页版编辑器一致） */
void mdLineRange(const QString &plain, int start, int end, int *lineStart, int *lineEnd)
{
    *lineStart = 0;
    if (start > 0)
        *lineStart = plain.lastIndexOf(QLatin1Char('\n'), start - 1) + 1;
    *lineEnd = plain.indexOf(QLatin1Char('\n'), end);
    if (*lineEnd < 0)
        *lineEnd = plain.size();
}

} // namespace

void mdWrapSelection(QPlainTextEdit *edit, const QString &marker, const QString &placeholder)
{
    if (!edit || marker.isEmpty())
        return;

    QTextCursor cursor = edit->textCursor();
    const QString plain = edit->toPlainText();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QString inner = plain.mid(start, end - start);

    // 选区两侧正好是同一对包裹符：反向拆掉
    const QString before = start >= marker.size() ? plain.mid(start - marker.size(), marker.size())
                                                  : QString();
    const QString after = plain.mid(end, marker.size());
    if (end > start && before == marker && after == marker)
    {
        cursor.beginEditBlock();
        cursor.removeSelectedText(); // 光标停在 start
        cursor.setPosition(start - marker.size());
        cursor.setPosition(start + marker.size(), QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.insertText(inner);
        cursor.endEditBlock();

        // 选中还原出来的内容，接着打字就能替换
        QTextCursor select = edit->textCursor();
        select.setPosition(start - marker.size());
        select.setPosition(start - marker.size() + inner.size(), QTextCursor::KeepAnchor);
        edit->setTextCursor(select);
        edit->ensureCursorVisible();
        return;
    }

    if (inner.isEmpty())
        inner = placeholder;

    cursor.beginEditBlock();
    cursor.removeSelectedText(); // 光标停在 start
    cursor.insertText(marker + inner + marker);
    cursor.endEditBlock();

    // 选中包住的内容：没选字时直接打字就能替换占位词
    QTextCursor select = edit->textCursor();
    select.setPosition(start + marker.size());
    select.setPosition(start + marker.size() + inner.size(), QTextCursor::KeepAnchor);
    edit->setTextCursor(select);
    edit->ensureCursorVisible();
}

void mdToggleLines(QPlainTextEdit *edit, const QString &kind)
{
    if (!edit)
        return;

    // 前缀识别与网页版编辑器同款；加前缀时先把旧的前缀剥掉
    static const QRegularExpression headingRe(QStringLiteral("^#{1,6}\\s+"));
    static const QRegularExpression quoteRe(QStringLiteral("^>\\s?"));
    static const QRegularExpression ulRe(QStringLiteral("^\\s*[-*+]\\s+"));
    static const QRegularExpression olRe(QStringLiteral("^\\s*\\d+[.)]\\s+"));

    const QRegularExpression *strip = nullptr;
    std::function<QString(const QString &, int)> make;
    if (kind == QStringLiteral("heading"))
    {
        strip = &headingRe;
        make = [](const QString &line, int) { return QStringLiteral("## ") + line; };
    }
    else if (kind == QStringLiteral("quote"))
    {
        strip = &quoteRe;
        make = [](const QString &line, int) { return QStringLiteral("> ") + line; };
    }
    else if (kind == QStringLiteral("ul"))
    {
        strip = &ulRe;
        make = [](const QString &line, int) { return QStringLiteral("- ") + line; };
    }
    else if (kind == QStringLiteral("ol"))
    {
        strip = &olRe;
        make = [](const QString &line, int index) {
            return QStringLiteral("%1. %2").arg(index + 1).arg(line);
        };
    }
    if (!strip)
        return;

    const QTextCursor cursor = edit->textCursor();
    const QString plain = edit->toPlainText();
    int lineStart = 0;
    int lineEnd = 0;
    mdLineRange(plain, cursor.selectionStart(), cursor.selectionEnd(), &lineStart, &lineEnd);

    const QStringList lines = plain.mid(lineStart, lineEnd - lineStart).split(QLatin1Char('\n'));
    // 选中的行都带前缀时整段去掉，否则给所有非空行加上
    const bool hasContent = std::any_of(lines.cbegin(), lines.cend(), [](const QString &line) {
        return !line.trimmed().isEmpty();
    });
    const bool allMarked = hasContent && std::all_of(lines.cbegin(), lines.cend(),
                                                     [strip](const QString &line) {
                                                         return line.trimmed().isEmpty()
                                                                || line.contains(*strip);
                                                     });

    QStringList changed;
    changed.reserve(lines.size());
    for (int index = 0; index < lines.size(); ++index)
    {
        QString stripped = lines.at(index);
        stripped.remove(*strip);
        if (allMarked)
            changed << stripped;
        else if (lines.at(index).trimmed().isEmpty())
            changed << lines.at(index);
        else
            changed << make(stripped, index);
    }

    const QString text = changed.join(QLatin1Char('\n'));
    QTextCursor apply = edit->textCursor();
    apply.beginEditBlock();
    apply.setPosition(lineStart);
    apply.setPosition(lineEnd, QTextCursor::KeepAnchor);
    apply.insertText(text);
    apply.endEditBlock();

    // 选中改过的行，连点一下就能来回切换
    QTextCursor select = edit->textCursor();
    select.setPosition(lineStart);
    select.setPosition(lineStart + text.size(), QTextCursor::KeepAnchor);
    edit->setTextCursor(select);
    edit->ensureCursorVisible();
}

void mdWrapCodeBlock(QPlainTextEdit *edit)
{
    if (!edit)
        return;

    const QTextCursor cursor = edit->textCursor();
    const QString plain = edit->toPlainText();
    int lineStart = 0;
    int lineEnd = 0;
    mdLineRange(plain, cursor.selectionStart(), cursor.selectionEnd(), &lineStart, &lineEnd);
    const QString block = plain.mid(lineStart, lineEnd - lineStart);

    const QString text = QStringLiteral("```\n") + block
                         + (block.isEmpty() ? QString() : QStringLiteral("\n"))
                         + QStringLiteral("```");
    QTextCursor apply = edit->textCursor();
    apply.beginEditBlock();
    apply.setPosition(lineStart);
    apply.setPosition(lineEnd, QTextCursor::KeepAnchor);
    apply.insertText(text);
    apply.endEditBlock();

    // 选中搬进围栏里的原内容（没有内容时光标停在两行 ``` 之间）
    const int blockStart = lineStart + 4; // "```\n"
    QTextCursor select = edit->textCursor();
    select.setPosition(blockStart);
    select.setPosition(blockStart + block.size(), QTextCursor::KeepAnchor);
    edit->setTextCursor(select);
    edit->ensureCursorVisible();
}

void mdInsertLink(QPlainTextEdit *edit)
{
    if (!edit)
        return;

    QTextCursor cursor = edit->textCursor();
    const int start = cursor.selectionStart();
    const QString inner = cursor.selectedText().trimmed();

    static const QRegularExpression urlRe(QStringLiteral("^(https?://|/)[^\\s)]+$"),
                                          QRegularExpression::CaseInsensitiveOption);
    if (!inner.isEmpty() && urlRe.match(inner).hasMatch())
    {
        // 选中的是链接：包成 [标题](链接)，光标落在标题位让用户填
        cursor.beginEditBlock();
        cursor.insertText(QStringLiteral("[](") + inner + QLatin1Char(')'));
        cursor.endEditBlock();
        QTextCursor move = edit->textCursor();
        move.setPosition(start + 1);
        edit->setTextCursor(move);
    }
    else if (!inner.isEmpty())
    {
        // 选中的是文字：包成 [文字](https://)，选中链接位让用户填
        const QString text = QStringLiteral("[%1](https://)").arg(inner);
        cursor.beginEditBlock();
        cursor.insertText(text);
        cursor.endEditBlock();
        const int urlStart = start + inner.size() + 3;
        QTextCursor select = edit->textCursor();
        select.setPosition(urlStart);
        select.setPosition(urlStart + 8, QTextCursor::KeepAnchor); // 选中 "https://"
        edit->setTextCursor(select);
    }
    else
    {
        // 什么都没选：整段占位，选中「标题」两个字让用户直接打字
        cursor.beginEditBlock();
        cursor.insertText(QStringLiteral("[标题](https://)"));
        cursor.endEditBlock();
        QTextCursor select = edit->textCursor();
        select.setPosition(start + 1);
        select.setPosition(start + 3, QTextCursor::KeepAnchor);
        edit->setTextCursor(select);
    }
    edit->ensureCursorVisible();
}

void mdInsertText(QPlainTextEdit *edit, const QString &text)
{
    if (!edit || text.isEmpty())
        return;

    QTextCursor cursor = edit->textCursor();
    cursor.beginEditBlock();
    cursor.insertText(text);
    cursor.endEditBlock();
    edit->setTextCursor(cursor);
    edit->ensureCursorVisible();
    edit->setFocus();
}

// ────────────────────────────────────────────────────────────── 点赞按钮

namespace {

constexpr int kLikeHeight = 28;
constexpr int kLikePadX = 6;
constexpr int kLikeHeart = 16;
constexpr int kLikeGap = 5;

/** 从主题令牌（如 @DANGER@）取颜色，取不到就用兜底色 */
QColor likeTokenColor(const QString &token, const QColor &fallback)
{
    QString resolved = StyleKit::resolve(QStringLiteral("color:%1;").arg(token));
    const int colon = resolved.indexOf(QLatin1Char(':'));
    if (colon >= 0)
    {
        resolved = resolved.mid(colon + 1);
        resolved.remove(QLatin1Char(';'));
        resolved.remove(QLatin1Char(' '));
        const QColor color(resolved);
        if (color.isValid())
            return color;
    }
    return fallback;
}

} // namespace

LikeButton::LikeButton(QWidget *parent)
    : QPushButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    // 只接受 Tab 聚焦：点一下不画焦点框，键盘切到它时才有
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName(tr("点赞"));

    // 字号字体自己存一份，避免全局 QPushButton 样式把文字撑大、按钮跟着变形
    m_font = font();
    m_font.setPixelSize(12);
    m_font.setWeight(QFont::DemiBold);

    m_likeColor = likeTokenColor(QStringLiteral("@DANGER@"), QColor(0xF4, 0x43, 0x36));

    m_anim = new QVariantAnimation(this);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setDuration(400);
    m_anim->setEasingCurve(QEasingCurve::Linear);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_t = value.toReal();
        update();
    });
    connect(m_anim, &QVariantAnimation::finished, this, [this]() {
        m_t = 0.0;
        update();
    });

    applySize();
}

void LikeButton::setLikes(int likes)
{
    m_likes = qMax(0, likes);
    applySize();
    update();
}

void LikeButton::setLiked(bool liked, bool animate)
{
    const bool changed = (liked != m_liked);
    m_liked = liked;
    update();
    if (animate && changed)
        playBurst(liked);
}

QSize LikeButton::sizeHint() const
{
    const QFontMetrics metrics(m_font);
    const int numberWidth = metrics.horizontalAdvance(QString::number(m_likes));
    const int width = kLikePadX * 2 + kLikeHeart + kLikeGap + numberWidth;
    return QSize(qMax(44, width), kLikeHeight);
}

QSize LikeButton::minimumSizeHint() const
{
    return sizeHint();
}

void LikeButton::applySize()
{
    setFixedSize(sizeHint());
}

void LikeButton::playBurst(bool liked)
{
    if (!m_anim)
        return;
    m_burstLiked = liked;
    m_t = 0.0;
    m_anim->stop();
    m_anim->start();
    update();
}

void LikeButton::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QPushButton::enterEvent(event);
}

void LikeButton::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_pressed = false;
    update();
    QPushButton::leaveEvent(event);
}

void LikeButton::mousePressEvent(QMouseEvent *event)
{
    m_pressed = true;
    update();
    QPushButton::mousePressEvent(event);
}

void LikeButton::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressed = false;
    update();
    QPushButton::mouseReleaseEvent(event);
}

void LikeButton::focusInEvent(QFocusEvent *event)
{
    const Qt::FocusReason reason = event->reason();
    m_focusVisible = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
                     || reason == Qt::ShortcutFocusReason;
    update();
    QPushButton::focusInEvent(event);
}

void LikeButton::focusOutEvent(QFocusEvent *event)
{
    m_focusVisible = false;
    update();
    QPushButton::focusOutEvent(event);
}

void LikeButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!isEnabled())
        painter.setOpacity(0.45);

    // 文字灰/红都不跟着全局 QSS 走：灰取当前主题的主文字色降透明度，
    // 红取主题危险色，深浅色主题下都看得清
    const QColor textBase(ThemeManager::instance()->currentTextColor());
    QColor fg = m_liked ? m_likeColor : textBase;
    if (!m_liked)
        fg.setAlphaF(0.70);

    if (m_hovered && isEnabled())
    {
        QColor bg = m_liked ? m_likeColor : textBase;
        bg.setAlphaF(m_liked ? 0.10 : 0.06);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);
    }

    // 心形图标：高分辨率渲染一次，动画放大时也不会糊
    if (m_outline.isNull() || m_iconTint != fg)
    {
        m_iconTint = fg;
        const int src = kLikeHeart * 3;
        m_outline = IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/like.svg"), fg, src)
                        .pixmap(src, src);
        m_filled = IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/like_filled.svg"),
                                               m_likeColor, src)
                       .pixmap(src, src);
    }

    const QRectF heartRect(kLikePadX, (height() - kLikeHeart) / 2.0, kLikeHeart, kLikeHeart);

    // 点赞：0 → 峰值 → 0 的三角曲线再 smoothstep，弹一下回位；取消：轻轻缩一下
    qreal scale = 1.0;
    if (m_t > 0.0)
    {
        const qreal raw = m_t < 0.35 ? (m_t / 0.35) : (1.0 - (m_t - 0.35) / 0.65);
        const qreal eased = raw * raw * (3.0 - 2.0 * raw);
        scale = m_burstLiked ? (1.0 + 0.40 * eased) : (1.0 - 0.20 * eased);
    }
    if (m_pressed && isEnabled())
        scale *= 0.90;

    // 动画的「余波」画在心形后面：一团往外散的红光 + 一圈扩散的细红环，
    // 心心放大时正好把它们盖住，回弹后才会露出来，看起来是从心里「炸」开
    if (m_t > 0.0 && m_burstLiked)
    {
        const QPointF center = heartRect.center();

        const qreal glowRadius = 8.0 + 6.0 * m_t;
        QRadialGradient glow(center, glowRadius);
        QColor glowInner = m_likeColor;
        glowInner.setAlphaF(0.38 * (1.0 - m_t));
        QColor glowOuter = m_likeColor;
        glowOuter.setAlphaF(0.0);
        glow.setColorAt(0.0, glowInner);
        glow.setColorAt(1.0, glowOuter);
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(center, glowRadius, glowRadius);

        QColor ring = m_likeColor;
        ring.setAlphaF(0.65 * (1.0 - m_t));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ring, 1.8));
        const qreal ringRadius = 7.5 + 6.0 * m_t;
        painter.drawEllipse(center, ringRadius, ringRadius);
    }

    const QPixmap &heart = m_liked ? m_filled : m_outline;
    if (!heart.isNull())
    {
        painter.save();
        painter.translate(heartRect.center());
        painter.scale(scale, scale);
        painter.drawPixmap(QRectF(-kLikeHeart / 2.0, -kLikeHeart / 2.0, kLikeHeart, kLikeHeart),
                           heart, QRectF(heart.rect()));
        painter.restore();
    }

    painter.setFont(m_font);
    painter.setPen(fg);
    const QRectF numberRect(heartRect.right() + kLikeGap, 0.0,
                            width() - heartRect.right() - kLikeGap - kLikePadX, height());
    painter.drawText(numberRect, Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_likes));

    if (m_focusVisible)
    {
        QColor ring = m_likeColor;
        ring.setAlphaF(0.40);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ring, 1.2));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 6.0, 6.0);
    }
}

} // namespace ForumKit
