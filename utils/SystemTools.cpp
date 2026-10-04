/**
 * @file   SystemTools.cpp
 * @brief  系统工具类实现
 * @author BlockBox Team
 * @date   2026-06-27
 */

#include "SystemTools.h"
#include "MultiThreadDownloader.h"
#include "DownloadTaskManager.h"

#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include "components/AppMessageBox.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTimeEdit>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#endif

SystemTools* SystemTools::m_instance = nullptr;

SystemTools::SystemTools(QObject* parent)
    : QObject(parent)
    , m_multiDownloader(nullptr)
{
}

SystemTools::~SystemTools()
{
}

SystemTools* SystemTools::instance()
{
    if (!m_instance)
    {
        m_instance = new SystemTools();
    }
    return m_instance;
}

QString SystemTools::formatSize(qint64 bytes)
{
    if (bytes < 1024)
    {
        return QString::number(bytes) + " B";
    }
    else if (bytes < 1024 * 1024)
    {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }
    else if (bytes < 1024LL * 1024 * 1024)
    {
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    }
    else
    {
        return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
    }
}

// ============================================================================
// 内存优化
// ============================================================================

qint64 SystemTools::emptyWorkingSet()
{
#ifdef Q_OS_WIN
    HANDLE hProcess = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS_EX pmc;
    pmc.cb = sizeof(pmc);

    if (!GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
    {
        return -1;
    }

    SIZE_T beforeWorkingSet = pmc.WorkingSetSize;

    if (!SetProcessWorkingSetSize(hProcess, (SIZE_T)-1, (SIZE_T)-1))
    {
        return -1;
    }

    // 重新获取以确认释放量
    if (!GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
    {
        return beforeWorkingSet;
    }

    SIZE_T afterWorkingSet = pmc.WorkingSetSize;
    return static_cast<qint64>(beforeWorkingSet) - static_cast<qint64>(afterWorkingSet);
#else
    return -1;
#endif
}

void SystemTools::optimizeMemory(QWidget* parentWidget)
{
    qint64 totalFreed = 0;
    QStringList details;

    // 1. 释放当前进程工作集
    qint64 freed = emptyWorkingSet();
    if (freed >= 0)
    {
        totalFreed += freed;
        details.append(QString("当前进程工作集: %1").arg(formatSize(freed)));
    }

    // 2. 尝试清理系统缓存（通过调用 EmptyWorkingSet 对系统进程）
#ifdef Q_OS_WIN
    // 清理系统 DLL 缓存
    QProcess process;
    process.start("ipconfig", QStringList() << "/flushdns");
    process.waitForFinished(5000);
    details.append("DNS 缓存: 已清理");
#endif

    QString message;
    if (totalFreed > 0)
    {
        message = QString("内存优化完成!\n共释放: %1\n\n%2")
                      .arg(formatSize(totalFreed))
                      .arg(details.join("\n"));
    }
    else
    {
        message = QString("内存优化完成!\n\n%1").arg(details.join("\n"));
    }

    AppMessageBox msgBox(parentWidget);
    msgBox.setWindowTitle("内存优化");
    msgBox.setText(message);
    msgBox.setIcon(AppMessageBox::Information);
    msgBox.exec();

    emit operationFinished(message, true);
}

// ============================================================================
// 磁盘清理
// ============================================================================

qint64 SystemTools::cleanTempDirectory(const QString& dirPath)
{
    qint64 totalSize = 0;
    QDir dir(dirPath);

    if (!dir.exists())
    {
        return 0;
    }

    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& info : entries)
    {
        if (info.isSymLink() || info.isShortcut())
            continue;

        if (info.isDir())
        {
            totalSize += cleanTempDirectory(info.absoluteFilePath());
            QDir(info.absoluteFilePath()).removeRecursively();
        }
        else
        {
            totalSize += info.size();
            QFile::remove(info.absoluteFilePath());
        }
    }

    return totalSize;
}

qint64 SystemTools::estimateDirSize(const QString& dirPath)
{
    qint64 totalSize = 0;
    QDir dir(dirPath);
    if (!dir.exists())
        return 0;

    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& info : entries)
    {
        if (info.isSymLink() || info.isShortcut())
            continue;
        if (info.isDir())
            totalSize += estimateDirSize(info.absoluteFilePath());
        else
            totalSize += info.size();
    }
    return totalSize;
}

// ---------------------------------------------------------------------------
// 各清理类别实现
// ---------------------------------------------------------------------------

qint64 SystemTools::cleanWindowsTemp()
{
#ifdef Q_OS_WIN
    QString winTemp = QDir::cleanPath(qgetenv("SystemRoot")) + "/Temp";
    return cleanTempDirectory(winTemp);
#else
    return 0;
#endif
}

qint64 SystemTools::cleanUserTemp()
{
    QString userTemp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return cleanTempDirectory(userTemp);
}

qint64 SystemTools::cleanPrefetch()
{
#ifdef Q_OS_WIN
    QString prefetchPath = QDir::cleanPath(qgetenv("SystemRoot")) + "/Prefetch";
    return cleanTempDirectory(prefetchPath);
#else
    return 0;
#endif
}

void SystemTools::cleanDnsCache()
{
#ifdef Q_OS_WIN
    QProcess dnsProcess;
    dnsProcess.start("ipconfig", QStringList() << "/flushdns");
    dnsProcess.waitForFinished(5000);
#endif
}

bool SystemTools::cleanRecycleBin()
{
#ifdef Q_OS_WIN
    SHEmptyRecycleBinW(nullptr, nullptr, 0);
    return true;
#else
    return false;
#endif
}

