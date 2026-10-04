/**
 * @file   BedrockResourcesPage.cpp
 * @brief  基岩版实例助手 - 资源管理页面实现
 * @author BlockBox Team
 * @date   2026-08-25
 */
#include "BedrockResourcesPage.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
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
 * @brief 递归计算目录大小
 */
qint64 dirSize(const QString &path)
{
    QDir dir(path);
    qint64 size = 0;
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
    {
        if (fi.isDir())
            size += dirSize(fi.absoluteFilePath());
        else
            size += fi.size();
    }
    return size;
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

        if (m_currentType == TypeWorlds)
            entry.fileSize = dirSize(fi.absoluteFilePath());
        else
            entry.fileSize = fi.size();

        m_fileList.append(entry);
    }

    applyFilterAndSearch();
}

void BedrockResourcesPage::applyFilterAndSearch()
{
    clearCards();

    QString search = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();

    int visibleCount = 0;
    for (int i = 0; i < m_fileList.size(); ++i)
    {
        const FileEntry &entry = m_fileList[i];
        if (!search.isEmpty() && !entry.fileName.toLower().contains(search))
            continue;

        QWidget *card = createFileCard(entry);
        m_cardLayout->insertWidget(m_cardLayout->count() - 1, card);
        m_cardWidgets.append(card);
        visibleCount++;
    }

    showEmptyHint(visibleCount == 0);
    updateStatsLabel();
}

void BedrockResourcesPage::clearCards()
{
    for (QWidget *w : m_cardWidgets)
        w->deleteLater();
    m_cardWidgets.clear();
    m_selectedIndices.clear();
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
    int selected = m_selectedIndices.size();
    if (selected > 0)
        m_statsLabel->setText(tr("%1/%2 项").arg(selected).arg(total));
    else
        m_statsLabel->setText(tr("%1 项").arg(total));
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
    if (m_selectedIndices.contains(idx))
        m_selectedIndices.remove(idx);
    else
        m_selectedIndices.insert(idx);

    // 更新卡片视觉
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    if (idx >= 0 && idx < m_cardWidgets.size())
    {
        QWidget *card = m_cardWidgets[idx];
        if (m_selectedIndices.contains(idx))
            card->setStyleSheet(QString("QWidget#bedrockResCard { border: 2px solid %1; }").arg(themeColor));
        else
            card->setStyleSheet(QString("QWidget#bedrockResCard { border: 1px solid %1; }")
                .arg(ThemeManager::instance()->currentBorderColor()));
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
        for (int i = 0; i < m_fileList.size(); ++i)
        {
            m_selectedIndices.insert(i);
            if (i < m_cardWidgets.size())
            {
                m_cardWidgets[i]->setStyleSheet(
                    QString("QWidget#bedrockResCard { border: 2px solid %1; }").arg(themeColor));
            }
        }
        m_allSelected = true;
        m_selectAllBtn->setText(tr("取消全选"));
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
