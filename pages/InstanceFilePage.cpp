/**
 * @file   InstanceFilePage.cpp
 * @brief  实例管理 - 通用文件浏览页面实现（资源包/光影包/存档/截图）
 * @author BlockBox Team
 * @date   2026-06-27
 */
#include "InstanceFilePage.h"
#include "ResourcesPage.h"
#include "components/BlurLoadingOverlay.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/mod/ModrinthAPI.h"
#include "utils/content/ContentDownloader.h"
#include "utils/DownloadTaskManager.h"

#include <QCryptographicHash>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QMimeData>
#include "components/AppMessageBox.h"
#include <QMouseEvent>
#include <QPixmapCache>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSpacerItem>
#include <QTimer>
#include <QUrl>

// 前向声明
static bool copyDir(const QString &srcPath, const QString &dstPath);

InstanceFilePage::InstanceFilePage(FileType type, QWidget *parent)
    : QWidget(parent)
    , m_fileType(type)
    , m_searchEdit(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_loadingOverlay(nullptr)
    , m_emptyLabel(nullptr)
    , m_downloadSearchBtn(nullptr)
    , m_stackedWidget(nullptr)
    , m_normalPage(nullptr)
    , m_dragPage(nullptr)
    , m_dragModeBtn(nullptr)
    , m_dragHintLabel(nullptr)
    , m_dragStatusLabel(nullptr)
    , m_isDragMode(false)
    , m_bottomBar(nullptr)
    , m_selectAllBtn(nullptr)
    , m_checkUpdateBtn(nullptr)
    , m_copyBtn(nullptr)
    , m_openFolderBtn(nullptr)
    , m_detailBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_pasteBtn(nullptr)
    , m_updateBtn(nullptr)
    , m_isLoading(false)
    , m_allSelected(false)
{
    m_updateCheckApi = new ModrinthAPI(this);
    initUI();
    setupConnections();
}

InstanceFilePage::~InstanceFilePage()
{
}

QString InstanceFilePage::subDirName() const
{
    switch (m_fileType)
    {
    case ResourcePack: return QStringLiteral("resourcepacks");
    case ShaderPack:   return QStringLiteral("shaderpacks");
    case Save:         return QStringLiteral("saves");
    case Screenshot:   return QStringLiteral("screenshots");
    }
    return QString();
}

QString InstanceFilePage::typeDisplayName() const
{
    switch (m_fileType)
    {
    case ResourcePack: return tr("资源包");
    case ShaderPack:   return tr("光影包");
    case Save:         return tr("存档");
    case Screenshot:   return tr("截图");
    }
    return QString();
}

void InstanceFilePage::setInstancePath(const QString &path)
{
    if (m_instancePath != path)
    {
        m_instancePath = path;
        if (!path.isEmpty())
        {
            refreshFileList();
        }
    }
}

void InstanceFilePage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // 顶部搜索/统计条：微透明玻璃态卡片
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    // 顶部栏：搜索框 + 拖入模式按钮
    QHBoxLayout *topBarLayout = new QHBoxLayout();
    topBarLayout->setSpacing(8);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索%1名称...").arg(typeDisplayName()));
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);
    topBarLayout->addWidget(m_searchEdit, 1);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    m_dragModeBtn = new QPushButton(tr("拖入此处"));
    m_dragModeBtn->setObjectName("dragModeBtn");
    m_dragModeBtn->setFixedHeight(36);
    m_dragModeBtn->setCursor(Qt::PointingHandCursor);
    m_dragModeBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/nav_import.svg", themeColor, 18));
    m_dragModeBtn->setIconSize(QSize(18, 18));
    topBarLayout->addWidget(m_dragModeBtn);

    cardLayout->addLayout(topBarLayout);

    // 统计行
    QHBoxLayout *statsBar = new QHBoxLayout();
    statsBar->setSpacing(8);
    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("statusLabel");
    statsBar->addWidget(m_statsLabel);
    statsBar->addStretch();
    cardLayout->addLayout(statsBar);
    mainLayout->addWidget(filterCard);

    // 堆叠窗口：普通模式 / 拖入模式
    m_stackedWidget = new QStackedWidget();
    mainLayout->addWidget(m_stackedWidget, 1);

    // ========== 普通模式页面 ==========
    m_normalPage = new QWidget();
    QVBoxLayout *normalLayout = new QVBoxLayout(m_normalPage);
    normalLayout->setContentsMargins(0, 0, 0, 0);
    normalLayout->setSpacing(10);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
    m_cardGridLayout = new QGridLayout(m_cardContainer);
    m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
    m_cardGridLayout->setSpacing(8);
    m_scrollArea->setWidget(m_cardContainer);
    normalLayout->addWidget(m_scrollArea, 1);

    // 空提示放在列表区域内（无资源时居中显示在卡片本该出现的位置）
    m_emptyLabel = new QLabel(tr("暂无%1").arg(typeDisplayName()));
    m_emptyLabel->setObjectName("modPlaceholderLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    normalLayout->addWidget(m_emptyLabel, 1);

    m_stackedWidget->addWidget(m_normalPage);

    // ========== 拖入模式页面 ==========
    initDragModeUI();

    // 底部操作栏
    m_bottomBar = new QWidget();
    m_bottomBar->setObjectName("modBottomBar");
    m_bottomBar->setFixedHeight(48);
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(14, 6, 14, 6);
    bottomLayout->setSpacing(10);

    m_checkUpdateBtn = new QPushButton(tr("检查更新"));
    m_checkUpdateBtn->setObjectName("bottomActionBtn");
    m_checkUpdateBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/update.svg", themeColor, 18));
    m_checkUpdateBtn->setIconSize(QSize(18, 18));
    m_checkUpdateBtn->hide();

    m_selectAllBtn = new QPushButton(tr("全选"));
    m_selectAllBtn->setObjectName("bottomActionBtn");

    m_copyBtn = new QPushButton(tr("复制"));
    m_copyBtn->setObjectName("bottomActionBtn");
    m_copyBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/copy.svg", themeColor, 18));
    m_copyBtn->setIconSize(QSize(18, 18));
    m_copyBtn->hide();

    m_openFolderBtn = new QPushButton(tr("打开文件夹"));
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/folder.svg", themeColor, 18));
    m_openFolderBtn->setIconSize(QSize(18, 18));

    m_detailBtn = new QPushButton(tr("详情"));
    m_detailBtn->setObjectName("bottomActionBtn");
    m_detailBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/list.svg", themeColor, 18));
    m_detailBtn->setIconSize(QSize(18, 18));
    m_detailBtn->hide();

    m_deleteBtn = new QPushButton(tr("删除"));
    m_deleteBtn->setObjectName("bottomActionBtn");
    m_deleteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    m_deleteBtn->setIconSize(QSize(18, 18));
    m_deleteBtn->hide();

    m_pasteBtn = new QPushButton(tr("粘贴%1").arg(typeDisplayName()));
    m_pasteBtn->setObjectName("bottomActionBtn");
    m_pasteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/nav_import.svg", themeColor, 18));
    m_pasteBtn->setIconSize(QSize(18, 18));

    m_updateBtn = new QPushButton(tr("更新"));
    m_updateBtn->setObjectName("bottomActionBtn");
    m_updateBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/update.svg", themeColor, 18));
    m_updateBtn->setIconSize(QSize(18, 18));
    m_updateBtn->hide();

    bottomLayout->addWidget(m_checkUpdateBtn);
    bottomLayout->addWidget(m_selectAllBtn);
    bottomLayout->addWidget(m_copyBtn);
    bottomLayout->addWidget(m_openFolderBtn);
    bottomLayout->addWidget(m_detailBtn);
    bottomLayout->addWidget(m_deleteBtn);
    bottomLayout->addWidget(m_pasteBtn);
    bottomLayout->addWidget(m_updateBtn);
    bottomLayout->addStretch();
    mainLayout->addWidget(m_bottomBar);

    // 加载遮罩
    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    // 搜索无结果时前往下载页按钮
    m_downloadSearchBtn = new QPushButton(tr("前往下载%1页搜索").arg(typeDisplayName()));
    m_downloadSearchBtn->setObjectName("downloadSearchBtn");
    m_downloadSearchBtn->setFixedHeight(36);
    m_downloadSearchBtn->hide();
    mainLayout->addWidget(m_downloadSearchBtn);
}

