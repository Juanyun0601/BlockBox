/**
 * @file   ForumComposerPage.cpp
 * @brief  发布详情页实现：顶部「帖子 / 资源」切换
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumComposerPage.h"

#include <QComboBox>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPair>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QFileDialog>
#include "components/NotificationManager.h"
#include "components/ForumKit.h"
#include "components/ForumCarouselEditor.h"
#include "components/ForumTagEditor.h"
#include "components/MasonryContentCard.h"
#include "components/PublishKit.h"
#include "components/QuizDialog.h"
#include "components/CommunityAgreementDialog.h"
#include "utils/CommandAssistant/LitematicReader.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/ForumLobbyService.h"
#include "utils/plugin/PluginManager.h"
#include "utils/StyleKit.h"

namespace {
constexpr int kMaxTags = 4;
constexpr int kMaxTagLength = 12;
constexpr int kTitleLimit = 80;
constexpr int kContentLimit = 20000;
/** 与服务端 maxPluginBytes 一致；超出直接本地提示，免得白传 */
constexpr qint64 kMaxPluginBytes = 20 * 1024 * 1024;
/** 与服务端 maxSchematicBytes 一致；投影文件比插件包大得多，放宽到 50 MB */
constexpr qint64 kMaxSchematicBytes = 50 * 1024 * 1024;
/** 与服务端 MAX_SCHEMATIC_FILES 一致：一帖最多挂几份投影（主世界 / 下界…） */
constexpr int kMaxSchematicFiles = 8;
/** 与服务端 MAX_SCHEMATIC_LABEL 一致：版本名最长多少字 */
constexpr int kMaxSchematicLabel = 20;
/** 只用改后缀挡不住手滑：本地也认这几种投影扩展名 */
const QStringList kSchematicExtensions = {
    QStringLiteral("litematic"), QStringLiteral("schem"), QStringLiteral("schematic"),
    QStringLiteral("nbt"),
};
/** 插件分享板块：资源→插件 的帖子发到这里，「插件与开发」只留讨论 */
constexpr auto kPluginShareBoardSlug = "plugin-share";
/** 投影分享板块：资源→投影 的帖子发到这里，普通帖子里也不提供 */
constexpr auto kSchematicShareBoardSlug = "schematic-share";
/** 教程分享板块：教程帖固定发到这里，与网页版同一口径 */
constexpr auto kTutorialShareBoardSlug = "tutorial-share";
/** 有奖问答板块：问答帖固定发到这里，普通发帖的下拉里不提供 */
constexpr auto kQuestionBoardSlug = "qa";
/** 网站导航板块：导航→网站 的帖子固定发到这里，普通发帖的下拉里不提供 */
constexpr auto kWebsiteNavBoardSlug = "website-nav";
/** 服务器导航板块：导航→服务器 的帖子固定发到这里，普通发帖的下拉里不提供 */
constexpr auto kServerNavBoardSlug = "server-nav";
/** 与服务端一致：导航帖的网址最多 500 字 */
constexpr int kMaxNavigationUrlLength = 500;
/** 有奖问答固定的赏金范围（与服务端 BOUNTY_MIN / BOUNTY_MAX 一致） */
constexpr int kMinBounty = 1;
constexpr int kMaxBounty = 500;
/** 默认悬赏 10 方块，和「10 个方块」的常见玩法对齐 */
constexpr int kDefaultBounty = 10;
/** 与服务端 maxFileBytes 一致：教程资料附件单个上限 50 MB */
constexpr qint64 kMaxAttachmentBytes = 50 * 1024 * 1024;
/** 与服务端一致：一帖最多 10 个资料附件 */
constexpr int kMaxAttachments = 10;
/** 教程资料附件的扩展名白名单（与服务端 ATTACHMENT_EXTS 保持同步） */
const QStringList kAttachmentExtensions = {
    QStringLiteral("pdf"), QStringLiteral("zip"), QStringLiteral("rar"),
    QStringLiteral("7z"),  QStringLiteral("txt"), QStringLiteral("md"),
    QStringLiteral("json"), QStringLiteral("csv"), QStringLiteral("log"),
    QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"),
};

/** 论坛热门标签缓存：发布页反复打开时只拉一次，失败也不反复打扰论坛 */
QVector<QPair<QString, int>> s_popularTags;
bool s_popularTagsRequested = false;
} // namespace

ForumComposerPage::ForumComposerPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    loadTagSuggestions();

    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            &ForumComposerPage::updateState);
}

void ForumComposerPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(12);

    // ── 未登录态 ──
    m_loginHost = new QWidget(content);
    m_loginHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *loginLayout = new QVBoxLayout(m_loginHost);
    loginLayout->setContentsMargins(0, 60, 0, 60);
    loginLayout->setSpacing(12);
    auto *loginHint = new QLabel(tr("登录论坛账号后就可以发帖了"), m_loginHost);
    loginHint->setObjectName(QStringLiteral("forumStatus"));
    loginHint->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(loginHint);
    auto *loginButton = new QPushButton(tr("去登录"), m_loginHost);
    loginButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setFixedHeight(34);
    connect(loginButton, &QPushButton::clicked, this, &ForumComposerPage::loginRequested);
    loginLayout->addWidget(loginButton, 0, Qt::AlignHCenter);
    layout->addWidget(m_loginHost);

    // ── 未答题态（答题和账号绑定，通过后才能发帖） ──
    m_quizHost = new QWidget(content);
    m_quizHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *quizLayout = new QVBoxLayout(m_quizHost);
    quizLayout->setContentsMargins(0, 60, 0, 60);
    quizLayout->setSpacing(12);
    auto *quizHint = new QLabel(tr("通过入站答题后就可以发帖了，成绩会记在你的账号上"), m_quizHost);
    quizHint->setObjectName(QStringLiteral("forumStatus"));
    quizHint->setAlignment(Qt::AlignCenter);
    quizLayout->addWidget(quizHint);
    auto *quizButton = new QPushButton(tr("去答题"), m_quizHost);
    quizButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    quizButton->setCursor(Qt::PointingHandCursor);
    quizButton->setFixedHeight(34);
    connect(quizButton, &QPushButton::clicked, this, &ForumComposerPage::openQuizDialog);
    quizLayout->addWidget(quizButton, 0, Qt::AlignHCenter);
    layout->addWidget(m_quizHost);

    // ── 表单态 ──
    m_formHost = new QWidget(content);
    m_formHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *formLayout = new QVBoxLayout(m_formHost);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(10);

    // ── 顶部「帖子 / 资源 / 教程」切换（分段控件，与登录方式切换同款样式） ──
    m_modeBar = new QWidget(m_formHost);
    m_modeBar->setObjectName(QStringLiteral("composerSegBar"));
    auto *modeLayout = new QHBoxLayout(m_modeBar);
    modeLayout->setContentsMargins(4, 4, 4, 4);
    modeLayout->setSpacing(4);

    m_postModeButton = new QPushButton(tr("帖子"), m_modeBar);
    m_postModeButton->setObjectName(QStringLiteral("composerSegBtn"));
    m_postModeButton->setCheckable(true);
    m_postModeButton->setAutoExclusive(true);
    m_postModeButton->setCursor(Qt::PointingHandCursor);
    m_postModeButton->setToolTip(tr("发帖讨论：选一个社区板块，交流、求助与作品分享"));
    modeLayout->addWidget(m_postModeButton, 1);

    m_resourceModeButton = new QPushButton(tr("资源"), m_modeBar);
    m_resourceModeButton->setObjectName(QStringLiteral("composerSegBtn"));
    m_resourceModeButton->setCheckable(true);
    m_resourceModeButton->setAutoExclusive(true);
    m_resourceModeButton->setCursor(Qt::PointingHandCursor);
    m_resourceModeButton->setToolTip(tr("发资源：先选分类，再上传文件（目前有插件、投影）"));
    modeLayout->addWidget(m_resourceModeButton, 1);

    m_tutorialModeButton = new QPushButton(tr("教程"), m_modeBar);
    m_tutorialModeButton->setObjectName(QStringLiteral("composerSegBtn"));
    m_tutorialModeButton->setCheckable(true);
    m_tutorialModeButton->setAutoExclusive(true);
    m_tutorialModeButton->setCursor(Qt::PointingHandCursor);
    m_tutorialModeButton->setToolTip(tr("写教程：固定发到「教程分享」，正文自动生成目录，可挂资料附件"));
    modeLayout->addWidget(m_tutorialModeButton, 1);

    m_questionModeButton = new QPushButton(tr("问答"), m_modeBar);
    m_questionModeButton->setObjectName(QStringLiteral("composerSegBtn"));
    m_questionModeButton->setCheckable(true);
    m_questionModeButton->setAutoExclusive(true);
    m_questionModeButton->setCursor(Qt::PointingHandCursor);
    m_questionModeButton->setToolTip(tr("有奖问答：先支付方块赏金，回答盖楼后你采纳哪一层，赏金就发给谁"));
    modeLayout->addWidget(m_questionModeButton, 1);

    m_navigationModeButton = new QPushButton(tr("导航"), m_modeBar);
    m_navigationModeButton->setObjectName(QStringLiteral("composerSegBtn"));
    m_navigationModeButton->setCheckable(true);
    m_navigationModeButton->setAutoExclusive(true);
    m_navigationModeButton->setCursor(Qt::PointingHandCursor);
    m_navigationModeButton->setToolTip(tr("发导航：分享一个好网站或服务器地址，点开就能一键直达"));
    modeLayout->addWidget(m_navigationModeButton, 1);

    connect(m_postModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Post); });
    connect(m_resourceModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Resource); });
    connect(m_tutorialModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Tutorial); });
    connect(m_questionModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Question); });
    connect(m_navigationModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Navigation); });
    formLayout->addWidget(m_modeBar);

    auto *formCard = ForumKit::makeCard(m_formHost);
    auto *cardLayout = new QVBoxLayout(formCard);
    cardLayout->setContentsMargins(20, 16, 20, 16);
    cardLayout->setSpacing(8);

    m_formTitleLabel = ForumKit::makeSectionTitle(formCard, tr("发表新主题"));
    cardLayout->addWidget(m_formTitleLabel);

    // ── 板块行（帖子模式）：发帖只做交流讨论，「插件与开发」也一样 ──
    m_boardRow = new QWidget(formCard);
    m_boardRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *boardRowLayout = new QVBoxLayout(m_boardRow);
    boardRowLayout->setContentsMargins(0, 0, 0, 0);
    boardRowLayout->setSpacing(6);
    boardRowLayout->addWidget(
        ForumKit::makeMuted(m_boardRow, tr("板块（发帖只做交流讨论，分享插件请切到「资源」）")));
    m_boardCombo = new QComboBox(m_boardRow);
    m_boardCombo->setObjectName(QStringLiteral("forumField"));
    boardRowLayout->addWidget(m_boardCombo);
    cardLayout->addWidget(m_boardRow);

    // ── 资源分类行（资源模式）：目前只有「插件」一个分类 ──
    m_kindRow = new QWidget(formCard);
    m_kindRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *kindRowLayout = new QVBoxLayout(m_kindRow);
    kindRowLayout->setContentsMargins(0, 0, 0, 0);
    kindRowLayout->setSpacing(6);
    kindRowLayout->addWidget(ForumKit::makeMuted(m_kindRow, tr("资源分类")));

    auto *kindChipsHost = new QWidget(m_kindRow);
    kindChipsHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *kindChipsLayout = new QHBoxLayout(kindChipsHost);
    kindChipsLayout->setContentsMargins(0, 0, 0, 0);
    kindChipsLayout->setSpacing(8);

    // 分类清单：以后加新资源分类，往这里补一条即可（slug 与 PublishKit::ResourceKind 对齐）
    const QVector<QPair<QString, QString>> resourceKinds = {
        { QStringLiteral("plugin"), tr("插件") },
        { QStringLiteral("schematic"), tr("投影") },
    };
    for (const auto &kind : resourceKinds)
    {
        auto *chip = new QPushButton(kind.second, kindChipsHost);
        chip->setObjectName(QStringLiteral("forumSortButton"));
        chip->setCheckable(true);
        chip->setAutoExclusive(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(26);
        chip->setProperty("kindSlug", kind.first);
        connect(chip, &QPushButton::clicked, this, [this, slug = kind.first]() {
            m_kindSlug = slug;
            applyMode();
        });
        kindChipsLayout->addWidget(chip);
        m_kindButtons.append(chip);
    }
    kindChipsLayout->addStretch(1);
    kindRowLayout->addWidget(kindChipsHost);

    auto *kindHint = new QLabel(
        tr("插件帖发到「插件分享」板块、投影帖发到「投影分享」板块：上传对应文件，填好标题和正文就能发布"),
        m_kindRow);
    kindHint->setObjectName(QStringLiteral("forumMuted"));
    kindHint->setWordWrap(true);
    kindRowLayout->addWidget(kindHint);
    cardLayout->addWidget(m_kindRow);

    // ── 导航行（导航模式）：选类型（网站 / 服务器），再填要分享的网址 ──
    m_navRow = new QWidget(formCard);
    m_navRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *navRowLayout = new QVBoxLayout(m_navRow);
    navRowLayout->setContentsMargins(0, 0, 0, 0);
    navRowLayout->setSpacing(6);
    navRowLayout->addWidget(ForumKit::makeMuted(m_navRow, tr("导航类型")));

    auto *navTypeChipsHost = new QWidget(m_navRow);
    navTypeChipsHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *navTypeChipsLayout = new QHBoxLayout(navTypeChipsHost);
    navTypeChipsLayout->setContentsMargins(0, 0, 0, 0);
    navTypeChipsLayout->setSpacing(8);

    // 类型清单：以后加新导航类型，往这里补一条即可（slug 与服务端 thread_navigations.kind 对齐）
    const QVector<QPair<QString, QString>> navigationKinds = {
        { QStringLiteral("website"), tr("网站") },
        { QStringLiteral("server"), tr("服务器") },
    };
    for (const auto &kind : navigationKinds)
    {
        auto *chip = new QPushButton(kind.second, navTypeChipsHost);
        chip->setObjectName(QStringLiteral("forumSortButton"));
        chip->setCheckable(true);
        chip->setAutoExclusive(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(26);
        chip->setProperty("kindSlug", kind.first);
        connect(chip, &QPushButton::clicked, this, [this, slug = kind.first]() {
            m_navKind = slug;
            applyMode();
        });
        navTypeChipsLayout->addWidget(chip);
        m_navTypeButtons.append(chip);
    }
    navTypeChipsLayout->addStretch(1);
    navRowLayout->addWidget(navTypeChipsHost);

    m_navUrlEdit = new QLineEdit(m_navRow);
    m_navUrlEdit->setObjectName(QStringLiteral("forumField"));
    m_navUrlEdit->setClearButtonEnabled(true);
    navRowLayout->addWidget(m_navUrlEdit);

    auto *navHint = new QLabel(
        tr("网站帖发到「网站导航」板块、服务器帖发到「服务器导航」板块：填好网址、标题和介绍就能发布，详情页支持一键打开与复制地址"),
        m_navRow);
    navHint->setObjectName(QStringLiteral("forumMuted"));
    navHint->setWordWrap(true);
    navRowLayout->addWidget(navHint);
    cardLayout->addWidget(m_navRow);

    // ── 赏金卡（问答模式）：发布时先从余额里托管，采纳后发给回答者 ──
    auto *bountyPanel = new QFrame(formCard);
    bountyPanel->setObjectName(QStringLiteral("forumBountyPanel"));
    m_bountyRow = bountyPanel;
    auto *bountyRowLayout = new QVBoxLayout(bountyPanel);
    bountyRowLayout->setContentsMargins(16, 14, 16, 14);
    bountyRowLayout->setSpacing(9);

    // 标题行：左边「🎁 悬赏方块」，右边当前余额小胶囊
    auto *bountyTitleRow = new QHBoxLayout();
    bountyTitleRow->setContentsMargins(0, 0, 0, 0);
    bountyTitleRow->setSpacing(8);
    auto *bountyTitle = new QLabel(tr("🎁 悬赏方块"), bountyPanel);
    bountyTitle->setObjectName(QStringLiteral("forumBountyTitle"));
    bountyTitleRow->addWidget(bountyTitle);
    bountyTitleRow->addStretch(1);
    m_bountyBalanceLabel = new QLabel(bountyPanel);
    m_bountyBalanceLabel->setObjectName(QStringLiteral("forumBountyBalance"));
    m_bountyBalanceLabel->setFixedHeight(22);
    m_bountyBalanceLabel->setAlignment(Qt::AlignCenter);
    bountyTitleRow->addWidget(m_bountyBalanceLabel);
    bountyRowLayout->addLayout(bountyTitleRow);

    auto *bountyInputRow = new QHBoxLayout();
    bountyInputRow->setContentsMargins(0, 0, 0, 0);
    bountyInputRow->setSpacing(10);
    m_bountySpin = new QSpinBox(bountyPanel);
    m_bountySpin->setObjectName(QStringLiteral("forumBountySpin"));
    m_bountySpin->setRange(kMinBounty, kMaxBounty);
    m_bountySpin->setValue(kDefaultBounty);
    m_bountySpin->setSingleStep(5);
    m_bountySpin->setSuffix(tr(" 方块"));
    // 数字输入不做上下箭头：右边那排快捷金额更好点，也不用挤两个小方块
    m_bountySpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_bountySpin->setFixedHeight(40);
    m_bountySpin->setFixedWidth(160);
    m_bountySpin->setToolTip(tr("悬赏 %1~%2 个方块").arg(kMinBounty).arg(kMaxBounty));
    bountyInputRow->addWidget(m_bountySpin);

    auto *bountyInlineHint = new QLabel(tr("发布时从余额托管，采纳后当场发给回答者"), bountyPanel);
    bountyInlineHint->setObjectName(QStringLiteral("forumMuted"));
    bountyInlineHint->setWordWrap(true);
    bountyInputRow->addWidget(bountyInlineHint, 1);
    bountyRowLayout->addLayout(bountyInputRow);

    // 快捷金额：和网页版发帖页同一排 5 / 10 / 20 / 50
    auto *bountyChipsRow = new QHBoxLayout();
    bountyChipsRow->setContentsMargins(0, 0, 0, 0);
    bountyChipsRow->setSpacing(8);
    auto *chipsLabel = new QLabel(tr("快捷"), bountyPanel);
    chipsLabel->setObjectName(QStringLiteral("forumMuted"));
    bountyChipsRow->addWidget(chipsLabel);
    for (int amount : { 5, 10, 20, 50 })
    {
        auto *chip = new QPushButton(QString::number(amount), bountyPanel);
        chip->setObjectName(QStringLiteral("forumBountyChip"));
        chip->setCheckable(true);
        chip->setAutoExclusive(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(26);
        chip->setProperty("amount", amount);
        connect(chip, &QPushButton::clicked, this, [this, amount]() {
            if (m_bountySpin)
                m_bountySpin->setValue(amount);
        });
        bountyChipsRow->addWidget(chip);
        m_bountyChips.append(chip);
    }
    bountyChipsRow->addStretch(1);
    bountyRowLayout->addLayout(bountyChipsRow);

    // 默认 10 方块对应的快捷按钮先高亮
    for (QPushButton *chip : m_bountyChips)
        chip->setChecked(chip->property("amount").toInt() == kDefaultBounty);

    m_bountyHintLabel = new QLabel(
        tr("30 天内没采纳，赏金自动发给点赞最多的回答；一层回答都没有则原路退回你的账户"),
        bountyPanel);
    m_bountyHintLabel->setObjectName(QStringLiteral("forumMuted"));
    m_bountyHintLabel->setWordWrap(true);
    bountyRowLayout->addWidget(m_bountyHintLabel);
    cardLayout->addWidget(m_bountyRow);

    connect(m_bountySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        for (QPushButton *chip : m_bountyChips)
        {
            if (chip)
                chip->setChecked(chip->property("amount").toInt() == value);
        }
        if (m_mode == Mode::Question && m_submitButton)
            m_submitButton->setText(tr("发布问答（托管 %1 方块）").arg(value));
    });

    // ── 插件附件卡片（资源模式的「插件」分类才显示） ──
    m_pluginCard = ForumKit::makeCard(formCard);
    auto *pluginLayout = new QHBoxLayout(m_pluginCard);
    pluginLayout->setContentsMargins(12, 10, 12, 10);
    pluginLayout->setSpacing(10);
    m_pluginIconLabel = new QLabel(m_pluginCard);
    m_pluginIconLabel->setFixedSize(40, 40);
    m_pluginIconLabel->setAlignment(Qt::AlignCenter);
    m_pluginIconLabel->setStyleSheet(QStringLiteral(
        "background:@BG_MUTED@;border:1px solid @BORDER@;border-radius:8px;"));
    pluginLayout->addWidget(m_pluginIconLabel);
    auto *pluginTextLayout = new QVBoxLayout();
    pluginTextLayout->setSpacing(2);
    m_pluginNameLabel = new QLabel(m_pluginCard);
    m_pluginNameLabel->setObjectName(QStringLiteral("forumStatus"));
    m_pluginNameLabel->setWordWrap(true);
    pluginTextLayout->addWidget(m_pluginNameLabel);
    m_pluginMetaLabel = new QLabel(m_pluginCard);
    m_pluginMetaLabel->setObjectName(QStringLiteral("forumStatus"));
    m_pluginMetaLabel->setWordWrap(true);
    pluginTextLayout->addWidget(m_pluginMetaLabel);
    m_pluginHintLabel = new QLabel(m_pluginCard);
    m_pluginHintLabel->setObjectName(QStringLiteral("forumStatus"));
    m_pluginHintLabel->setWordWrap(true);
    pluginTextLayout->addWidget(m_pluginHintLabel);
    pluginLayout->addLayout(pluginTextLayout, 1);
    m_pluginRemoveButton = new QPushButton(tr("移除"), m_pluginCard);
    m_pluginRemoveButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_pluginRemoveButton->setCursor(Qt::PointingHandCursor);
    m_pluginRemoveButton->setFixedHeight(26);
    connect(m_pluginRemoveButton, &QPushButton::clicked, this,
            &ForumComposerPage::clearPluginAttachment);
    pluginLayout->addWidget(m_pluginRemoveButton);
    m_pluginAddButton = new QPushButton(tr("添加插件包…"), m_pluginCard);
    m_pluginAddButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_pluginAddButton->setCursor(Qt::PointingHandCursor);
    m_pluginAddButton->setFixedHeight(26);
    connect(m_pluginAddButton, &QPushButton::clicked, this, &ForumComposerPage::pickPluginFile);
    pluginLayout->addWidget(m_pluginAddButton);
    m_pluginCard->setVisible(false);
    cardLayout->addWidget(m_pluginCard);

    // ── 投影附件卡片（资源模式的「投影」分类才显示） ──
    // 同一个建筑常要分维度发：主世界一份、下界一份，还能各自起名。
    m_schematicCard = ForumKit::makeCard(formCard);
    auto *schematicLayout = new QVBoxLayout(m_schematicCard);
    schematicLayout->setContentsMargins(12, 10, 12, 10);
    schematicLayout->setSpacing(8);
    m_schematicRowsLayout = new QVBoxLayout();
    m_schematicRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_schematicRowsLayout->setSpacing(8);
    schematicLayout->addLayout(m_schematicRowsLayout);
    auto *schematicButtonRow = new QHBoxLayout();
    schematicButtonRow->setContentsMargins(0, 0, 0, 0);
    m_schematicHintLabel = new QLabel(m_schematicCard);
    m_schematicHintLabel->setObjectName(QStringLiteral("forumStatus"));
    m_schematicHintLabel->setWordWrap(true);
    schematicButtonRow->addWidget(m_schematicHintLabel, 1);
    m_schematicAddButton = new QPushButton(tr("添加投影文件…"), m_schematicCard);
    m_schematicAddButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_schematicAddButton->setCursor(Qt::PointingHandCursor);
    m_schematicAddButton->setFixedHeight(26);
    m_schematicAddButton->setToolTip(
        tr("可一次选多份：主世界、下界等各选一个，每份都能自己起名，最多 %1 份").arg(kMaxSchematicFiles));
    connect(m_schematicAddButton, &QPushButton::clicked, this,
            &ForumComposerPage::pickSchematicFiles);
    schematicButtonRow->addWidget(m_schematicAddButton);
    schematicLayout->addLayout(schematicButtonRow);
    m_schematicCard->setVisible(false);
    cardLayout->addWidget(m_schematicCard);

    // ── 资料附件卡片（教程模式才显示）：pdf / 压缩包等，一帖最多 10 个 ──
    m_attachmentCard = ForumKit::makeCard(formCard);
    auto *attachmentLayout = new QVBoxLayout(m_attachmentCard);
    attachmentLayout->setContentsMargins(12, 10, 12, 10);
    attachmentLayout->setSpacing(6);
    m_attachmentRowsLayout = new QVBoxLayout();
    m_attachmentRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_attachmentRowsLayout->setSpacing(4);
    attachmentLayout->addLayout(m_attachmentRowsLayout);
    auto *attachmentButtonRow = new QHBoxLayout();
    attachmentButtonRow->setContentsMargins(0, 0, 0, 0);
    m_attachmentHintLabel = new QLabel(m_attachmentCard);
    m_attachmentHintLabel->setObjectName(QStringLiteral("forumStatus"));
    m_attachmentHintLabel->setWordWrap(true);
    attachmentButtonRow->addWidget(m_attachmentHintLabel, 1);
    m_attachmentAddButton = new QPushButton(tr("添加附件…"), m_attachmentCard);
    m_attachmentAddButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_attachmentAddButton->setCursor(Qt::PointingHandCursor);
    m_attachmentAddButton->setFixedHeight(26);
    m_attachmentAddButton->setToolTip(
        tr("支持 PDF、压缩包、TXT / MD / JSON / CSV / 日志等文件，单个最大 50 MB，最多 10 个"));
    connect(m_attachmentAddButton, &QPushButton::clicked, this,
            &ForumComposerPage::pickTutorialAttachments);
    attachmentButtonRow->addWidget(m_attachmentAddButton);
    attachmentLayout->addLayout(attachmentButtonRow);
    m_attachmentCard->setVisible(false);
    cardLayout->addWidget(m_attachmentCard);

    // ── 相关链接行（资源模式才显示）：绑官网 / GitHub 仓库 / 反馈等入口 ──
    m_linksRow = new QWidget(formCard);
    m_linksRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *linksLayout = new QVBoxLayout(m_linksRow);
    linksLayout->setContentsMargins(0, 0, 0, 0);
    linksLayout->setSpacing(6);
    linksLayout->addWidget(ForumKit::makeMuted(m_linksRow, tr("相关链接")));
    m_linksEditor = new ForumLinkRowsEditor(m_linksRow);
    linksLayout->addWidget(m_linksEditor);
    cardLayout->addWidget(m_linksRow);

    // ── 作品来源行（资源模式才显示）：原创 / 转载二选一，转载要交代出处 ──
    m_sourceRow = new QWidget(formCard);
    m_sourceRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *sourceLayout = new QVBoxLayout(m_sourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(6);
    sourceLayout->addWidget(ForumKit::makeMuted(m_sourceRow, tr("作品来源")));

    auto *sourceChipsRow = new QWidget(m_sourceRow);
    sourceChipsRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *sourceChipsLayout = new QHBoxLayout(sourceChipsRow);
    sourceChipsLayout->setContentsMargins(0, 0, 0, 0);
    sourceChipsLayout->setSpacing(8);
    m_originalSourceButton = new QPushButton(tr("原创"), sourceChipsRow);
    m_originalSourceButton->setObjectName(QStringLiteral("forumSortButton"));
    m_originalSourceButton->setCheckable(true);
    m_originalSourceButton->setAutoExclusive(true);
    m_originalSourceButton->setChecked(true);
    m_originalSourceButton->setCursor(Qt::PointingHandCursor);
    m_originalSourceButton->setFixedHeight(26);
    m_originalSourceButton->setToolTip(tr("自己制作的作品"));
    connect(m_originalSourceButton, &QPushButton::clicked, this,
            &ForumComposerPage::updateRepostFields);
    sourceChipsLayout->addWidget(m_originalSourceButton);
    m_repostSourceButton = new QPushButton(tr("转载"), sourceChipsRow);
    m_repostSourceButton->setObjectName(QStringLiteral("forumSortButton"));
    m_repostSourceButton->setCheckable(true);
    m_repostSourceButton->setAutoExclusive(true);
    m_repostSourceButton->setCursor(Qt::PointingHandCursor);
    m_repostSourceButton->setFixedHeight(26);
    m_repostSourceButton->setToolTip(tr("分享别人做的作品：要填原作品链接和转载说明"));
    connect(m_repostSourceButton, &QPushButton::clicked, this,
            &ForumComposerPage::updateRepostFields);
    sourceChipsLayout->addWidget(m_repostSourceButton);
    sourceChipsLayout->addStretch(1);
    sourceLayout->addWidget(sourceChipsRow);

    // 转载的两栏：默认收起，点「转载」才展开
    m_repostFields = new QWidget(m_sourceRow);
    m_repostFields->setAttribute(Qt::WA_StyledBackground, false);
    auto *repostLayout = new QVBoxLayout(m_repostFields);
    repostLayout->setContentsMargins(0, 0, 0, 0);
    repostLayout->setSpacing(6);
    m_repostUrlEdit = new QLineEdit(m_repostFields);
    m_repostUrlEdit->setObjectName(QStringLiteral("forumField"));
    m_repostUrlEdit->setMaxLength(500);
    m_repostUrlEdit->setPlaceholderText(tr("原作品链接（http:// 或 https:// 开头，必填）"));
    repostLayout->addWidget(m_repostUrlEdit);
    m_repostNoteEdit = new QLineEdit(m_repostFields);
    m_repostNoteEdit->setObjectName(QStringLiteral("forumField"));
    m_repostNoteEdit->setMaxLength(300);
    m_repostNoteEdit->setPlaceholderText(tr("转载说明：原作者、来源或授权情况（必填）"));
    repostLayout->addWidget(m_repostNoteEdit);
    m_repostFields->setVisible(false);
    sourceLayout->addWidget(m_repostFields);
    cardLayout->addWidget(m_sourceRow);

    // ── 轮播图卡片（资源 / 导航模式才显示）：图片或 B 站视频，展示在帖子顶部 ──
    m_carouselCard = ForumKit::makeCard(formCard);
    auto *carouselLayout = new QVBoxLayout(m_carouselCard);
    carouselLayout->setContentsMargins(12, 10, 12, 10);
    carouselLayout->setSpacing(6);
    carouselLayout->addWidget(ForumKit::makeMuted(m_carouselCard, tr("轮播图")));
    m_carouselEditor = new ForumCarouselEditor(m_carouselCard);
    carouselLayout->addWidget(m_carouselEditor);
    cardLayout->addWidget(m_carouselCard);

    // ── 封面卡片（教程 / 资源 / 导航模式才显示）：板块瀑布流里的展示图 ──
    // 只上传一张小图，板块列表按封面排瀑布流；配了 CDN 时列表直接向 CDN 取图
    m_coverCard = ForumKit::makeCard(formCard);
    auto *coverLayout = new QVBoxLayout(m_coverCard);
    coverLayout->setContentsMargins(12, 10, 12, 10);
    coverLayout->setSpacing(8);
    coverLayout->addWidget(ForumKit::makeMuted(m_coverCard, tr("封面（板块瀑布流里展示的图）")));

    auto *coverBody = new QHBoxLayout();
    coverBody->setContentsMargins(0, 0, 0, 0);
    coverBody->setSpacing(12);

    m_coverPreviewLabel = new QLabel(m_coverCard);
    m_coverPreviewLabel->setObjectName(QStringLiteral("forumCoverPreview"));
    m_coverPreviewLabel->setFixedSize(192, 108);
    m_coverPreviewLabel->setAlignment(Qt::AlignCenter);
    m_coverPreviewLabel->setText(tr("还没选封面"));
    m_coverPreviewLabel->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QLabel#forumCoverPreview{background:@BG_MUTED@;border:1px solid @BORDER@;"
        "border-radius:10px;color:@TEXT_MUTED@;font-size:12px;}")));
    coverBody->addWidget(m_coverPreviewLabel, 0, Qt::AlignTop);

    auto *coverSide = new QVBoxLayout();
    coverSide->setContentsMargins(0, 0, 0, 0);
    coverSide->setSpacing(6);
    m_coverPickButton = new QPushButton(tr("选择图片…"), m_coverCard);
    m_coverPickButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_coverPickButton->setCursor(Qt::PointingHandCursor);
    m_coverPickButton->setFixedHeight(28);
    connect(m_coverPickButton, &QPushButton::clicked, this, &ForumComposerPage::pickCoverImage);
    coverSide->addWidget(m_coverPickButton, 0, Qt::AlignLeft);
    m_coverClearButton = new QPushButton(tr("移除封面"), m_coverCard);
    m_coverClearButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_coverClearButton->setCursor(Qt::PointingHandCursor);
    m_coverClearButton->setFixedHeight(28);
    m_coverClearButton->setVisible(false);
    connect(m_coverClearButton, &QPushButton::clicked, this, &ForumComposerPage::clearCover);
    coverSide->addWidget(m_coverClearButton, 0, Qt::AlignLeft);
    m_coverHintLabel = ForumKit::makeMuted(
        m_coverCard, tr("建议 16:9 横图。会自动压成小图走 CDN，不占服务器流量；不选就显示类型渐变封面。"));
    coverSide->addWidget(m_coverHintLabel, 1);
    coverBody->addLayout(coverSide, 1);
    coverLayout->addLayout(coverBody);
    cardLayout->addWidget(m_coverCard);

    // 标题
    cardLayout->addWidget(ForumKit::makeMuted(formCard, tr("标题（2–80 字，写清现象更容易被解答）")));
    m_titleEdit = new QLineEdit(formCard);
    m_titleEdit->setObjectName(QStringLiteral("forumField"));
    m_titleEdit->setMaxLength(kTitleLimit);
    m_titleEdit->setPlaceholderText(tr("例如：1.20.4 启动报 Java 找不到"));
    cardLayout->addWidget(m_titleEdit);

    // 标签：胶囊式输入，支持推荐标签和常用标签一键添加
    m_tagsEditor = new ForumTagEditor(formCard);
    m_tagsEditor->setMaxTags(kMaxTags);
    m_tagsEditor->setMaxTagLength(kMaxTagLength);
    cardLayout->addWidget(m_tagsEditor);

    // 正文（编辑框先建出来，工具栏的格式按钮都作用在它上面）
    cardLayout->addWidget(ForumKit::makeMuted(formCard, tr("正文（支持 Markdown，可粘贴图片链接）")));

    m_contentStack = new QStackedWidget(formCard);
    m_contentEdit = new QPlainTextEdit(formCard);
    m_contentEdit->setObjectName(QStringLiteral("forumReplyEdit"));
    m_contentEdit->setPlaceholderText(tr("正文支持 Markdown：**加粗**、`代码`、> 引用、![](图片链接)"));
    m_contentEdit->setMinimumHeight(230);
    m_contentStack->addWidget(m_contentEdit);
    m_previewBrowser = new ForumPostBrowser(formCard);
    m_previewBrowser->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QTextBrowser{background:@BG_MUTED@;border:1px solid @BORDER@;"
        "border-radius:10px;padding:10px 12px;}")));
    m_contentStack->addWidget(m_previewBrowser);

    // ── Markdown 工具栏：与官网发帖框同款，格式按钮 + 预设 + 编辑/预览切换 ──
    auto *toolbar = new QWidget(formCard);
    toolbar->setAttribute(Qt::WA_StyledBackground, false);
    auto *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->setSpacing(2);
    auto addMdButton = [this, toolbar, toolLayout](const QString &text, const QString &tip,
                                                   const QString &command) {
        auto *button = new QPushButton(text, toolbar);
        button->setObjectName(QStringLiteral("forumMdButton"));
        button->setToolTip(tip);
        button->setFixedHeight(26);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, toolbar,
                [this, command]() { runMdCommand(command); });
        toolLayout->addWidget(button);
        return button;
    };

    auto *boldButton = addMdButton(QStringLiteral("B"), tr("加粗（Ctrl+B）"), QStringLiteral("bold"));
    QFont boldFont = boldButton->font();
    boldFont.setBold(true);
    boldButton->setFont(boldFont);
    auto *italicButton = addMdButton(QStringLiteral("I"), tr("斜体（Ctrl+I）"), QStringLiteral("italic"));
    QFont italicFont = italicButton->font();
    italicFont.setItalic(true);
    italicButton->setFont(italicFont);
    auto *strikeButton = addMdButton(QStringLiteral("S"), tr("删除线"), QStringLiteral("strike"));
    QFont strikeFont = strikeButton->font();
    strikeFont.setStrikeOut(true);
    strikeButton->setFont(strikeFont);
    addMdButton(QStringLiteral("H"), tr("标题"), QStringLiteral("heading"));
    addMdButton(QStringLiteral("❝"), tr("引用"), QStringLiteral("quote"));

    QFont monoFont;
    monoFont.setFamily(QStringLiteral("Consolas"));
    monoFont.setStyleHint(QFont::Monospace);
    auto *inlineCodeButton =
        addMdButton(QStringLiteral("</>"), tr("行内代码"), QStringLiteral("code"));
    inlineCodeButton->setFont(monoFont);
    auto *codeBlockButton =
        addMdButton(QStringLiteral("{}"), tr("代码块"), QStringLiteral("codeblock"));
    codeBlockButton->setFont(monoFont);

    addMdButton(QStringLiteral("• 列表"), tr("无序列表"), QStringLiteral("ul"));
    addMdButton(QStringLiteral("1. 编号"), tr("有序列表"), QStringLiteral("ol"));
    addMdButton(tr("链接"), tr("插入链接（Ctrl+K）"), QStringLiteral("link"));
    m_imageButton = addMdButton(tr("图片"), tr("上传图片到论坛图床，插进正文"), QStringLiteral("image"));
    addMdButton(tr("▶ 折叠"), tr("插入折叠块：点标题展开查看的内容"), QStringLiteral("fold"));
    addMdButton(tr("表格"), tr("插入表格"), QStringLiteral("table"));
    addMdButton(tr("导图"), tr("插入思维导图（缩进列表排成树）"), QStringLiteral("mindmap"));

    toolLayout->addSpacing(6);
    m_presetButton = new QPushButton(tr("预设…"), toolbar);
    m_presetButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_presetButton->setCursor(Qt::PointingHandCursor);
    m_presetButton->setFixedHeight(26);
    m_presetButton->setToolTip(tr("插入常用的 Markdown 正文模板"));
    connect(m_presetButton, &QPushButton::clicked, this, &ForumComposerPage::showPresetMenu);
    toolLayout->addWidget(m_presetButton);
    toolLayout->addStretch(1);

    // 编辑 / 预览分段切换：预览态把正文渲染成发出去之后的真实排版
    m_editModeButton = new QPushButton(tr("编辑"), toolbar);
    m_editModeButton->setObjectName(QStringLiteral("forumSortButton"));
    m_editModeButton->setCheckable(true);
    m_editModeButton->setAutoExclusive(true);
    m_editModeButton->setChecked(true);
    m_editModeButton->setCursor(Qt::PointingHandCursor);
    m_editModeButton->setFixedHeight(26);
    connect(m_editModeButton, &QPushButton::clicked, this, [this]() { setPreviewMode(false); });
    toolLayout->addWidget(m_editModeButton);
    m_previewModeButton = new QPushButton(tr("预览"), toolbar);
    m_previewModeButton->setObjectName(QStringLiteral("forumSortButton"));
    m_previewModeButton->setCheckable(true);
    m_previewModeButton->setAutoExclusive(true);
    m_previewModeButton->setCursor(Qt::PointingHandCursor);
    m_previewModeButton->setFixedHeight(26);
    connect(m_previewModeButton, &QPushButton::clicked, this, [this]() { setPreviewMode(true); });
    toolLayout->addWidget(m_previewModeButton);
    cardLayout->addWidget(toolbar);

    m_contentStack->setCurrentIndex(0);
    cardLayout->addWidget(m_contentStack);

    // 常用格式快捷键，与网页版一致；只在正文框内生效，别抢标题 / 标签框的输入
    auto *boldShortcut = new QShortcut(QKeySequence::Bold, m_contentEdit);
    boldShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(boldShortcut, &QShortcut::activated, this,
            [this]() { runMdCommand(QStringLiteral("bold")); });
    auto *italicShortcut = new QShortcut(QKeySequence::Italic, m_contentEdit);
    italicShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(italicShortcut, &QShortcut::activated, this,
            [this]() { runMdCommand(QStringLiteral("italic")); });
    auto *linkShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), m_contentEdit);
    linkShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(linkShortcut, &QShortcut::activated, this,
            [this]() { runMdCommand(QStringLiteral("link")); });

    formLayout->addWidget(formCard);

    // 提交行
    auto *submitCard = ForumKit::makeCard(m_formHost);
    auto *submitLayout = new QHBoxLayout(submitCard);
    submitLayout->setContentsMargins(20, 12, 20, 12);
    submitLayout->setSpacing(10);
    m_statusLabel = new QLabel(submitCard);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    submitLayout->addWidget(m_statusLabel, 1);
    m_submitButton = new QPushButton(tr("发布主题"), submitCard);
    m_submitButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_submitButton->setCursor(Qt::PointingHandCursor);
    m_submitButton->setFixedHeight(34);
    m_submitButton->setMinimumWidth(120);
    connect(m_submitButton, &QPushButton::clicked, this, &ForumComposerPage::submit);
    submitLayout->addWidget(m_submitButton);
    formLayout->addWidget(submitCard);

    formLayout->addStretch(1);
    layout->addWidget(m_formHost);

    scroll->setWidget(content);
    outer->addWidget(scroll);

    // 初始按「帖子」模式摆好：板块行可见、分类行与插件卡隐藏
    applyMode();
}

