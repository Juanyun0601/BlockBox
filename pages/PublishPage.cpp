/**
 * @file   PublishPage.cpp
 * @brief  发布页实现：资源 / 帖子 / 教程三个入口
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "PublishPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/ForumKit.h"
#include "components/OutlinedLabel.h"
#include "components/PublishKit.h"
#include "layouts/FlowLayout.h"

PublishPage::PublishPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void PublishPage::buildUi()
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

    auto *pageTitle = new OutlinedLabel(tr("发布中心"), content);
    pageTitle->setObjectName(QStringLiteral("resourcesPageTitle"));
    layout->addWidget(pageTitle);

    auto *subtitle = new QLabel(tr("把你的资源或帖子分享给更多玩家"), content);
    subtitle->setObjectName(QStringLiteral("resourcesPageSubtitle"));
    layout->addWidget(subtitle);

    layout->addSpacing(10);
    layout->addWidget(PublishKit::makeSectionTitle(content, tr("我要发布")));

    auto *cardsRow = new QWidget(content);
    cardsRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *flow = new FlowLayout(cardsRow, 0, 12, 12);

    auto *resourcesCard = PublishKit::makeChoiceCard(
        cardsRow, tr("资源"), tr("模组、整合包、资源包、光影包、数据包、世界与插件"),
        QColor(QStringLiteral("#10B981")), QColor(QStringLiteral("#047857")),
        QStringLiteral(":/Images/Icons/nav_resources.svg"), 340, 126);
    resourcesCard->onClick = [this]() { emit resourcesRequested(); };
    flow->addWidget(resourcesCard);

    auto *postsCard = PublishKit::makeChoiceCard(
        cardsRow, tr("帖子"), tr("选择一个社区板块，发布你的讨论、求助或作品"),
        QColor(QStringLiteral("#6366F1")), QColor(QStringLiteral("#4338CA")),
        QStringLiteral(":/Images/Icons/nav_forum.svg"), 340, 126);
    postsCard->onClick = [this]() { emit postsRequested(); };
    flow->addWidget(postsCard);

    auto *tutorialsCard = PublishKit::makeChoiceCard(
        cardsRow, tr("教程"), tr("图文 / 视频教程与攻略：正文自动生成目录，可挂资料附件"),
        QColor(QStringLiteral("#0EA5E9")), QColor(QStringLiteral("#0369A1")),
        QStringLiteral(":/Images/Icons/nav_forum.svg"), 340, 126);
    tutorialsCard->onClick = [this]() { emit tutorialsRequested(); };
    flow->addWidget(tutorialsCard);

    auto *questionsCard = PublishKit::makeChoiceCard(
        cardsRow, tr("问答"), tr("悬赏方块提问：先托管赏金，采纳回答后发给对方"),
        QColor(QStringLiteral("#F59E0B")), QColor(QStringLiteral("#B45309")),
        QStringLiteral(":/Images/Icons/nav_forum.svg"), 340, 126);
    questionsCard->onClick = [this]() { emit questionsRequested(); };
    flow->addWidget(questionsCard);

    auto *navigationCard = PublishKit::makeChoiceCard(
        cardsRow, tr("导航"), tr("分享好玩的网站或服务器地址，点开就能一键直达"),
        QColor(QStringLiteral("#8B5CF6")), QColor(QStringLiteral("#6D28D9")),
        QStringLiteral(":/Images/Icons/globe.svg"), 340, 126);
    navigationCard->onClick = [this]() { emit navigationRequested(); };
    flow->addWidget(navigationCard);

    layout->addWidget(cardsRow);
    layout->addStretch(1);

    auto *hint = new QLabel(
        tr("模组、资源包等会跳转到 CurseForge / Modrinth 发布；插件（方块盒子插件）、帖子、教程、有奖问答和导航可以直接在方块盒子社区发布——插件发到「插件分享」板块，帖子只做交流讨论，教程发到「教程分享」板块，有奖问答发到「有奖问答」板块，导航按类型发到「网站导航」或「服务器导航」板块。"),
        content);
    hint->setObjectName(QStringLiteral("forumMuted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}