void InstanceFilePage::setupConnections()
{
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &InstanceFilePage::onSearchTextChanged);
    connect(m_selectAllBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onSelectAllClicked);
    connect(m_checkUpdateBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onCheckUpdatesClicked);
    connect(m_updateBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onUpdateSelectedClicked);
    connect(m_openFolderBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onOpenFolderClicked);
    connect(m_pasteBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onPasteFileClicked);
    connect(m_copyBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onCopyFileClicked);
    connect(m_detailBtn, &QPushButton::clicked, this, [this]() {
        if (!m_selectedIndices.isEmpty()) {
            int idx = *m_selectedIndices.begin();
            if (idx >= 0 && idx < m_fileList.size()) {
                const FileEntry &entry = m_fileList.at(idx);
                QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
                    .arg(entry.fileName)
                    .arg(entry.filePath)
                    .arg(formatFileSize(entry.fileSize))
                    .arg(entry.lastModified.toString("yyyy-MM-dd hh:mm:ss"));
                AppMessageBox::information(this, tr("文件详情"), info);
            }
        }
    });
    connect(m_deleteBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onDeleteSelectedClicked);
    connect(m_downloadSearchBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onDownloadSearchClicked);
    connect(m_updateCheckApi, &ModrinthAPI::hashesMatched,
            this, &InstanceFilePage::onHashesMatched);
    connect(m_updateCheckApi, &ModrinthAPI::hashesMatchFailed,
            this, &InstanceFilePage::onUpdateCheckBatchFailed);
    connect(m_updateCheckApi, &ModrinthAPI::updatesChecked,
            this, &InstanceFilePage::onUpdatesChecked);
    connect(m_updateCheckApi, &ModrinthAPI::updatesCheckFailed,
            this, &InstanceFilePage::onUpdateCheckBatchFailed);
    connect(m_dragModeBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onToggleDragMode);
}

void InstanceFilePage::initDragModeUI()
{
    m_dragPage = new QWidget();
    QVBoxLayout *dragLayout = new QVBoxLayout(m_dragPage);
    dragLayout->setContentsMargins(20, 20, 20, 20);
    dragLayout->setSpacing(16);

    // 拖入区域容器
    QWidget *dropZone = new QWidget();
    dropZone->setObjectName("dropZone");
    dropZone->setAcceptDrops(true);
    dropZone->setMinimumHeight(200);
    dropZone->installEventFilter(this);

    QVBoxLayout *dropLayout = new QVBoxLayout(dropZone);
    dropLayout->setAlignment(Qt::AlignCenter);
    dropLayout->setSpacing(12);

    // 拖入图标
    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("dropZoneIcon");
    QColor tc(ThemeManager::instance()->currentThemeColor());
    iconLabel->setPixmap(IconHelper::loadColoredIcon(
        ":/Images/Icons/nav_import.svg", tc, 64).pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignCenter);
    dropLayout->addWidget(iconLabel);

    // 拖入提示文字
    m_dragHintLabel = new QLabel(tr("将%1文件拖放到此处").arg(typeDisplayName()));
    m_dragHintLabel->setObjectName("dropZoneHint");
    m_dragHintLabel->setAlignment(Qt::AlignCenter);
    QFont hintFont = m_dragHintLabel->font();
    hintFont.setPointSize(12);
    hintFont.setBold(true);
    m_dragHintLabel->setFont(hintFont);
    dropLayout->addWidget(m_dragHintLabel);

    // 补充说明
    QString suffixHint;
    switch (m_fileType)
    {
    case ResourcePack:
    case ShaderPack:
        suffixHint = tr("支持 .zip 格式");
        break;
    case Save:
        suffixHint = tr("支持文件夹格式");
        break;
    case Screenshot:
        suffixHint = tr("支持 .png / .jpg / .webp 格式");
        break;
    }
    QLabel *subHintLabel = new QLabel(suffixHint);
    subHintLabel->setObjectName("dropZoneSubHint");
    subHintLabel->setAlignment(Qt::AlignCenter);
    dropLayout->addWidget(subHintLabel);

    dragLayout->addWidget(dropZone, 1);

    // 状态标签（显示复制进度/结果）
    m_dragStatusLabel = new QLabel();
    m_dragStatusLabel->setObjectName("dropStatusLabel");
    m_dragStatusLabel->setAlignment(Qt::AlignCenter);
    m_dragStatusLabel->hide();
    dragLayout->addWidget(m_dragStatusLabel);

    m_stackedWidget->addWidget(m_dragPage);
}

void InstanceFilePage::refreshFileList()
{
    if (m_instancePath.isEmpty())
    {
        return;
    }
    m_isLoading = true;
    showLoading(true);
    showEmptyHint(false);
    clearCards();
    scanFiles();
}

void InstanceFilePage::scanFiles()
{
    m_fileList.clear();

    QString dirPath = m_instancePath + "/" + subDirName();
    QDir dir(dirPath);
    if (!dir.exists())
    {
        m_isLoading = false;
        showLoading(false);
        showEmptyHint(true);
        return;
    }

    QFileInfoList entries;
    switch (m_fileType)
    {
    case Save:
        // 存档是目录
        entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        break;
    case ResourcePack:
    case ShaderPack:
        // 资源包/光影包是 .zip 文件
        entries = dir.entryInfoList(QStringList() << "*.zip", QDir::Files, QDir::Name);
        break;
    case Screenshot:
        // 截图是图片文件
        entries = dir.entryInfoList(
            QStringList() << "*.png" << "*.jpg" << "*.jpeg" << "*.webp",
            QDir::Files, QDir::Name);
        break;
    }

    for (const QFileInfo &fi : entries)
    {
        FileEntry entry;
        entry.filePath = fi.absoluteFilePath();
        entry.fileName = fi.fileName();
        entry.fileSize = fi.isDir() ? 0 : fi.size();
        entry.lastModified = fi.lastModified();
        // 从缓存恢复 SHA-1（避免每次刷新都重新计算哈希）
        if (m_sha1Cache.contains(entry.filePath))
            entry.sha1Hash = m_sha1Cache.value(entry.filePath);
        m_fileList.append(entry);
    }

    m_isLoading = false;
    showLoading(false);

    // 重建 SHA-1 → 索引映射（卡片创建前先建好，供更新标记使用）
    m_sha1ToIndex.clear();
    for (int i = 0; i < m_fileList.size(); ++i)
    {
        const QString &h = m_fileList.at(i).sha1Hash;
        if (!h.isEmpty())
            m_sha1ToIndex.insert(h, i);
    }

    m_statsLabel->setText(tr("共 %1 个%2").arg(m_fileList.size()).arg(typeDisplayName()));

    if (m_fileList.isEmpty())
    {
        showEmptyHint(true);
        return;
    }

    // 分批创建卡片，避免资源过多时一次性创建导致闪退
    ++m_batchGeneration;
    m_batchIndex = 0;
    int gen = m_batchGeneration;
    QTimer::singleShot(0, this, [this, gen]() {
        if (gen == m_batchGeneration)
            createNextBatch();
    });
}

