#include "ContentListPage.h"
#include "components/BlurLoadingOverlay.h"
#include "components/FavoriteFolderDialog.h"
#include "components/LocalCategoryDialog.h"
#include "components/MasonryContentCard.h"
#include "layouts/MasonryLayout.h"
#include "utils/IconHelper.h"
#include "utils/LocalCategoryManager.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include "components/AppMessageBox.h"
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSpacerItem>
#include <QUrl>
#include <QtConcurrent>

ContentListPage::ContentListPage(ContentType contentType, QWidget *parent)
    : QWidget(parent)
    , m_contentType(contentType)
    , m_config(ContentTypeConfig::getConfig(contentType))
    , m_instanceCombo(nullptr)
    , m_filterCombo(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryCombo(nullptr)
    , m_categoryManageBtn(nullptr)
    , m_refreshBtn(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_cardFlowLayout(nullptr)
    , m_viewSwitch(nullptr)
    , m_viewMode(ContentViewSwitch::List)
    , m_loadingOverlay(nullptr)
    , m_emptyLabel(nullptr)
    , m_currentFilter(FilterAll)
    , m_isLoading(false)
{
    m_categoryManager = new LocalCategoryManager(this);
    m_categoryManager->setResourceType(m_config.folderName);
    initUI();
    setupConnections();
    loadInstances();
}

ContentListPage::~ContentListPage()
{
}

void ContentListPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // Top bar
    QHBoxLayout *topBar = new QHBoxLayout();
    QLabel *instLabel = new QLabel(tr("实例:"));
    instLabel->setObjectName("filterLabel");
    m_instanceCombo = new QComboBox();
    m_instanceCombo->setObjectName("instanceCombo");
    m_instanceCombo->setFixedHeight(32);
    m_instanceCombo->setMinimumWidth(200);
    QLabel *filterLabel = new QLabel(tr("筛选:"));
    filterLabel->setObjectName("filterLabel");
    m_filterCombo = new QComboBox();
    m_filterCombo->setObjectName("filterCombo");
    m_filterCombo->setFixedHeight(32);
    m_filterCombo->addItem(tr("全部"), FilterAll);
    m_filterCombo->addItem(tr("已启用"), FilterEnabled);
    m_filterCombo->addItem(tr("已禁用"), FilterDisabled);
    // 分类筛选（本地资源手动分类）
    QLabel *categoryLabel = new QLabel(tr("分类:"));
    categoryLabel->setObjectName("filterLabel");
    m_categoryCombo = new QComboBox();
    m_categoryCombo->setObjectName("categoryCombo");
    m_categoryCombo->setFixedHeight(32);
    m_categoryCombo->setMinimumWidth(130);
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categoryCombo->setMinimumContentsLength(5);
    reloadCategoryCombo();
    m_categoryManageBtn = new QPushButton(tr("管理分类"));
    m_categoryManageBtn->setObjectName("categoryManageBtn");
    m_categoryManageBtn->setFixedHeight(32);
    m_categoryManageBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn = new QPushButton(tr("刷新"));
    m_refreshBtn->setObjectName("bottomActionBtn");
    m_refreshBtn->setFixedHeight(32);
    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("statusLabel");
    topBar->addWidget(instLabel);
    topBar->addWidget(m_instanceCombo);
    topBar->addWidget(filterLabel);
    topBar->addWidget(m_filterCombo);
    topBar->addWidget(categoryLabel);
    topBar->addWidget(m_categoryCombo);
    topBar->addWidget(m_categoryManageBtn);
    topBar->addWidget(m_refreshBtn);
    topBar->addWidget(m_statsLabel);

    // 视图切换（列表式 / 瀑布流），偏好按内容类型持久化
    m_viewSwitch = new ContentViewSwitch(this);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted(viewPrefKey(), ContentViewSwitch::List));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    topBar->addWidget(m_viewSwitch);
    topBar->addStretch();
    mainLayout->addLayout(topBar);

    // Scroll area
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
    if (m_viewMode == ContentViewSwitch::List)
    {
        m_cardGridLayout = new QGridLayout(m_cardContainer);
        m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
        m_cardGridLayout->setSpacing(8);
    }
    else
    {
        m_cardFlowLayout = new MasonryLayout(m_cardContainer, 0, 10, 10);
        m_cardFlowLayout->setContentsMargins(14, 0, 14, 0);
    }
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // Blur loading overlay
    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    // Empty hint label
    m_emptyLabel = new QLabel(m_config.emptyHint);
    m_emptyLabel->setObjectName("exampleContentLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    mainLayout->addWidget(m_emptyLabel);
}

void ContentListPage::setupConnections()
{
    connect(m_instanceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentListPage::onInstanceChanged);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentListPage::onFilterChanged);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentListPage::onCategoryFilterChanged);
    connect(m_categoryManageBtn, &QPushButton::clicked,
            this, &ContentListPage::onCategoryManageClicked);
    connect(m_categoryManager, &LocalCategoryManager::categoriesChanged,
            this, &ContentListPage::onCategoriesChanged);
    connect(m_categoryManager, &LocalCategoryManager::assignmentsChanged,
            this, &ContentListPage::onAssignmentsChanged);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ContentListPage::onRefreshClicked);
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &ContentListPage::onViewModeChanged);
}

