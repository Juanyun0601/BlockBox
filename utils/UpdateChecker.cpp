/**
 * @file   UpdateChecker.cpp
 * @brief  启动器更新检查器实现
 * @author BlockBox Team
 * @date   2026-10-05
 */
#include "utils/UpdateChecker.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QTimer>

#include "components/AppMessageBox.h"
#include "platform.h"
#include "utils/AndroidBridge.h"
#include "utils/GithubAccelerator.h"
#include "utils/HarmonyBridge.h"
#include "utils/SettingsManager.h"
#include "utils/Safety.h"
#include "utils/RuntimeDependencyManager.h"

namespace {

constexpr int kRequestTimeoutMs = 12000;
constexpr int kMaxAttempts = 3; // 直连、用户配置的加速节点、默认镜像
// 直连 api.github.com 受限时的兜底镜像（前缀拼接式，支持 API 透传），
// 优先级低于用户在 GithubAccelerator 中配置的加速节点。
constexpr const char *kApiMirrorPrefix = "https://gh-proxy.com/";
// 应用内更新包的存放目录（相对数据目录）
constexpr const char *kUpdateDirName = "update";

// 第 attempt 次尝试使用的请求地址：
//   0 = 直连；1 = 用户配置的 GitHub 加速节点（未启用则跳过）；
//   2 = 默认镜像兜底
QUrl requestUrlFor(int attempt, const QString &url)
{
    if (attempt == 0)
        return QUrl(url);
    if (attempt == 1) {
        const QString accelerated = GithubAccelerator::instance()->accelerateUrl(url);
        if (!accelerated.isEmpty() && accelerated != url)
            return QUrl(accelerated);
    }
    return QUrl(QStringLiteral("%1%2").arg(QLatin1String(kApiMirrorPrefix), url));
}

// ───────────────────────────── 版本号解析与比较 ─────────────────────────────
//
// 支持的格式：
//   正式版     X.Y.Z       -> {X, Y, Z}，非测试版
//   测试版     X.Y.Z-betaN -> {X, Y, Z}，beta 第 N 期
//   公测遗留   betaN       -> {0, 0, 0}，beta 第 N 期（当前发布的 beta5 即此格式）
// 宽容处理：可带 v/V 前缀；beta 与数字间允许其他字符（如 beta.15）。

struct SemanticVersion
{
    int base[3] = {0, 0, 0};
    bool isBeta = false;
    int betaNumber = 0;
};

QVector<int> extractNumbers(const QString &text)
{
    static const QRegularExpression digits(QStringLiteral("\\d+"));
    QVector<int> numbers;
    auto it = digits.globalMatch(text);
    while (it.hasNext())
        numbers.append(it.next().captured(0).toInt());
    return numbers;
}

SemanticVersion parseVersion(const QString &raw)
{
    SemanticVersion v;
    QString s = raw.trimmed();
    if (s.startsWith(QLatin1Char('v')) || s.startsWith(QLatin1Char('V')))
        s.remove(0, 1);

    // 公测遗留格式：整个字符串以 beta 开头（无 "-"），基础版本视为 0.0.0
    if (s.startsWith(QStringLiteral("beta"), Qt::CaseInsensitive)) {
        const QVector<int> numbers = extractNumbers(s);
        v.isBeta = true;
        v.betaNumber = numbers.isEmpty() ? 0 : numbers.first();
        return v;
    }

    const int dash = s.indexOf(QLatin1Char('-'));
    const QString basePart = dash >= 0 ? s.left(dash) : s;
    const QString suffixPart = dash >= 0 ? s.mid(dash + 1) : QString();

    const QVector<int> baseNumbers = extractNumbers(basePart);
    for (int i = 0; i < 3 && i < baseNumbers.size(); ++i)
        v.base[i] = baseNumbers.at(i);

    if (!suffixPart.isEmpty()) {
        v.isBeta = true;
        const QVector<int> suffixNumbers = extractNumbers(suffixPart);
        v.betaNumber = suffixNumbers.isEmpty() ? 0 : suffixNumbers.first();
    }
    return v;
}

// 比较 a 与 b：返回 <0 表示 a 更旧，>0 表示 a 更新，0 表示相同。
// 同基础版本时正式版新于测试版；同为测试版时比较 beta 期数。
int compareVersions(const SemanticVersion &a, const SemanticVersion &b)
{
    for (int i = 0; i < 3; ++i) {
        if (a.base[i] != b.base[i])
            return a.base[i] < b.base[i] ? -1 : 1;
    }
    if (a.isBeta != b.isBeta)
        return a.isBeta ? -1 : 1;
    if (a.isBeta && a.betaNumber != b.betaNumber)
        return a.betaNumber < b.betaNumber ? -1 : 1;
    return 0;
}

// 当前 Windows 架构对应的资产名片段："win64" / "winarm64"
QString windowsArchAssetTag()
{
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch.contains(QStringLiteral("arm64")) || arch.contains(QStringLiteral("aarch64")))
        return QStringLiteral("winarm64");
    return QStringLiteral("win64");
}

// 当前 Android ABI 对应的资产名片段："arm64-v8a" / "x86_64" / ...
QString androidAbiTag()
{
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch.contains(QStringLiteral("arm64")) || arch.contains(QStringLiteral("aarch64")))
        return QStringLiteral("arm64-v8a");
    if (arch.contains(QStringLiteral("x86_64")))
        return QStringLiteral("x86_64");
    if (arch.contains(QStringLiteral("x86")) || arch.contains(QStringLiteral("i386"))
        || arch.contains(QStringLiteral("i686")))
        return QStringLiteral("x86");
    return QStringLiteral("armeabi-v7a");
}

// 当前 Linux 架构对应的资产名片段："x64" / "arm64"
QString linuxArchTag()
{
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch.contains(QStringLiteral("arm64")) || arch.contains(QStringLiteral("aarch64")))
        return QStringLiteral("arm64");
    return QStringLiteral("x64");
}

// 当前实例是否为"安装版"（对应资产选择与安装方式）：
//   Windows：Inno 安装器的卸载程序 unins000.exe 位于应用目录
//   Linux：应用位于 /opt 或 /usr 下（deb 安装）；否则为解压即用的 tar.gz
//   Android：不区分，始终为 apk
bool isInstalledEdition()
{
#if defined(Q_OS_WIN)
    const QFileInfo appExe(QCoreApplication::applicationFilePath());
    return QFileInfo::exists(appExe.absolutePath() + QStringLiteral("/unins000.exe"));
#elif defined(Q_OS_LINUX)
    const QString appDir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    return appDir.startsWith(QStringLiteral("/opt/"))
        || appDir.startsWith(QStringLiteral("/opt"))
        || appDir.startsWith(QStringLiteral("/usr/"));
#else
    return false;
#endif
}

// 清理 Release 说明文本用于弹窗展示：统一换行（弹窗支持 Markdown 滚动，不再截断）
QString normalizeNotes(const QString &notes)
{
    QString text = notes;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return text.trimmed();
}

