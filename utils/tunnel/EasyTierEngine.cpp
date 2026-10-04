/**
 * @file   EasyTierEngine.cpp
 * @brief  EasyTier 内网穿透引擎实现（--no-tun 免管理员模式）
 * @author BlockBox Team
 * @date   2026-09-26
 */
#include "EasyTierEngine.h"

#include "platform.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

/** BlockBox 专用 RPC 端口，避免与陶瓦联机等其它 EasyTier 实例冲突 */
constexpr char kRpcPortal[] = "127.0.0.1:15890";
constexpr char kInstanceName[] = "blockbox";

constexpr int kPollIntervalMs = 2000;
/** 下载停滞阈值：超过该时长无进度即判定线路挂起并切换 */
constexpr int kStallTimeoutMs = 30000;

/**
 * EasyTier 官方文档（easytier.cn 下载页）推荐的 GitHub 加速镜像，
 * 按本机实测速度排序：直连 GitHub 仅 ~10KB/s（32MB 包要几十分钟，表现为"下不动"），
 * 这些镜像可达 1~2MB/s。前缀 + 原始 release 地址即为可用的加速链接。
 */
const QStringList &downloadMirrors()
{
    static const QStringList mirrors = {
        QStringLiteral("https://cdn.gh-proxy.org/"),
        QStringLiteral("https://edgeone.gh-proxy.org/"),
        QStringLiteral("https://v6.gh-proxy.org/"),
        QStringLiteral("https://hk.gh-proxy.org/"),
        QStringLiteral("https://ghfast.top/"),
    };
    return mirrors;
}

/** 给子进程加 CREATE_NO_WINDOW，避免弹出控制台黑窗 */
void makeSilent(QProcess *process)
{
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
}

QString findBinary(const QString &root, const QString &fileName)
{
    if (root.isEmpty())
        return QString();
    QDirIterator it(root, QStringList() << fileName, QDir::Files,
                    QDirIterator::Subdirectories);
    if (it.hasNext())
        return it.next();
    return QString();
}

} // namespace

EasyTierEngine::EasyTierEngine(QObject *parent)
    : TunnelEngine(parent)
{
    locateBinaries();
}

EasyTierEngine::~EasyTierEngine()
{
    stop();
}

// ───────────────────────────── 安装 ─────────────────────────────

QString EasyTierEngine::installRoot() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = Platform::getDataDirectory();
    return base + QStringLiteral("/easytier");
}

void EasyTierEngine::locateBinaries()
{
    m_corePath = findBinary(installRoot(), QStringLiteral("easytier-core.exe"));
    m_cliPath = findBinary(installRoot(), QStringLiteral("easytier-cli.exe"));
#ifdef Q_OS_MACOS
    if (m_corePath.isEmpty())
        m_corePath = findBinary(installRoot(), QStringLiteral("easytier-core"));
    if (m_cliPath.isEmpty())
        m_cliPath = findBinary(installRoot(), QStringLiteral("easytier-cli"));
#endif
}

bool EasyTierEngine::isAvailable() const
{
    return !m_corePath.isEmpty() && !m_cliPath.isEmpty()
        && QFileInfo::exists(m_corePath) && QFileInfo::exists(m_cliPath);
}

// ───────────────────────────── 启动 / 停止 ─────────────────────────────

QStringList EasyTierEngine::buildCoreArgs(const TunnelConfig &config) const
{
    QStringList args;
    if (config.noTun)
        args << QStringLiteral("--no-tun");
    if (config.privateMode)
        args << QStringLiteral("--private-mode") << QStringLiteral("true");
    // DHCP：由 EasyTier 自动分配虚拟 IP。不加此项节点没有虚拟地址，
    // 既无法被其他节点访问，也拿不到对方的 port-forward 目标。
    args << QStringLiteral("--dhcp");

    args << QStringLiteral("--network-name") << config.networkName;
    args << QStringLiteral("--network-secret") << config.networkSecret;
    if (!config.hostname.isEmpty())
        args << QStringLiteral("--hostname") << config.hostname;
    args << QStringLiteral("-m") << QLatin1String(kInstanceName);
    args << QStringLiteral("-r") << QLatin1String(kRpcPortal);

    for (const QString &url : config.peerUrls)
    {
        if (!url.trimmed().isEmpty())
            args << QStringLiteral("-p") << url.trimmed();
    }
    return args;
}

