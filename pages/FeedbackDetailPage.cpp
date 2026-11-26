/**
 * @file   FeedbackDetailPage.cpp
 * @brief  反馈详情页实现：反馈正文 + 管理员回复 + 评论区
 * @author BlockBox Team
 * @date   2026-11-21
 */

#include "FeedbackDetailPage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/ForumKit.h"
#include "components/QuizDialog.h"
#include "utils/StyleKit.h"

namespace {
/** 一条评论最多 300 字（和房间评论 / 网页论坛一致） */
constexpr int kCommentMaxLength = 300;

/** 灰色分类徽章（类型 / 已忽略共用一套配色） */
QLabel *makeNeutralBadge(QWidget *parent, const QString &text)
{
    QLabel *badge = ForumKit::makeBadge(parent, text);
    badge->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QLabel{background:@BG_MUTED@;color:@TEXT_TERTIARY@;border:none;"
        "border-radius:8px;font-size:11px;font-weight:600;padding:1px 8px;}")));
    return badge;
}

/** 状态胶囊的配色：待处理绿 / 已处理紫 / 已忽略灰 */
void applyStatePillStyle(QLabel *pill, const QString &status)
{
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

/**
 * 点击就地吃掉的容器：评论区里的空白处不响应点击，
 * 免得点评论 / 输入框附近触发别的动作。
 */
class ClickGuard : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void mousePressEvent(QMouseEvent *event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent *event) override { event->accept(); }
};

/** 评论区（评论行 + 输入区）的一整套样式，挂在评论卡片上 */
QString commentSectionStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "QFrame#fbCommentDivider {"
        "  background: @BORDER_LIGHT@; border: none;"
        "}"
        "QLabel#fbCommentHint {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QPushButton#fbCommentRefresh {"
        "  background: transparent; border: 1px solid @BORDER_LIGHT@; border-radius: 8px;"
        "  color: @TEXT_TERTIARY@; font-size: 11px; padding: 0 10px;"
        "}"
        "QPushButton#fbCommentRefresh:hover {"
        "  color: @TEXT_PRIMARY@; border-color: @PRIMARY@; background: @BG_HOVER@;"
        "}"
        "QPushButton#fbCommentRefresh:disabled {"
        "  color: @TEXT_DISABLED@; border-color: @BORDER_DISABLED@;"
        "}"
        "QFrame#fbCommentRow {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER_LIGHT@; border-radius: 12px;"
        "}"
        "QLabel#fbCommentName {"
        "  color: @TEXT_PRIMARY@; font-size: 12px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#fbCommentBadge {"
        "  color: @TEXT_ACCENT@; background: @PRIMARY_BG@; border-radius: 7px;"
        "  padding: 0 6px; font-size: 10px; font-weight: 700;"
        "}"
        "QLabel#fbCommentBadge[kind=\"self\"] { color: #ffffff; background: @PRIMARY@; }"
        "QLabel#fbCommentBadge[kind=\"admin\"] { color: #ffffff; background: #8250df; }"
        "QLabel#fbCommentTime {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QLabel#fbCommentBody {"
        "  color: @TEXT_SECONDARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QPushButton#fbCommentDelete {"
        "  background: transparent; border: none; border-radius: 6px;"
        "  color: @TEXT_TERTIARY@; font-size: 11px; padding: 0 6px;"
        "}"
        "QPushButton#fbCommentDelete:hover {"
        "  color: @DANGER_TEXT@; background: @DANGER_BG@;"
        "}"
        "QPlainTextEdit#fbCommentEdit {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 10px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 8px 10px;"
        "}"
        "QPlainTextEdit#fbCommentEdit:focus { border-color: @PRIMARY@; }"
        "QLabel#fbCommentCount {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QLabel#fbCommentCount[over=\"true\"] { color: @DANGER_TEXT@; }"
        "QPushButton#fbCommentSend {"
        "  background: @PRIMARY@; color: @TEXT_ON_PRIMARY@; border: none; border-radius: 9px;"
        "  padding: 0 16px; font-size: 12px; font-weight: 700;"
        "}"
        "QPushButton#fbCommentSend:hover { background: @PRIMARY_HOVER@; }"
        "QPushButton#fbCommentSend:pressed { background: @PRIMARY_PRESSED@; }"
        "QPushButton#fbCommentSend:disabled {"
        "  background: @BG_DISABLED@; color: @TEXT_DISABLED@;"
        "}"
        "QLabel#fbCommentIdentity {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QLabel#fbCommentStatus {"
        "  color: @TEXT_ACCENT@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QLabel#fbCommentStatus[error=\"true\"] { color: @DANGER_TEXT@; }"));
}