// 当前平台对应的资产名片段（与 软件发布/一键打包.bat 的命名保持一致）：
//   BlockBox-<版本>-win64|winarm64-{setup.exe|portable.zip}
//   BlockBox-<版本>-android-{arm64-v8a|x86_64|...}.apk
//   BlockBox-<版本>-linux-{x64|arm64}{.deb|.tar.gz}
//   BlockBox-<版本>-harmony-arm64-v8a.hap（通过 HarmonyBridge 拉起系统安装）
QString platformAssetSuffix()
{
#if defined(Q_OS_WIN)
    const QString arch = windowsArchAssetTag();
    return isInstalledEdition()
        ? QStringLiteral("%1-setup.exe").arg(arch)
        : QStringLiteral("%1-portable.zip").arg(arch);
#elif defined(Q_OS_ANDROID)
    return QStringLiteral("android-%1.apk").arg(androidAbiTag());
#elif defined(Q_OS_HARMONY)
    // 鸿蒙仅产出 arm64-v8a 的 HAP（打包脚本 OHOS_ABI 固定）；
    // .zip 资产是 DevEco 工程，不用于应用内更新
    return QStringLiteral("harmony-arm64-v8a.hap");
#elif defined(Q_OS_LINUX)
    const QString arch = linuxArchTag();
    return isInstalledEdition()
        ? QStringLiteral("linux-%1.deb").arg(arch)
        : QStringLiteral("linux-%1.tar.gz").arg(arch);
#else
    return QString(); // macOS 等：暂无应用内更新资产，仅提供浏览器下载
#endif
}

// 更新包落盘目录：移动端（安卓/鸿蒙）放外部存储共享目录，
// 便于系统安装器、文件管理器与 hdc shell 访问；桌面端放数据目录
QString updateDownloadDir()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/BlockBox/update");
#else
    return Platform::getDataDirectory() + QLatin1String("/") + QLatin1String(kUpdateDirName);
#endif
}

// shell 单引号转义
QString shQuote(QString path)
{
    path.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return path;
}

// 桌面平台安装确认文案（按平台与更新包形态区分）
QString installConfirmText(bool setupPackage = false)
{
#if defined(Q_OS_WIN)
    if (setupPackage)
        return QCoreApplication::translate("UpdateChecker",
            "将关闭方块盒子，静默运行安装程序把新版本装到当前程序目录，"
            "完成后自动重新打开，确定继续吗？");
    return QCoreApplication::translate("UpdateChecker",
        "将关闭方块盒子，用更新包里的文件覆盖当前程序目录，完成后自动重新打开，确定继续吗？");
#elif defined(Q_OS_HARMONY)
    Q_UNUSED(setupPackage)
    return QCoreApplication::translate("UpdateChecker",
        "将尝试通过系统包管理器安装更新；若无权限将提供手动安装命令，确定继续吗？");
#elif defined(Q_OS_LINUX)
    Q_UNUSED(setupPackage)
    if (isInstalledEdition())
        return QCoreApplication::translate("UpdateChecker",
            "将打开系统授权安装更新，完成后自动重新启动方块盒子，确定继续吗？");
    return QCoreApplication::translate("UpdateChecker",
        "将解压更新包到当前应用目录旁并从新版本目录重启，确定继续吗？");
#else
    Q_UNUSED(setupPackage)
    return QCoreApplication::translate("UpdateChecker",
        "将关闭方块盒子并完成更新安装，确定继续吗？");
#endif
}

// 桌面平台（Windows/Linux）按发行形态启动延迟安装流程；
// 返回 false 表示当前平台没有可用的应用内安装方式。

#if defined(Q_OS_WIN)
// ─────────────────────────── Windows 应用内更新 ───────────────────────────
//
// 官方 Windows 资产有两种形态：
//   安装包      BlockBox-<版本>-win64-setup.exe      （Inno Setup，支持 /VERYSILENT）
//   免安装压缩包 BlockBox-<版本>-win64-portable.zip   （顶层还套着一层版本目录）
//
// 旧实现把 zip 直接 Expand-Archive 到程序目录，官方包里那一层版本目录会让文件
// 全部解到 <程序目录>/BlockBox-<版本>-win64-portable/ 下面，原来那份程序一个字节
// 都没被替换，重启后仍是旧版本——这就是「拖入新版本文件却装不上」的直接原因。
// 另外固定 Start-Sleep 2 秒并不能保证旧进程已经退出，安装程序会因文件被占用
// 而静默失败；失败后也无任何提示。
//
// 现在的做法：
//   1. 判定包形态；免安装包先在应用内解压到临时目录，并找到真正放着程序的目录；
//   2. 写一份后台辅助脚本，等旧进程完全退出后再覆盖文件或静默运行安装包；
//   3. 成功后重新打开方块盒子，失败时弹窗说明原因并重新打开原版本。

enum class WinPackageKind { Unknown, Setup, Portable };

WinPackageKind winPackageKind(const QString &packagePath)
{
    const QString lower = QFileInfo(packagePath).fileName().toLower();
    if (lower.endsWith(QStringLiteral(".exe")))
        return WinPackageKind::Setup;
    if (lower.endsWith(QStringLiteral(".zip")))
        return WinPackageKind::Portable;
    return WinPackageKind::Unknown;
}

// 本次更新的暂存目录：解压出来的新版本、辅助脚本与日志都放这里
QString updateStagingDir()
{
    return QDir(QDir::tempPath())
        .filePath(QStringLiteral("BlockBox-update-%1").arg(QCoreApplication::applicationPid()));
}

// 清掉可能残留的暂存目录（上次失败、或这次解压中途出错）
void removeUpdateStagingDir()
{
    QDir(updateStagingDir()).removeRecursively();
}

// 读 PE 头里的机器类型，用来挡住「把 arm64 包拖到 x64 版本上」这类错包
quint16 peMachineType(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray head = file.read(4096);
    if (head.size() < 0x40)
        return 0;
    const uchar *data = reinterpret_cast<const uchar *>(head.constData());
    const quint32 peOffset = quint32(data[0x3C]) | (quint32(data[0x3D]) << 8)
        | (quint32(data[0x3E]) << 16) | (quint32(data[0x3F]) << 24);
    if (peOffset + 6 > quint32(head.size()))
        return 0;
    if (head.at(int(peOffset)) != 'P' || head.at(int(peOffset) + 1) != 'E')
        return 0;
    return quint16(quint8(head.at(int(peOffset) + 4))
                   | (quint16(quint8(head.at(int(peOffset) + 5))) << 8));
}

QString peMachineName(quint16 machine)
{
    switch (machine) {
    case 0x8664: return QStringLiteral("x64");
    case 0xAA64: return QStringLiteral("arm64");
    case 0x014C: return QStringLiteral("x86");
    default:     return QString();
    }
}

