/**
 * @file   FeedbackPage.cpp
 * @brief  软件内反馈页实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "FeedbackPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextCursor>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "utils/AppVersion.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"

namespace {
constexpr int kContentMinLength = 5;
constexpr int kContentLimit = 1000;

/** 问题反馈模板：自动带上当前启动器版本，省得用户手填 */
QString bugTemplate()
{
    return QStringLiteral(
               "【问题】一句话说清遇到了什么：\n\n"
               "【复现步骤】\n1. \n2. \n\n"
               "【期望】正常应该是：\n\n"
               "【实际】结果是：\n\n"
               "【环境】系统与实例版本（如 Windows 11 + 1.20.1）：\n"
               "启动器版本：%1")
        .arg(AppVersion::current());
}

/** 功能建议模板 */
QString featureTemplate()
{
    return QStringLiteral(
        "【建议】想要的功能：\n\n"
        "【场景】什么时候会用到、能解决什么问题：\n\n"
        "【参考】其他启动器或软件有没有类似做法：");
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
    formLayout->setSpacing(8);

    formLayout->addWidget(ForumKit::makeSectionTitle(formCard, tr("提交反馈")));
    formLayout->addWidget(ForumKit::makeMuted(
        formCard, tr("遇到启动失败、实例异常这类问题，在这里写清楚现象和版本号，我们会在后台看到并处理。")));

    // 模板按钮行：一键套格式，照着填就行
    auto *tplRow = new QHBoxLayout();
    tplRow->setSpacing(6);
    const QList<QPair<QString, QString>> templates = {
        {QStringLiteral("bug"), tr("🐛 问题反馈模板")},
        {QStringLiteral("feature"), tr("💡 功能建议模板")},
    };
    for (const auto &item : templates)
    {
        auto *tplButton = new QPushButton(item.second, formCard);
        tplButton->setObjectName(QStringLiteral("forumGhostButton"));
        tplButton->setCursor(Qt::PointingHandCursor);
        tplButton->setFixedHeight(26);
        const QString tpl = item.first == QStringLiteral("bug") ? bugTemplate() : featureTemplate();
        connect(tplButton, &QPushButton::clicked, this,
                [this, tpl]() { applyTemplate(tpl); });
        tplRow->addWidget(tplButton);
    }
    auto *tplHint = ForumKit::makeMuted(formCard, tr("点模板快速套格式"));
    tplRow->addWidget(tplHint);
    tplRow->addStretch(1);
    formLayout->addLayout(tplRow);

    m_contentEdit = new QPlainTextEdit(formCard);
    m_contentEdit->setObjectName(QStringLiteral("forumReplyEdit"));
    m_contentEdit->setPlaceholderText(
        tr("点上方模板快速套格式，或直接描述遇到的问题；涉及实例或版本时请写明版本号。"));
    m_contentEdit->setMinimumHeight(140);
    m_contentEdit->setMaximumHeight(220);
    formLayout->addWidget(m_contentEdit);

    auto *countRow = new QHBoxLayout();
    countRow->addStretch(1);
    m_countLabel = ForumKit::makeMuted(formCard, tr("0/%1 字").arg(kContentLimit));
    countRow->addWidget(m_countLabel);
    formLayout->addLayout(countRow);

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

    connect(m_contentEdit, &QPlainTextEdit::textChanged, this, [this]() {
        m_countLabel->setText(tr("%1/%2 字").arg(m_contentEdit->toPlainText().size()).arg(kContentLimit));
    });

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void FeedbackPage::open()
{
    updateState();

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

        // 头行：状态徽章 + 来源 + 时间
        auto *headRow = new QHBoxLayout();
        headRow->setSpacing(8);
        if (item.status == QStringLiteral("resolved"))
            headRow->addWidget(ForumKit::makeBadge(card, tr("已处理")));
        else if (item.status == QStringLiteral("ignored"))
        {
            QLabel *ignoredBadge = ForumKit::makeBadge(card, tr("已忽略"));
            ignoredBadge->setStyleSheet(StyleKit::resolve(QStringLiteral(
                "QLabel{background:@BG_MUTED@;color:@TEXT_TERTIARY@;border:none;"
                "border-radius:8px;font-size:11px;font-weight:600;padding:1px 8px;}")));
            headRow->addWidget(ignoredBadge);
        }
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

        // 反馈正文
        auto *contentLabel = new QLabel(item.content, card);
        contentLabel->setWordWrap(true);
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
    const QString content = m_contentEdit->toPlainText().trimmed();
    if (content.size() < kContentMinLength)
    {
        NotificationManager::showInfo(this, tr("反馈内容至少要 %1 个字").arg(kContentMinLength));
        m_contentEdit->setFocus();
        return;
    }
    if (content.size() > kContentLimit)
    {
        NotificationManager::showInfo(this, tr("反馈内容最多 %1 个字").arg(kContentLimit));
        return;
    }

    m_submitButton->setEnabled(false);
    setStatus(tr("正在提交…"));

    FeedbackService::instance()->submitFeedback(
        content,
        [this](int) {
            m_submitButton->setEnabled(true);
            m_contentEdit->clear();
            setStatus(QString());
            NotificationManager::showInfo(this, tr("反馈已提交，感谢你的反馈！处理进度会在下方列表更新。"));
            loadPage(1);
        },
        [this](const QString &message, int) {
            m_submitButton->setEnabled(true);
            setStatus(message, true);
        });
}

void FeedbackPage::applyTemplate(const QString &tpl)
{
    const QString current = m_contentEdit->toPlainText().trimmed();
    if (!current.isEmpty() && current != tpl)
    {
        const auto answer = QMessageBox::question(this, tr("套用模板"),
            tr("套用模板会替换当前已填写的内容，确定吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    m_contentEdit->setPlainText(tpl);
    m_contentEdit->moveCursor(QTextCursor::End);
    m_contentEdit->setFocus();
}

void FeedbackPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