/**
 * @brief 一条评论：头像 + 名字（提交人 / 管理员 / 你 标签）+ 时间 + 正文 + 删除
 * @param onDelete 非空时右侧显示「删除」（自己发的、反馈提交者或管理员清理留言）
 */
QFrame *makeCommentRow(QWidget *parent, const FeedbackComment &comment,
                       const std::function<void()> &onDelete)
{
    auto *row = new QFrame(parent);
    row->setObjectName(QStringLiteral("fbCommentRow"));

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 9, 10, 10);
    layout->setSpacing(9);

    const QString name = comment.display();
    QLabel *avatar = ForumKit::makeAvatar(row, comment.avatar, name, 28);
    layout->addWidget(avatar, 0, Qt::AlignTop);

    auto *body = new QVBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);

    auto *head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(6);

    auto *nameLabel = new QLabel(name, row);
    nameLabel->setObjectName(QStringLiteral("fbCommentName"));
    nameLabel->setToolTip(name);
    head->addWidget(nameLabel, 0);

    auto addBadge = [row, head](const QString &text, const QString &kind) {
        auto *badge = new QLabel(text, row);
        badge->setObjectName(QStringLiteral("fbCommentBadge"));
        badge->setProperty("kind", kind);
        head->addWidget(badge, 0);
    };
    if (comment.isAuthor)
        addBadge(QObject::tr("提交人"), QStringLiteral("author"));
    if (comment.isAdmin)
        addBadge(QObject::tr("管理员"), QStringLiteral("admin"));
    if (comment.mine)
        addBadge(QObject::tr("你"), QStringLiteral("self"));

    auto *timeLabel = new QLabel(ForumKit::relativeTime(comment.createdAt), row);
    timeLabel->setObjectName(QStringLiteral("fbCommentTime"));
    head->addWidget(timeLabel, 0);
    head->addStretch(1);

    if (onDelete)
    {
        auto *del = new QPushButton(QObject::tr("删除"), row);
        del->setObjectName(QStringLiteral("fbCommentDelete"));
        del->setCursor(Qt::PointingHandCursor);
        del->setFixedHeight(22);
        QObject::connect(del, &QPushButton::clicked, row, [onDelete]() { onDelete(); });
        head->addWidget(del, 0);
    }
    body->addLayout(head);

    auto *content = new QLabel(comment.content, row);
    content->setObjectName(QStringLiteral("fbCommentBody"));
    // 评论是纯文本：粘贴进来的 <b> 之类按原样显示，不当 HTML 解析
    content->setTextFormat(Qt::PlainText);
    content->setWordWrap(true);
    content->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(content);

    layout->addLayout(body, 1);
    return row;
}

/** 清空 QBoxLayout 里现有的控件（立即摘掉，下一帧删除） */
void clearLayoutWidgets(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}
} // namespace

FeedbackDetailPage::FeedbackDetailPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    // 登录 / 换账号 / 答题通过后：输入区跟着重建，评论区也刷新一遍
    //（mine 标记和「删除」按钮都跟登录状态有关）
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this, [this]() {
        rebuildCommentComposer();
        // 登录完成时这页可能还没显示出来：照样刷一遍，「你」标记和删除按钮才不会漏
        if (m_item.id > 0)
            requestComments();
    });
}