void ContentListPage::loadInstances()
{
    m_instanceCombo->clear();
    m_instanceCombo->addItem(tr("请选择实例"), "");
    QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    for (const InstanceFolderInfo &folder : folders)
    {
        m_instanceCombo->addItem(folder.name, folder.path);
    }
}

void ContentListPage::onInstanceChanged(int index)
{
    Q_UNUSED(index);
    QString instancePath = m_instanceCombo->currentData().toString();
    m_categoryManager->setInstancePath(instancePath);
    m_currentCategoryId.clear();
    reloadCategoryCombo();
    if (!instancePath.isEmpty())
    {
        refreshContentList();
    }
}

void ContentListPage::onRefreshClicked()
{
    refreshContentList();
}

void ContentListPage::onFilterChanged(int index)
{
    Q_UNUSED(index);
    m_currentFilter = m_filterCombo->currentData().toInt();
    applyFilter();
}

void ContentListPage::refreshContentList()
{
    QString instancePath = m_instanceCombo->currentData().toString();
    if (instancePath.isEmpty())
    {
        return;
    }
    m_isLoading = true;
    showLoading(true);
    showEmptyHint(false);
    clearCards();

    QString folderPath = instancePath + "/" + m_config.folderName;
    doScan(folderPath);
}

void ContentListPage::doScan(const QString &folderPath)
{
    QtConcurrent::run([this, folderPath]()
    {
        LocalModList result;
        result.mods.clear();
        result.enabledCount = 0;
        result.disabledCount = 0;
        result.totalCount = 0;

        QDir dir(folderPath);
        if (!dir.exists())
        {
            QMetaObject::invokeMethod(this, [this, result]()
            {
                onScanFinished(result);
            }, Qt::QueuedConnection);
            return;
        }

        if (m_contentType == ContentType::World)
        {
            // Scan subdirectories that contain a level.dat file
            QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &entry : entries)
            {
                QString dirName = entry.fileName();
                bool isDisabled = dirName.endsWith(".disabled");
                QString worldPath = entry.absoluteFilePath();

                // Check for level.dat
                QFileInfo levelDat(worldPath + "/level.dat");
                if (!levelDat.exists())
                {
                    continue;
                }

                ModInfo info;
                info.fileName = dirName;
                info.filePath = worldPath;
                if (isDisabled)
                {
                    info.name = dirName.left(dirName.length() - 9);
                }
                else
                {
                    info.name = dirName;
                }
                info.enabled = !isDisabled;
                result.mods.append(info);
            }
        }
        else
        {
            // Scan files matching configured extensions
            QStringList nameFilters;
            for (const QString &ext : m_config.extensions)
            {
                nameFilters << ext << (ext + ".disabled");
            }

            QFileInfoList entries = dir.entryInfoList(nameFilters, QDir::Files);
            for (const QFileInfo &entry : entries)
            {
                QString fileName = entry.fileName();
                bool isDisabled = fileName.endsWith(".disabled");

                ModInfo info;
                info.fileName = fileName;
                info.filePath = entry.absoluteFilePath();
                info.fileSize = entry.size();
                if (isDisabled)
                {
                    info.name = fileName.left(fileName.length() - 9);
                }
                else
                {
                    info.name = fileName;
                }
                info.enabled = !isDisabled;
                result.mods.append(info);
            }
        }

        // Sort by name (case insensitive)
        std::sort(result.mods.begin(), result.mods.end(),
                  [](const ModInfo &a, const ModInfo &b)
                  {
                      return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                  });

        // Count enabled/disabled/total
        for (const auto &mod : qAsConst(result.mods))
        {
            if (mod.enabled)
            {
                ++result.enabledCount;
            }
            else
            {
                ++result.disabledCount;
            }
        }
        result.totalCount = result.mods.size();

        QMetaObject::invokeMethod(this, [this, result]()
        {
            onScanFinished(result);
        }, Qt::QueuedConnection);
    });
}

