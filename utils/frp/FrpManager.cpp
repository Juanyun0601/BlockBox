/**
 * @file   FrpManager.cpp
 * @brief  frp 服务商联机管理实现
 * @author BlockBox Team
 * @date   2026-10-31
 */
#include "FrpManager.h"

#include "platform.h"
#include "utils/SettingsManager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

/** 下载停滞阈值：超过该时长无进度即判定线路挂起并切换 */
constexpr int kStallTimeoutMs = 30000;

/** 给子进程加 CREATE_NO_WINDOW，避免弹出控制台黑窗 */
void makeSilent(QProcess *process)
{
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#else
    Q_UNUSED(process);
#endif
}

/** 剥离 ANSI 转义色码（樱花/OpenFrp 的 frpc 输出带颜色） */
QString stripAnsi(const QString &text)
{
    static const QRegularExpression ansiRe(
        QStringLiteral("\x1B\\[[0-9;]*[A-Za-z]"));
    return QString(text).remove(ansiRe);
}

/** 域名:端口 / IPv4:端口 */
const QRegularExpression &domainAddrRe()
{
    static const QRegularExpression re(
        QStringLiteral("((?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z]{2,}):(\\d{1,5})"));
    return re;
}

const QRegularExpression &ipAddrRe()
{
    static const QRegularExpression re(
        QStringLiteral("((?:\\d{1,3}\\.){3}\\d{1,3}):(\\d{1,5})"));
    return re;
}

/** 隧道建立的成功标记（各家魔改 frpc 通用 + 上游原版） */
const QStringList &successMarkers()
{
    static const QStringList markers = {
        QStringLiteral("start proxy success"),
        QStringLiteral("启动成功"),
        QStringLiteral("隧道已启动"),
        QStringLiteral("隧道启动成功"),
        QStringLiteral("代理启动成功"),
    };
    return markers;
}

bool isPrivateHost(const QString &host)
{
    const QString h = host.toLower();
    if (h == QStringLiteral("localhost") || h.startsWith(QStringLiteral("127.")))
        return true;
    if (h.startsWith(QStringLiteral("0.0.0.0")) || h.startsWith(QStringLiteral("10.")))
        return true;
    if (h.startsWith(QStringLiteral("192.168.")) || h.startsWith(QStringLiteral("169.254.")))
        return true;
    if (h.startsWith(QStringLiteral("172."))) {
        const QStringList parts = h.split(QLatin1Char('.'));
        if (parts.size() > 1) {
            bool ok = false;
            const int second = parts.at(1).toInt(&ok);
            if (ok && second >= 16 && second <= 31)
                return true;
        }
    }
    return false;
}

/** 服务商 API/官网域名，避免把 frpc 连接控制端的地址误认成映射地址 */
const QStringList &controlHostHints()
{
    static const QStringList hints = {
        QStringLiteral("natfrp.com"),
        QStringLiteral("openfrp.net"),
        QStringLiteral("openfrp.io"),
        QStringLiteral("openfrp.com"),
        QStringLiteral("zyghit.cn"),
        QStringLiteral("globalslb.net"),
    };
    return hints;
}

} // namespace

FrpManager *FrpManager::m_instance = nullptr;

FrpManager *FrpManager::instance()
{
    if (!m_instance)
        m_instance = new FrpManager();
    return m_instance;
}

FrpManager::FrpManager(QObject *parent)
    : QObject(parent)
{
}

FrpManager::~FrpManager()
{
    stop();
    if (m_dlReply) {
        m_dlReply->abort();
        m_dlReply->deleteLater();
    }
    m_instance = nullptr;
}

// ───────────────────────────── 服务商参数 ─────────────────────────────

QString FrpManager::providerId(Provider p)
{
    return p == Provider::Sakura ? QStringLiteral("sakura")
                                 : QStringLiteral("openfrp");
}

QString FrpManager::providerName(Provider p)
{
    return p == Provider::Sakura ? QStringLiteral("樱花联机")
                                 : QStringLiteral("OpenFrp 联机");
}

QColor FrpManager::providerColor(Provider p)
{
    return p == Provider::Sakura ? QColor(233, 30, 99)   // 樱花粉
                                 : QColor(33, 150, 243); // OpenFrp 蓝
}