void EasyTierEngine::start(const TunnelConfig &config)
{
    if (isRunning())
        return;

    if (!isAvailable())
    {
        m_status = TunnelStatus();
        m_status.message = tr("EasyTier 尚未安装");
        emit installRequired();
        emit statusChanged();
        return;
    }

    m_config = config;
    m_status = TunnelStatus();
    m_status.running = false;
    m_status.message = tr("正在启动组网…");
    emit statusChanged();

    m_process = new QProcess(this);
    makeSilent(m_process);
    m_process->setProgram(m_corePath);
    m_process->setArguments(buildCoreArgs(config));
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::started, this, [this]() {
        m_status.running = true;
        m_status.message = tr("组网中，正在获取虚拟地址…");
        emit statusChanged();

        if (!m_pollTimer)
        {
            m_pollTimer = new QTimer(this);
            m_pollTimer->setInterval(kPollIntervalMs);
            connect(m_pollTimer, &QTimer::timeout, this,
                    &EasyTierEngine::refreshPeers);
        }
        m_pollTimer->start();

        // 首次状态稍等片刻，让节点完成握手
        QTimer::singleShot(1200, this, [this]() {
            if (isRunning())
                refreshPeers();
        });
    });

    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                onCoreFinished(exitCode, status);
            });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart)
                {
                    m_status.running = false;
                    m_status.message = tr("启动失败：无法运行 easytier-core");
                    emit errorOccurred(m_status.message);
                    emit statusChanged();
                    if (m_process)
                    {
                        m_process->deleteLater();
                        m_process = nullptr;
                    }
                }
            });

    m_process->start();
}

void EasyTierEngine::onCoreFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_pollTimer)
        m_pollTimer->stop();

    QString output;
    if (m_process)
        output = QString::fromUtf8(m_process->readAllStandardOutput()).trimmed();

    const bool crashed = (status != QProcess::NormalExit) || exitCode != 0;
    m_status.running = false;
    m_status.virtualIp.clear();
    m_status.peers.clear();
    m_status.message = crashed
        ? tr("组网已停止（%1）").arg(output.isEmpty()
                                        ? tr("退出码 %1").arg(exitCode)
                                        : output.left(200))
        : tr("组网已停止");

    if (crashed && !output.isEmpty())
        emit errorOccurred(m_status.message);

    if (m_process)
    {
        m_process->deleteLater();
        m_process = nullptr;
    }

    emit statusChanged();
    emit peersChanged();
}

void EasyTierEngine::stop()
{
    if (m_pollTimer)
        m_pollTimer->stop();

    for (auto it = m_forwards.constBegin(); it != m_forwards.constEnd(); ++it)
        closeForward(it.key());
    m_forwards.clear();

    if (!m_process)
        return;

    QProcess *proc = m_process;
    m_process = nullptr;
    disconnect(proc, nullptr, this, nullptr);
    proc->terminate();
    if (!proc->waitForFinished(3000))
    {
        proc->kill();
        proc->waitForFinished(2000);
    }
    proc->deleteLater();

    m_status = TunnelStatus();
    m_status.message = tr("组网已停止");
    emit statusChanged();
    emit peersChanged();
}

bool EasyTierEngine::isRunning() const
{
    return m_process != nullptr && m_process->state() != QProcess::NotRunning;
}

TunnelStatus EasyTierEngine::status() const
{
    TunnelStatus snapshot = m_status;
    snapshot.running = isRunning();
    if (snapshot.running && snapshot.virtualIp.isEmpty()
        && snapshot.message.isEmpty())
    {
        snapshot.message = tr("组网中…");
    }
    return snapshot;
}

// ───────────────────────────── 状态查询 ─────────────────────────────

QString EasyTierEngine::runCli(const QStringList &args, int timeoutMs) const
{
    if (m_cliPath.isEmpty())
        return QString();

    QProcess proc;
    makeSilent(&proc);
    proc.setProgram(m_cliPath);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(3000))
        return QString();
    if (!proc.waitForFinished(timeoutMs))
    {
        proc.kill();
        proc.waitForFinished(1000);
        return QString();
    }
    if (proc.exitCode() != 0)
        return QString();
    return QString::fromUtf8(proc.readAllStandardOutput());
}