// 用 PowerShell -File 运行脚本：参数走 argv，中文与空格路径都不需要手工拼引号
bool runPowerShellScript(const QString &scriptPath, const QStringList &arguments, QString *error)
{
    QProcess process;
    process.setProgram(QStringLiteral("powershell.exe"));
    process.setArguments(QStringList{QStringLiteral("-NoProfile"),
                                     QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                     QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                                     QStringLiteral("-File"), scriptPath}
                         + arguments);
    process.start();
    if (!process.waitForStarted(15000)) {
        *error = QCoreApplication::translate("UpdateChecker",
                                             "无法启动系统解压组件（PowerShell）");
        return false;
    }
    if (!process.waitForFinished(180000)) {
        process.kill();
        process.waitForFinished(3000);
        *error = QCoreApplication::translate("UpdateChecker", "解压更新包超时，请稍后重试");
        return false;
    }
    const QString errText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = errText.isEmpty()
            ? QCoreApplication::translate("UpdateChecker", "解压更新包失败")
            : errText;
        return false;
    }
    return true;
}

// 写 UTF-8 BOM 的脚本文件：Windows PowerShell 5.1 只有带 BOM 才按 UTF-8 解析，否则中文乱码
bool writePowerShellScript(const QString &path, const QString &script, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QCoreApplication::translate("UpdateChecker", "无法写入临时文件：%1").arg(path);
        return false;
    }
    file.write("\xEF\xBB\xBF");
    file.write(script.toUtf8());
    file.close();
    return true;
}

// 在解压结果里找「真正放着主程序」的目录：
// 兼容官方包的一层版本目录，也兼容直接平铺（或多套一层）的包
QString locatePayloadRoot(const QString &rootPath, const QString &exeName)
{
    if (QFileInfo::exists(QDir(rootPath).filePath(exeName)))
        return rootPath;

    const QFileInfoList entries =
        QDir(rootPath).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (QFileInfo::exists(QDir(entry.absoluteFilePath()).filePath(exeName)))
            return entry.absoluteFilePath();
    }
    for (const QFileInfo &entry : entries) {
        const QFileInfoList children =
            QDir(entry.absoluteFilePath())
                .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &child : children) {
            if (QFileInfo::exists(QDir(child.absoluteFilePath()).filePath(exeName)))
                return child.absoluteFilePath();
        }
    }
    return QString();
}

// 解压脚本（独立脚本文件，避免 -Command 里的引号转义问题）
const char *kExtractScript = R"PSEOF(
param([string]$Zip, [string]$Dest)
$ErrorActionPreference = 'Stop'
try {
    if (-not (Test-Path -LiteralPath $Dest)) { New-Item -ItemType Directory -Force -Path $Dest | Out-Null }
    Expand-Archive -LiteralPath $Zip -DestinationPath $Dest -Force -ErrorAction Stop
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
)PSEOF";

// 安装脚本：等旧进程退出 → 覆盖/安装 → 重新启动；任何失败都弹窗并重开原版本
const char *kInstallScript = R"PSEOF(
param(
    [int]$AppPid,
    [string]$AppDir,
    [string]$ExeName,
    [string]$Kind,
    [string]$Package,
    [string]$PayloadDir,
    [string]$StagingDir,
    [string]$LogPath,
    [int]$NeedElevate,
    [int]$Elevated
)
$ErrorActionPreference = 'Stop'

function Write-Note([string]$text) {
    try {
        if ($LogPath) {
            $line = '[' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '] ' + $text
            Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8
        }
    } catch { }
}

function Show-Tip([string]$text, [int]$icon) {
    try {
        $shell = New-Object -ComObject WScript.Shell
        [void]$shell.Popup($text, 0, '方块盒子 更新', $icon)
    } catch { }
}

function Start-BlockBox {
    try {
        $exe = Join-Path $AppDir $ExeName
        if (-not (Test-Path -LiteralPath $exe)) { return $false }
        if ($Elevated -eq 1) {
            # 提权运行时改用资源管理器启动，避免新版本跟着以管理员身份运行
            Start-Process -FilePath 'explorer.exe' -ArgumentList ('"' + $exe + '"')
        } else {
            Start-Process -FilePath $exe -WorkingDirectory $AppDir
        }
        return $true
    } catch {
        Write-Note ('启动方块盒子失败：' + $_.Exception.Message)
        return $false
    }
}

function Stop-WithError([string]$text) {
    Write-Note ('失败：' + $text)
    $message = '方块盒子未能完成更新。' + "`r`n`r`n" + $text
    if ($LogPath) { $message = $message + "`r`n`r`n" + '详细日志：' + $LogPath }
    Show-Tip $message 16
    [void](Start-BlockBox)
    exit 1
}

Write-Note ('开始更新：Kind=' + $Kind + '；Package=' + $Package + '；AppDir=' + $AppDir)

# 0) 程序目录不可写（例如装在 Program Files）时，申请一次管理员权限继续
if (($NeedElevate -eq 1) -and ($Elevated -eq 0)) {
    Write-Note '程序目录需要管理员权限，尝试提权重试'
    $argumentLine = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden',
        '-File', ('"' + $PSCommandPath + '"'),
        '-AppPid', $AppPid,
        '-AppDir', ('"' + $AppDir + '"'),
        '-ExeName', ('"' + $ExeName + '"'),
        '-Kind', $Kind,
        '-Package', ('"' + $Package + '"'),
        '-PayloadDir', ('"' + $PayloadDir + '"'),
        '-StagingDir', ('"' + $StagingDir + '"'),
        '-LogPath', ('"' + $LogPath + '"'),
        '-NeedElevate', '0',
        '-Elevated', '1'
    ) -join ' '
    try {
        Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argumentLine
        exit 0
    } catch {
        Stop-WithError '写入程序目录需要管理员权限，但未能获得授权。'
    }
}

