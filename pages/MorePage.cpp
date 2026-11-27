/**
 * @file   MorePage.cpp
 * @brief  更多功能页实现：图书馆 / 皮肤站 / 种子地图 / 投影编辑器 / 文件传输 / 内存优化 / 插件管理 / 新手引导
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 除内置入口外，还会把已启用插件声明的「更多页入口」（manifest.ui.moreEntries）
 * 追加在下方「插件入口」区：卡片标题/说明/配色/图标都来自插件清单，
 * 点卡片后由主窗口按入口类型（url / script）执行。
 */

#include "MorePage.h"

#include <QColor>
#include <QLabel>
#include <QLayoutItem>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/ForumKit.h"
#include "components/OutlinedLabel.h"
#include "components/PublishKit.h"
#include "layouts/FlowLayout.h"
#include "utils/plugin/PluginManager.h"

namespace {

/** 插件入口卡片默认渐变配色（与「插件管理」卡片一致，插件没写 color 时使用） */
const QColor kDefaultEntryColor1 = QColor(QStringLiteral("#6366F1"));
const QColor kDefaultEntryColor2 = QColor(QStringLiteral("#4338CA"));

/** 插件没提供图标时的兜底图标 */
const char *const kFallbackEntryIcon = ":/Images/Icons/nav_plugin.svg";

} // namespace

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
    layout->addWidget(PublishKit::makeSectionTitle(content, tr("全部入口")));

    auto *cardsRow = new QWidget(content);
    cardsRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *flow = new FlowLayout(cardsRow, 0, 12, 12);

    auto *libraryCard = PublishKit::makeChoiceCard(
        cardsRow, tr("方块图书馆"),
        tr("使用教程、插件开发、开放平台 API 与 MCP 文档"),
        QColor(QStringLiteral("#0EA5E9")), QColor(QStringLiteral("#0369A1")),
        QStringLiteral(":/Images/Icons/nav_library.svg"), 340, 126);
    libraryCard->onClick = [this]() { emit libraryRequested(); };
    flow->addWidget(libraryCard);

    auto *skinStationCard = PublishKit::makeChoiceCard(
        cardsRow, tr("方块皮肤站"),
        tr("衣柜、皮肤库与 3D 预览，换皮肤不用开网页"),
        QColor(QStringLiteral("#8B5CF6")), QColor(QStringLiteral("#6D28D9")),
        QStringLiteral(":/Images/Icons/nav_skins.svg"), 340, 126);
    skinStationCard->onClick = [this]() { emit skinStationRequested(); };
    flow->addWidget(skinStationCard);

    auto *seedMapCard = PublishKit::makeChoiceCard(
        cardsRow, tr("种子地图"),
        tr("填个种子就能看群系和地形，不用先有存档"),
        QColor(QStringLiteral("#10B981")), QColor(QStringLiteral("#047857")),
        QStringLiteral(":/Images/Icons/map.svg"), 340, 126);
    seedMapCard->onClick = [this]() { emit seedMapRequested(); };
    flow->addWidget(seedMapCard);

    auto *projectionCard = PublishKit::makeChoiceCard(
        cardsRow, tr("投影编辑器"),
        tr("选一个投影文件，改元数据、转版本、拆方块"),
        QColor(QStringLiteral("#F59E0B")), QColor(QStringLiteral("#B45309")),
        QStringLiteral(":/Images/Icons/brush.svg"), 340, 126);
    projectionCard->onClick = [this]() { emit projectionEditorRequested(); };
    flow->addWidget(projectionCard);

    auto *transferCard = PublishKit::makeChoiceCard(
        cardsRow, tr("文件传输"),
        tr("同一局域网直接互传，不在一个网络就组网后再传"),
        QColor(QStringLiteral("#F43F5E")), QColor(QStringLiteral("#BE123C")),
        QStringLiteral(":/Images/Icons/share.svg"), 340, 126);
    transferCard->onClick = [this]() { emit transferRequested(); };
    flow->addWidget(transferCard);

    auto *memoryCard = PublishKit::makeChoiceCard(
        cardsRow, tr("内存优化"),
        tr("看清内存被谁占着，点一下把闲置的还给系统"),
        QColor(QStringLiteral("#06B6D4")), QColor(QStringLiteral("#0E7490")),
        QStringLiteral(":/Images/Icons/memory.svg"), 340, 126);
    memoryCard->onClick = [this]() { emit memoryOptimizeRequested(); };
    flow->addWidget(memoryCard);

    auto *pluginCard = PublishKit::makeChoiceCard(
        cardsRow, tr("插件管理"),
        tr("装插件、写插件、给 AI 加工具，都在这里"),
        QColor(QStringLiteral("#6366F1")), QColor(QStringLiteral("#4338CA")),
        QStringLiteral(":/Images/Icons/nav_plugin.svg"), 340, 126);
    pluginCard->onClick = [this]() { emit pluginManageRequested(); };
    flow->addWidget(pluginCard);

    auto *onboardingCard = PublishKit::makeChoiceCard(
        cardsRow, tr("新手引导"),
        tr("第一次用启动器？从装 Java 到进游戏重新走一遍"),
        QColor(QStringLiteral("#14B8A6")), QColor(QStringLiteral("#0F766E")),
        QStringLiteral(":/Images/Icons/tip.svg"), 340, 126);
    onboardingCard->onClick = [this]() { emit onboardingRequested(); };
    flow->addWidget(onboardingCard);

    layout->addWidget(cardsRow);

    // ── 插件入口：只在有插件提供入口时出现（默认隐藏） ──
    m_pluginSection = new QWidget(content);
    m_pluginSection->setAttribute(Qt::WA_StyledBackground, false);
    auto *pluginSectionLayout = new QVBoxLayout(m_pluginSection);
    pluginSectionLayout->setContentsMargins(0, 6, 0, 0);
    pluginSectionLayout->setSpacing(10);
    pluginSectionLayout->addWidget(
        PublishKit::makeSectionTitle(m_pluginSection, tr("插件入口")));

    m_pluginCardsHost = new QWidget(m_pluginSection);
    m_pluginCardsHost->setAttribute(Qt::WA_StyledBackground, false);
    m_pluginFlow = new FlowLayout(m_pluginCardsHost, 0, 12, 12);
    pluginSectionLayout->addWidget(m_pluginCardsHost);

    m_pluginSection->setVisible(false);
    layout->addWidget(m_pluginSection);

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