void ContentListPage::onScanFinished(const LocalModList &result)
{
    m_isLoading = false;
    showLoading(false);
    m_modList = result;
    clearCards();

    for (const ModInfo &info : result.mods)
    {
        addCard(info);
    }

    placeCards();
    applyFilter();

    QString displayName = m_config.displayName;
    m_statsLabel->setText(tr("共 %1 个%2 (%3 启用, %4 禁用)")
        .arg(result.totalCount)
        .arg(displayName)
        .arg(result.enabledCount)
        .arg(result.disabledCount));

    if (result.totalCount == 0)
    {
        showEmptyHint(true);
    }
}

QString ContentListPage::viewPrefKey() const
{
    return QStringLiteral("content_list_%1").arg(m_config.folderName);
}

/* 列表式卡片（自 onScanFinished 内联构建抽取，行为不变） */
QWidget *ContentListPage::createListCard(const ModInfo &info)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("modFilePath", info.filePath);
    card->setProperty("modFileName", info.fileName);
    card->setProperty("modEnabled", info.enabled);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->installEventFilter(this);
    connect(card, &QWidget::customContextMenuRequested, this, &ContentListPage::onContentContextMenu);

    QHBoxLayout *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        QPixmap iconPix(48, 48);
        QColor iconColor = info.enabled ? QColor("#4CAF50") : QColor("#9E9E9E");
        iconPix.fill(iconColor.lighter(180));
        QPainter painter(&iconPix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        QFont font;
        font.setBold(true);
        font.setPixelSize(24);
        painter.setFont(font);
        QString letter = info.name.isEmpty() ? "?" : info.name.left(1).toUpper();
        painter.drawText(iconPix.rect(), Qt::AlignCenter, letter);
        painter.end();
        iconLabel->setPixmap(iconPix);
    }
    mainLayout->addWidget(iconLabel);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QString displayName = info.name.isEmpty() ? info.fileName : info.name;
    QLabel *nameLabel = new QLabel(displayName);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    if (!info.latestVersion.isEmpty()) {
        QLabel *verChip = new QLabel(info.latestVersion);
        verChip->setObjectName("modChip");
        verChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(verChip);
    }
    if (!info.loaderType.isEmpty()) {
        QLabel *loaderChip = new QLabel(info.loaderType);
        loaderChip->setObjectName("modChip");
        loaderChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(loaderChip);
    }

    // 分类标签（手动分类，默认隐藏，样式见 style.qss categoryChip="true"）
    QLabel *categoryChip = new QLabel();
    categoryChip->setObjectName("modChip");
    categoryChip->setProperty("categoryChip", true);
    const QString listCatName = m_categoryManager->categoryNameOf(info.fileName);
    if (!listCatName.isEmpty())
    {
        categoryChip->setText(listCatName);
        categoryChip->show();
    }
    chipsLayout->addWidget(categoryChip);

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
    mainLayout->addLayout(infoLayout, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton* {
        QPushButton *btn = new QPushButton();
        btn->setObjectName("modCardActionBtn");
        btn->setProperty("cardRole", "actionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
        btn->setIconSize(QSize(18, 18));
        return btn;
    };

    ModInfo capturedInfo = info;

    QPushButton *toggleBtn = createBtn(
        ":/Images/Icons/install.svg",
        info.enabled ? tr("禁用") : tr("启用"));
    connect(toggleBtn, &QPushButton::clicked, this, [this, capturedInfo]() {
        onToggleMod(capturedInfo);
    });
    btnLayout->addWidget(toggleBtn);

    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, this, [this, capturedInfo]() {
        onDeleteMod(capturedInfo);
    });
    btnLayout->addWidget(deleteBtn);

    mainLayout->addLayout(btnLayout);

    return card;
}