void InstanceFilePage::createNextBatch()
{
    static const int BATCH_SIZE = 30;
    int start = m_batchIndex;
    int end = qMin(start + BATCH_SIZE, m_fileList.size());

    for (int i = start; i < end; ++i)
    {
        const FileEntry &entry = m_fileList.at(i);
        QWidget *card = createFileCard(entry.filePath, entry.fileName,
                                       entry.fileSize, entry.lastModified);
        m_cardWidgets.append(card);
    }

    m_batchIndex = end;

    if (m_batchIndex < m_fileList.size())
    {
        // 还有剩余，继续下一批（yield to event loop）
        int gen = m_batchGeneration;
        QTimer::singleShot(0, this, [this, gen]() {
            if (gen == m_batchGeneration)
                createNextBatch();
        });
    }
    else
    {
        // 全部创建完毕，一次性排布并标记更新
        for (auto it = m_updateCache.constBegin(); it != m_updateCache.constEnd(); ++it)
        {
            if (m_sha1ToIndex.contains(it.key()))
                markCardHasUpdate(m_sha1ToIndex.value(it.key()), it.value());
        }

        placeCards();
        applyFilterAndSearch();
    }
}

QWidget *InstanceFilePage::createFileCard(const QString &filePath, const QString &fileName,
                                          qint64 fileSize, const QDateTime &lastModified)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("filePath", filePath);
    card->setProperty("fileName", fileName);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->installEventFilter(this);

    QHBoxLayout *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    // 图标 - 从本地文件提取，失败则回退到类型 SVG 图标
    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        QString cacheKey = QString("rp_icon:%1").arg(filePath);
        QPixmap icon;
        if (!QPixmapCache::find(cacheKey, &icon))
        {
            switch (m_fileType)
            {
            case ResourcePack:
            case ShaderPack:
                icon = ResourcesPage::extractResourcePackIcon(filePath);
                break;
            case Save:
                icon = ResourcesPage::extractSaveIcon(filePath);
                break;
            case Screenshot:
                icon = ResourcesPage::loadImageThumbnail(filePath);
                break;
            }
            if (!icon.isNull())
                QPixmapCache::insert(cacheKey, icon);
        }
        if (!icon.isNull())
        {
            iconLabel->setPixmap(icon.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        else
        {
            QColor tc(ThemeManager::instance()->currentThemeColor());
            QString fallbackIcon;
            switch (m_fileType)
            {
            case ResourcePack: fallbackIcon = ":/Images/Icons/nav_resourcepacks.svg"; break;
            case ShaderPack:   fallbackIcon = ":/Images/Icons/nav_shaders.svg"; break;
            case Save:         fallbackIcon = ":/Images/Icons/nav_worlds.svg"; break;
            case Screenshot:   fallbackIcon = ":/Images/Icons/screenshot.svg"; break;
            }
            iconLabel->setPixmap(IconHelper::loadColoredIcon(fallbackIcon, tc, 36).pixmap(36, 36));
        }
    }
    mainLayout->addWidget(iconLabel);

    // 信息区域
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    // 文件名
    QLabel *nameLabel = new QLabel(fileName);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(10);
    nameLabel->setFont(nameFont);
    infoLayout->addWidget(nameLabel);

    // 标签行
    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    if (fileSize > 0 || m_fileType != Save)
    {
        QString sizeText = (m_fileType == Save)
            ? tr("文件夹")
            : formatFileSize(fileSize);
        QLabel *sizeChip = new QLabel(sizeText);
        sizeChip->setObjectName("modChip");
        sizeChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(sizeChip);
    }

    if (lastModified.isValid())
    {
        QString dateText = lastModified.toString("yyyy-MM-dd");
        QLabel *dateChip = new QLabel(dateText);
        dateChip->setObjectName("modChip");
        dateChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(dateChip);
    }

    // 更新提示芯片（仅资源包/光影包显示）
    if (m_fileType == ResourcePack || m_fileType == ShaderPack)
    {
        QLabel *updateChip = new QLabel(tr("可更新"));
        updateChip->setObjectName("fileUpdateChip");
        updateChip->setProperty("cardRole", "chip");
        QColor tc(ThemeManager::instance()->currentThemeColor());
        updateChip->setStyleSheet(QString(
            "color: white; background-color: %1; border-radius: 9px;"
            "padding: 2px 8px; font-size: 11px;").arg(tc.name()));
        updateChip->hide();
        chipsLayout->addWidget(updateChip);
    }

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
    mainLayout->addLayout(infoLayout, 1);

    // 操作按钮
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

    QString capturedPath = filePath;
    QString capturedName = fileName;

    // 快捷启动按钮（仅存档类型显示）：启动游戏并直接进入该存档
    if (m_fileType == Save)
    {
        QPushButton *quickLaunchBtn = createBtn(":/Images/Icons/quick_launch.svg", tr("快捷启动"));
        connect(quickLaunchBtn, &QPushButton::clicked, this, [this, capturedName]() {
            emit quickLaunchSaveRequested(capturedName);
        });
        btnLayout->addWidget(quickLaunchBtn);

        // 存档设置按钮：请求打开存档设置页
        QPushButton *settingsBtn = createBtn(":/Images/Icons/instance_settings.svg", tr("存档设置"));
        connect(settingsBtn, &QPushButton::clicked, this, [this, capturedName]() {
            emit saveSettingsRequested(capturedName);
        });
        btnLayout->addWidget(settingsBtn);
    }

    // 光影设置按钮（仅光影包类型显示）：可视化编辑光影包选项
    if (m_fileType == ShaderPack)
    {
        QPushButton *shaderSettingsBtn = createBtn(
            ":/Images/Icons/instance_settings.svg", tr("光影设置"));
        connect(shaderSettingsBtn, &QPushButton::clicked, this, [this, capturedPath]() {
            if (m_instancePath.isEmpty())
                return;
            emit shaderSettingsRequested(capturedPath);
        });
        btnLayout->addWidget(shaderSettingsBtn);
    }

    // 详情按钮
    QPushButton *detailBtn = createBtn(":/Images/Icons/list.svg", tr("查看详情"));
    connect(detailBtn, &QPushButton::clicked, this, [this, capturedPath, capturedName, fileSize, lastModified]() {
        QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
            .arg(capturedName)
            .arg(capturedPath)
            .arg(formatFileSize(fileSize))
            .arg(lastModified.toString("yyyy-MM-dd hh:mm:ss"));
        AppMessageBox::information(this, tr("文件详情"), info);
    });
    btnLayout->addWidget(detailBtn);

    // 截图查看按钮（仅截图类型显示）
    if (m_fileType == Screenshot)
    {
        QPushButton *viewBtn = createBtn(":/Images/Icons/screenshot.svg", tr("查看截图"));
        connect(viewBtn, &QPushButton::clicked, this, [capturedPath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(capturedPath));
        });
        btnLayout->addWidget(viewBtn);
    }

    // 打开文件夹按钮
    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开文件位置"));
    connect(folderBtn, &QPushButton::clicked, this, [capturedPath]() {
        QFileInfo fi(capturedPath);
        QString dirPath = fi.isDir() ? capturedPath : fi.absolutePath();
        if (!dirPath.isEmpty())
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
        }
    });
    btnLayout->addWidget(folderBtn);

    // 删除按钮
    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, this, [this, capturedPath, capturedName]() {
        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除%1").arg(typeDisplayName()),
            tr("确定要删除 \"%1\" 吗？此操作不可撤销。").arg(capturedName),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);

        if (reply == AppMessageBox::Yes)
        {
            QFileInfo fi(capturedPath);
            if (fi.isDir())
            {
                QDir(capturedPath).removeRecursively();
            }
            else
            {
                QFile::remove(capturedPath);
            }
            refreshFileList();
        }
    });
    btnLayout->addWidget(deleteBtn);

    mainLayout->addLayout(btnLayout);

    createSelectionOverlay(card);

    return card;
}

