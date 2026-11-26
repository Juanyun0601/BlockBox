/**
 * @file   ForumClaimDialog.cpp
 * @brief  转载认领弹窗实现
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "ForumClaimDialog.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

#include "AppInputDialog.h"
#include "AppMessageBox.h"
#include "ForumKit.h"
#include "NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/ThemeManager.h"

namespace {

constexpr int kCardWidth = 560;

/** 认领状态的中文说法 */
QString statusTextOf(const QString &status)
{
    if (status == QStringLiteral("pending"))
        return QObject::tr("等待作者确认");
    if (status == QStringLiteral("approved"))
        return QObject::tr("已转移作品");
    if (status == QStringLiteral("rejected"))
        return QObject::tr("已被拒绝");
    if (status == QStringLiteral("cancelled"))
        return QObject::tr("已撤回");
    return QString();
}

/** 状态胶囊配色：待处理琥珀、成功绿色、拒绝红色、撤回灰色 */
QString chipStyleOf(const QString &status, bool light)
{
    QString bg;
    QString fg;
    if (status == QStringLiteral("pending"))
    {
        bg = light ? QStringLiteral("#FEF3C7") : QStringLiteral("#4a3a12");
        fg = light ? QStringLiteral("#92400E") : QStringLiteral("#FCD34D");
    }
    else if (status == QStringLiteral("approved"))
    {
        bg = light ? QStringLiteral("#DCFCE7") : QStringLiteral("#123524");
        fg = light ? QStringLiteral("#166534") : QStringLiteral("#86EFAC");
    }
    else if (status == QStringLiteral("rejected"))
    {
        bg = light ? QStringLiteral("#FEE2E2") : QStringLiteral("#3f1717");
        fg = light ? QStringLiteral("#B91C1C") : QStringLiteral("#FCA5A5");
    }
    else
    {
        bg = light ? QStringLiteral("#F1F5F9") : QStringLiteral("#2e3238");
        fg = light ? QStringLiteral("#475569") : QStringLiteral("#aab3bf");
    }
    return QStringLiteral("QLabel { background: %1; color: %2; border-radius: 9px;"
                          " padding: 2px 9px; font-size: 11.5px; font-weight: 600; }")
        .arg(bg, fg);
}

/** 双方都显示昵称，没有昵称退回账号名 */
QString nameOf(const ForumUser &user)
{
    return user.displayName.isEmpty() ? user.username : user.displayName;
}

} // namespace

// ═══════════════════════════════════════════════════ 认领沟通

ForumClaimChatDialog::ForumClaimChatDialog(int claimId, QWidget *parent)
    : AppDialogBase(parent)
    , m_claimId(claimId)
{
    setObjectName(QStringLiteral("forumClaimChatDialog"));
    buildUi();
    applyStyle();
}

void ForumClaimChatDialog::showDialog(QWidget *parent, int claimId,
                                std::function<void(bool approved)> onHandled)
{
    if (claimId <= 0)
        return;
    auto *dialog = new ForumClaimChatDialog(claimId, parent);
    dialog->m_onHandled = std::move(onHandled);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    dialog->load();
}