QString FrpManager::panelUrl(Provider p)
{
    return p == Provider::Sakura ? QStringLiteral("https://www.natfrp.com/tunnel/")
                                 : QStringLiteral("https://www.openfrp.net/");
}

// ───────────────────────────── 安装 ─────────────────────────────

QString FrpManager::installRoot(Provider p) const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = Platform::getDataDirectory();
    return base + QStringLiteral("/frp/") + providerId(p);
}

void FrpManager::locateFrpc(Provider p)
{
    const QString root = installRoot(p);
    if (root.isEmpty()) {
        m_frpcPaths.remove(providerId(p));
        return;
    }

    // 精确 frpc.exe 优先，其次任意 frpc*.exe
    QDirIterator exact(root, QStringList() << QStringLiteral("frpc.exe"),
                       QDir::Files, QDirIterator::Subdirectories);
    if (exact.hasNext()) {
        m_frpcPaths.insert(providerId(p), exact.next());
        return;
    }
    QDirIterator loose(root, QStringList() << QStringLiteral("frpc*.exe"),
                       QDir::Files, QDirIterator::Subdirectories);
    if (loose.hasNext()) {
        m_frpcPaths.insert(providerId(p), loose.next());
        return;
    }
    m_frpcPaths.remove(providerId(p));
}

bool FrpManager::isFrpcPresent(Provider p) const
{
    auto *self = const_cast<FrpManager *>(this);
    const QString cached = self->m_frpcPaths.value(providerId(p));
    if (!cached.isEmpty() && QFileInfo::exists(cached))
        return true;
    // 缓存缺失或文件已不存在时重新扫描安装目录
    self->locateFrpc(p);
    const QString path = self->m_frpcPaths.value(providerId(p));
    return !path.isEmpty() && QFileInfo::exists(path);
}

QString FrpManager::frpcPath(Provider p) const
{
    if (!isFrpcPresent(p))
        return QString();
    return const_cast<FrpManager *>(this)->m_frpcPaths.value(providerId(p));
}

QString FrpManager::installedVersion(Provider p) const
{
    return SettingsManager::instance()
        ->getProperty(QStringLiteral("frp/%1/version").arg(providerId(p)))
        .toString();
}

QString FrpManager::latestVersion(Provider p) const
{
    return isDownloading(p) ? m_latestVersion : QString();
}

bool FrpManager::importFrpc(Provider p, const QString &exePath)
{
    if (exePath.isEmpty() || !QFileInfo::exists(exePath))
        return false;

    const QString root = installRoot(p);
    QDir().mkpath(root);
    const QString target = root + QStringLiteral("/frpc.exe");
    QFile::remove(target);
    if (!QFile::copy(exePath, target))
        return false;

    locateFrpc(p);
    if (m_frpcPaths.value(providerId(p)).isEmpty())
        return false;

    SettingsManager::instance()->setProperty(
        QStringLiteral("frp/%1/version").arg(providerId(p)),
        QStringLiteral("手动导入"));
    return true;
}

// ───────────────────────────── 下载安装 ─────────────────────────────

void FrpManager::downloadFrpc(Provider p)
{
    if (isDownloading())
        return;

    m_dlProvider = p;
    m_latestVersion.clear();
    m_downloadQueue.clear();
    m_expectedSize = -1;
    m_downloadProgress = 0;
    emit downloadProgressChanged(m_downloadProgress);
    emit downloadStateChanged();

    emitDownloadInfo(tr("正在查询 frpc 版本…"));

    if (p == Provider::Sakura) {
        fetchSakuraDownload();
    } else {
        fetchOpenFrpDownload();
    }
}