void FeedbackDetailPage::buildUi()
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

    // ── 反馈正文卡片 ──
    auto *card = ForumKit::makeCard(content);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 18, 20, 18);
    cardLayout->setSpacing(8);

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);

    m_statePill = new QLabel(card);
    m_statePill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    titleRow->addWidget(m_statePill, 0, Qt::AlignTop);

    m_titleLabel = new QLabel(card);
    m_titleLabel->setObjectName(QStringLiteral("forumThreadTitle"));
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleRow->addWidget(m_titleLabel, 1);

    m_typeBadge = makeNeutralBadge(card, QString());
    titleRow->addWidget(m_typeBadge, 0, Qt::AlignTop);
    cardLayout->addLayout(titleRow);

    m_metaLabel = ForumKit::makeMuted(card);
    m_metaLabel->setWordWrap(true);
    cardLayout->addWidget(m_metaLabel);

    auto *divider = new QFrame(card);
    divider->setFixedHeight(1);
    divider->setStyleSheet(StyleKit::resolve(
        QStringLiteral("QFrame{background:@BORDER_LIGHT@;border:none;}")));
    cardLayout->addSpacing(4);
    cardLayout->addWidget(divider);
    cardLayout->addSpacing(4);

    m_contentLabel = new QLabel(card);
    m_contentLabel->setObjectName(QStringLiteral("fbDetailBody"));
    m_contentLabel->setWordWrap(true);
    m_contentLabel->setTextFormat(Qt::RichText);
    m_contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_contentLabel->setStyleSheet(StyleKit::resolve(
        QStringLiteral("QLabel{color:@TEXT_SECONDARY@;font-size:13px;background:transparent;"
                       "border:none;}")));
    cardLayout->addWidget(m_contentLabel);

    m_replyLabel = new QLabel(card);
    m_replyLabel->setObjectName(QStringLiteral("forumMuted"));
    m_replyLabel->setWordWrap(true);
    m_replyLabel->setTextFormat(Qt::PlainText);
    m_replyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_replyLabel->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QLabel{background:@BG_MUTED@;border:1px solid @BORDER@;"
        "border-radius:10px;padding:9px 12px;color:@TEXT_SECONDARY@;font-size:12px;}")));
    m_replyLabel->hide();
    cardLayout->addWidget(m_replyLabel);

    layout->addWidget(card);

    // ── 评论区卡片 ──
    auto *commentCard = ForumKit::makeCard(content);
    commentCard->setStyleSheet(commentSectionStyle());
    auto *commentLayout = new QVBoxLayout(commentCard);
    commentLayout->setContentsMargins(18, 14, 18, 16);
    commentLayout->setSpacing(8);

    auto *headRow = new QHBoxLayout();
    headRow->setContentsMargins(0, 0, 0, 0);
    headRow->setSpacing(8);
    headRow->addWidget(ForumKit::makeSectionTitle(commentCard, tr("评论")));
    m_commentHint = new QLabel(tr("正在加载评论…"), commentCard);
    m_commentHint->setObjectName(QStringLiteral("fbCommentHint"));
    m_commentHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    headRow->addWidget(m_commentHint, 1);
    m_commentRefresh = new QPushButton(tr("刷新"), commentCard);
    m_commentRefresh->setObjectName(QStringLiteral("fbCommentRefresh"));
    m_commentRefresh->setCursor(Qt::PointingHandCursor);
    m_commentRefresh->setFixedHeight(24);
    m_commentRefresh->setToolTip(tr("重新拉取最新评论"));
    connect(m_commentRefresh, &QPushButton::clicked, this, [this]() { requestComments(); });
    headRow->addWidget(m_commentRefresh);
    commentLayout->addLayout(headRow);

    // 评论行容器：点击就地吃掉，不触发别的动作
    m_commentRowsHost = new ClickGuard(commentCard);
    m_commentRowsLayout = new QVBoxLayout(m_commentRowsHost);
    m_commentRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_commentRowsLayout->setSpacing(6);
    m_commentRowsHost->setVisible(false);
    commentLayout->addWidget(m_commentRowsHost);

    // 输入区 + 结果提示
    m_composerHost = new ClickGuard(commentCard);
    m_composerLayout = new QVBoxLayout(m_composerHost);
    m_composerLayout->setContentsMargins(0, 0, 0, 0);
    m_composerLayout->setSpacing(6);
    commentLayout->addWidget(m_composerHost);

    m_commentStatus = new QLabel(commentCard);
    m_commentStatus->setObjectName(QStringLiteral("fbCommentStatus"));
    m_commentStatus->setWordWrap(true);
    m_commentStatus->hide();
    commentLayout->addWidget(m_commentStatus);

    layout->addWidget(commentCard);
    layout->addStretch(1);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void FeedbackDetailPage::open(const PublicFeedbackItem &item)
{
    m_item = item;

    // 上一条反馈留下的评论 / 输入 / 草稿统统清掉
    m_comments.clear();
    m_commentTotal = 0;
    m_commentLoading = false;
    m_commentQueued = false;
    m_commentFailed = false;
    m_commentSubmitting = false;
    m_rowsSignature.clear();
    m_composerState.clear();
    m_draft.clear();
    if (m_commentRowsLayout)
        clearLayoutWidgets(m_commentRowsLayout);
    if (m_commentRowsHost)
        m_commentRowsHost->setVisible(false);
    setCommentStatus(QString());
    if (m_commentHint)
        m_commentHint->setText(tr("正在加载评论…"));

    renderFeedback();
    rebuildCommentComposer();

    // 每次进详情页都从顶部开始看
    if (auto *scroll = findChild<QScrollArea *>())
        scroll->verticalScrollBar()->setValue(0);

    requestComments();
}

