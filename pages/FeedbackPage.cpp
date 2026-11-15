/**
 * @file   FeedbackPage.cpp
 * @brief  软件内反馈页实现：反馈列表 + 详情里的评论
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "FeedbackPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStyle>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
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

/**
 * 点击就地吃掉的容器：评论区里的空白处不响应点击，
 * 免得点评论 / 输入框附近把整个详情又点收起来了。
 */
class ClickGuard : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void mousePressEvent(QMouseEvent *event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent *event) override { event->accept(); }
};

/** 评论区（评论行 + 输入区）的一整套样式，挂在详情展开区上 */
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

FeedbackPage::FeedbackPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    // 登录 / 换账号 / 答题通过后：输入区跟着重建，展开着的评论也刷新一遍
    //（mine 标记和「删除」按钮都跟登录状态有关）
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this, [this]() {
        for (auto it = m_commentSections.constBegin(); it != m_commentSections.constEnd(); ++it)
        {
            rebuildCommentComposer(it.key());
            if (it.value()->detail && it.value()->detail->isVisible())
                requestComments(it.key());
        }
    });
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

    // ── 反馈列表（仿 GitHub Issues 列表页，未登录也可见） ──
    m_publicHost = new QWidget(content);
    m_publicHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *publicLayout = new QVBoxLayout(m_publicHost);
    publicLayout->setContentsMargins(0, 0, 0, 0);
    publicLayout->setSpacing(10);

    auto *publicHeadRow = new QHBoxLayout();
    publicHeadRow->setSpacing(10);
    publicHeadRow->addWidget(ForumKit::makeSectionTitle(m_publicHost, tr("反馈")));
    auto *hint = ForumKit::makeMuted(m_publicHost, tr("大家提交的反馈和处理进度都在这里"));
    hint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    publicHeadRow->addWidget(hint, 1);
    m_composeButton = new QPushButton(tr("提交新反馈"), m_publicHost);
    m_composeButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_composeButton->setCursor(Qt::PointingHandCursor);
    m_composeButton->setFixedHeight(34);
    m_composeButton->setMinimumWidth(110);
    connect(m_composeButton, &QPushButton::clicked, this, &FeedbackPage::composeRequested);
    publicHeadRow->addWidget(m_composeButton);
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
}

void FeedbackPage::open()
{
    loadPublic(1);
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
            clearCommentSections();
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
    clearCommentSections();
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
        detail->setStyleSheet(commentSectionStyle());
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

        // 详情里的评论区：展开时拉取，登录用户可以留言讨论
        buildCommentSection(detailLayout, item);
        rowLayout->addWidget(detail);

        const int feedbackId = item.id;
        card->onClick = [this, detail, feedbackId]() {
            detail->setVisible(!detail->isVisible());
            if (detail->isVisible())
            {
                // 每次展开都重建输入区（登录 / 答题状态可能变了）并刷新评论
                rebuildCommentComposer(feedbackId);
                requestComments(feedbackId);
            }
        };

        m_publicListLayout->addWidget(card);
    }
}

// ────────────────────────────────────────────── 反馈详情里的评论区

void FeedbackPage::clearCommentSections()
{
    // 重建前列表行还在：把写到一半的评论先收起来，新行挂上后原样放回
    for (auto it = m_commentSections.constBegin(); it != m_commentSections.constEnd(); ++it)
    {
        if (it.value()->edit)
            m_commentDrafts.insert(it.key(), it.value()->edit->toPlainText());
    }
    qDeleteAll(m_commentSections);
    m_commentSections.clear();
}

