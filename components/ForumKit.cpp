/**
 * @file   ForumKit.cpp
 * @brief  论坛页面共用小部件实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumKit.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QImage>
#include <QLayoutItem>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTextCursor>
#include <QVBoxLayout>

#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
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

    QPointer<CoverBanner> guard(this);
    ForumImageStore::instance()->requestImage(
        ForumAccountService::instance()->absoluteUrl(path), [guard](const QImage &image) {
            if (guard.isNull() || image.isNull())
                return;
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

QVector<MarkdownPreset> lobbyIntroPresets()
{
    return {
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

} // namespace ForumKit