void MorePage::setPluginEntries(const QList<PluginPageEntry> &entries)
{
    m_pluginEntries = entries;
    rebuildPluginCards();
}

void MorePage::rebuildPluginCards()
{
    if (!m_pluginFlow || !m_pluginSection)
        return;

    // 清掉旧卡片（直接 delete：deleteLater 要等事件循环回到主循环才真正删除，
    // 期间旧卡片仍挂在宿主上会和新卡片叠在一起）
    while (m_pluginFlow->count() > 0) {
        QLayoutItem *item = m_pluginFlow->takeAt(0);
        if (item) {
            if (QWidget *w = item->widget()) {
                m_pluginFlow->removeWidget(w);
                delete w;
            }
            delete item;
        }
    }

    if (m_pluginEntries.isEmpty()) {
        m_pluginSection->setVisible(false);
        return;
    }

    PluginManager *pm = PluginManager::instance();
    for (const PluginPageEntry &entry : m_pluginEntries) {
        const PluginInfo info = pm->pluginById(entry.pluginId);

        QColor color1 = kDefaultEntryColor1;
        QColor color2 = kDefaultEntryColor2;
        const QColor custom(entry.color);
        if (custom.isValid()) {
            color1 = custom;
            color2 = custom.darker(130);
        }

        QString iconPath = pm->pluginEntryIconPath(info, entry);
        if (iconPath.isEmpty())
            iconPath = QString::fromLatin1(kFallbackEntryIcon);

        const QString desc = entry.description.isEmpty()
                                 ? tr("由插件「%1」提供").arg(info.name)
                                 : entry.description;

        auto *card = PublishKit::makeChoiceCard(
            m_pluginCardsHost, entry.title, desc, color1, color2, iconPath, 340, 126);
        const QString pluginId = entry.pluginId;
        const QString entryId = entry.id;
        card->onClick = [this, pluginId, entryId]() {
            emit pluginEntryRequested(pluginId, entryId);
        };
        m_pluginFlow->addWidget(card);
    }

    m_pluginSection->setVisible(true);
}