void FeedbackPage::buildCommentSection(QVBoxLayout *detailLayout, const PublicFeedbackItem &item)
{
    const int feedbackId = item.id;
    auto *section = new FeedbackCommentSection;
    section->detail = detailLayout->parentWidget();
    section->authorId = item.authorId;
    m_commentSections.insert(feedbackId, section);

    auto *divider = new QFrame(detailLayout->parentWidget());
    divider->setObjectName(QStringLiteral("fbCommentDivider"));
    divider->setFixedHeight(1);
    detailLayout->addSpacing(4);
    detailLayout->addWidget(divider);

    // 状态行（共 N 条评论…）+ 手动刷新
    auto *hintRow = new QHBoxLayout();
    hintRow->setContentsMargins(0, 0, 0, 0);
    hintRow->setSpacing(8);
    section->hint = new QLabel(detailLayout->parentWidget());
    section->hint->setObjectName(QStringLiteral("fbCommentHint"));
    section->hint->setText(tr("正在加载评论…"));
    hintRow->addWidget(section->hint, 1);
    section->refresh = new QPushButton(tr("刷新"), detailLayout->parentWidget());
    section->refresh->setObjectName(QStringLiteral("fbCommentRefresh"));
    section->refresh->setCursor(Qt::PointingHandCursor);
    section->refresh->setFixedHeight(24);
    section->refresh->setToolTip(tr("重新拉取最新评论"));
    connect(section->refresh, &QPushButton::clicked, this,
            [this, feedbackId]() { requestComments(feedbackId); });
    hintRow->addWidget(section->refresh);
    detailLayout->addLayout(hintRow);

    // 评论行容器：点击就地吃掉，不触发外层卡片的展开 / 收起
    section->rowsHost = new ClickGuard(detailLayout->parentWidget());
    section->rowsLayout = new QVBoxLayout(section->rowsHost);
    section->rowsLayout->setContentsMargins(0, 0, 0, 0);
    section->rowsLayout->setSpacing(6);
    section->rowsHost->setVisible(false);
    detailLayout->addWidget(section->rowsHost);

    // 输入区 + 结果提示
    section->composerHost = new ClickGuard(detailLayout->parentWidget());
    section->composerLayout = new QVBoxLayout(section->composerHost);
    section->composerLayout->setContentsMargins(0, 0, 0, 0);
    section->composerLayout->setSpacing(6);
    detailLayout->addWidget(section->composerHost);

    section->status = new QLabel(detailLayout->parentWidget());
    section->status->setObjectName(QStringLiteral("fbCommentStatus"));
    section->status->setWordWrap(true);
    section->status->hide();
    detailLayout->addWidget(section->status);
}

FeedbackCommentSection *FeedbackPage::commentSection(int feedbackId) const
{
    return m_commentSections.value(feedbackId, nullptr);
}

void FeedbackPage::requestComments(int feedbackId)
{
    if (feedbackId <= 0)
        return;
    if (m_commentLoading.contains(feedbackId))
    {
        // 正在拉取时又要求刷新（刚发完 / 刚删完）：等这一轮回来再补一次，
        // 免得新评论被还在路上的旧列表盖掉
        m_commentQueued.insert(feedbackId);
        return;
    }

    m_commentLoading.insert(feedbackId);
    m_commentFailed.remove(feedbackId);
    if (FeedbackCommentSection *section = commentSection(feedbackId))
    {
        if (section->refresh)
            section->refresh->setEnabled(false);
    }

    QPointer<FeedbackPage> guard(this);
    FeedbackService::instance()->fetchComments(
        feedbackId,
        [guard, feedbackId](const QVector<FeedbackComment> &comments, int total) {
            if (guard.isNull())
                return;
            guard->m_commentLoading.remove(feedbackId);
            guard->m_commentFailed.remove(feedbackId);
            guard->m_comments.insert(feedbackId, comments);
            guard->m_commentTotals.insert(feedbackId, total);
            if (FeedbackCommentSection *section = guard->commentSection(feedbackId))
            {
                if (section->refresh)
                    section->refresh->setEnabled(true);
            }
            guard->renderComments(feedbackId);
            if (guard->m_commentQueued.remove(feedbackId) > 0)
                guard->requestComments(feedbackId);
        },
        [guard, feedbackId](const QString &message, int) {
            if (guard.isNull())
                return;
            guard->m_commentLoading.remove(feedbackId);
            guard->m_commentFailed.insert(feedbackId);
            if (FeedbackCommentSection *section = guard->commentSection(feedbackId))
            {
                if (section->refresh)
                    section->refresh->setEnabled(true);
                if (guard->m_comments.value(feedbackId).isEmpty())
                    guard->setCommentStatus(feedbackId, message, true);
            }
            guard->renderComments(feedbackId);
            if (guard->m_commentQueued.remove(feedbackId) > 0)
                guard->requestComments(feedbackId);
        });
}