void ForumComposerPage::updateState()
{
    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool quizPassed = loggedIn && account->hasPassedQuiz();
    m_formHost->setVisible(quizPassed);
    m_quizHost->setVisible(loggedIn && !quizPassed);
    m_loginHost->setVisible(!loggedIn);

    // 登录 / 答题状态变化后才可能出现表单；此时补拉板块，
    // 避免从发布中心直接进来（未登录 → 登录返回）时板块下拉框为空
    if (quizPassed && !m_boardsLoaded && m_mode == Mode::Post)
        loadBoards();

    // refreshProfile 回来时顺手把赏金卡上的余额刷成最新值
    if (m_mode == Mode::Question && m_bountyBalanceLabel && loggedIn)
        m_bountyBalanceLabel->setText(tr("余额 %1 方块").arg(account->user().points));
}

void ForumComposerPage::openQuizDialog()
{
    QWidget *host = window();
    QuizDialog dialog(host);
    connect(&dialog, &QuizDialog::loginRequested, this, &ForumComposerPage::loginRequested);
    dialog.exec();
    // 通过后 reportQuizPass 会发 userChanged 带动 updateState；这里再刷一次兜底
    updateState();
}

void ForumComposerPage::startNewThread(const QString &preselectSlug)
{
    m_preselectSlug = preselectSlug;
    m_mode = Mode::Post;
    resetForm();
    applyMode();
    updateState();

    if (m_formHost->isVisible())
    {
        // 已登录进来时 updateState 会补拉板块；这里只负责再次套用预选板块
        if (m_boardsLoaded)
            loadBoards();
        m_titleEdit->setFocus();
    }
}