/* 瀑布流卡片（复用 MasonryContentCard 组件，补启用状态/大小芯片） */
QWidget *ContentListPage::createMasonryCard(const ModInfo &info)
{
    // 副本仅用于卡片构建：注入稳定 id/来源，使各卡片渐变色随路径散列变化
    ModInfo cardInfo = info;
    if (cardInfo.source.isEmpty())
        cardInfo.source = m_config.folderName;
    if (cardInfo.id.isEmpty())
        cardInfo.id = cardInfo.filePath;

    QList<MasonryContentCard::ActionSpec> actions;
    ModInfo capturedInfo = info;

    actions.append({ QStringLiteral(":/Images/Icons/install.svg"),
                     info.enabled ? tr("禁用") : tr("启用"), QColor(),
                     [this, capturedInfo]() { onToggleMod(capturedInfo); } });

    actions.append({ QStringLiteral(":/Images/Icons/folder.svg"), tr("打开文件位置"), QColor(),
                     [capturedInfo]() {
                         QFileInfo fi(capturedInfo.filePath);
                         QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
                     } });

    actions.append({ QStringLiteral(":/Images/Icons/delete.svg"), tr("删除"), QColor("#F44336"),
                     [this, capturedInfo]() { onDeleteMod(capturedInfo); } });

    QWidget *card = MasonryContentCard::build(cardInfo, m_cardContainer, actions);

    // 兼容既有逻辑的属性与事件（右键菜单 / 点击打开详情）
    card->setProperty("modFilePath", info.filePath);
    card->setProperty("modFileName", info.fileName);
    card->setProperty("modEnabled", info.enabled);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->installEventFilter(this);
    connect(card, &QWidget::customContextMenuRequested, this, &ContentListPage::onContentContextMenu);

    // chips：启用状态 / 文件大小 / 分类（追加到 chips 行末尾的 stretch 之前）
    if (QWidget *chipsWidget = card->findChild<QWidget *>("contentCardChips"))
    {
        if (QHBoxLayout *chipsLayout = qobject_cast<QHBoxLayout *>(chipsWidget->layout()))
        {
            if (!info.enabled)
            {
                QLabel *disabledChip = new QLabel(tr("已禁用"));
                disabledChip->setObjectName("contentChip");
                chipsLayout->insertWidget(chipsLayout->count() - 1, disabledChip);
            }
            if (info.fileSize > 0)
            {
                double mb = info.fileSize / 1024.0 / 1024.0;
                QLabel *sizeChip = new QLabel(mb >= 1.0
                    ? QString("%1 MB").arg(mb, 0, 'f', 1)
                    : QString("%1 KB").arg(info.fileSize / 1024.0, 0, 'f', 0));
                sizeChip->setObjectName("contentChip");
                chipsLayout->insertWidget(chipsLayout->count() - 1, sizeChip);
            }

            // 分类标签（手动分类，默认隐藏）
            QLabel *categoryChip = new QLabel();
            categoryChip->setObjectName("contentChip");
            categoryChip->setProperty("categoryChip", true);
            const QString catName = m_categoryManager->categoryNameOf(info.fileName);
            if (!catName.isEmpty())
            {
                categoryChip->setText(catName);
                categoryChip->show();
            }
            chipsLayout->insertWidget(chipsLayout->count() - 1, categoryChip);
        }
    }

    return card;
}

void ContentListPage::addCard(const ModInfo &info)
{
    if (m_viewMode == ContentViewSwitch::List)
        m_cardWidgets.append(createListCard(info));
    else
        m_cardWidgets.append(createMasonryCard(info));
}

/* 视图切换：按内容类型持久化偏好并重建卡片 */
void ContentListPage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted(viewPrefKey(), mode);
    rebuildCards();
}

