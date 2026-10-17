/**
 * @file   BedrockResourcesPage.cpp
 * @brief  基岩版实例助手 - 资源管理页面实现
 * @author BlockBox Team
 * @date   2026-08-25
 */
#include "BedrockResourcesPage.h"

#include <algorithm>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QCursor>
#include <QEasingCurve>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/OutlinedLabel.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

namespace {
const QString kDisabledPrefix = QStringLiteral("[disabled] ");

/**
 * @brief 递归统计目录总大小（大小筛选/排序用）
 */
qint64 dirTotalSize(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
} // namespace

BedrockResourcesPage::BedrockResourcesPage(QWidget *parent)
    : QWidget(parent)
    , m_tabWorldsBtn(nullptr)
    , m_tabResourcePacksBtn(nullptr)
    , m_tabBehaviorPacksBtn(nullptr)
    , m_tabSkinPacksBtn(nullptr)
    , m_tabBar(nullptr)
    , m_tabHighlight(nullptr)
    , m_tabAnim(nullptr)
    , m_highlightPos(0)
    , m_searchEdit(nullptr)
    , m_statsLabel(nullptr)
    , m_statusCombo(nullptr)
    , m_timeCombo(nullptr)
    , m_sizeCombo(nullptr)
    , m_sortCombo(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardLayout(nullptr)
    , m_emptyLabel(nullptr)
    , m_bottomBar(nullptr)
    , m_selectAllBtn(nullptr)
    , m_openFolderBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_currentType(TypeWorlds)
    , m_allSelected(false)
{
    initUI();
    setupConnections();

    m_tabAnim = new QPropertyAnimation(this, "highlightPos", this);
    m_tabAnim->setDuration(200);
    m_tabAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &BedrockResourcesPage::applyTabThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged,
            this, &BedrockResourcesPage::applyTabThemeStyles);
}

BedrockResourcesPage::~BedrockResourcesPage() = default;

void BedrockResourcesPage::setInstancePath(const QString &path)
{
    m_instancePath = path;
    scanFiles();
}

void BedrockResourcesPage::refreshList()
{
    scanFiles();
}

// ============================================================================
// UI 构建
// ============================================================================

void BedrockResourcesPage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ---- Tab 栏 ----
    m_tabBar = new QWidget(this);
    m_tabBar->setObjectName("bedrockResTabBar");
    auto *tabBarLayout = new QHBoxLayout(m_tabBar);
    tabBarLayout->setContentsMargins(8, 0, 8, 0);
    tabBarLayout->setSpacing(2);

    m_tabWorldsBtn = createTabButton(tr("存档"), 0);
    m_tabResourcePacksBtn = createTabButton(tr("资源包"), 1);
    m_tabBehaviorPacksBtn = createTabButton(tr("行为包"), 2);
    m_tabSkinPacksBtn = createTabButton(tr("皮肤包"), 3);

    m_tabButtons = {m_tabWorldsBtn, m_tabResourcePacksBtn, m_tabBehaviorPacksBtn, m_tabSkinPacksBtn};
    for (auto *btn : m_tabButtons)
        tabBarLayout->addWidget(btn);

    m_tabHighlight = new QWidget(m_tabBar);
    m_tabHighlight->setObjectName("bedrockResTabHighlight");
    m_tabHighlight->setFixedHeight(2);
    m_tabHighlight->raise();

    mainLayout->addWidget(m_tabBar);

    // ---- 搜索栏：微透明玻璃态卡片 ----
    auto *searchBar = new QWidget(this);
    searchBar->setObjectName("filterCard");
    auto *searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(14, 10, 14, 10);
    searchLayout->setSpacing(6);

