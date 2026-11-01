/**
 * @file   StructureDetailDialog.cpp
 * @brief  结构详情弹窗实现（顶部「简介 / 预览 / 战利品」三个选项）
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include "StructureDetailDialog.h"

#include <QButtonGroup>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "components/VoxelViewWidget.h"
#include "layouts/FlowLayout.h"
#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/SchematicDocument.h"
#include "utils/WorldMap/StructureModelBuilder.h"

namespace {

// 战利品芯片左侧图标的逻辑边长（像素画按 16 取，缩放后仍是整数倍）
const int kLootIconSize = 16;

// 居中卡片的默认尺寸（全屏预览时临时放开，退出后再收回来）
const int kCardMinWidth = 600;
const int kCardMinHeight = 430;
const int kCardMaxWidth = 820;
const int kCardMaxHeight = 620;

/** 是否为结构里的藏宝箱（箱子 / 陷阱箱 / 木桶 / 潜影盒） */
bool isLootChestBlock(const QString &name)
{
    return name == QLatin1String("minecraft:chest")
        || name == QLatin1String("minecraft:trapped_chest")
        || name == QLatin1String("minecraft:barrel")
        || name.endsWith(QLatin1String("shulker_box"));
}

} // namespace

StructureDetailDialog::StructureDetailDialog(const QString &title,
                                             const QString &wikiTitle,
                                             const QPixmap &icon,
                                             const QString &structKey,
                                             qint64 x, qint64 z,
                                             bool hasCoords,
                                             const QString &dimName,
                                             bool rare,
                                             const StructureLoot::Result &loot,
                                             quint64 seed, bool hasSeed,
                                             const QString &instancePath,
                                             const QString &mcVersion,
                                             QWidget *parent)
    : AppDialogBase(parent)
    , m_structKey(structKey)
    , m_wikiTitle(wikiTitle)
    , m_x(x)
    , m_z(z)
    , m_seed(seed)
    , m_hasSeed(hasSeed)
    , m_instancePath(instancePath)
    , m_mcVersion(mcVersion)
    , m_supportsPreview(StructureModelBuilder::supports(structKey))
{
    setObjectName(QStringLiteral("structDetailDialog"));
    setWindowTitle(title);

    // 外层：居中卡片（铺满窗口的模糊遮罩由 AppDialogBase 绘制）
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    m_outer = outer;

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("structDetailCard"));
    card->setMinimumSize(kCardMinWidth, kCardMinHeight);
    card->setMaximumSize(kCardMaxWidth, kCardMaxHeight);
    outer->addWidget(card, 0, Qt::AlignCenter);
    m_card = card;

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(12);
    m_cardLayout = layout;

    // 标题行：结构图标 + 名称（不设关闭按钮，点击卡片外的遮罩即关闭）
    // 单独包一层容器：全屏预览时整行要能一起隐藏
    m_titleBar = new QWidget(card);
    m_titleBar->setObjectName(QStringLiteral("structDetailTitleBar"));
    auto *titleRow = new QHBoxLayout(m_titleBar);
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(10);

    if (!icon.isNull())
    {
        auto *iconLabel = new QLabel(m_titleBar);
        iconLabel->setObjectName(QStringLiteral("structDetailIcon"));
        iconLabel->setPixmap(icon);
        titleRow->addWidget(iconLabel, 0, Qt::AlignVCenter);
    }

    auto *titleLabel = new QLabel(title, m_titleBar);
    titleLabel->setObjectName(QStringLiteral("structDetailTitle"));
    titleRow->addWidget(titleLabel, 0, Qt::AlignVCenter);

    if (rare)
    {
        auto *rareChip = new QLabel(tr("稀有"), m_titleBar);
        rareChip->setObjectName(QStringLiteral("structDetailRareChip"));
        titleRow->addWidget(rareChip, 0, Qt::AlignVCenter);
    }

    titleRow->addStretch(1);

    layout->addWidget(m_titleBar);

    // 三页：简介 = 结构信息；预览 = 按种子重建三维模型；战利品 = 概率推算
    m_tabs = new QTabWidget(card);
    m_tabs->setObjectName(QStringLiteral("structDetailTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->addTab(createIntroPage(title, wikiTitle, x, z, hasCoords,
                                   dimName, rare),
                   tr("简介"));
    m_tabs->addTab(createPreviewPage(), tr("预览"));
    m_tabs->addTab(createLootPage(loot), tr("战利品"));
    layout->addWidget(m_tabs, 1);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == PreviewPage)
            ensurePreviewBuilt();
        else if (index == LootPage)
            ensureLootIcons();
    });
}

StructureDetailDialog::~StructureDetailDialog()
{
    if (m_previewView)
        m_previewView->setDocument(nullptr);
    delete m_previewDoc;
    m_previewDoc = nullptr;
}