QStringList SystemTools::getBrowserCachePaths()
{
    QStringList paths;
#ifdef Q_OS_WIN
    QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString userProfile = QDir::homePath();

    // Chrome
    QString chromeCache = localAppData + "/../Local/Google/Chrome/User Data/Default/Cache";
    paths << QDir::cleanPath(chromeCache);
    paths << QDir::cleanPath(localAppData + "/../Local/Google/Chrome/User Data/Default/Code Cache");

    // Edge
    paths << QDir::cleanPath(localAppData + "/../Local/Microsoft/Edge/User Data/Default/Cache");
    paths << QDir::cleanPath(localAppData + "/../Local/Microsoft/Edge/User Data/Default/Code Cache");

    // Firefox
    QDir firefoxProfiles(localAppData + "/../Local/Mozilla/Firefox/Profiles");
    if (firefoxProfiles.exists())
    {
        for (const QFileInfo& info : firefoxProfiles.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            paths << info.absoluteFilePath() + "/cache2";
            paths << info.absoluteFilePath() + "/thumbnails";
        }
    }

    // Chromium-based (Brave, Vivaldi, Opera)
    paths << QDir::cleanPath(localAppData + "/../Local/BraveSoftware/Brave-Browser/User Data/Default/Cache");
    paths << QDir::cleanPath(localAppData + "/../Local/Vivaldi/User Data/Default/Cache");
    paths << QDir::cleanPath(localAppData + "/../Local/Opera Software/Opera Stable/Cache");
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    QString home = QDir::homePath();

    // Chrome
    paths << home + "/.cache/google-chrome/Default/Cache";
    paths << home + "/.cache/google-chrome/Default/Code Cache";

    // Firefox
    QDir firefoxProfiles(home + "/.mozilla/firefox");
    if (firefoxProfiles.exists())
    {
        for (const QFileInfo& info : firefoxProfiles.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            paths << info.absoluteFilePath() + "/cache2";
        }
    }

    // Chromium
    paths << home + "/.cache/chromium/Default/Cache";
#elif defined(Q_OS_MACOS)
    QString home = QDir::homePath();
    QString lib = home + "/Library";

    // Chrome
    paths << lib + "/Caches/Google/Chrome/Default/Cache";
    paths << lib + "/Caches/Google/Chrome/Default/Code Cache";

    // Firefox
    QDir firefoxProfiles(lib + "/Caches/Firefox/Profiles");
    if (firefoxProfiles.exists())
    {
        for (const QFileInfo& info : firefoxProfiles.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            paths << info.absoluteFilePath() + "/cache2";
        }
    }

    // Safari
    paths << lib + "/Caches/com.apple.Safari";
#endif

    return paths;
}

qint64 SystemTools::cleanBrowserCache()
{
    qint64 total = 0;
    for (const QString& path : getBrowserCachePaths())
    {
        total += cleanTempDirectory(path);
    }
    return total;
}

qint64 SystemTools::cleanRecentDocs()
{
#ifdef Q_OS_WIN
    QString recentPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                         + "/../Microsoft/Windows/Recent";
    return cleanTempDirectory(QDir::cleanPath(recentPath));
#else
    return 0;
#endif
}

qint64 SystemTools::cleanThumbnailCache()
{
#ifdef Q_OS_WIN
    // Windows thumbnail cache is in AppData/Local/Microsoft/Windows/Explorer
    QString thumbPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + "/../Local/Microsoft/Windows/Explorer";
    qint64 total = cleanTempDirectory(QDir::cleanPath(thumbPath));

    // Also clean the thumbcache_*.db files in user profile
    QString userThumb = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + "/../Local/Microsoft/Windows/Caches";
    total += cleanTempDirectory(QDir::cleanPath(userThumb));

    return total;
#else
    return 0;
#endif
}

qint64 SystemTools::cleanWindowsLogs()
{
#ifdef Q_OS_WIN
    QString logPath = QDir::cleanPath(qgetenv("SystemRoot")) + "/System32/logfiles";
    return cleanTempDirectory(logPath);
#else
    return 0;
#endif
}

