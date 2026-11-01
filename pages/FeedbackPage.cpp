/**
 * @file   FeedbackPage.cpp
 * @brief  软件内反馈页实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "FeedbackPage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "utils/AppVersion.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"

namespace {
constexpr int kDescMinLength = 5;
constexpr int kDescMaxLength = 800;
constexpr int kStepsMaxLength = 800;
constexpr int kExpectedMaxLength = 400;
constexpr int kActualMaxLength = 400;
constexpr int kOsVersionMaxLength = 100;
constexpr int kAppVersionMaxLength = 40;
constexpr int kLogsMaxLength = 2000;
constexpr int kScreenshotsMaxLength = 500;
constexpr int kSceneMaxLength = 800;

/** 灰色分类徽章（类型 / 已忽略共用一套配色） */
QLabel *makeNeutralBadge(QWidget *parent, const QString &text)
{
    QLabel *badge = ForumKit::makeBadge(parent, text);
    badge->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QLabel{background:@BG_MUTED@;color:@TEXT_TERTIARY@;border:none;"
        "border-radius:8px;font-size:11px;font-weight:600;padding:1px 8px;}")));
    return badge;
}

/** 「小标题 + 说明行 + 输入框」的字段块，切换类型时整块显隐 */
QWidget *makeFieldBlock(QWidget *parent, QLabel **titleLabel, const QString &title,
                        QPlainTextEdit **edit, const QString &placeholder, int minHeight,
                        const QString &hint = QString())
{
    auto *block = new QWidget(parent);
    block->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *label = ForumKit::makeMuted(block, title);
    layout->addWidget(label);
    if (!hint.isEmpty())
        layout->addWidget(ForumKit::makeMuted(block, hint));
    auto *input = new QPlainTextEdit(block);
    input->setObjectName(QStringLiteral("forumReplyEdit"));
    input->setPlaceholderText(placeholder);
    input->setMinimumHeight(minHeight);
    layout->addWidget(input);
    if (titleLabel)
        *titleLabel = label;
    if (edit)
        *edit = input;
    return block;
}

/** 「小标题 + 单行输入框」的字段块 */
QWidget *makeInputBlock(QWidget *parent, const QString &title, QLineEdit **edit,
                        const QString &placeholder, const QString &hint = QString())
{
    auto *block = new QWidget(parent);
    block->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(ForumKit::makeMuted(block, title));
    if (!hint.isEmpty())
        layout->addWidget(ForumKit::makeMuted(block, hint));
    auto *input = new QLineEdit(block);
    input->setObjectName(QStringLiteral("forumField"));
    input->setPlaceholderText(placeholder);
    layout->addWidget(input);
    if (edit)
        *edit = input;
    return block;
}

/** 正文里的 GitHub 模板标题（### xxx）转成加粗富文本 */
QString renderContent(const QString &content)
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

/** 公开列表的状态胶囊（仿 GitHub：待处理绿 / 已处理紫 / 已忽略灰） */
QLabel *makeStatePill(QWidget *parent, const QString &status)
{
    QLabel *pill = new QLabel(parent);
    pill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (status == QStringLiteral("resolved"))
    {
        pill->setText(QLabel::tr("已处理"));
        pill->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "QLabel{background:#8250df;color:#ffffff;border:none;border-radius:9px;"
            "font-size:11px;font-weight:700;padding:2px 9px;}")));
    }
    else if (status == QStringLiteral("ignored"))
    {
        pill->setText(QLabel::tr("已忽略"));
        pill->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "QLabel{background:@BG_MUTED@;color:@TEXT_TERTIARY@;border:none;border-radius:9px;"
            "font-size:11px;font-weight:700;padding:2px 9px;}")));
    }
    else
    {
        pill->setText(QLabel::tr("待处理"));
        pill->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "QLabel{background:@PRIMARY@;color:@TEXT_ON_PRIMARY@;border:none;border-radius:9px;"
            "font-size:11px;font-weight:700;padding:2px 9px;}")));
    }
    return pill;
}
} // namespace

FeedbackPage::FeedbackPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            &FeedbackPage::updateState);
}