# 1) 等旧进程真正退出：固定等待会抢在退出前写文件，安装程序只能失败
$deadline = (Get-Date).AddSeconds(120)
while ($AppPid -gt 0 -and (Get-Date) -lt $deadline) {
    if (-not (Get-Process -Id $AppPid -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Milliseconds 200
}
Start-Sleep -Milliseconds 600
if ($AppPid -gt 0 -and (Get-Process -Id $AppPid -ErrorAction SilentlyContinue)) {
    Stop-WithError '旧版本仍在运行，无法覆盖程序文件。'
}

$dataDir = Join-Path $AppDir 'BlockBox'
$dataBackup = Join-Path $StagingDir 'data-backup'

# 2) 安装
if ($Kind -eq 'setup') {
    # 安装包里带了一份出厂数据目录，先备份用户数据，装完再放回，
    # 避免账户、设置、收藏这些真正的用户数据被更新包里的默认值覆盖
    if (Test-Path -LiteralPath $dataDir) {
        Write-Note '备份用户数据目录'
        robocopy $dataDir $dataBackup /E /R:3 /W:1 /NFL /NDL /NJH /NJS | Out-Null
    }
    $setupLog = Join-Path $StagingDir 'setup.log'
    $setupArgs = @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/CLOSEAPPLICATIONS',
        ('/DIR="' + $AppDir + '"'),
        ('/LOG="' + $setupLog + '"')
    )
    Write-Note ('运行安装程序：' + $Package)
    $code = -1
    try {
        $proc = Start-Process -FilePath $Package -ArgumentList $setupArgs -Wait -PassThru
        $code = $proc.ExitCode
    } catch {
        Stop-WithError ('无法启动安装程序：' + $_.Exception.Message)
    }
    Write-Note ('安装程序退出码：' + $code)
    if ($code -ne 0) {
        Stop-WithError ('安装程序未能完成安装（退出码 ' + $code + '）。')
    }
    if (Test-Path -LiteralPath $dataBackup) {
        Write-Note '还原用户数据目录'
        robocopy $dataBackup $dataDir /E /IS /R:3 /W:1 /NFL /NDL /NJH /NJS | Out-Null
    }
} else {
    if (($PayloadDir -eq '') -or -not (Test-Path -LiteralPath $PayloadDir)) {
        Stop-WithError '更新包里没有找到可用的程序文件。'
    }
    Write-Note '把新版本文件覆盖到程序目录'
    # 跳过用户数据目录：更新只替换程序文件，不碰账户、设置与游戏数据
    robocopy $PayloadDir $AppDir /E /IS /IT /R:10 /W:1 /NFL /NDL /NJH /NJS /XD 'BlockBox' '.minecraft' 'BedrockData' | Out-Null
    $code = $LASTEXITCODE
    Write-Note ('复制结果代码：' + $code)
    if ($code -ge 8) {
        Stop-WithError ('复制更新文件失败（错误码 ' + $code + '），可能有程序正在占用文件。')
    }
}

# 3) 启动新版本
Write-Note '更新完成，重新打开方块盒子'
if (-not (Start-BlockBox)) {
    Show-Tip '更新已经完成，但没能自动打开方块盒子，请手动启动。' 48
    exit 1
}

# 4) 交给独立进程延迟清理暂存目录（脚本自身还在里面，不能直接删）
try {
    $cleanup = '/c ping -n 3 127.0.0.1 >nul & rmdir /s /q "' + $StagingDir + '"'
    Start-Process -FilePath 'cmd.exe' -ArgumentList $cleanup -WindowStyle Hidden
} catch { }
exit 0
)PSEOF";

/**
 * @brief 准备 Windows 更新：识别包形态，免安装包先在应用内解压并定位程序目录
 * @param packagePath 更新包绝对路径
 * @param[out] isSetup 是否为 setup.exe 安装包
 * @param[out] payloadDir 免安装包解压后真正的程序目录（安装包为空）
 * @param[out] error 失败原因（面向用户的说明）
 */
bool prepareWindowsUpdate(const QString &packagePath, bool *isSetup, QString *payloadDir,
                          QString *error)
{
    const WinPackageKind kind = winPackageKind(packagePath);
    if (kind == WinPackageKind::Unknown) {
        *error = QCoreApplication::translate("UpdateChecker",
                                             "无法识别这个文件的类型，需要 setup.exe 或 portable.zip");
        return false;
    }

    *isSetup = kind == WinPackageKind::Setup;
    payloadDir->clear();
    if (*isSetup)
        return true; // 安装包交给安装程序自己解压

    const QString stagingDir = updateStagingDir();
    const QString extractDir = QDir(stagingDir).filePath(QStringLiteral("payload"));
    removeUpdateStagingDir(); // 上一次失败可能留下半截解压结果
    if (!QDir().mkpath(extractDir)) {
        *error = QCoreApplication::translate("UpdateChecker",
                                             "无法创建更新临时目录：%1").arg(stagingDir);
        return false;
    }
    const QString extractScript = QDir(stagingDir).filePath(QStringLiteral("extract.ps1"));
    if (!writePowerShellScript(extractScript, QString::fromLatin1(kExtractScript), error)) {
        removeUpdateStagingDir();
        return false;
    }
    if (!runPowerShellScript(extractScript,
                             {QStringLiteral("-Zip"), packagePath,
                              QStringLiteral("-Dest"), extractDir},
                             error)) {
        removeUpdateStagingDir();
        return false;
    }

    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const QString root = locatePayloadRoot(extractDir, exeName);
    if (root.isEmpty()) {
        *error = QCoreApplication::translate("UpdateChecker",
                                             "更新包里没有找到 %1，可能不是完整的更新包")
                     .arg(exeName);
        removeUpdateStagingDir();
        return false;
    }

    // 防止把另一个架构的包装上来（例如把 winarm64 包拖进 x64 版本）
    const QString newExe = QDir(root).filePath(exeName);
    if (QFileInfo(newExe).size() <= 0) {
        *error = QCoreApplication::translate("UpdateChecker", "更新包里的程序文件是空的，已停止安装");
        removeUpdateStagingDir();
        return false;
    }
    const quint16 currentMachine = peMachineType(QCoreApplication::applicationFilePath());
    const quint16 incomingMachine = peMachineType(newExe);
    if (currentMachine != 0 && incomingMachine != 0 && currentMachine != incomingMachine) {
        *error = QCoreApplication::translate("UpdateChecker",
                                             "这个更新包是给 %1 用的，当前版本是 %2，不能直接安装")
                     .arg(peMachineName(incomingMachine), peMachineName(currentMachine));
        removeUpdateStagingDir();
        return false;
    }

    *payloadDir = root;
    return true;
}

/**
 * @brief 启动后台辅助脚本完成安装
 *
 * 脚本会等当前进程退出后再动手，所以调用方在返回 true 后即可退出应用。
 */
bool launchWindowsUpdate(const QString &packagePath, bool isSetup, const QString &payloadDir,
                         QString *error)
{
    const QFileInfo appExe(QCoreApplication::applicationFilePath());
    const QString appDir = appExe.absolutePath();
    const QString stagingDir = updateStagingDir();
    if (!QDir().mkpath(stagingDir)) {
        *error = QCoreApplication::translate("UpdateChecker",
                                             "无法创建更新临时目录：%1").arg(stagingDir);
        return false;
    }

    const QString scriptPath = QDir(stagingDir).filePath(QStringLiteral("install.ps1"));
    if (!writePowerShellScript(scriptPath, QString::fromLatin1(kInstallScript), error)) {
        removeUpdateStagingDir();
        return false;
    }

    // 程序目录不可写（装在 Program Files 这类位置）时，辅助脚本会申请一次管理员权限
    bool needElevate = false;
    {
        QTemporaryFile probe(QDir(appDir).filePath(QStringLiteral("bb_update_probe_XXXXXX")));
        needElevate = !probe.open();
    }

    const QString logPath = QDir(stagingDir).filePath(QStringLiteral("update.log"));
    const QStringList arguments{
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
        QStringLiteral("-File"), scriptPath,
        QStringLiteral("-AppPid"), QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("-AppDir"), appDir,
        QStringLiteral("-ExeName"), appExe.fileName(),
        QStringLiteral("-Kind"), isSetup ? QStringLiteral("setup") : QStringLiteral("portable"),
        QStringLiteral("-Package"), packagePath,
        QStringLiteral("-PayloadDir"), payloadDir,
        QStringLiteral("-StagingDir"), stagingDir,
        QStringLiteral("-LogPath"), logPath,
        QStringLiteral("-NeedElevate"), needElevate ? QStringLiteral("1") : QStringLiteral("0"),
        QStringLiteral("-Elevated"), QStringLiteral("0"),
    };
    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), arguments)) {
        *error = QCoreApplication::translate("UpdateChecker", "无法启动更新辅助程序，请手动安装");
        removeUpdateStagingDir();
        return false;
    }
    return true;
}
#endif // Q_OS_WIN