qint64 SystemTools::cleanMemoryDumps()
{
#ifdef Q_OS_WIN
    qint64 total = 0;
    QStringList dumpPaths;
    dumpPaths << QDir::cleanPath(qgetenv("SystemRoot")) + "/memory.dmp";
    dumpPaths << QDir::cleanPath(qgetenv("SystemRoot")) + "/Minidump";

    for (const QString& path : dumpPaths)
    {
        QFileInfo info(path);
        if (info.isDir())
            total += cleanTempDirectory(path);
        else if (info.exists())
        {
            total += info.size();
            QFile::remove(path);
        }
    }
    return total;
#else
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// 获取所有可清理类别（含预估大小）
// ---------------------------------------------------------------------------

QVector<CleanCategory> SystemTools::getCleanCategories()
{
    QVector<CleanCategory> categories;

    CleanCategory cat;

#ifdef Q_OS_WIN
    cat.id        = "windows_temp";
    cat.displayName = QStringLiteral("Windows 临时文件");
    cat.description = QStringLiteral("C:/Windows/Temp 下的系统临时文件");
    cat.estimatedSize = estimateDirSize(QDir::cleanPath(qgetenv("SystemRoot")) + "/Temp");
    cat.checked     = true;
    categories.append(cat);
#endif

    cat.id        = "user_temp";
    cat.displayName = QStringLiteral("用户临时文件");
    cat.description = QStringLiteral("用户临时目录");
    cat.estimatedSize = estimateDirSize(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    cat.checked     = true;
    categories.append(cat);

#ifdef Q_OS_WIN
    cat.id        = "prefetch";
    cat.displayName = QStringLiteral("Prefetch 预读文件");
    cat.description = QStringLiteral("系统预读缓存文件，可安全清理");
    cat.estimatedSize = estimateDirSize(QDir::cleanPath(qgetenv("SystemRoot")) + "/Prefetch");
    cat.checked     = true;
    categories.append(cat);

    cat.id        = "dns_cache";
    cat.displayName = QStringLiteral("DNS 缓存");
    cat.description = QStringLiteral("刷新 DNS 解析缓存");
    cat.estimatedSize = -1;
    cat.checked     = true;
    categories.append(cat);

    cat.id        = "recycle_bin";
    cat.displayName = QStringLiteral("回收站");
    cat.description = QStringLiteral("清空回收站中的文件");
    cat.estimatedSize = -1;
    cat.checked     = false;
    categories.append(cat);
#endif

    cat.id        = "browser_cache";
    cat.displayName = QStringLiteral("浏览器缓存");
    cat.description = QStringLiteral("浏览器缓存文件");
    {
        qint64 browserSize = 0;
        for (const QString& p : getBrowserCachePaths())
            browserSize += estimateDirSize(p);
        cat.estimatedSize = browserSize;
    }
    cat.checked     = false;
    categories.append(cat);

#ifdef Q_OS_WIN
    cat.id        = "recent_docs";
    cat.displayName = QStringLiteral("最近文档记录");
    cat.description = QStringLiteral("最近打开的文件快捷方式列表");
    cat.estimatedSize = estimateDirSize(QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + "/../Microsoft/Windows/Recent"));
    cat.checked     = false;
    categories.append(cat);

    cat.id        = "thumbnail_cache";
    cat.displayName = QStringLiteral("缩略图缓存");
    cat.description = QStringLiteral("Windows 缩略图缓存文件");
    {
        QString thumbPath = QDir::cleanPath(
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + "/../Local/Microsoft/Windows/Explorer");
        cat.estimatedSize = estimateDirSize(thumbPath);
    }
    cat.checked     = false;
    categories.append(cat);

    cat.id        = "windows_logs";
    cat.displayName = QStringLiteral("Windows 日志文件");
    cat.description = QStringLiteral("C:/Windows/System32/logfiles 下的日志");
    cat.estimatedSize = estimateDirSize(
        QDir::cleanPath(qgetenv("SystemRoot")) + "/System32/logfiles");
    cat.checked     = false;
    categories.append(cat);

    cat.id        = "memory_dumps";
    cat.displayName = QStringLiteral("系统内存转储");
    cat.description = QStringLiteral("系统崩溃时的内存转储文件 (memory.dmp, Minidump)");
    {
        qint64 dumpSize = 0;
        QString memDump = QDir::cleanPath(qgetenv("SystemRoot")) + "/memory.dmp";
        QString minidumpDir = QDir::cleanPath(qgetenv("SystemRoot")) + "/Minidump";
        QFileInfo memInfo(memDump);
        if (memInfo.exists())
            dumpSize += memInfo.size();
        dumpSize += estimateDirSize(minidumpDir);
        cat.estimatedSize = dumpSize;
    }
    cat.checked     = false;
    categories.append(cat);
#endif

    return categories;
}

// ---------------------------------------------------------------------------
// 磁盘清理选择对话框
// ---------------------------------------------------------------------------

void SystemTools::showCleanupDialog(QWidget* parentWidget)
{
    QDialog dialog(parentWidget);
    dialog.setWindowTitle(QStringLiteral("磁盘清理"));
    dialog.setMinimumWidth(560);
    dialog.setMinimumHeight(480);

    auto* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 20, 24, 20);

    // 标题
    auto* titleLabel = new QLabel(QStringLiteral("选择要清理的项目"));
    titleLabel->setObjectName("homeSectionTitle");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    mainLayout->addWidget(titleLabel);

    auto* descLabel = new QLabel(QStringLiteral("勾选需要清理的类别，系统将删除对应的临时文件和缓存。"));
    descLabel->setStyleSheet("color: #666; font-size: 13px;");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // 获取可清理类别
    QVector<CleanCategory> categories = getCleanCategories();

    // 滚动区域
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");

    auto* checkContainer = new QWidget();
    auto* checkLayout = new QVBoxLayout(checkContainer);
    checkLayout->setSpacing(6);
    checkLayout->setContentsMargins(0, 0, 0, 0);

    QVector<QCheckBox*> checkBoxes;
    QVector<QLabel*> sizeLabels;

    for (int i = 0; i < categories.size(); ++i)
    {
        const auto& cat = categories[i];

        auto* row = new QWidget();
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 6, 8, 6);
        rowLayout->setSpacing(8);

        auto* cb = new QCheckBox(cat.displayName);
        cb->setChecked(cat.checked);
        cb->setToolTip(cat.description);
        cb->setStyleSheet("font-size: 14px;");
        checkBoxes.append(cb);

        auto* sizeLabel = new QLabel();
        if (cat.estimatedSize >= 0)
            sizeLabel->setText(formatSize(cat.estimatedSize));
        else
            sizeLabel->setText(QStringLiteral("未知"));
        sizeLabel->setStyleSheet("color: #999; font-size: 12px;");
        sizeLabel->setFixedWidth(80);
        sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sizeLabels.append(sizeLabel);

        rowLayout->addWidget(cb, 1);
        rowLayout->addWidget(sizeLabel);

        checkLayout->addWidget(row);
    }

    // 全选/取消全选
    auto* selectRow = new QWidget();
    auto* selectLayout = new QHBoxLayout(selectRow);
    selectLayout->setContentsMargins(8, 4, 8, 4);

    auto* selectAllBtn = new QPushButton(QStringLiteral("全选"));
    selectAllBtn->setObjectName("launchButton");
    selectAllBtn->setCursor(Qt::PointingHandCursor);
    selectAllBtn->setFixedWidth(80);

    auto* deselectAllBtn = new QPushButton(QStringLiteral("取消全选"));
    deselectAllBtn->setObjectName("launchButton");
    deselectAllBtn->setCursor(Qt::PointingHandCursor);
    deselectAllBtn->setFixedWidth(80);

    selectLayout->addWidget(selectAllBtn);
    selectLayout->addWidget(deselectAllBtn);
    selectLayout->addStretch();
    checkLayout->addWidget(selectRow);

    QObject::connect(selectAllBtn, &QPushButton::clicked, [&]() {
        for (auto* cb : checkBoxes)
            cb->setChecked(true);
    });
    QObject::connect(deselectAllBtn, &QPushButton::clicked, [&]() {
        for (auto* cb : checkBoxes)
            cb->setChecked(false);
    });

    scrollArea->setWidget(checkContainer);
    mainLayout->addWidget(scrollArea, 1);

    // 预估总计
    auto* totalLabel = new QLabel();
    {
        qint64 total = 0;
        for (const auto& cat : categories)
            if (cat.estimatedSize >= 0)
                total += cat.estimatedSize;
        totalLabel->setText(QStringLiteral("可释放空间: %1").arg(formatSize(total)));
    }
    totalLabel->setStyleSheet("color: #555; font-size: 13px; font-weight: bold;");
    mainLayout->addWidget(totalLabel);

    // 按钮
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始清理"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    mainLayout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    // ----- 执行清理 -----
    qint64 totalCleaned = 0;
    QStringList details;
    bool anySelected = false;

    for (int i = 0; i < categories.size(); ++i)
    {
        if (!checkBoxes[i]->isChecked())
            continue;
        anySelected = true;

        const QString& id = categories[i].id;

        if (id == "windows_temp")
        {
            qint64 sz = cleanWindowsTemp();
            if (sz > 0) { totalCleaned += sz; details.append(QStringLiteral("Windows 临时文件: %1").arg(formatSize(sz))); }
        }
        else if (id == "user_temp")
        {
            qint64 sz = cleanUserTemp();
            if (sz > 0) { totalCleaned += sz; details.append(QStringLiteral("用户临时文件: %1").arg(formatSize(sz))); }
        }
        else if (id == "prefetch")
        {
            qint64 sz = cleanPrefetch();
            if (sz > 0) { totalCleaned += sz; details.append(QStringLiteral("Prefetch 预读文件: %1").arg(formatSize(sz))); }
        }
        else if (id == "dns_cache")
        {
            cleanDnsCache();
            details.append(QStringLiteral("DNS 缓存: 已清理"));
        }
        else if (id == "recycle_bin")
        {
            if (cleanRecycleBin())
                details.append(QStringLiteral("回收站: 已清空"));
            else
                details.append(QStringLiteral("回收站: 清理失败"));
        }
        else if (id == "browser_cache")
        {
            qint64 sz = cleanBrowserCache();
            if (sz > 0) { totalCleaned += sz; details.append(QStringLiteral("浏览器缓存: %1").arg(formatSize(sz))); }
        }
        else if (id == "recent_docs")
        {
            qint64 sz = cleanRecentDocs();
            if (sz > 0) { totalCleaned += sz; details.append(QStringLiteral("最近文档记录: %1").arg(formatSize(sz))); }
        }
        else if (id == "thumbnail_cache")
        {
            qint64 sz = cleanThumbnailCache();
            if (sz > 0) { totalCleaned += sz; details.append(QStringLiteral("缩略图缓存: %1").arg(formatSize(sz))); }
        }
        else if (id == "windows_logs")
        {
            qint64 sz = cleanWindowsLogs();
            if (sz > 0) { totalCleaned += sz; details.append(QStringLiteral("Windows 日志: %1").arg(formatSize(sz))); }
        }
        else if (id == "memory_dumps")
        {
            qint64 sz = cleanMemoryDumps();
            if (sz > 0) { totalCleaned += sz; details.append(QStringLiteral("系统内存转储: %1").arg(formatSize(sz))); }
        }
    }

    QString resultMsg;
    if (!anySelected)
    {
        resultMsg = QStringLiteral("没有选择任何清理项目。");
    }
    else if (totalCleaned > 0)
    {
        resultMsg = QStringLiteral("磁盘清理完成!\n共释放: %1\n\n%2")
                        .arg(formatSize(totalCleaned))
                        .arg(details.join("\n"));
    }
    else
    {
        resultMsg = QStringLiteral("磁盘清理完成!\n没有找到需要清理的文件。\n\n%1").arg(details.join("\n"));
    }

    AppMessageBox msgBox(parentWidget);
    msgBox.setWindowTitle(QStringLiteral("磁盘清理"));
    msgBox.setText(resultMsg);
    msgBox.setIcon(totalCleaned > 0 ? AppMessageBox::Information : AppMessageBox::Information);
    msgBox.exec();

    emit operationFinished(resultMsg, true);
}