    m_searchEdit = new QLineEdit(searchBar);
    m_searchEdit->setPlaceholderText(tr("搜索..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setObjectName("bedrockResSearchEdit");
    searchLayout->addWidget(m_searchEdit, 1);

    m_statsLabel = new QLabel(searchBar);
    m_statsLabel->setObjectName("bedrockResStatsLabel");
    m_statsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    searchLayout->addWidget(m_statsLabel);

    mainLayout->addWidget(searchBar);

    // ---- 筛选行（状态/时间/大小/排序，叠加在搜索之上；顶部最多两行：搜索行 + 本行）----
    auto makeFilterCombo = [this]() -> QComboBox * {
        auto *combo = new QComboBox(this);
        combo->setObjectName("filterCombo");
        combo->setFixedHeight(28);
        combo->setMinimumWidth(80);   // 窄窗 422px：四下拉靠内容自适应，不锁 104
        return combo;
    };

    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);
    filterRow->setContentsMargins(14, 4, 14, 4);

    m_statusCombo = makeFilterCombo();
    m_statusCombo->setToolTip(tr("状态筛选"));
    m_statusCombo->addItem(tr("全部"), 0);
    m_statusCombo->addItem(tr("已启用"), 1);
    m_statusCombo->addItem(tr("已禁用"), 2);
    filterRow->addWidget(m_statusCombo);

    m_timeCombo = makeFilterCombo();
    m_timeCombo->setToolTip(tr("时间筛选"));
    m_timeCombo->addItem(tr("全部时间"), 0);
    m_timeCombo->addItem(tr("最近 7 天"), 1);
    m_timeCombo->addItem(tr("最近 30 天"), 2);
    m_timeCombo->addItem(tr("最近一年"), 3);
    filterRow->addWidget(m_timeCombo);

    m_sizeCombo = makeFilterCombo();
    m_sizeCombo->setToolTip(tr("大小筛选"));
    m_sizeCombo->addItem(tr("全部大小"), 0);
    m_sizeCombo->addItem(tr("小于 10 MB"), 1);
    m_sizeCombo->addItem(tr("10 ~ 100 MB"), 2);
    m_sizeCombo->addItem(tr("大于 100 MB"), 3);
    filterRow->addWidget(m_sizeCombo);

    m_sortCombo = makeFilterCombo();
    m_sortCombo->setObjectName("searchSortCombo");
    m_sortCombo->setToolTip(tr("排序"));
    m_sortCombo->addItem(tr("名称 A→Z"), 0);
    m_sortCombo->addItem(tr("名称 Z→A"), 1);
    m_sortCombo->addItem(tr("大小 大→小"), 2);
    m_sortCombo->addItem(tr("大小 小→大"), 3);
    m_sortCombo->addItem(tr("时间 新→旧"), 4);
    m_sortCombo->addItem(tr("时间 旧→新"), 5);
    filterRow->addWidget(m_sortCombo);

    filterRow->addStretch();
    mainLayout->addLayout(filterRow);

    // ---- 卡片滚动区 ----
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_cardContainer = new QWidget(m_scrollArea);
    m_cardLayout = new QVBoxLayout(m_cardContainer);
    m_cardLayout->setContentsMargins(8, 4, 8, 8);
    m_cardLayout->setSpacing(4);

    // 空态提示（先加入布局，无卡片时显示）
    m_emptyLabel = new QLabel(tr("暂无资源"), m_cardContainer);
    m_emptyLabel->setObjectName("bedrockResEmptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    m_cardLayout->addWidget(m_emptyLabel);

    m_cardLayout->addStretch();

    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // ---- 底部操作栏 ----
    m_bottomBar = new QWidget(this);
    m_bottomBar->setObjectName("bedrockResBottomBar");
    auto *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(8, 6, 8, 6);
    bottomLayout->setSpacing(6);

    m_selectAllBtn = new QPushButton(tr("全选"), m_bottomBar);
    m_selectAllBtn->setObjectName("bedrockResBottomBtn");
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);

    m_openFolderBtn = new QPushButton(tr("打开文件夹"), m_bottomBar);
    m_openFolderBtn->setObjectName("bedrockResBottomBtn");
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);