void EasyTierEngine::parseNodeInfo(const QString &output)
{
    QString virtualIp;
    QString hostname;

    // 优先 JSON 输出（easytier-cli -o json node）
    const QJsonObject nodeObj = QJsonDocument::fromJson(output.toUtf8()).object();
    if (!nodeObj.isEmpty())
    {
        virtualIp = nodeObj.value(QStringLiteral("ipv4_addr")).toString();
        hostname = nodeObj.value(QStringLiteral("hostname")).toString();
        // 带掩码（10.126.126.1/24）→ 取地址部分
        const int slash = virtualIp.indexOf(QLatin1Char('/'));
        if (slash > 0)
            virtualIp = virtualIp.left(slash);
    }

    if (virtualIp.isEmpty())
    {
        // 兜底：解析表格输出
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &raw : lines)
        {
            const QString line = raw.trimmed();
            if (!line.contains(QChar(0x2502)) && !line.contains(QLatin1Char('|')))
                continue;
            QString normalized = line;
            normalized.replace(QChar(0x2502), QLatin1Char('|'));
            const QStringList cells = normalized.split(QLatin1Char('|'));
            if (cells.size() < 3)
                continue;
            const QString key = cells.at(1).trimmed();
            const QString value = cells.at(2).trimmed();
            if (key.compare(QStringLiteral("Virtual IP"), Qt::CaseInsensitive) == 0)
                virtualIp = value;
            else if (key.compare(QStringLiteral("Hostname"), Qt::CaseInsensitive) == 0)
                hostname = value;
        }
        const int slash = virtualIp.indexOf(QLatin1Char('/'));
        if (slash > 0)
            virtualIp = virtualIp.left(slash);
    }

    if (virtualIp != m_status.virtualIp || hostname != m_status.hostname)
    {
        m_status.virtualIp = virtualIp;
        if (!hostname.isEmpty())
            m_status.hostname = hostname;
        if (!virtualIp.isEmpty())
            m_status.message = tr("组网已就绪");
        emit statusChanged();
    }
}

