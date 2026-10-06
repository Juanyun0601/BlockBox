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

#include "components/AppMessageBox.h"
#include "platform.h"
#include "utils/AndroidBridge.h"
#include "utils/GithubAccelerator.h"
#include "utils/HarmonyBridge.h"
#include "utils/SettingsManager.h"

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
//   公测遗留   betaN       -> {0, 0, 0}，beta 第 N 期（当前发布的 beta2 即此格式）
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

// PowerShell 单引号字符串转义
QString psQuote(QString path)
{
    path.replace(QLatin1Char('\''), QStringLiteral("''"));
    return path;
}

// 清理 Release 说明文本用于弹窗展示：统一换行并截断过长内容
QString normalizeNotes(const QString &notes)
{
    QString text = notes;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    constexpr int kMaxNotesLength = 800;
    if (text.size() > kMaxNotesLength) {
        text = text.left(kMaxNotesLength) + QStringLiteral("\n…");
    }
    return text.trimmed();
}

// 当前平台对应的资产名片段（与 软件发布/一键打包.bat 的命名保持一致）：
//   BlockBox-<版本>-win64|winarm64-{setup.exe|portable.zip}
//   BlockBox-<版本>-android-{arm64-v8a|x86_64|...}.apk
//   BlockBox-<版本>-linux-{x64|arm64}{.deb|.tar.gz}
//   BlockBox-<版本>-harmony-arm64-v8a.hap（HAP 无法在应用内自装，不参与应用内更新）
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

// 桌面平台安装确认文案（按平台与发行形态区分）
QString installConfirmText()
{
#if defined(Q_OS_WIN)
    if (isInstalledEdition())
        return QCoreApplication::translate("UpdateChecker",
            "将关闭方块盒子并静默运行安装程序完成更新，确定继续吗？");
    return QCoreApplication::translate("UpdateChecker",
        "将解压更新包覆盖当前程序目录并重启方块盒子，确定继续吗？");
#elif defined(Q_OS_HARMONY)
    return QCoreApplication::translate("UpdateChecker",
        "将尝试通过系统包管理器安装更新；若无权限将提供手动安装命令，确定继续吗？");
#elif defined(Q_OS_LINUX)
    if (isInstalledEdition())
        return QCoreApplication::translate("UpdateChecker",
            "将打开系统授权安装更新，完成后自动重新启动方块盒子，确定继续吗？");
    return QCoreApplication::translate("UpdateChecker",
        "将解压更新包到当前应用目录旁并从新版本目录重启，确定继续吗？");
#else
    return QCoreApplication::translate("UpdateChecker",
        "将关闭方块盒子并完成更新安装，确定继续吗？");
#endif
}

// 桌面平台（Windows/Linux）按发行形态启动延迟安装流程；
// 返回 false 表示当前平台没有可用的应用内安装方式。
bool launchDesktopUpdateInstaller(const QString &packagePath)
{
#if defined(Q_OS_WIN)
    // PowerShell 延迟 2 秒启动，确保本进程完全退出后再写入文件
    const QFileInfo appExe(QCoreApplication::applicationFilePath());
    const QString appDir = QDir::toNativeSeparators(appExe.absolutePath());
    const QString appExeName = appExe.fileName();
    const QString package = QDir::toNativeSeparators(packagePath);
    QString command;
    if (isInstalledEdition()) {
        // 安装版：静默运行 Inno 安装程序（BlockBox.iss 支持 /VERYSILENT）
        command = QStringLiteral(
            "Start-Sleep -Seconds 2; "
            "& '%1' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART")
            .arg(psQuote(package));
    } else {
        // 绿色版：解压覆盖应用目录后重新启动
        command = QStringLiteral(
            "Start-Sleep -Seconds 2; "
            "Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force; "
            "Start-Process '%3\\%4'")
            .arg(psQuote(package), psQuote(appDir), psQuote(appDir), psQuote(appExeName));
    }
    QProcess::startDetached(QStringLiteral("powershell.exe"),
                            {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"),
                             QStringLiteral("Hidden"), QStringLiteral("-Command"), command});
    return true;
#elif defined(Q_OS_LINUX)
    const QFileInfo appExe(QCoreApplication::applicationFilePath());
    const QString appDir = appExe.absolutePath();
    if (isInstalledEdition()) {
        // deb：polkit 授权后 dpkg 安装，成功即重启应用
        const QString cmd = QStringLiteral("sleep 2; pkexec dpkg -i '%1' && exec '%2'")
            .arg(shQuote(packagePath), shQuote(appExe.absoluteFilePath()));
        QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), cmd});
    } else {
        // tar.gz：包内为带顶层版本目录的目录树，解压到应用目录旁后从新目录启动
        const QString parentDir = QFileInfo(appDir).absolutePath();
        const QString topDir = QFileInfo(packagePath).completeBaseName();
        const QString cmd = QStringLiteral("sleep 2; tar -xzf '%1' -C '%2' && exec '%3/%4/%5'")
            .arg(shQuote(packagePath), shQuote(parentDir), shQuote(parentDir),
                 shQuote(topDir), shQuote(appExe.fileName()));
        QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), cmd});
    }
    return true;