    m_deleteBtn = new QPushButton(tr("删除"), m_bottomBar);
    m_deleteBtn->setObjectName("bedrockResBottomBtnDelete");
    m_deleteBtn->setCursor(Qt::PointingHandCursor);

    bottomLayout->addWidget(m_selectAllBtn);
    bottomLayout->addWidget(m_openFolderBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_deleteBtn);

    m_bottomBar->hide(); // 无选中时隐藏
    mainLayout->addWidget(m_bottomBar);

    applyTabThemeStyles();
    updateBottomBarState();
}

void BedrockResourcesPage::setupConnections()
{
    connect(m_tabWorldsBtn, &QPushButton::clicked, this, [this]() { switchTab(0); });
    connect(m_tabResourcePacksBtn, &QPushButton::clicked, this, [this]() { switchTab(1); });
    connect(m_tabBehaviorPacksBtn, &QPushButton::clicked, this, [this]() { switchTab(2); });
    connect(m_tabSkinPacksBtn, &QPushButton::clicked, this, [this]() { switchTab(3); });

    connect(m_searchEdit, &QLineEdit::textChanged, this, &BedrockResourcesPage::onSearchTextChanged);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &BedrockResourcesPage::onSelectAllClicked);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &BedrockResourcesPage::onOpenFolderClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &BedrockResourcesPage::onDeleteClicked);

    connect(m_statusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedrockResourcesPage::onStatusFilterChanged);
    connect(m_timeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedrockResourcesPage::onTimeFilterChanged);
    connect(m_sizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedrockResourcesPage::onSizeFilterChanged);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedrockResourcesPage::onSortChanged);
}

// ============================================================================
// Tab 切换
// ============================================================================

QPushButton *BedrockResourcesPage::createTabButton(const QString &text, int index)
{
    auto *btn = new QPushButton(text, this);
    btn->setObjectName(QString("bedrockResTabBtn%1").arg(index));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(30);
    return btn;
}

void BedrockResourcesPage::switchTab(int index)
{
    if (index == m_currentType)
        return;

    m_currentType = static_cast<ResourceType>(index);
    for (int i = 0; i < m_tabButtons.size(); ++i)
        m_tabButtons[i]->setChecked(i == index);

    // 指示条动画
    if (m_tabAnim->state() == QPropertyAnimation::Running)
        m_tabAnim->stop();
    m_tabAnim->setStartValue(m_highlightPos);
    m_tabAnim->setEndValue(m_tabButtons[index]->x() + m_tabButtons[index]->width() / 2
                           - m_tabHighlight->width() / 2);
    m_tabAnim->start();

    deselectAllCards();
    scanFiles();
}

void BedrockResourcesPage::updateHighlightGeometry()
{
    if (m_currentType < 0 || m_currentType >= m_tabButtons.size())
        return;
    QPushButton *btn = m_tabButtons[m_currentType];
    int barWidth = btn->width() * 0.5;
    if (barWidth < 20) barWidth = 20;
    m_tabHighlight->setFixedWidth(barWidth);
    int x = btn->x() + btn->width() / 2 - barWidth / 2;
    m_tabHighlight->move(x, m_tabBar->height() - 2);
    m_highlightPos = x;
}

void BedrockResourcesPage::setHighlightPos(int pos)
{
    m_highlightPos = pos;
    m_tabHighlight->move(pos, m_tabBar->height() - 2);
}