#if defined(Q_OS_LINUX)
// 等当前进程退出后再安装：固定 sleep 会抢在退出前写文件
QString waitForSelfExitScript()
{
    return QStringLiteral("while kill -0 %1 2>/dev/null; do sleep 0.2; done; sleep 0.5;")
        .arg(QCoreApplication::applicationPid());
}

bool launchLinuxUpdateInstaller(const QString &packagePath)
{
    const QFileInfo appExe(QCoreApplication::applicationFilePath());
    const QString appDir = appExe.absolutePath();
    const QString wait = waitForSelfExitScript();

    if (isInstalledEdition()) {
        // deb：polkit 授权后 dpkg 安装，成功即重启应用
        const QString cmd = QStringLiteral("%1 pkexec dpkg -i '%2' && exec '%3'")
            .arg(wait, shQuote(packagePath), shQuote(appExe.absoluteFilePath()));
        QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), cmd});
        return true;
    }

    // tar.gz：包内为带顶层版本目录的目录树；顶层目录名从包里取，不靠文件名猜
    const QString parentDir = QFileInfo(appDir).absolutePath();
    const QString script = QStringLiteral(
        "%1 TOP=$(tar -tzf '%2' 2>/dev/null | head -1 | cut -d/ -f1); "
        "tar -xzf '%2' -C '%3' && exec '%3'/$TOP/'%4'")
        .arg(wait, shQuote(packagePath), shQuote(parentDir), shQuote(appExe.fileName()));
    QProcess::startDetached(QStringLiteral("sh"),
                            {QStringLiteral("-c"), QStringLiteral("sh -c ") + shQuote(script)});
    return true;
}
#endif // Q_OS_LINUX

} // namespace

UpdateChecker *UpdateChecker::instance()
{
    static UpdateChecker s_instance;
    return &s_instance;
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // 鸿蒙：系统包管理器安装结果回调
    connect(HarmonyBridge::instance(), &HarmonyBridge::installFinished,
            this, &UpdateChecker::onHarmonyInstallFinished);
}

void UpdateChecker::checkForUpdates(QWidget *parent, bool silent)
{
    if (m_checking)
        return;
    m_parent = parent;
    m_silent = silent;
    m_checking = true;
    fetchLatest();
}

void UpdateChecker::fetchLatest()
{
    m_attempt = 0;
    issueRequest();
}

void UpdateChecker::issueRequest()
{
    QNetworkRequest req(requestUrlFor(m_attempt, AppVersion::releaseApiUrl()));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(kRequestTimeoutMs);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

UpdateChecker::ReleaseCandidate
UpdateChecker::pickReleaseForChannel(const QJsonArray &releases) const
{
    // 抢先升级（默认）看测试版+正式版；保守升级只看正式版
    const QString channel = SettingsManager::instance()
                                ->getProperty(QStringLiteral("update_channel"),
                                              QStringLiteral("beta"))
                                .toString();
    const bool stableOnly = channel == QLatin1String("stable");

    // 列表按发布时间新→旧排列，取第一个属于当前通道的版本
    for (const QJsonValue &value : releases) {
        const QJsonObject obj = value.toObject();
        if (obj.value(QStringLiteral("draft")).toBool())
            continue;

        const QString tag = obj.value(QStringLiteral("tag_name")).toString().trimmed();
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();

        // tag 完全不含数字时（如公测期的纯 "beta"）解析不出版本，回退用 Release 名称
        static const QRegularExpression anyDigit(QStringLiteral("\\d"));
        SemanticVersion parsed = parseVersion(tag);
        QString version = tag;
        if (!tag.contains(anyDigit) && !name.isEmpty()) {
            version = name;
            parsed = parseVersion(name);
        }

        ReleaseCandidate candidate;
        candidate.version = version;
        candidate.releaseName = name;
        candidate.notes = obj.value(QStringLiteral("body")).toString();
        candidate.pageUrl = QUrl(obj.value(QStringLiteral("html_url")).toString());
        candidate.isBeta = parsed.isBeta
            || obj.value(QStringLiteral("prerelease")).toBool();

        if (stableOnly && candidate.isBeta)
            continue; // 保守升级跳过测试版，继续找更新的正式版

        pickAssetForPlatform(candidate, obj.value(QStringLiteral("assets")).toArray());
        return candidate;
    }
    return ReleaseCandidate{};
}

void UpdateChecker::pickAssetForPlatform(ReleaseCandidate &candidate,
                                         const QJsonArray &assets) const
{
    const QString suffix = platformAssetSuffix();
    if (assets.isEmpty() || suffix.isEmpty())
        return; // 当前平台无应用内更新资产，仅提供浏览器下载

    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString assetName = asset.value(QStringLiteral("name")).toString();
        if (!assetName.endsWith(suffix, Qt::CaseInsensitive))
            continue;
        candidate.assetName = assetName;
        candidate.assetUrl = asset.value(QStringLiteral("browser_download_url")).toString();
        // GitHub 会给出 "sha256:<hex>" 形式的资产指纹，用于校验下载到的更新包
        candidate.assetDigest = asset.value(QStringLiteral("digest")).toString();
        break;
    }
}

void UpdateChecker::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();

    QString errorText;
    ReleaseCandidate chosen;

    if (reply->error() != QNetworkReply::NoError) {
        errorText = reply->errorString();
    } else {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isArray())
            chosen = pickReleaseForChannel(doc.array());
        else
            errorText = tr("无法解析版本信息");
    }

    // 请求失败时按 直连 -> 加速节点 -> 默认镜像 依次重试
    const bool failed = !errorText.isEmpty();
    if (failed && m_attempt < kMaxAttempts - 1) {
        ++m_attempt;
        issueRequest();
        return;
    }

    const bool hasUpdate = !failed && chosen.isValid()
        && compareVersions(parseVersion(AppVersion::current()),
                           parseVersion(chosen.version)) < 0;

    m_checking = false;
    if (chosen.isValid())
        m_lastCandidate = chosen;
    emit checkFinished(hasUpdate, chosen.version);

    // 父窗口已销毁（应用正在退出）时不再弹窗
    if (!m_parent)
        return;

    // 移动端（安卓/鸿蒙）：启动器更新与运行依赖更新合并到同一个窗口
    if (RuntimeDependencyManager::isSupportedPlatform()) {
        beginCombinedUpdateFlow(hasUpdate, failed, errorText);
        return;
    }

    if (hasUpdate) {
        showUpdateDialog(chosen);
        return;
    }

    if (m_silent)
        return; // 静默模式：已是最新或检查失败均不打扰

    if (failed) {
        AppMessageBox::warning(m_parent, tr("检查更新"),
                               tr("检查更新失败，请检查网络后重试。\n\n%1").arg(errorText));
    } else {
        AppMessageBox::information(m_parent, tr("检查更新"),
                                   tr("当前已是最新版本（%1）。").arg(AppVersion::current()));
    }
}