void FeedbackPage::buildUi()
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
    auto *loginHint = new QLabel(tr("登录论坛账号后就可以提交反馈了，处理进度也会同步显示在这里"), m_loginHost);
    loginHint->setObjectName(QStringLiteral("forumStatus"));
    loginHint->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(loginHint);
    auto *loginButton = new QPushButton(tr("去登录"), m_loginHost);
    loginButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setFixedHeight(34);
    connect(loginButton, &QPushButton::clicked, this, &FeedbackPage::loginRequested);
    loginLayout->addWidget(loginButton, 0, Qt::AlignHCenter);
    layout->addWidget(m_loginHost);

    // ── 登录后的提交表单 + 反馈列表 ──
    m_mainHost = new QWidget(content);
    m_mainHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *mainLayout = new QVBoxLayout(m_mainHost);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(10);

    auto *formCard = ForumKit::makeCard(m_mainHost);
    auto *formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(20, 16, 20, 16);
    formLayout->setSpacing(10);

    formLayout->addWidget(ForumKit::makeSectionTitle(formCard, tr("提交反馈")));
    formLayout->addWidget(ForumKit::makeMuted(
        formCard, tr("带 * 的为必填项；信息填得越完整，问题定位和修复得越快。")));

    // 反馈类型
    auto *typeRow = new QHBoxLayout();
    typeRow->setSpacing(10);
    typeRow->addWidget(ForumKit::makeMuted(formCard, tr("反馈类型 *")));
    m_typeCombo = new QComboBox(formCard);
    m_typeCombo->setObjectName(QStringLiteral("forumField"));
    m_typeCombo->addItem(tr("🐛 问题反馈"));
    m_typeCombo->addItem(tr("💡 功能建议"));
    m_typeCombo->setFixedWidth(160);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, [this](int) { applyType(); });
    typeRow->addWidget(m_typeCombo);
    typeRow->addStretch(1);
    formLayout->addLayout(typeRow);

    // ── 问题反馈字段（与 GitHub Issue 模板一致） ──

    // 启动器版本（必填，预填当前版本）
    m_appVersionField = makeInputBlock(formCard, tr("启动器版本 *"), &m_appVersionEdit,
                                       tr("例如 beta7"), tr("在「设置 → 关于」里能看到版本号"));
    m_appVersionEdit->setText(AppVersion::current());
    m_appVersionEdit->setMaxLength(kAppVersionMaxLength);
    formLayout->addWidget(m_appVersionField);

    // 运行平台（必填下拉）
    m_platformField = new QWidget(formCard);
    m_platformField->setAttribute(Qt::WA_StyledBackground, false);
    {
        auto *platformLayout = new QVBoxLayout(m_platformField);
        platformLayout->setContentsMargins(0, 0, 0, 0);
        platformLayout->setSpacing(4);
        platformLayout->addWidget(ForumKit::makeMuted(m_platformField, tr("运行平台 *")));
        m_platformCombo = new QComboBox(m_platformField);
        m_platformCombo->setObjectName(QStringLiteral("forumField"));
        m_platformCombo->addItem(tr("Windows（x64）"));
        m_platformCombo->addItem(tr("Windows（ARM64）"));
        m_platformCombo->addItem(tr("Android"));
        m_platformCombo->addItem(tr("鸿蒙 HarmonyOS"));
        m_platformCombo->addItem(tr("其他（请在问题描述里说明）"));
        m_platformCombo->setFixedWidth(240);
        platformLayout->addWidget(m_platformCombo);
    }
    formLayout->addWidget(m_platformField);

    // 系统版本（选填）
    m_osVersionField = makeInputBlock(formCard, tr("系统版本（可选）"), &m_osVersionEdit,
                                      tr("例如 Windows 11 23H2 / Android 14"));
    m_osVersionEdit->setMaxLength(kOsVersionMaxLength);
    formLayout->addWidget(m_osVersionField);

    // 问题描述（必填）
    {
        QLabel *title = nullptr;
        QWidget *descField = makeFieldBlock(formCard, &title, tr("问题描述 *"), &m_descEdit,
                                            tr("说说发生了什么，涉及哪个页面、哪个实例，越具体越好。"), 90);
        m_descTitle = title;
        formLayout->addWidget(descField);
    }

    // 复现步骤（必填，与 GitHub 模板一样预填编号）
    m_stepsField = makeFieldBlock(formCard, nullptr, tr("复现步骤 *"), &m_stepsEdit,
                                  QString(), 90,
                                  tr("按顺序写下你点过哪些地方，方便照着再试一遍"));
    m_stepsEdit->setPlainText(QStringLiteral("1. \n2. \n3. "));
    formLayout->addWidget(m_stepsField);

    // 期望结果（选填）
    m_expectedField = makeFieldBlock(formCard, nullptr, tr("期望结果（可选）"), &m_expectedEdit,
                                     tr("例如：安装完成后自动打开新实例。"), 60);
    formLayout->addWidget(m_expectedField);

    // 实际结果（选填）
    m_actualField = makeFieldBlock(formCard, nullptr, tr("实际结果（可选）"), &m_actualEdit,
                                   tr("如果有报错弹窗，把提示文字原样贴进来。"), 60);
    formLayout->addWidget(m_actualField);

    // 日志（选填）
    m_logsField = makeFieldBlock(formCard, nullptr, tr("日志（可选）"), &m_logsEdit,
                                 tr("在「设置 → 高级设置 → 运行日志」里点「导出日志」；很长时贴最后几十行就可以。"), 80);
    formLayout->addWidget(m_logsField);

    // 截图 / 录屏（选填）
    m_screenshotsField = makeFieldBlock(formCard, nullptr, tr("截图 / 录屏（可选）"), &m_screenshotsEdit,
                                        tr("能直接说明问题的截图或录屏，可以贴图片链接或文字说明。"), 60);
    formLayout->addWidget(m_screenshotsField);

    // ── 功能建议字段 ──
    m_sceneField = makeFieldBlock(formCard, nullptr, tr("使用场景（可选）"), &m_sceneEdit,
                                  tr("什么时候会用到、能解决什么问题"), 80);
    formLayout->addWidget(m_sceneField);

    mainLayout->addWidget(formCard);

    // 提交行
    auto *submitCard = ForumKit::makeCard(m_mainHost);
    auto *submitLayout = new QHBoxLayout(submitCard);
    submitLayout->setContentsMargins(20, 12, 20, 12);
    submitLayout->setSpacing(10);
    m_statusLabel = new QLabel(submitCard);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    submitLayout->addWidget(m_statusLabel, 1);
    m_submitButton = new QPushButton(tr("提交反馈"), submitCard);
    m_submitButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_submitButton->setCursor(Qt::PointingHandCursor);
    m_submitButton->setFixedHeight(34);
    m_submitButton->setMinimumWidth(110);
    connect(m_submitButton, &QPushButton::clicked, this, &FeedbackPage::submit);
    submitLayout->addWidget(m_submitButton);
    mainLayout->addWidget(submitCard);

    // ── 我的反馈列表 ──
    auto *listHeadRow = new QHBoxLayout();
    listHeadRow->setSpacing(10);
    listHeadRow->addWidget(ForumKit::makeSectionTitle(m_mainHost, tr("我的反馈")));
    listHeadRow->addStretch(1);
    m_refreshButton = new QPushButton(tr("刷新"), m_mainHost);
    m_refreshButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setFixedHeight(28);
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() { loadPage(m_page); });
    listHeadRow->addWidget(m_refreshButton);
    mainLayout->addLayout(listHeadRow);

    m_listLayout = new QVBoxLayout();
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);
    mainLayout->addLayout(m_listLayout);

    m_pager = new ForumKit::ForumPager(m_mainHost);
    connect(m_pager, &ForumKit::ForumPager::pageRequested, this,
            [this](int page) { loadPage(page); });
    mainLayout->addWidget(m_pager);
    mainLayout->addStretch(1);

    layout->addWidget(m_loginHost);
    layout->addWidget(m_mainHost);

    // ── 全部反馈（仿 GitHub Issues 列表页，未登录也可见） ──
    m_publicHost = new QWidget(content);
    m_publicHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *publicLayout = new QVBoxLayout(m_publicHost);
    publicLayout->setContentsMargins(0, 16, 0, 0);
    publicLayout->setSpacing(10);

    auto *publicHeadRow = new QHBoxLayout();
    publicHeadRow->setSpacing(10);
    publicHeadRow->addWidget(ForumKit::makeSectionTitle(m_publicHost, tr("全部反馈")));
    publicHeadRow->addWidget(ForumKit::makeMuted(m_publicHost, tr("大家提交的反馈都在这里，点一条可以看详情")));
    publicHeadRow->addStretch(1);
    publicLayout->addLayout(publicHeadRow);

    auto *publicCard = ForumKit::makeCard(m_publicHost);
    auto *publicCardLayout = new QVBoxLayout(publicCard);
    publicCardLayout->setContentsMargins(0, 0, 0, 0);
    publicCardLayout->setSpacing(0);

    m_publicTabsLayout = new QHBoxLayout();
    m_publicTabsLayout->setContentsMargins(12, 10, 12, 10);
    m_publicTabsLayout->setSpacing(6);
    m_publicTabsLayout->addStretch(1);
    publicCardLayout->addLayout(m_publicTabsLayout);

    m_publicListLayout = new QVBoxLayout();
    m_publicListLayout->setContentsMargins(0, 0, 0, 0);
    m_publicListLayout->setSpacing(0);
    publicCardLayout->addLayout(m_publicListLayout);

    publicLayout->addWidget(publicCard);

    m_publicPager = new ForumKit::ForumPager(m_publicHost);
    connect(m_publicPager, &ForumKit::ForumPager::pageRequested, this,
            [this](int page) { loadPublic(page); });
    publicLayout->addWidget(m_publicPager);
    publicLayout->addStretch(1);

    layout->addWidget(m_publicHost);

    scroll->setWidget(content);
    outer->addWidget(scroll);

    applyType();
}