void BedrockResourcesPage::applyTabThemeStyles()
{
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    QString textColor = ThemeManager::instance()->currentTextColor();
    QString borderColor = ThemeManager::instance()->currentBorderColor();
    QColor tc(themeColor);
    QString hoverBg = QString("rgba(%1, %2, %3, 0.12)").arg(tc.red()).arg(tc.green()).arg(tc.blue());

    m_tabBar->setStyleSheet(
        QString("#bedrockResTabBar { background: palette(window); border-bottom: 1px solid %1; }")
            .arg(borderColor));

    m_tabHighlight->setStyleSheet(
        QString("#bedrockResTabHighlight { background: %1; border-radius: 1px; }").arg(themeColor));

    QString btnStyle = QString(
        "QPushButton { border: none; border-radius: 6px; padding: 4px 10px; font-size: 12px; "
        "font-weight: 500; color: %1; background: transparent; }"
        "QPushButton:hover { background: %2; }"
        "QPushButton:checked { color: %3; }"
    ).arg(textColor, hoverBg, themeColor);

    for (auto *btn : m_tabButtons)
        btn->setStyleSheet(btnStyle);

    // 搜索框
    m_searchEdit->setStyleSheet(
        QString("QLineEdit { background: palette(base); border: 1px solid %1; border-radius: 6px; "
                "padding: 5px 8px; font-size: 12px; color: %2; }"
                "QLineEdit:focus { border: 1px solid %3; }")
            .arg(borderColor, textColor, themeColor));

    // 统计标签
    m_statsLabel->setStyleSheet(
        QString("QLabel { color: %1; font-size: 11px; background: transparent; border: none; }")
            .arg(textColor));

    // 空态
    m_emptyLabel->setStyleSheet(
        QString("QLabel { color: %1; font-size: 13px; background: transparent; border: none; }")
            .arg(textColor));

    // 底部栏按钮
    QString bottomBtnStyle = QString(
        "QPushButton { border: none; border-radius: 6px; padding: 5px 12px; font-size: 12px; "
        "color: %1; background: palette(base); border: 1px solid %2; }"
        "QPushButton:hover { background: %3; }"
    ).arg(textColor, borderColor, hoverBg);

    QString deleteBtnStyle = QString(
        "QPushButton { border: none; border-radius: 6px; padding: 5px 12px; font-size: 12px; "
        "color: #EF4444; background: palette(base); border: 1px solid #FCA5A5; }"
        "QPushButton:hover { background: #FEE2E2; }"
        "QPushButton:disabled { color: #9CA3AF; background: palette(base); border: 1px solid #E5E7EB; }"
    );

    m_selectAllBtn->setStyleSheet(bottomBtnStyle);
    m_openFolderBtn->setStyleSheet(bottomBtnStyle);
    m_deleteBtn->setStyleSheet(deleteBtnStyle);
}

// ============================================================================
// 文件扫描与显示
// ============================================================================

QString BedrockResourcesPage::resolveResourceDir() const
{
    if (m_instancePath.isEmpty())
        return {};

    switch (m_currentType)
    {
    case TypeWorlds:
        return m_instancePath + QStringLiteral("/minecraftWorlds");
    case TypeResourcePacks:
        return m_instancePath + QStringLiteral("/resource_packs");
    case TypeBehaviorPacks:
        return m_instancePath + QStringLiteral("/behavior_packs");
    case TypeSkinPacks:
        return m_instancePath + QStringLiteral("/skin_packs");
    }
    return {};
}

QString BedrockResourcesPage::typeDisplayName() const
{
    switch (m_currentType)
    {
    case TypeWorlds:       return tr("存档");
    case TypeResourcePacks: return tr("资源包");
    case TypeBehaviorPacks: return tr("行为包");
    case TypeSkinPacks:     return tr("皮肤包");
    }
    return {};
}

void BedrockResourcesPage::scanFiles()
{
    m_fileList.clear();

    QString dirPath = resolveResourceDir();
    QDir dir(dirPath);
    if (!dir.exists())
    {
        clearCards();
        showEmptyHint(true);
        updateStatsLabel();
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                                    QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : entries)
    {
        FileEntry entry;
        entry.filePath = fi.absoluteFilePath();
        entry.fileName = fi.fileName();
        entry.isDir = fi.isDir();
        entry.lastModified = fi.lastModified();
        entry.isEnabled = !entry.fileName.startsWith(kDisabledPrefix);

        // 目录条目递归统计大小（供大小筛选/排序）
        if (entry.isDir)
            entry.fileSize = dirTotalSize(fi.absoluteFilePath());
        else
            entry.fileSize = fi.size();

        m_fileList.append(entry);
    }

    // 重扫后保持用户当前选择的排序
    sortFileList();
    applyFilterAndSearch();
}