void UpdateChecker::showUpdateDialog(const ReleaseCandidate &release)
{
    QUrl url = release.pageUrl;
    if (!url.isValid() || url.isEmpty())
        url = QUrl(AppVersion::releasePageUrl());

    const QString notes = normalizeNotes(release.notes);
    const QString versionLabel = release.isBeta
        ? tr("%1（测试版）").arg(release.version)
        : release.version;
    QString text = tr("发现新版本 %1，当前版本为 %2，是否前往下载？")
                       .arg(versionLabel, AppVersion::current());
    if (!release.releaseName.isEmpty() && release.releaseName != release.version)
        text += QStringLiteral("\n\n[%1]").arg(release.releaseName);

    AppMessageBox box(m_parent);
    box.setIcon(AppMessageBox::Information);
    box.setWindowTitle(tr("发现新版本"));
    box.setText(text);
    if (!notes.isEmpty()) {
        // 更新日志按 Markdown 渲染，过长时可在弹窗内上下滚动查看全文
        box.setInformativeMarkdown(QStringLiteral("## %1\n\n%2")
                                       .arg(tr("更新说明"), notes));
    }

    // 应用内更新可用时提供"更新"按钮：后台下载，进度显示在侧边栏圆形按钮上
    QPushButton *updateBtn = nullptr;
    if (!release.assetUrl.isEmpty()) {
        const bool alreadyDownloaded = m_downloadState == DownloadCompleted
            && QFile::exists(m_downloadPath);
        updateBtn = box.addButton(alreadyDownloaded ? tr("重启安装") : tr("更新"),
                                  AppMessageBox::AcceptRole);
    }
    QPushButton *downloadPageBtn = box.addButton(tr("前往浏览器下载"), AppMessageBox::ActionRole);
    box.addButton(tr("以后再说"), AppMessageBox::RejectRole);
    box.exec();

    if (updateBtn && box.clickedButton() == updateBtn) {
        if (m_downloadState == DownloadCompleted && QFile::exists(m_downloadPath))
            installDownloadedUpdate();
        else
            startDownload();
    } else if (box.clickedButton() == downloadPageBtn) {
        QDesktopServices::openUrl(url);
    }
}

// ───────────────────────────── 移动端合并更新窗口 ─────────────────────────────

void UpdateChecker::beginCombinedUpdateFlow(bool launcherHasUpdate, bool checkFailed,
                                            const QString &errorText)
{
    m_launcherHasUpdate = launcherHasUpdate;
    m_launcherCheckFailed = checkFailed;
    m_launcherErrorText = errorText;
    m_combinedFlowActive = true;
    m_depsCheckPending = true;

    RuntimeDependencyManager *deps = RuntimeDependencyManager::instance();
    connect(deps, &RuntimeDependencyManager::checkFinished, this,
            &UpdateChecker::onRuntimeDependenciesChecked, Qt::UniqueConnection);
    connect(deps, &RuntimeDependencyManager::allInstallsFinished, this,
            &UpdateChecker::onRuntimeDependenciesInstalled, Qt::UniqueConnection);

    // 依赖检查始终静默：失败时在合并窗口里如实说明，不再单独弹一次
    deps->checkForUpdates(m_parent, true);
}

void UpdateChecker::onRuntimeDependenciesChecked(bool success, const QString &error)
{
    Q_UNUSED(success)
    // 只处理本次合并流程触发的检查结果
    if (!m_depsCheckPending)
        return;
    m_depsCheckPending = false;

    if (!m_combinedFlowActive || !m_parent)
        return;

    showCombinedUpdateDialog(error);
}

void UpdateChecker::onRuntimeDependenciesInstalled(bool allSucceeded)
{
    Q_UNUSED(allSucceeded)
    // 依赖装完后提醒用户启动器本身还有更新
    if (!m_parent || !m_launcherHasUpdate || !m_lastCandidate.isValid())
        return;
    if (m_downloadState == DownloadRunning || m_downloadState == DownloadCompleted)
        return;
    showUpdateDialog(m_lastCandidate);
}