#else
    Q_UNUSED(packagePath);
    return false;
#endif
}

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
    if (!notes.isEmpty())
        box.setInformativeText(tr("更新说明：\n%1").arg(notes));

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

    m_downloadPath = updateDir.filePath(m_lastCandidate.assetName);
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

void UpdateChecker::handleDownloadReply(QNetworkReply *reply)
{
    reply->deleteLater();

    QFile *file = m_downloadFile;
    m_downloadFile = nullptr;
    const bool ok = reply->error() == QNetworkReply::NoError;
    const qint64 total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();

    // 成功判定：有数据写入，且（内容长度未知或与已收字节数一致）
    if (ok && file && m_received > 0 && (total <= 0 || m_received == total)) {
        file->close();
        file->deleteLater();
        m_downloadState = DownloadCompleted;
        emit downloadProgress(m_received, m_received);
        emit downloadFinished();
        return;
    }

    // 下载失败：删除半截文件，按 直连 -> 加速节点 -> 镜像 重试
    const QString error = reply->errorString();
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

#if defined(Q_OS_ANDROID)
    // Android：确认后拉起系统安装器（不退出应用，由系统接管覆盖安装）
    QWidget *parent = m_parent ? static_cast<QWidget *>(m_parent)
                               : QApplication::activeWindow();
    if (parent) {
        const auto choice = AppMessageBox::question(
            parent, tr("安装更新"), tr("将打开系统安装器完成更新安装，确定继续吗？"),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (choice != AppMessageBox::Yes)
            return;
    }
    if (!AndroidBridge::installApk(m_downloadPath) && parent) {
        AppMessageBox::warning(parent, tr("安装更新"),
                               tr("无法打开系统安装器，请前往浏览器手动下载安装。"));
    }
#elif defined(Q_OS_HARMONY)
    // 鸿蒙：确认后尝试通过系统包管理器安装 HAP（需 INSTALL_BUNDLE 权限），
    // 结果异步返回；无权限时由 onHarmonyInstallFinished 提供 hdc 命令兜底
    QWidget *parent = m_parent ? static_cast<QWidget *>(m_parent)
                               : QApplication::activeWindow();
    if (parent) {
        const auto choice = AppMessageBox::question(
            parent, tr("安装更新"), installConfirmText(),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (choice != AppMessageBox::Yes)
            return;
    }
    HarmonyBridge::instance()->installHap(m_downloadPath);
#else
    QWidget *parent = m_parent ? static_cast<QWidget *>(m_parent)
                               : QApplication::activeWindow();
    if (parent) {
        const auto choice = AppMessageBox::question(
            parent, tr("安装更新"), installConfirmText(),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (choice != AppMessageBox::Yes)
            return;
    }
    if (launchDesktopUpdateInstaller(m_downloadPath))
        QCoreApplication::quit();
    else if (parent)
        AppMessageBox::information(parent, tr("安装更新"),
                                   tr("当前平台暂不支持应用内更新，请前往浏览器手动安装。"));
#endif
}