void BedrockResourcesPage::applyFilterAndSearch()
{
    clearCards();

    QString search = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();

    // 时间筛选截止点（0 = 不启用）
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime timeCutoff;
    if (m_timeFilter == 1)
        timeCutoff = now.addDays(-7);
    else if (m_timeFilter == 2)
        timeCutoff = now.addDays(-30);
    else if (m_timeFilter == 3)
        timeCutoff = now.addDays(-365);

    const qint64 k10MB = 10LL * 1024 * 1024;
    const qint64 k100MB = 100LL * 1024 * 1024;

    int visibleCount = 0;
    for (int i = 0; i < m_fileList.size(); ++i)
    {
        const FileEntry &entry = m_fileList[i];

        // 搜索 + 状态 + 时间 + 大小 全部 AND 叠加
        if (!search.isEmpty() && !entry.fileName.toLower().contains(search))
            continue;
        if (m_statusFilter == 1 && !entry.isEnabled)
            continue;
        if (m_statusFilter == 2 && entry.isEnabled)
            continue;
        if (timeCutoff.isValid() && entry.lastModified < timeCutoff)
            continue;
        if (m_sizeFilter == 1 && entry.fileSize >= k10MB)
            continue;
        if (m_sizeFilter == 2 && (entry.fileSize < k10MB || entry.fileSize > k100MB))
            continue;
        if (m_sizeFilter == 3 && entry.fileSize <= k100MB)
            continue;

        QWidget *card = createFileCard(entry);
        m_cardLayout->insertWidget(m_cardLayout->count() - 1, card);
        m_cardWidgets.append(card);
        visibleCount++;
    }

    if (visibleCount == 0 && !m_fileList.isEmpty())
    {
        // 有资源但全被筛掉
        m_emptyLabel->setText(tr("没有符合筛选条件的%1").arg(typeDisplayName()));
        m_emptyLabel->show();
    }
    else
    {
        showEmptyHint(visibleCount == 0);
    }
    updateStatsLabel();
}

void BedrockResourcesPage::clearCards()
{
    for (QWidget *w : m_cardWidgets)
        w->deleteLater();
    m_cardWidgets.clear();
    m_selectedIndices.clear();
    // 选择被整体清空，全选状态与按钮文案一并复位
    m_allSelected = false;
    m_selectAllBtn->setText(tr("全选"));
    updateBottomBarState();
}

void BedrockResourcesPage::showEmptyHint(bool show)
{
    if (!m_emptyLabel) return;
    if (show)
    {
        m_emptyLabel->setText(tr("暂无%1").arg(typeDisplayName()));
        m_emptyLabel->show();
    }
    else
    {
        m_emptyLabel->hide();
    }
}

void BedrockResourcesPage::updateStatsLabel()
{
    if (!m_statsLabel) return;
    int total = m_fileList.size();
    int visible = m_cardWidgets.size();   // 卡片只建可见项，即可见数
    int selected = m_selectedIndices.size();
    const bool filterActive = (m_searchEdit && !m_searchEdit->text().trimmed().isEmpty())
        || m_statusFilter != 0 || m_timeFilter != 0 || m_sizeFilter != 0;
    if (filterActive)
    {
        if (selected > 0)
            m_statsLabel->setText(tr("%1/%2 项").arg(selected).arg(visible));
        else
            m_statsLabel->setText(tr("%1/%2 项").arg(visible).arg(total));
    }
    else if (selected > 0)
        m_statsLabel->setText(tr("%1/%2 项").arg(selected).arg(total));
    else
        m_statsLabel->setText(tr("%1 项").arg(total));
}