void FeedbackPage::open()
{
    updateState();
    loadPublic(1);

    // updateState 里刚自动拉过列表时（正在加载中）不重复拉
    if (ForumAccountService::instance()->isLoggedIn() && m_refreshButton && m_refreshButton->isEnabled())
        loadPage(1);
}

void FeedbackPage::updateState()
{
    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    m_mainHost->setVisible(loggedIn);
    m_loginHost->setVisible(!loggedIn);

    // 刚登录（或切换了账号）时自动加载该账号的反馈列表，从账户页返回即可见
    if (loggedIn && account->user().id != m_loadedUserId)
    {
        m_loadedUserId = account->user().id;
        setStatus(QString());
        loadPage(1);
    }
}

void FeedbackPage::applyType()
{
    const bool isBug = m_typeCombo->currentIndex() == 0;
    m_descTitle->setText(isBug ? tr("问题描述 *") : tr("建议内容 *"));
    m_descEdit->setPlaceholderText(isBug
                                       ? tr("说说发生了什么，涉及哪个页面、哪个实例，越具体越好。")
                                       : tr("想要什么功能，一句话说清。"));
    m_appVersionField->setVisible(isBug);
    m_platformField->setVisible(isBug);
    m_osVersionField->setVisible(isBug);
    m_stepsField->setVisible(isBug);
    m_expectedField->setVisible(isBug);
    m_actualField->setVisible(isBug);
    m_logsField->setVisible(isBug);
    m_screenshotsField->setVisible(isBug);
    m_sceneField->setVisible(!isBug);
}