QWidget *InstanceFilePage::createSelectionOverlay(QWidget *card)
{
    QWidget *overlay = new QWidget(card);
    overlay->setObjectName("cardSelectionOverlay");
    overlay->setGeometry(card->rect());
    overlay->setStyleSheet(selectionOverlayStyle());
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->raise();
    overlay->hide();

    QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(overlay);
    effect->setOpacity(0.0);
    overlay->setGraphicsEffect(effect);

    QPropertyAnimation *anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(180);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // 淡出结束后隐藏遮罩（overlay 销毁时自动断开，避免悬空指针）
    connect(anim, &QPropertyAnimation::finished, overlay, [overlay, effect]() {
        if (effect->opacity() <= 0.01)
            overlay->hide();
    });

    m_selectionOverlays.append(overlay);
    m_selectionEffects.append(effect);
    m_selectionAnims.append(anim);
    return overlay;
}

void InstanceFilePage::animateCardHighlight(int idx, bool selected)
{
    if (idx < 0 || idx >= m_selectionOverlays.size())
        return;

    QWidget *overlay = m_selectionOverlays[idx];
    QGraphicsOpacityEffect *effect = m_selectionEffects[idx];
    QPropertyAnimation *anim = m_selectionAnims[idx];

    anim->stop();
    anim->setStartValue(effect->opacity());
    anim->setEndValue(selected ? 1.0 : 0.0);
    if (selected)
        overlay->show();
    anim->start();
}

QString InstanceFilePage::selectionOverlayStyle() const
{
    QColor c = ThemeManager::instance()->currentThemeColor();
    return QString("#cardSelectionOverlay { background-color: rgba(%1, %2, %3, 40); "
                   "border: 2px solid %4; border-radius: 6px; }")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name());
}

void InstanceFilePage::onSearchTextChanged(const QString &text)
{
    m_currentSearch = text.trimmed();
    applyFilterAndSearch();
}

void InstanceFilePage::onSelectAllClicked()
{
    m_allSelected = !m_allSelected;
    m_selectAllBtn->setText(m_allSelected ? tr("取消全选") : tr("全选"));

    m_selectedIndices.clear();

    if (m_allSelected)
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            if (m_cardWidgets[i]->isVisible())
            {
                m_selectedIndices.insert(i);
                animateCardHighlight(i, true);
            }
        }
    }
    else
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            animateCardHighlight(i, false);
        }
    }

    updateBottomBarState();
}

void InstanceFilePage::onOpenFolderClicked()
{
    if (m_instancePath.isEmpty())
        return;

    QString dirPath = m_instancePath + "/" + subDirName();
    QDir dir(dirPath);
    if (dir.exists())
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
    }
    else
    {
        AppMessageBox::information(this, tr("提示"),
            tr("%1目录不存在: %2").arg(typeDisplayName()).arg(dirPath));
    }
}

void InstanceFilePage::onPasteFileClicked()
{
    if (m_instancePath.isEmpty())
        return;

    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    if (!mimeData->hasUrls())
    {
        AppMessageBox::information(this, tr("粘贴%1").arg(typeDisplayName()),
            tr("剪贴板中没有文件，请先复制文件。"));
        return;
    }

    QString targetDir = m_instancePath + "/" + subDirName();
    QDir dir(targetDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    int copiedCount = 0;
    QStringList validSuffixes;
    switch (m_fileType)
    {
    case ResourcePack:
    case ShaderPack:
        validSuffixes = { ".zip" };
        break;
    case Screenshot:
        validSuffixes = { ".png", ".jpg", ".jpeg", ".webp" };
        break;
    case Save:
        // 存档是目录，粘贴整个文件夹
        break;
    }

    for (const QUrl &url : mimeData->urls())
    {
        QString srcPath = url.toLocalFile();
        if (srcPath.isEmpty())
            continue;

        QFileInfo fi(srcPath);

        if (m_fileType == Save)
        {
            // 存档：复制整个目录
            if (!fi.isDir())
                continue;

            QString destPath = targetDir + "/" + fi.fileName();
            if (QDir(destPath).exists())
            {
                int ret = AppMessageBox::question(this, tr("目录已存在"),
                    tr("存档 \"%1\" 已存在，是否覆盖？").arg(fi.fileName()),
                    AppMessageBox::Yes | AppMessageBox::No | AppMessageBox::Cancel);
                if (ret == AppMessageBox::Cancel)
                    break;
                if (ret == AppMessageBox::No)
                    continue;
                QDir(destPath).removeRecursively();
            }
            if (copyDir(srcPath, destPath))
            {
                copiedCount++;
            }
        }
        else
        {
            // 文件：检查后缀
            bool isValid = false;
            for (const QString &suf : validSuffixes)
            {
                if (fi.fileName().endsWith(suf, Qt::CaseInsensitive))
                {
                    isValid = true;
                    break;
                }
            }
            if (!isValid)
                continue;

            QString destPath = targetDir + "/" + fi.fileName();
            if (QFile::exists(destPath))
            {
                int ret = AppMessageBox::question(this, tr("文件已存在"),
                    tr("文件 \"%1\" 已存在，是否覆盖？").arg(fi.fileName()),
                    AppMessageBox::Yes | AppMessageBox::No | AppMessageBox::Cancel);
                if (ret == AppMessageBox::Cancel)
                    break;
                if (ret == AppMessageBox::No)
                    continue;
                QFile::remove(destPath);
            }

            if (QFile::copy(srcPath, destPath))
            {
                copiedCount++;
            }
            else
            {
                AppMessageBox::warning(this, tr("粘贴失败"),
                    tr("无法复制文件 \"%1\"").arg(fi.fileName()));
            }
        }
    }

    if (copiedCount > 0)
    {
        m_statsLabel->setText(tr("已粘贴 %1 个%2，正在刷新列表...")
            .arg(copiedCount).arg(typeDisplayName()));
        refreshFileList();
    }
}

void InstanceFilePage::onCopyFileClicked()
{
    if (m_selectedIndices.isEmpty())
        return;

    QList<QUrl> urls;
    QStringList names;
    for (int idx : m_selectedIndices)
    {
        if (idx < 0 || idx >= m_fileList.size())
            continue;

        const FileEntry &entry = m_fileList.at(idx);
        QFileInfo fi(entry.filePath);
        if (fi.exists())
        {
            urls.append(QUrl::fromLocalFile(fi.absoluteFilePath()));
            names.append(fi.fileName());
        }
    }

    if (urls.isEmpty())
    {
        AppMessageBox::warning(this, tr("复制失败"), tr("没有可复制的文件"));
        return;
    }

    QMimeData *mimeData = new QMimeData();
    mimeData->setUrls(urls);

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setMimeData(mimeData);

    m_statsLabel->setText(tr("已复制 %1 个%2").arg(urls.size()).arg(typeDisplayName()));
}

void InstanceFilePage::onDeleteSelectedClicked()
{
    if (m_selectedIndices.isEmpty())
        return;

    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("批量删除"),
        tr("确定要删除选中的 %1 个%2吗？此操作不可撤销。")
            .arg(m_selectedIndices.size()).arg(typeDisplayName()),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply != AppMessageBox::Yes)
        return;

    for (int idx : m_selectedIndices)
    {
        if (idx < 0 || idx >= m_fileList.size())
            continue;

        const FileEntry &entry = m_fileList.at(idx);
        QFileInfo fi(entry.filePath);
        if (fi.isDir())
        {
            QDir(entry.filePath).removeRecursively();
        }
        else
        {
            QFile::remove(entry.filePath);
        }
    }

    refreshFileList();
}