void FeedbackPage::renderComments(int feedbackId)
{
    FeedbackCommentSection *section = commentSection(feedbackId);
    if (!section || !section->hint || !section->rowsHost)
        return;

    const QVector<FeedbackComment> comments = m_comments.value(feedbackId);
    const int total = m_commentTotals.value(feedbackId, comments.size());
    const bool loading = m_commentLoading.contains(feedbackId);
    const bool failed = m_commentFailed.contains(feedbackId);

    if (loading && comments.isEmpty())
        section->hint->setText(tr("正在加载评论…"));
    else if (failed && comments.isEmpty())
        section->hint->setText(tr("评论加载失败，点「刷新」再试一次。"));
    else if (comments.isEmpty())
        section->hint->setText(tr("还没有人评论，来聊聊这条反馈吧。"));
    else if (total > comments.size())
        section->hint->setText(tr("共 %1 条评论 · 显示最近 %2 条").arg(total).arg(comments.size()));
    else
        section->hint->setText(tr("共 %1 条评论").arg(total));

    section->rowsHost->setVisible(!comments.isEmpty());

    // 评论内容没变就不重画（展开 / 收起 / 定时场景别打断正在选中的文字）
    QStringList parts;
    parts.reserve(comments.size() + 1);
    for (const FeedbackComment &comment : comments)
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
    if (signature == section->rowsSignature)
        return;
    section->rowsSignature = signature;

    clearLayoutWidgets(section->rowsLayout);

    const ForumUser me = ForumAccountService::instance()->user();
    const bool iAmAuthor = me.id > 0 && section->authorId > 0 && me.id == section->authorId;
    const QString role = me.role.trimmed().toLower();
    const bool iAmAdmin = me.id > 0 && (role == QLatin1String("admin") || role == QLatin1String("moderator"));

    QPointer<FeedbackPage> guard(this);
    for (const FeedbackComment &comment : comments)
    {
        // 自己发的、反馈提交者和管理员可以删（服务端把关）
        std::function<void()> onDelete;
        if (comment.mine || iAmAuthor || iAmAdmin)
        {
            onDelete = [guard, feedbackId, comment]() {
                if (!guard.isNull())
                    guard->deleteComment(feedbackId, comment);
            };
        }
        section->rowsLayout->addWidget(makeCommentRow(section->rowsHost, comment, onDelete));
    }
}

void FeedbackPage::rebuildCommentComposer(int feedbackId)
{
    FeedbackCommentSection *section = commentSection(feedbackId);
    if (!section || !section->composerHost || !section->composerLayout)
        return;

    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool quizPassed = account->hasPassedQuiz();
    const QString state = QStringLiteral("%1|%2").arg(loggedIn ? 1 : 0).arg(quizPassed ? 1 : 0);
    if (state == section->composerState)
    {
        // 状态没变：别重建输入框，只把字数 / 按钮同步一下
        updateCommentCount(feedbackId);
        return;
    }
    section->composerState = state;

    // 重建前先把已经写了一半的评论收起来，重新挂上后原样放回
    if (section->edit)
        m_commentDrafts.insert(feedbackId, section->edit->toPlainText());

    clearLayoutWidgets(section->composerLayout);
    section->edit = nullptr;
    section->count = nullptr;
    section->send = nullptr;

    auto addHint = [this, section](const QString &text) {
        auto *hint = new QLabel(text, section->composerHost);
        hint->setObjectName(QStringLiteral("fbCommentHint"));
        hint->setWordWrap(true);
        section->composerLayout->addWidget(hint);
    };
    auto addAction = [this, section](const QString &text, const std::function<void()> &onClick) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        auto *button = new QPushButton(text, section->composerHost);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
        row->addWidget(button);
        row->addStretch(1);
        section->composerLayout->addLayout(row);
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
    identityRow->addWidget(ForumKit::makeAvatar(section->composerHost, me.avatar, name, 24), 0,
                           Qt::AlignVCenter);
    auto *identity = new QLabel(tr("以 %1 的身份评论 · Ctrl + Enter 发送").arg(name),
                                section->composerHost);
    identity->setObjectName(QStringLiteral("fbCommentIdentity"));
    identity->setWordWrap(true);
    identityRow->addWidget(identity, 1);
    section->composerLayout->addLayout(identityRow);

    section->edit = new QPlainTextEdit(section->composerHost);
    section->edit->setObjectName(QStringLiteral("fbCommentEdit"));
    section->edit->setPlaceholderText(tr("说点什么…补充信息、确认复现或聊聊解决办法"));
    section->edit->setFixedHeight(78);
    section->edit->setTabChangesFocus(true);
    connect(section->edit, &QPlainTextEdit::textChanged, this,
            [this, feedbackId]() { updateCommentCount(feedbackId); });
    section->composerLayout->addWidget(section->edit);

    // 回车换行，Ctrl + Enter 直接发表（只在输入框里生效，别抢别的页面快捷键）
    for (const int key : {Qt::Key_Return, Qt::Key_Enter})
    {
        auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | key), section->edit);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this,
                [this, feedbackId]() { submitComment(feedbackId); });
    }

    auto *foot = new QHBoxLayout();
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(8);
    section->count = new QLabel(section->composerHost);
    section->count->setObjectName(QStringLiteral("fbCommentCount"));
    foot->addWidget(section->count);
    foot->addStretch(1);
    section->send = new QPushButton(tr("发表评论"), section->composerHost);
    section->send->setObjectName(QStringLiteral("fbCommentSend"));
    section->send->setCursor(Qt::PointingHandCursor);
    section->send->setFixedHeight(32);
    connect(section->send, &QPushButton::clicked, this,
            [this, feedbackId]() { submitComment(feedbackId); });
    foot->addWidget(section->send);
    section->composerLayout->addLayout(foot);

    const QString draft = m_commentDrafts.take(feedbackId);
    if (!draft.isEmpty())
    {
        section->edit->setPlainText(draft);
        section->edit->moveCursor(QTextCursor::End);
    }
    updateCommentCount(feedbackId);
}