void BedrockResourcesPage::sortFileList()
{
    const int mode = m_sortMode;
    // 名称比较忽略 [disabled] 前缀，禁用项与本体排在一起
    auto displayName = [](const FileEntry &e) {
        QString n = e.fileName;
        if (n.startsWith(kDisabledPrefix))
            n = n.mid(kDisabledPrefix.length());
        return n;
    };
    std::sort(m_fileList.begin(), m_fileList.end(),
              [mode, &displayName](const FileEntry &a, const FileEntry &b) {
        switch (mode)
        {
        case 1:  // 名称 Z→A
            return QString::compare(displayName(a), displayName(b), Qt::CaseInsensitive) > 0;
        case 2:  // 大小 大→小
            return a.fileSize > b.fileSize;
        case 3:  // 大小 小→大
            return a.fileSize < b.fileSize;
        case 4:  // 时间 新→旧
            return a.lastModified > b.lastModified;
        case 5:  // 时间 旧→新
            return a.lastModified < b.lastModified;
        default: // 名称 A→Z
            return QString::compare(displayName(a), displayName(b), Qt::CaseInsensitive) < 0;
        }
    });
}

void BedrockResourcesPage::onStatusFilterChanged(int index)
{
    if (!m_statusCombo)
        return;
    m_statusFilter = m_statusCombo->itemData(index).toInt();
    applyFilterAndSearch();
}

void BedrockResourcesPage::onTimeFilterChanged(int index)
{
    if (!m_timeCombo)
        return;
    m_timeFilter = m_timeCombo->itemData(index).toInt();
    applyFilterAndSearch();
}

void BedrockResourcesPage::onSizeFilterChanged(int index)
{
    if (!m_sizeCombo)
        return;
    m_sizeFilter = m_sizeCombo->itemData(index).toInt();
    applyFilterAndSearch();
}

void BedrockResourcesPage::onSortChanged(int index)
{
    if (!m_sortCombo)
        return;
    const int newMode = m_sortCombo->itemData(index).toInt();
    if (newMode == m_sortMode)
        return;
    m_sortMode = newMode;
    sortFileList();
    applyFilterAndSearch();
}

// ============================================================================
// 卡片创建与交互
// ============================================================================