void ContentListPage::rebuildCards()
{
    const QList<ModInfo> infos = m_modList.mods;
    clearCards();

    if (m_cardGridLayout) { delete m_cardGridLayout; m_cardGridLayout = nullptr; }
    if (m_cardFlowLayout) { delete m_cardFlowLayout; m_cardFlowLayout = nullptr; }
    if (m_viewMode == ContentViewSwitch::List)
    {
        m_cardGridLayout = new QGridLayout(m_cardContainer);
        m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
        m_cardGridLayout->setSpacing(8);
    }
    else
    {
        m_cardFlowLayout = new MasonryLayout(m_cardContainer, 0, 10, 10);
        m_cardFlowLayout->setContentsMargins(14, 0, 14, 0);
    }

    for (const ModInfo &info : infos)
        addCard(info);
    placeCards();
    applyFilter();
}

void ContentListPage::onToggleMod(const ModInfo &info)
{
    QString oldPath = info.filePath;
    QString newPath;
    if (info.enabled)
    {
        // Disable: rename to add .disabled extension
        newPath = oldPath + ".disabled";
    }
    else
    {
        // Enable: rename to remove .disabled extension
        if (oldPath.endsWith(".disabled"))
        {
            newPath = oldPath.left(oldPath.length() - 9);
        }
        else
        {
            return;
        }
    }

    if (QFile::rename(oldPath, newPath))
    {
        refreshContentList();
    }
}

void ContentListPage::onDeleteMod(const ModInfo &info)
{
    QString displayName = info.name.isEmpty() ? info.fileName : info.name;
    QString contentTypeName = m_config.displayName;
    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("删除%1").arg(contentTypeName),
        tr("确定要删除%1 \"%2\" 吗？此操作不可撤销。").arg(contentTypeName).arg(displayName),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply == AppMessageBox::Yes)
    {
        if (m_contentType == ContentType::World)
        {
            // Remove world directory recursively
            QDir worldDir(info.filePath);
            worldDir.removeRecursively();
            // Also remove the .disabled version if it exists
            QString disabledPath = info.filePath + ".disabled";
            QDir disabledDir(disabledPath);
            if (disabledDir.exists())
            {
                disabledDir.removeRecursively();
            }
        }
        else
        {
            QFile::remove(info.filePath);
            // Also remove the .disabled version if it exists
            QString disabledPath = info.filePath + ".disabled";
            if (QFile::exists(disabledPath))
            {
                QFile::remove(disabledPath);
            }
        }
        refreshContentList();
    }
}

void ContentListPage::onContentContextMenu(const QPoint &pos)
{
    QWidget *card = qobject_cast<QWidget *>(sender());
    if (!card)
    {
        return;
    }

    QString filePath = card->property("modFilePath").toString();
    QString fileName = card->property("modFileName").toString();
    bool enabled = card->property("modEnabled").toBool();

    if (filePath.isEmpty())
    {
        return;
    }

    QString contentTypeName = m_config.displayName;

    QMenu menu(this);

    QAction *toggleAction = menu.addAction(enabled ? tr("禁用%1").arg(contentTypeName) : tr("启用%1").arg(contentTypeName));
    menu.addSeparator();
    QAction *openFolderAction = menu.addAction(tr("打开文件位置"));
    QAction *copyPathAction = menu.addAction(tr("复制文件路径"));
    appendCategoryMenu(&menu, fileName, filePath);
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("删除%1").arg(contentTypeName));

    QAction *chosen = menu.exec(card->mapToGlobal(pos));

    if (chosen == toggleAction)
    {
        ModInfo info;
        info.filePath = filePath;
        info.enabled = enabled;
        onToggleMod(info);
    }
    else if (chosen == openFolderAction)
    {
        QFileInfo fi(filePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }
    else if (chosen == copyPathAction)
    {
        QApplication::clipboard()->setText(filePath);
    }
    else if (chosen == deleteAction)
    {
        QString displayName = fileName.isEmpty() ? QFileInfo(filePath).fileName() : fileName;
        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除%1").arg(contentTypeName),
            tr("确定要删除%1 \"%2\" 吗？此操作不可撤销。").arg(contentTypeName).arg(displayName),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);

        if (reply == AppMessageBox::Yes)
        {
            if (m_contentType == ContentType::World)
            {
                QDir worldDir(filePath);
                worldDir.removeRecursively();
                QString disabledPath = filePath + ".disabled";
                QDir disabledDir(disabledPath);
                if (disabledDir.exists())
                {
                    disabledDir.removeRecursively();
                }
            }
            else
            {
                QFile::remove(filePath);
                QString disabledPath = filePath + ".disabled";
                if (QFile::exists(disabledPath))
                {
                    QFile::remove(disabledPath);
                }
            }
            refreshContentList();
        }
    }
}