// ---------------------------------------------------------------------------
// 旧版 cleanDisk（兼容调用，现在转发到新对话框）
// ---------------------------------------------------------------------------

void SystemTools::cleanDisk(QWidget* parentWidget)
{
    showCleanupDialog(parentWidget);
}

// ============================================================================
// 定时关机
// ============================================================================

void SystemTools::scheduleShutdown(QWidget* parentWidget)
{
    QDialog dialog(parentWidget);
    dialog.setWindowTitle("定时关机");
    dialog.setMinimumWidth(420);

    auto* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 20, 24, 20);

    // 标题
    auto* titleLabel = new QLabel("设置定时关机");
    titleLabel->setObjectName("homeSectionTitle");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    mainLayout->addWidget(titleLabel);

    // 关机模式选择
    auto* modeGroup = new QGroupBox("关机模式");
    auto* modeLayout = new QVBoxLayout(modeGroup);

    auto* shutdownRadio = new QRadioButton("关机 (shutdown)");
    auto* restartRadio = new QRadioButton("重启 (restart)");
    auto* cancelRadio = new QRadioButton("取消已计划的关机");

    shutdownRadio->setChecked(true);
    modeLayout->addWidget(shutdownRadio);
    modeLayout->addWidget(restartRadio);
    modeLayout->addWidget(cancelRadio);
    mainLayout->addWidget(modeGroup);

    // 时间类型选择
    auto* timeTypeGroup = new QGroupBox("时间类型");
    auto* timeTypeLayout = new QHBoxLayout(timeTypeGroup);

    auto* countdownRadio = new QRadioButton("多久后关机");
    auto* specificTimeRadio = new QRadioButton("指定时间关机");
    countdownRadio->setChecked(true);
    timeTypeLayout->addWidget(countdownRadio);
    timeTypeLayout->addWidget(specificTimeRadio);
    mainLayout->addWidget(timeTypeGroup);

    // 倒计时设置
    auto* countdownGroup = new QGroupBox("倒计时设置");
    auto* countdownLayout = new QFormLayout(countdownGroup);

    auto* hourSpin = new QSpinBox();
    hourSpin->setRange(0, 23);
    hourSpin->setSuffix(" 小时");
    hourSpin->setValue(0);
    hourSpin->setMinimumWidth(120);

    auto* minuteSpin = new QSpinBox();
    minuteSpin->setRange(0, 59);
    minuteSpin->setSuffix(" 分钟");
    minuteSpin->setValue(30);
    minuteSpin->setMinimumWidth(120);

    countdownLayout->addRow("时:", hourSpin);
    countdownLayout->addRow("分:", minuteSpin);

    auto* timeHintLabel = new QLabel("设置 0 小时 0 分钟将立即执行");
    timeHintLabel->setStyleSheet("color: #999; font-size: 12px;");
    countdownLayout->addRow(timeHintLabel);

    mainLayout->addWidget(countdownGroup);

    // 指定时间设置
    auto* specificGroup = new QGroupBox("指定时间");
    auto* specificLayout = new QFormLayout(specificGroup);

    auto* timeEdit = new QTimeEdit();
    timeEdit->setDisplayFormat("HH:mm");
    timeEdit->setTime(QTime::currentTime().addSecs(3600));
    timeEdit->setMinimumWidth(120);

    auto* specificHintLabel = new QLabel("设置今天指定时间执行关机（时间已过则延至明天）");
    specificHintLabel->setStyleSheet("color: #999; font-size: 12px;");

    specificLayout->addRow("时间:", timeEdit);
    specificLayout->addRow(specificHintLabel);

    mainLayout->addWidget(specificGroup);
    specificGroup->setVisible(false);

    // 切换时间类型
    QObject::connect(countdownRadio, &QRadioButton::toggled, [&](bool checked) {
        countdownGroup->setVisible(checked);
        specificGroup->setVisible(!checked);
    });

    // 禁用/启用时间设置
    QObject::connect(cancelRadio, &QRadioButton::toggled, [&](bool checked) {
        timeTypeGroup->setEnabled(!checked);
        countdownGroup->setEnabled(!checked);
        specificGroup->setEnabled(!checked);
    });

    // 按钮
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText("确定");
    buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");
    mainLayout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