void ForumClaimChatDialog::buildUi()
{
    auto *main = new QVBoxLayout(this);
    main->setContentsMargins(18, 18, 18, 18);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("forumClaimCard"));
    card->setFixedWidth(kCardWidth);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);

    // ── 标题行 ──
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    m_titleLabel = new QLabel(tr("认领沟通"), card);
    m_titleLabel->setObjectName(QStringLiteral("forumClaimTitle"));
    titleRow->addWidget(m_titleLabel, 1);
    m_statusChip = new QLabel(card);
    m_statusChip->setObjectName(QStringLiteral("forumClaimChip"));
    titleRow->addWidget(m_statusChip, 0, Qt::AlignVCenter);
    layout->addLayout(titleRow);

    // ── 作品与双方信息 ──
    m_peopleLabel = new QLabel(card);
    m_peopleLabel->setObjectName(QStringLiteral("forumClaimMeta"));
    m_peopleLabel->setWordWrap(true);
    layout->addWidget(m_peopleLabel);

    m_claimMessageLabel = new QLabel(card);
    m_claimMessageLabel->setObjectName(QStringLiteral("forumClaimNote"));
    m_claimMessageLabel->setWordWrap(true);
    layout->addWidget(m_claimMessageLabel);

    // ── 沟通记录 ──
    m_scroll = new QScrollArea(card);
    m_scroll->setObjectName(QStringLiteral("forumClaimScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setMinimumHeight(180);
    m_scroll->setMaximumHeight(300);
    m_scroll->viewport()->setAutoFillBackground(false);

    m_messagesHost = new QWidget(m_scroll);
    m_messagesHost->setObjectName(QStringLiteral("forumClaimMessages"));
    m_messagesHost->setAttribute(Qt::WA_StyledBackground, false);
    m_messagesLayout = new QVBoxLayout(m_messagesHost);
    m_messagesLayout->setContentsMargins(2, 2, 2, 2);
    m_messagesLayout->setSpacing(6);
    m_messagesLayout->addStretch(1);
    m_scroll->setWidget(m_messagesHost);
    layout->addWidget(m_scroll);

    // ── 输入框 + 发送 ──
    m_input = new QPlainTextEdit(card);
    m_input->setObjectName(QStringLiteral("forumClaimInput"));
    m_input->setPlaceholderText(tr("和对方说点什么…（Ctrl+Enter 发送）"));
    m_input->setFixedHeight(64);
    m_input->setMaximumBlockCount(20);
    layout->addWidget(m_input);

    auto *sendRow = new QHBoxLayout();
    sendRow->addStretch(1);
    m_sendButton = new QPushButton(tr("发送"), card);
    m_sendButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setFixedHeight(30);
    connect(m_sendButton, &QPushButton::clicked, this, &ForumClaimChatDialog::sendMessage);
    sendRow->addWidget(m_sendButton);
    layout->addLayout(sendRow);

    // ── 处理按钮（作者同意 / 拒绝，认领人撤回） ──
    m_actionsRow = new QWidget(card);
    auto *actions = new QHBoxLayout(m_actionsRow);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    m_approveButton = new QPushButton(tr("同意转移作品"), m_actionsRow);
    m_approveButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_approveButton->setCursor(Qt::PointingHandCursor);
    m_approveButton->setFixedHeight(32);
    connect(m_approveButton, &QPushButton::clicked, this, &ForumClaimChatDialog::approveClaim);
    actions->addWidget(m_approveButton);
    m_rejectButton = new QPushButton(tr("拒绝认领"), m_actionsRow);
    m_rejectButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_rejectButton->setCursor(Qt::PointingHandCursor);
    m_rejectButton->setFixedHeight(32);
    connect(m_rejectButton, &QPushButton::clicked, this, &ForumClaimChatDialog::rejectClaim);
    actions->addWidget(m_rejectButton);
    m_cancelButton = new QPushButton(tr("撤回认领"), m_actionsRow);
    m_cancelButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setFixedHeight(32);
    connect(m_cancelButton, &QPushButton::clicked, this, &ForumClaimChatDialog::cancelClaim);
    actions->addWidget(m_cancelButton);
    actions->addStretch(1);
    layout->addWidget(m_actionsRow);

    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("forumClaimStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    layout->addWidget(m_statusLabel);

    // ── 关闭 ──
    auto *closeRow = new QHBoxLayout();
    closeRow->addStretch(1);
    auto *closeButton = new QPushButton(tr("关闭"), card);
    closeButton->setObjectName(QStringLiteral("forumGhostButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedHeight(30);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    closeRow->addWidget(closeButton);
    layout->addLayout(closeRow);

    main->addWidget(card, 0, Qt::AlignCenter);

    auto *shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), this);
    connect(shortcut, &QShortcut::activated, this, &ForumClaimChatDialog::sendMessage);
    auto *shortcut2 = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Enter")), this);
    connect(shortcut2, &QShortcut::activated, this, &ForumClaimChatDialog::sendMessage);
}

void ForumClaimChatDialog::applyStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool light = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg = light ? QStringLiteral("rgba(255,255,255,250)")
                                 : QStringLiteral("rgba(44,44,48,250)");
    const QString border = light ? QStringLiteral("#dcdcdc") : QStringLiteral("#5a5a60");
    const QString titleColor = light ? QStringLiteral("#1a1a1a") : QStringLiteral("#f2f2f2");
    const QString textColor = light ? QStringLiteral("#333333") : QStringLiteral("#e8e8e8");
    const QString mutedColor = light ? QStringLiteral("#64748B") : QStringLiteral("#9aa3b2");
    const QString fieldBg = light ? QStringLiteral("#f4f4f4") : QStringLiteral("#3b3b40");
    const QString fieldBorder = light ? QStringLiteral("#d4d4d4") : QStringLiteral("#55555a");
    const QString hoverBg = light ? QStringLiteral("rgba(15,23,42,0.06)")
                                  : QStringLiteral("rgba(255,255,255,0.08)");

    setStyleSheet(
        QStringLiteral(
            "QDialog { background-color: transparent; border: none; }"
            "QFrame#forumClaimCard { background-color: %1; border: 1px solid %2;"
            " border-radius: 18px; }"
            "QLabel#forumClaimTitle { color: %3; font-size: 15px; font-weight: 700;"
            " background: transparent; }"
            "QLabel#forumClaimMeta, QLabel#forumClaimNote, QLabel#forumClaimStatus"
            " { color: %4; font-size: 12.5px; background: transparent; }"
            "QLabel#forumClaimNote { color: %5; font-size: 12px; }"
            "QScrollArea#forumClaimScroll { background: transparent; border: none; }"
            "QWidget#forumClaimMessages { background: transparent; }"
            "QLabel#forumClaimMsgHead { color: %5; font-size: 11.5px; background: transparent; }"
            "QFrame#forumClaimMsgRow { background: %6; border: 1px solid %7;"
            " border-radius: 10px; }"
            "QLabel#forumClaimMsgText { color: %8; font-size: 13px; background: transparent; }"
            "QPlainTextEdit#forumClaimInput { background: %6; color: %8;"
            " border: 1px solid %7; border-radius: 10px; padding: 6px 8px;"
            " font-size: 13px; }"
            "QPlainTextEdit#forumClaimInput:focus { border-color: %9; }"
        )
            .arg(cardBg, border, titleColor, mutedColor, mutedColor, fieldBg, fieldBorder,
                 textColor, tm->currentThemeColor()));
}

void ForumClaimChatDialog::load()
{
    if (m_loading || m_claimId <= 0)
        return;
    m_loading = true;
    setStatus(tr("正在读取认领…"));
    const int claimId = m_claimId;
    ForumContentService::instance()->fetchClaim(
        claimId,
        [this, claimId](const ForumClaimDetail &detail) {
            m_loading = false;
            if (claimId != m_claimId)
                return;
            m_detail = detail;
            setStatus(QString());
            renderClaim();
            renderMessages();
        },
        [this, claimId](const QString &message, int) {
            m_loading = false;
            if (claimId != m_claimId)
                return;
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void ForumClaimChatDialog::renderClaim()
{
    const ForumClaimInfo &claim = m_detail.claim;
    m_titleLabel->setText(tr("认领沟通 · %1").arg(claim.threadTitle));
    m_statusChip->setText(statusTextOf(claim.status));
    const bool light = (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme);
    m_statusChip->setStyleSheet(chipStyleOf(claim.status, light));

    m_peopleLabel->setText(tr("认领人：%1　·　转载作者：%2")
                               .arg(nameOf(claim.claimant), nameOf(claim.author)));
    if (!claim.message.isEmpty())
    {
        m_claimMessageLabel->setText(tr("认领说明：%1").arg(claim.message));
        m_claimMessageLabel->show();
    }
    else
    {
        m_claimMessageLabel->hide();
    }

    const bool pending = claim.isPending();
    const bool isAuthor = m_detail.myRole == QStringLiteral("author");
    const bool isClaimant = m_detail.myRole == QStringLiteral("claimant");

    m_input->setEnabled(pending);
    m_sendButton->setEnabled(pending);
    if (!pending)
        m_input->setPlaceholderText(tr("这张认领单已经处理完了，只能查看记录"));

    m_actionsRow->setVisible(pending && (isAuthor || isClaimant));
    m_approveButton->setVisible(pending && isAuthor);
    m_rejectButton->setVisible(pending && isAuthor);
    m_cancelButton->setVisible(pending && isClaimant);
}

void ForumClaimChatDialog::renderMessages()
{
    ForumKit::clearLayout(m_messagesLayout);
    QWidget *host = m_messagesHost;
    if (m_detail.messages.isEmpty())
    {
        auto *empty = ForumKit::makeMuted(host, tr("还没有沟通记录，可以在这里和对方说明情况"));
        empty->setAlignment(Qt::AlignCenter);
        m_messagesLayout->addWidget(empty);
    }
    else
    {
        for (const ForumClaimMessage &message : m_detail.messages)
        {
            auto *row = new QFrame(host);
            row->setObjectName(QStringLiteral("forumClaimMsgRow"));
            auto *rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(10, 7, 10, 8);
            rowLayout->setSpacing(2);
            const QString name = nameOf(message.sender);
            auto *head = new QLabel(tr("%1 · %2").arg(
                                        name, ForumKit::relativeTime(message.createdAt)),
                                    row);
            head->setObjectName(QStringLiteral("forumClaimMsgHead"));
            rowLayout->addWidget(head);
            auto *text = new QLabel(message.content, row);
            text->setObjectName(QStringLiteral("forumClaimMsgText"));
            text->setWordWrap(true);
            rowLayout->addWidget(text);
            m_messagesLayout->addWidget(row);
        }
    }
    m_messagesLayout->addStretch(1);

    QTimer::singleShot(0, this, [this]() {
        if (m_scroll && m_scroll->verticalScrollBar())
            m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
}

void ForumClaimChatDialog::sendMessage()
{
    if (m_busy || m_claimId <= 0 || !m_detail.claim.isPending())
        return;
    const QString content = m_input->toPlainText().trimmed();
    if (content.isEmpty())
    {
        setStatus(tr("先写点内容再发送"), true);
        return;
    }
    if (content.size() > 500)
    {
        setStatus(tr("消息最多 500 个字"), true);
        return;
    }

    setBusy(true);
    setStatus(tr("正在发送…"));
    ForumContentService::instance()->sendClaimMessage(
        m_claimId, content,
        [this]() {
            m_input->clear();
            setBusy(false);
            setStatus(QString());
            load();
        },
        [this](const QString &message, int) {
            setBusy(false);
            setStatus(tr("发送失败：%1").arg(message), true);
        });
}

void ForumClaimChatDialog::approveClaim()
{
    if (m_busy || !m_detail.claim.isPending())
        return;
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("转移作品"),
        tr("确定把《%1》转移给 %2 吗？\n\n转移后作品归对方所有，转载标记和原链接会被去掉；"
           "这个操作不可撤销。")
            .arg(m_detail.claim.threadTitle, nameOf(m_detail.claim.claimant)),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    setBusy(true);
    setStatus(tr("正在转移作品…"));
    ForumContentService::instance()->approveClaim(
        m_claimId,
        [this]() {
            setBusy(false);
            NotificationManager::showSuccess(this, tr("作品已转移给认领人"));
            const auto callback = m_onHandled;
            if (callback)
                callback(true);
            accept();
        },
        [this](const QString &message, int) {
            setBusy(false);
            setStatus(tr("转移失败：%1").arg(message), true);
        });
}

void ForumClaimChatDialog::rejectClaim()
{
    if (m_busy || !m_detail.claim.isPending())
        return;
    bool ok = false;
    const QString reason = AppInputDialog::getText(
        this, tr("拒绝认领"), tr("写一句拒绝理由（可留空，会记进沟通记录）"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;

    setBusy(true);
    setStatus(tr("正在处理…"));
    ForumContentService::instance()->rejectClaim(
        m_claimId, reason.trimmed(),
        [this]() {
            setBusy(false);
            NotificationManager::showInfo(this, tr("已拒绝这次认领"));
            const auto callback = m_onHandled;
            if (callback)
                callback(false);
            accept();
        },
        [this](const QString &message, int) {
            setBusy(false);
            setStatus(tr("处理失败：%1").arg(message), true);
        });
}

void ForumClaimChatDialog::cancelClaim()
{
    if (m_busy || !m_detail.claim.isPending())
        return;
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("撤回认领"), tr("确定撤回这次认领吗？撤回后可以重新提交。"),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    setBusy(true);
    setStatus(tr("正在撤回…"));
    ForumContentService::instance()->cancelClaim(
        m_claimId,
        [this]() {
            setBusy(false);
            NotificationManager::showInfo(this, tr("已撤回认领"));
            const auto callback = m_onHandled;
            if (callback)
                callback(false);
            accept();
        },
        [this](const QString &message, int) {
            setBusy(false);
            setStatus(tr("撤回失败：%1").arg(message), true);
        });
}

void ForumClaimChatDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_sendButton->setEnabled(!busy && m_detail.claim.isPending());
    m_approveButton->setEnabled(!busy);
    m_rejectButton->setEnabled(!busy);
    m_cancelButton->setEnabled(!busy);
}

void ForumClaimChatDialog::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
    m_statusLabel->setStyleSheet(
        error ? QStringLiteral("color: #dc2626; font-size: 12px; background: transparent;")
              : QString());
}

// ═══════════════════════════════════════════════════ 提交认领

ForumClaimSubmitDialog::ForumClaimSubmitDialog(int threadId, const QString &threadTitle,
                                               const QString &authorName, QWidget *parent)
    : AppDialogBase(parent)
    , m_threadId(threadId)
    , m_threadTitle(threadTitle)
    , m_authorName(authorName)
{
    setObjectName(QStringLiteral("forumClaimSubmitDialog"));
    buildUi();
    applyStyle();
}

void ForumClaimSubmitDialog::request(QWidget *parent, const ForumThreadInfo &thread,
                                     std::function<void()> onSubmitted)
{
    if (thread.id <= 0 || !thread.source.isRepost())
        return;
    ForumClaimSubmitDialog dialog(thread.id, thread.title, thread.authorName, parent);
    dialog.exec();
    if (dialog.m_submitted && onSubmitted)
        onSubmitted();
}

void ForumClaimSubmitDialog::buildUi()
{
    auto *main = new QVBoxLayout(this);
    main->setContentsMargins(18, 18, 18, 18);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("forumClaimCard"));
    card->setFixedWidth(520);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("认领该内容"), card);
    title->setObjectName(QStringLiteral("forumClaimTitle"));
    layout->addWidget(title);

    auto *meta = new QLabel(tr("作品：《%1》\n转载作者：%2")
                                .arg(m_threadTitle, m_authorName),
                            card);
    meta->setObjectName(QStringLiteral("forumClaimMeta"));
    meta->setWordWrap(true);
    layout->addWidget(meta);

    auto *hint = new QLabel(
        tr("如果你是原作品的作者，可以提交认领并向转载作者说明情况；"
           "对方同意后，作品会转移到你的创作中心。"),
        card);
    hint->setObjectName(QStringLiteral("forumClaimNote"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_edit = new QPlainTextEdit(card);
    m_edit->setObjectName(QStringLiteral("forumClaimInput"));
    m_edit->setPlaceholderText(tr("说明你是原作者，并附上原作品链接更有说服力"));
    m_edit->setPlainText(defaultMessage());
    m_edit->setFixedHeight(96);
    layout->addWidget(m_edit);

    m_errorLabel = new QLabel(card);
    m_errorLabel->setObjectName(QStringLiteral("forumClaimStatus"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    auto *actions = new QHBoxLayout();
    actions->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), card);
    cancel->setObjectName(QStringLiteral("forumGhostButton"));
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setFixedHeight(30);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    actions->addWidget(cancel);
    m_submitButton = new QPushButton(tr("提交认领"), card);
    m_submitButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_submitButton->setCursor(Qt::PointingHandCursor);
    m_submitButton->setFixedHeight(30);
    connect(m_submitButton, &QPushButton::clicked, this, &ForumClaimSubmitDialog::submit);
    actions->addWidget(m_submitButton);
    layout->addLayout(actions);

    main->addWidget(card, 0, Qt::AlignCenter);
}

void ForumClaimSubmitDialog::applyStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool light = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg = light ? QStringLiteral("rgba(255,255,255,250)")
                                 : QStringLiteral("rgba(44,44,48,250)");
    const QString border = light ? QStringLiteral("#dcdcdc") : QStringLiteral("#5a5a60");
    const QString titleColor = light ? QStringLiteral("#1a1a1a") : QStringLiteral("#f2f2f2");
    const QString textColor = light ? QStringLiteral("#333333") : QStringLiteral("#e8e8e8");
    const QString mutedColor = light ? QStringLiteral("#64748B") : QStringLiteral("#9aa3b2");
    const QString fieldBg = light ? QStringLiteral("#f4f4f4") : QStringLiteral("#3b3b40");
    const QString fieldBorder = light ? QStringLiteral("#d4d4d4") : QStringLiteral("#55555a");

    setStyleSheet(
        QStringLiteral(
            "QDialog { background-color: transparent; border: none; }"
            "QFrame#forumClaimCard { background-color: %1; border: 1px solid %2;"
            " border-radius: 18px; }"
            "QLabel#forumClaimTitle { color: %3; font-size: 15px; font-weight: 700;"
            " background: transparent; }"
            "QLabel#forumClaimMeta { color: %4; font-size: 12.5px; background: transparent; }"
            "QLabel#forumClaimNote { color: %4; font-size: 12px; background: transparent; }"
            "QLabel#forumClaimStatus { color: #dc2626; font-size: 12px; background: transparent; }"
            "QPlainTextEdit#forumClaimInput { background: %5; color: %6;"
            " border: 1px solid %7; border-radius: 10px; padding: 6px 8px;"
            " font-size: 13px; }"
            "QPlainTextEdit#forumClaimInput:focus { border-color: %8; }"
        )
            .arg(cardBg, border, titleColor, mutedColor, fieldBg, textColor, fieldBorder,
                 tm->currentThemeColor()));
}

void ForumClaimSubmitDialog::submit()
{
    if (m_busy)
        return;
    const QString message = m_edit->toPlainText().trimmed();
    if (message.size() > 300)
    {
        m_errorLabel->setText(tr("认领说明最多 300 个字"));
        m_errorLabel->show();
        return;
    }

    m_busy = true;
    m_submitButton->setEnabled(false);
    m_submitButton->setText(tr("正在提交…"));
    ForumContentService::instance()->createClaim(
        m_threadId, message,
        [this](const ForumClaimInfo &) {
            m_busy = false;
            m_submitted = true;
            accept();
        },
        [this](const QString &error, int) {
            m_busy = false;
            m_submitButton->setEnabled(true);
            m_submitButton->setText(tr("提交认领"));
            m_errorLabel->setText(error);
            m_errorLabel->show();
        });
}

QString ForumClaimSubmitDialog::defaultMessage()
{
    return tr("我是这个作品的原创作者，请把作品转给我。");
}
