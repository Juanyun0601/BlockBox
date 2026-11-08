/**
 * @file   MultiplayerUiParts.h
 * @brief  联机页共用的版式零件（滚动页 / 卡片 / 状态条 / 状态圆点）
 * @author BlockBox Team
 * @date   2026-11-08
 *
 * 实例助手的联机页有两套：Java 版（MultiplayerPage）和基岩版
 * （BedrockMultiplayerPage），两者的版式完全一致，只是核心和字段不同。
 * 这里把重复出现的"零件"收敛成一组内联函数，两个页面直接复用：
 *   - makeScrollPage：内容可滚动 + 底部按钮固定；
 *   - makeCard / makeStatusCard / makeNoteCard：卡片、状态条、说明卡；
 *   - makeFieldLabel / makeHintLabel / makeDivider：表单里的小零件；
 *   - makeDot / setDotTone：状态小圆点，颜色由 tone 属性驱动；
 *   - makeLatencyRow：圆点 + 状态文字居中的一行。
 *
 * 颜色不在这里写死：所有卡片的底色 / 描边 / 圆角由使用方的页面样式表按
 * objectName（mpStatusCard / mpNoteCard / mpFormCard / mpCodeCard …）给出。
 */
#ifndef MULTIPLAYERUIPARTS_H
#define MULTIPLAYERUIPARTS_H

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

#include "utils/TouchScroll.h"

namespace MultiplayerUi
{
/// 页面滚动容器：内容随窗口高度滚，底部操作按钮留在滚动区外
struct ScrollPage
{
    QVBoxLayout *outer = nullptr; ///< 页面外层布局（底部固定按钮放这里）
    QVBoxLayout *body = nullptr;  ///< 滚动区内容布局
};

inline ScrollPage makeScrollPage(QWidget *page)
{
    ScrollPage parts;
    parts.outer = new QVBoxLayout(page);
    parts.outer->setContentsMargins(0, 12, 0, 12);
    parts.outer->setSpacing(12);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("mpScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    TouchScroll::enableDragScroll(scroll);

    auto *body = new QWidget();
    body->setObjectName(QStringLiteral("mpScrollBody"));
    parts.body = new QVBoxLayout(body);
    parts.body->setContentsMargins(0, 0, 2, 0);
    parts.body->setSpacing(12);
    scroll->setWidget(body);

    parts.outer->addWidget(scroll, 1);
    return parts;
}

/// 卡片容器：圆角 / 描边 / 底色都按 objectName 在页面样式表里给
inline QFrame *makeCard(const QString &objectName, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(objectName);
    return card;
}

/// 卡片内标题（13px 粗体）
inline QLabel *makeCardTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("mpCardTitle"));
    return label;
}

/// 表单字段名（12px 半粗）
inline QLabel *makeFieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("mpFieldLabel"));
    return label;
}

/// 字段下方的灰色小字说明（11px）
inline QLabel *makeHintLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("mpFieldHint"));
    label->setWordWrap(true);
    return label;
}

/// 卡片内分隔线
inline QFrame *makeDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("mpDivider"));
    line->setFixedHeight(1);
    return line;
}

/**
 * @brief 给富文本说明里的超链接上色（按当前主题）
 *
 * Qt 的样式表会把 QLabel 的调色板接管掉，光设 QPalette::Link 不生效，
 * 链接会一直用默认的深蓝——暗色主题下几乎看不清。这里直接改 HTML：
 * 先抹掉上一次留下的颜色，再按当前主题给每个 <a> 补一条行内色。
 */
inline void applyDescriptionLinkColor(QLabel *label, const QColor &color)
{
    if (!label || !color.isValid())
        return;

    const QString original = label->text();
    if (!original.contains(QLatin1String("<a ")))
        return;

    // 1. 抹掉上一次主题留下的颜色属性
    static const QRegularExpression previous(
        QStringLiteral("(<a\\s[^>]*?)\\s+style=\"color:[^\"]*\""),
        QRegularExpression::CaseInsensitiveOption);
    QString html = original;
    html.replace(previous, QStringLiteral("\\1"));

    // 2. 把颜色补在标签末尾（Qt 对 <a> 的属性顺序挑剔，放 href 前面会解析失败）
    static const QRegularExpression anchor(
        QStringLiteral("(<a\\s[^>]*[^/>\\s])>"),
        QRegularExpression::CaseInsensitiveOption);
    html.replace(anchor,
                 QStringLiteral("\\1 style=\"color:%1\">").arg(color.name()));

    if (html != label->text())
        label->setText(html);
}