void ForumComposerPage::startNewResource(const QString &kindSlug)
{
    m_preselectSlug.clear();
    m_kindSlug = kindSlug.isEmpty() ? QStringLiteral("plugin") : kindSlug;
    m_mode = Mode::Resource;
    resetForm();
    applyMode();
    updateState();

    if (m_formHost->isVisible())
        m_titleEdit->setFocus();
}

void ForumComposerPage::startNewTutorial()
{
    m_preselectSlug.clear();
    m_mode = Mode::Tutorial;
    resetForm();
    applyMode();
    updateState();

    if (m_formHost->isVisible())
        m_titleEdit->setFocus();
}

void ForumComposerPage::startNewQuestion()
{
    m_preselectSlug.clear();
    m_mode = Mode::Question;
    resetForm();
    applyMode();
    updateState();

    // 余额以服务端为准：发过赏金之后本地缓存的余额会偏大，先拉一次最新的
    ForumAccountService::instance()->refreshProfile();

    if (m_formHost->isVisible())
        m_titleEdit->setFocus();
}

void ForumComposerPage::startNewNavigation()
{
    m_preselectSlug.clear();
    m_mode = Mode::Navigation;
    resetForm();
    applyMode();
    updateState();

    if (m_formHost->isVisible())
    {
        // 网址是导航帖的主角，进来先落到网址输入框上
        if (m_navUrlEdit)
            m_navUrlEdit->setFocus();
        else if (m_titleEdit)
            m_titleEdit->setFocus();
    }
}

void ForumComposerPage::startNewNavigation(const QString &kindSlug, const QString &url,
                                           const QString &title, const QString &content,
                                           const QStringList &tags)
{
    // 先切到导航模式（resetForm 会把类型复位成网站），再按调用方给的预填
    startNewNavigation();

    m_navKind = (kindSlug == QLatin1String("website")) ? QStringLiteral("website")
                                                       : QStringLiteral("server");
    // 类型胶囊的选中态与网址提示跟着类型走
    for (QPushButton *chip : m_navTypeButtons)
    {
        if (chip)
            chip->setChecked(chip->property("kindSlug").toString() == m_navKind);
    }
    if (m_navUrlEdit)
    {
        m_navUrlEdit->setText(url);
        m_navUrlEdit->setPlaceholderText(
            m_navKind == QLatin1String("server")
                ? tr("填别人能连上的地址，如 play.example.com:25565 或 1.2.3.4:25565")
                : tr("https://example.com"));
    }
    if (m_titleEdit && !title.isEmpty())
        m_titleEdit->setText(title);
    if (m_contentEdit && !content.isEmpty())
        m_contentEdit->setPlainText(content);
    if (m_tagsEditor && !tags.isEmpty())
        m_tagsEditor->setTags(tags);

    // 预填好之后把焦点落到提交按钮上，用户看一眼就能发
    if (m_formHost->isVisible() && m_submitButton)
        m_submitButton->setFocus();
}