QWidget *BedrockResourcesPage::createFileCard(const FileEntry &entry)
{
    auto *card = new QWidget(m_cardContainer);
    card->setObjectName("bedrockResCard");
    card->setFixedHeight(56);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("fileIndex", m_fileList.indexOf(entry));

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(10);

    // 图标块
    auto *iconLabel = new QLabel(card);
    iconLabel->setObjectName("bedrockResCardIcon");
    iconLabel->setFixedSize(36, 36);
    iconLabel->setAlignment(Qt::AlignCenter);

    // 根据资源类型选择图标颜色
    QColor grad1, grad2;
    QString iconPath;
    switch (m_currentType)
    {
    case TypeWorlds:
        grad1 = QColor("#84CC16"); grad2 = QColor("#4D7C0F");
        iconPath = ":/Images/Icons/nav_worlds.svg";
        break;
    case TypeResourcePacks:
        grad1 = QColor("#06B6D4"); grad2 = QColor("#0E7490");
        iconPath = ":/Images/Icons/nav_resourcepacks.svg";
        break;
    case TypeBehaviorPacks:
        grad1 = QColor("#8B5CF6"); grad2 = QColor("#6D28D9");
        iconPath = ":/Images/Icons/nav_scripts.svg";
        break;
    case TypeSkinPacks:
        grad1 = QColor("#F59E0B"); grad2 = QColor("#D97706");
        iconPath = ":/Images/Icons/nav_textures.svg";
        break;
    }

    // 绘制渐变图标
    QPixmap pix(36, 36);
    pix.fill(Qt::transparent);
    {
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient grad(0, 0, 36, 36);
        grad.setColorAt(0.0, grad1);
        grad.setColorAt(1.0, grad2);
        p.setBrush(grad);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(0, 0, 36, 36), 8, 8);
        QIcon icon = IconHelper::loadColoredIcon(iconPath, Qt::white, 18);
        QPixmap iconPix = icon.pixmap(18, 18);
        if (!iconPix.isNull())
            p.drawPixmap(9, 9, iconPix);
        p.end();
    }
    iconLabel->setPixmap(pix);

    // 禁用态半透明
    if (!entry.isEnabled)
    {
        auto *effect = new QGraphicsOpacityEffect(iconLabel);
        effect->setOpacity(0.4);
        iconLabel->setGraphicsEffect(effect);
    }

    layout->addWidget(iconLabel);

    // 名称 + 大小
    auto *infoWidget = new QWidget(card);
    auto *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(2);

    auto *nameLabel = new QLabel(entry.fileName, infoWidget);
    nameLabel->setObjectName("bedrockResCardName");
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(11);
    nameFont.setWeight(QFont::Medium);
    nameLabel->setFont(nameFont);
    if (!entry.isEnabled)
    {
        auto *effect = new QGraphicsOpacityEffect(nameLabel);
        effect->setOpacity(0.5);
        nameLabel->setGraphicsEffect(effect);
    }
    infoLayout->addWidget(nameLabel);

    auto *sizeLabel = new QLabel(formatFileSize(entry.fileSize), infoWidget);
    sizeLabel->setObjectName("bedrockResCardSize");
    infoLayout->addWidget(sizeLabel);

    layout->addWidget(infoWidget, 1);

    // 启用/禁用标签
    if (!entry.isEnabled)
    {
        auto *disabledTag = new QLabel(tr("已禁用"), card);
        disabledTag->setObjectName("bedrockResDisabledTag");
        layout->addWidget(disabledTag, 0, Qt::AlignVCenter);
    }

    // 右键菜单
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, [this, entry](const QPoint &pos) {
        Q_UNUSED(pos);
        QMenu menu(this);
        if (entry.isEnabled)
            menu.addAction(tr("禁用"), this, [this, entry]() {
                // 重命名目录添加 [disabled] 前缀
                QDir dir(entry.filePath);
                QString parentPath = QDir::cleanPath(entry.filePath + QStringLiteral("/.."));
                QString newName = parentPath + QStringLiteral("/") + kDisabledPrefix + entry.fileName;
                if (dir.rename(entry.filePath, newName))
                    scanFiles();
            });
        else
            menu.addAction(tr("启用"), this, [this, entry]() {
                // 移除 [disabled] 前缀
                QString name = entry.fileName;
                QString cleanName = name.mid(kDisabledPrefix.length());
                QDir dir(entry.filePath);
                QString parentPath = QDir::cleanPath(entry.filePath + QStringLiteral("/.."));
                QString newName = parentPath + QStringLiteral("/") + cleanName;
                if (dir.rename(entry.filePath, newName))
                    scanFiles();
            });
        menu.addSeparator();
        menu.addAction(tr("在文件管理器中打开"), this, [entry]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(entry.filePath));
        });
        menu.addSeparator();
        menu.addAction(tr("删除"), this, [this, entry]() {
            AppMessageBox::StandardButton reply = AppMessageBox::question(this, tr("确认删除"),
                tr("确定要删除「%1」吗？此操作不可恢复。").arg(entry.fileName),
                AppMessageBox::Yes | AppMessageBox::No);
            if (reply == AppMessageBox::Yes)
            {
                QDir dir(entry.filePath);
                if (dir.removeRecursively())
                    scanFiles();
            }
        });
        menu.exec(QCursor::pos());
    });

    // 点击处理（左键选中，由 eventFilter 完成）
    card->installEventFilter(this);

    return card;
}

