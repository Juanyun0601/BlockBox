/**
 * @file   ForumComposerPage.cpp
 * @brief  发布详情页实现：顶部「帖子 / 资源」切换
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumComposerPage.h"

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPair>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QFileDialog>
#include "components/NotificationManager.h"
#include "components/ForumKit.h"
#include "components/QuizDialog.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/plugin/PluginManager.h"
#include "utils/StyleKit.h"

namespace {
constexpr int kMaxTags = 4;
constexpr int kMaxTagLength = 12;
constexpr int kTitleLimit = 80;
constexpr int kContentLimit = 20000;
/** 与服务端 maxPluginBytes 一致；超出直接本地提示，免得白传 */
constexpr qint64 kMaxPluginBytes = 20 * 1024 * 1024;
/** 插件分享板块：资源→插件 的帖子发到这里，「插件与开发」只留讨论 */
constexpr auto kPluginShareBoardSlug = "plugin-share";
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

    // ── 顶部「帖子 / 资源」切换（分段控件，与登录方式切换同款样式） ──
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
    m_postModeButton->setToolTip(tr("发帖讨论：选一个社区板块，交流、求助与教程"));
    modeLayout->addWidget(m_postModeButton, 1);

    m_resourceModeButton = new QPushButton(tr("资源"), m_modeBar);
    m_resourceModeButton->setObjectName(QStringLiteral("composerSegBtn"));
    m_resourceModeButton->setCheckable(true);
    m_resourceModeButton->setAutoExclusive(true);
    m_resourceModeButton->setCursor(Qt::PointingHandCursor);
    m_resourceModeButton->setToolTip(tr("发资源：先选分类，再上传文件（目前有插件）"));
    modeLayout->addWidget(m_resourceModeButton, 1);

    connect(m_postModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Post); });
    connect(m_resourceModeButton, &QPushButton::clicked, this,
            [this]() { setMode(Mode::Resource); });
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
        tr("插件帖会发到「插件分享」板块：上传 .BlockBox 插件包，填好标题和正文就能发布"),
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

    // 正文 + 预设 + 预览切换
    auto *contentHeaderRow = new QHBoxLayout();
    contentHeaderRow->addWidget(ForumKit::makeMuted(formCard, tr("正文（支持 Markdown，可粘贴图片链接）")));
    contentHeaderRow->addStretch(1);
    m_presetButton = new QPushButton(tr("预设"), formCard);
    m_presetButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_presetButton->setCursor(Qt::PointingHandCursor);
    m_presetButton->setFixedHeight(26);
    m_presetButton->setToolTip(tr("插入常用的 Markdown 正文模板"));
    connect(m_presetButton, &QPushButton::clicked, this, &ForumComposerPage::showPresetMenu);
    contentHeaderRow->addWidget(m_presetButton);
    m_previewButton = new QPushButton(tr("预览"), formCard);
    m_previewButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_previewButton->setCheckable(true);
    m_previewButton->setCursor(Qt::PointingHandCursor);
    m_previewButton->setFixedHeight(26);
    connect(m_previewButton, &QPushButton::toggled, this,
            [this](bool checked) { setPreviewMode(checked); });
    contentHeaderRow->addWidget(m_previewButton);
    cardLayout->addLayout(contentHeaderRow);

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
    m_contentStack->setCurrentIndex(0);
    cardLayout->addWidget(m_contentStack);

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

void ForumComposerPage::setMode(Mode mode)
{
    m_mode = mode;
    // 从资源切回帖子时才需要板块列表；资源模式不看板块
    if (m_mode == Mode::Post && !m_boardsLoaded)
        loadBoards();
    applyMode();
}

void ForumComposerPage::applyMode()
{
    const bool resourceMode = (m_mode == Mode::Resource);

    if (m_postModeButton)
        m_postModeButton->setChecked(!resourceMode);
    if (m_resourceModeButton)
        m_resourceModeButton->setChecked(resourceMode);

    if (m_boardRow)
        m_boardRow->setVisible(!resourceMode);
    if (m_kindRow)
        m_kindRow->setVisible(resourceMode);
    for (QPushButton *chip : m_kindButtons)
    {
        if (chip)
            chip->setChecked(chip->property("kindSlug").toString() == m_kindSlug);
    }

    const bool pluginKind = m_kindSlug == QLatin1String("plugin");
    if (m_pluginCard)
        m_pluginCard->setVisible(resourceMode && pluginKind);

    if (m_formTitleLabel)
        m_formTitleLabel->setText(resourceMode ? tr("发布资源") : tr("发表新主题"));
    if (m_submitButton)
        m_submitButton->setText(resourceMode ? tr("发布插件") : tr("发布主题"));
    if (m_titleEdit)
    {
        m_titleEdit->setPlaceholderText(resourceMode
                                            ? tr("例如：自动整理模组的辅助插件")
                                            : tr("例如：1.20.4 启动报 Java 找不到"));
    }

    // 资源模式下插件包是必填的，没选包时先把要求写在卡片上
    if (m_pluginHintLabel && resourceMode && !m_pluginUploading && !m_pluginUpload.isValid()
        && m_pluginHintLabel->text().isEmpty())
    {
        m_pluginHintLabel->setText(tr("发布插件必须带一个插件包（.BlockBox，最大 20 MB）"));
    }
}

void ForumComposerPage::resetForm()
{
    m_titleEdit->clear();
    m_tagsEdit->clear();
    m_contentEdit->clear();
    m_previewBrowser->setPostMarkdown(QString());
    if (m_previewButton->isChecked())
        m_previewButton->setChecked(false);
    m_statusLabel->clear();
    m_statusLabel->setStyleSheet(QString());
    clearPluginAttachment();
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
                // 「插件分享」是资源→插件专用板块，帖子里不提供（发帖只做讨论）
                if (board.slug == QLatin1String(kPluginShareBoardSlug))
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

    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const ForumKit::MarkdownPreset &preset : ForumKit::postPresets())
    {
        menu->addAction(preset.label, this, [this, text = preset.text]() {
            // 插模板时先切回编辑态，不然用户看不到刚插进去的内容
            if (m_previewButton->isChecked())
                m_previewButton->setChecked(false);
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
    else
        m_previewButton->setText(tr("预览"));
    m_previewButton->setText(preview ? tr("继续编辑") : tr("预览"));
    m_contentStack->setCurrentIndex(preview ? 1 : 0);
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

void ForumComposerPage::submit()
{
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

    // 资源模式：目前只有「插件」分类，固定发到插件分享板块；
    // 帖子模式：发到板选里的板块，只是一篇普通讨论帖
    const bool resourceMode = (m_mode == Mode::Resource);
    if (resourceMode)
    {
        if (m_kindSlug != QLatin1String("plugin"))
        {
            NotificationManager::showInfo(this, tr("请先选择资源分类"));
            return;
        }
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

    const QString boardSlug = resourceMode ? QString::fromLatin1(kPluginShareBoardSlug)
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

    if (resourceMode)
    {
        ForumContentService::instance()->createPluginThread(
            boardSlug, title, content, tags, m_pluginUpload.uploadId,
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
