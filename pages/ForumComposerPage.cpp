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
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPair>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QFileDialog>
#include "components/NotificationManager.h"
#include "components/ForumKit.h"
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
/** 插件分享板块：资源→插件 的帖子发到这里，「插件与开发」只留讨论 */
constexpr auto kPluginShareBoardSlug = "plugin-share";
/** 投影分享板块：资源→投影 的帖子发到这里，普通帖子里也不提供 */
constexpr auto kSchematicShareBoardSlug = "schematic-share";
/** 教程分享板块：教程帖固定发到这里，与网页版同一口径 */
constexpr auto kTutorialShareBoardSlug = "tutorial-share";
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
} // namespace

ForumComposerPage::ForumComposerPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

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

    connect(m_postModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Post); });
    connect(m_resourceModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Resource); });
    connect(m_tutorialModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Tutorial); });
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
    m_schematicCard = ForumKit::makeCard(formCard);
    auto *schematicLayout = new QHBoxLayout(m_schematicCard);
    schematicLayout->setContentsMargins(12, 10, 12, 10);
    schematicLayout->setSpacing(10);
    m_schematicIconLabel = new QLabel(m_schematicCard);
    m_schematicIconLabel->setFixedSize(40, 40);
    m_schematicIconLabel->setAlignment(Qt::AlignCenter);
    m_schematicIconLabel->setStyleSheet(QStringLiteral(
        "background:@BG_MUTED@;border:1px solid @BORDER@;border-radius:8px;"));
    schematicLayout->addWidget(m_schematicIconLabel);
    auto *schematicTextLayout = new QVBoxLayout();
    schematicTextLayout->setSpacing(2);
    m_schematicNameLabel = new QLabel(m_schematicCard);
    m_schematicNameLabel->setObjectName(QStringLiteral("forumStatus"));
    m_schematicNameLabel->setWordWrap(true);
    schematicTextLayout->addWidget(m_schematicNameLabel);
    m_schematicMetaLabel = new QLabel(m_schematicCard);
    m_schematicMetaLabel->setObjectName(QStringLiteral("forumStatus"));
    m_schematicMetaLabel->setWordWrap(true);
    schematicTextLayout->addWidget(m_schematicMetaLabel);
    m_schematicHintLabel = new QLabel(m_schematicCard);
    m_schematicHintLabel->setObjectName(QStringLiteral("forumStatus"));
    m_schematicHintLabel->setWordWrap(true);
    schematicTextLayout->addWidget(m_schematicHintLabel);
    schematicLayout->addLayout(schematicTextLayout, 1);
    m_schematicRemoveButton = new QPushButton(tr("移除"), m_schematicCard);
    m_schematicRemoveButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_schematicRemoveButton->setCursor(Qt::PointingHandCursor);
    m_schematicRemoveButton->setFixedHeight(26);
    connect(m_schematicRemoveButton, &QPushButton::clicked, this,
            &ForumComposerPage::clearSchematicAttachment);
    schematicLayout->addWidget(m_schematicRemoveButton);
    m_schematicAddButton = new QPushButton(tr("添加投影文件…"), m_schematicCard);
    m_schematicAddButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_schematicAddButton->setCursor(Qt::PointingHandCursor);
    m_schematicAddButton->setFixedHeight(26);
    connect(m_schematicAddButton, &QPushButton::clicked, this, &ForumComposerPage::pickSchematicFile);
    schematicLayout->addWidget(m_schematicAddButton);
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

    // 标题
    cardLayout->addWidget(ForumKit::makeMuted(formCard, tr("标题（2–80 字，写清现象更容易被解答）")));
    m_titleEdit = new QLineEdit(formCard);
    m_titleEdit->setObjectName(QStringLiteral("forumField"));
    m_titleEdit->setMaxLength(kTitleLimit);
    m_titleEdit->setPlaceholderText(tr("例如：1.20.4 启动报 Java 找不到"));
    cardLayout->addWidget(m_titleEdit);

    // 标签
    cardLayout->addWidget(ForumKit::makeMuted(formCard, tr("标签（用逗号分隔，最多 4 个，每个最长 12 字）")));
    m_tagsEdit = new QLineEdit(formCard);
    m_tagsEdit->setObjectName(QStringLiteral("forumField"));
    m_tagsEdit->setPlaceholderText(tr("例如：1.20.4, 优化"));
    cardLayout->addWidget(m_tagsEdit);

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

void ForumComposerPage::setMode(Mode mode)
{
    m_mode = mode;
    // 从资源切回帖子时才需要板块列表；资源 / 教程模式不看板块
    if (m_mode == Mode::Post && !m_boardsLoaded)
        loadBoards();
    applyMode();
}