void InstanceFilePage::onDownloadSearchClicked()
{
    emit fileSearchRequested(m_currentSearch);
}

bool InstanceFilePage::eventFilter(QObject *watched, QEvent *event)
{
    // 处理拖入区域的拖放事件
    if (watched == m_dragPage->findChild<QWidget *>("dropZone"))
    {
        if (event->type() == QEvent::DragEnter)
        {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent *>(event);
            if (dragEvent->mimeData()->hasUrls())
            {
                dragEvent->acceptProposedAction();
                // 高亮拖入区域
                QWidget *dropZone = qobject_cast<QWidget *>(watched);
                if (dropZone)
                {
                    QColor tc(ThemeManager::instance()->currentThemeColor());
                    dropZone->setStyleSheet(QString(
                        "#dropZone { background-color: rgba(%1, %2, %3, 30); "
                        "border: 2px dashed %4; border-radius: 12px; }")
                        .arg(tc.red()).arg(tc.green()).arg(tc.blue()).arg(tc.name()));
                }
                return true;
            }
            return false;
        }
        if (event->type() == QEvent::DragLeave)
        {
            QWidget *dropZone = qobject_cast<QWidget *>(watched);
            if (dropZone)
            {
                dropZone->setStyleSheet(QString(
                    "#dropZone { background-color: transparent; "
                    "border: 2px dashed %1; border-radius: 12px; }")
                    .arg(ThemeManager::instance()->currentThemeColor()));
            }
            return true;
        }
        if (event->type() == QEvent::Drop)
        {
            QDropEvent *dropEvent = static_cast<QDropEvent *>(event);
            const QMimeData *mimeData = dropEvent->mimeData();
            if (mimeData->hasUrls())
            {
                QList<QUrl> urls = mimeData->urls();
                handleDroppedFiles(urls);
                dropEvent->acceptProposedAction();
            }
            // 恢复拖入区域样式
            QWidget *dropZone = qobject_cast<QWidget *>(watched);
            if (dropZone)
            {
                QColor tc(ThemeManager::instance()->currentThemeColor());
                dropZone->setStyleSheet(QString(
                    "#dropZone { background-color: transparent; "
                    "border: 2px dashed %1; border-radius: 12px; }")
                    .arg(tc.name()));
            }
            return true;
        }
    }

    QWidget *card = qobject_cast<QWidget *>(watched);
    if (!card)
        return QWidget::eventFilter(watched, event);

    QString filePath = card->property("filePath").toString();
    if (filePath.isEmpty())
        return QWidget::eventFilter(watched, event);

    // 找到卡片在 m_cardWidgets 中的索引
    int idx = -1;
    for (int i = 0; i < m_cardWidgets.size(); ++i)
    {
        if (m_cardWidgets[i] == card)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonDblClick)
    {
        // 双击打开文件/文件夹
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            // 按下左键开始滑动多选：抓取鼠标，拖拽经过的卡片将被快速勾选
            m_dragSelectActive = true;
            m_dragMoved = false;
            m_dragPressPos = mouseEvent->globalPos();
            m_dragAnchorIdx = idx;
            m_dragLastHoverIdx = idx;
            card->grabMouse();
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_dragSelectActive && (mouseEvent->buttons() & Qt::LeftButton))
        {
            // 超过拖拽阈值才视为滑动多选，避免与单击冲突
            if (!m_dragMoved &&
                (mouseEvent->globalPos() - m_dragPressPos).manhattanLength() >= QApplication::startDragDistance())
            {
                m_dragMoved = true;
                selectCardDuringDrag(m_dragAnchorIdx);
            }

            if (m_dragMoved)
            {
                int hoverIdx = cardIndexAtGlobal(mouseEvent->globalPos());
                if (hoverIdx >= 0 && hoverIdx != m_dragLastHoverIdx)
                {
                    if (hoverIdx > m_dragLastHoverIdx)
                    {
                        // 正向滑动：勾选滑过的可见卡片
                        for (int i = m_dragLastHoverIdx + 1; i <= hoverIdx; ++i)
                        {
                            if (m_cardWidgets[i]->isVisible())
                                selectCardDuringDrag(i);
                        }
                    }
                    else
                    {
                        // 反向滑动：取消勾选滑过的可见卡片
                        for (int i = m_dragLastHoverIdx - 1; i >= hoverIdx; --i)
                        {
                            if (m_cardWidgets[i]->isVisible())
                                deselectCardDuringDrag(i);
                        }
                    }
                    m_dragLastHoverIdx = hoverIdx;
                }
            }
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_dragSelectActive && mouseEvent->button() == Qt::LeftButton)
        {
            m_dragSelectActive = false;
            card->releaseMouse();
            if (!m_dragMoved)
            {
                // 未发生拖拽：视为单击，切换该卡片的选中状态
                toggleCardSelection(m_dragAnchorIdx);
            }
            m_dragMoved = false;
            m_dragAnchorIdx = -1;
            m_dragLastHoverIdx = -1;
            return true;
        }
    }

    if (event->type() == QEvent::Resize)
    {
        int ovIdx = m_cardWidgets.indexOf(card);
        if (ovIdx >= 0 && ovIdx < m_selectionOverlays.size())
            m_selectionOverlays[ovIdx]->setGeometry(card->rect());
    }

    if (event->type() == QEvent::ContextMenu)
    {
        QContextMenuEvent *ctxEvent = static_cast<QContextMenuEvent *>(event);
        QMenu menu(this);

        QAction *openAction = menu.addAction(tr("打开"));
        QAction *openFolderAction = menu.addAction(tr("打开文件位置"));
        QAction *copyPathAction = menu.addAction(tr("复制文件路径"));
        menu.addSeparator();
        QAction *detailAction = menu.addAction(tr("文件详情"));
        menu.addSeparator();
        QAction *deleteAction = menu.addAction(tr("删除"));

        QAction *chosen = menu.exec(ctxEvent->globalPos());

        if (chosen == openAction)
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        }
        else if (chosen == openFolderAction)
        {
            QFileInfo fi(filePath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? filePath : fi.absolutePath()));
        }
        else if (chosen == copyPathAction)
        {
            QApplication::clipboard()->setText(filePath);
        }
        else if (chosen == detailAction && idx >= 0 && idx < m_fileList.size())
        {
            const FileEntry &entry = m_fileList.at(idx);
            QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
                .arg(entry.fileName)
                .arg(entry.filePath)
                .arg(formatFileSize(entry.fileSize))
                .arg(entry.lastModified.toString("yyyy-MM-dd hh:mm:ss"));
            AppMessageBox::information(this, tr("文件详情"), info);
        }
        else if (chosen == deleteAction)
        {
            QString fileName = card->property("fileName").toString();
            AppMessageBox::StandardButton reply = AppMessageBox::question(
                this, tr("删除%1").arg(typeDisplayName()),
                tr("确定要删除 \"%1\" 吗？此操作不可撤销。").arg(fileName),
                AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);

            if (reply == AppMessageBox::Yes)
            {
                QFileInfo fi(filePath);
                if (fi.isDir())
                    QDir(filePath).removeRecursively();
                else
                    QFile::remove(filePath);
                refreshFileList();
            }
        }
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