bool BedrockResourcesPage::eventFilter(QObject *watched, QEvent *event)
{
    auto *card = qobject_cast<QWidget *>(watched);
    if (!card)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            int idx = card->property("fileIndex").toInt();
            if (idx >= 0 && idx < m_fileList.size())
            {
                // 如果卡片有 childIndex 属性（导航卡片模式），则发送导航信号
                bool ok = false;
                int childIndex = card->property("childIndex").toInt(&ok);
                if (ok)
                {
                    emit navItemClicked(childIndex);
                    return true;
                }
                toggleCardSelection(idx);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BedrockResourcesPage::toggleCardSelection(int idx)
{
    // idx 是 m_fileList 下标；m_cardWidgets 只含可见卡片，须按 fileIndex 属性定位
    if (m_selectedIndices.contains(idx))
        m_selectedIndices.remove(idx);
    else
        m_selectedIndices.insert(idx);

    // 更新卡片视觉
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    for (QWidget *card : m_cardWidgets)
    {
        if (card->property("fileIndex").toInt() != idx)
            continue;
        if (m_selectedIndices.contains(idx))
            card->setStyleSheet(QString("QWidget#bedrockResCard { border: 2px solid %1; }").arg(themeColor));
        else
            card->setStyleSheet(QString("QWidget#bedrockResCard { border: 1px solid %1; }")
                .arg(ThemeManager::instance()->currentBorderColor()));
        break;
    }

    updateBottomBarState();
    updateStatsLabel();
}

void BedrockResourcesPage::deselectAllCards()
{
    m_selectedIndices.clear();
    QString borderColor = ThemeManager::instance()->currentBorderColor();
    for (int i = 0; i < m_cardWidgets.size(); ++i)
    {
        m_cardWidgets[i]->setStyleSheet(
            QString("QWidget#bedrockResCard { border: 1px solid %1; }").arg(borderColor));
    }
    updateBottomBarState();
    updateStatsLabel();
}

// ============================================================================
// 底部操作栏
// ============================================================================

void BedrockResourcesPage::updateBottomBarState()
{
    if (m_bottomBar)
        m_bottomBar->setVisible(!m_selectedIndices.isEmpty());
}

void BedrockResourcesPage::onSelectAllClicked()
{
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    if (m_allSelected)
    {
        deselectAllCards();
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }
    else
    {
        // 只全选当前可见卡片，索引取自各卡片的 fileIndex 属性
        for (QWidget *card : m_cardWidgets)
        {
            const int idx = card->property("fileIndex").toInt();
            if (idx < 0 || idx >= m_fileList.size())
                continue;
            m_selectedIndices.insert(idx);
            card->setStyleSheet(
                QString("QWidget#bedrockResCard { border: 2px solid %1; }").arg(themeColor));
        }
        m_allSelected = !m_selectedIndices.isEmpty();
        m_selectAllBtn->setText(m_allSelected ? tr("取消全选") : tr("全选"));
        updateBottomBarState();
        updateStatsLabel();
    }
}

void BedrockResourcesPage::onOpenFolderClicked()
{
    QString dirPath = resolveResourceDir();
    if (!dirPath.isEmpty())
        QDir().mkpath(dirPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

void BedrockResourcesPage::onDeleteClicked()
{
    if (m_selectedIndices.isEmpty())
        return;

    QStringList names;
    for (int idx : m_selectedIndices)
    {
        if (idx >= 0 && idx < m_fileList.size())
            names.append(m_fileList[idx].fileName);
    }

    AppMessageBox::StandardButton reply = AppMessageBox::question(this, tr("确认删除"),
        tr("确定要删除选中的 %1 个项目吗？此操作不可恢复。").arg(names.size()),
        AppMessageBox::Yes | AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    int deletedCount = 0;
    for (int idx : m_selectedIndices)
    {
        if (idx >= 0 && idx < m_fileList.size())
        {
            QDir dir(m_fileList[idx].filePath);
            if (dir.removeRecursively())
                deletedCount++;
        }
    }

    if (deletedCount > 0)
        scanFiles();
}

// ============================================================================
// 搜索
// ============================================================================

void BedrockResourcesPage::onSearchTextChanged(const QString &text)
{
    Q_UNUSED(text);
    applyFilterAndSearch();
}

void BedrockResourcesPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateHighlightGeometry();
}

void BedrockResourcesPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateHighlightGeometry();
}

QString BedrockResourcesPage::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}
