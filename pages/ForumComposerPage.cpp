/**
 * @file   ForumComposerPage.cpp
 * @brief  论坛发帖页实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumComposerPage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "components/ForumKit.h"
#include "components/QuizDialog.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"

namespace {
constexpr int kMaxTags = 4;
constexpr int kMaxTagLength = 12;
constexpr int kTitleLimit = 80;
constexpr int kContentLimit = 20000;
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

    auto *formCard = ForumKit::makeCard(m_formHost);
    auto *cardLayout = new QVBoxLayout(formCard);
    cardLayout->setContentsMargins(20, 16, 20, 16);
    cardLayout->setSpacing(8);

    auto *titleLabel = ForumKit::makeSectionTitle(formCard, tr("发表新主题"));
    cardLayout->addWidget(titleLabel);

    // 板块
    cardLayout->addWidget(ForumKit::makeMuted(formCard, tr("板块")));
    m_boardCombo = new QComboBox(formCard);
    m_boardCombo->setObjectName(QStringLiteral("forumField"));
    cardLayout->addWidget(m_boardCombo);

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

    // 正文 + 预览切换
    auto *contentHeaderRow = new QHBoxLayout();
    contentHeaderRow->addWidget(ForumKit::makeMuted(formCard, tr("正文（支持 Markdown，可粘贴图片链接）")));
    contentHeaderRow->addStretch(1);
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
}

void ForumComposerPage::updateState()
{
    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool quizPassed = loggedIn && account->hasPassedQuiz();
    m_formHost->setVisible(quizPassed);
    m_quizHost->setVisible(loggedIn && !quizPassed);
    m_loginHost->setVisible(!loggedIn);
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
    m_titleEdit->clear();
    m_tagsEdit->clear();
    m_contentEdit->clear();
    m_previewBrowser->setPostMarkdown(QString());
    if (m_previewButton->isChecked())
        m_previewButton->setChecked(false);
    m_statusLabel->clear();

    updateState();

    if (m_formHost->isVisible())
    {
        loadBoards();
        m_titleEdit->setFocus();
    }
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

void ForumComposerPage::setPreviewMode(bool preview)
{
    if (preview)
        m_previewBrowser->setPostMarkdown(m_contentEdit->toPlainText());
    else
        m_previewButton->setText(tr("预览"));
    m_previewButton->setText(preview ? tr("继续编辑") : tr("预览"));
    m_contentStack->setCurrentIndex(preview ? 1 : 0);
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

    const QString boardSlug = m_boardCombo->currentData().toString();

    m_submitButton->setEnabled(false);
    m_statusLabel->setText(tr("正在发布…"));
    m_statusLabel->setStyleSheet(QString());

    ForumContentService::instance()->createThread(
        boardSlug, title, content, tags,
        [this](int threadId) {
            m_submitButton->setEnabled(true);
            m_statusLabel->clear();
            if (threadId > 0)
                emit threadCreated(threadId);
        },
        [this](const QString &message, int) {
            m_submitButton->setEnabled(true);
            m_statusLabel->setText(message);
            m_statusLabel->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;")));
        });
}