QWidget *StructureDetailDialog::createIntroPage(const QString &title,
                                                const QString &wikiTitle,
                                                qint64 x, qint64 z,
                                                bool hasCoords,
                                                const QString &dimName,
                                                bool rare)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("structDetailPage"));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(0);

    auto *sectionTitle = new QLabel(tr("结构信息"), page);
    sectionTitle->setObjectName(QStringLiteral("structDetailIntroSection"));
    layout->addWidget(sectionTitle);

    auto *spacerTop = new QLabel(page);
    spacerTop->setObjectName(QStringLiteral("structDetailIntroSpacer"));
    spacerTop->setFixedHeight(10);
    layout->addWidget(spacerTop);

    // 信息行：左标签 / 右数值，同一 objectName 便于探针批量取
    const auto addRow = [page, layout](const QString &key,
                                       const QString &value) {
        auto *row = new QWidget(page);
        row->setObjectName(QStringLiteral("structDetailIntroRow"));
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 6, 0, 6);
        h->setSpacing(12);

        auto *keyLabel = new QLabel(key, row);
        keyLabel->setObjectName(QStringLiteral("structDetailIntroKey"));
        keyLabel->setFixedWidth(72);
        h->addWidget(keyLabel, 0, Qt::AlignTop);

        auto *valLabel = new QLabel(value, row);
        valLabel->setObjectName(QStringLiteral("structDetailIntroValue"));
        valLabel->setWordWrap(true);
        valLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        h->addWidget(valLabel, 1);

        layout->addWidget(row);
        return valLabel;
    };

    addRow(tr("名称"), title);
    if (!wikiTitle.isEmpty())
        addRow(tr("Wiki"), wikiTitle);
    addRow(tr("维度"), dimName.isEmpty() ? tr("未知") : dimName);
    if (hasCoords)
    {
        addRow(tr("坐标"),
               QStringLiteral("X %1   Z %2").arg(x).arg(z));
    }
    else
    {
        addRow(tr("坐标"), tr("暂无坐标信息"));
    }

    auto *sep = new QFrame(page);
    sep->setObjectName(QStringLiteral("structDetailIntroSep"));
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    auto *tip = new QLabel(
        hasCoords ? tr("坐标由世界种子预测，进入游戏后以实际生成为准。")
                  : tr("更多详细资料正在制作中，可先在 Minecraft Wiki 查看。"),
        page);
    tip->setObjectName(QStringLiteral("structDetailIntroTip"));
    tip->setWordWrap(true);
    layout->addWidget(tip);

    if (!wikiTitle.isEmpty())
    {
        auto *wikiBtn = new QPushButton(tr("查看 Wiki"), page);
        wikiBtn->setObjectName(QStringLiteral("structDetailWikiBtn"));
        wikiBtn->setCursor(Qt::PointingHandCursor);
        wikiBtn->setFixedHeight(30);
        const QUrl wikiUrl(
            QStringLiteral("https://zh.minecraft.wiki/w/")
            + QString::fromLatin1(QUrl::toPercentEncoding(wikiTitle)));
        wikiBtn->setProperty("wikiUrl", wikiUrl); // 探针读取，不真开浏览器
        connect(wikiBtn, &QPushButton::clicked, wikiBtn, [wikiUrl]() {
            QDesktopServices::openUrl(wikiUrl);
        });
        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch(1);
        btnRow->addWidget(wikiBtn, 0, Qt::AlignRight);
        btnRow->addStretch(1);
        layout->addLayout(btnRow);
    }

    layout->addStretch(1);
    (void)rare; // 稀有标记在标题行芯片上展示
    return page;
}

