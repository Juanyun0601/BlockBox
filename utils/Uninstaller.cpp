/**
 * @file   Uninstaller.cpp
 * @brief  方块盒子自卸载工具实现
 * @author BlockBox Team
 * @date   2026-09-12
 */
#include "Uninstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "components/AppMessageBox.h"
#include "components/UninstallDialog.h"
#include "SettingsManager.h"
#include "SystemTools.h"

namespace {

#if defined(Q_OS_WIN)
/** PowerShell 单引号字符串转义：单引号写成两个单引号 */
QString escapePsString(const QString &value)
{
    QString result = value;
    result.replace('\'', QStringLiteral("''"));
    return result;
}

/** 清理脚本等待主进程退出后所需的额外缓冲（句柄释放） */
const char kCleanupDelayMs[] = "1200";
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_ANDROID)
/** shell 单引号字符串转义（POSIX）：' → '\'' */
QString escapeShellString(const QString &value)
{
    QString result = value;
    result.replace('\'', QStringLiteral("'\\''"));
    return result;
}
#endif

} // namespace

void Uninstaller::runUninstallFlow(QWidget *parent)
{
    UninstallDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted || !dialog.confirmed()) {
        return;
    }
    const bool deleteGameData = dialog.deleteGameData();

    // 先清理由启动器创建、登记在案的系统级残留（脚本只负责它够不到的部分）
    removeDesktopQuickLaunchFiles();
    restoreHostsIfModified();

    QWidget *hostWindow = parent ? parent->window() : nullptr;
    if (hostWindow) {
        hostWindow->hide();
    }

    QString errorMsg;
    if (!startCleanupProcess(deleteGameData, &errorMsg)) {
        // 清理脚本启动失败：恢复界面并提示，不退出
        if (hostWindow) {
            hostWindow->show();
        }
        AppMessageBox::critical(parent,
            QCoreApplication::translate("Uninstaller", "卸载失败"),
            QCoreApplication::translate("Uninstaller", "无法启动卸载清理程序：") + errorMsg);
        return;
    }

    QCoreApplication::quit();
}

void Uninstaller::removeDesktopQuickLaunchFiles()
{
    SettingsManager *settings = SettingsManager::instance();
    const QList<InstanceFolderInfo> folders = settings->getInstanceFolders();
    for (const InstanceFolderInfo &folder : folders) {
        if (folder.path.isEmpty()) {
            continue;
        }
        const QString recorded = settings
            ->getProperty(QStringLiteral("instance/") + folder.path
                          + QStringLiteral("/quickLaunchFile"))
            .toString();
        if (!recorded.isEmpty()) {
            QFile::remove(recorded);
        }
    }
}

void Uninstaller::restoreHostsIfModified()
{
#ifdef Q_OS_WIN
    const QString content = SystemTools::readHostsFile();
    if (content.isEmpty()) {
        return;
    }
    const QString cleaned = SystemTools::removeBlockBoxHostsSection(content);
    if (cleaned == content) {
        return; // 未启用过 GitHub 加速，hosts 无需处理
    }
    // 尽力而为：用户拒绝管理员授权则跳过，不阻塞卸载
    QString ignoredError;
    SystemTools::applyHostsElevated(cleaned, ignoredError);
#else
    // GitHub hosts 加速为 Windows 专属功能
#endif
}