/// 状态小圆点：颜色由 tone 属性（idle / ok / warn / err）决定
inline QLabel *makeDot(const QString &objectName, QWidget *parent)
{
    auto *dot = new QLabel(parent);
    dot->setObjectName(objectName);
    dot->setProperty("mpDot", true);
    dot->setProperty("tone", QStringLiteral("idle"));
    dot->setFixedSize(8, 8);
    return dot;
}

inline void setDotTone(QLabel *dot, const QString &tone)
{
    if (!dot || dot->property("tone").toString() == tone)
        return;
    dot->setProperty("tone", tone);
    dot->style()->unpolish(dot);
    dot->style()->polish(dot);
    dot->update();
}

/**
 * @brief 状态条：小圆点 + 一行状态文字
 * @param dotOut   返回圆点控件（后续用 setDotTone 换色）
 * @param labelOut 返回文字控件（内容由 refresh* 填）
 */
inline QFrame *makeStatusCard(QWidget *parent, QLabel *&dotOut, QLabel *&labelOut,
                              const QString &labelObjectName)
{
    auto *card = makeCard(QStringLiteral("mpStatusCard"), parent);
    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(9);

    dotOut = makeDot(QStringLiteral("mpStatusDot"), card);
    row->addWidget(dotOut, 0, Qt::AlignVCenter);

    labelOut = new QLabel(card);
    labelOut->setObjectName(labelObjectName);
    labelOut->setWordWrap(true);
    row->addWidget(labelOut, 1);
    return card;
}

/// 说明卡片：小标题 + 富文本说明（链接可点）
inline QFrame *makeNoteCard(QWidget *parent, const QString &title, QLabel *&labelOut,
                            const QString &labelObjectName)
{
    auto *card = makeCard(QStringLiteral("mpNoteCard"), parent);
    auto *column = new QVBoxLayout(card);
    column->setContentsMargins(14, 12, 14, 14);
    column->setSpacing(8);

    column->addWidget(makeCardTitle(title, card));

    labelOut = new QLabel(card);
    labelOut->setObjectName(labelObjectName);
    labelOut->setWordWrap(true);
    labelOut->setTextFormat(Qt::RichText);
    labelOut->setTextInteractionFlags(Qt::TextBrowserInteraction);
    labelOut->setOpenExternalLinks(true);
    column->addWidget(labelOut);
    return card;
}

/**
 * @brief 联机状态行：小圆点 + 状态文字，整组居中
 *
 * 圆点和文字放进同一个组里再居中，文字就不会被两侧的弹簧压成两行。
 */
inline QHBoxLayout *makeLatencyRow(QWidget *page, QLabel *&dotOut, QLabel *&labelOut)
{
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addStretch(1);

    auto *group = new QWidget(page);
    group->setObjectName(QStringLiteral("mpLatencyGroup"));
    auto *groupRow = new QHBoxLayout(group);
    groupRow->setContentsMargins(0, 0, 0, 0);
    groupRow->setSpacing(8);

    dotOut = makeDot(QStringLiteral("mpLatencyDot"), group);
    groupRow->addWidget(dotOut, 0, Qt::AlignVCenter);

    labelOut = new QLabel(group);
    labelOut->setObjectName(QStringLiteral("multiplayerLatencyLabel"));
    // 不开自动换行：开了之后 sizeHint 会被压窄，一行状态会被拆成两行
    labelOut->setWordWrap(false);
    groupRow->addWidget(labelOut, 1);

    row->addWidget(group);
    row->addStretch(1);
    return row;
}