void ForumComposerPage::applyMode()
{
    const bool resourceMode = (m_mode == Mode::Resource);
    const bool tutorialMode = (m_mode == Mode::Tutorial);

    if (m_postModeButton)
        m_postModeButton->setChecked(m_mode == Mode::Post);
    if (m_resourceModeButton)
        m_resourceModeButton->setChecked(resourceMode);
    if (m_tutorialModeButton)
        m_tutorialModeButton->setChecked(tutorialMode);

    if (m_boardRow)
        m_boardRow->setVisible(!resourceMode && !tutorialMode);
    if (m_kindRow)
        m_kindRow->setVisible(resourceMode);
    for (QPushButton *chip : m_kindButtons)
    {
        if (chip)
            chip->setChecked(chip->property("kindSlug").toString() == m_kindSlug);
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

    if (m_formTitleLabel)
    {
        m_formTitleLabel->setText(tutorialMode ? tr("发布教程")
                                               : (resourceMode ? tr("发布资源") : tr("发表新主题")));
    }
    if (m_submitButton)
    {
        m_submitButton->setText(tutorialMode ? tr("发布教程")
                                             : (resourceMode ? (pluginKind ? tr("发布插件") : tr("发布投影"))
                                                             : tr("发布主题")));
    }
    if (m_titleEdit)
    {
        m_titleEdit->setPlaceholderText(
            tutorialMode ? tr("例如：低配电脑流畅跑 1.20 的完整设置教程")
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
    // 投影文件同理
    if (m_schematicHintLabel && resourceMode && !m_schematicUploading && !m_schematicUpload.isValid()
        && m_schematicHintLabel->text().isEmpty())
    {
        m_schematicHintLabel->setText(tr("发布投影必须带一个投影文件（.litematic / .schem 等，最大 50 MB）"));
    }
    // 教程模式下提示附件容量口径（附件可选，不强制）
    if (m_attachmentHintLabel && tutorialMode && m_attachmentHintLabel->text().isEmpty())
    {
        m_attachmentHintLabel->setText(
            tr("可选：教程里用到的资料包、示例文件都能挂上（单个最大 50 MB，最多 10 个）"));
    }
}

void ForumComposerPage::resetForm()
{
    m_titleEdit->clear();
    m_tagsEdit->clear();
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
                // 「插件分享」「投影分享」是资源→插件/投影专用板块，「教程分享」是教程模式
                // 专用板块：帖子里都不提供（发帖只做讨论）
                if (board.slug == QLatin1String(kPluginShareBoardSlug)
                    || board.slug == QLatin1String(kSchematicShareBoardSlug)
                    || board.slug == QLatin1String(kTutorialShareBoardSlug))
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
    m_schematicFilePath.clear();
    m_schematicUpload = ForumSchematicUpload();
    m_schematicUploading = false;
    m_schematicIconLabel->setPixmap(QPixmap());
    m_schematicIconLabel->setText(QString());
    m_schematicNameLabel->clear();
    m_schematicMetaLabel->clear();
    m_schematicHintLabel->clear();
    m_schematicAddButton->setEnabled(true);
    applyMode();
}

void ForumComposerPage::pickSchematicFile()
{
    if (m_schematicUploading)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择投影文件"), QString(),
        QStringLiteral("投影文件 (*.litematic *.nbt *.schematic *.schem)"));
    if (path.isEmpty())
        return;

    const QFileInfo info(path);
    if (info.size() > kMaxSchematicBytes)
    {
        NotificationManager::showInfo(
            this, tr("投影文件超过 50 MB，服务器不收这么大，先删减一下"));
        return;
    }

    clearSchematicAttachment();
    m_schematicFilePath = path;
    m_schematicUploading = true;
    m_schematicIconLabel->setPixmap(PublishKit::makeGradientIconPixmap(
        QColor(QStringLiteral("#3B82F6")), QColor(QStringLiteral("#1D4ED8")),
        QStringLiteral(":/Images/Icons/list.svg"), 40));

    // .litematic 能本地解出名称与包围盒，先亮出来（其他格式就用文件名）
    QString displayName = info.fileName();
    QString metaText = ForumKit::humanFileSize(info.size());
    if (path.endsWith(QLatin1String(".litematic"), Qt::CaseInsensitive))
    {
        const auto parsed = LitematicReader::readFile(path);
        if (parsed && !parsed->name.isEmpty())
            displayName = parsed->name;
        if (parsed && parsed->enclosingSizeX != 0)
        {
            metaText = tr("%1 · %2×%3×%4")
                           .arg(ForumKit::humanFileSize(info.size()))
                           .arg(qAbs(parsed->enclosingSizeX))
                           .arg(qAbs(parsed->enclosingSizeY))
                           .arg(qAbs(parsed->enclosingSizeZ));
        }
    }
    m_schematicNameLabel->setText(tr("%1（正在上传…）").arg(displayName));
    m_schematicMetaLabel->setText(metaText);
    m_schematicHintLabel->setText(tr("上传完成后才能发布"));
    m_schematicAddButton->setEnabled(false);

    ForumContentService::instance()->uploadSchematicFile(
        path,
        [this](int percent) {
            m_schematicMetaLabel->setText(tr("正在上传 %1%").arg(percent));
        },
        [this](const ForumSchematicUpload &upload) {
            m_schematicUploading = false;
            m_schematicUpload = upload;
            m_schematicAddButton->setEnabled(true);
            m_schematicNameLabel->setText(upload.name.isEmpty() ? upload.fileName : upload.name);
            m_schematicMetaLabel->setText(ForumKit::humanFileSize(upload.fileSize));
            m_schematicHintLabel->setText(tr("我将随主题一起发布这个投影"));
        },
        [this](const QString &message, int httpStatus) {
            m_schematicUploading = false;
            m_schematicAddButton->setEnabled(true);
            const bool wasSessionProblem =
                ForumAccountService::instance()->needsHumanCheck() ||
                (httpStatus == 401);
            if (wasSessionProblem)
            {
                // 登录态 / 人机验证问题：让上层先把账号状态弄好，附件保留等再传
                m_schematicHintLabel->setText(tr("上传被拦下，处理好账号状态后点「添加投影文件…」重试"));
                NotificationManager::showInfo(this, message);
                return;
            }
            m_schematicFilePath.clear();
            m_schematicHintLabel->clear();
            NotificationManager::showInfo(this, message);
        });
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

    QStringList tags;
    for (const QString &part : m_tagsEdit->text().split(QLatin1Char(',')))
    {
        const QString tag = part.trimmed();
        if (tag.isEmpty())
            continue;
        if (tag.size() > kMaxTagLength)
        {
            NotificationManager::showInfo(this, tr("标签「%1」超过 12 个字").arg(tag));
            return;
        }
        tags.append(tag);
        if (tags.size() >= kMaxTags)
            break;
    }
    if (m_tagsEdit->text().split(QLatin1Char(',')).count() > kMaxTags)
    {
        NotificationManager::showInfo(this, tr("标签最多 4 个"));
        return;
    }

    // 资源模式：插件固定发到插件分享板块、投影固定发到投影分享板块；
    // 教程模式：固定发到教程分享板块；帖子模式：发到板选里的板块，只是一篇普通讨论帖
    const bool resourceMode = (m_mode == Mode::Resource);
    const bool tutorialMode = (m_mode == Mode::Tutorial);
    const bool pluginKind = m_kindSlug == QLatin1String("plugin");
    const bool schematicKind = m_kindSlug == QLatin1String("schematic");
    if (resourceMode)
    {
        if (!pluginKind && !schematicKind)
        {
            NotificationManager::showInfo(this, tr("请先选择资源分类"));
            return;
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
            if (m_schematicUploading)
            {
                NotificationManager::showInfo(this, tr("投影文件还没传完，等一下再发布"));
                return;
            }
            if (!m_schematicUpload.isValid())
            {
                NotificationManager::showInfo(this, tr("发布投影需要先添加一个投影文件"));
                return;
            }
        }
    }

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

    if (tutorialMode)
    {
        // 教程帖：资料附件是可选的，已上传的随帖一起绑定
        QVector<int> attachmentIds;
        for (const ForumAttachmentUpload &upload : m_attachments)
            attachmentIds.append(upload.uploadId);
        ForumContentService::instance()->createTutorialThread(
            title, content, tags, attachmentIds,
            [this, finish](int threadId) {
                finish();
                if (threadId > 0)
                    emit threadCreated(threadId);
            },
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
                boardSlug, title, content, tags, m_pluginUpload.uploadId, links,
                [this, finish](int threadId) {
                    finish();
                    if (threadId > 0)
                        emit threadCreated(threadId);
                },
                [this, fail](const QString &message, int) {
                    fail(message);
                });
        }
        else
        {
            ForumContentService::instance()->createSchematicThread(
                boardSlug, title, content, tags, m_schematicUpload.uploadId, links,
                [this, finish](int threadId) {
                    finish();
                    if (threadId > 0)
                        emit threadCreated(threadId);
                },
                [this, fail](const QString &message, int) {
                    fail(message);
                });
        }
        return;
    }

    ForumContentService::instance()->createThread(
        boardSlug, title, content, tags,
        [this, finish](int threadId) {
            finish();
            if (threadId > 0)
                emit threadCreated(threadId);
        },
        [this, fail](const QString &message, int) {
            fail(message);
        });
}