#ifdef Q_OS_WIN
    if (cancelRadio->isChecked())
    {
        cancelShutdown(parentWidget);
        return;
    }

    int totalSeconds;
    QString timeStr;

    if (countdownRadio->isChecked())
    {
        totalSeconds = hourSpin->value() * 3600 + minuteSpin->value() * 60;

        if (totalSeconds == 0)
        {
            timeStr = "立即";
        }
        else
        {
            int h = totalSeconds / 3600;
            int m = (totalSeconds % 3600) / 60;
            int s = totalSeconds % 60;
            if (h > 0)
                timeStr = QString("%1 小时 %2 分钟 %3 秒后").arg(h).arg(m).arg(s);
            else if (m > 0)
                timeStr = QString("%1 分钟 %2 秒后").arg(m).arg(s);
            else
                timeStr = QString("%1 秒后").arg(s);
        }
    }
    else
    {
        QTime selectedTime = timeEdit->time();
        QTime currentTime = QTime::currentTime();
        int secsFromNow = currentTime.secsTo(selectedTime);
        if (secsFromNow < 0)
            secsFromNow += 24 * 3600;
        totalSeconds = secsFromNow;
        timeStr = selectedTime.toString("HH:mm");
    }

    QString command;
    QString actionName;

    if (restartRadio->isChecked())
    {
        command = QString("shutdown /r /t %1").arg(totalSeconds);
        actionName = "重启";
    }
    else
    {
        command = QString("shutdown /s /t %1").arg(totalSeconds);
        actionName = "关机";
    }

    QProcess::startDetached(command);

    AppMessageBox msgBox(parentWidget);
    msgBox.setWindowTitle("定时关机");
    msgBox.setText(QString("已设置%1在 %2 执行！\n\n如需取消，请重新打开定时关机并选择\"取消已计划的关机\"。")
                       .arg(actionName)
                       .arg(timeStr));
    msgBox.setIcon(AppMessageBox::Information);
    msgBox.exec();
#else
    AppMessageBox::information(parentWidget, "定时关机", "此功能仅在 Windows 系统下可用。");
#endif

    emit operationFinished("定时关机已设置", true);
}

void SystemTools::cancelShutdown(QWidget* parentWidget)
{
#ifdef Q_OS_WIN
    QProcess::startDetached("shutdown /a");

    AppMessageBox msgBox(parentWidget);
    msgBox.setWindowTitle("定时关机");
    msgBox.setText("已取消计划的关机。");
    msgBox.setIcon(AppMessageBox::Information);
    msgBox.exec();
#endif
    emit operationFinished("已取消定时关机", true);
}

// ============================================================================
// 多线程下载
// ============================================================================