/// 整页样式：Java 版与基岩版联机页共用这一份（对象名一致，颜色走 @TOKEN@ 令牌）
inline QString pageStyleSheet()
{
    return QStringLiteral(
        /* ───────────── 页面与滚动区 ───────────── */
        // 联机页自己铺主题底色：外层实例助手窗口用的是 palette(window)，
        // 深色主题下那块还是系统浅色，不自己铺底会露出一大片浅灰
        "QWidget#multiplayerRoot, QWidget#bedrockMultiplayerRoot,"
        "QWidget#multiplayerHome, QWidget#multiplayerRoom,"
        "QWidget#multiplayerHongshiHome, QWidget#multiplayerHongshiTunnel,"
        "QWidget#multiplayerFrpHome, QWidget#multiplayerFrpRoom,"
        "QWidget#bedrockMultiplayerHome, QWidget#bedrockMultiplayerRoom {"
        "  background: @BG_BASE@; border: none;"
        "}"
        "QScrollArea#mpScroll, QWidget#mpScrollBody,"
        "QScrollArea#multiplayerPlayersScroll, QWidget#multiplayerPlayersContainer {"
        "  background: transparent; border: none;"
        "}"

        /* ───────────── 顶部：核心选择 + 更新 ───────────── */
        "QWidget#multiplayerTopBar { background: transparent; border: none; }"
        "QLabel#mpTopCaption {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QFrame#mpTopDivider { background: @BORDER_LIGHT@; border: none; }"
        "QComboBox#multiplayerCoreCombo {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 600;"
        "  padding: 0 30px 0 12px;"
        "}"
        "QComboBox#multiplayerCoreCombo:hover { border-color: @BORDER_HOVER@; }"
        "QComboBox#multiplayerCoreCombo:focus { border-color: @PRIMARY@; }"
        "QComboBox#multiplayerCoreCombo::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: top right;"
        "  width: 26px; border: none; border-left: 1px solid @BORDER_LIGHT@;"
        "  border-top-right-radius: 9px; border-bottom-right-radius: 9px;"
        "  background: transparent;"
        "}"
        "QComboBox#multiplayerCoreCombo::drop-down:hover { background: @BG_HOVER@; }"
        "QComboBox#multiplayerCoreCombo::down-arrow {"
        "  image: url(:/Images/Icons/arrow_down_triangle.svg);"
        "  width: 10px; height: 10px; margin-right: 8px; border: none;"
        "}"
        "QComboBox#multiplayerCoreCombo QAbstractItemView {"
        "  background: @BG_CARD@; color: @TEXT_PRIMARY@; border: 1px solid @BORDER_STRONG@;"
        "  border-radius: 10px; padding: 4px; outline: none;"
        "  selection-background-color: @PRIMARY_BG@; selection-color: @TEXT_ACCENT@;"
        "}"
        "QPushButton#multiplayerUpdateBtn {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_SECONDARY@; font-size: 12px; font-weight: 600; padding: 0 12px;"
        "}"
        "QPushButton#multiplayerUpdateBtn:hover {"
        "  background: @BG_HOVER@; border-color: @BORDER_STRONG@; color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#multiplayerUpdateBtn:pressed { background: @BG_PRESSED@; }"
        "QPushButton#multiplayerUpdateBtn:disabled {"
        "  background: @BG_DISABLED@; border-color: transparent; color: @TEXT_DISABLED@;"
        "}"

        /* ───────────── 卡片 ───────────── */
        "QFrame#mpStatusCard {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER_LIGHT@; border-radius: 10px;"
        "}"
        "QFrame#mpNoteCard, QFrame#mpFormCard {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 12px;"
        "}"
        "QFrame#mpCodeCard {"
        "  background: @BG_MUTED@; border: 1px dashed @BORDER_DASHED@; border-radius: 12px;"
        "}"
        "QFrame#mpDivider { background: @BORDER_LIGHT@; border: none; }"
        "QLabel#mpCardTitle {"
        "  color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mpSectionTitle {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; font-weight: 700; letter-spacing: 1px;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mpCodeCaption {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mpFieldLabel {"
        "  color: @TEXT_SECONDARY@; font-size: 12px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mpFieldHint {"
        "  color: @TEXT_SECONDARY@; font-size: 11px;"
        "  background: transparent; border: none;"
        "}"

        /* ───────────── 状态文字与状态点 ───────────── */
        "QLabel#multiplayerStatusLabel {"
        "  color: @TEXT_SECONDARY@; font-size: 12px;"
        "  background: transparent; border: none;"
        "}"
        "QLabel[mpDot=\"true\"] {"
        "  background: @TEXT_TERTIARY@; border: none; border-radius: 4px;"
        "}"
        "QLabel[mpDot=\"true\"][tone=\"ok\"] { background: @SUCCESS@; }"
        "QLabel[mpDot=\"true\"][tone=\"warn\"] { background: @WARNING@; }"
        "QLabel[mpDot=\"true\"][tone=\"err\"] { background: @DANGER@; }"
        "QLabel#multiplayerLatencyLabel {"
        "  color: @TEXT_SECONDARY@; font-size: 12px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#multiplayerLatencyLabel[status=\"good\"] { color: @SUCCESS@; }"
        "QLabel#multiplayerLatencyLabel[status=\"medium\"] { color: @WARNING_TEXT@; }"
        "QLabel#multiplayerLatencyLabel[status=\"bad\"] { color: @DANGER_TEXT@; }"

        /* ───────────── 邀请码 / 联机地址 / 说明正文 ───────────── */
        "QLabel#multiplayerRoomCodeLabel {"
        "  color: @TEXT_PRIMARY@; font-size: 20px; font-weight: 800;"
        "  font-family: \"Consolas\", \"Cascadia Mono\", \"Microsoft YaHei\";"
        "  letter-spacing: 1px;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#multiplayerDescLabel {"
        "  color: @TEXT_SECONDARY@; font-size: 12px;"
        "  background: transparent; border: none;"
        "}"

        /* ───────────── 按钮 ───────────── */
        "QPushButton#multiplayerJoinBtn {"
        "  background: transparent; border: 1px solid @PRIMARY_RGBA_38@; border-radius: 11px;"
        "  color: @TEXT_ACCENT@; font-size: 14px; font-weight: 600;"
        "}"
        "QPushButton#multiplayerJoinBtn:hover {"
        "  background: @PRIMARY_RGBA_10@; border-color: @PRIMARY@;"
        "}"
        "QPushButton#multiplayerJoinBtn:pressed { background: @PRIMARY_RGBA_20@; }"
        "QPushButton#multiplayerJoinBtn:disabled {"
        "  background: @BG_DISABLED@; border-color: transparent; color: @TEXT_DISABLED@;"
        "}"
        "QPushButton#multiplayerCreateBtn, QPushButton#hongshiStartBtn {"
        "  background: @PRIMARY@; border: none; border-radius: 11px;"
        "  color: @TEXT_ON_PRIMARY@; font-size: 14px; font-weight: 700;"
        "}"
        "QPushButton#multiplayerCreateBtn:hover, QPushButton#hongshiStartBtn:hover {"
        "  background: @PRIMARY_HOVER@;"
        "}"
        "QPushButton#multiplayerCreateBtn:pressed, QPushButton#hongshiStartBtn:pressed {"
        "  background: @PRIMARY_PRESSED@;"
        "}"
        "QPushButton#multiplayerCreateBtn:disabled, QPushButton#hongshiStartBtn:disabled {"
        "  background: @BG_DISABLED@; color: @TEXT_DISABLED@;"
        "}"
        "QPushButton#multiplayerPublishBtn {"
        "  background: @PRIMARY_RGBA_10@; border: 1px solid @PRIMARY_RGBA_38@;"
        "  border-radius: 11px; color: @TEXT_ACCENT@; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton#multiplayerPublishBtn:hover {"
        "  background: @PRIMARY_RGBA_18@; border-color: @PRIMARY@;"
        "}"
        "QPushButton#multiplayerPublishBtn:pressed {"
        "  background: @PRIMARY_RGBA_26@; border-color: @PRIMARY@;"
        "}"
        "QPushButton#multiplayerPublishBtn:disabled {"
        "  background: transparent; border-color: @BORDER_LIGHT@; color: @TEXT_DISABLED@;"
        "}"
        // 已在大厅展示：底色更深一档，一眼看出再点就是下架
        "QPushButton#multiplayerPublishBtn[published=\"true\"] {"
        "  background: @PRIMARY_RGBA_20@; border-color: @PRIMARY@; color: @TEXT_ACCENT@;"
        "}"
        "QPushButton#multiplayerShareBtn {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 11px;"
        "  color: @TEXT_SECONDARY@; font-size: 13px; font-weight: 600; padding: 0 14px;"
        "}"
        "QPushButton#multiplayerShareBtn:hover {"
        "  background: @BG_HOVER@; border-color: @BORDER_STRONG@; color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#multiplayerShareBtn:pressed {"
        "  background: @PRIMARY_RGBA_10@; border-color: @PRIMARY_RGBA_38@; color: @TEXT_ACCENT@;"
        "}"
        "QPushButton#multiplayerShareBtn:disabled {"
        "  background: transparent; border-color: @BORDER_LIGHT@; color: @TEXT_DISABLED@;"
        "}"
        "QPushButton#multiplayerLeaveBtn {"
        "  background: transparent; border: 1px solid @DANGER_BORDER@; border-radius: 11px;"
        "  color: @DANGER_TEXT@; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton#multiplayerLeaveBtn:hover {"
        "  background: @DANGER_BG@; border-color: @DANGER@;"
        "}"
        "QPushButton#multiplayerLeaveBtn:pressed { background: @DANGER_BORDER@; }"
        "QPushButton#multiplayerCopyCodeBtn {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 9px;"
        "}"
        "QPushButton#multiplayerCopyCodeBtn:hover {"
        "  background: @PRIMARY_RGBA_10@; border-color: @PRIMARY_RGBA_38@;"
        "}"
        "QPushButton#multiplayerCopyCodeBtn:pressed { background: @PRIMARY_RGBA_20@; }"
        "QPushButton#hongshiNodeRefreshBtn {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 8px;"
        "  color: @TEXT_SECONDARY@; font-size: 12px; font-weight: 600; padding: 0 10px;"
        "}"
        "QPushButton#hongshiNodeRefreshBtn:hover {"
        "  background: @BG_HOVER@; border-color: @BORDER_STRONG@; color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#hongshiNodeRefreshBtn:disabled {"
        "  color: @TEXT_DISABLED@; border-color: @BORDER_LIGHT@;"
        "}"

        /* ───────────── 输入类控件 ───────────── */
        "QComboBox#hongshiNodeCombo {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 0 30px 0 12px;"
        "}"
        "QComboBox#hongshiNodeCombo:hover { border-color: @BORDER_HOVER@; }"
        "QComboBox#hongshiNodeCombo:focus { border-color: @PRIMARY@; }"
        "QComboBox#hongshiNodeCombo::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: top right;"
        "  width: 26px; border: none; border-left: 1px solid @BORDER_LIGHT@;"
        "  border-top-right-radius: 9px; border-bottom-right-radius: 9px;"
        "  background: transparent;"
        "}"
        "QComboBox#hongshiNodeCombo::drop-down:hover { background: @BG_HOVER@; }"
        "QComboBox#hongshiNodeCombo::down-arrow {"
        "  image: url(:/Images/Icons/arrow_down_triangle.svg);"
        "  width: 10px; height: 10px; margin-right: 8px; border: none;"
        "}"
        "QComboBox#hongshiNodeCombo QAbstractItemView {"
        "  background: @BG_CARD@; color: @TEXT_PRIMARY@; border: 1px solid @BORDER_STRONG@;"
        "  border-radius: 10px; padding: 4px; outline: none;"
        "  selection-background-color: @PRIMARY_BG@; selection-color: @TEXT_ACCENT@;"
        "}"
        "QLineEdit#frpKeyEdit, QLineEdit#frpIdEdit {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 0 12px;"
        "  selection-background-color: @PRIMARY_RGBA_38@; selection-color: @TEXT_PRIMARY@;"
        "}"
        "QLineEdit#frpKeyEdit:hover, QLineEdit#frpIdEdit:hover { border-color: @BORDER_HOVER@; }"
        "QLineEdit#frpKeyEdit:focus, QLineEdit#frpIdEdit:focus {"
        "  background: @BG_CARD@; border-color: @PRIMARY@;"
        "}"
        "QSpinBox#hongshiPortSpin {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 0 8px;"
        "}"
        "QSpinBox#hongshiPortSpin:hover { border-color: @BORDER_HOVER@; }"
        "QSpinBox#hongshiPortSpin:focus { background: @BG_CARD@; border-color: @PRIMARY@; }"


        /* ───────────── 玩家卡片 ───────────── */
        "QWidget[cardRole=\"mpContainer\"] {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 12px;"
        "}"
        "QWidget[cardRole=\"mpContainer\"]:hover { border-color: @PRIMARY_RGBA_38@; }"
        "QLabel[cardRole=\"mpName\"] {"
        "  color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QLabel[cardRole=\"mpChip\"] {"
        "  color: @TEXT_SECONDARY@; background: @BG_MUTED@; border: 1px solid @BORDER@;"
        "  border-radius: 8px; padding: 1px 7px; font-size: 11px; font-weight: 600;"
        "}"
        "QPushButton[cardRole=\"mpActionBtn\"] {"
        "  background: transparent; border: none; border-radius: 8px;"
        "}"
        "QPushButton[cardRole=\"mpActionBtn\"]:hover { background: @PRIMARY_RGBA_10@; }"
        "QLabel#mpEmptyPlayers {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; padding: 20px 8px;"
        "  background: transparent; border: none;"
        "}"

        /* ───────────── 基岩版联机页专用 ───────────── */
        // 基岩版核心固定为 GravityCone，用一个只读小标签代替下拉框
        "QLabel#multiplayerCoreBadge {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_SECONDARY@; font-size: 13px; font-weight: 600; padding: 0 12px;"
        "}"
        // 房客模式：游戏里要输入的服务器地址
        "QLabel#multiplayerAddressLabel {"
        "  color: @TEXT_PRIMARY@; font-size: 14px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}")
        ;
}
} // namespace MultiplayerUi

#endif // MULTIPLAYERUIPARTS_H
