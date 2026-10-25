/**
 * @file   StructureDetailDialog.cpp
 * @brief  结构详情弹窗实现（顶部「简介 / 预览 / 战利品」三个选项）
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include "StructureDetailDialog.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "layouts/FlowLayout.h"

StructureDetailDialog::StructureDetailDialog(const QString &title,
                                             const QString &wikiTitle,
                                             const QPixmap &icon,
                                             const QString &structKey,
                                             qint64 x, qint64 z,
                                             bool hasCoords,
                                             const QString &dimName,
                                             bool rare,
                                             const StructureLoot::Result &loot,
                                             QWidget *parent)
    : AppDialogBase(parent)
    , m_structKey(structKey)
    , m_wikiTitle(wikiTitle)
{
    setObjectName(QStringLiteral("structDetailDialog"));
    setWindowTitle(title);

    // 外层：居中卡片（铺满窗口的模糊遮罩由 AppDialogBase 绘制）
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("structDetailCard"));
    card->setMinimumSize(600, 430);
    card->setMaximumSize(820, 620);
    outer->addWidget(card, 0, Qt::AlignCenter);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(12);

    // 标题行：结构图标 + 名称 + 关闭按钮
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);

    if (!icon.isNull())
    {
        auto *iconLabel = new QLabel(card);
        iconLabel->setObjectName(QStringLiteral("structDetailIcon"));
        iconLabel->setPixmap(icon);
        titleRow->addWidget(iconLabel, 0, Qt::AlignVCenter);
    }

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("structDetailTitle"));
    titleRow->addWidget(titleLabel, 0, Qt::AlignVCenter);

    if (rare)
    {
        auto *rareChip = new QLabel(tr("稀有"), card);
        rareChip->setObjectName(QStringLiteral("structDetailRareChip"));
        titleRow->addWidget(rareChip, 0, Qt::AlignVCenter);
    }

    titleRow->addStretch(1);

    auto *closeBtn = new QPushButton(QStringLiteral("×"), card);
    closeBtn->setObjectName(QStringLiteral("structDetailClose"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(28, 28);
    closeBtn->setToolTip(tr("关闭"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleRow->addWidget(closeBtn, 0, Qt::AlignVCenter);

    layout->addLayout(titleRow);

    // 三页：简介填当前结构信息 + 战利品；预览 / 战利品页仍占位
    m_tabs = new QTabWidget(card);
    m_tabs->setObjectName(QStringLiteral("structDetailTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->addTab(createIntroPage(title, wikiTitle, x, z, hasCoords,
                                   dimName, rare, loot),
                   tr("简介"));
    m_tabs->addTab(createPlaceholderPage(tr("预览"),
                                         tr("结构预览正在制作中，敬请期待")),
                   tr("预览"));
    m_tabs->addTab(createPlaceholderPage(tr("战利品"),
                                         tr("战利品一览正在制作中，敬请期待")),
                   tr("战利品"));
    layout->addWidget(m_tabs, 1);
}

QWidget *StructureDetailDialog::createIntroPage(const QString &title,
                                                const QString &wikiTitle,
                                                qint64 x, qint64 z,
                                                bool hasCoords,
                                                const QString &dimName,
                                                bool rare,
                                                const StructureLoot::Result &loot)
{
    // 战利品多时内容变高：整页塞进滚动区，避免坐标 / 按钮被裁掉
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

    // 战利品：有种子时按坐标掷取；无种子回退常见概览
    if (!loot.stacks.isEmpty())
    {
        auto *lootTitle = new QLabel(
            loot.fromSeed ? tr("战利品（按种子）") : tr("常见战利品"),
            page);
        lootTitle->setObjectName(QStringLiteral("structDetailLootSection"));
        if (loot.fromSeed)
        {
            lootTitle->setToolTip(
                tr("根据当前世界种子与该结构坐标模拟箱内掷取，结果稳定；"
                   "实际生成以游戏内为准。"));
            lootTitle->setProperty("fromSeed", true);
        }
        layout->addWidget(lootTitle);

        auto *lootSpacer = new QLabel(page);
        lootSpacer->setObjectName(QStringLiteral("structDetailIntroSpacer"));
        lootSpacer->setFixedHeight(6);
        layout->addWidget(lootSpacer);

        auto *lootWrap = new QWidget(page);
        lootWrap->setObjectName(QStringLiteral("structDetailLootWrap"));
        auto *lootFlow = new FlowLayout(lootWrap, 0, 6, 6);
        lootFlow->setContentsMargins(0, 0, 0, 0);
        for (const StructureLoot::Stack &item : loot.stacks)
        {
            if (item.name.isEmpty())
                continue;
            const QString text =
                item.count > 1
                    ? QStringLiteral("%1 ×%2").arg(item.name).arg(item.count)
                    : item.name;
            auto *chip = new QLabel(text, lootWrap);
            chip->setObjectName(QStringLiteral("structDetailLootChip"));
            chip->setTextInteractionFlags(Qt::TextSelectableByMouse);
            chip->setProperty("itemName", item.name);
            chip->setProperty("itemCount", item.count);
            if (loot.fromSeed)
                chip->setProperty("fromSeed", true);
            lootFlow->addWidget(chip);
        }
        layout->addWidget(lootWrap);

        auto *lootNote = new QLabel(
            loot.fromSeed
                ? tr("已按原版权重与世界种子推算各箱结果；坐标预测与结构内箱位可能有偏差，以游戏内为准。")
                : tr("当前无可用种子，这里只列常见物品；有种子后会按坐标掷取。"),
            page);
        lootNote->setObjectName(QStringLiteral("structDetailLootNote"));
        lootNote->setWordWrap(true);
        layout->addWidget(lootNote);
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
    (void)rare; // 稀有标记在标题行芯片上展示，简介页不再重复

    auto *host = new QWidget;
    host->setObjectName(QStringLiteral("structDetailIntroHost"));
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto *scroll = new QScrollArea(host);
    scroll->setObjectName(QStringLiteral("structDetailIntroScrollArea"));
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea#structDetailIntroScrollArea {"
        " background: transparent; border: none; }"));
    hostLayout->addWidget(scroll);
    return host;
}

QWidget *StructureDetailDialog::createPlaceholderPage(const QString &title,
                                                      const QString &hint)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("structDetailPage"));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(6);
    layout->addStretch(1);

    auto *titleLabel = new QLabel(title, page);
    titleLabel->setObjectName(QStringLiteral("structDetailPlaceholderTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto *hintLabel = new QLabel(hint, page);
    hintLabel->setObjectName(QStringLiteral("structDetailPlaceholderHint"));
    hintLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(hintLabel);

    layout->addStretch(1);
    return page;
}

void StructureDetailDialog::setCurrentPage(Page page)
{
    if (m_tabs)
        m_tabs->setCurrentIndex(static_cast<int>(page));
}