void ContentListPage::clearCards()
{
    for (QWidget *w : m_cardWidgets)
    {
        if (m_cardGridLayout)
            m_cardGridLayout->removeWidget(w);
        if (m_cardFlowLayout)
            m_cardFlowLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
}

void ContentListPage::placeCards()
{
    if (m_viewMode == ContentViewSwitch::List && m_cardGridLayout)
    {
        // Remove all layout items first
        while (QLayoutItem *item = m_cardGridLayout->takeAt(0))
        {
            delete item;
        }

        int row = 0;
        for (QWidget *card : m_cardWidgets)
        {
            m_cardGridLayout->addWidget(card, row, 0);
            row++;
        }

        if (m_cardWidgets.isEmpty())
        {
            QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            m_cardGridLayout->addItem(spacer, 0, 0);
        }
        return;
    }

    if (m_cardFlowLayout)
    {
        while (QLayoutItem *item = m_cardFlowLayout->takeAt(0))
        {
            delete item;
        }
        for (QWidget *card : m_cardWidgets)
        {
            m_cardFlowLayout->addWidget(card);
        }
        if (m_cardWidgets.isEmpty())
        {
            QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            m_cardFlowLayout->addItem(spacer);
        }
    }
}

void ContentListPage::showLoading(bool show)
{
    if (show) {
        QString displayName = m_config.displayName;
        m_loadingOverlay->showOverlay(tr("正在扫描%1...").arg(displayName));
    } else {
        m_loadingOverlay->hideOverlay();
    }
}

void ContentListPage::showEmptyHint(bool show)
{
    m_emptyLabel->setVisible(show);
}

void ContentListPage::applyFilter()
{
    int visibleCount = 0;

    for (int i = 0; i < m_cardWidgets.size() && i < m_modList.mods.size(); ++i)
    {
        const ModInfo &info = m_modList.mods.at(i);
        bool visible = false;

        switch (m_currentFilter)
        {
        case FilterAll:
            visible = true;
            break;
        case FilterEnabled:
            visible = info.enabled;
            break;
        case FilterDisabled:
            visible = !info.enabled;
            break;
        }

        // 分类筛选
        if (visible && !m_currentCategoryId.isEmpty())
        {
            const QString contentCategory = m_categoryManager->categoryOf(info.fileName);
            if (m_currentCategoryId == QStringLiteral("__uncategorized__"))
                visible = contentCategory.isEmpty();
            else
                visible = (contentCategory == m_currentCategoryId);
        }

        m_cardWidgets[i]->setVisible(visible);

        if (visible)
        {
            visibleCount++;
        }
    }

    QString displayName = m_config.displayName;

    // 分类筛选生效时显示筛选后的数量
    if (!m_currentCategoryId.isEmpty())
    {
        m_statsLabel->setText(tr("显示 %1 个%2").arg(visibleCount).arg(displayName));
        return;
    }

    // Update stats label to reflect filter state
    if (m_currentFilter == FilterAll)
    {
        m_statsLabel->setText(tr("共 %1 个%2 (%3 启用, %4 禁用)")
            .arg(m_modList.totalCount)
            .arg(displayName)
            .arg(m_modList.enabledCount)
            .arg(m_modList.disabledCount));
    }
    else if (m_currentFilter == FilterEnabled)
    {
        m_statsLabel->setText(tr("已启用: %1 个%2").arg(m_modList.enabledCount).arg(displayName));
    }
    else
    {
        m_statsLabel->setText(tr("已禁用: %1 个%2").arg(m_modList.disabledCount).arg(displayName));
    }
}

bool ContentListPage::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            for (int i = 0; i < m_cardWidgets.size(); ++i)
            {
                if (m_cardWidgets[i] == obj && i < m_modList.mods.size())
                {
                    emit contentClicked(m_modList.mods.at(i));
                    break;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ============================
// 本地资源分类筛选
// ============================

void ContentListPage::reloadCategoryCombo()
{
    if (!m_categoryCombo)
    {
        return;
    }

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(tr("全部"), QString());
    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        m_categoryCombo->addItem(c.name, c.id);
    }
    m_categoryCombo->addItem(tr("未分类"), QStringLiteral("__uncategorized__"));

    // 尝试恢复之前选中的分类（可能已被删除，回退到“全部”）
    int restore = 0;
    if (!m_currentCategoryId.isEmpty())
    {
        for (int i = 0; i < m_categoryCombo->count(); ++i)
        {
            if (m_categoryCombo->itemData(i).toString() == m_currentCategoryId)
            {
                restore = i;
                break;
            }
        }
    }
    m_categoryCombo->setCurrentIndex(restore);
    m_currentCategoryId = m_categoryCombo->itemData(restore).toString();
    m_categoryCombo->blockSignals(false);
}

void ContentListPage::onCategoryFilterChanged(int index)
{
    if (!m_categoryCombo)
    {
        return;
    }
    m_currentCategoryId = m_categoryCombo->itemData(index).toString();
    applyFilter();
}

void ContentListPage::onCategoryManageClicked()
{
    LocalCategoryDialog dialog(m_categoryManager, this);
    dialog.exec();
}

void ContentListPage::onCategoriesChanged()
{
    reloadCategoryCombo();
    refreshCardCategoryChips();
    applyFilter();
}

void ContentListPage::onAssignmentsChanged()
{
    refreshCardCategoryChips();
    applyFilter();
}

void ContentListPage::refreshCardCategoryChips()
{
    for (int i = 0; i < m_cardWidgets.size() && i < m_modList.mods.size(); ++i)
    {
        const QString fileName = m_modList.mods.at(i).fileName;
        const QString catName = m_categoryManager->categoryNameOf(fileName);
        const QList<QLabel *> labels = m_cardWidgets.at(i)->findChildren<QLabel *>();
        for (QLabel *chip : labels)
        {
            if (!chip->property("categoryChip").toBool())
            {
                continue;
            }
            if (catName.isEmpty())
            {
                chip->clear();
                chip->hide();
            }
            else
            {
                chip->setText(catName);
                chip->show();
            }
        }
    }
}

void ContentListPage::appendCategoryMenu(QMenu *menu, const QString &fileName, const QString &filePath)
{
    // ── 设置分类子菜单 ──
    QMenu *categoryMenu = menu->addMenu(tr("设置分类"));
    const QString currentCat = m_categoryManager->categoryOf(fileName);
    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        QAction *action = categoryMenu->addAction(c.name);
        action->setCheckable(true);
        action->setChecked(currentCat == c.id);
        connect(action, &QAction::triggered, this,
            [this, filePath, categoryId = c.id]() {
                m_categoryManager->assignCategory(filePath, categoryId);
            });
    }
    if (cats.isEmpty())
    {
        QAction *emptyAction = categoryMenu->addAction(tr("暂无分类"));
        emptyAction->setEnabled(false);
    }
    QAction *clearAction = categoryMenu->addAction(tr("清除分类"));
    clearAction->setEnabled(!currentCat.isEmpty());
    connect(clearAction, &QAction::triggered, this,
        [this, filePath]() {
            m_categoryManager->assignCategory(filePath, QString());
        });
    categoryMenu->addSeparator();
    QAction *newCatAction = categoryMenu->addAction(tr("新建分类…"));
    connect(newCatAction, &QAction::triggered, this,
        [this, filePath]() {
            FavoriteFolderDialog dialog(FavoriteFolderDialog::CreateMode, this);
            dialog.setWindowTitle(tr("新建分类"));
            dialog.setPresets(LocalCategoryManager::suggestedNames(m_categoryManager->resourceType()));
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QString name = dialog.folderName();
            if (name.isEmpty())
                return;
            const QString newId = m_categoryManager->addCategory(name);
            m_categoryManager->assignCategory(filePath, newId);
        });
}