void FeedbackDetailPage::renderFeedback()
{
    applyStatePillStyle(m_statePill, m_item.status);
    m_typeBadge->setText(m_item.type == QStringLiteral("feature") ? tr("功能建议")
                                                                 : tr("问题反馈"));

    QString title = m_item.title.trimmed();
    if (title.isEmpty())
    {
        // 服务端没给标题时退回正文第一行，避免详情页顶部空着
        const QStringList lines = m_item.content.split(QLatin1Char('\n'));
        for (const QString &line : lines)
        {
            if (!line.trimmed().isEmpty())
            {
                title = line.trimmed();
                break;
            }
        }
    }
    m_titleLabel->setText(title.isEmpty() ? tr("反馈 #%1").arg(m_item.id) : title);

    QStringList meta;
    meta << QStringLiteral("#%1").arg(m_item.id);
    meta << (m_item.authorName.isEmpty() ? tr("由已注销用户提交")
                                         : tr("由 %1 提交").arg(m_item.authorName));
    meta << ForumKit::relativeTime(m_item.createdAt);
    if (m_item.handledAt > 0)
    {
        const QString handled = m_item.status == QStringLiteral("ignored") ? tr("已忽略 · %1")
                                                                           : tr("已处理 · %1");
        meta << handled.arg(ForumKit::relativeTime(m_item.handledAt));
    }
    m_metaLabel->setText(meta.join(QStringLiteral(" · ")));

    m_contentLabel->setText(renderContent(m_item.content));

    if (!m_item.reply.trimmed().isEmpty())
    {
        m_replyLabel->setText(tr("管理员回复：%1").arg(m_item.reply));
        m_replyLabel->show();
    }
    else
    {
        m_replyLabel->clear();
        m_replyLabel->hide();
    }
}

// ────────────────────────────────────────────── 评论区