void FrpManager::fetchSakuraDownload()
{
    QNetworkRequest req(QUrl(QStringLiteral("https://api.natfrp.com/v4/system/clients")));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(30000);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finishInstall(false, tr("查询樱花 frpc 版本失败：%1\n\n"
                                    "可到官网面板手动下载后，点「手动选择 frpc」导入。")
                                     .arg(reply->errorString()));
            return;
        }

        const QJsonObject frpc = QJsonDocument::fromJson(reply->readAll())
                                     .object()
                                     .value(QStringLiteral("frpc"))
                                     .toObject();
        m_latestVersion = frpc.value(QStringLiteral("ver")).toString();
        const QJsonObject archs = frpc.value(QStringLiteral("archs")).toObject();

        // 平台/架构键名不确定，按优先级模糊匹配：windows+amd64 > amd64 > windows
        QString chosenUrl;
        QString fallbackUrl;
        for (auto it = archs.constBegin(); it != archs.constEnd(); ++it) {
            const QString key = it.key().toLower();
            const QString url = it.value().toObject()
                                    .value(QStringLiteral("url")).toString();
            if (url.isEmpty())
                continue;
            if (key.contains(QStringLiteral("windows")) && key.contains(QStringLiteral("amd64"))) {
                chosenUrl = url;
                break;
            }
            if (key.contains(QStringLiteral("amd64")) && fallbackUrl.isEmpty())
                fallbackUrl = url;
            else if (key.contains(QStringLiteral("windows")) && fallbackUrl.isEmpty())
                fallbackUrl = url;
        }
        if (chosenUrl.isEmpty())
            chosenUrl = fallbackUrl;

        if (chosenUrl.isEmpty()) {
            finishInstall(false, tr("樱花 frpc 版本清单中没有适用的 Windows 安装包"));
            return;
        }

        m_pendingIsZip = QUrl(chosenUrl).fileName()
                            .endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
        m_downloadQueue << chosenUrl;
        startAssetDownload(chosenUrl, -1);
    });
}

void FrpManager::fetchOpenFrpDownload()
{
    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.openfrp.net/commonQuery/get?key=software")));
    req.setRawHeader("User-Agent", "BlockBox"); // 空 UA 会被防火墙拦截
    req.setTransferTimeout(30000);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finishInstall(false, tr("查询 OpenFrp frpc 版本失败：%1\n\n"
                                    "可到官网面板手动下载后，点「手动选择 frpc」导入。")
                                     .arg(reply->errorString()));
            return;
        }

        const QJsonObject data = QJsonDocument::fromJson(reply->readAll())
                                     .object()
                                     .value(QStringLiteral("data"))
                                     .toObject();
        const QString version = data.value(QStringLiteral("latest_full")).toString();
        QString arch = QStringLiteral("amd64");
        const QString cpu = QSysInfo::currentCpuArchitecture().toLower();
        if (cpu.contains(QStringLiteral("arm64")) || cpu.contains(QStringLiteral("aarch64")))
            arch = QStringLiteral("arm64");
        else if (cpu.contains(QStringLiteral("x86_64")) || cpu.contains(QStringLiteral("amd64")))
            arch = QStringLiteral("amd64");
        else
            arch = QStringLiteral("386");

        m_latestVersion = version;
        const QString fileName =
            QStringLiteral("frpc_windows_%1.zip").arg(arch);
        for (const QJsonValue &value : data.value(QStringLiteral("source")).toArray()) {
            const QString base = value.toObject().value(QStringLiteral("value")).toString();
            if (!base.isEmpty())
                m_downloadQueue << base + QLatin1Char('/') + version + QLatin1Char('/')
                                       + fileName;
        }
        if (m_downloadQueue.isEmpty()) {
            finishInstall(false, tr("OpenFrp 版本清单中没有可用的下载源"));
            return;
        }

        m_pendingIsZip = true;
        emitDownloadInfo(tr("准备下载 frpc %1…").arg(version));
        startAssetDownload(m_downloadQueue.takeFirst(), -1);
    });
}

void FrpManager::startAssetDownload(const QString &url, qint64 expectedSize)
{
    if (m_dlReply) {
        m_dlReply->disconnect(this);
        m_dlReply->abort();
        m_dlReply->deleteLater();
        m_dlReply = nullptr;
    }

    m_expectedSize = expectedSize;
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(30 * 60 * 1000);

    m_dlReply = m_nam.get(req);
    m_dlReply->setProperty("url", url);
    m_lastProgressAt = QDateTime::currentMSecsSinceEpoch();

    const QString host = QUrl(url).host();
    emitDownloadInfo(tr("线路 %1").arg(host.isEmpty() ? tr("直连") : host));
    ensureWatchdog();
    m_watchdog->start();

    connect(m_dlReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_lastProgressAt = QDateTime::currentMSecsSinceEpoch();
                m_downloadProgress = (total > 0) ? double(received) / double(total) : -1;
                emit downloadProgressChanged(m_downloadProgress);
            });
    connect(m_dlReply, &QNetworkReply::finished, this,
            &FrpManager::onAssetDownloadFinished);
}