void SystemTools::startMultiThreadDownload(QWidget* parentWidget)
{
    if (m_multiDownloader) {
        AppMessageBox::information(parentWidget, tr("多线程下载"),
                                 tr("已有下载任务正在进行中，请等待完成或取消后再试。"));
        return;
    }

    QDialog dialog(parentWidget);
    dialog.setWindowTitle(tr("多线程下载"));
    dialog.setMinimumWidth(520);

    auto* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 20, 24, 20);

    auto* titleLabel = new QLabel(tr("多线程下载"));
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    mainLayout->addWidget(titleLabel);

    auto* urlGroup = new QGroupBox(tr("下载地址"));
    auto* urlLayout = new QVBoxLayout(urlGroup);
    auto* urlEdit = new QLineEdit();
    urlEdit->setPlaceholderText(tr("请输入直链下载链接 (http/https)..."));
    urlLayout->addWidget(urlEdit);

    auto* linkHint = new QLabel(tr("注意：百度网盘、夸克网盘等网盘链接不支持多线程下载。\n"
                                   "直链获取方式：在网页的下载按钮右键，点击复制链接地址，在上方粘贴。\n"
                                   "直链特征：以 http:// 或 https:// 开头，且服务器支持 Range 请求（断点续传）。"));
    linkHint->setStyleSheet("color: #999; font-size: 11px; padding: 4px 0;");
    linkHint->setWordWrap(true);
    urlLayout->addWidget(linkHint);
    mainLayout->addWidget(urlGroup);

    auto* saveGroup = new QGroupBox(tr("保存路径"));
    auto* saveLayout = new QHBoxLayout(saveGroup);
    auto* saveEdit = new QLineEdit();
    saveEdit->setPlaceholderText(tr("选择保存路径..."));
    saveEdit->setReadOnly(true);
    saveLayout->addWidget(saveEdit);

    auto* browseBtn = new QPushButton(tr("浏览..."));
    browseBtn->setObjectName("launchButton");
    browseBtn->setCursor(Qt::PointingHandCursor);
    saveLayout->addWidget(browseBtn);
    mainLayout->addWidget(saveGroup);

    QObject::connect(browseBtn, &QPushButton::clicked, [&]() {
        QString defaultName = "download";
        QUrl url(urlEdit->text());
        if (url.isValid() && !url.fileName().isEmpty())
        {
            defaultName = url.fileName();
        }
        QString filePath = AppFileDialog::getSaveFileName(&dialog, tr("选择保存路径"), defaultName);
        if (!filePath.isEmpty())
        {
            saveEdit->setText(filePath);
        }
    });

    auto* threadGroup = new QGroupBox(tr("下载设置"));
    auto* threadLayout = new QFormLayout(threadGroup);
    auto* threadSpin = new QSpinBox();
    threadSpin->setRange(1, 128);
    threadSpin->setValue(8);
    threadSpin->setSuffix(tr(" 线程"));
    threadLayout->addRow(tr("线程数:"), threadSpin);

    auto* uaEdit = new QLineEdit();
    uaEdit->setPlaceholderText(tr("留空则使用默认: BlockBox/1.0"));
    threadLayout->addRow(tr("User-Agent:"), uaEdit);

    auto* threadHint = new QLabel(tr("推荐 4-16 线程，文件小于 1MB 自动降为单线程。"
                                     "超过 64 线程可能增加服务器压力，实际速度提升有限。"));
    threadHint->setStyleSheet("color: #999; font-size: 12px;");
    threadHint->setWordWrap(true);
    threadLayout->addRow(threadHint);
    mainLayout->addWidget(threadGroup);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("开始下载"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    mainLayout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    QString url = urlEdit->text().trimmed();
    QString savePath = saveEdit->text().trimmed();
    int threadCount = threadSpin->value();
    QString userAgent = uaEdit->text().trimmed();

    if (url.isEmpty()) {
        AppMessageBox::warning(parentWidget, tr("多线程下载"), tr("请输入下载链接！"));
        return;
    }
    if (savePath.isEmpty()) {
        AppMessageBox::warning(parentWidget, tr("多线程下载"), tr("请选择保存路径！"));
        return;
    }

    QString fileName = QFileInfo(savePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(fileName, QFileInfo(savePath).absolutePath(),
                                                              QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(taskId, DownloadTaskStatus::Downloading, tr("连接中..."));

    m_multiDownloader = new MultiThreadDownloader(this);

    connect(m_multiDownloader, &MultiThreadDownloader::downloadProgress, this,
        [taskId](const QString &, qint64 received, qint64 total) {
            DownloadTaskManager *mgr = DownloadTaskManager::instance();
            if (total > 0) {
                int pct = static_cast<int>((received * 100) / total);
                mgr->updateTaskProgressPercent(taskId, pct);
            }
            mgr->updateTaskFileProgress(taskId, QString(), received, total);
        });

    connect(m_multiDownloader, &MultiThreadDownloader::downloadCompleted, this,
        [this, taskId, savePath, parentWidget](const QString &) {
            DownloadTaskManager::instance()->updateTaskStatus(taskId, DownloadTaskStatus::Completed,
                                                              tr("下载完成"));
            DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
            m_multiDownloader->deleteLater();
            m_multiDownloader = nullptr;
            AppMessageBox::information(parentWidget, tr("多线程下载"),
                                     tr("下载完成!\n保存至: %1").arg(savePath));
        });

    connect(m_multiDownloader, &MultiThreadDownloader::downloadFailed, this,
        [this, taskId, parentWidget](const QString &, const QString &error) {
            DownloadTaskManager::instance()->updateTaskStatus(taskId, DownloadTaskStatus::Failed, error);
            m_multiDownloader->deleteLater();
            m_multiDownloader = nullptr;
            AppMessageBox::warning(parentWidget, tr("多线程下载"),
                                 tr("下载失败: %1").arg(error));
        });

    DownloadTaskManager::instance()->updateTaskStatus(taskId, DownloadTaskStatus::Downloading, tr("下载中"));
    m_multiDownloader->startDownload(url, savePath, threadCount, taskId, userAgent);

    emit operationFinished(tr("下载任务已启动"), true);
}

// ============================================================================
// GitHub 加速（通过修改本地 hosts 文件）
// ============================================================================

namespace {

const QString kHostsStartMarker = QStringLiteral("# === BlockBox GitHub Accelerator Start ===");
const QString kHostsEndMarker = QStringLiteral("# === BlockBox GitHub Accelerator End ===");

bool isIpv4Address(const QString& token)
{
    QStringList parts = token.split('.');
    if (parts.size() != 4) return false;
    for (const QString& p : parts)
    {
        bool ok = false;
        int v = p.toInt(&ok);
        if (!ok || v < 0 || v > 255) return false;
    }
    return true;
}

// 解析在线 hosts 文本，提取 "IP 主机名" 条目，按主机名去重
QStringList parseGithubHosts(const QString& content)
{
    QStringList entries;
    QSet<QString> seenHosts;
    const QStringList lines = content.split('\n');
    for (QString line : lines)
    {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        // 规范化空白分隔
        QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 2) continue;
        if (!isIpv4Address(parts.first())) continue;
        const QString ip = parts.first();
        // 每个主机名生成一条（hosts 文件仅取首个匹配，故去重保留首条）
        for (int i = 1; i < parts.size(); ++i)
        {
            const QString host = parts[i];
            if (seenHosts.contains(host)) continue;
            // 仅处理 GitHub 相关域名
            if (!host.contains("github", Qt::CaseInsensitive)
                && !host.contains("githubusercontent", Qt::CaseInsensitive)
                && !host.contains("fastly", Qt::CaseInsensitive)) continue;
            seenHosts.insert(host);
            entries.append(ip + " " + host);
        }
    }
    return entries;
}

// 网络获取失败时的内置兜底 IP 映射
QStringList defaultGithubHostsEntries()
{
    return QStringList {
        "140.82.112.3 github.com",
        "140.82.113.5 api.github.com",
        "185.199.108.133 raw.githubusercontent.com",
        "185.199.108.133 assets-cdn.github.com",
        "185.199.108.133 objects.githubusercontent.com",
        "185.199.108.133 codeload.github.com",
        "199.232.69.194 github.global.ssl.fastly.net"
    };
}

} // namespace

QString SystemTools::hostsFilePath()
{
#ifdef Q_OS_WIN
    QString systemRoot = QString::fromLocal8Bit(qgetenv("SystemRoot"));
    if (systemRoot.isEmpty()) systemRoot = QStringLiteral("C:\\Windows");
    return systemRoot + "\\System32\\drivers\\etc\\hosts";
#else
    return QStringLiteral("/etc/hosts");
#endif
}

QString SystemTools::readHostsFile()
{
    QFile file(hostsFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QByteArray data = file.readAll();
    file.close();
    return QString::fromUtf8(data);
}

QString SystemTools::removeBlockBoxHostsSection(const QString& content)
{
    int startIdx = content.indexOf(kHostsStartMarker);
    if (startIdx < 0) return content;
    int endIdx = content.indexOf(kHostsEndMarker, startIdx + kHostsStartMarker.length());
    int removeEnd;
    if (endIdx < 0)
    {
        removeEnd = content.length();
    }
    else
    {
        removeEnd = endIdx + kHostsEndMarker.length();
    }
    // 移除标记段后紧跟的换行符，避免留下空行
    while (removeEnd < content.length() && (content[removeEnd] == '\n' || content[removeEnd] == '\r'))
    {
        ++removeEnd;
    }
    return content.left(startIdx) + content.mid(removeEnd);
}

bool SystemTools::applyHostsElevated(const QString& newContent, QString& errorMsg)
{
#ifdef Q_OS_WIN
    // 写入临时文件（hosts 内容用 UTF-8，hosts 文件本身支持 UTF-8）
    const QString tempPath = QDir::tempPath() + "/blockbox_hosts_new.txt";
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly))
    {
        errorMsg = tr("无法写入临时文件: %1").arg(tempFile.errorString());
        return false;
    }
    tempFile.write(newContent.toUtf8());
    tempFile.close();

    const QString dest = hostsFilePath();
    const QString scriptPath = QDir::tempPath() + "/blockbox_apply_hosts.ps1";
    const QString resultPath = QDir::tempPath() + "/blockbox_hosts_result.txt";

    // 清理可能残留的旧结果文件
    QFile::remove(resultPath);

    // 使用 PowerShell 脚本，比批处理更可靠：
    // - Set-Content -Force 能覆盖只读/系统文件
    // - 原生支持 UTF-8 读写，避免编码问题
    // - 完整的错误捕获，能输出具体失败原因
    // PowerShell 单引号字符串中，转义单引号的方式是写成两个单引号
    QString srcEscaped = tempPath; srcEscaped.replace('\'', "''");
    QString dstEscaped = dest; dstEscaped.replace('\'', "''");
    QString resEscaped = resultPath; resEscaped.replace('\'', "''");

    QString script = QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "try {\n"
        "    $src = '%1'\n"
        "    $dst = '%2'\n"
        "    $resultFile = '%3'\n"
        "    # 去除只读/隐藏属性\n"
        "    if (Test-Path $dst) { attrib -r -h $dst }\n"
        "    # 用 Get-Content + Set-Content 方式写入（-Force 覆盖只读）\n"
        "    $content = Get-Content -Path $src -Raw -Encoding UTF8\n"
        "    Set-Content -Path $dst -Value $content -Force -NoNewline -Encoding UTF8\n"
        "    # 刷新 DNS 缓存\n"
        "    ipconfig /flushdns | Out-Null\n"
        "    'OK' | Out-File -FilePath $resultFile -Encoding ASCII\n"
        "} catch {\n"
        "    $msg = $_.Exception.Message\n"
        "    ('FAIL: ' + $msg) | Out-File -FilePath $resultFile -Encoding UTF8\n"
        "}\n"
    ).arg(srcEscaped, dstEscaped, resEscaped);

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        errorMsg = tr("无法写入 PowerShell 脚本");
        return false;
    }
    // PowerShell 脚本用 UTF-8 编码（带 BOM 更稳妥，但无 BOM 也可）
    scriptFile.write("\xEF\xBB\xBF"); // UTF-8 BOM
    scriptFile.write(script.toUtf8());
    scriptFile.close();

    // 通过 cmd 启动 PowerShell 执行脚本，使用 runas 提权
    // 命令行参数：-NoProfile 不加载配置 -ExecutionPolicy Bypass 绕过策略 -File 指定脚本
    QString params = QStringLiteral("-NoProfile -ExecutionPolicy Bypass -File \"%1\"")
                         .arg(scriptPath);

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = reinterpret_cast<LPCWSTR>(params.utf16());
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei))
    {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED)
        {
            errorMsg = tr("已取消管理员授权");
        }
        else
        {
            errorMsg = tr("无法启动管理员进程 (错误码: %1)").arg(err);
        }
        return false;
    }
    if (sei.hProcess)
    {
        // PowerShell 启动较慢，给足等待时间
        WaitForSingleObject(sei.hProcess, 60000);
        CloseHandle(sei.hProcess);
    }

    // 读取结果文件，判断是否真正执行成功
    QFile resultFile(resultPath);
    bool success = false;
    if (resultFile.open(QIODevice::ReadOnly))
    {
        QByteArray data = resultFile.readAll();
        // 结果文件可能是 UTF-8 或 ASCII，去掉 BOM
        if (data.startsWith("\xEF\xBB\xBF")) data = data.mid(3);
        QString result = QString::fromUtf8(data).trimmed();
        resultFile.close();
        if (result == "OK")
        {
            success = true;
        }
        else
        {
            // 结果文件包含具体的异常信息
            errorMsg = result.isEmpty()
                ? tr("写入 hosts 文件失败（未知原因，可能被安全软件拦截）")
                : tr("写入 hosts 文件失败：%1").arg(result);
        }
    }
    else
    {
        errorMsg = tr("管理员进程执行异常，未获取到结果（可能被安全软件拦截）");
    }

    QFile::remove(tempPath);
    QFile::remove(scriptPath);
    QFile::remove(resultPath);

    if (success) errorMsg.clear();
    return success;
