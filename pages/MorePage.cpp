/**
 * @file   MorePage.cpp
 * @brief  更多功能页实现：分类卡片网格（联机 / 皮肤 / 图书馆 / 任务 / 年报 / 账户 / 引导）
 * @author BlockBox Team
 * @date   2026-11-26
 */

#include "MorePage.h"

#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/ForumKit.h"
#include "components/OutlinedLabel.h"
#include "components/PublishKit.h"
#include "layouts/FlowLayout.h"

MorePage::MorePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void MorePage::buildUi()
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

    auto *pageTitle = new OutlinedLabel(tr("更多功能"), content);
    pageTitle->setObjectName(QStringLiteral("resourcesPageTitle"));
    layout->addWidget(pageTitle);

    auto *subtitle = new QLabel(tr("侧边栏一列放不下的功能都收在这里，点卡片直接进"), content);
    subtitle->setObjectName(QStringLiteral("resourcesPageSubtitle"));
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // ── 第一组：社区与联机 ──
    layout->addWidget(PublishKit::makeSectionTitle(content, tr("社区与联机")));

    auto *communityRow = new QWidget(content);
    communityRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *communityFlow = new FlowLayout(communityRow, 0, 12, 12);

    auto *lobbyCard = PublishKit::makeChoiceCard(
        communityRow, tr("联机大厅"),
        tr("找房间、一键加入，或者自己开房让朋友连进来"),
        QColor(QStringLiteral("#14B8A6")), QColor(QStringLiteral("#0F766E")),
        QStringLiteral(":/Images/Icons/nav_multiplayer.svg"), 340, 126);
    lobbyCard->onClick = [this]() { emit lobbyRequested(); };
    communityFlow->addWidget(lobbyCard);

    auto *skinStationCard = PublishKit::makeChoiceCard(
        communityRow, tr("皮肤站"),
        tr("衣柜、皮肤库与 3D 预览，换皮肤不用开网页"),
        QColor(QStringLiteral("#8B5CF6")), QColor(QStringLiteral("#6D28D9")),
        QStringLiteral(":/Images/Icons/nav_skins.svg"), 340, 126);
    skinStationCard->onClick = [this]() { emit skinStationRequested(); };
    communityFlow->addWidget(skinStationCard);

    auto *libraryCard = PublishKit::makeChoiceCard(
        communityRow, tr("方块图书馆"),
        tr("使用教程、插件开发、开放平台 API 与 MCP 文档"),
        QColor(QStringLiteral("#0EA5E9")), QColor(QStringLiteral("#0369A1")),
        QStringLiteral(":/Images/Icons/nav_library.svg"), 340, 126);
    libraryCard->onClick = [this]() { emit libraryRequested(); };
    communityFlow->addWidget(libraryCard);

    layout->addWidget(communityRow);
    layout->addSpacing(6);

    // ── 第二组：我的与帮助 ──
    layout->addWidget(PublishKit::makeSectionTitle(content, tr("我的与帮助")));

    auto *mineRow = new QWidget(content);
    mineRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *mineFlow = new FlowLayout(mineRow, 0, 12, 12);

    auto *taskCard = PublishKit::makeChoiceCard(
        mineRow, tr("任务中心"),
        tr("下载、安装、更新的进度和以前跑过的任务都在这里"),
        QColor(QStringLiteral("#F59E0B")), QColor(QStringLiteral("#B45309")),
        QStringLiteral(":/Images/Icons/download.svg"), 340, 126);
    taskCard->onClick = [this]() { emit taskCenterRequested(); };
    mineFlow->addWidget(taskCard);

    auto *reportCard = PublishKit::makeChoiceCard(
        mineRow, tr("年度报告"),
        tr("这一年玩了多久、挖了多少方块、去过哪些地方"),
        QColor(QStringLiteral("#EC4899")), QColor(QStringLiteral("#BE185D")),
        QStringLiteral(":/Images/Icons/sparkles.svg"), 340, 126);
    reportCard->onClick = [this]() { emit annualReportRequested(); };
    mineFlow->addWidget(reportCard);

    auto *accountCard = PublishKit::makeChoiceCard(
        mineRow, tr("账户管理"),
        tr("登录方块盒子账号，管理皮肤、创作内容与设置"),
        QColor(QStringLiteral("#6366F1")), QColor(QStringLiteral("#4338CA")),
        QStringLiteral(":/Images/Icons/smile.svg"), 340, 126);
    accountCard->onClick = [this]() { emit accountRequested(); };
    mineFlow->addWidget(accountCard);

    auto *onboardingCard = PublishKit::makeChoiceCard(
        mineRow, tr("新手引导"),
        tr("第一次用启动器？从装 Java 到进游戏重新走一遍"),
        QColor(QStringLiteral("#10B981")), QColor(QStringLiteral("#047857")),
        QStringLiteral(":/Images/Icons/tip.svg"), 340, 126);
    onboardingCard->onClick = [this]() { emit onboardingRequested(); };
    mineFlow->addWidget(onboardingCard);

    layout->addWidget(mineRow);
    layout->addStretch(1);

    auto *hint = new QLabel(
        tr("这些入口在左侧「更多」分类下也能直接点到。没找到想要的功能？可以在「反馈」里告诉我们，"
           "或者到「社区」搜一搜别的玩家是怎么玩的。"),
        content);
    hint->setObjectName(QStringLiteral("forumMuted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}