void FrpManager::onAssetDownloadFinished()
{
    QNetworkReply *reply = m_dlReply;
    m_dlReply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    const QString triedUrl = reply->property("url").toString();

    QString reason;
    if (reply->error() != QNetworkReply::NoError) {
        reason = reply->errorString();
    } else {
        QDir().mkpath(installRoot(m_dlProvider));
        m_pendingArchive = installRoot(m_dlProvider) + QLatin1Char('/')
                           + QUrl(triedUrl).fileName();
        QFile out(m_pendingArchive);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            reason = tr("无法写入临时文件");
        } else {
            out.write(reply->readAll());
            out.close();
            if (!validateArchive(m_pendingArchive, m_pendingIsZip, m_expectedSize))
                reason = tr("下载内容不完整或格式不正确");
        }
    }

    if (!reason.isEmpty()) {
        if (!m_downloadQueue.isEmpty()) {
            const QString next = m_downloadQueue.takeFirst();
            m_downloadProgress = 0;
            emit downloadProgressChanged(m_downloadProgress);
            emitDownloadInfo(tr("线路无响应，切换下一条…"));
            qWarning() << "[Frp] 下载失败：" << triedUrl << reason
                       << "→ 下一条：" << next;
            startAssetDownload(next, -1);
            return;
        }

        finishInstall(false, tr("下载 frpc 失败：%1\n\n"
                                "可到官网面板手动下载 frpc，再点「手动选择 frpc」导入。")
                                 .arg(reason));
        return;
    }

    m_downloadProgress = -1;
    emit downloadProgressChanged(m_downloadProgress);
    stopWatchdog();
    emitDownloadInfo(tr("解压安装中…"));
    extractAndInstall(m_pendingArchive, m_dlProvider, m_latestVersion, m_pendingIsZip);
}

bool FrpManager::validateArchive(const QString &path, bool isZip,
                                 qint64 expectedSize) const
{
    const QFileInfo info(path);
    if (!info.exists())
        return false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(2);
    f.close();

    if (isZip)
        return head == QByteArrayLiteral("PK"); // ZIP 文件头
    if (head != QByteArrayLiteral("MZ"))        // Windows PE 文件头
        return false;

    if (expectedSize > 0 && info.size() != expectedSize)
        return false;
    return info.size() > 1024 * 1024; // 明显偏小的包一律视为坏文件
}

void FrpManager::extractAndInstall(const QString &archivePath, Provider p,
                                   const QString &version, bool isZip)
{
    const QString targetDir = installRoot(p);
    QDir().mkpath(targetDir);

    if (!isZip) {
        // 直链是单个 exe：已落在安装目录，改名后即完成
        const QString target = targetDir + QStringLiteral("/frpc.exe");
        QFile::remove(target);
        if (!QFile::rename(archivePath, target)) {
            finishInstall(false, tr("安装 frpc 失败：无法写入程序文件"));
            return;
        }
        emitDownloadInfo(QString());
        locateFrpc(p);
        SettingsManager::instance()->setProperty(
            QStringLiteral("frp/%1/version").arg(providerId(p)), version);
        finishInstall(isFrpcPresent(p),
                      isFrpcPresent(p) ? tr("frpc 安装完成")
                                       : tr("安装后仍未找到 frpc 程序"));
        return;
    }

    // Windows 10+ 自带 bsdtar，可直接解开 zip（与联机核心安装方式一致）
    auto *tar = new QProcess(this);
    makeSilent(tar);
    tar->setWorkingDirectory(targetDir);
    tar->setProgram(QStringLiteral("tar"));
    tar->setArguments({QStringLiteral("-xf"), archivePath,
                       QStringLiteral("-C"), targetDir});

    connect(tar, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, tar, archivePath, p, version](int exitCode,
                                                       QProcess::ExitStatus) {
                tar->deleteLater();
                QFile::remove(archivePath);
                emitDownloadInfo(QString());

                if (exitCode != 0) {
                    finishInstall(false, tr("解压 frpc 包失败"));
                    return;
                }

                locateFrpc(p);
                SettingsManager::instance()->setProperty(
                    QStringLiteral("frp/%1/version").arg(providerId(p)), version);
                finishInstall(isFrpcPresent(p),
                              isFrpcPresent(p) ? tr("frpc 安装完成")
                                               : tr("安装后仍未找到 frpc 程序"));
            });

    tar->start();
}