void FeedbackPage::loadPage(int page)
{
    m_refreshButton->setEnabled(false);
    setStatus(tr("正在加载反馈列表…"));

    FeedbackService::instance()->fetchMyFeedbacks(
        page,
        [this](const QVector<FeedbackItem> &items, const ForumPagination &pagination) {
            m_refreshButton->setEnabled(true);
            setStatus(QString());
            m_loadedUserId = ForumAccountService::instance()->user().id;
            m_page = qMax(1, pagination.page);
            m_totalPages = qMax(1, pagination.pages);
            renderFeedbacks(items);
            m_pager->setPage(m_page, m_totalPages);
        },
        [this](const QString &message, int) {
            m_refreshButton->setEnabled(true);
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void FeedbackPage::renderFeedbacks(const QVector<FeedbackItem> &items)
{
    ForumKit::clearLayout(m_listLayout);

    if (items.isEmpty())
    {
        auto *emptyLabel =
            ForumKit::makeMuted(this, tr("还没有提交过反馈。遇到软件问题，就在上方写下来告诉我们。"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_listLayout->addSpacing(24);
        m_listLayout->addWidget(emptyLabel);
        m_listLayout->addSpacing(24);
        return;
    }

    for (const FeedbackItem &item : items)
    {
        auto *card = ForumKit::makeCard(this, QStringLiteral("forumThreadRow"));
        auto *rowLayout = new QVBoxLayout(card);
        rowLayout->setContentsMargins(14, 12, 14, 12);
        rowLayout->setSpacing(6);

        // 头行：类型 + 状态徽章 + 来源 + 时间
        auto *headRow = new QHBoxLayout();
        headRow->setSpacing(8);
        headRow->addWidget(makeNeutralBadge(card, item.type == QStringLiteral("feature")
                                                      ? tr("功能建议")
                                                      : tr("问题反馈")));
        if (item.status == QStringLiteral("resolved"))
            headRow->addWidget(ForumKit::makeBadge(card, tr("已处理")));
        else if (item.status == QStringLiteral("ignored"))
            headRow->addWidget(makeNeutralBadge(card, tr("已忽略")));
        else
            headRow->addWidget(ForumKit::makeBadge(card, tr("待处理"), true));

        QStringList headParts;
        if (item.source == QStringLiteral("launcher"))
            headParts << (item.appVersion.isEmpty()
                              ? tr("来自启动器")
                              : tr("来自启动器 v%1").arg(item.appVersion));
        else
            headParts << tr("来自网页版");
        auto *sourceLabel = ForumKit::makeMuted(card, headParts.join(QStringLiteral(" ")));
        headRow->addWidget(sourceLabel);
        headRow->addStretch(1);
        auto *timeLabel = ForumKit::makeMuted(card, ForumKit::relativeTime(item.createdAt));
        headRow->addWidget(timeLabel);
        rowLayout->addLayout(headRow);

        // 反馈正文（### 模板标题渲染成加粗）
        auto *contentLabel = new QLabel(renderContent(item.content), card);
        contentLabel->setWordWrap(true);
        contentLabel->setTextFormat(Qt::RichText);
        contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rowLayout->addWidget(contentLabel);

        // 管理员回复
        if (!item.reply.isEmpty())
        {
            auto *replyLabel = new QLabel(tr("管理员回复：%1").arg(item.reply), card);
            replyLabel->setObjectName(QStringLiteral("forumMuted"));
            replyLabel->setWordWrap(true);
            replyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            replyLabel->setStyleSheet(StyleKit::resolve(QStringLiteral(
                "QLabel{background:@BG_MUTED@;border:1px solid @BORDER@;"
                "border-radius:10px;padding:9px 12px;}")));
            rowLayout->addWidget(replyLabel);
        }

        m_listLayout->addWidget(card);
    }
}

void FeedbackPage::submit()
{
    const bool isBug = m_typeCombo->currentIndex() == 0;

    FeedbackDraft draft;
    draft.type = isBug ? QStringLiteral("bug") : QStringLiteral("feature");
    draft.description = m_descEdit->toPlainText().trimmed();

    if (draft.description.size() < kDescMinLength)
    {
        NotificationManager::showInfo(this, isBug
                                                ? tr("问题描述至少要 %1 个字").arg(kDescMinLength)
                                                : tr("建议内容至少要 %1 个字").arg(kDescMinLength));
        m_descEdit->setFocus();
        return;
    }
    if (draft.description.size() > kDescMaxLength)
    {
        NotificationManager::showInfo(this, tr("描述最多 %1 个字").arg(kDescMaxLength));
        return;
    }

    if (isBug)
    {
        draft.appVersion = m_appVersionEdit->text().trimmed();
        if (draft.appVersion.isEmpty())
        {
            NotificationManager::showInfo(this, tr("启动器版本不能为空：在「设置 → 关于」里能看到"));
            m_appVersionEdit->setFocus();
            return;
        }
        draft.platform = m_platformCombo->currentText();
        draft.osVersion = m_osVersionEdit->text().trimmed();
        draft.steps = m_stepsEdit->toPlainText().trimmed();
        if (draft.steps.isEmpty())
        {
            NotificationManager::showInfo(this, tr("复现步骤不能为空：按顺序写下你点过哪些地方"));
            m_stepsEdit->setFocus();
            return;
        }
        if (draft.steps.size() > kStepsMaxLength)
        {
            NotificationManager::showInfo(this, tr("复现步骤最多 %1 个字").arg(kStepsMaxLength));
            return;
        }
        draft.expected = m_expectedEdit->toPlainText().trimmed();
        draft.actual = m_actualEdit->toPlainText().trimmed();
        draft.logs = m_logsEdit->toPlainText().trimmed();
        draft.screenshots = m_screenshotsEdit->toPlainText().trimmed();
        if (draft.expected.size() > kExpectedMaxLength)
        {
            NotificationManager::showInfo(this, tr("期望结果最多 %1 个字").arg(kExpectedMaxLength));
            return;
        }
        if (draft.actual.size() > kActualMaxLength)
        {
            NotificationManager::showInfo(this, tr("实际结果最多 %1 个字").arg(kActualMaxLength));
            return;
        }
        if (draft.logs.size() > kLogsMaxLength)
        {
            NotificationManager::showInfo(this, tr("日志最多 %1 个字").arg(kLogsMaxLength));
            return;
        }
        if (draft.screenshots.size() > kScreenshotsMaxLength)
        {
            NotificationManager::showInfo(this, tr("截图 / 录屏最多 %1 个字").arg(kScreenshotsMaxLength));
            return;
        }
    }
    else
    {
        draft.scene = m_sceneEdit->toPlainText().trimmed();
        if (draft.scene.size() > kSceneMaxLength)
        {
            NotificationManager::showInfo(this, tr("使用场景最多 %1 个字").arg(kSceneMaxLength));
            return;
        }
    }

    m_submitButton->setEnabled(false);
    setStatus(tr("正在提交…"));

    FeedbackService::instance()->submitFeedback(
        draft,
        [this](int) {
            m_submitButton->setEnabled(true);
            m_descEdit->clear();
            m_stepsEdit->setPlainText(QStringLiteral("1. \n2. \n3. "));
            m_expectedEdit->clear();
            m_actualEdit->clear();
            m_logsEdit->clear();
            m_screenshotsEdit->clear();
            m_sceneEdit->clear();
            setStatus(QString());
            NotificationManager::showInfo(this, tr("反馈已提交，感谢你的反馈！处理进度会在下方列表更新。"));
            loadPage(1);
            loadPublic(1);
        },
        [this](const QString &message, int) {
            m_submitButton->setEnabled(true);
            setStatus(message, true);
        });
}

void FeedbackPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}

void FeedbackPage::loadPublic(int page)
{
    FeedbackService::instance()->fetchPublicFeedbacks(
        m_publicStatus, page,
        [this, page](const QVector<PublicFeedbackItem> &items, const FeedbackCounts &counts,
                     const ForumPagination &pagination) {
            m_publicCounts = counts;
            m_publicPage = qMax(1, pagination.page);
            m_publicTotalPages = qMax(1, pagination.pages);
            renderPublicTabs();
            renderPublic(items);
            m_publicPager->setPage(m_publicPage, m_publicTotalPages);
        },
        [this](const QString &message, int) {
            ForumKit::clearLayout(m_publicListLayout);
            auto *errorLabel = ForumKit::makeMuted(this, tr("加载失败：%1").arg(message));
            errorLabel->setAlignment(Qt::AlignCenter);
            m_publicListLayout->addSpacing(20);
            m_publicListLayout->addWidget(errorLabel);
            m_publicListLayout->addSpacing(20);
        });
}

void FeedbackPage::renderPublicTabs()
{
    ForumKit::clearLayout(m_publicTabsLayout);
    m_publicTabButtons.clear();

    const QVector<QPair<QString, QString>> tabs = {
        {QStringLiteral("all"),
         tr("全部 %1").arg(m_publicCounts.open + m_publicCounts.resolved + m_publicCounts.ignored)},
        {QStringLiteral("open"), tr("待处理 %1").arg(m_publicCounts.open)},
        {QStringLiteral("resolved"), tr("已处理 %1").arg(m_publicCounts.resolved)},
        {QStringLiteral("ignored"), tr("已忽略 %1").arg(m_publicCounts.ignored)},
    };
    for (const auto &tab : tabs)
    {
        auto *button = new QPushButton(tab.second, m_publicHost);
        button->setObjectName(QStringLiteral("forumSortButton"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(tab.first == m_publicStatus);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(26);
        const QString status = tab.first;
        connect(button, &QPushButton::clicked, this, [this, status]() {
            if (m_publicStatus == status)
                return;
            m_publicStatus = status;
            loadPublic(1);
        });
        m_publicTabButtons.append(button);
        m_publicTabsLayout->addWidget(button);
    }
    m_publicTabsLayout->addStretch(1);
}

void FeedbackPage::renderPublic(const QVector<PublicFeedbackItem> &items)
{
    ForumKit::clearLayout(m_publicListLayout);

    if (items.isEmpty())
    {
        auto *emptyLabel = ForumKit::makeMuted(this, tr("还没有人提交过反馈，来当第一个吧。"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_publicListLayout->addSpacing(24);
        m_publicListLayout->addWidget(emptyLabel);
        m_publicListLayout->addSpacing(24);
        return;
    }

    for (const PublicFeedbackItem &item : items)
    {
        auto *card = new ForumKit::ClickableCard(this);
        card->setObjectName(QStringLiteral("forumThreadRow"));
        auto *rowLayout = new QVBoxLayout(card);
        rowLayout->setContentsMargins(14, 12, 14, 12);
        rowLayout->setSpacing(5);

        // 标题行：状态胶囊 + 标题 + 类型徽章
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(8);
        titleRow->addWidget(makeStatePill(card, item.status));
        auto *title = new ForumKit::ElidedLabel(card);
        title->setObjectName(QStringLiteral("forumRowTitle"));
        title->setFullText(item.title);
        title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        titleRow->addWidget(title, 1);
        titleRow->addWidget(makeNeutralBadge(
            card, item.type == QStringLiteral("feature") ? tr("功能建议") : tr("问题反馈")));
        rowLayout->addLayout(titleRow);

        // 元信息行（仿 GitHub：#编号 由某人提交 · 时间）
        QStringList meta;
        meta << QStringLiteral("#%1").arg(item.id);
        meta << (item.authorName.isEmpty() ? tr("由已注销用户提交")
                                           : tr("由 %1 提交").arg(item.authorName));
        meta << ForumKit::relativeTime(item.createdAt);
        if (item.handledAt > 0)
            meta << (item.status == QStringLiteral("ignored") ? tr("已忽略") : tr("已处理"));
        rowLayout->addWidget(ForumKit::makeMuted(card, meta.join(QStringLiteral(" · "))));

        // 详情（点行展开 / 收起）
        auto *detail = new QWidget(card);
        detail->setAttribute(Qt::WA_StyledBackground, false);
        detail->setVisible(false);
        auto *detailLayout = new QVBoxLayout(detail);
        detailLayout->setContentsMargins(0, 8, 0, 0);
        detailLayout->setSpacing(6);
        auto *contentLabel = new QLabel(renderContent(item.content), detail);
        contentLabel->setWordWrap(true);
        contentLabel->setTextFormat(Qt::RichText);
        contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        detailLayout->addWidget(contentLabel);
        if (!item.reply.isEmpty())
        {
            auto *replyLabel = new QLabel(tr("管理员回复：%1").arg(item.reply), detail);
            replyLabel->setObjectName(QStringLiteral("forumMuted"));
            replyLabel->setWordWrap(true);
            replyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            replyLabel->setStyleSheet(StyleKit::resolve(QStringLiteral(
                "QLabel{background:@BG_MUTED@;border:1px solid @BORDER@;"
                "border-radius:10px;padding:9px 12px;}")));
            detailLayout->addWidget(replyLabel);
        }
        rowLayout->addWidget(detail);

        card->onClick = [detail]() { detail->setVisible(!detail->isVisible()); };

        m_publicListLayout->addWidget(card);
    }
}
