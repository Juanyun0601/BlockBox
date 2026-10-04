#include "ContentListPage.h"
#include "components/BlurLoadingOverlay.h"
#include "utils/IconHelper.h"
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
    , m_refreshBtn(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_loadingOverlay(nullptr)
    , m_emptyLabel(nullptr)
    , m_currentFilter(FilterAll)
    , m_isLoading(false)
{
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
    m_refreshBtn = new QPushButton(tr("刷新"));
    m_refreshBtn->setObjectName("bottomActionBtn");
    m_refreshBtn->setFixedHeight(32);
    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("statusLabel");
    topBar->addWidget(instLabel);
    topBar->addWidget(m_instanceCombo);
    topBar->addWidget(filterLabel);
    topBar->addWidget(m_filterCombo);
    topBar->addWidget(m_refreshBtn);
    topBar->addWidget(m_statsLabel);
    topBar->addStretch();
    mainLayout->addLayout(topBar);

    // Scroll area
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
    m_cardGridLayout = new QGridLayout(m_cardContainer);
    m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
    m_cardGridLayout->setSpacing(8);
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
    connect(m_refreshBtn, &QPushButton::clicked, this, &ContentListPage::onRefreshClicked);
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

        m_cardWidgets.append(card);
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
        m_cardGridLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
}

void ContentListPage::placeCards()
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
    int enabledCount = 0;
    int disabledCount = 0;

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

        m_cardWidgets[i]->setVisible(visible);

        if (info.enabled)
        {
            enabledCount++;
        }
        else
        {
            disabledCount++;
        }
    }

    QString displayName = m_config.displayName;

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