void InstanceFilePage::clearCards()
{
    for (QWidget *w : m_cardWidgets)
    {
        m_cardGridLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
    m_selectedIndices.clear();
    m_allSelected = false;
    m_dragSelectActive = false;
    m_dragMoved = false;
    m_dragAnchorIdx = -1;
    m_dragLastHoverIdx = -1;
    // 遮罩随卡片销毁，动画由本页持有，需显式释放
    m_selectionOverlays.clear();
    m_selectionEffects.clear();
    for (QPropertyAnimation *anim : m_selectionAnims)
        anim->deleteLater();
    m_selectionAnims.clear();
    updateBottomBarState();
}

void InstanceFilePage::placeCards()
{
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

    QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    m_cardGridLayout->addItem(spacer, row, 0);
}

void InstanceFilePage::showLoading(bool show)
{
    if (show)
    {
        m_loadingOverlay->showOverlay(tr("正在扫描%1...").arg(typeDisplayName()));
    }
    else
    {
        m_loadingOverlay->hideOverlay();
    }
}

void InstanceFilePage::showEmptyHint(bool show)
{
    m_emptyLabel->setVisible(show);
    // 空态时隐藏列表滚动区，让提示文字占据列表区域
    m_scrollArea->setVisible(!show);
    if (!show)
        m_downloadSearchBtn->hide();
}

void InstanceFilePage::updateBottomBarState()
{
    bool isPackType = (m_fileType == ResourcePack || m_fileType == ShaderPack);
    m_checkUpdateBtn->setVisible(isPackType);

    bool hasSelection = !m_selectedIndices.isEmpty();
    m_copyBtn->setVisible(hasSelection);
    m_detailBtn->setVisible(hasSelection);
    m_deleteBtn->setVisible(hasSelection);
    m_pasteBtn->setVisible(!hasSelection);

    // 更新按钮：仅当选中了有更新的文件时显示
    bool hasUpdatable = false;
    if (hasSelection && isPackType)
    {
        for (int idx : m_selectedIndices)
        {
            if (idx < 0 || idx >= m_fileList.size())
                continue;
            const QString &h = m_fileList.at(idx).sha1Hash;
            if (!h.isEmpty() && m_updateCache.contains(h))
            {
                hasUpdatable = true;
                break;
            }
        }
    }
    m_updateBtn->setVisible(hasUpdatable);
    m_updateBtn->setEnabled(!m_updateInProgress);
}

void InstanceFilePage::toggleCardSelection(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    if (m_selectedIndices.contains(idx))
    {
        m_selectedIndices.remove(idx);
        animateCardHighlight(idx, false);
    }
    else
    {
        m_selectedIndices.insert(idx);
        animateCardHighlight(idx, true);
    }

    updateBottomBarState();
}

int InstanceFilePage::cardIndexAtGlobal(const QPoint &globalPos) const
{
    QWidget *w = QApplication::widgetAt(globalPos);
    while (w)
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            if (m_cardWidgets[i] == w)
                return i;
        }
        w = w->parentWidget();
    }
    return -1;
}

void InstanceFilePage::selectCardDuringDrag(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    if (m_selectedIndices.contains(idx))
        return;

    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    m_selectedIndices.insert(idx);
    animateCardHighlight(idx, true);
    updateBottomBarState();
}

void InstanceFilePage::deselectCardDuringDrag(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    if (!m_selectedIndices.contains(idx))
        return;

    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    m_selectedIndices.remove(idx);
    animateCardHighlight(idx, false);
    updateBottomBarState();
}

void InstanceFilePage::deselectAllCards()
{
    for (int idx : m_selectedIndices)
    {
        if (idx >= 0 && idx < m_cardWidgets.size())
            animateCardHighlight(idx, false);
    }
    m_selectedIndices.clear();
    m_allSelected = false;
    m_selectAllBtn->setText(tr("全选"));
    updateBottomBarState();
}

void InstanceFilePage::applyFilterAndSearch()
{
    int visibleCount = 0;

    for (int i = 0; i < m_cardWidgets.size() && i < m_fileList.size(); ++i)
    {
        bool visible = true;

        if (!m_currentSearch.isEmpty())
        {
            const FileEntry &entry = m_fileList.at(i);
            visible = entry.fileName.contains(m_currentSearch, Qt::CaseInsensitive);
        }

        m_cardWidgets[i]->setVisible(visible);

        if (visible)
        {
            visibleCount++;
        }
    }

    if (m_currentSearch.isEmpty())
    {
        m_statsLabel->setText(tr("共 %1 个%2").arg(m_fileList.size()).arg(typeDisplayName()));
    }
    else
    {
        m_statsLabel->setText(tr("显示 %1 个%2 (共 %3 个)")
            .arg(visibleCount).arg(typeDisplayName()).arg(m_fileList.size()));
    }

    // 搜索结果为空时显示提示；截图为本地生成内容，不支持在线搜索
    if (visibleCount == 0 && m_fileList.size() > 0 && m_fileType != Screenshot)
    {
        m_emptyLabel->setText(tr("没有找到匹配的%1").arg(typeDisplayName()));
        m_emptyLabel->setVisible(true);
        m_downloadSearchBtn->setVisible(true);
    }
    else
    {
        m_emptyLabel->setVisible(m_fileList.isEmpty());
        m_downloadSearchBtn->hide();
    }
    // 提示可见时列表为空，隐藏滚动区让提示占据列表区域
    m_scrollArea->setVisible(!m_emptyLabel->isVisible());
}