void ForumComposerPage::setMode(Mode mode)
{
    m_mode = mode;
    // 从资源切回帖子时才需要板块列表；资源 / 教程 / 问答 / 导航模式不看板块
    if (m_mode == Mode::Post && !m_boardsLoaded)
        loadBoards();
    // 进问答模式顺手刷新一次账户资料，赏金卡上显示的余额才不会是上次登录时的旧数
    if (m_mode == Mode::Question)
        ForumAccountService::instance()->refreshProfile();
    applyMode();
}

void ForumComposerPage::applyMode()
{
    const bool resourceMode = (m_mode == Mode::Resource);
    const bool tutorialMode = (m_mode == Mode::Tutorial);
    const bool questionMode = (m_mode == Mode::Question);
    const bool navigationMode = (m_mode == Mode::Navigation);

    applyTagRecommendations();

    if (m_postModeButton)
        m_postModeButton->setChecked(m_mode == Mode::Post);
    if (m_resourceModeButton)
        m_resourceModeButton->setChecked(resourceMode);
    if (m_tutorialModeButton)
        m_tutorialModeButton->setChecked(tutorialMode);
    if (m_questionModeButton)
        m_questionModeButton->setChecked(questionMode);
    if (m_navigationModeButton)
        m_navigationModeButton->setChecked(navigationMode);

    if (m_boardRow)
        m_boardRow->setVisible(!resourceMode && !tutorialMode && !questionMode && !navigationMode);
    if (m_kindRow)
        m_kindRow->setVisible(resourceMode);
    if (m_sourceRow)
        m_sourceRow->setVisible(resourceMode);
    if (m_navRow)
        m_navRow->setVisible(navigationMode);
    for (QPushButton *chip : m_kindButtons)
    {
        if (chip)
            chip->setChecked(chip->property("kindSlug").toString() == m_kindSlug);
    }
    for (QPushButton *chip : m_navTypeButtons)
    {
        if (chip)
            chip->setChecked(chip->property("kindSlug").toString() == m_navKind);
    }

    // 网址输入框的占位提示按类型换：网站补协议，服务器填地址就行
    if (m_navUrlEdit)
    {
        m_navUrlEdit->setPlaceholderText(
            m_navKind == QLatin1String("server")
                ? tr("服务器地址，例如 mc.example.com 或 123.45.67.89:25565")
                : tr("网址，例如 https://www.example.com"));
    }

    const bool pluginKind = m_kindSlug == QLatin1String("plugin");
    const bool schematicKind = m_kindSlug == QLatin1String("schematic");
    if (m_pluginCard)
        m_pluginCard->setVisible(resourceMode && pluginKind);
    if (m_schematicCard)
        m_schematicCard->setVisible(resourceMode && schematicKind);
    if (m_attachmentCard)
        m_attachmentCard->setVisible(tutorialMode);
    if (m_linksRow)
        m_linksRow->setVisible(resourceMode);
    if (m_carouselCard)
        m_carouselCard->setVisible(resourceMode || navigationMode);
    // 封面：教程 / 资源 / 导航这三类内容才有瀑布流封面
    if (m_coverCard)
        m_coverCard->setVisible(tutorialMode || resourceMode || navigationMode);
    if (m_bountyRow)
        m_bountyRow->setVisible(questionMode);

    // 赏金卡里的余额：问答模式才刷新，省得每次切 Tab 都查一次
    if (questionMode && m_bountyBalanceLabel)
    {
        const int balance = ForumAccountService::instance()->user().points;
        m_bountyBalanceLabel->setText(tr("余额 %1 方块").arg(balance));
    }

    if (m_formTitleLabel)
    {
        m_formTitleLabel->setText(questionMode ? tr("发布有奖问答")
                                   : tutorialMode ? tr("发布教程")
                                   : navigationMode ? tr("发布导航")
                                   : (resourceMode ? tr("发布资源") : tr("发表新主题")));
    }
    if (m_submitButton)
    {
        m_submitButton->setText(questionMode
                                    ? tr("发布问答（托管 %1 方块）").arg(m_bountySpin ? m_bountySpin->value() : kDefaultBounty)
                                : tutorialMode ? tr("发布教程")
                                : navigationMode ? tr("发布导航")
                                : (resourceMode ? (pluginKind ? tr("发布插件") : tr("发布投影"))
                                                : tr("发布主题")));
    }
    if (m_titleEdit)
    {
        m_titleEdit->setPlaceholderText(
            questionMode ? tr("例如：1.20.4 启动报 Java 找不到，怎么解决？")
            : tutorialMode ? tr("例如：低配电脑流畅跑 1.20 的完整设置教程")
            : navigationMode ? (m_navKind == QLatin1String("server")
                                    ? tr("例如：XYZ 生存服，肝了一天值得推荐")
                                    : tr("例如：一个查合成表的好用网站"))
            : resourceMode ? (pluginKind ? tr("例如：自动整理模组的辅助插件")
                                         : tr("例如：中世纪风格的小镇投影"))
                           : tr("例如：1.20.4 启动报 Java 找不到"));
    }

    // 资源模式下插件包是必填的，没选包时先把要求写在卡片上
    if (m_pluginHintLabel && resourceMode && !m_pluginUploading && !m_pluginUpload.isValid()
        && m_pluginHintLabel->text().isEmpty())
    {
        m_pluginHintLabel->setText(tr("发布插件必须带一个插件包（.BlockBox，最大 20 MB）"));
    }
    // 投影同理：还没添加文件时先把要求写在卡片上
    if (m_schematicHintLabel && resourceMode && m_schematics.isEmpty()
        && m_schematicHintLabel->text().isEmpty())
    {
        m_schematicHintLabel->setText(
            tr("发布投影必须至少带一份投影文件（.litematic / .schem 等，单份最大 50 MB）；"
               "主世界、下界等可以一次挂多份，各自命名"));
    }
    // 教程模式下提示附件容量口径（附件可选，不强制）
    if (m_attachmentHintLabel && tutorialMode && m_attachmentHintLabel->text().isEmpty())
    {
        m_attachmentHintLabel->setText(
            tr("可选：教程里用到的资料包、示例文件都能挂上（单个最大 50 MB，最多 10 个）"));
    }
}

/** 按当前来源胶囊展开或收起转载两栏：选「转载」才需要填原链接和说明 */
void ForumComposerPage::updateRepostFields()
{
    if (!m_repostFields)
        return;
    const bool repost = m_repostSourceButton && m_repostSourceButton->isChecked();
    m_repostFields->setVisible(repost);
    if (repost)
    {
        if (m_repostUrlEdit->text().trimmed().isEmpty())
            m_repostUrlEdit->setFocus();
        else
            m_repostNoteEdit->setFocus();
    }
}

void ForumComposerPage::resetForm()
{
    m_titleEdit->clear();
    m_tagsEditor->clear();
    m_contentEdit->clear();
    m_previewBrowser->setPostMarkdown(QString());
    if (m_previewModeButton->isChecked())
        m_previewModeButton->setChecked(false);
    m_statusLabel->clear();
    m_statusLabel->setStyleSheet(QString());
    clearPluginAttachment();
    clearSchematicAttachment();
    m_attachments.clear();
    refreshTutorialAttachmentRows();
    if (m_attachmentHintLabel)
        m_attachmentHintLabel->clear();
    if (m_linksEditor)
        m_linksEditor->setLinks({});
    if (m_carouselEditor)
        m_carouselEditor->clear();
    clearCover();
    // 来源回到默认「原创」，转载两栏清空收起
    if (m_originalSourceButton)
        m_originalSourceButton->setChecked(true);
    if (m_repostUrlEdit)
        m_repostUrlEdit->clear();
    if (m_repostNoteEdit)
        m_repostNoteEdit->clear();
    if (m_repostFields)
        m_repostFields->setVisible(false);
    // 导航回到默认「网站」类型，网址清空
    m_navKind = QStringLiteral("website");
    if (m_navUrlEdit)
        m_navUrlEdit->clear();
    if (m_bountySpin)
        m_bountySpin->setValue(kDefaultBounty);
    if (m_bountyBalanceLabel)
        m_bountyBalanceLabel->clear();
}

void ForumComposerPage::loadBoards()
{
    m_boardCombo->clear();
    m_boardCombo->addItem(tr("正在加载板块…"));

    ForumContentService::instance()->fetchBoards(
        [this](const QVector<ForumBoard> &boards) {
            m_boards = boards;
            m_boardCombo->clear();
            int selectIndex = -1;
            for (const ForumBoard &board : boards)
            {
                // 「插件分享」「投影分享」是资源→插件/投影专用板块，「教程分享」是教程模式、
                // 「有奖问答」是问答模式、「网站导航 / 服务器导航」是导航模式的专用板块：
                // 帖子里都不提供（发帖只做讨论）
                if (board.slug == QLatin1String(kPluginShareBoardSlug)
                    || board.slug == QLatin1String(kSchematicShareBoardSlug)
                    || board.slug == QLatin1String(kTutorialShareBoardSlug)
                    || board.slug == QLatin1String(kQuestionBoardSlug)
                    || board.slug == QLatin1String(kWebsiteNavBoardSlug)
                    || board.slug == QLatin1String(kServerNavBoardSlug))
                    continue;
                m_boardCombo->addItem(
                    QStringLiteral("%1 %2").arg(board.icon.isEmpty() ? QStringLiteral("💬")
                                                                     : board.icon,
                                               board.name),
                    board.slug);
                if (board.slug == m_preselectSlug)
                    selectIndex = m_boardCombo->count() - 1;
            }
            if (m_boardCombo->count() == 0)
                m_boardCombo->addItem(tr("综合讨论"), QStringLiteral("general"));
            if (selectIndex >= 0)
                m_boardCombo->setCurrentIndex(selectIndex);
            m_boardsLoaded = true;
        },
        [this](const QString &message, int) {
            m_boardCombo->clear();
            m_boardCombo->addItem(tr("板块加载失败，默认发到「综合讨论」"), QStringLiteral("general"));
            m_statusLabel->setText(message);
            m_statusLabel->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;")));
        });
}

void ForumComposerPage::loadTagSuggestions()
{
    // 已经拉过就直接用缓存；正在拉 / 拉失败也不重复请求
    if (s_popularTagsRequested)
    {
        if (m_tagsEditor)
            m_tagsEditor->setPopularTags(s_popularTags);
        return;
    }
    s_popularTagsRequested = true;

    const QPointer<ForumComposerPage> self(this);
    ForumContentService::instance()->fetchTags(
        [self](const QVector<ForumTagInfo> &tags) {
            s_popularTags.clear();
            for (const ForumTagInfo &info : tags)
                s_popularTags.append(qMakePair(info.tag, info.count));
            if (self && self->m_tagsEditor)
                self->m_tagsEditor->setPopularTags(s_popularTags);
        },
        [](const QString &, int) {
            // 热门标签只是锦上添花：失败时不缓存「已拉过」，下次打开发布页再试
            s_popularTagsRequested = false;
        });
}

