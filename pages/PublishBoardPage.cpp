/**
 * @file   PublishBoardPage.cpp
 * @brief  发布帖子页实现：社区板块卡片
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "PublishBoardPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/ForumKit.h"
#include "components/OutlinedLabel.h"
#include "components/PublishKit.h"
#include "layouts/FlowLayout.h"
#include "utils/StyleKit.h"

PublishBoardPage::PublishBoardPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void PublishBoardPage::buildUi()
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
    layout->setSpacing(10);

    auto *pageTitle = new OutlinedLabel(tr("发布帖子"), content);
    pageTitle->setObjectName(QStringLiteral("resourcesPageTitle"));
    layout->addWidget(pageTitle);

    auto *subtitle = new QLabel(
        tr("先选一个板块，再填写标题和正文；发帖需要登录方块盒子论坛账号"), content);
    subtitle->setObjectName(QStringLiteral("resourcesPageSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    layout->addSpacing(10);
    layout->addWidget(PublishKit::makeSectionTitle(content, tr("选择板块")));

    m_statusLabel = new QLabel(content);
    m_statusLabel->setObjectName(QStringLiteral("forumMuted"));
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_retryButton = new QPushButton(tr("重新加载"), content);
    m_retryButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_retryButton->setCursor(Qt::PointingHandCursor);
    m_retryButton->setFixedHeight(28);
    m_retryButton->hide();
    connect(m_retryButton, &QPushButton::clicked, this, &PublishBoardPage::loadBoards);
    layout->addWidget(m_retryButton, 0, Qt::AlignLeft);

    auto *cardsHost = new QWidget(content);
    cardsHost->setAttribute(Qt::WA_StyledBackground, false);
    m_cardsLayout = new FlowLayout(cardsHost, 0, 10, 10);
    layout->addWidget(cardsHost);

    layout->addStretch(1);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void PublishBoardPage::open()
{
    loadBoards();
}

void PublishBoardPage::refresh()
{
    loadBoards();
}

void PublishBoardPage::loadBoards()
{
    if (m_loading)
        return;
    m_loading = true;
    m_retryButton->hide();
    m_statusLabel->setText(tr("正在加载板块…"));
    m_statusLabel->setVisible(true);

    if (m_cardsLayout)
        ForumKit::clearLayout(m_cardsLayout);

    ForumContentService::instance()->fetchBoards(
        [this](const QVector<ForumBoard> &boards) {
            m_loading = false;
            m_statusLabel->setVisible(boards.isEmpty());
            if (boards.isEmpty())
                m_statusLabel->setText(tr("没有拿到板块数据，稍后刷新再试"));
            else
                m_statusLabel->clear();
            renderBoards(boards);
        },
        [this](const QString &message, int) {
            m_loading = false;
            renderStatus(tr("板块加载失败：%1").arg(message), true);
        });
}

void PublishBoardPage::renderBoards(const QVector<ForumBoard> &boards)
{
    if (!m_cardsLayout)
        return;

    ForumKit::clearLayout(m_cardsLayout);

    for (const ForumBoard &board : boards)
    {
        // 「插件分享」「投影分享」只能从「资源」发（必须带文件）、「教程分享」只能从
        // 「教程」发、「有奖问答」只能从「问答」发：帖子这里都不列出来
        if (board.slug == QLatin1String("plugin-share")
            || board.slug == QLatin1String("schematic-share")
            || board.slug == QLatin1String("tutorial-share")
            || board.slug == QLatin1String("qa"))
            continue;
        auto *card = new ForumKit::ClickableCard(this);
        card->setObjectName(QStringLiteral("communityBoardCard"));
        card->setFixedSize(336, 78);
        card->setCursor(Qt::PointingHandCursor);
        // 板块页直接铺在背景图上，补一层卡片底色，避免文字和背景糊在一起；
        // 社区页的同一套卡片在白色容器里，保持透明不受影响
        card->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "QFrame#communityBoardCard{background:@BG_CARD@;border:1px solid @BORDER@;"
            "border-radius:12px;}"
            "QFrame#communityBoardCard:hover{background:@BG_HOVER@;"
            "border-color:@PRIMARY_RGBA_38@;}")));
        card->onClick = [this, slug = board.slug, name = board.name]() {
            emit boardSelected(slug, name);
        };

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(10);

        auto *icon = new QLabel(board.icon.isEmpty() ? QStringLiteral("💬") : board.icon, card);
        icon->setObjectName(QStringLiteral("communityBoardIcon"));
        icon->setAlignment(Qt::AlignCenter);
        icon->setFixedSize(36, 36);
        cardLayout->addWidget(icon);

        auto *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);

        auto *name = new QLabel(board.name, card);
        name->setObjectName(QStringLiteral("communityBoardName"));
        textLayout->addWidget(name);

        auto *desc = new ForumKit::ElidedLabel(card);
        desc->setObjectName(QStringLiteral("communityMuted"));
        desc->setFullText(board.description.isEmpty() ? tr("进入该板块发布帖子") : board.description);
        textLayout->addWidget(desc);
        cardLayout->addLayout(textLayout, 1);

        auto *count = new QLabel(tr("%1 主题").arg(board.threads), card);
        count->setObjectName(QStringLiteral("communityBoardCount"));
        count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cardLayout->addWidget(count);

        m_cardsLayout->addWidget(card);
    }
}

void PublishBoardPage::renderStatus(const QString &text, bool withRetry)
{
    if (m_cardsLayout)
        ForumKit::clearLayout(m_cardsLayout);
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(true);
    m_retryButton->setVisible(withRetry);
}