void FeedbackDetailPage::requestComments()
{
    if (m_item.id <= 0)
        return;
    if (m_commentLoading)
    {
        // 正在拉取时又要求刷新（刚发完 / 刚删完）：等这一轮回来再补一次，
        // 免得新评论被还在路上的旧列表盖掉
        m_commentQueued = true;
        return;
    }

    m_commentLoading = true;
    m_commentFailed = false;
    if (m_commentRefresh)
        m_commentRefresh->setEnabled(false);
    if (m_comments.isEmpty() && m_commentHint)
        m_commentHint->setText(tr("正在加载评论…"));

    QPointer<FeedbackDetailPage> guard(this);
    FeedbackService::instance()->fetchComments(
        m_item.id,
        [guard](const QVector<FeedbackComment> &comments, int total) {
            if (guard.isNull())
                return;
            guard->m_commentLoading = false;
            guard->m_commentFailed = false;
            guard->m_comments = comments;
            guard->m_commentTotal = total;
            if (guard->m_commentRefresh)
                guard->m_commentRefresh->setEnabled(true);
            guard->renderComments();
            if (guard->m_commentQueued)
            {
                guard->m_commentQueued = false;
                guard->requestComments();
            }
        },
        [guard](const QString &message, int) {
            if (guard.isNull())
                return;
            guard->m_commentLoading = false;
            guard->m_commentFailed = true;
            if (guard->m_commentRefresh)
                guard->m_commentRefresh->setEnabled(true);
            if (guard->m_comments.isEmpty())
                guard->setCommentStatus(message, true);
            guard->renderComments();
            if (guard->m_commentQueued)
            {
                guard->m_commentQueued = false;
                guard->requestComments();
            }
        });
}

void FeedbackDetailPage::renderComments()
{
    if (!m_commentHint || !m_commentRowsHost || !m_commentRowsLayout)
        return;

    const int total = m_commentTotal > 0 ? m_commentTotal : m_comments.size();

    if (m_commentLoading && m_comments.isEmpty())
        m_commentHint->setText(tr("正在加载评论…"));
    else if (m_commentFailed && m_comments.isEmpty())
        m_commentHint->setText(tr("评论加载失败，点「刷新」再试一次。"));
    else if (m_comments.isEmpty())
        m_commentHint->setText(tr("还没有人评论，来聊聊这条反馈吧。"));
    else if (total > m_comments.size())
        m_commentHint->setText(tr("共 %1 条评论 · 显示最近 %2 条").arg(total).arg(m_comments.size()));
    else
        m_commentHint->setText(tr("共 %1 条评论").arg(total));

    m_commentRowsHost->setVisible(!m_comments.isEmpty());

    // 评论内容没变就不重画（刷新 / 登录状态变化场景别打断正在选中的文字）
    QStringList parts;
    parts.reserve(m_comments.size() + 1);
    for (const FeedbackComment &comment : m_comments)
    {
        parts << QStringLiteral("%1@%2@%3@%4@%5@%6@%7")
                     .arg(comment.id)
                     .arg(comment.userId)
                     .arg(comment.createdAt)
                     .arg(comment.isAuthor ? 1 : 0)
                     .arg(comment.isAdmin ? 1 : 0)
                     .arg(comment.mine ? 1 : 0)
                     .arg(comment.content);
    }
    const QString signature = parts.join(QLatin1Char('|'));
    if (signature == m_rowsSignature)
        return;
    m_rowsSignature = signature;

    clearLayoutWidgets(m_commentRowsLayout);

    const ForumUser me = ForumAccountService::instance()->user();
    const bool iAmAuthor = me.id > 0 && m_item.authorId > 0 && me.id == m_item.authorId;
    const QString role = me.role.trimmed().toLower();
    const bool iAmAdmin = me.id > 0 && (role == QLatin1String("admin") || role == QLatin1String("moderator"));

    QPointer<FeedbackDetailPage> guard(this);
    for (const FeedbackComment &comment : m_comments)
    {
        // 自己发的、反馈提交者和管理员可以删（服务端把关）
        std::function<void()> onDelete;
        if (comment.mine || iAmAuthor || iAmAdmin)
        {
            onDelete = [guard, comment]() {
                if (!guard.isNull())
                    guard->deleteComment(comment);
            };
        }
        m_commentRowsLayout->addWidget(makeCommentRow(m_commentRowsHost, comment, onDelete));
    }
}