bool Uninstaller::startCleanupProcess(bool deleteGameData, QString *errorMsg)
{
#if defined(Q_OS_WIN)
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString processName =
        QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName();
    const QString scriptPath = QDir::tempPath() + QStringLiteral("/blockbox_uninstall.ps1");
    const QString launcherPath =
        QDir::tempPath() + QStringLiteral("/blockbox_uninstall_launcher.vbs");

    // 清理脚本：等主进程退出 → 注册表 → 公共数据目录 → 临时残留 → 程序目录 → 自删除。
    // 全程 SilentlyContinue：个别文件被占用（如游戏仍在运行）时跳过，不中断整体卸载。
    QString ps;
    ps += QStringLiteral("$ErrorActionPreference = 'SilentlyContinue'\r\n");
    ps += QStringLiteral("Wait-Process -Id %1 -Timeout 20\r\n")
              .arg(QCoreApplication::applicationPid());
    ps += QStringLiteral("Stop-Process -Name '%1' -Force\r\n").arg(escapePsString(processName));
    ps += QStringLiteral("Start-Sleep -Milliseconds %1\r\n").arg(QLatin1String(kCleanupDelayMs));

    // 注册表设置项（QSettings("BlockBox","BlockBox") 的原生存储位置）
    ps += QStringLiteral("Remove-Item -Path 'HKCU:\\Software\\BlockBox' -Recurse -Force\r\n");

    // 系统公共数据目录：Java 运行时（LocalAppData）、清单缓存等（Roaming）
    ps += QStringLiteral(
        "Remove-Item -LiteralPath (Join-Path $env:LOCALAPPDATA 'BlockBox') -Recurse -Force\r\n");
    ps += QStringLiteral(
        "Remove-Item -LiteralPath (Join-Path $env:APPDATA 'BlockBox') -Recurse -Force\r\n");

    // 临时目录中的下载与 hosts 脚本残留（排除本脚本自身）
    ps += QStringLiteral(
        "Get-ChildItem -LiteralPath $env:TEMP | "
        "Where-Object { $_.Name -like 'blockbox_*' -and $_.FullName -ne $PSCommandPath } | "
        "Remove-Item -Recurse -Force\r\n");

    // 程序目录
    ps += QStringLiteral("$appDir = '%1'\r\n").arg(escapePsString(appDir));
    if (deleteGameData) {
        ps += QStringLiteral("Remove-Item -LiteralPath $appDir -Recurse -Force\r\n");
    } else {
        ps += QStringLiteral(
            "Get-ChildItem -LiteralPath $appDir -Force | "
            "Where-Object { $_.Name -ne '.minecraft' -and $_.Name -ne 'BedrockData' } | "
            "Remove-Item -Recurse -Force\r\n");
    }

    // 自删除（PowerShell 已把脚本读入内存，删除自身安全）
    ps += QStringLiteral("Remove-Item -LiteralPath $PSCommandPath -Force\r\n");

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMsg) {
            *errorMsg = scriptFile.errorString();
        }
        return false;
    }
    // 带 BOM 的 UTF-8：Windows PowerShell 5.1 无 BOM 时会按 ANSI 解析中文路径
    scriptFile.write("\xEF\xBB\xBF");
    scriptFile.write(ps.toUtf8());
    scriptFile.close();

    // 通过 wscript 静默拉起 PowerShell（wscript 为 GUI 程序，不闪黑控制台窗）
    const QString vbs = QStringLiteral(
        "Set sh = CreateObject(\"WScript.Shell\")\r\n"
        "sh.Run \"powershell.exe -NoProfile -ExecutionPolicy Bypass "
        "-WindowStyle Hidden -File \"\"%1\"\"\", 0, False\r\n").arg(scriptPath);
    QFile launcherFile(launcherPath);
    if (!launcherFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMsg) {
            *errorMsg = launcherFile.errorString();
        }
        return false;
    }
    // wscript 按 ANSI（系统代码页）读取脚本，路径含中文时须用本地编码
    launcherFile.write(vbs.toLocal8Bit());
    launcherFile.close();

    return QProcess::startDetached(QStringLiteral("wscript.exe"),
                                   QStringList() << launcherPath,
                                   QDir::tempPath());
#elif defined(Q_OS_ANDROID)
    // Android 由系统"应用管理"负责卸载，不会走到这里
    Q_UNUSED(deleteGameData);
    if (errorMsg) {
        *errorMsg = QCoreApplication::translate("Uninstaller", "当前平台不支持应用内卸载");
    }
    return false;
#else
    // POSIX 允许删除运行中的可执行文件：直接由后台 shell 完成全部清理
    const QString appDir = QCoreApplication::applicationDirPath();

    QString command = QStringLiteral("sleep 1");
    // 公共数据目录（清单缓存等，位置随平台不同；重复删除无害）
    const QStringList extraDirs = {
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/BlockBox"),
    };
    for (const QString &dir : extraDirs) {
        if (dir.isEmpty() || dir == appDir || !dir.startsWith(QDir::homePath())) {
            continue; // 只清理家目录内的启动器数据，避免误删
        }
        command += QStringLiteral("; rm -rf '%1'").arg(escapeShellString(dir));
    }
    if (deleteGameData) {
        command += QStringLiteral("; rm -rf '%1'").arg(escapeShellString(appDir));
    } else {
        command += QStringLiteral(
            "; find '%1' -mindepth 1 -maxdepth 1 "
            "! -name '.minecraft' ! -name 'BedrockData' -exec rm -rf {} +")
            .arg(escapeShellString(appDir));
    }

    return QProcess::startDetached(QStringLiteral("/bin/sh"),
                                   QStringList() << QStringLiteral("-c") << command);
#endif
}
