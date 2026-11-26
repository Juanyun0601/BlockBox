/**
 * @file   ChatSettingsDialog.cpp
 * @brief  聊天设置弹窗实现
 * @author BlockBox Team
 * @date   2026-11-20
 */
#include "ChatSettingsDialog.h"

#include "CustomCheckBox.h"
#include "utils/ChatPrefs.h"
#include "utils/ChatUiStore.h"
#include "utils/ThemeManager.h"

#include <QColor>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "utils/UiMetrics.h"

namespace {

const char kCardName[] = "chatSettingsCard";

} // namespace

ChatSettingsDialog::ChatSettingsDialog(QWidget *parent) : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("chatSettingsDialog"));
    setWindowTitle(tr("聊天设置"));

    initUI();
    initStyle();
    syncFontButtons();
}

void ChatSettingsDialog::showEvent(QShowEvent *event)
{
    AppDialogBase::showEvent(event);
    // 样式在显示时才真正参与字体测量（主题色也可能在这之间变过），这里补一次样式，
    // 再按量出来的内容高度定卡片尺寸。
    initStyle();
    if (!m_card)
        return;

    // 卡片高度按内容量，但最多占满宿主窗口（四周留一点边）；
    // 设置项多了、窗口又矮的时候，只有中间内容区滚动，底部「完成」始终看得见。
    QSize available = UiMetrics::fitToScreen(this, QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    if (QWidget *host = hostWindow())
    {
        const QSize hostSize = host->size();
        if (hostSize.width() > 0 && hostSize.height() > 0)
        {
            available.setWidth(qMin(available.width(), hostSize.width()));
            available.setHeight(qMin(available.height(), hostSize.height()));
        }
    }
    const int maxWidth = qMax(360, available.width() - 48);
    const int maxHeight = qMax(260, available.height() - 48);

    // 卡片里除内容区以外的固定高度（标题、说明、底部按钮、边距）
    int chrome = 0;
    if (QLayout *cardLayout = m_card->layout())
    {
        for (int i = 0; i < cardLayout->count(); ++i)
        {
            QLayoutItem *item = cardLayout->itemAt(i);
            if (!item || (m_bodyScroll && item->widget() == m_bodyScroll))
                continue;
            chrome += item->sizeHint().height();
        }
        chrome += cardLayout->spacing() * qMax(0, cardLayout->count() - 1);
        const QMargins margins = cardLayout->contentsMargins();
        chrome += margins.top() + margins.bottom();
    }
    const int bodyHeight = (m_bodyScroll && m_bodyScroll->widget())
                               ? m_bodyScroll->widget()->sizeHint().height()
                               : 0;

    const int width = qMin(520, maxWidth);
    const int height = qBound(qMin(320, maxHeight), chrome + bodyHeight, maxHeight);
    m_card->setMinimumSize(qMin(460, width), qMin(320, maxHeight));
    m_card->setFixedSize(width, height);
    if (QLayout *dialogLayout = layout())
        dialogLayout->activate();
}

// ───────────────────────────── UI ─────────────────────────────

void ChatSettingsDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kCardName));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setMinimumSize(460, 320);
    card->setMaximumWidth(540);
    m_card = card;

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 18);
    cardLayout->setSpacing(10);

    auto *title = new QLabel(tr("聊天设置"), card);
    title->setObjectName(QStringLiteral("chatSettingsTitle"));
    cardLayout->addWidget(title);
    auto *subtitle = new QLabel(tr("设置只保存在这台设备上，不会同步到其他设备。"), card);
    subtitle->setObjectName(QStringLiteral("chatSettingsSubtitle"));
    subtitle->setWordWrap(true);
    cardLayout->addWidget(subtitle);
    cardLayout->addSpacing(2);

    // 设置项放进可滚动的「身体」里：窗口不高时只滚内容，底部「完成」始终露在外面
    m_bodyScroll = new QScrollArea(card);
    m_bodyScroll->setObjectName(QStringLiteral("chatSettingsScroll"));
    m_bodyScroll->setWidgetResizable(true);
    m_bodyScroll->setFrameShape(QFrame::NoFrame);
    m_bodyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget(m_bodyScroll);
    body->setObjectName(QStringLiteral("chatSettingsBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 10, 0);
    bodyLayout->setSpacing(10);
    m_bodyScroll->setWidget(body);
    cardLayout->addWidget(m_bodyScroll, 1);

    // ── 当前会话：置顶 / 免打扰（没有打开会话时整块隐藏） ──
    m_conversationSection = new QWidget(card);
    auto *conversationLayout = new QVBoxLayout(m_conversationSection);
    conversationLayout->setContentsMargins(0, 0, 0, 0);
    conversationLayout->setSpacing(6);

    auto *conversationHeader = new QHBoxLayout();
    conversationHeader->setSpacing(8);
    auto *conversationLabel = new QLabel(tr("当前会话"), m_conversationSection);
    conversationLabel->setObjectName(QStringLiteral("chatSettingsSection"));
    conversationHeader->addWidget(conversationLabel);
    conversationHeader->addStretch(1);
    m_conversationName = new QLabel(m_conversationSection);
    m_conversationName->setObjectName(QStringLiteral("chatSettingsConversation"));
    conversationHeader->addWidget(m_conversationName);
    conversationLayout->addLayout(conversationHeader);

    // 会话备注：只在本机生效的昵称，改完列表和聊天顶部立刻换名字
    auto *remarkRow = new QFrame(m_conversationSection);
    remarkRow->setObjectName(QStringLiteral("chatSettingRow"));
    remarkRow->setAttribute(Qt::WA_StyledBackground, true);
    auto *remarkLayout = new QVBoxLayout(remarkRow);
    remarkLayout->setContentsMargins(14, 10, 14, 12);
    remarkLayout->setSpacing(2);
    auto *remarkTitle = new QLabel(tr("会话备注"), remarkRow);
    remarkTitle->setObjectName(QStringLiteral("chatSettingTitle"));
    remarkLayout->addWidget(remarkTitle);
    auto *remarkHint = new QLabel(tr("只在本机显示，好友看不到；留空就恢复原来的昵称。"), remarkRow);
    remarkHint->setObjectName(QStringLiteral("chatSettingHint"));
    remarkHint->setWordWrap(true);
    remarkLayout->addWidget(remarkHint);
    remarkLayout->addSpacing(6);
    m_remarkEdit = new QLineEdit(remarkRow);
    m_remarkEdit->setObjectName(QStringLiteral("chatSettingInput"));
    m_remarkEdit->setPlaceholderText(tr("给这个会话起个名字，例如：一起玩生存的老王"));
    m_remarkEdit->setMaxLength(24);
    m_remarkEdit->setClearButtonEnabled(true);
    m_remarkEdit->setFixedHeight(32);
    remarkLayout->addWidget(m_remarkEdit);
    conversationLayout->addWidget(remarkRow);
    connect(m_remarkEdit, &QLineEdit::editingFinished, this, &ChatSettingsDialog::commitRemark);

    conversationLayout->addWidget(makeSwitchRow(
        m_conversationSection, tr("置顶会话"), tr("固定在会话列表最前面，随时一眼找到。"), &m_pinSwitch));
    conversationLayout->addWidget(makeSwitchRow(
        m_conversationSection, tr("消息免打扰"),
        tr("有新消息时只留一个小圆点，不再显示未读数字。"), &m_muteSwitch));
    conversationLayout->addWidget(makeSwitchRow(
        m_conversationSection, tr("隐藏消息预览"),
        tr("会话列表里只显示「消息内容已隐藏」，不露出最后一条消息的内容。"),
        &m_hidePreviewSwitch));

    // 有没发出去的草稿时，多给一个「清空」的入口（和右键菜单里的一致）
    m_draftRow = new QFrame(m_conversationSection);
    m_draftRow->setObjectName(QStringLiteral("chatSettingRow"));
    m_draftRow->setAttribute(Qt::WA_StyledBackground, true);
    auto *draftLayout = new QHBoxLayout(m_draftRow);
    draftLayout->setContentsMargins(14, 10, 12, 10);
    draftLayout->setSpacing(12);
    auto *draftText = new QVBoxLayout();
    draftText->setSpacing(2);
    auto *draftTitle = new QLabel(tr("未发送的草稿"), m_draftRow);
    draftTitle->setObjectName(QStringLiteral("chatSettingTitle"));
    draftText->addWidget(draftTitle);
    auto *draftHint = new QLabel(tr("删掉后输入框里还没发出去的文字也会一起清空。"), m_draftRow);
    draftHint->setObjectName(QStringLiteral("chatSettingHint"));
    draftHint->setWordWrap(true);
    draftText->addWidget(draftHint);
    draftLayout->addLayout(draftText, 1);
    auto *clearDraftButton = new QPushButton(tr("清空"), m_draftRow);
    clearDraftButton->setObjectName(QStringLiteral("chatDraftClear"));
    clearDraftButton->setCursor(Qt::PointingHandCursor);
    clearDraftButton->setFixedHeight(28);
    connect(clearDraftButton, &QPushButton::clicked, this, [this]() {
        if (m_key.isEmpty())
            return;
        ChatUiStore::instance()->setDraft(m_key, QString());
        if (m_draftRow)
            m_draftRow->hide();
        emit draftCleared();
    });
    draftLayout->addWidget(clearDraftButton, 0, Qt::AlignVCenter);
    m_draftRow->hide();
    conversationLayout->addWidget(m_draftRow);
    bodyLayout->addWidget(m_conversationSection);

    // 备注 / 置顶 / 免打扰 / 预览开关改动即时写入（与右键菜单共用同一份记录）
    connect(m_pinSwitch, &CustomCheckBox::toggled, this, [this](bool on) {
        if (!m_key.isEmpty())
            ChatUiStore::instance()->setPinned(m_key, on);
    });
    connect(m_muteSwitch, &CustomCheckBox::toggled, this, [this](bool on) {
        if (!m_key.isEmpty())
            ChatUiStore::instance()->setMuted(m_key, on);
    });
    connect(m_hidePreviewSwitch, &CustomCheckBox::toggled, this, [this](bool on) {
        if (!m_key.isEmpty())
            ChatUiStore::instance()->setHidesPreview(m_key, on);
    });
    // 点「完成」或被遮罩关掉都会走 finished：把还没提交的备注写进去
    connect(this, &QDialog::finished, this, &ChatSettingsDialog::commitRemark);

    // ── 通用：发送方式 + 聊天字号 ──
    bodyLayout->addSpacing(4);
    auto *generalLabel = new QLabel(tr("通用"), card);
    generalLabel->setObjectName(QStringLiteral("chatSettingsSection"));
    bodyLayout->addWidget(generalLabel);

    bodyLayout->addWidget(makeSwitchRow(
        card, tr("按 Enter 发送消息"), tr("关闭后 Enter 换行，改用 Ctrl+Enter 发送。"),
        &m_enterSwitch));
    m_enterSwitch->setChecked(ChatPrefs::instance()->sendOnEnter());
    connect(m_enterSwitch, &CustomCheckBox::toggled, this, [](bool on) {
        ChatPrefs::instance()->setSendOnEnter(on);
    });

    bodyLayout->addWidget(makeSwitchRow(
        card, tr("私聊发文件优先直传"),
        tr("和好友互发文件时先试试点对点直传，大文件不再受 5 MB 和下载流量限制；"
           "连不上时会自动改走服务器。"),
        &m_p2pSwitch));
    m_p2pSwitch->setChecked(ChatPrefs::instance()->p2pDirectSend());
    connect(m_p2pSwitch, &CustomCheckBox::toggled, this, [](bool on) {
        ChatPrefs::instance()->setP2pDirectSend(on);
    });

    auto *fontRow = new QFrame(card);
    fontRow->setObjectName(QStringLiteral("chatSettingRow"));
    fontRow->setAttribute(Qt::WA_StyledBackground, true);
    // 字号一行拆成上下两层：标题 + 说明在上面，整条分段按钮占满下面一行，
    // 三个档位的点击区域更宽，说明文字也不会被按钮挤成两行。
    auto *fontLayout = new QVBoxLayout(fontRow);
    fontLayout->setContentsMargins(14, 10, 14, 12);
    fontLayout->setSpacing(2);
    auto *fontTitle = new QLabel(tr("聊天字号"), fontRow);
    fontTitle->setObjectName(QStringLiteral("chatSettingTitle"));
    fontLayout->addWidget(fontTitle);
    auto *fontHint = new QLabel(tr("消息文字大小，改完立刻生效。"), fontRow);
    fontHint->setObjectName(QStringLiteral("chatSettingHint"));
    fontHint->setWordWrap(true);
    fontLayout->addWidget(fontHint);
    fontLayout->addSpacing(6);

    auto *fontBar = new QWidget(fontRow);
    fontBar->setObjectName(QStringLiteral("chatFontBar"));
    auto *fontBarLayout = new QHBoxLayout(fontBar);
    fontBarLayout->setContentsMargins(2, 2, 2, 2);
    fontBarLayout->setSpacing(0);
    const auto addFontButton = [&](const QString &text, ChatPrefs::FontSize size,
                                   QPushButton **out) {
        auto *button = new QPushButton(text, fontBar);
        button->setObjectName(QStringLiteral("chatFontOption"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        button->setMinimumWidth(50);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(button, &QPushButton::clicked, this,
                [size]() { ChatPrefs::instance()->setFontSize(size); });
        fontBarLayout->addWidget(button, 1);
        *out = button;
    };
    addFontButton(tr("小"), ChatPrefs::FontSmall, &m_smallButton);
    addFontButton(tr("标准"), ChatPrefs::FontNormal, &m_normalButton);
    addFontButton(tr("大"), ChatPrefs::FontLarge, &m_largeButton);
    fontLayout->addWidget(fontBar);
    bodyLayout->addWidget(fontRow);

    cardLayout->addSpacing(10);
    auto *footer = new QHBoxLayout();
    footer->addStretch(1);
    auto *doneButton = new QPushButton(tr("完成"), card);
    doneButton->setObjectName(QStringLiteral("chatSettingsDoneButton"));
    doneButton->setCursor(Qt::PointingHandCursor);
    doneButton->setFixedHeight(34);
    connect(doneButton, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(doneButton);
    cardLayout->addLayout(footer);

    // 卡片按内容高度居中：上下留弹性空白，小屏幕由基类的卡片内滚动兜底
    auto *main = new QVBoxLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addStretch(1);
    main->addWidget(card, 0, Qt::AlignHCenter);
    main->addStretch(1);
}

QWidget *ChatSettingsDialog::makeSwitchRow(QWidget *parent, const QString &title,
                                           const QString &hint, CustomCheckBox **toggleOut)
{
    auto *row = new QFrame(parent);
    row->setObjectName(QStringLiteral("chatSettingRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 12, 10);
    layout->setSpacing(12);

    auto *textColumn = new QVBoxLayout();
    textColumn->setSpacing(2);
    auto *titleLabel = new QLabel(title, row);
    titleLabel->setObjectName(QStringLiteral("chatSettingTitle"));
    textColumn->addWidget(titleLabel);
    if (!hint.isEmpty())
    {
        auto *hintLabel = new QLabel(hint, row);
        hintLabel->setObjectName(QStringLiteral("chatSettingHint"));
        hintLabel->setWordWrap(true);
        textColumn->addWidget(hintLabel);
    }
    layout->addLayout(textColumn, 1);

    auto *toggle = new CustomCheckBox(QString(), row);
    toggle->setFixedHeight(24);
    layout->addWidget(toggle, 0, Qt::AlignVCenter);
    *toggleOut = toggle;
    return row;
}

void ChatSettingsDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg      = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder  = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor  = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor   = isLight ? "#333333" : "#e8e8e8";
    const QString muted       = isLight ? "#94A3B8" : "#7a8290";
    const QString optionColor = isLight ? "#475569" : "#c8ccd4";
    const QString rowBg       = isLight ? "#f7f8fa" : "#38383d";
    const QString fieldBorder = isLight ? "#e2e5ea" : "#4a4a50";
    const QString fieldBg     = isLight ? "#eceef1" : "#2f2f34";
    const QString themeHover  = QColor(themeColor).lighter(112).name();

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QWidget#chatSettingsBody { background: transparent; }"
        "QScrollArea#chatSettingsScroll { background: transparent; border: none; }"
        "QScrollArea#chatSettingsScroll QScrollBar:vertical {"
        "    background: transparent; width: 8px; margin: 4px 0 4px 0; }"
        "QScrollArea#chatSettingsScroll QScrollBar::handle:vertical {"
        "    background: rgba(128, 128, 128, 140); border-radius: 4px; min-height: 28px; }"
        "QScrollArea#chatSettingsScroll QScrollBar::handle:vertical:hover {"
        "    background: rgba(128, 128, 128, 190); }"
        "QScrollArea#chatSettingsScroll QScrollBar::add-line:vertical,"
        "QScrollArea#chatSettingsScroll QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollArea#chatSettingsScroll QScrollBar::add-page:vertical,"
        "QScrollArea#chatSettingsScroll QScrollBar::sub-page:vertical { background: transparent; }"
        "QLabel#chatSettingsTitle { color: %4; font-size: 16px; font-weight: 700; background: transparent; }"
        "QLabel#chatSettingsSubtitle { color: %5; font-size: 11.5px; background: transparent; }"
        "QLabel#chatSettingsSection { color: %5; font-size: 11.5px; font-weight: 600; background: transparent; }"
        "QLabel#chatSettingsConversation { color: %6; font-size: 11.5px; background: transparent; }"
        "QFrame#chatSettingRow {"
        "    background-color: %7; border: 1px solid %8; border-radius: 12px;"
        "}"
        "QLabel#chatSettingTitle { color: %6; font-size: 13px; font-weight: 600; background: transparent; }"
        "QLabel#chatSettingHint { color: %5; font-size: 11.5px; background: transparent; }"
        "QLineEdit#chatSettingInput {"
        "    background-color: %9; border: 1px solid %8; border-radius: 8px;"
        "    color: %6; padding: 0 10px; font-size: 12.5px;"
        "}"
        "QLineEdit#chatSettingInput:focus { border-color: %10; }"
        "QPushButton#chatDraftClear {"
        "    background: transparent; border: 1px solid %8; border-radius: 7px;"
        "    color: %12; padding: 0 14px; font-size: 11.5px;"
        "}"
        "QPushButton#chatDraftClear:hover { border-color: %10; color: %6; }"
        "QWidget#chatFontBar { background-color: %9; border-radius: 9px; }"
        "QPushButton#chatFontOption {"
        "    background: transparent; border: none; border-radius: 7px;"
        "    color: %12; padding: 0 10px; font-size: 12px;"
        "}"
        "QPushButton#chatFontOption:hover { color: %6; }"
        "QPushButton#chatFontOption:checked { background-color: %10; color: #ffffff; font-weight: 600; }"
        "QPushButton#chatSettingsDoneButton {"
        "    background-color: %10; border: none; border-radius: 9px;"
        "    color: #ffffff; padding: 0 26px; font-size: 12.5px; font-weight: 600;"
        "}"
        "QPushButton#chatSettingsDoneButton:hover { background-color: %11; }"
    ).arg(QString::fromLatin1(kCardName), cardBg, cardBorder, titleColor, muted, textColor,
          rowBg, fieldBorder, fieldBg, themeColor, themeHover, optionColor);

    setStyleSheet(style);
}

// ───────────────────────────── 状态 ─────────────────────────────

void ChatSettingsDialog::setConversation(const QString &key, const QString &name, bool group)
{
    m_key = key;
    const bool hasConversation = !key.isEmpty();
    m_conversationSection->setVisible(hasConversation);
    if (!hasConversation)
        return;

    // 群名 / 昵称可能很长：横排里只留一行，超出部分省略，完整内容放 tooltip
    const QString caption =
        group ? tr("群聊 · %1").arg(name) : tr("与 %1 的私聊").arg(name);
    const QFontMetrics metrics(m_conversationName->font());
    m_conversationName->setMaximumWidth(220);
    m_conversationName->setText(metrics.elidedText(caption, Qt::ElideRight, 210));
    m_conversationName->setToolTip(caption);

    ChatUiStore *store = ChatUiStore::instance();
    m_remarkEdit->setText(store->remark(key));
    m_pinSwitch->setChecked(store->isPinned(key));
    m_muteSwitch->setChecked(store->isMuted(key));
    m_hidePreviewSwitch->setChecked(store->hidesPreview(key));
    if (m_draftRow)
        m_draftRow->setVisible(!store->draft(key).isEmpty());
}

void ChatSettingsDialog::commitRemark()
{
    if (m_key.isEmpty() || !m_remarkEdit)
        return;
    ChatUiStore::instance()->setRemark(m_key, m_remarkEdit->text());
}

void ChatSettingsDialog::syncFontButtons()
{
    const ChatPrefs::FontSize size = ChatPrefs::instance()->fontSize();
    m_smallButton->setChecked(size == ChatPrefs::FontSmall);
    m_normalButton->setChecked(size == ChatPrefs::FontNormal);
    m_largeButton->setChecked(size == ChatPrefs::FontLarge);
}