void FrpManager::finishInstall(bool success, const QString &message)
{
    m_downloadProgress = -1;
    m_downloadInfo.clear();
    emit downloadProgressChanged(-1);
    emit downloadInfoChanged(QString());
    stopWatchdog();
    emit installFinished(success, message);
    emit downloadStateChanged();
}

void FrpManager::emitDownloadInfo(const QString &info)
{
    m_downloadInfo = info;
    emit downloadInfoChanged(info);
}

void FrpManager::ensureWatchdog()
{
    if (m_watchdog)
        return;
    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(3000);
    connect(m_watchdog, &QTimer::timeout, this, &FrpManager::checkStall);
}

void FrpManager::stopWatchdog()
{
    if (m_watchdog)
        m_watchdog->stop();
}

void FrpManager::checkStall()
{
    if (!m_dlReply)
        return;
    if (m_lastProgressAt <= 0)
        return;

    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastProgressAt;
    if (elapsed < kStallTimeoutMs)
        return;

    qWarning() << "[Frp] 下载无进度" << elapsed << "ms，线路："
               << m_dlReply->property("url").toString();
    emitDownloadInfo(tr("线路无响应，切换下一条…"));
    m_lastProgressAt = QDateTime::currentMSecsSinceEpoch();
    m_dlReply->abort();
}

// ───────────────────────────── 配置 ─────────────────────────────

QString FrpManager::accessKey(Provider p) const
{
    return SettingsManager::instance()
        ->getProperty(QStringLiteral("frp/%1/accessKey").arg(providerId(p)))
        .toString();
}

void FrpManager::setAccessKey(Provider p, const QString &key)
{
    SettingsManager::instance()->setProperty(
        QStringLiteral("frp/%1/accessKey").arg(providerId(p)), key.trimmed());
}

int FrpManager::tunnelId(Provider p) const
{
    return SettingsManager::instance()
        ->getProperty(QStringLiteral("frp/%1/tunnelId").arg(providerId(p)))
        .toInt();
}

void FrpManager::setTunnelId(Provider p, int id)
{
    SettingsManager::instance()->setProperty(
        QStringLiteral("frp/%1/tunnelId").arg(providerId(p)), id);
}

// ───────────────────────────── 运行 ─────────────────────────────

QStringList FrpManager::buildLaunchArgs(Provider p) const
{
    const QString key = accessKey(p);
    const int id = tunnelId(p);
    QStringList args;
    if (p == Provider::Sakura) {
        // 樱花：frpc -f <访问密钥>:<隧道ID>
        args << QStringLiteral("-f")
             << QStringLiteral("%1:%2").arg(key).arg(id);
    } else {
        // OpenFrp：frpc -u <访问密钥> -p <隧道ID>
        args << QStringLiteral("-u") << key
             << QStringLiteral("-p") << QString::number(id);
    }
    return args;
}

