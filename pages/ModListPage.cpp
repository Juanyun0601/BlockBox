#include "ModListPage.h"
#include "components/BlurLoadingOverlay.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include "components/AppMessageBox.h"
#include <QPainter>
#include <QScrollBar>
#include <QSpacerItem>
#include <QUrl>

ModListPage::ModListPage(QWidget *parent)
    : QWidget(parent)
    , m_instanceCombo(nullptr)
    , m_filterCombo(nullptr)
    , m_refreshBtn(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_loadingOverlay(nullptr)
    , m_emptyLabel(nullptr)
    , m_scanner(nullptr)
    , m_currentFilter(FilterAll)
    , m_isLoading(false)
{
    m_scanner = new ModScanner(this);
    initUI();
    setupConnections();
    loadInstances();
}

ModListPage::~ModListPage()
{
}

void ModListPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // Top bar
    QHBoxLayout *topBar = new QHBoxLayout();
    QLabel *instLabel = new QLabel(tr("实例:"));
    m_instanceCombo = new QComboBox();
    m_instanceCombo->setFixedHeight(32);
    m_instanceCombo->setMinimumWidth(200);
    QLabel *filterLabel = new QLabel(tr("筛选:"));
    m_filterCombo = new QComboBox();
    m_filterCombo->setFixedHeight(32);
    m_filterCombo->addItem(tr("全部"), FilterAll);
    m_filterCombo->addItem(tr("已启用"), FilterEnabled);
    m_filterCombo->addItem(tr("已禁用"), FilterDisabled);
    m_refreshBtn = new QPushButton(tr("刷新"));
    m_refreshBtn->setFixedHeight(32);
    m_statsLabel = new QLabel();
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
    m_emptyLabel = new QLabel(tr("暂无模组"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    mainLayout->addWidget(m_emptyLabel);
}

void ModListPage::setupConnections()
{
    connect(m_instanceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ModListPage::onInstanceChanged);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ModListPage::onFilterChanged);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ModListPage::onRefreshClicked);
    connect(m_scanner, &ModScanner::scanCompleted, this, &ModListPage::onScanCompleted);
    connect(m_scanner, &ModScanner::scanFailed, this, &ModListPage::onScanFailed);
}

void ModListPage::loadInstances()
{
    m_instanceCombo->clear();
    m_instanceCombo->addItem(tr("请选择实例"), "");
    QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    for (const InstanceFolderInfo &folder : folders)
    {
        m_instanceCombo->addItem(folder.name, folder.path);
    }
}

void ModListPage::onInstanceChanged(int index)
{
    Q_UNUSED(index);
    QString instancePath = m_instanceCombo->currentData().toString();
    if (!instancePath.isEmpty())
    {
        refreshModList();
    }
}

void ModListPage::onRefreshClicked()
{
    refreshModList();
}

void ModListPage::onFilterChanged(int index)
{
    Q_UNUSED(index);
    m_currentFilter = m_filterCombo->currentData().toInt();
    applyFilter();
}

void ModListPage::refreshModList()
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
    m_scanner->scanMods(instancePath);
}

void ModListPage::onScanCompleted(const LocalModList &result)
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

        // 存储 ModInfo 并设置右键菜单
        card->setProperty("modFilePath", info.filePath);
        card->setProperty("modFileName", info.fileName);
        card->setProperty("modEnabled", info.enabled);
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QWidget::customContextMenuRequested, this, &ModListPage::onModContextMenu);

        m_cardWidgets.append(card);
    }

    placeCards();
    applyFilter();

    m_statsLabel->setText(tr("共 %1 个模组 (%2 启用, %3 禁用)")
        .arg(result.totalCount)
        .arg(result.enabledCount)
        .arg(result.disabledCount));

    if (result.totalCount == 0)
    {
        showEmptyHint(true);
    }
}

void ModListPage::onScanFailed(const QString &error)
{
    m_isLoading = false;
    showLoading(false);
    m_loadingOverlay->showError(tr("扫描失败: %1").arg(error));
}

void ModListPage::onToggleMod(const ModInfo &info)
{
    QString oldPath = info.filePath;
    QString newPath;
    if (info.enabled)
    {
        // Disable: rename .jar to .jar.disabled
        newPath = oldPath + ".disabled";
    }
    else
    {
        // Enable: rename .jar.disabled to .jar
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
        refreshModList();
    }
}

void ModListPage::onDeleteMod(const ModInfo &info)
{
    QString displayName = info.name.isEmpty() ? info.fileName : info.name;
    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("删除模组"),
        tr("确定要删除模组 \"%1\" 吗？此操作不可撤销。").arg(displayName),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply == AppMessageBox::Yes)
    {
        QFile::remove(info.filePath);
        // Also remove the .disabled version if it exists
        QString disabledPath = info.filePath + ".disabled";
        if (QFile::exists(disabledPath))
        {
            QFile::remove(disabledPath);
        }
        refreshModList();
    }
}

void ModListPage::onModContextMenu(const QPoint &pos)
{
    QWidget *card = qobject_cast<QWidget*>(sender());
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

    QMenu menu(this);
    menu.setStyleSheet(card->styleSheet());

    QAction *toggleAction = menu.addAction(enabled ? tr("禁用模组") : tr("启用模组"));
    menu.addSeparator();
    QAction *openFolderAction = menu.addAction(tr("打开模组文件夹"));
    QAction *copyPathAction = menu.addAction(tr("复制文件路径"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("删除模组"));
    deleteAction->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 16));

    QAction *chosen = menu.exec(card->mapToGlobal(pos));

    if (chosen == toggleAction)
    {
        // 构建一个简易 ModInfo 用于切换
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
            tr("删除模组"),
            tr("确定要删除模组 \"%1\" 吗？此操作不可撤销。").arg(displayName),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);

        if (reply == AppMessageBox::Yes)
        {
            QFile::remove(filePath);
            QString disabledPath = filePath + ".disabled";
            if (QFile::exists(disabledPath))
            {
                QFile::remove(disabledPath);
            }
            refreshModList();
        }
    }
}

void ModListPage::clearCards()
{
    for (QWidget *w : m_cardWidgets)
    {
        m_cardGridLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
}

void ModListPage::placeCards()
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

void ModListPage::showLoading(bool show)
{
    if (show) {
        m_loadingOverlay->showOverlay(tr("正在扫描模组..."));
    } else {
        m_loadingOverlay->hideOverlay();
    }
}

void ModListPage::showEmptyHint(bool show)
{
    m_emptyLabel->setVisible(show);
}

void ModListPage::applyFilter()
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

    // Update stats label to reflect filter state
    if (m_currentFilter == FilterAll)
    {
        m_statsLabel->setText(tr("共 %1 个模组 (%2 启用, %3 禁用)")
            .arg(m_modList.totalCount)
            .arg(m_modList.enabledCount)
            .arg(m_modList.disabledCount));
    }
    else if (m_currentFilter == FilterEnabled)
    {
        m_statsLabel->setText(tr("已启用: %1 个模组").arg(m_modList.enabledCount));
    }
    else
    {
        m_statsLabel->setText(tr("已禁用: %1 个模组").arg(m_modList.disabledCount));
    }
}