#else
    Q_UNUSED(newContent)
    errorMsg = tr("此功能仅在 Windows 系统下可用");
    return false;
#endif
}

void SystemTools::accelerateGithub(QWidget* parentWidget)
{
#ifdef Q_OS_WIN
    QDialog dialog(parentWidget);
    dialog.setWindowTitle(tr("GitHub 加速"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *descLabel = new QLabel(tr(
        "通过修改本地 hosts 文件，将 GitHub 相关域名指向更快的 IP 地址，从而提升访问速度。\n"
        "点击“启用加速”将从网络获取最新 IP 映射并写入 hosts 文件（需要管理员权限）。"));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    bool currentlyEnabled = readHostsFile().contains(kHostsStartMarker);
    auto *statusLabel = new QLabel(currentlyEnabled
        ? tr("当前状态：已启用加速")
        : tr("当前状态：未启用加速"));
    layout->addWidget(statusLabel);

    layout->addWidget(new QLabel(tr("即将写入的 hosts 映射：")));

    auto *previewEdit = new QTextEdit();
    previewEdit->setReadOnly(true);
    previewEdit->setMaximumHeight(180);
    previewEdit->setPlaceholderText(tr("正在获取最新的 GitHub IP 映射..."));
    layout->addWidget(previewEdit);

    auto *buttonBox = new QDialogButtonBox();
    auto *applyBtn = buttonBox->addButton(tr("启用加速"), QDialogButtonBox::AcceptRole);
    auto *restoreBtn = buttonBox->addButton(tr("恢复默认"), QDialogButtonBox::ActionRole);
    auto *closeBtn = buttonBox->addButton(QDialogButtonBox::Close);
    applyBtn->setEnabled(false);
    restoreBtn->setEnabled(currentlyEnabled);
    layout->addWidget(buttonBox);

    QStringList fetchedEntries;
    auto *networkManager = new QNetworkAccessManager(&dialog);
    QNetworkRequest request(QUrl("https://raw.hellogithub.com/hosts"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBox/1.0"));
    request.setTransferTimeout(10000);
    QNetworkReply *reply = networkManager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, &dialog,
        [&fetchedEntries, reply, applyBtn, previewEdit]() {
            reply->deleteLater();
            bool usedFallback = false;
            if (reply->error() != QNetworkReply::NoError)
            {
                fetchedEntries = defaultGithubHostsEntries();
                usedFallback = true;
            }
            else
            {
                QString body = QString::fromUtf8(reply->readAll());
                fetchedEntries = parseGithubHosts(body);
                if (fetchedEntries.isEmpty())
                {
                    fetchedEntries = defaultGithubHostsEntries();
                    usedFallback = true;
                }
            }
            QString display;
            if (usedFallback)
            {
                display = tr("（在线数据获取失败，使用内置 IP 映射）\n");
            }
            display += fetchedEntries.join("\n");
            previewEdit->setPlainText(display);
            applyBtn->setEnabled(!fetchedEntries.isEmpty());
        });

    QObject::connect(applyBtn, &QPushButton::clicked, &dialog,
        [&dialog, &fetchedEntries, applyBtn, restoreBtn, statusLabel]() {
            if (fetchedEntries.isEmpty()) return;
            QString base = removeBlockBoxHostsSection(readHostsFile());
            if (!base.isEmpty() && !base.endsWith('\n')) base += "\n";
            QString section = kHostsStartMarker + "\n"
                + "# Updated: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "\n"
                + fetchedEntries.join("\n") + "\n"
                + kHostsEndMarker + "\n";
            // 统一为 CRLF（Windows hosts 文件标准换行）
            QString full = (base + section);
            full.replace("\r\n", "\n").replace("\n", "\r\n");

            applyBtn->setEnabled(false);
            restoreBtn->setEnabled(false);
            QString errorMsg;
            if (applyHostsElevated(full, errorMsg))
            {
                statusLabel->setText(tr("当前状态：已启用加速"));
                restoreBtn->setEnabled(true);
                AppMessageBox::information(&dialog, tr("GitHub 加速"),
                    tr("已成功更新 hosts 文件并刷新 DNS 缓存！"));
                dialog.accept();
            }
            else
            {
                applyBtn->setEnabled(true);
                restoreBtn->setEnabled(true);
                AppMessageBox::warning(&dialog, tr("GitHub 加速"),
                    tr("操作失败：%1").arg(errorMsg));
            }
        });

    QObject::connect(restoreBtn, &QPushButton::clicked, &dialog,
        [&dialog, restoreBtn, statusLabel]() {
            QString current = readHostsFile();
            if (!current.contains(kHostsStartMarker))
            {
                AppMessageBox::information(&dialog, tr("GitHub 加速"),
                    tr("hosts 文件中未发现加速条目，无需恢复。"));
                return;
            }
            QString base = removeBlockBoxHostsSection(current);
            if (!base.isEmpty() && !base.endsWith('\n')) base += "\n";
            base.replace("\r\n", "\n").replace("\n", "\r\n");

            restoreBtn->setEnabled(false);
            QString errorMsg;
            if (applyHostsElevated(base, errorMsg))
            {
                statusLabel->setText(tr("当前状态：未启用加速"));
                AppMessageBox::information(&dialog, tr("GitHub 加速"),
                    tr("已移除加速条目并恢复默认 hosts。"));
                dialog.accept();
            }
            else
            {
                restoreBtn->setEnabled(true);
                AppMessageBox::warning(&dialog, tr("GitHub 加速"),
                    tr("操作失败：%1").arg(errorMsg));
            }
        });

    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
#else
    AppMessageBox::information(parentWidget, tr("GitHub 加速"),
        tr("此功能仅在 Windows 系统下可用。"));
#endif
}