void ForumComposerPage::applyTagRecommendations()
{
    if (!m_tagsEditor)
        return;

    // 标签是论坛里的数据，不跟界面语言翻译，避免同一种标签被拆成多份
    QStringList recommended;
    if (m_mode == Mode::Tutorial)
    {
        recommended = { QStringLiteral("教程"), QStringLiteral("新手"), QStringLiteral("图文"),
                        QStringLiteral("整合包") };
    }
    else if (m_mode == Mode::Resource)
    {
        if (m_kindSlug == QLatin1String("schematic"))
            recommended = { QStringLiteral("投影"), QStringLiteral("建筑"),
                            QStringLiteral("生存"), QStringLiteral("地图") };
        else
            recommended = { QStringLiteral("插件"), QStringLiteral("辅助"),
                            QStringLiteral("工具"), QStringLiteral("优化") };
    }
    else if (m_mode == Mode::Question)
    {
        recommended = { QStringLiteral("求助"), QStringLiteral("悬赏"),
                        QStringLiteral("报错"), QStringLiteral("整合包") };
    }
    else if (m_mode == Mode::Navigation)
    {
        if (m_navKind == QLatin1String("server"))
            recommended = { QStringLiteral("服务器"), QStringLiteral("联机"),
                            QStringLiteral("生存"), QStringLiteral("推荐") };
        else
            recommended = { QStringLiteral("网站"), QStringLiteral("工具"),
                            QStringLiteral("资源"), QStringLiteral("推荐") };
    }
    else
    {
        recommended = { QStringLiteral("求助"), QStringLiteral("联机"),
                        QStringLiteral("整合包"), QStringLiteral("客户端") };
    }
    m_tagsEditor->setRecommendedTags(recommended);
}

void ForumComposerPage::showPresetMenu()
{
    if (!m_presetButton)
        return;

    // 教程模式用教程模板（图文 / 视频 / 攻略合集），帖子模式用通用模板
    const QVector<ForumKit::MarkdownPreset> presets =
        m_mode == Mode::Tutorial ? ForumKit::tutorialPresets() : ForumKit::postPresets();
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const ForumKit::MarkdownPreset &preset : presets)
    {
        menu->addAction(preset.label, this, [this, text = preset.text]() {
            // 插模板时先切回编辑态，不然用户看不到刚插进去的内容
            if (m_previewModeButton->isChecked())
                m_previewModeButton->setChecked(false);
            ForumKit::applyMarkdownPreset(m_contentEdit, text);
        });
    }
    menu->popup(m_presetButton->mapToGlobal(
        QPoint(0, m_presetButton->height() + 2)));
}

void ForumComposerPage::setPreviewMode(bool preview)
{
    if (preview)
        m_previewBrowser->setPostMarkdown(m_contentEdit->toPlainText());

    // 分段按钮跟状态走：预设 / 插图这些程序内切换也要同步高亮
    {
        QSignalBlocker editBlocker(m_editModeButton);
        QSignalBlocker previewBlocker(m_previewModeButton);
        m_editModeButton->setChecked(!preview);
        m_previewModeButton->setChecked(preview);
    }
    m_contentStack->setCurrentIndex(preview ? 1 : 0);
    if (!preview)
        m_contentEdit->setFocus();
}

void ForumComposerPage::runMdCommand(const QString &command)
{
    if (command == QLatin1String("bold"))
        ForumKit::mdWrapSelection(m_contentEdit, QStringLiteral("**"), tr("加粗文字"));
    else if (command == QLatin1String("italic"))
        ForumKit::mdWrapSelection(m_contentEdit, QStringLiteral("*"), tr("斜体文字"));
    else if (command == QLatin1String("strike"))
        ForumKit::mdWrapSelection(m_contentEdit, QStringLiteral("~~"), tr("删除文字"));
    else if (command == QLatin1String("heading"))
        ForumKit::mdToggleLines(m_contentEdit, QStringLiteral("heading"));
    else if (command == QLatin1String("quote"))
        ForumKit::mdToggleLines(m_contentEdit, QStringLiteral("quote"));
    else if (command == QLatin1String("code"))
        ForumKit::mdWrapSelection(m_contentEdit, QStringLiteral("`"), tr("代码"));
    else if (command == QLatin1String("codeblock"))
        ForumKit::mdWrapCodeBlock(m_contentEdit);
    else if (command == QLatin1String("ul"))
        ForumKit::mdToggleLines(m_contentEdit, QStringLiteral("ul"));
    else if (command == QLatin1String("ol"))
        ForumKit::mdToggleLines(m_contentEdit, QStringLiteral("ol"));
    else if (command == QLatin1String("link"))
        ForumKit::mdInsertLink(m_contentEdit);
    else if (command == QLatin1String("image"))
        pickImageToUpload();
    else if (command == QLatin1String("fold"))
        ForumKit::mdInsertText(m_contentEdit, QStringLiteral(
            "\n::: fold 标题\n点标题展开查看的内容\n:::\n"));
    else if (command == QLatin1String("table"))
        ForumKit::mdInsertText(m_contentEdit, QStringLiteral(
            "\n| 表头 1 | 表头 2 |\n| --- | --- |\n| 内容 | 内容 |\n"));
    else if (command == QLatin1String("mindmap"))
        ForumKit::mdInsertText(m_contentEdit, QStringLiteral(
            "\n```mindmap\n- 中心主题\n  - 分支 1\n  - 分支 2\n```\n"));
}

void ForumComposerPage::pickImageToUpload()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif)"));
    if (path.isEmpty())
        return;

    // 论坛图床限 4 MB，超了直接提醒，免得白等一次上传
    if (QFileInfo(path).size() > 4 * 1024 * 1024)
    {
        NotificationManager::showInfo(this, tr("图片超过 4 MB，先压缩一下再传"));
        return;
    }

    m_imageButton->setEnabled(false);
    m_statusLabel->setText(tr("正在上传图片…"));
    m_statusLabel->setStyleSheet(QString());
    ForumLobbyService::instance()->uploadImage(
        path,
        [this, path](const QString &url, const QString &message) {
            m_imageButton->setEnabled(true);
            m_statusLabel->clear();
            if (url.isEmpty())
            {
                NotificationManager::showInfo(
                    this, message.isEmpty() ? tr("图片没传上去，稍后再试一次") : message);
                return;
            }
            // 插图前先切回编辑态，不然看不到刚插进来的链接
            if (m_previewModeButton->isChecked())
                m_previewModeButton->setChecked(false);
            ForumKit::mdInsertText(
                m_contentEdit,
                QStringLiteral("\n![%1](%2)\n").arg(QFileInfo(path).fileName(), url));
            NotificationManager::showSuccess(this, tr("图片已插入正文"));
        });
}

void ForumComposerPage::pickCoverImage()
{
    if (m_coverUploading)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择封面"), QString(), tr("图片 (*.png *.jpg *.jpeg *.webp *.gif)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).size() > 4 * 1024 * 1024)
    {
        NotificationManager::showInfo(this, tr("封面超过 4 MB，先压缩一下再传"));
        return;
    }

    m_coverUploading = true;
    if (m_coverPickButton)
        m_coverPickButton->setEnabled(false);
    // 先拿本机图片顶上做预览，传好之后再换成图床地址
    const QPixmap local(path);
    if (!local.isNull() && m_coverPreviewLabel)
    {
        m_coverPreviewLabel->setText(QString());
        m_coverPreviewLabel->setPixmap(MasonryContentCard::makeCoverPixmap(local, 190, 106));
    }
    if (m_coverHintLabel)
        m_coverHintLabel->setText(tr("封面上传中…"));

    ForumLobbyService::instance()->uploadImageFull(
        path, [this](const ForumImageUpload &upload, const QString &message) {
            m_coverUploading = false;
            if (m_coverPickButton)
                m_coverPickButton->setEnabled(true);
            if (!upload.isValid())
            {
                NotificationManager::showInfo(
                    this, message.isEmpty() ? tr("封面没传上去，稍后再试一次") : message);
                if (m_coverHintLabel)
                    m_coverHintLabel->setText(
                        tr("建议 16:9 横图。会自动压成小图走 CDN，不占服务器流量；不选就显示类型渐变封面。"));
                refreshCoverPreview();
                return;
            }
            m_coverUpload = upload;
            if (m_coverHintLabel)
                m_coverHintLabel->setText(tr("封面已就绪：瀑布流只加载这张小图"));
            refreshCoverPreview();
        });
}

void ForumComposerPage::clearCover()
{
    m_coverUpload = ForumImageUpload();
    m_coverUploading = false;
    if (m_coverPickButton)
        m_coverPickButton->setEnabled(true);
    if (m_coverHintLabel)
        m_coverHintLabel->setText(
            tr("建议 16:9 横图。会自动压成小图走 CDN，不占服务器流量；不选就显示类型渐变封面。"));
    refreshCoverPreview();
}

void ForumComposerPage::refreshCoverPreview()
{
    if (!m_coverPreviewLabel)
        return;
    if (!m_coverUpload.isValid())
    {
        m_coverPreviewLabel->setPixmap(QPixmap());
        m_coverPreviewLabel->setText(tr("还没选封面"));
        if (m_coverClearButton)
            m_coverClearButton->setVisible(false);
        return;
    }
    if (m_coverClearButton)
        m_coverClearButton->setVisible(true);
    const QString path = m_coverUpload.thumb.isEmpty() ? m_coverUpload.url : m_coverUpload.thumb;
    const QString url = ForumAccountService::instance()->absoluteUrl(path);
    QPointer<QLabel> guard(m_coverPreviewLabel);
    ForumImageStore::instance()->requestImage(url, [guard](const QImage &image) {
        if (guard.isNull() || image.isNull())
            return;
        guard->setText(QString());
        guard->setPixmap(MasonryContentCard::makeCoverPixmap(QPixmap::fromImage(image), 190, 106));
    });
}

void ForumComposerPage::clearPluginAttachment()
{
    m_pluginFilePath.clear();
    m_pluginUpload = ForumPluginUpload();
    m_pluginUploading = false;
    m_pluginIconLabel->setPixmap(QPixmap());
    m_pluginIconLabel->setText(QString());
    m_pluginNameLabel->clear();
    m_pluginMetaLabel->clear();
    m_pluginHintLabel->clear();
    m_pluginAddButton->setEnabled(true);
    applyMode();
}