void FeedbackDetailPage::rebuildCommentComposer()
{
    if (!m_composerHost || !m_composerLayout)
        return;

    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool quizPassed = account->hasPassedQuiz();
    const QString state = QStringLiteral("%1|%2").arg(loggedIn ? 1 : 0).arg(quizPassed ? 1 : 0);
    if (state == m_composerState)
    {
        // 状态没变：别重建输入框，只把字数 / 按钮同步一下
        updateCommentCount();
        return;
    }
    m_composerState = state;

    // 重建前先把已经写了一半的评论收起来，重新挂上后原样放回
    if (m_edit)
        m_draft = m_edit->toPlainText();

    clearLayoutWidgets(m_composerLayout);
    m_edit = nullptr;
    m_count = nullptr;
    m_send = nullptr;

    auto addHint = [this](const QString &text) {
        auto *hint = new QLabel(text, m_composerHost);
        hint->setObjectName(QStringLiteral("fbCommentHint"));
        hint->setWordWrap(true);
        m_composerLayout->addWidget(hint);
    };
    auto addAction = [this](const QString &text, const std::function<void()> &onClick) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        auto *button = new QPushButton(text, m_composerHost);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
        row->addWidget(button);
        row->addStretch(1);
        m_composerLayout->addLayout(row);
    };

    if (!loggedIn)
    {
        addHint(tr("登录论坛账号后，就能在这条反馈下面留言：补充信息、确认复现、聊聊解决办法。"));
        addAction(tr("登录 / 注册"), [this]() { emit loginRequested(); });
        return;
    }
    if (!quizPassed)
    {
        addHint(tr("评论前需要先通过入站答题，成绩会记在你的账号上，网页论坛同样生效。"));
        addAction(tr("去答题"), [this]() { openQuizDialog(); });
        return;
    }

    const ForumUser me = account->user();
    QString name = me.displayName.trimmed();
    if (name.isEmpty())
        name = me.username.trimmed();
    if (name.isEmpty())
        name = tr("方块盒子用户");

    auto *identityRow = new QHBoxLayout();
    identityRow->setContentsMargins(0, 0, 0, 0);
    identityRow->setSpacing(8);
    identityRow->addWidget(ForumKit::makeAvatar(m_composerHost, me.avatar, name, 24), 0,
                           Qt::AlignVCenter);
    auto *identity = new QLabel(tr("以 %1 的身份评论 · Ctrl + Enter 发送").arg(name),
                                m_composerHost);
    identity->setObjectName(QStringLiteral("fbCommentIdentity"));
    identity->setWordWrap(true);
    identityRow->addWidget(identity, 1);
    m_composerLayout->addLayout(identityRow);

    m_edit = new QPlainTextEdit(m_composerHost);
    m_edit->setObjectName(QStringLiteral("fbCommentEdit"));
    m_edit->setPlaceholderText(tr("说点什么…补充信息、确认复现或聊聊解决办法"));
    m_edit->setFixedHeight(78);
    m_edit->setTabChangesFocus(true);
    connect(m_edit, &QPlainTextEdit::textChanged, this,
            [this]() { updateCommentCount(); });
    m_composerLayout->addWidget(m_edit);

    // 回车换行，Ctrl + Enter 直接发表（只在输入框里生效，别抢别的页面快捷键）
    for (const int key : {Qt::Key_Return, Qt::Key_Enter})
    {
        auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | key), m_edit);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this]() { submitComment(); });
    }

    auto *foot = new QHBoxLayout();
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(8);
    m_count = new QLabel(m_composerHost);
    m_count->setObjectName(QStringLiteral("fbCommentCount"));
    foot->addWidget(m_count);
    foot->addStretch(1);
    m_send = new QPushButton(tr("发表评论"), m_composerHost);
    m_send->setObjectName(QStringLiteral("fbCommentSend"));
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setFixedHeight(32);
    connect(m_send, &QPushButton::clicked, this, [this]() { submitComment(); });
    foot->addWidget(m_send);
    m_composerLayout->addLayout(foot);

    if (!m_draft.isEmpty())
    {
        m_edit->setPlainText(m_draft);
        m_edit->moveCursor(QTextCursor::End);
    }
    m_draft.clear();
    updateCommentCount();
}