QString InstanceFilePage::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString InstanceFilePage::computeFileSha1(const QString &filePath)
{
    if (m_sha1Cache.contains(filePath))
        return m_sha1Cache.value(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QByteArray data = file.readAll();
    QString sha1 = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    m_sha1Cache.insert(filePath, sha1);
    return sha1;
}

void InstanceFilePage::onCheckUpdatesClicked()
{
    if (m_fileType != ResourcePack && m_fileType != ShaderPack)
        return;
    if (m_fileList.isEmpty())
        return;

    resetUpdateState();

    // 计算当前类型所有文件的 SHA-1（懒计算，缓存复用）
    showLoading(true);
    m_statsLabel->setText(tr("正在计算文件哈希..."));
    m_sha1ToIndex.clear();
    QList<QString> sha1s;
    for (int i = 0; i < m_fileList.size(); ++i)
    {
        FileEntry &entry = m_fileList[i];
        if (entry.sha1Hash.isEmpty())
            entry.sha1Hash = computeFileSha1(entry.filePath);
        if (entry.sha1Hash.isEmpty())
            continue;
        m_sha1ToIndex.insert(entry.sha1Hash, i);
        sha1s.append(entry.sha1Hash);
    }
    showLoading(false);

    sha1s.removeDuplicates();
    sha1s.sort();

    if (sha1s.isEmpty())
    {
        m_statsLabel->setText(tr("没有可检查更新的%1").arg(typeDisplayName()));
        return;
    }

    m_checkUpdateBtn->setEnabled(false);
    m_checkUpdateBtn->setText(tr("检查中..."));
    m_statsLabel->setText(tr("正在检查更新..."));

    // 第一步：批量哈希匹配，得到已安装版本（一次请求）
    m_updateCheckApi->matchHashes(sha1s);
}

void InstanceFilePage::onHashesMatched(const QMap<QString, ModVersionFile> &versions)
{
    m_installedBySha1 = versions;

    // 第二步：批量获取最新版本。资源包/光影包不按加载器与游戏版本过滤
    m_updateCheckApi->checkModUpdates(m_sha1ToIndex.keys(), QStringList(), QStringList());
}

void InstanceFilePage::onUpdatesChecked(const QMap<QString, ModVersionFile> &versions)
{
    for (auto it = versions.constBegin(); it != versions.constEnd(); ++it)
    {
        const QString &sha1 = it.key();

        // 必须能在本地已安装版本中找到，否则跳过
        if (!m_installedBySha1.contains(sha1))
            continue;

        const ModVersionFile &latest = it.value();
        const ModVersionFile installed = m_installedBySha1.value(sha1);

        // 更新判断（参考 PCL-CE/HMCL）：最新版发布日期更晚 且 文件哈希不同
        bool hasUpdate = false;
        if (latest.sha1.isEmpty() || installed.sha1.isEmpty())
        {
            hasUpdate = latest.sha1 != installed.sha1;
        }
        else if (latest.sha1 != installed.sha1
                 && installed.datePublished.isValid()
                 && latest.datePublished > installed.datePublished)
        {
            hasUpdate = true;
        }

        if (hasUpdate)
        {
            m_updateCache.insert(sha1, latest);
            if (m_sha1ToIndex.contains(sha1))
                markCardHasUpdate(m_sha1ToIndex.value(sha1), latest);
        }
    }

    m_updateCheckFound = m_updateCache.size();

    m_checkUpdateBtn->setEnabled(true);
    m_checkUpdateBtn->setText(tr("检查更新"));

    if (m_updateCheckFound > 0)
    {
        m_statsLabel->setText(tr("检查完成，发现 %1 个%2有更新")
            .arg(m_updateCheckFound).arg(typeDisplayName()));
    }
    else
    {
        m_statsLabel->setText(tr("所有%1已是最新版本").arg(typeDisplayName()));
    }

    updateBottomBarState();
}

void InstanceFilePage::onUpdateCheckBatchFailed(const QString &error)
{
    Q_UNUSED(error);
    m_installedBySha1.clear();
    m_checkUpdateBtn->setEnabled(true);
    m_checkUpdateBtn->setText(tr("检查更新"));
    m_statsLabel->setText(tr("检查更新失败"));
    updateBottomBarState();
}

void InstanceFilePage::resetUpdateState()
{
    // 清除之前标记的更新芯片
    for (QWidget *card : m_cardWidgets)
    {
        QLabel *chip = card->findChild<QLabel *>("fileUpdateChip");
        if (chip)
            chip->hide();
    }
    m_installedBySha1.clear();
    m_updateCheckFound = 0;
}

void InstanceFilePage::markCardHasUpdate(int index, const ModVersionFile &target)
{
    if (index < 0 || index >= m_cardWidgets.size())
        return;

    QWidget *card = m_cardWidgets[index];
    QLabel *chip = card->findChild<QLabel *>("fileUpdateChip");
    if (!chip)
        return;

    chip->show();
    QString targetVersion = target.version.isEmpty() ? target.fileName : target.version;
    chip->setToolTip(tr("可更新到 %1").arg(targetVersion));
}

void InstanceFilePage::onUpdateSelectedClicked()
{
    if (m_updateInProgress)
        return;

    m_updateQueue.clear();
    for (int idx : m_selectedIndices)
    {
        if (idx < 0 || idx >= m_fileList.size())
            continue;
        const FileEntry &entry = m_fileList.at(idx);
        if (!entry.sha1Hash.isEmpty() && m_updateCache.contains(entry.sha1Hash))
            m_updateQueue.append(idx);
    }

    if (m_updateQueue.isEmpty())
        return;

    m_updateInProgress = true;
    m_updateBtn->setEnabled(false);
    m_updateBtn->setText(tr("更新中..."));
    m_statsLabel->setText(tr("正在更新 %1 个%2...")
        .arg(m_updateQueue.size()).arg(typeDisplayName()));
    processNextModUpdate();
}

void InstanceFilePage::processNextModUpdate()
{
    if (m_updateQueue.isEmpty())
    {
        m_updateInProgress = false;
        m_updateBtn->setEnabled(true);
        m_updateBtn->setText(tr("更新"));
        m_statsLabel->setText(tr("%1更新完成").arg(typeDisplayName()));
        refreshFileList();
        return;
    }

    int idx = m_updateQueue.takeFirst();
    if (idx < 0 || idx >= m_fileList.size())
    {
        processNextModUpdate();
        return;
    }

    const FileEntry entry = m_fileList.at(idx);
    if (entry.sha1Hash.isEmpty() || !m_updateCache.contains(entry.sha1Hash))
    {
        processNextModUpdate();
        return;
    }

    const ModVersionFile target = m_updateCache.value(entry.sha1Hash);
    if (target.downloadUrl.isEmpty())
    {
        processNextModUpdate();
        return;
    }

    // 目标文件名需为 .zip，否则回退到下载 URL 文件名或本地文件名
    QString fileName = target.fileName;
    if (fileName.isEmpty() || !fileName.endsWith(".zip", Qt::CaseInsensitive))
    {
        QUrl url(target.downloadUrl);
        fileName = url.fileName();
        if (fileName.isEmpty() || fileName.contains('?')
            || !fileName.endsWith(".zip", Qt::CaseInsensitive))
        {
            fileName = entry.fileName;
        }
    }

    // 注册下载任务
    QString instanceName = QFileInfo(m_instancePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(
        instanceName, m_instancePath, QString(), QStringList());

    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading,
        tr("更新%1: ").arg(typeDisplayName()) + entry.fileName);
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

    auto *downloader = new ContentDownloader(this);

    connect(downloader, &ContentDownloader::downloadProgress, this,
        [taskId, fileName](qint64 received, qint64 total)
        {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0)
            {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
        });

    connect(downloader, &ContentDownloader::downloadFinished, this,
        [this, taskId, downloader, entry, fileName](const QString &destPath)
        {
            DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
            DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Completed, tr("更新完成"));
            downloader->deleteLater();
            m_updateCache.remove(entry.sha1Hash);
            m_sha1Cache.remove(entry.filePath);
            removeOldFile(entry, destPath);
            processNextModUpdate();
        });

    connect(downloader, &ContentDownloader::downloadFailed, this,
        [this, taskId, downloader](const QString &error)
        {
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Failed, error);
            downloader->deleteLater();
            processNextModUpdate();
        });

    ContentType ct = (m_fileType == ResourcePack)
        ? ContentType::ResourcePack : ContentType::ShaderPack;
    downloader->downloadToInstance(target.downloadUrl, fileName, m_instancePath, ct);
}