QList<TunnelPeer> EasyTierEngine::parsePeerList(const QString &output) const
{
    QList<TunnelPeer> peers;

    // 优先 JSON（easytier-cli -o json peer）
    const QJsonArray arr = QJsonDocument::fromJson(output.toUtf8()).array();
    if (!arr.isEmpty())
    {
        for (const QJsonValue &value : arr)
        {
            const QJsonObject obj = value.toObject();
            TunnelPeer peer;
            peer.virtualIp = obj.value(QStringLiteral("ipv4")).toString();
            peer.hostname = obj.value(QStringLiteral("hostname")).toString();
            peer.cost = obj.value(QStringLiteral("cost")).toString();
            peer.latency = obj.value(QStringLiteral("lat_ms")).toString();
            peer.tunnelProto = obj.value(QStringLiteral("tunnel_proto")).toString();
            if (peer.virtualIp.isEmpty())
                continue; // 无私有虚拟 IP 的行（共享节点）
            peer.isSelf = (peer.cost.compare(QStringLiteral("Local"),
                                             Qt::CaseInsensitive) == 0)
                || (!m_status.virtualIp.isEmpty()
                    && peer.virtualIp == m_status.virtualIp);
            peers.append(peer);
        }
        return peers;
    }

    // 兜底：解析表格输出
    static const QRegularExpression ipRe(
        QStringLiteral("\\b([0-9]{1,3}(?:\\.[0-9]{1,3}){3})\\b"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &raw : lines)
    {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const auto ipMatch = ipRe.match(line);
        if (!ipMatch.hasMatch())
            continue;

        TunnelPeer peer;
        peer.virtualIp = ipMatch.captured(1);
        const QString rest = line.mid(ipMatch.capturedEnd(1)).trimmed();
        const QStringList tokens = rest.split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;
        peer.hostname = tokens.at(0);
        if (tokens.size() > 1)
            peer.cost = tokens.at(1);
        if (tokens.size() > 2)
            peer.latency = tokens.at(2);

        static const QRegularExpression protoRe(
            QStringLiteral("\\b(udp|tcp|wss|quic|wg|wireguard|kcp)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        const auto protoMatch = protoRe.match(rest);
        if (protoMatch.hasMatch())
            peer.tunnelProto = protoMatch.captured(1).toLower();

        peer.isSelf = (!m_status.virtualIp.isEmpty()
                       && peer.virtualIp == m_status.virtualIp);
        peers.append(peer);
    }
    return peers;
}

void EasyTierEngine::refreshPeers()
{
    if (!isRunning())
        return;

    const QStringList base{QStringLiteral("-p"), QLatin1String(kRpcPortal),
                           QStringLiteral("-n"), QLatin1String(kInstanceName),
                           QStringLiteral("-o"), QStringLiteral("json")};

    const QString nodeOut = runCli(base + QStringList{QStringLiteral("node")});
    if (!nodeOut.isEmpty())
        parseNodeInfo(nodeOut);

    const QString peerOut = runCli(base + QStringList{QStringLiteral("peer")});
    if (peerOut.isEmpty())
        return;

    const QList<TunnelPeer> peers = parsePeerList(peerOut);
    bool changed = (peers.size() != m_status.peers.size());
    if (!changed)
    {
        for (int i = 0; i < peers.size(); ++i)
        {
            const TunnelPeer &a = peers.at(i);
            const TunnelPeer &b = m_status.peers.at(i);
            if (a.virtualIp != b.virtualIp || a.hostname != b.hostname
                || a.cost != b.cost || a.tunnelProto != b.tunnelProto)
            {
                changed = true;
                break;
            }
        }
    }
    if (changed)
    {
        m_status.peers = peers;
        emit peersChanged();
    }
}

// ───────────────────────────── 端口转发 ─────────────────────────────

quint16 EasyTierEngine::allocateLocalPort() const
{
    // 从固定区间挑一个空闲端口；命中已占用就继续试
    for (quint16 port = 47400; port < 47499; ++port)
    {
        bool used = false;
        for (auto it = m_forwards.constBegin(); it != m_forwards.constEnd(); ++it)
        {
            if (it.key().endsWith(QLatin1Char(':') + QString::number(port)))
            {
                used = true;
                break;
            }
        }
        if (!used)
            return port;
    }
    return 47400;
}

QString EasyTierEngine::openForward(const QString &remoteIp, quint16 remotePort,
                                    quint16 localPort)
{
    if (!isRunning() || remoteIp.isEmpty())
        return QString();

    const quint16 port = (localPort > 0) ? localPort : allocateLocalPort();
    const QString localAddr =
        QStringLiteral("127.0.0.1:%1").arg(port);
    const QString remoteAddr =
        QStringLiteral("%1:%2").arg(remoteIp).arg(remotePort);

    const QString out = runCli({QStringLiteral("-p"), QLatin1String(kRpcPortal),
                                QStringLiteral("-n"), QLatin1String(kInstanceName),
                                QStringLiteral("port-forward"), QStringLiteral("add"),
                                QStringLiteral("tcp"), localAddr, remoteAddr},
                               8000);
    // 命令无输出且未超时（成功时 CLI 通常静默）；失败时 runCli 返回空，同样无法区分，
    // 因此再用 list 确认一次
    Q_UNUSED(out);
    const QString list = runCli({QStringLiteral("-p"), QLatin1String(kRpcPortal),
                                 QStringLiteral("-n"), QLatin1String(kInstanceName),
                                 QStringLiteral("port-forward"), QStringLiteral("list")},
                                8000);
    if (!list.contains(localAddr))
        return QString();

    m_forwards.insert(localAddr, remoteAddr);
    return localAddr;
}

void EasyTierEngine::closeForward(const QString &forwardAddr)
{
    if (forwardAddr.isEmpty() || m_cliPath.isEmpty())
        return;

    runCli({QStringLiteral("-p"), QLatin1String(kRpcPortal),
            QStringLiteral("-n"), QLatin1String(kInstanceName),
            QStringLiteral("port-forward"), QStringLiteral("remove"),
            QStringLiteral("tcp"), forwardAddr},
           8000);
    m_forwards.remove(forwardAddr);
}

// ───────────────────────────── 下载安装 ─────────────────────────────

void EasyTierEngine::downloadLatest()
{
    if (m_installing)
        return;
    m_installing = true;
    emit installStateChanged();

    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.github.com/repos/EasyTier/EasyTier/releases/latest")));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(30000);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            m_installing = false;
            emitDownloadInfo(QString());
            emit installFinished(false, tr("检查 EasyTier 版本失败：%1")
                                            .arg(reply->errorString()));
            emit installStateChanged();
            return;
        }
        onVersionReceived(reply->readAll());
    });
}