void ForumComposerPage::pickPluginFile()
{
    if (m_pluginUploading)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择插件包"), QString(), QStringLiteral("BlockBox 插件 (*.BlockBox *.blockbox)"));
    if (path.isEmpty())
        return;

    const QFileInfo info(path);
    if (info.size() > kMaxPluginBytes)
    {
        NotificationManager::showInfo(
            this, tr("插件包超过 20 MB，服务器不收这么大，先删减一下"));
        return;
    }

    // 本地先解析一遍：清单不合法的发帖也发不成，早些拦下来
    const PluginInfo localInfo = PluginManager::instance()->inspectPluginFile(path);
    if (!localInfo.loaded || localInfo.id.isEmpty() || localInfo.name.isEmpty())
    {
        NotificationManager::showInfo(this, tr("这不是有效的插件包：%1").arg(
            localInfo.loadError.isEmpty() ? tr("清单缺少 id 或 name") : localInfo.loadError));
        return;
    }

    clearPluginAttachment();
    m_pluginFilePath = path;
    m_pluginUploading = true;
    m_pluginNameLabel->setText(tr("%1（正在上传…）").arg(localInfo.name));
    m_pluginMetaLabel->setText(ForumKit::humanFileSize(info.size()));
    m_pluginHintLabel->setText(tr("上传完成后才能发布"));
    m_pluginAddButton->setEnabled(false);

    if (!localInfo.icon.isEmpty())
    {
        // 本地预览用包内图标（缓存到插件目录 .icons/，拿不到也不阻塞）
        const QString iconPath = PluginManager::instance()->pluginIconPath(localInfo);
        QPixmap icon(iconPath);
        if (!icon.isNull())
            m_pluginIconLabel->setPixmap(icon.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    ForumContentService::instance()->uploadPluginPackage(
        path,
        [this](int percent) {
            m_pluginMetaLabel->setText(tr("正在上传 %1%").arg(percent));
        },
        [this](const ForumPluginUpload &upload) {
            m_pluginUploading = false;
            m_pluginUpload = upload;
            m_pluginAddButton->setEnabled(true);
            m_pluginNameLabel->setText(upload.name);
            m_pluginMetaLabel->setText(tr("v%1 · %2 · %3")
                                           .arg(upload.version.isEmpty() ? QStringLiteral("1.0") : upload.version,
                                                upload.author.isEmpty() ? tr("未知作者") : upload.author,
                                                ForumKit::humanFileSize(upload.fileSize)));
            m_pluginHintLabel->setText(tr("我将随主题一起发布这个插件"));
        },
        [this](const QString &message, int httpStatus) {
            m_pluginUploading = false;
            m_pluginAddButton->setEnabled(true);
            const bool wasSessionProblem =
                ForumAccountService::instance()->needsHumanCheck() ||
                (httpStatus == 401);
            if (wasSessionProblem)
            {
                // 登录态 / 人机验证问题：让上层先把账号状态弄好，附件保留等再传
                m_pluginHintLabel->setText(tr("上传被拦下，处理好账号状态后点「添加插件包…」重试"));
                NotificationManager::showInfo(this, message);
                return;
            }
            m_pluginFilePath.clear();
            m_pluginHintLabel->clear();
            NotificationManager::showInfo(this, message);
        });
}

void ForumComposerPage::clearSchematicAttachment()
{
    m_schematics.clear();
    m_schematicMetaLabels.clear();
    refreshSchematicRows();
}

bool ForumComposerPage::anySchematicUploading() const
{
    for (const SchematicEntry &entry : m_schematics)
    {
        if (entry.uploading)
            return true;
    }
    return false;
}

void ForumComposerPage::refreshSchematicRows()
{
    if (!m_schematicRowsLayout)
        return;

    // 行是按顺序铺的，重建一次的成本很小（最多 8 份）
    while (QLayoutItem *item = m_schematicRowsLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_schematicMetaLabels.clear();

    for (int index = 0; index < m_schematics.size(); ++index)
    {
        SchematicEntry &entry = m_schematics[index];

        auto *row = new QWidget(m_schematicCard);
        row->setAttribute(Qt::WA_StyledBackground, false);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(3);

        auto *topRow = new QHBoxLayout();
        topRow->setContentsMargins(0, 0, 0, 0);
        topRow->setSpacing(8);
        auto *labelEdit = new QLineEdit(row);
        labelEdit->setObjectName(QStringLiteral("forumField"));
        labelEdit->setPlaceholderText(tr("给这份投影起个名字，如 主世界 / 下界"));
        labelEdit->setMaxLength(kMaxSchematicLabel);
        labelEdit->setText(entry.label);
        const int key = entry.key;
        connect(labelEdit, &QLineEdit::textEdited, this, [this, key](const QString &text) {
            for (SchematicEntry &item : m_schematics)
            {
                if (item.key == key)
                {
                    item.label = text.trimmed();
                    break;
                }
            }
        });
        topRow->addWidget(labelEdit, 1);

        auto *removeButton = new QPushButton(tr("移除"), row);
        removeButton->setObjectName(QStringLiteral("forumGhostButton"));
        removeButton->setCursor(Qt::PointingHandCursor);
        removeButton->setFixedHeight(24);
        connect(removeButton, &QPushButton::clicked, this, [this, key]() {
            removeSchematicEntry(key);
        });
        topRow->addWidget(removeButton);
        rowLayout->addLayout(topRow);

        auto *meta = new QLabel(row);
        meta->setObjectName(QStringLiteral("forumStatus"));
        meta->setWordWrap(true);
        const QFileInfo info(entry.filePath);
        QString metaText = QStringLiteral("%1 · %2")
                               .arg(info.fileName(), ForumKit::humanFileSize(info.size()));
        if (entry.uploading)
            metaText = tr("%1 · 正在上传…").arg(info.fileName());
        else if (!entry.upload.isValid())
            metaText = tr("%1 · 还没传上去").arg(info.fileName());
        meta->setText(metaText);
        rowLayout->addWidget(meta);
        m_schematicMetaLabels.insert(entry.key, meta);

        m_schematicRowsLayout->addWidget(row);
    }

    if (m_schematicHintLabel)
    {
        if (m_schematics.isEmpty())
        {
            m_schematicHintLabel->setText(
                tr("发布投影必须至少带一份投影文件（.litematic / .schem 等，单份最大 50 MB）；"
                   "主世界、下界等可以一次挂多份，各自命名"));
        }
        else
        {
            m_schematicHintLabel->setText(tr("已添加 %1/%2 份 · 下载时读者可以自己挑要哪一份")
                                              .arg(m_schematics.size())
                                              .arg(kMaxSchematicFiles));
        }
    }
    if (m_schematicAddButton)
        m_schematicAddButton->setEnabled(m_schematics.size() < kMaxSchematicFiles);
}

void ForumComposerPage::pickSchematicFiles()
{
    const int remaining = kMaxSchematicFiles - m_schematics.size();
    if (remaining <= 0)
    {
        NotificationManager::showInfo(
            this, tr("一条帖子最多挂 %1 份投影").arg(kMaxSchematicFiles));
        return;
    }

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("选择投影文件"), QString(),
        QStringLiteral("投影文件 (*.litematic *.nbt *.schematic *.schem)"));
    if (paths.isEmpty())
        return;

    int added = 0;
    for (const QString &path : paths)
    {
        if (added >= remaining)
            break;
        if (path.trimmed().isEmpty())
            continue;

        // 同一个文件重复选进来没有意义（服务端也会当成一份）
        bool duplicated = false;
        for (const SchematicEntry &entry : m_schematics)
        {
            if (entry.filePath == path)
            {
                duplicated = true;
                break;
            }
        }
        if (duplicated)
            continue;

        const QFileInfo info(path);
        if (!kSchematicExtensions.contains(info.suffix().toLower()))
        {
            NotificationManager::showInfo(
                this, tr("%1 不是投影文件，支持的格式：.litematic / .schem / .schematic / .nbt")
                          .arg(info.fileName()));
            continue;
        }
        if (info.size() > kMaxSchematicBytes)
        {
            NotificationManager::showInfo(
                this, tr("%1 超过 50 MB，服务器不收这么大").arg(info.fileName()));
            continue;
        }

        // 版本名默认取投影自带的名字（.litematic 能解出来），没有就用文件名
        QString label = info.completeBaseName();
        if (path.endsWith(QLatin1String(".litematic"), Qt::CaseInsensitive))
        {
            const auto parsed = LitematicReader::readFile(path);
            if (parsed && !parsed->name.trimmed().isEmpty())
                label = parsed->name.trimmed();
        }
        label = label.left(kMaxSchematicLabel);

        SchematicEntry entry;
        entry.key = ++m_schematicKeySeed;
        entry.filePath = path;
        entry.label = label;
        m_schematics.append(entry);
        ++added;
    }

    if (added == 0)
        return;
    if (paths.size() > remaining)
    {
        NotificationManager::showInfo(
            this, tr("一条帖子最多挂 %1 份投影，多选的文件没有加进来").arg(kMaxSchematicFiles));
    }

    refreshSchematicRows();
    // 结构变了之后按 key 逐个开传（同时上传多份，进度各自显示）
    for (const QString &path : paths)
    {
        for (const SchematicEntry &entry : m_schematics)
        {
            if (entry.filePath == path && !entry.uploading && !entry.upload.isValid())
            {
                uploadSchematicEntry(entry.key);
                break;
            }
        }
    }
}

void ForumComposerPage::uploadSchematicEntry(int key)
{
    int index = -1;
    for (int i = 0; i < m_schematics.size(); ++i)
    {
        if (m_schematics[i].key == key)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    SchematicEntry &entry = m_schematics[index];
    entry.uploading = true;
    const QString path = entry.filePath;
    if (QLabel *meta = m_schematicMetaLabels.value(key, nullptr))
    {
        meta->setText(tr("%1 · 正在上传…").arg(QFileInfo(path).fileName()));
    }

    ForumContentService::instance()->uploadSchematicFile(
        path,
        [this, key, path](int percent) {
            if (QLabel *meta = m_schematicMetaLabels.value(key, nullptr))
            {
                meta->setText(tr("%1 · 正在上传 %2%")
                                  .arg(QFileInfo(path).fileName())
                                  .arg(percent));
            }
        },
        [this, key](const ForumSchematicUpload &upload) {
            for (SchematicEntry &item : m_schematics)
            {
                if (item.key != key)
                    continue;
                item.uploading = false;
                item.upload = upload;
                // 用户起的名字优先；还没改过就用投影自带的名字，再不行用文件名
                if (item.label.trimmed().isEmpty())
                {
                    const QString fallback = upload.name.isEmpty() ? upload.fileName : upload.name;
                    item.label = QFileInfo(fallback).completeBaseName().left(kMaxSchematicLabel);
                }
                break;
            }
            refreshSchematicRows();
        },
        [this, key](const QString &message, int httpStatus) {
            const bool wasSessionProblem =
                ForumAccountService::instance()->needsHumanCheck() || (httpStatus == 401);
            // 传不上去的这一行撤掉，用户重新点「添加投影文件…」就能重试（同一个文件不会重复占位）
            for (int i = 0; i < m_schematics.size(); ++i)
            {
                if (m_schematics[i].key != key)
                    continue;
                m_schematics.removeAt(i);
                break;
            }
            refreshSchematicRows();
            if (wasSessionProblem && m_schematicHintLabel)
            {
                m_schematicHintLabel->setText(
                    tr("上传被拦下，处理好账号状态后点「添加投影文件…」重试"));
            }
            NotificationManager::showInfo(this, message);
        });
}

void ForumComposerPage::removeSchematicEntry(int key)
{
    for (int i = 0; i < m_schematics.size(); ++i)
    {
        if (m_schematics[i].key == key)
        {
            m_schematics.removeAt(i);
            break;
        }
    }
    refreshSchematicRows();
}

void ForumComposerPage::pickTutorialAttachments()
{
    if (m_attachments.size() >= kMaxAttachments)
    {
        NotificationManager::showInfo(this, tr("资料附件最多 10 个"));
        return;
    }

    QStringList extFilter;
    for (const QString &ext : kAttachmentExtensions)
        extFilter << QStringLiteral("*.%1").arg(ext);
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("选择资料附件"), QString(),
        tr("常用资料文件 (%1)").arg(extFilter.join(QLatin1Char(' '))));
    if (paths.isEmpty())
        return;

    const int remaining = kMaxAttachments - m_attachments.size();
    for (int i = 0; i < paths.size() && i < remaining; ++i)
        uploadTutorialAttachment(paths.at(i));
    if (paths.size() > remaining)
        NotificationManager::showInfo(this, tr("资料附件最多 10 个，多选的文件没有上传"));
}

void ForumComposerPage::uploadTutorialAttachment(const QString &path)
{
    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();
    if (!kAttachmentExtensions.contains(ext))
    {
        NotificationManager::showInfo(
            this, tr("不支持 .%1 文件，试试 PDF、压缩包或文本类").arg(ext));
        return;
    }
    if (info.size() > kMaxAttachmentBytes)
    {
        NotificationManager::showInfo(this, tr("%1 超过 50 MB").arg(info.fileName()));
        return;
    }

    m_attachmentAddButton->setEnabled(false);
    m_attachmentHintLabel->setText(tr("正在上传 %1…").arg(info.fileName()));
    ForumContentService::instance()->uploadAttachmentFile(
        path,
        [this](int percent) {
            m_attachmentHintLabel->setText(tr("正在上传附件… %1%").arg(percent));
        },
        [this](const ForumAttachmentUpload &upload) {
            m_attachmentAddButton->setEnabled(true);
            m_attachmentHintLabel->clear();
            if (upload.isValid())
            {
                m_attachments.append(upload);
                refreshTutorialAttachmentRows();
            }
        },
        [this](const QString &message, int) {
            m_attachmentAddButton->setEnabled(true);
            m_attachmentHintLabel->setText(message);
        });
}

void ForumComposerPage::refreshTutorialAttachmentRows()
{
    // 清空旧行重新铺（附件最多 10 个，重建的成本可以忽略）
    if (m_attachmentRowsLayout)
    {
        while (m_attachmentRowsLayout->count() > 0)
        {
            QLayoutItem *item = m_attachmentRowsLayout->takeAt(0);
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        for (int i = 0; i < m_attachments.size(); ++i)
        {
            const ForumAttachmentUpload &upload = m_attachments.at(i);
            auto *row = new QWidget(m_attachmentCard);
            row->setAttribute(Qt::WA_StyledBackground, false);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);
            auto *nameLabel = new QLabel(
                QStringLiteral("📎 %1 · %2")
                    .arg(upload.fileName,
                         ForumKit::humanFileSize(upload.fileSize)),
                row);
            nameLabel->setObjectName(QStringLiteral("forumStatus"));
            rowLayout->addWidget(nameLabel, 1);
            auto *removeButton = new QPushButton(tr("移除"), row);
            removeButton->setObjectName(QStringLiteral("forumGhostButton"));
            removeButton->setCursor(Qt::PointingHandCursor);
            removeButton->setFixedHeight(24);
            connect(removeButton, &QPushButton::clicked, row, [this, i]() {
                if (i >= 0 && i < m_attachments.size())
                {
                    m_attachments.removeAt(i);
                    refreshTutorialAttachmentRows();
                }
            });
            rowLayout->addWidget(removeButton);
            m_attachmentRowsLayout->addWidget(row);
        }
        m_attachmentRowsLayout->addStretch(1);
    }
    if (m_attachmentCard)
        m_attachmentCard->setVisible(m_mode == Mode::Tutorial);
}

void ForumComposerPage::submit()
{
    // 首次发帖 / 发资源前要先同意社区规范，同意一次长期生效
    if (!CommunityAgreementDialog::ensureAccepted(this))
        return;

    const QString title = m_titleEdit->text().trimmed();
    const QString content = m_contentEdit->toPlainText().trimmed();

    if (title.size() < 2)
    {
        NotificationManager::showInfo(this, tr("标题至少要 2 个字"));
        m_titleEdit->setFocus();
        return;
    }
    if (content.size() < 2)
    {
        NotificationManager::showInfo(this, tr("正文至少要 2 个字"));
        m_contentEdit->setFocus();
        return;
    }
    if (content.size() > kContentLimit)
    {
        NotificationManager::showInfo(this, tr("正文太长了，最多 2 万字"));
        return;
    }

    // 输入框里还没按回车的文字也算标签，别让用户以为已经加上了
    m_tagsEditor->commitPending();
    const QStringList tags = m_tagsEditor->tags();

    // 资源模式：插件固定发到插件分享板块、投影固定发到投影分享板块；
    // 教程模式：固定发到教程分享板块；问答模式：固定发到有奖问答板块；
    // 导航模式：按类型固定发到网站导航 / 服务器导航板块；
    // 帖子模式：发到板选里的板块，只是一篇普通讨论帖
    const bool resourceMode = (m_mode == Mode::Resource);
    const bool tutorialMode = (m_mode == Mode::Tutorial);
    const bool questionMode = (m_mode == Mode::Question);
    const bool navigationMode = (m_mode == Mode::Navigation);
    const bool pluginKind = m_kindSlug == QLatin1String("plugin");
    const bool schematicKind = m_kindSlug == QLatin1String("schematic");

    // 导航模式：网址是必填项；网站类型没有协议前缀时自动补 https://，服务器地址原样填
    QString navigationUrl;
    if (navigationMode)
    {
        navigationUrl = m_navUrlEdit ? m_navUrlEdit->text().trimmed() : QString();
        if (navigationUrl.isEmpty())
        {
            NotificationManager::showInfo(this, tr("请先填写要分享的网址"));
            if (m_navUrlEdit)
                m_navUrlEdit->setFocus();
            return;
        }
        if (m_navKind == QLatin1String("website")
            && !navigationUrl.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            && !navigationUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        {
            navigationUrl.prepend(QStringLiteral("https://"));
        }
        if (navigationUrl.size() > kMaxNavigationUrlLength)
        {
            NotificationManager::showInfo(
                this, tr("网址太长了，最多 %1 字").arg(kMaxNavigationUrlLength));
            if (m_navUrlEdit)
                m_navUrlEdit->setFocus();
            return;
        }
    }

    // 问答模式：赏金必须是有余额支撑的整数，发出去之后就托管在服务端
    int bounty = 0;
    if (questionMode)
    {
        bounty = m_bountySpin ? m_bountySpin->value() : kDefaultBounty;
        if (bounty < kMinBounty || bounty > kMaxBounty)
        {
            NotificationManager::showInfo(
                this, tr("悬赏要填 %1~%2 个方块").arg(kMinBounty).arg(kMaxBounty));
            return;
        }
        const int balance = ForumAccountService::instance()->user().points;
        if (bounty > balance)
        {
            NotificationManager::showInfo(
                this, tr("方块不够：你现在有 %1 个，悬赏要 %2 个").arg(balance).arg(bounty));
            return;
        }
    }

    QVector<ForumSchematicBinding> schematicBindings;
    // 作品来源：默认原创；选了「转载」就必须交代出处
    ForumWorkSource workSource;
    if (resourceMode)
    {
        if (!pluginKind && !schematicKind)
        {
            NotificationManager::showInfo(this, tr("请先选择资源分类"));
            return;
        }
        if (m_repostSourceButton && m_repostSourceButton->isChecked())
        {
            const QString sourceUrl = m_repostUrlEdit->text().trimmed();
            const QString sourceNote = m_repostNoteEdit->text().trimmed();
            if (sourceUrl.isEmpty()
                || !(sourceUrl.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                     || sourceUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)))
            {
                NotificationManager::showInfo(
                    this, tr("转载要填原作品链接（以 http:// 或 https:// 开头）"));
                m_repostUrlEdit->setFocus();
                return;
            }
            if (sourceNote.isEmpty())
            {
                NotificationManager::showInfo(this, tr("转载要填转载说明，交代原作者或来源"));
                m_repostNoteEdit->setFocus();
                return;
            }
            workSource.type = QStringLiteral("repost");
            workSource.url = sourceUrl;
            workSource.note = sourceNote;
        }
        if (pluginKind)
        {
            if (m_pluginUploading)
            {
                NotificationManager::showInfo(this, tr("插件包还没传完，等一下再发布"));
                return;
            }
            if (!m_pluginUpload.isValid())
            {
                NotificationManager::showInfo(this, tr("发布插件需要先添加一个插件包"));
                return;
            }
        }
        else
        {
            if (anySchematicUploading())
            {
                NotificationManager::showInfo(this, tr("投影文件还没传完，等一下再发布"));
                return;
            }
            QStringList usedLabels;
            for (const SchematicEntry &entry : m_schematics)
            {
                if (!entry.upload.isValid())
                    continue;
                const QString label = entry.label.trimmed();
                if (!label.isEmpty())
                {
                    if (usedLabels.contains(label))
                    {
                        NotificationManager::showInfo(
                            this, tr("两份投影都叫「%1」，换个名字更容易分清").arg(label));
                        return;
                    }
                    usedLabels.append(label);
                }
                ForumSchematicBinding binding;
                binding.uploadId = entry.upload.uploadId;
                binding.label = label;
                schematicBindings.append(binding);
            }
            if (schematicBindings.isEmpty())
            {
                NotificationManager::showInfo(
                    this, tr("发布投影需要先添加至少一份投影文件"));
                return;
            }
        }
    }

    // 轮播图（资源 / 导航模式选填）：有图片还在上传就先拦一下，
    // 就绪的条目按当前顺序随帖提交
    QVector<ForumCarouselItem> carouselItems;
    if ((resourceMode || navigationMode) && m_carouselEditor)
    {
        if (m_carouselEditor->hasPendingUploads())
        {
            NotificationManager::showInfo(this, tr("轮播图片还没传完，等一下再发布"));
            return;
        }
        carouselItems = m_carouselEditor->items();
    }

    // 封面（教程 / 资源 / 导航选填）：正在上传就先拦一下，传好的一起随帖提交
    if ((tutorialMode || resourceMode || navigationMode) && m_coverUploading)
    {
        NotificationManager::showInfo(this, tr("封面还在上传，等一下再发布"));
        return;
    }
    const int coverUploadId = m_coverUpload.isValid() ? m_coverUpload.id : 0;

    const QString boardSlug = tutorialMode
                                  ? QString::fromLatin1(kTutorialShareBoardSlug)
                                  : resourceMode
                                        ? QString::fromLatin1(pluginKind ? kPluginShareBoardSlug
                                                                         : kSchematicShareBoardSlug)
                                        : m_boardCombo->currentData().toString();
    m_submitButton->setEnabled(false);
    m_statusLabel->setText(tr("正在发布…"));
    m_statusLabel->setStyleSheet(QString());

    const auto finish = [this]() {
        m_submitButton->setEnabled(true);
        m_statusLabel->clear();
    };
    const auto fail = [this](const QString &message) {
        m_submitButton->setEnabled(true);
        m_statusLabel->setText(message);
        m_statusLabel->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;")));
    };
    // 三种发布类型共用：成功后先记住这次用的标签，下次发同类内容可以直接点选
    const auto onCreated = [this, finish](int threadId) {
        finish();
        if (threadId <= 0)
            return;
        m_tagsEditor->rememberTags();
        emit threadCreated(threadId);
    };

    if (questionMode)
    {
        // 有奖问答：赏金随帖子上传，服务端在同一个事务里托管，余额不够整帖都不会发出
        ForumContentService::instance()->createQuestionThread(
            title, content, tags, bounty, onCreated,
            [this, fail](const QString &message, int) {
                fail(message);
            });
        return;
    }

    if (navigationMode)
    {
        // 导航帖：网址随帖子一起提交，板块由服务端按类型固定指定
        ForumContentService::instance()->createNavigationThread(
            m_navKind, navigationUrl, title, content, tags, carouselItems, coverUploadId, onCreated,
            [this, fail](const QString &message, int) {
                fail(message);
            });
        return;
    }

    if (tutorialMode)
    {
        // 教程帖：资料附件是可选的，已上传的随帖一起绑定
        QVector<int> attachmentIds;
        for (const ForumAttachmentUpload &upload : m_attachments)
            attachmentIds.append(upload.uploadId);
        ForumContentService::instance()->createTutorialThread(
            title, content, tags, attachmentIds, coverUploadId, onCreated,
            [this, fail](const QString &message, int) {
                fail(message);
            });
        return;
    }

    if (resourceMode)
    {
        // 相关链接：发布页填的官网 / 仓库 / 反馈等，随资源一起绑到帖子上
        const QVector<ForumResourceLink> links = m_linksEditor ? m_linksEditor->links()
                                                               : QVector<ForumResourceLink>();
        if (pluginKind)
        {
            ForumContentService::instance()->createPluginThread(
                boardSlug, title, content, tags, m_pluginUpload.uploadId, links, workSource,
                carouselItems, coverUploadId, onCreated,
                [this, fail](const QString &message, int) {
                    fail(message);
                });
        }
        else
        {
            ForumContentService::instance()->createSchematicThread(
                boardSlug, title, content, tags, schematicBindings, links, workSource,
                carouselItems, coverUploadId, onCreated,
                [this, fail](const QString &message, int) {
                    fail(message);
                });
        }
        return;
    }

    ForumContentService::instance()->createThread(
        boardSlug, title, content, tags, onCreated,
        [this, fail](const QString &message, int) {
            fail(message);
        });
}