QWidget *StructureDetailDialog::createLootPage(
    const StructureLoot::Result &loot)
{
    // 战利品条目可能很多（村庄 16 类箱子加起来 80 多件），整页放进滚动区
    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("structDetailLootScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("structDetailPage"));
    scroll->setWidget(page);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(0);

    auto *sectionTitle = new QLabel(
        loot.fromSeed ? tr("战利品预测（按种子）") : tr("常见战利品"), page);
    sectionTitle->setObjectName(QStringLiteral("structDetailLootSection"));
    if (loot.fromSeed)
    {
        sectionTitle->setToolTip(
            tr("按世界种子定位到该结构，再用原版战利品表推算每个箱子里各物品"
               "的出现概率；标「必出」的物品出现概率不低于 95%。"));
        sectionTitle->setProperty("fromSeed", true);
    }
    layout->addWidget(sectionTitle);

    auto *spacerTop = new QLabel(page);
    spacerTop->setObjectName(QStringLiteral("structDetailIntroSpacer"));
    spacerTop->setFixedHeight(10);
    layout->addWidget(spacerTop);

    if (loot.stacks.isEmpty())
    {
        auto *empty = new QLabel(
            tr("该结构暂无可预测的箱子战利品。"), page);
        empty->setObjectName(QStringLiteral("structDetailLootEmpty"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        layout->addWidget(empty);
        layout->addStretch(1);
        return scroll;
    }

    // 口径说明：给的是概率，不是「这箱一定有什么」
    auto *hint = new QLabel(
        loot.fromSeed
            ? tr("按典型布局约有 %1 处宝箱。箱内物品由生成时掷出的种子决定，"
                 "这里给出「随机打开一个宝箱」时各物品的出现概率。")
                  .arg(loot.chests)
            : tr("当前无可用种子，先按原版战利品表给出各物品的出现概率。"),
        page);
    hint->setObjectName(QStringLiteral("structDetailLootHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // 分类：任一宝箱 / 各类箱子（按战利品表拆）
    if (loot.chestLoots.size() > 1)
    {
        auto *catBar = new QWidget(page);
        catBar->setObjectName(QStringLiteral("structDetailLootCats"));
        auto *catFlow = new FlowLayout(catBar, 0, 6, 6);
        catFlow->setContentsMargins(0, 0, 0, 0);

        auto *catGroup = new QButtonGroup(catBar);
        catGroup->setExclusive(true);
        const auto addCat = [this, catBar, catFlow, catGroup](
                                const QString &text, int chestIndex,
                                const QString &tip) {
            auto *btn = new QPushButton(text, catBar);
            btn->setObjectName(QStringLiteral("structDetailLootCat"));
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setProperty("chestIndex", chestIndex);
            if (!tip.isEmpty())
                btn->setToolTip(tip);
            catGroup->addButton(btn);
            catFlow->addWidget(btn);
            connect(btn, &QPushButton::clicked, this,
                    [this, chestIndex]() { populateLootChips(chestIndex); });
            return btn;
        };
        addCat(tr("任一宝箱"), -1, tr("随机打开该结构的一个宝箱时的概率"))
            ->setChecked(true);
        // 箱子数是固定小数字的结构才给「合计」：村庄把 16 类房屋都算上时
        // 合计没有意义（一个村庄不会有全部 16 类房子）
        if (loot.chests > 1 && loot.chests <= 8)
            addCat(tr("全部合计"), -2,
                   tr("把该结构所有宝箱合在一起，至少出现一次的概率"));
        for (int i = 0; i < loot.chestLoots.size(); ++i)
        {
            const StructureLoot::ChestLoot &cl = loot.chestLoots.at(i);
            const QString label = cl.count > 1
                ? tr("%1 ×%2").arg(cl.label).arg(cl.count)
                : cl.label;
            addCat(label, i, cl.tableId);
        }
        layout->addWidget(catBar);

        auto *catGap = new QLabel(page);
        catGap->setObjectName(QStringLiteral("structDetailIntroSpacer"));
        catGap->setFixedHeight(8);
        layout->addWidget(catGap);
    }

    // 芯片区：换分类时整片重造（同一时刻只留当前分类的芯片）
    m_lootChipWrap = new QWidget(page);
    m_lootChipWrap->setObjectName(QStringLiteral("structDetailLootWrap"));
    m_lootFlow = new FlowLayout(m_lootChipWrap, 0, 6, 6);
    m_lootFlow->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_lootChipWrap);

    if (loot.chestLoots.size() > 1)
    {
        m_lootEmptyHint = new QLabel(tr("这一类箱子没有可掷出的物品。"), page);
        m_lootEmptyHint->setObjectName(QStringLiteral("structDetailLootEmpty"));
        m_lootEmptyHint->setWordWrap(true);
        m_lootEmptyHint->hide();
        layout->addWidget(m_lootEmptyHint);
    }

    m_lootSummary = new QLabel(page);
    m_lootSummary->setObjectName(QStringLiteral("structDetailLootSummary"));
    m_lootSummary->setWordWrap(true);
    layout->addWidget(m_lootSummary);

    m_loot = loot;
    populateLootChips(-1);

    QString noteText = loot.fromSeed
        ? tr("概率由原版 1.21 战利品表精确推算：每个箱子独立掷取，"
             "标「必出」的物品出现概率不低于 95%，其余按概率从高到低排列。")
        : tr("有世界种子后，这里会按该结构的实际宝箱种类给出概率。");
    // 战利品表数据取自 1.21.4；更早版本个别物品（如盔甲纹饰模板）当时还没有
    const QStringList verParts = m_mcVersion.split(QLatin1Char('.'));
    if (verParts.size() >= 2 && verParts.at(1).toInt() < 21)
    {
        noteText += QLatin1Char('\n')
            + tr("当前实例是 %1，战利品数据取自 1.21，个别物品可能有出入。")
                  .arg(m_mcVersion);
    }
    auto *lootNote = new QLabel(noteText, page);
    lootNote->setObjectName(QStringLiteral("structDetailLootNote"));
    lootNote->setWordWrap(true);
    layout->addWidget(lootNote);

    if (!loot.tableIds.isEmpty())
    {
        auto *meta = new QLabel(
            tr("宝箱种类：%1 种 · 典型共 %2 处")
                .arg(loot.chestLoots.size())
                .arg(loot.chests),
            page);
        meta->setObjectName(QStringLiteral("structDetailLootMeta"));
        meta->setWordWrap(true);
        meta->setToolTip(loot.tableIds);
        layout->addWidget(meta);
    }

    layout->addStretch(1);
    return scroll;
}

void StructureDetailDialog::populateLootChips(int chestIndex)
{
    if (!m_lootFlow || !m_lootChipWrap)
        return;

    // 换分类＝整片芯片重造：图标列表与「已补图」标记一并重置，
    // 否则新芯片会沿用上一组的下标、图标错位
    for (QWidget *chip : m_lootChips)
    {
        m_lootFlow->removeWidget(chip);
        delete chip; // 立即销毁：延迟删除会让旧芯片在切换后多闪一帧
    }
    m_lootChips.clear();
    m_lootIconLabels.clear();
    m_lootItemIds.clear();
    m_lootIconsBuilt = false;

    const bool byChest = chestIndex >= 0
        && chestIndex < m_loot.chestLoots.size();
    const QList<StructureLoot::Stack> &items =
        chestIndex == -2
            ? m_loot.totals
            : (byChest ? m_loot.chestLoots.at(chestIndex).stacks : m_loot.stacks);

    int guaranteed = 0;
    for (const StructureLoot::Stack &item : items)
    {
        if (item.name.isEmpty())
            continue;
        if (item.guaranteed)
            ++guaranteed;
        QWidget *chip = createLootChip(item, m_loot.fromSeed, m_lootChipWrap);
        m_lootFlow->addWidget(chip);
        m_lootChips.append(chip);
    }

    if (m_lootSummary)
    {
        m_lootSummary->setText(
            tr("共 %1 件物品 · 必出 %2 件（出现概率 ≥95%）")
                .arg(m_lootChips.size())
                .arg(guaranteed));
    }

    if (m_lootEmptyHint)
        m_lootEmptyHint->setVisible(m_lootChips.isEmpty());
    m_lootChipWrap->setVisible(!m_lootChips.isEmpty());
    m_lootChipWrap->updateGeometry();

    // 构造期还没切到战利品页：补图留给页签切换时的 ensureLootIcons
    if (m_tabs && m_tabs->currentIndex() == LootPage)
        ensureLootIcons();
}

QWidget *StructureDetailDialog::createLootChip(const StructureLoot::Stack &item,
                                              bool fromSeed, QWidget *parent)
{
    // 芯片 = 图标 + 「名称 ×期望数量」+ 出现概率；图标位先占住，等页签真正
    // 打开时再补图
    auto *chip = new QWidget(parent);
    chip->setObjectName(QStringLiteral("structDetailLootChip"));
    chip->setAttribute(Qt::WA_StyledBackground, true);
    if (item.guaranteed)
        chip->setProperty("guaranteed", true);

    auto *row = new QHBoxLayout(chip);
    row->setContentsMargins(8, 3, 10, 3);
    row->setSpacing(6);

    auto *iconLabel = new QLabel(chip);
    iconLabel->setObjectName(QStringLiteral("structDetailLootIcon"));
    iconLabel->setFixedSize(kLootIconSize, kLootIconSize);
    iconLabel->setAlignment(Qt::AlignCenter);
    row->addWidget(iconLabel, 0, Qt::AlignVCenter);

    const QString text =
        item.count > 1
            ? QStringLiteral("%1 ×%2").arg(item.name).arg(item.count)
            : item.name;
    auto *textLabel = new QLabel(text, chip);
    textLabel->setObjectName(QStringLiteral("structDetailLootName"));
    textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(textLabel, 0, Qt::AlignVCenter);

    // 概率：必出给徽标，其余给百分比
    auto *chanceLabel = new QLabel(chip);
    chanceLabel->setObjectName(QStringLiteral("structDetailLootChance"));
    if (item.guaranteed)
    {
        chanceLabel->setText(tr("必出"));
        chanceLabel->setProperty("guaranteed", true);
    }
    else
    {
        chanceLabel->setText(
            QStringLiteral("%1%").arg(qRound(item.chance * 100.0)));
    }
    row->addWidget(chanceLabel, 0, Qt::AlignVCenter);

    const QString tip =
        tr("%1\n出现概率 %2%\n期望数量 %3 个（%4~%5）")
            .arg(item.itemId)
            .arg(QString::number(item.chance * 100.0, 'f', 1))
            .arg(item.count)
            .arg(item.minCount)
            .arg(item.maxCount);
    textLabel->setToolTip(tip);
    chip->setToolTip(tip);
    chip->setProperty("itemName", item.name);
    chip->setProperty("itemCount", item.count);
    chip->setProperty("itemChance", item.chance);
    if (fromSeed)
        chip->setProperty("fromSeed", true);

    m_lootIconLabels.append(iconLabel);
    m_lootItemIds.append(item.itemId);
    return chip;
}

void StructureDetailDialog::ensureLootIcons()
{
    if (m_lootIconsBuilt)
        return;
    m_lootIconsBuilt = true;
    if (m_lootIconLabels.isEmpty())
        return;

    // 图标来源与投影编辑器一致：实例客户端 jar 里的贴图（同一份共享图集）
    InstanceTextureLoader *loader =
        m_instancePath.isEmpty()
            ? nullptr
            : InstanceTextureLoader::sharedForInstance(m_instancePath);

    // 按屏幕缩放倍数取像素：像素画用最近邻放大到物理像素才不糊
    qreal dpr = devicePixelRatioF();
    if (dpr <= 0.0)
        dpr = 1.0;
    const int px = qBound(kLootIconSize, qRound(kLootIconSize * dpr),
                          kLootIconSize * 4);

    const int count = qMin(m_lootIconLabels.size(), m_lootItemIds.size());
    for (int i = 0; i < count; ++i)
    {
        QLabel *iconLabel = m_lootIconLabels.at(i);
        if (!iconLabel)
            continue;
        const QString itemId = m_lootItemIds.at(i);
        if (itemId.isEmpty())
        {
            iconLabel->hide();   // 没有 id 就不占位，芯片只显示文字
            continue;
        }

        QPixmap pm;
        if (loader)
        {
            QIcon icon = loader->itemIcon(itemId, px);
            if (icon.isNull())
                icon = loader->blockIcon(itemId, px);
            if (!icon.isNull())
                pm = icon.pixmap(QSize(px, px));
        }
        if (pm.isNull())
            pm = InstanceTextureLoader::swatchPixmap(
                VoxelViewWidget::blockColorOf(itemId), kLootIconSize, dpr);
        if (pm.isNull())
        {
            iconLabel->hide();
            continue;
        }
        pm.setDevicePixelRatio(dpr);
        iconLabel->setPixmap(pm);
    }
}

QWidget *StructureDetailDialog::createPreviewPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("structDetailPreviewPage"));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    m_previewVariant = new QLabel(tr("结构预览"), page);
    m_previewVariant->setObjectName(QStringLiteral("structPreviewVariant"));
    head->addWidget(m_previewVariant);
    head->addStretch(1);
    m_previewStats = new QLabel(page);
    m_previewStats->setObjectName(QStringLiteral("structPreviewStats"));
    head->addWidget(m_previewStats);

    m_previewChestToggle = new QToolButton(page);
    m_previewChestToggle->setObjectName(QStringLiteral("structPreviewChestToggle"));
    m_previewChestToggle->setText(tr("高亮藏宝箱"));
    m_previewChestToggle->setCheckable(true);
    m_previewChestToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_previewChestToggle->setAccessibleName(tr("高亮藏宝箱"));
    m_previewChestToggle->setCursor(Qt::PointingHandCursor);
    m_previewChestToggle->setEnabled(false);
    m_previewChestToggle->hide();   // 有宝箱时在 refreshPreviewChestUi 里出现
    m_previewChestToggle->setToolTip(
        tr("在模型里标出箱子、木桶等藏宝箱；被方块挡住的会透出淡金色轮廓。"
           "切换「显示层」时只高亮当前可见层里的宝箱"));
    head->addWidget(m_previewChestToggle);
    connect(m_previewChestToggle, &QToolButton::toggled, this,
            &StructureDetailDialog::applyPreviewChestHighlight);

    // 全屏预览：把预览页铺满整个窗口，模型能占满整块屏幕看细节
    m_previewFsBtn = new QToolButton(page);
    m_previewFsBtn->setObjectName(QStringLiteral("structPreviewFullscreenBtn"));
    m_previewFsBtn->setText(tr("全屏"));
    m_previewFsBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_previewFsBtn->setCursor(Qt::PointingHandCursor);
    m_previewFsBtn->setAccessibleName(tr("全屏预览"));
    m_previewFsBtn->setEnabled(false);   // 模型建好后才可用
    m_previewFsBtn->setToolTip(
        tr("把结构预览放大到整个窗口，方便整体观察结构；再点一次或按 Esc 退出"));
    head->addWidget(m_previewFsBtn);
    connect(m_previewFsBtn, &QToolButton::clicked, this,
            [this]() { setPreviewFullscreen(!m_previewFs); });
    layout->addLayout(head);

    m_previewView = new VoxelViewWidget(page);
    m_previewView->setObjectName(QStringLiteral("structPreviewView"));
    m_previewView->setMinimumHeight(300);
    m_previewView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewView->hide();
    layout->addWidget(m_previewView, 1);

    // ---- 层数条：只看其中一层，或该层及以下整段（掀开屋顶看内部用）----
    m_previewLayerBar = new QWidget(page);
    m_previewLayerBar->setObjectName(QStringLiteral("structPreviewLayerBar"));
    auto *layerRow = new QHBoxLayout(m_previewLayerBar);
    layerRow->setContentsMargins(2, 0, 2, 0);
    layerRow->setSpacing(6);

    auto *layerTitle = new QLabel(tr("显示层"), m_previewLayerBar);
    layerTitle->setObjectName(QStringLiteral("structPreviewLayerLabel"));
    layerTitle->setToolTip(tr("只看结构中的某一层，或该层及以下整段"));
    layerRow->addWidget(layerTitle);

    // 箭头用「◂ ▸」（小三角，无彩色 emoji 变体）：常见的大三角「◀ ▶」
    // 在部分系统上会被彩色 emoji 字体接管，渲染成蓝底方块。
    // 用 QToolButton 而不是 QPushButton：全局样式里有
    // 「QDialog QPushButton { min-width: 80px }」，会把按钮拉成长条。
    m_previewLayerDown = new QToolButton(m_previewLayerBar);
    m_previewLayerDown->setObjectName(QStringLiteral("structPreviewStepBtn"));
    m_previewLayerDown->setText(QStringLiteral(u"\u25C2"));
    m_previewLayerDown->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_previewLayerDown->setFixedSize(24, 24);
    m_previewLayerDown->setCursor(Qt::PointingHandCursor);
    m_previewLayerDown->setToolTip(tr("往下一层"));
    m_previewLayerDown->setAccessibleName(tr("往下一层"));
    layerRow->addWidget(m_previewLayerDown);

    m_previewLayerSlider = new QSlider(Qt::Horizontal, m_previewLayerBar);
    m_previewLayerSlider->setObjectName(QStringLiteral("structPreviewLayerSlider"));
    m_previewLayerSlider->setRange(0, 0);
    m_previewLayerSlider->setMinimumWidth(110);
    m_previewLayerSlider->setToolTip(
        tr("最左为「全部」；往右依次是自下往上的每一层"));
    layerRow->addWidget(m_previewLayerSlider, 1);

    m_previewLayerUp = new QToolButton(m_previewLayerBar);
    m_previewLayerUp->setObjectName(QStringLiteral("structPreviewStepBtn"));
    m_previewLayerUp->setText(QStringLiteral(u"\u25B8"));
    m_previewLayerUp->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_previewLayerUp->setFixedSize(24, 24);
    m_previewLayerUp->setCursor(Qt::PointingHandCursor);
    m_previewLayerUp->setToolTip(tr("往上一层"));
    m_previewLayerUp->setAccessibleName(tr("往上一层"));
    layerRow->addWidget(m_previewLayerUp);

    m_previewLayerValue = new QLabel(tr("全部"), m_previewLayerBar);
    m_previewLayerValue->setObjectName(QStringLiteral("structPreviewLayerValue"));
    m_previewLayerValue->setMinimumWidth(80);
    m_previewLayerValue->setAlignment(Qt::AlignCenter);
    layerRow->addWidget(m_previewLayerValue);

    m_previewBelowToggle = new QToolButton(m_previewLayerBar);
    m_previewBelowToggle->setObjectName(QStringLiteral("structPreviewBelowToggle"));
    m_previewBelowToggle->setText(tr("此层以下"));
    m_previewBelowToggle->setCheckable(true);
    m_previewBelowToggle->setCursor(Qt::PointingHandCursor);
    m_previewBelowToggle->setToolTip(
        tr("勾选后显示该层及以下的全部层（选中层正常、下方调暗），"
           "不勾选则只看这一层"));
    layerRow->addWidget(m_previewBelowToggle);

    m_previewLayerBar->hide();
    layout->addWidget(m_previewLayerBar);

    connect(m_previewLayerSlider, &QSlider::valueChanged, this,
            &StructureDetailDialog::applyPreviewLayer);
    connect(m_previewLayerDown, &QToolButton::clicked, this, [this]() {
        if (m_previewLayerSlider)
            m_previewLayerSlider->setValue(m_previewLayerSlider->value() - 1);
    });
    connect(m_previewLayerUp, &QToolButton::clicked, this, [this]() {
        if (m_previewLayerSlider)
            m_previewLayerSlider->setValue(m_previewLayerSlider->value() + 1);
    });
    connect(m_previewBelowToggle, &QToolButton::toggled, this, [this](bool checked) {
        if (!m_previewLayerSlider)
            return;
        int value = m_previewLayerSlider->value();
        // 还在「全部」时勾上「此层以下」：自动落到最上面一层，方便直接往下拖
        if (checked && value <= 0)
        {
            const QSignalBlocker blocker(m_previewLayerSlider);
            m_previewLayerSlider->setValue(m_previewLayerSlider->maximum());
            value = m_previewLayerSlider->value();
        }
        applyPreviewLayer(value);
    });

    m_previewHint = new QLabel(page);
    m_previewHint->setObjectName(QStringLiteral("structPreviewHint"));
    m_previewHint->setWordWrap(true);
    m_previewHint->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_previewHint);
    return page;
}

void StructureDetailDialog::ensurePreviewBuilt()
{
    if (m_previewBuilt)
        return;
    m_previewBuilt = true;
    if (!m_previewView || !m_previewHint)
        return;

    if (!m_supportsPreview)
    {
        m_previewHint->setText(tr("该结构暂无三维预览。"));
        return;
    }
    if (!m_hasSeed)
    {
        m_previewHint->setText(
            tr("尚未读取到世界种子，无法按种子生成结构预览。"));
        return;
    }

    StructureModelInfo info;
    SchematicDocument *doc = StructureModelBuilder::build(
        m_structKey, m_x, m_z, m_seed, m_hasSeed, m_mcVersion, &info);
    if (!doc)
    {
        m_previewHint->setText(info.error.isEmpty()
                                   ? tr("结构模型生成失败。")
                                   : info.error);
        return;
    }
    m_previewDoc = doc;


    if (!m_instancePath.isEmpty())
    {
        m_previewView->setTextureLoader(
            InstanceTextureLoader::sharedForInstance(m_instancePath));
        m_previewView->setShapeLoader(
            BlockShapeLoader::sharedForInstance(m_instancePath));
    }
    m_previewView->setDocument(doc);
    m_previewView->show();
    m_previewView->applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
    m_previewView->focusAll();
    if (m_previewFsBtn)
        m_previewFsBtn->setEnabled(true);

    // 层数条：范围按模型高度给，默认「全部」；只有一层就没得切
    if (m_previewLayerBar)
    {
        if (m_previewLayerSlider)
        {
            QSignalBlocker blocker(m_previewLayerSlider);
            m_previewLayerSlider->setRange(0, qMax(0, doc->sizeY()));
            m_previewLayerSlider->setValue(0);
        }
        if (m_previewBelowToggle)
        {
            QSignalBlocker blocker(m_previewBelowToggle);
            m_previewBelowToggle->setChecked(false);
        }
        m_previewLayerBar->setVisible(doc->sizeY() > 1);
        refreshPreviewLayerUi();
    }

    // 藏宝箱：扫一遍调色板，把模型里的宝箱格收起来；有就默认高亮
    collectPreviewChestCells();
    refreshPreviewChestUi();

    if (!info.variant.isEmpty())
        m_previewVariant->setText(info.variant);
    m_previewStats->setText(tr("%1×%2×%3 · %4 方块")
                                .arg(info.sizeX)
                                .arg(info.sizeY)
                                .arg(info.sizeZ)
                                .arg(info.blockCount));
    if (info.fromVanillaTemplate)
    {
        if (m_structKey == QLatin1String("desert_pyramid"))
        {
            m_previewHint->setText(tr(
                "按游戏内的生成规则逐块还原：底座、九层台阶、前侧双塔与门厅、地面"
                "陶瓦花纹、地下宝藏室（含压力板与 TNT 陷阱）的位置都与游戏内一致；"
                "只有「可疑的沙」和沙坑里少量随机沙块用普通沙子代替。"));
        }
        else if (m_structKey == QLatin1String("village"))
        {
            m_previewHint->setText(tr(
                "按原版拼图规则逐段还原：村庄风格取自所在群系，各段建筑的选择、朝向与"
                "位置都和世界种子对应，与游戏内实际生成的村庄一致；苔藓、路面风化这类"
                "随机装饰与周边地形、实体未包含。"));
        }
        else if (m_structKey == QLatin1String("abandoned_camp"))
        {
            m_previewHint->setText(tr(
                "按原版拼图规则逐段还原：帐篷款与营地款都由世界种子与所在群系决定，"
                "帐篷、篝火、箱子、木桶、铜像等方块的位置与游戏内实际生成的一致；"
                "游戏另外长出的树、地形整平与垫子等实体未包含。"));
        }
        else if (m_structKey == QLatin1String("desert_well"))
        {
            m_previewHint->setText(tr(
                "按游戏内的生成规则逐块还原：地下三层 5×5 砂岩井体、井口十字水槽、"
                "四边正中换成砂岩台阶的井沿、四角砂岩柱和 4 格高的台阶顶盖；"
                "1.20 起水底换成沙子，并各有一格沙子、砂岩是可刷扫的「可疑的沙」"
                "（位置按世界种子随机）。周边沙丘地形未包含。"));
        }
        else if (m_structKey == QLatin1String("swamp_hut"))
        {
            m_previewHint->setText(tr(
                "按游戏内的生成规则逐块还原：云杉木板地板、四角橡木立柱与吊脚、"
                "两层墙和栅栏窗、屋顶一圈转角台阶，以及屋里的炼药锅、工作台和"
                "红色蘑菇盆栽，位置都与游戏内一致；吊脚长度按沼泽水位取整，"
                "周边地形与实体未包含。"));
        }
        else if (m_structKey == QLatin1String("stronghold"))
        {
            m_previewHint->setText(tr(
                "按游戏内的生成规则逐段还原：用世界种子和要塞所在区块算出全部"
                "房间（走廊、拐角、五叉路口、楼梯、监狱、图书馆、末地传送门房间）"
                "的类型、朝向与位置，石砖、台阶、铁栏杆、门和末地传送门框架都按"
                "游戏内规则摆放，与游戏里实际生成的要塞一致（1.18 及以后版本）；"
                "石砖的裂纹/苔藓变色是随机装饰，比例与游戏一致但具体格子不同，"
                "洞穴里的缺口与周边地形、实体未包含。"));
        }
        else if (m_structKey == QLatin1String("fortress"))
        {
            m_previewHint->setText(tr(
                "按游戏内的生成规则逐段还原：用世界种子和要塞所在区块算出全部"
                "桥段、走廊、路口、楼梯塔、烈焰人房间与下界疣农田的类型、朝向"
                "和位置，下界砖、栏杆、楼梯、刷怪笼、箱子与岩浆池都按游戏内规则"
                "摆放，与游戏里实际生成的下界要塞一致（1.16 及以后版本）；"
                "桥墩向下的长度取决于当地地形，周边地形与实体未包含。"));
        }
        else if (m_structKey == QLatin1String("mineshaft"))
        {
            m_previewHint->setText(tr(
                "按游戏内的生成规则逐段还原：用世界种子和矿井所在区块算出全部"
                "主厅、巷道、路口、楼梯的类型、朝向与高度，木支架、横梁、铁轨、"
                "火把、蜘蛛网、刷怪笼和箱子矿车的铁轨都按游戏内规则摆放，与游戏里"
                "实际生成的废弃矿井一致（1.13 及以后版本）；木支架只在顶上压着岩层"
                "时才会搭，游戏里矿井穿过洞穴、含水层时会少一部分，预览按「四周是"
                "实心岩层」补齐，周边地形与实体未包含。"));
        }
        else
        {
            m_previewHint->setText(tr(
                "模型逐块还原自原版结构模板（与游戏内数据一致），位置与朝向按世界种子"
                "推定；周边地形实体等未包含。"));
        }
    }
    else
    {
        m_previewHint->setText(tr(
            "按世界种子与坐标重建的结构主体，方块与朝向为程序化近似；"
            "实际生成以游戏内为准。"));
    }
}

void StructureDetailDialog::applyPreviewLayer(int layer)
{
    if (!m_previewView || !m_previewDoc || !m_previewDoc->isLoaded())
        return;

    const int sy = m_previewDoc->sizeY();
    if (layer <= 0 || sy <= 0)
    {
        // 滑块最左端：显示全部
        m_previewView->setLayerView(-1, VoxelViewWidget::LayerMode::All);
    }
    else
    {
        const int index = qBound(0, layer - 1, sy - 1);
        const bool below =
            m_previewBelowToggle && m_previewBelowToggle->isChecked();
        m_previewView->setLayerView(index,
                                    below ? VoxelViewWidget::LayerMode::Below
                                          : VoxelViewWidget::LayerMode::Single);
    }
    refreshPreviewLayerUi();
}

void StructureDetailDialog::refreshPreviewLayerUi()
{
    if (!m_previewLayerSlider || !m_previewDoc || !m_previewDoc->isLoaded())
        return;

    const int sy = m_previewDoc->sizeY();
    const int value = m_previewLayerSlider->value();
    if (m_previewLayerValue)
    {
        m_previewLayerValue->setText(
            value <= 0 ? tr("全部（%1 层）").arg(sy)
                       : tr("第 %1 / %2 层").arg(value).arg(sy));
    }
    if (m_previewLayerDown)
        m_previewLayerDown->setEnabled(value > 0);
    if (m_previewLayerUp)
        m_previewLayerUp->setEnabled(value < sy);
}

void StructureDetailDialog::collectPreviewChestCells()
{
    m_previewChestCells.clear();
    if (!m_previewDoc || !m_previewDoc->isLoaded())
        return;

    // 先按调色板找出宝箱类条目，再扫一遍体积索引：大结构也只需一次逐格比较
    QSet<int> paletteHits;
    const int paletteSize = m_previewDoc->paletteSize();
    for (int p = 0; p < paletteSize; ++p)
    {
        if (isLootChestBlock(m_previewDoc->paletteEntry(p).name))
            paletteHits.insert(p);
    }
    if (paletteHits.isEmpty())
        return;

    const QList<int> &indices = m_previewDoc->indices();
    const int volume = qMin(m_previewDoc->volume(), int(indices.size()));
    m_previewChestCells.reserve(64);
    for (int i = 0; i < volume; ++i)
    {
        if (paletteHits.contains(indices.at(i)))
            m_previewChestCells.insert(i);
    }
}

void StructureDetailDialog::refreshPreviewChestUi()
{
    if (!m_previewChestToggle)
        return;

    const int count = m_previewChestCells.size();
    if (count <= 0)
    {
        m_previewChestToggle->setChecked(false);
        m_previewChestToggle->setEnabled(false);
        m_previewChestToggle->hide();
        if (m_previewView)
            m_previewView->clearHighlight();
        return;
    }

    m_previewChestToggle->setEnabled(true);
    m_previewChestToggle->show();
    m_previewChestToggle->setText(tr("高亮藏宝箱 %1").arg(count));
    m_previewChestToggle->setToolTip(
        tr("在模型里标出箱子、木桶等藏宝箱；被方块挡住的会透出淡金色轮廓。"
           "切换「显示层」时只高亮当前可见层里的宝箱"));
    {
        QSignalBlocker blocker(m_previewChestToggle);
        m_previewChestToggle->setChecked(true);
    }
    if (m_previewView)
        m_previewView->setHighlightCells(m_previewChestCells);
}

void StructureDetailDialog::applyPreviewChestHighlight(bool on)
{
    if (!m_previewView)
        return;
    if (on && !m_previewChestCells.isEmpty())
        m_previewView->setHighlightCells(m_previewChestCells);
    else
        m_previewView->clearHighlight();
}

void StructureDetailDialog::setCurrentPage(Page page)
{
    if (m_tabs)
        m_tabs->setCurrentIndex(static_cast<int>(page));
    if (page == PreviewPage)
        ensurePreviewBuilt();
    else if (page == LootPage)
        ensureLootIcons();
}

void StructureDetailDialog::setPreviewFullscreen(bool on)
{
    // 没有可用的三维模型就不进全屏（按钮此时也是灰的，这里再兜一层）
    if (on && !m_previewDoc)
        return;
    if (m_previewFs == on)
        return;

    // 全屏只针对预览页：先切过去，免得别的页被藏在页签条后面
    if (on && m_tabs && m_tabs->currentIndex() != PreviewPage)
        setCurrentPage(PreviewPage);

    m_previewFs = on;

    if (m_card)
    {
        if (on)
        {
            m_card->setMinimumSize(0, 0);
            m_card->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
        else
        {
            m_card->setMaximumSize(kCardMaxWidth, kCardMaxHeight);
            m_card->setMinimumSize(kCardMinWidth, kCardMinHeight);
        }
    }
    // 居中 → 铺满：去掉布局项的对齐，卡片才会吃满整个窗口
    if (m_outer && m_card)
        m_outer->setAlignment(m_card, on ? Qt::Alignment() : Qt::AlignCenter);

    // 铺满时把卡片的圆角与描边去掉（QSS 用动态属性区分两种状态）
    if (m_card)
    {
        m_card->setProperty("fs", on);
        m_card->style()->unpolish(m_card);
        m_card->style()->polish(m_card);
        m_card->update();
    }

    if (m_cardLayout)
        m_cardLayout->setContentsMargins(on ? 14 : 22, on ? 12 : 18,
                                         on ? 14 : 22, on ? 12 : 18);

    // 全屏时标题行与页签条一并收起来，画面里只剩结构预览本身
    if (m_titleBar)
        m_titleBar->setVisible(!on);
    if (m_tabs)
    {
        if (QTabBar *bar = m_tabs->tabBar())
        {
            // 只隐藏页签条时 Qt 会按它的 sizeHint 继续预留高度，
            // 再压到 0 高才能把这段空间让给模型
            bar->setMaximumHeight(on ? 0 : QWIDGETSIZE_MAX);
            bar->setVisible(!on);
        }
    }

    if (m_previewFsBtn)
    {
        m_previewFsBtn->setText(on ? tr("退出全屏") : tr("全屏"));
        m_previewFsBtn->setToolTip(
            on ? tr("回到窗口大小（也可以按 Esc 退出）")
               : tr("把结构预览放大到整个窗口，方便整体观察结构；"
                    "再点一次或按 Esc 退出"));
    }

    // 视口大小变了，重新取景，模型始终完整落在画面里
    if (m_previewView && m_previewDoc)
    {
        QTimer::singleShot(0, this, [this]() {
            if (!m_previewView)
                return;
            m_previewView->focusAll();
            if (m_previewFs)
                m_previewView->setFocus(Qt::OtherFocusReason);
        });
    }
}

void StructureDetailDialog::keyPressEvent(QKeyEvent *event)
{
    // 全屏预览时 Esc 先退出全屏，而不是直接关掉整个弹窗
    if (event->key() == Qt::Key_Escape && m_previewFs)
    {
        setPreviewFullscreen(false);
        event->accept();
        return;
    }
    AppDialogBase::keyPressEvent(event);
}