void EasyTierEngine::onVersionReceived(const QByteArray &data)
{
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const QString version =
        root.value(QStringLiteral("tag_name")).toString().trimmed();
    if (version.isEmpty())
    {
        m_installing = false;
        emit installFinished(false, tr("无法解析 EasyTier 版本信息"));
        emit installStateChanged();
        return;
    }
    m_latestVersion = version;

    QString arch = QSysInfo::currentCpuArchitecture().toLower();
    QString tag;
    if (arch.contains(QStringLiteral("x86_64")) || arch.contains(QStringLiteral("amd64")))
        tag = QStringLiteral("x86_64");
    else if (arch.contains(QStringLiteral("arm64")) || arch.contains(QStringLiteral("aarch64")))
        tag = QStringLiteral("arm64");
    else
        tag = QStringLiteral("i686");

    const QString expect =
        QStringLiteral("easytier-windows-%1-%2.zip").arg(tag, version);

    QString url;
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets)
    {
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("name")).toString() == expect)
        {
            url = asset.value(QStringLiteral("browser_download_url")).toString();
            break;
        }
    }
    if (url.isEmpty())
    {
        // 找不到精确匹配时退回任意 Windows 包
        for (const QJsonValue &value : assets)
        {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (name.startsWith(QStringLiteral("easytier-windows-"))
                && name.endsWith(QStringLiteral(".zip")))
            {
                url = asset.value(QStringLiteral("browser_download_url")).toString();
                break;
            }
        }
    }
    if (url.isEmpty())
    {
        m_installing = false;
        emit installFinished(false, tr("该版本没有适用的 Windows 安装包"));
        emit installStateChanged();
        return;
    }

    const QString dir = installRoot() + QLatin1Char('/') + version;
    QDir().mkpath(dir);
    m_pendingArchive = dir + QLatin1Char('/') + QFileInfo(url).fileName();
    m_manualUrl = url;

    // 体积校验用（镜像可能返回错误页，靠体积 + ZIP 头识别）
    m_expectedSize = -1;
    const QJsonArray assetArr = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assetArr)
    {
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("browser_download_url")).toString() == url)
        {
            m_expectedSize = static_cast<qint64>(
                asset.value(QStringLiteral("size")).toDouble(-1));
            break;
        }
    }

    emitDownloadInfo(tr("准备下载 EasyTier %1…").arg(version));

    // 线路队列：官方镜像优先（快），直连 GitHub 兜底
    m_downloadQueue.clear();
    for (const QString &mirror : downloadMirrors())
        m_downloadQueue << mirror + url;
    m_downloadQueue << url;

    ensureDownloadWatchdog();
    m_downloadWatchdog->start();
    startAssetDownload(m_downloadQueue.takeFirst());
    emit installStateChanged();
}

void EasyTierEngine::startAssetDownload(const QString &url)
{
    if (m_downloadReply)
    {
        m_downloadReply->disconnect(this);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }

    const QUrl parsedUrl(url);
    QNetworkRequest req(parsedUrl);
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(30 * 60 * 1000);

    m_downloadReply = m_nam.get(req);
    m_downloadReply->setProperty("url", url);
    m_lastProgressAt = QDateTime::currentMSecsSinceEpoch();

    const QString host = QUrl(url).host();
    emitDownloadInfo(tr("线路 %1").arg(host.isEmpty() ? tr("直连") : host));

    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_lastProgressAt = QDateTime::currentMSecsSinceEpoch();
                m_downloadProgress = (total > 0) ? double(received) / double(total) : -1;
                emit downloadProgressChanged(m_downloadProgress);
            });
    connect(m_downloadReply, &QNetworkReply::finished, this, [this]() {
        onAssetDownloadFinished();
    });
}