void UpdateChecker::showCombinedUpdateDialog(const QString &launcherError)
{
    RuntimeDependencyManager *deps = RuntimeDependencyManager::instance();
    const QList<RuntimeDependency> pending = deps->pendingDependencies();

    const bool hasLauncher = m_launcherHasUpdate && m_lastCandidate.isValid();
    const bool hasDeps = !pending.isEmpty();

    if (!hasLauncher && !hasDeps) {
        m_combinedFlowActive = false;
        if (m_silent)
            return;
        if (m_launcherCheckFailed) {
            AppMessageBox::warning(m_parent, tr("检查更新"),
                                   tr("检查更新失败，请检查网络后重试。\n\n%1").arg(launcherError));
        } else {
            AppMessageBox::information(m_parent, tr("检查更新"),
                                       tr("当前已是最新版本（%1），运行依赖也是最新的。")
                                           .arg(AppVersion::current()));
        }
        return;
    }

    QStringList lines;
    if (hasLauncher) {
        const QString versionLabel = m_lastCandidate.isBeta
            ? tr("%1（测试版）").arg(m_lastCandidate.version)
            : m_lastCandidate.version;
        lines << tr("启动器有新版本 %1，当前版本为 %2。").arg(versionLabel, AppVersion::current());
    }
    if (hasDeps)
        lines << tr("有 %1 项运行依赖需要安装或更新。").arg(pending.size());
    if (!hasLauncher && m_launcherCheckFailed && !launcherError.isEmpty())
        lines << tr("启动器检查更新失败：%1").arg(launcherError);

    QString markdown;
    if (hasLauncher) {
        const QString notes = normalizeNotes(m_lastCandidate.notes);
        if (!notes.isEmpty()) {
            markdown += QStringLiteral("## %1\n\n%2\n\n")
                            .arg(tr("启动器更新说明"), notes);
        }
    }
    if (hasDeps) {
        markdown += QStringLiteral("## %1\n\n").arg(tr("运行依赖"));
        for (const RuntimeDependency &dependency : pending) {
            QString line = QStringLiteral("- **%1** %2").arg(dependency.name, dependency.version);
            if (!dependency.installed)
                line += tr("（未安装）");
            else if (dependency.needsUpdate())
                line += tr("（已安装 %1，可更新）").arg(dependency.installedVersion);
            else
                line += tr("（已安装）");
            const QString size = dependency.downloadSizeText();
            if (!size.isEmpty())
                line += tr("，%1").arg(size);
            if (!dependency.description.isEmpty())
                line += QStringLiteral("\n  ") + dependency.description;
            markdown += line + QLatin1Char('\n');
        }
    }

    AppMessageBox box(m_parent);
    box.setIcon(AppMessageBox::Information);
    box.setWindowTitle(hasLauncher && hasDeps ? tr("发现更新")
                                              : (hasLauncher ? tr("发现新版本") : tr("运行依赖更新")));
    box.setText(lines.join(QLatin1Char('\n')));
    if (!markdown.isEmpty())
        box.setInformativeMarkdown(markdown);

    QPushButton *launcherBtn = nullptr;
    if (hasLauncher) {
        if (!m_lastCandidate.assetUrl.isEmpty()) {
            const bool alreadyDownloaded = m_downloadState == DownloadCompleted
                && QFile::exists(m_downloadPath);
            launcherBtn = box.addButton(alreadyDownloaded ? tr("重启安装启动器")
                                                         : tr("下载启动器更新"),
                                        AppMessageBox::AcceptRole);
        } else {
            launcherBtn = box.addButton(tr("前往浏览器下载"), AppMessageBox::ActionRole);
        }
    }
    QPushButton *depsBtn = hasDeps ? box.addButton(tr("一键安装运行依赖"), AppMessageBox::ActionRole)
                                   : nullptr;
    box.addButton(tr("以后再说"), AppMessageBox::RejectRole);
    box.exec();

    m_combinedFlowActive = false;
    QPushButton *clicked = box.clickedButton();

    if (launcherBtn && clicked == launcherBtn) {
        if (m_lastCandidate.assetUrl.isEmpty()) {
            QUrl url = m_lastCandidate.pageUrl;
            if (!url.isValid() || url.isEmpty())
                url = QUrl(AppVersion::releasePageUrl());
            QDesktopServices::openUrl(url);
        } else if (m_downloadState == DownloadCompleted && QFile::exists(m_downloadPath)) {
            installDownloadedUpdate();
        } else {
            startDownload();
        }
        return;
    }

    if (depsBtn && clicked == depsBtn) {
        deps->installAll();
        return;
    }
}

// ───────────────────────────── 应用内下载与安装 ─────────────────────────────

void UpdateChecker::startDownload()
{
    if (m_downloadState == DownloadRunning)
        return;
    if (!m_lastCandidate.isValid() || m_lastCandidate.assetUrl.isEmpty())
        return;

    QDir updateDir(updateDownloadDir());
    if (!updateDir.exists())
        QDir().mkpath(updateDir.absolutePath());

    // 资产名来自远程 JSON，先清洗成安全的单层文件名，避免 "../" 写到更新目录之外
    const QString safeAssetName = Safety::safeFileName(m_lastCandidate.assetName,
                                                       QStringLiteral("BlockBox-update"));
    m_downloadPath = QDir::cleanPath(updateDir.filePath(safeAssetName));
    m_downloadAttempt = 0;
    m_downloadState = DownloadRunning;
    emit downloadStarted();
    beginDownloadAttempt();
}

void UpdateChecker::beginDownloadAttempt()
{
    // 每次尝试都重新建文件（重试会清掉上次的半截文件）
    if (m_downloadFile)
        m_downloadFile->deleteLater();
    m_downloadFile = new QFile(m_downloadPath, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        resetDownloadState(DownloadFailed);
        emit downloadFailed(tr("无法创建更新包文件：%1").arg(m_downloadPath));
        return;
    }
    m_received = 0;
    issueDownloadRequest();
}

void UpdateChecker::issueDownloadRequest()
{
    QNetworkRequest req(requestUrlFor(m_downloadAttempt, m_lastCandidate.assetUrl));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(0); // 大文件下载不设超时
    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        m_received = received;
        // 进度信号限流：每 100ms 才向 UI 广播一次，避免高刷下载刷爆重绘
        if (m_progressThrottle.isValid() && m_progressThrottle.elapsed() < 100
            && (total <= 0 || received < total))
            return;
        m_progressThrottle.restart();
        emit downloadProgress(received, total);
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (m_downloadFile)
            m_downloadFile->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleDownloadReply(reply);
    });

    m_progressThrottle.start();
}

namespace {

/**
 * 校验文件 SHA-256 是否与给定指纹一致。
 * @param path        文件路径
 * @param digestText  形如 "sha256:<hex>" 或纯 16 进制串
 */
bool verifyFileDigest(const QString &path, const QString &digestText)
{
    QString expected = digestText.trimmed();
    const int colon = expected.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        expected = expected.mid(colon + 1).trimmed();
    if (expected.isEmpty())
        return true;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return false;

    return hash.result().toHex().compare(expected.toLatin1(), Qt::CaseInsensitive) == 0;
}

} // namespace

void UpdateChecker::handleDownloadReply(QNetworkReply *reply)
{
    reply->deleteLater();

    QFile *file = m_downloadFile;
    m_downloadFile = nullptr;
    bool ok = reply->error() == QNetworkReply::NoError;
    const qint64 total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    QString failureReason = reply->errorString();

    // 成功判定：有数据写入，且（内容长度未知或与已收字节数一致）
    if (ok && file && m_received > 0 && (total <= 0 || m_received == total)) {
        file->close();
        // 指纹校验：发布信息带 digest 时必须一致。
        // 更新包可能经第三方加速地址/镜像下载，不校验就等于信任中间人。
        if (m_lastCandidate.assetDigest.isEmpty()
            || verifyFileDigest(m_downloadPath, m_lastCandidate.assetDigest)) {
            file->deleteLater();
            m_downloadState = DownloadCompleted;
            emit downloadProgress(m_received, m_received);
            emit downloadFinished();
            return;
        }
        ok = false;
        failureReason = tr("更新包校验失败，可能已被篡改，已阻止安装");
        file->remove();
    }

    // 下载失败：删除半截文件，按 直连 -> 加速节点 -> 镜像 重试
    const QString error = failureReason;
    if (file) {
        file->close();
        file->remove();
        file->deleteLater();
    }

    if (m_downloadAttempt < kMaxAttempts - 1) {
        ++m_downloadAttempt;
        beginDownloadAttempt();
        return;
    }

    resetDownloadState(DownloadFailed);
    emit downloadFailed(error);
}

void UpdateChecker::onHarmonyInstallFinished(bool success, const QString &error)
{
    if (success) {
        if (m_parent)
            AppMessageBox::information(m_parent, tr("安装更新"),
                                       tr("更新包已提交系统安装，安装完成后请重新打开方块盒子。"));
        return;
    }

    // 安装失败（典型原因：未授予 INSTALL_BUNDLE 权限）→ 提供 hdc 命令兜底
    showHdcInstallFallbackDialog(error, m_downloadPath);
}