void FeedbackDetailPage::updateCommentCount()
{
    if (!m_edit || !m_count || !m_send)
        return;

    const int length = m_edit->toPlainText().size();
    m_count->setText(tr("%1 / %2 字").arg(length).arg(kCommentMaxLength));
    const bool over = length > kCommentMaxLength;
    if (m_count->property("over").toBool() != over)
    {
        m_count->setProperty("over", over);
        m_count->style()->unpolish(m_count);
        m_count->style()->polish(m_count);
    }
    if (!m_commentSubmitting)
        m_send->setText(tr("发表评论"));
    m_send->setEnabled(!m_commentSubmitting && length > 0 && !over);
}

void FeedbackDetailPage::submitComment()
{
    if (m_commentSubmitting || !m_edit)
        return;

    auto *account = ForumAccountService::instance();
    if (!account->isLoggedIn())
    {
        emit loginRequested();
        return;
    }
    if (!account->hasPassedQuiz())
    {
        openQuizDialog();
        return;
    }

    const QString content = m_edit->toPlainText().trimmed();
    if (content.isEmpty())
    {
        setCommentStatus(tr("先写点什么再发表吧。"), true);
        return;
    }
    if (content.size() > kCommentMaxLength)
    {
        setCommentStatus(tr("评论最多 %1 个字，删掉一些再发。").arg(kCommentMaxLength), true);
        return;
    }

    m_commentSubmitting = true;
    m_send->setEnabled(false);
    m_send->setText(tr("发表中…"));

    QPointer<FeedbackDetailPage> guard(this);
    FeedbackService::instance()->postComment(
        m_item.id, content,
        [guard](const FeedbackComment &) {
            if (guard.isNull())
                return;
            guard->m_commentSubmitting = false;
            guard->m_draft.clear();
            if (guard->m_edit)
                guard->m_edit->clear();
            guard->updateCommentCount();
            guard->setCommentStatus(tr("评论已发表。"), false);
            guard->requestComments();
        },
        [guard](const QString &message, int) {
            if (guard.isNull())
                return;
            guard->m_commentSubmitting = false;
            guard->updateCommentCount();
            guard->setCommentStatus(message, true);
        });
}

void FeedbackDetailPage::deleteComment(const FeedbackComment &comment)
{
    if (comment.id <= 0)
        return;

    const auto reply = AppMessageBox::question(
        this, tr("删除评论"),
        comment.mine ? tr("删除后这条评论就没了，确定删除吗？")
                     : tr("删除后这条留言就没了，确定删除吗？"),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    QPointer<FeedbackDetailPage> guard(this);
    FeedbackService::instance()->deleteComment(
        comment.id,
        [guard]() {
            if (guard.isNull())
                return;
            guard->setCommentStatus(tr("评论已删除。"), false);
            guard->requestComments();
        },
        [guard](const QString &message, int) {
            if (guard.isNull())
                return;
            guard->setCommentStatus(message, true);
        });
}

void FeedbackDetailPage::setCommentStatus(const QString &text, bool error)
{
    if (!m_commentStatus)
        return;
    m_commentStatus->setText(text);
    m_commentStatus->setVisible(!text.isEmpty());
    if (m_commentStatus->property("error").toBool() != error)
    {
        m_commentStatus->setProperty("error", error);
        m_commentStatus->style()->unpolish(m_commentStatus);
        m_commentStatus->style()->polish(m_commentStatus);
    }
}

void FeedbackDetailPage::openQuizDialog()
{
    QWidget *host = window();
    QuizDialog dialog(host);
    connect(&dialog, &QuizDialog::loginRequested, this, &FeedbackDetailPage::loginRequested);
    dialog.exec();
    // 通过后 userChanged 会带动输入区重建；这里再兜一次
    rebuildCommentComposer();
}