void FeedbackPage::updateCommentCount(int feedbackId)
{
    FeedbackCommentSection *section = commentSection(feedbackId);
    if (!section || !section->edit || !section->count || !section->send)
        return;

    const int length = section->edit->toPlainText().size();
    section->count->setText(tr("%1 / %2 字").arg(length).arg(kCommentMaxLength));
    const bool over = length > kCommentMaxLength;
    if (section->count->property("over").toBool() != over)
    {
        section->count->setProperty("over", over);
        section->count->style()->unpolish(section->count);
        section->count->style()->polish(section->count);
    }
    if (!m_commentSubmitting.contains(feedbackId))
        section->send->setText(tr("发表评论"));
    section->send->setEnabled(!m_commentSubmitting.contains(feedbackId) && length > 0 && !over);
}

void FeedbackPage::submitComment(int feedbackId)
{
    FeedbackCommentSection *section = commentSection(feedbackId);
    if (!section || m_commentSubmitting.contains(feedbackId) || !section->edit)
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

    const QString content = section->edit->toPlainText().trimmed();
    if (content.isEmpty())
    {
        setCommentStatus(feedbackId, tr("先写点什么再发表吧。"), true);
        return;
    }
    if (content.size() > kCommentMaxLength)
    {
        setCommentStatus(feedbackId, tr("评论最多 %1 个字，删掉一些再发。").arg(kCommentMaxLength),
                         true);
        return;
    }

    m_commentSubmitting.insert(feedbackId);
    section->send->setEnabled(false);
    section->send->setText(tr("发表中…"));

    QPointer<FeedbackPage> guard(this);
    FeedbackService::instance()->postComment(
        feedbackId, content,
        [guard, feedbackId](const FeedbackComment &) {
            if (guard.isNull())
                return;
            guard->m_commentSubmitting.remove(feedbackId);
            guard->m_commentDrafts.remove(feedbackId);
            if (FeedbackCommentSection *live = guard->commentSection(feedbackId))
            {
                if (live->edit)
                    live->edit->clear();
            }
            guard->updateCommentCount(feedbackId);
            guard->setCommentStatus(feedbackId, tr("评论已发表。"), false);
            guard->requestComments(feedbackId);
        },
        [guard, feedbackId](const QString &message, int) {
            if (guard.isNull())
                return;
            guard->m_commentSubmitting.remove(feedbackId);
            guard->updateCommentCount(feedbackId);
            guard->setCommentStatus(feedbackId, message, true);
        });
}

void FeedbackPage::deleteComment(int feedbackId, const FeedbackComment &comment)
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

    QPointer<FeedbackPage> guard(this);
    FeedbackService::instance()->deleteComment(
        comment.id,
        [guard, feedbackId]() {
            if (guard.isNull())
                return;
            guard->setCommentStatus(feedbackId, tr("评论已删除。"), false);
            guard->requestComments(feedbackId);
        },
        [guard, feedbackId](const QString &message, int) {
            if (guard.isNull())
                return;
            guard->setCommentStatus(feedbackId, message, true);
        });
}

void FeedbackPage::setCommentStatus(int feedbackId, const QString &text, bool error)
{
    FeedbackCommentSection *section = commentSection(feedbackId);
    if (!section || !section->status)
        return;
    section->status->setText(text);
    section->status->setVisible(!text.isEmpty());
    if (section->status->property("error").toBool() != error)
    {
        section->status->setProperty("error", error);
        section->status->style()->unpolish(section->status);
        section->status->style()->polish(section->status);
    }
}

void FeedbackPage::openQuizDialog()
{
    QWidget *host = window();
    QuizDialog dialog(host);
    connect(&dialog, &QuizDialog::loginRequested, this, &FeedbackPage::loginRequested);
    dialog.exec();
    // 通过后 userChanged 会带动输入区重建；这里再兜一次
    for (auto it = m_commentSections.constBegin(); it != m_commentSections.constEnd(); ++it)
        rebuildCommentComposer(it.key());
}