void InstanceFilePage::removeOldFile(const FileEntry &entry, const QString &newPath)
{
    if (entry.filePath.isEmpty())
        return;

    // 新旧文件同名时，ContentDownloader 已覆盖写入，无需删除
    if (QFileInfo(entry.filePath).absoluteFilePath()
        == QFileInfo(newPath).absoluteFilePath())
        return;

    if (QFile::exists(entry.filePath))
        QFile::remove(entry.filePath);
}

void InstanceFilePage::onToggleDragMode()
{
    m_isDragMode = !m_isDragMode;

    if (m_isDragMode)
    {
        // 切换到拖入模式
        m_stackedWidget->setCurrentWidget(m_dragPage);
        m_dragModeBtn->setText(tr("返回列表"));
        QColor tc(ThemeManager::instance()->currentThemeColor());
        m_dragModeBtn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/back.svg", tc, 18));

        // 初始化拖入区域样式
        QWidget *dropZone = m_dragPage->findChild<QWidget *>("dropZone");
        if (dropZone)
        {
            dropZone->setStyleSheet(QString(
                "#dropZone { background-color: transparent; "
                "border: 2px dashed %1; border-radius: 12px; }")
                .arg(tc.name()));
        }

        m_dragStatusLabel->hide();
    }
    else
    {
        // 切换回普通模式
        m_stackedWidget->setCurrentWidget(m_normalPage);
        m_dragModeBtn->setText(tr("拖入此处"));
        QColor tc(ThemeManager::instance()->currentThemeColor());
        m_dragModeBtn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/nav_import.svg", tc, 18));
    }
}

void InstanceFilePage::handleDroppedFiles(const QList<QUrl> &urls)
{
    if (m_instancePath.isEmpty())
    {
        m_dragStatusLabel->setText(tr("实例路径未设置"));
        m_dragStatusLabel->setStyleSheet("color: #F44336;");
        m_dragStatusLabel->show();
        return;
    }

    QString targetDir = m_instancePath + "/" + subDirName();
    QDir dir(targetDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    int copiedCount = 0;
    int skippedCount = 0;
    QStringList validSuffixes;

    switch (m_fileType)
    {
    case ResourcePack:
    case ShaderPack:
        validSuffixes = { ".zip" };
        break;
    case Screenshot:
        validSuffixes = { ".png", ".jpg", ".jpeg", ".webp" };
        break;
    case Save:
        // 存档是目录，不需要后缀过滤
        break;
    }

    for (const QUrl &url : urls)
    {
        QString srcPath = url.toLocalFile();
        if (srcPath.isEmpty())
            continue;

        QFileInfo fi(srcPath);

        if (m_fileType == Save)
        {
            // 存档：复制整个目录
            if (!fi.isDir())
            {
                skippedCount++;
                continue;
            }

            QString destPath = targetDir + "/" + fi.fileName();
            if (QDir(destPath).exists())
            {
                // 跳过已存在的目录
                skippedCount++;
                continue;
            }
            if (copyDir(srcPath, destPath))
            {
                copiedCount++;
            }
        }
        else
        {
            // 文件：检查后缀
            bool isValid = false;
            for (const QString &suf : validSuffixes)
            {
                if (fi.fileName().endsWith(suf, Qt::CaseInsensitive))
                {
                    isValid = true;
                    break;
                }
            }
            if (!isValid)
            {
                skippedCount++;
                continue;
            }

            QString destPath = targetDir + "/" + fi.fileName();
            if (QFile::exists(destPath))
            {
                // 跳过已存在的文件
                skippedCount++;
                continue;
            }

            if (QFile::copy(srcPath, destPath))
            {
                copiedCount++;
            }
        }
    }

    // 显示结果
    QString resultText;
    if (copiedCount > 0 && skippedCount > 0)
    {
        resultText = tr("已添加 %1 个%2，跳过 %3 个")
            .arg(copiedCount).arg(typeDisplayName()).arg(skippedCount);
        m_dragStatusLabel->setStyleSheet("color: #4CAF50;");
    }
    else if (copiedCount > 0)
    {
        resultText = tr("已成功添加 %1 个%2").arg(copiedCount).arg(typeDisplayName());
        m_dragStatusLabel->setStyleSheet("color: #4CAF50;");
    }
    else if (skippedCount > 0)
    {
        resultText = tr("跳过 %1 个文件（格式不匹配或已存在）").arg(skippedCount);
        m_dragStatusLabel->setStyleSheet("color: #FF9800;");
    }
    else
    {
        resultText = tr("没有有效的文件被添加");
        m_dragStatusLabel->setStyleSheet("color: #F44336;");
    }

    m_dragStatusLabel->setText(resultText);
    m_dragStatusLabel->show();

    // 如果有文件被添加，刷新普通模式的文件列表
    if (copiedCount > 0)
    {
        refreshFileList();
    }
}

// 递归复制目录
static bool copyDir(const QString &srcPath, const QString &dstPath)
{
    QDir srcDir(srcPath);
    if (!srcDir.exists())
        return false;

    QDir dstDir(dstPath);
    if (!dstDir.exists())
    {
        dstDir.mkpath(".");
    }

    for (const QFileInfo &fi : srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
    {
        QString destPath = dstPath + "/" + fi.fileName();
        if (fi.isDir())
        {
            if (!copyDir(fi.absoluteFilePath(), destPath))
                return false;
        }
        else
        {
            if (!QFile::copy(fi.absoluteFilePath(), destPath))
                return false;
        }
    }
    return true;
}