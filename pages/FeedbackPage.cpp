/**
 * @file   FeedbackPage.cpp
 * @brief  软件内反馈页实现：反馈列表
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "FeedbackPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "utils/StyleKit.h"

namespace {
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
} // namespace

FeedbackPage::FeedbackPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
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