void EasyTierEngine::onAssetDownloadFinished()
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    const QString triedUrl = reply->property("url").toString();

    // ── 失败判定：网络错误 / 落盘失败 / 内容不合法（镜像可能返回错误页）──
    QString reason;
    if (reply->error() != QNetworkReply::NoError)
    {
        reason = reply->errorString();
    }
    else
    {
        QFile out(m_pendingArchive);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            reason = tr("无法写入临时文件");
        }
        else
        {
            out.write(reply->readAll());
            out.close();
            if (!validateArchive(m_expectedSize))
                reason = tr("下载内容不完整或格式不正确");
        }
    }

    if (!reason.isEmpty())
    {
        // 还有备用线路就接着试
        if (!m_downloadQueue.isEmpty())
        {
            const QString next = m_downloadQueue.takeFirst();
            m_downloadProgress = -1;
            emit downloadProgressChanged(-1);
            emitDownloadInfo(tr("线路无响应，切换下一条…"));
            qWarning() << "[EasyTier] download failed on" << triedUrl << reason
                       << "→ next:" << next;
            startAssetDownload(next);
            return;
        }

        // 全部线路失败：给出可手动操作的指引
        m_installing = false;
        m_downloadProgress = -1;
        emit downloadProgressChanged(-1);
        emitDownloadInfo(QString());
        stopDownloadWatchdog();
        emit installFinished(
            false,
            tr("下载 EasyTier 失败：%1\n\n"
               "已尝试全部下载线路。你可以手动下载后使用：\n"
               "1. 打开 %2\n"
               "2. 解压其中的 easytier-core.exe、easytier-cli.exe 及同目录下的 "
               "wintun.dll、Packet.dll 到该文件夹")
                .arg(reason, installRoot(), m_manualUrl));
        emit installStateChanged();
        return;
    }

    // ── 下载成功，解压安装 ──
    m_downloadProgress = -1;
    emit downloadProgressChanged(-1);
    stopDownloadWatchdog();
    emitDownloadInfo(tr("解压安装中…"));

    const QString version = m_latestVersion;
    extractAndInstall(m_pendingArchive, version);
}

bool EasyTierEngine::validateArchive(qint64 expectedSize) const
{
    const QFileInfo info(m_pendingArchive);
    if (!info.exists())
        return false;

    // ZIP 文件头固定为 "PK"
    QFile f(m_pendingArchive);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(2);
    f.close();
    if (head != QByteArrayLiteral("PK"))
        return false;

    if (expectedSize > 0 && info.size() != expectedSize)
        return false;

    // 明显偏小的包一律视为坏文件
    if (info.size() < 1024 * 1024)
        return false;

    return true;
}

void EasyTierEngine::extractAndInstall(const QString &archivePath,
                                       const QString &version)
{
    const QString targetDir = installRoot() + QLatin1Char('/') + version;
    QDir().mkpath(targetDir);

    // Windows 10+ 自带 bsdtar，可直接解开 zip（与陶瓦联机安装方式一致）
    auto *tar = new QProcess(this);
    makeSilent(tar);
    tar->setWorkingDirectory(targetDir);
    tar->setProgram(QStringLiteral("tar"));
    tar->setArguments({QStringLiteral("-xf"), archivePath, QStringLiteral("-C"),
                       targetDir});

    connect(tar, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, tar, archivePath, targetDir](int exitCode, QProcess::ExitStatus) {
                tar->deleteLater();
                QFile::remove(archivePath);
                emitDownloadInfo(QString());

                if (exitCode != 0)
                {
                    m_installing = false;
                    emit installFinished(false, tr("解压 EasyTier 包失败"));
                    emit installStateChanged();
                    return;
                }

                Q_UNUSED(targetDir);
                locateBinaries();
                m_installing = false;

                const bool okNow = isAvailable();
                emit installFinished(okNow,
                    okNow ? tr("EasyTier 安装完成")
                          : tr("安装后仍未找到 easytier-core"));
                emit installStateChanged();
            });

    tar->start();
}

// ───────────────────── 下载线路文案与停滞看门狗 ─────────────────────

void EasyTierEngine::emitDownloadInfo(const QString &info)
{
    emit downloadInfoChanged(info);
}

void EasyTierEngine::ensureDownloadWatchdog()
{
    if (m_downloadWatchdog)
        return;
    m_downloadWatchdog = new QTimer(this);
    m_downloadWatchdog->setInterval(3000);
    connect(m_downloadWatchdog, &QTimer::timeout, this,
            &EasyTierEngine::checkDownloadStall);
}

void EasyTierEngine::stopDownloadWatchdog()
{
    if (m_downloadWatchdog)
        m_downloadWatchdog->stop();
}

void EasyTierEngine::checkDownloadStall()
{
    if (!m_downloadReply)
        return;
    if (m_lastProgressAt <= 0)
        return;

    // 超过阈值没有进度变化：判定该线路挂起，中止后走失败分支自动切换
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastProgressAt;
    if (elapsed < kStallTimeoutMs)
        return;

    qWarning() << "[EasyTier] download stalled" << elapsed << "ms on"
               << m_downloadReply->property("url").toString();
    emitDownloadInfo(tr("线路无响应，切换下一条…"));
    m_lastProgressAt = QDateTime::currentMSecsSinceEpoch(); // 避免重复触发
    m_downloadReply->abort();
}