void UpdateChecker::showHdcInstallFallbackDialog(const QString &error,
                                                 const QString &devicePath)
{
    if (!m_parent)
        return;

    const QString command = QStringLiteral("hdc shell bm install -p %1").arg(devicePath);
    AppMessageBox box(m_parent);
    box.setIcon(AppMessageBox::Warning);
    box.setWindowTitle(tr("安装更新"));
    box.setText(tr("通过系统包管理器安装失败：\n%1\n\n"
                   "更新包已下载到设备：\n%2\n\n"
                   "设备连接电脑后可执行以下命令安装：")
                    .arg(error, devicePath));
    box.setInformativeText(command);

    QPushButton *copyBtn = box.addButton(tr("复制安装命令"), AppMessageBox::AcceptRole);
    box.addButton(tr("确定"), AppMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == copyBtn)
        QGuiApplication::clipboard()->setText(command);
}

void UpdateChecker::resetDownloadState(DownloadState state)
{
    if (m_downloadFile) {
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
    }
    m_downloadState = state;
}

void UpdateChecker::installDownloadedUpdate()
{
    if (m_downloadState != DownloadCompleted || !QFile::exists(m_downloadPath)) {
        if (m_parent)
            AppMessageBox::warning(m_parent, tr("安装更新"),
                                   tr("更新包不存在或未下载完成，请重新检查更新。"));
        return;
    }
    installPackageAtPath(m_parent, m_downloadPath);
}

bool UpdateChecker::isUpdatePackageFile(const QString &filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile())
        return false;

    // 官方包名均为 BlockBox-<版本>-<平台>-…，要求含 blockbox 降低误装任意安装包的风险
    const QString lower = fi.fileName().toLower();
    if (!lower.contains(QStringLiteral("blockbox")))
        return false;

#if defined(Q_OS_WIN)
    // 桌面端只区分「安装包」和「免安装压缩包」，具体是哪种交给安装流程判断。
    // 不再要求文件名以 -setup.exe / -portable.zip 结尾：浏览器重复下载会加
    // " (1)" 后缀，改名后以前会被当成无关文件直接忽略，拖进来毫无反应。
    return lower.endsWith(QStringLiteral(".exe"))
        || lower.endsWith(QStringLiteral(".zip"));
#elif defined(Q_OS_ANDROID)
    return lower.endsWith(QStringLiteral(".apk"));
#elif defined(Q_OS_HARMONY)
    return lower.endsWith(QStringLiteral(".hap"));
#elif defined(Q_OS_LINUX)
    return lower.endsWith(QStringLiteral(".deb"))
        || lower.endsWith(QStringLiteral(".tar.gz"));
#else
    return false;
#endif
}

void UpdateChecker::installExternalPackage(QWidget *parent, const QString &filePath)
{
    if (!isUpdatePackageFile(filePath)) {
        if (parent)
            AppMessageBox::warning(parent, tr("安装更新"),
                                   tr("无法识别为方块盒子更新包：\n%1")
                                       .arg(QDir::toNativeSeparators(filePath)));
        return;
    }
    if (m_downloadState == DownloadRunning) {
        if (parent)
            AppMessageBox::information(parent, tr("安装更新"),
                                       tr("正在下载更新，请等待下载完成后再试。"));
        return;
    }
    installPackageAtPath(parent, QFileInfo(filePath).absoluteFilePath());
}

void UpdateChecker::installPackageAtPath(QWidget *parent, const QString &packagePath)
{
    if (!parent)
        parent = m_parent ? static_cast<QWidget *>(m_parent)
                          : QApplication::activeWindow();
    if (!QFile::exists(packagePath)) {
        if (parent)
            AppMessageBox::warning(parent, tr("安装更新"),
                                   tr("更新包不存在或未下载完成，请重新检查更新。"));
        return;
    }

#if defined(Q_OS_ANDROID)
    // Android：确认后拉起系统安装器（不退出应用，由系统接管覆盖安装）
    if (parent) {
        const auto choice = AppMessageBox::question(
            parent, tr("安装更新"), tr("将打开系统安装器完成更新安装，确定继续吗？"),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (choice != AppMessageBox::Yes)
            return;
    }
    if (!AndroidBridge::installApk(packagePath) && parent) {
        AppMessageBox::warning(parent, tr("安装更新"),
                               tr("无法打开系统安装器，请前往浏览器手动下载安装。"));
    }
#elif defined(Q_OS_HARMONY)
    // 鸿蒙：确认后尝试通过系统包管理器安装 HAP（需 INSTALL_BUNDLE 权限），
    // 结果异步返回；无权限时由 onHarmonyInstallFinished 提供 hdc 命令兜底
    if (parent) {
        const auto choice = AppMessageBox::question(
            parent, tr("安装更新"), installConfirmText(),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (choice != AppMessageBox::Yes)
            return;
    }
    // 外部拖入的包不在下载目录，安装失败兜底对话框需要能读到该路径
    m_downloadPath = packagePath;
    HarmonyBridge::instance()->installHap(packagePath);
#else
#if defined(Q_OS_WIN)
    const bool setupPackage = winPackageKind(packagePath) == WinPackageKind::Setup;
#else
    const bool setupPackage = false;
#endif
    if (parent) {
        const auto choice = AppMessageBox::question(
            parent, tr("安装更新"), installConfirmText(setupPackage),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (choice != AppMessageBox::Yes)
            return;
    }

#if defined(Q_OS_WIN)
    // 先在应用内完成识别与解压：出问题时还能当场提示，而不是关掉程序就没下文
    bool isSetup = false;
    QString payloadDir;
    QString errorText;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool prepared = prepareWindowsUpdate(packagePath, &isSetup, &payloadDir, &errorText);
    QApplication::restoreOverrideCursor();
    if (!prepared) {
        if (parent)
            AppMessageBox::warning(parent, tr("安装更新"),
                                   tr("这个更新包无法安装：\n%1\n\n%2")
                                       .arg(QDir::toNativeSeparators(packagePath), errorText));
        return;
    }
    if (!launchWindowsUpdate(packagePath, isSetup, payloadDir, &errorText)) {
        if (parent)
            AppMessageBox::warning(parent, tr("安装更新"), errorText);
        return;
    }
    // 拖放事件里直接 quit 会打断 OLE 拖拽的收尾，放到下一轮事件循环再退出
    QTimer::singleShot(0, qApp, []() { QCoreApplication::quit(); });
#elif defined(Q_OS_LINUX)
    if (launchLinuxUpdateInstaller(packagePath))
        QTimer::singleShot(0, qApp, []() { QCoreApplication::quit(); });
    else if (parent)
        AppMessageBox::information(parent, tr("安装更新"),
                                   tr("当前平台暂不支持应用内更新，请前往浏览器手动安装。"));
#else
    if (parent)
        AppMessageBox::information(parent, tr("安装更新"),
                                   tr("当前平台暂不支持应用内更新，请前往浏览器手动安装。"));
#endif
#endif
}