void FrpManager::start(Provider p)
{
    if (isRunning())
        return;

    if (!isFrpcPresent(p)) {
        emit errorOccurred(tr("尚未安装 frpc 客户端，请先点击顶部「下载」按钮安装。"));
        return;
    }
    if (accessKey(p).isEmpty()) {
        emit errorOccurred(tr("请先填写访问密钥（在官网面板「访问密钥」页面获取）。"));
        return;
    }
    if (tunnelId(p) <= 0) {
        emit errorOccurred(tr("请先填写隧道 ID（在官网面板隧道列表中查看）。"));
        return;
    }

    m_runningProvider = p;
    m_established = false;
    m_remoteAddress.clear();
    m_lastLogLine.clear();
    m_exitReason.clear();
    m_logBuffer.clear();
    emit stateChanged();
    emit remoteAddressChanged();

    m_process = new QProcess(this);
    makeSilent(m_process);
    m_process->setProgram(frpcPath(p));
    m_process->setArguments(buildLaunchArgs(p));
    m_process->setWorkingDirectory(installRoot(p));
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &FrpManager::onProcessReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FrpManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    m_exitReason = tr("无法运行 frpc 程序");
                    emit errorOccurred(m_exitReason);
                    if (m_process) {
                        m_process->deleteLater();
                        m_process = nullptr;
                    }
                    emit stateChanged();
                }
            });

    m_process->start();
    emit stateChanged();
}

void FrpManager::onProcessReadyRead()
{
    if (!m_process)
        return;
    m_logBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
    QStringList lines = m_logBuffer.split(QLatin1Char('\n'));
    m_logBuffer = lines.takeLast(); // 末尾可能是半行，留到下次
    for (const QString &raw : lines)
        parseLogLine(raw);
}

void FrpManager::parseLogLine(const QString &raw)
{
    const QString line = stripAnsi(raw).trimmed();
    if (line.isEmpty())
        return;
    m_lastLogLine = line;

    if (!m_established) {
        for (const QString &marker : successMarkers()) {
            if (line.contains(marker, Qt::CaseInsensitive)) {
                m_established = true;
                emit stateChanged();
                break;
            }
        }
    }

    const QString addr = extractAddress(line);
    if (!addr.isEmpty() && addr != m_remoteAddress) {
        m_remoteAddress = addr;
        emit remoteAddressChanged();
    }
}

QString FrpManager::extractAddress(const QString &line) const
{
    // 服务商专属映射域名后缀（可信度最高）
    const QString preferred = (m_runningProvider == Provider::Sakura)
        ? QStringLiteral(".natfrp.cloud")
        : QStringLiteral(".openfrpcloud.com");

    QString generic;
    for (const QRegularExpression *re :
         {&domainAddrRe(), &ipAddrRe()}) {
        auto it = re->globalMatch(line);
        while (it.hasNext()) {
            const auto match = it.next();
            const QString host = match.captured(1);
            bool portOk = false;
            const int port = match.captured(2).toInt(&portOk);
            if (!portOk || port < 1 || port > 65535)
                continue;
            if (isPrivateHost(host))
                continue;

            const QString addr = host + QLatin1Char(':') + QString::number(port);
            if (host.endsWith(preferred, Qt::CaseInsensitive))
                return addr; // 专属后缀直接采用
            if (generic.isEmpty()
                && !std::any_of(controlHostHints().constBegin(),
                                controlHostHints().constEnd(),
                                [&host](const QString &hint) {
                                    return host.contains(hint, Qt::CaseInsensitive);
                                })) {
                generic = addr;
            }
        }
    }
    return generic;
}

void FrpManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    const bool crashed = (status != QProcess::NormalExit) || exitCode != 0;
    if (crashed && !m_lastLogLine.isEmpty())
        m_exitReason = m_lastLogLine.left(200);
    else if (crashed)
        m_exitReason = tr("退出码 %1").arg(exitCode);

    emit stateChanged();
    if (crashed && !m_exitReason.isEmpty())
        emit errorOccurred(tr("frpc 已退出：%1").arg(m_exitReason));
}

bool FrpManager::isRunning() const
{
    return m_process != nullptr && m_process->state() != QProcess::NotRunning;
}

QString FrpManager::statusText() const
{
    if (!isRunning())
        return m_exitReason.isEmpty() ? tr("未启动")
                                      : tr("已断开（%1）").arg(m_exitReason);
    if (!m_established)
        return tr("连接节点中…");
    return tr("隧道已建立");
}

void FrpManager::stop()
{
    if (!m_process)
        return;

    QProcess *proc = m_process;
    m_process = nullptr;
    disconnect(proc, nullptr, this, nullptr);
    proc->terminate();
    if (!proc->waitForFinished(3000)) {
        proc->kill();
        proc->waitForFinished(2000);
    }
    proc->deleteLater();

    m_established = false;
    emit stateChanged();
}
