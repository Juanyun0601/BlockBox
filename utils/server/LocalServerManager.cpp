/**
 * @file   LocalServerManager.cpp
 * @brief  一键本地开服实现（准备目录 / 下载核心 / 启动进程 / 停止存盘）
 * @author BlockBox Team
 * @date   2026-11-25
 */
#include "LocalServerManager.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTcpServer>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent>
#include <QUrl>
#include <QVector>

#include "utils/DownloadUtils.h"
#include "utils/GameLauncher.h"
#include "utils/JavaScanWorker.h"
#include "utils/SettingsManager.h"
#include "utils/download/DownloadMirrors.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

/** 给子进程加 CREATE_NO_WINDOW，避免开服时弹控制台黑窗 */
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

/** 剥离 ANSI 转义色码（服务端有些插件/核心会带颜色） */
QString stripAnsi(const QString &text)
{
    static const QRegularExpression ansiRe(QStringLiteral("\x1B\\[[0-9;]*[A-Za-z]"));
    return QString(text).remove(ansiRe);
}

/** 端口是否空闲：临时监听 0.0.0.0 试一次 */
bool isPortFree(int port)
{
    if (port <= 0 || port > 65535)
        return false;
    QTcpServer probe;
    const bool ok = probe.listen(QHostAddress::Any, static_cast<quint16>(port));
    if (ok)
        probe.close();
    return ok;
}

/**
 * @brief server.properties 是 Java Properties 格式（ISO-8859-1），
 *        中文值必须写成 \uXXXX，否则服务端读出来是乱码
 */
QString escapePropertyValue(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        if (code >= 0x20 && code <= 0x7E)
            out.append(ch);
        else
            out.append(QStringLiteral("\\u%1").arg(code, 4, 16, QLatin1Char('0')));
    }
    return out;
}

/** 读一份 properties 文件，返回有序的键值列表（保留原文件顺序） */
QVector<QPair<QString, QString>> readProperties(const QString &path)
{
    QVector<QPair<QString, QString>> items;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return items;
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        if (key.isEmpty() || key.startsWith(QLatin1Char('#')))
            continue;
        items.append(qMakePair(key, line.mid(eq + 1)));
    }
    return items;
}

/** 把键值列表写回 properties 文件 */
bool writeProperties(const QString &path, const QVector<QPair<QString, QString>> &items)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "# 由方块盒子「一键本地开服」维护：改过的项会保留，开服时只更新端口/人数等必要项\n";
    for (const auto &item : items)
        out << item.first << '=' << item.second << '\n';
    out.flush();
    return true;
}

/** 从 Java 版本输出里取主版本号（"openjdk version "17.0.9"" → 17） */
int probeJavaMajor(const QString &javaPath)
{
    if (javaPath.isEmpty() || !QFileInfo::exists(javaPath))
        return 0;
    QProcess proc;
    makeSilent(&proc);
    proc.start(javaPath, {QStringLiteral("-version")});
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished(1000);
        return 0;
    }
    const QString text = QString::fromUtf8(proc.readAllStandardError())
                         + QString::fromUtf8(proc.readAllStandardOutput());
    static const QRegularExpression re(
        QStringLiteral("version\\s+\"([^\"]+)\""), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    if (!m.hasMatch())
        return 0;
    return GameLauncher::javaMajorOf(m.captured(1));
}

} // namespace

LocalServerManager::LocalServerManager(QObject *parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    // 卡住的连接（CDN 半死不活 / 被墙）要能自动断掉重来，否则「正在准备」会一直转
    m_network->setTransferTimeout(45000);

    m_stopTimer = new QTimer(this);
    m_stopTimer->setSingleShot(true);
    m_stopTimer->setInterval(15000);
    connect(m_stopTimer, &QTimer::timeout, this, [this]() {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            log(tr("服务端没有响应停止指令，正在强制结束。"));
            m_process->kill();
        }
    });

    // 准备阶段总超时：下载服务端卡住（CDN 连不上又不报错）时给个明确结论
    m_prepareTimer = new QTimer(this);
    m_prepareTimer->setSingleShot(true);
    m_prepareTimer->setInterval(5 * 60 * 1000);
    connect(m_prepareTimer, &QTimer::timeout, this, [this]() {
        if (m_state != State::Preparing)
            return;
        failWith(tr("下载服务端超时了。检查一下网络再试；"
                    "也可以换个服务端核心或换个时段再开。"));
    });
}

LocalServerManager::~LocalServerManager()
{
    // 世界复制线程只碰共享原子量，置位后它自己会收工，不会回头动 this
    if (m_worldCopyCancel)
        m_worldCopyCancel->store(true);

    if (m_activeReply) {
        // 先断开回调再 abort：abort 会同步触发 finished，不能再动已经在销毁的成员
        QObject::disconnect(m_activeReply, nullptr, this, nullptr);
        m_activeReply->abort();
        m_activeReply = nullptr;
    }
    // 装到一半退出：把安装器也停掉
    if (m_installerProcess && m_installerProcess->state() != QProcess::NotRunning) {
        m_installerProcess->kill();
        m_installerProcess->waitForFinished(2000);
    }
    // 窗口被销毁（退出软件）时把服务端一起收掉，避免留下孤儿 javaw；
    // 只等一小会儿，服务端存盘慢就直接结束，别让退出卡住
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->write("stop\n");
        m_process->waitForBytesWritten(300);
        if (!m_process->waitForFinished(2500)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// 静态描述
// ════════════════════════════════════════════════════════════════════

QString LocalServerManager::coreKey(Core core)
{
    switch (core) {
    case Core::Paper:
        return QStringLiteral("paper");
    case Core::Fabric:
        return QStringLiteral("fabric");
    case Core::Forge:
        return QStringLiteral("forge");
    case Core::NeoForge:
        return QStringLiteral("neoforge");
    case Core::Vanilla:
    default:
        return QStringLiteral("vanilla");
    }
}

LocalServerManager::Core LocalServerManager::coreFromKey(const QString &key, Core fallback)
{
    const QString lower = key.trimmed().toLower();
    if (lower == QStringLiteral("paper"))
        return Core::Paper;
    if (lower == QStringLiteral("fabric"))
        return Core::Fabric;
    if (lower == QStringLiteral("forge"))
        return Core::Forge;
    if (lower == QStringLiteral("neoforge"))
        return Core::NeoForge;
    if (lower == QStringLiteral("vanilla"))
        return Core::Vanilla;
    return fallback;
}

QString LocalServerManager::coreDisplayName(Core core)
{
    switch (core) {
    case Core::Paper:
        return tr("Paper（更省资源）");
    case Core::Fabric:
        return tr("Fabric（模组服）");
    case Core::Forge:
        return tr("Forge（模组服）");
    case Core::NeoForge:
        return tr("NeoForge（模组服）");
    case Core::Vanilla:
    default:
        return tr("原版服务端");
    }
}

LocalServerManager::Core LocalServerManager::defaultCoreForLoader(const QString &loader)
{
    const QString lower = loader.toLower();
    if (lower.contains(QStringLiteral("neoforge")))
        return Core::NeoForge;
    if (lower.contains(QStringLiteral("forge")))
        return Core::Forge;
    if (lower.contains(QStringLiteral("fabric")) || lower.contains(QStringLiteral("quilt")))
        return Core::Fabric;
    return Core::Vanilla;
}

bool LocalServerManager::loaderSupported(const QString &loader)
{
    // 五种核心都能一键开服：Forge / NeoForge 走官方安装器，版本太老时由
    // coreAvailableForVersion 拦下来（1.13 之前的 Forge 没有 --installServer）
    Q_UNUSED(loader);
    return true;
}

bool LocalServerManager::coreUsesInstaller(Core core)
{
    return core == Core::Forge || core == Core::NeoForge;
}

bool LocalServerManager::coreAvailableForVersion(Core core, const QString &version)
{
    // 只认 1.x 版本号；快照 / 自定义命名不放进来，免得装错版本
    static const QRegularExpression versionRe(QStringLiteral(R"(^1\.(\d+)(?:\.(\d+))?)"));
    const auto match = versionRe.match(version.trimmed());
    if (!match.hasMatch())
        return core == Core::Vanilla || core == Core::Paper || core == Core::Fabric;

    const int minor = match.captured(1).toInt();
    const int patch = match.captured(2).toInt();

    switch (core) {
    case Core::Forge:
        // 1.13 起官方安装器才支持 --installServer，之前的只能图形界面装
        return minor >= 13;
    case Core::NeoForge:
        // NeoForge 从 1.20.2 开始才有
        return minor > 20 || (minor == 20 && patch >= 2);
    case Core::Fabric:
        // Fabric 从 1.14 开始
        return minor >= 14;
    case Core::Paper:
    case Core::Vanilla:
    default:
        return true;
    }
}

bool LocalServerManager::pickMavenVersion(const QByteArray &xml, const QString &prefix,
                                          QString *best)
{
    if (prefix.isEmpty())
        return false;

    static const QRegularExpression versionTag(
        QStringLiteral("<version>\\s*([^<\\s]+)\\s*</version>"));
    auto it = versionTag.globalMatch(QString::fromUtf8(xml));

    QString stableCandidate;
    QString anyCandidate;
    while (it.hasNext()) {
        // 返回的是完整标签：Forge 是 "1.20.1-47.2.0"，NeoForge 就是 "21.1.77"，
        // 前缀只用来圈定版本段（调用方要短版本号自己再切）
        const QString tag = it.next().captured(1);
        if (!tag.startsWith(prefix))
            continue;
        const QString rest = tag.mid(prefix.size());
        if (rest.isEmpty())
            continue;

        // 数字部分用来比大小（"47.2.0" / "21.1.77"）
        const QString numeric = rest.section(QLatin1Char('-'), 0, 0);
        const auto parts = numeric.split(QLatin1Char('.'));
        if (parts.isEmpty() || parts.first().toInt() <= 0)
            continue;

        const auto versionKey = [](const QString &text) {
            QList<int> numbers;
            for (const QString &piece : text.split(QLatin1Char('.')))
                numbers.append(piece.toInt());
            return numbers;
        };

        // "47.2.0-beta" / "21.1.77-alpha" 这类预发布排在稳定版后面
        const QString suffix = tag.section(QLatin1Char('-'), -1);
        const bool isPrerelease = suffix != tag
                                  && (suffix.contains(QStringLiteral("beta"), Qt::CaseInsensitive)
                                      || suffix.contains(QStringLiteral("alpha"),
                                                         Qt::CaseInsensitive)
                                      || suffix.contains(QStringLiteral("pre"),
                                                         Qt::CaseInsensitive));

        if (!isPrerelease) {
            const QList<int> candidateKey = versionKey(numeric);
            const QList<int> currentKey = versionKey(stableCandidate.mid(prefix.size())
                                                         .section(QLatin1Char('-'), 0, 0));
            if (stableCandidate.isEmpty() || candidateKey > currentKey)
                stableCandidate = tag;
        }
        const QList<int> anyKey = versionKey(numeric);
        const QList<int> anyCurrent =
            versionKey(anyCandidate.mid(prefix.size()).section(QLatin1Char('-'), 0, 0));
        if (anyCandidate.isEmpty() || anyKey > anyCurrent)
            anyCandidate = tag;
    }

    const QString chosen = stableCandidate.isEmpty() ? anyCandidate : stableCandidate;
    if (chosen.isEmpty())
        return false;
    if (best)
        *best = chosen;
    return true;
}

// ════════════════════════════════════════════════════════════════════
// 上下文 / 路径
// ════════════════════════════════════════════════════════════════════

void LocalServerManager::setInstance(const QString &path, const QString &version,
                                     const QString &loader)
{
    const bool sameInstance = !path.isEmpty()
                              && QDir::cleanPath(path).compare(QDir::cleanPath(m_config.instancePath),
                                                              Qt::CaseInsensitive) == 0;

    m_config.instancePath = path;
    m_config.version = version;
    m_config.loader = loader;

    // 换了实例才跟着换核心；同一实例重复注入（每次打开助手窗口都会调）保留用户选的核心
    if (!sameInstance) {
        m_config.core = defaultCoreForLoader(loader);
        m_activePort = m_config.port;
    }
}

QString LocalServerManager::serverDir() const
{
    // 「下载服务端」直接指定目录，不跟实例走
    if (!m_config.serverDirOverride.trimmed().isEmpty())
        return QDir(m_config.serverDirOverride).absolutePath();
    if (m_config.instancePath.isEmpty())
        return QString();
    return QDir(m_config.instancePath).absoluteFilePath(QStringLiteral("server"));
}

QString LocalServerManager::jarPath() const
{
    const QString dir = serverDir();
    if (dir.isEmpty())
        return QString();
    QString name;
    switch (m_config.core) {
    case Core::Paper:
        name = QStringLiteral("paper-server.jar");
        break;
    case Core::Fabric:
        name = QStringLiteral("fabric-server.jar");
        break;
    case Core::Forge:
    case Core::NeoForge:
        name = QStringLiteral("installer.jar");
        break;
    case Core::Vanilla:
    default:
        name = QStringLiteral("server.jar");
        break;
    }
    return QDir(dir).absoluteFilePath(name);
}

bool LocalServerManager::jarReady() const
{
    // Forge / NeoForge 的「就绪」看安装标记与启动配置，不看 jar
    if (coreUsesInstaller(m_config.core)) {
        if (!QFileInfo::exists(readyMarkerPath()))
            return false;
        QString argsFile;
        QString launcherJar;
        return locateInstallerLauncher(&argsFile, &launcherJar);
    }

    const QString path = jarPath();
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    return info.isFile() && info.size() > 4096;
}

QString LocalServerManager::serverWorldDir() const
{
    const QString dir = serverDir();
    return dir.isEmpty() ? QString() : QDir(dir).absoluteFilePath(QStringLiteral("world"));
}

QString LocalServerManager::serverWorldSource() const
{
    QFile file(QDir(serverDir()).absoluteFilePath(QStringLiteral("world_source.txt")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool LocalServerManager::resetServerWorld(QString *error)
{
    const QString worldDir = serverWorldDir();
    if (worldDir.isEmpty()) {
        if (error)
            *error = tr("还没选到实例");
        return false;
    }
    if (QDir(worldDir).exists() && !QDir(worldDir).removeRecursively()) {
        if (error)
            *error = tr("删不掉服务端现有的世界 %1，可能被占用（先关掉正在跑的服务端）。")
                         .arg(worldDir);
        return false;
    }
    QFile::remove(QDir(serverDir()).absoluteFilePath(QStringLiteral("world_source.txt")));
    return true;
}

bool LocalServerManager::isBusy() const
{
    return m_state == State::Preparing || m_state == State::Starting || m_state == State::Stopping;
}

QString LocalServerManager::address() const
{
    return QStringLiteral("127.0.0.1:%1").arg(port());
}

void LocalServerManager::setConfig(const Config &config)
{
    m_config = config;
    // 没开服时端口马上跟着变：界面上的地址与之后写进 server.properties 的端口要一致
    if (!isRunning() && !isBusy())
        m_activePort = m_config.port;
}

// ════════════════════════════════════════════════════════════════════
// 一键开服
// ════════════════════════════════════════════════════════════════════

void LocalServerManager::start()
{
    if (m_state != State::Idle)
        return;

    m_lastError.clear();
    m_progressText.clear();
    m_progressPercent = -1;
    m_aborted = false;

    // 「下载服务端」模式：不跟实例走，只要一个能写的目录
    if (m_config.downloadOnly) {
        if (m_config.serverDirOverride.trimmed().isEmpty()) {
            failWith(tr("还没选下载到哪个目录。"));
            return;
        }
        if (!QDir().mkpath(m_config.serverDirOverride)) {
            failWith(tr("建不出下载目录：%1。").arg(m_config.serverDirOverride));
            return;
        }
    } else if (m_config.instancePath.isEmpty() || !QDir(m_config.instancePath).exists()) {
        failWith(tr("还没选到实例：请先在启动器里选一个实例，再回来开服。"));
        return;
    }
    if (m_config.version.trimmed().isEmpty()) {
        failWith(tr("读不到这个实例的游戏版本，无法下载对应的服务端。"));
        return;
    }
    if (!coreAvailableForVersion(m_config.core, m_config.version)) {
        failWith(tr("%1 不支持 Minecraft %2：Forge 要 1.13 之后、NeoForge 要 1.20.2 之后。"
                    "换成能用的服务端核心，或者把实例换成对应版本。")
                     .arg(coreDisplayName(m_config.core), m_config.version));
        return;
    }

    // 端口：默认端口被别的程序占了就顺延一个
    if (!m_config.downloadOnly) {
        const int wanted = m_config.port > 0 ? m_config.port : 25565;
        m_activePort = pickFreePort(wanted);
        if (m_activePort != wanted)
            log(tr("端口 %1 已被占用，改用 %2 开服。").arg(wanted).arg(m_activePort));
    }

    setState(State::Preparing);
    emit progressChanged(QString(), -1);
    if (m_prepareTimer)
        m_prepareTimer->start();

    // 只下载：不写 eula / server.properties，也不碰实例目录
    if (m_config.downloadOnly) {
        ensureServerProvided();
        return;
    }

    QString cfgError;
    if (!writeConfigs(&cfgError)) {
        failWith(cfgError);
        return;
    }

    prepareModsDir();

    QString javaError;
    if (!resolveJava(&m_javaPath, &javaError)) {
        failWith(javaError);
        return;
    }
    log(tr("使用 Java：%1").arg(m_javaPath));

    // 用实例存档开局时先把世界复制过来（服务端已有世界就直接沿用）
    if (needsWorldCopy()) {
        beginWorldCopy();
        return;
    }
    ensureServerProvided();
}

void LocalServerManager::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        setState(State::Idle);
        return;
    }
    if (m_state == State::Stopping)
        return;

    setState(State::Stopping);
    log(tr("正在停止服务器（等它把存档写完）…"));
    m_process->write("stop\n");
    m_process->waitForBytesWritten(200);
    if (m_stopTimer)
        m_stopTimer->start();
}

bool LocalServerManager::sendCommand(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
        return false;
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return false;

    m_process->write(trimmed.toUtf8() + '\n');
    m_process->waitForBytesWritten(200);
    // 服务端自己会把这条命令回显进日志，这里只标一下是本地发出去的
    log(QStringLiteral("> %1").arg(trimmed));
    return true;
}

void LocalServerManager::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void LocalServerManager::failWith(const QString &reason)
{
    m_lastError = reason;
    log(tr("开服失败：%1").arg(reason));
    m_aborted = true;
    if (m_prepareTimer)
        m_prepareTimer->stop();
    // 正在装服务端：把安装器一起停掉，别留个孤儿进程在后台继续下文件
    if (m_installerProcess && m_installerProcess->state() != QProcess::NotRunning) {
        m_installerProcess->kill();
    }
    if (m_jarDownloading) {
        m_jarDownloading = false;
        if (m_activeReply) {
            m_activeReply->abort();
        }
    }
    setState(State::Idle);
    emit failed(reason);
}

void LocalServerManager::log(const QString &line)
{
    if (!line.isEmpty())
        emit logLine(line);
}

int LocalServerManager::pickFreePort(int preferred)
{
    int port = qBound(1024, preferred, 65535);
    for (int offset = 0; offset < 30; ++offset) {
        const int candidate = port + offset;
        if (candidate > 65535)
            break;
        if (isPortFree(candidate))
            return candidate;
    }
    // 全被占了：还是用原端口，让服务端自己报错，比随便挑一个更清楚
    return port;
}

bool LocalServerManager::writeConfigs(QString *error)
{
    const QString dir = serverDir();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        if (error)
            *error = tr("创建服务端目录失败：%1").arg(dir);
        return false;
    }

    // ── EULA：点「一键开服」即视为同意 Minecraft 服务器最终用户许可协议 ──
    QFile eula(QDir(dir).absoluteFilePath(QStringLiteral("eula.txt")));
    if (!eula.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = tr("写入 eula.txt 失败，请检查实例目录是否有写入权限。");
        return false;
    }
    {
        QTextStream out(&eula);
        out.setEncoding(QStringConverter::Utf8);
        out << "# 由方块盒子「一键本地开服」生成\n"
            << "# 点「一键开服」即表示同意 Minecraft 服务器最终用户许可协议：\n"
            << "# https://aka.ms/MinecraftEULA\n"
            << "eula=true\n";
    }
    eula.close();

    // ── server.properties：保留玩家改过的项，只覆盖必要的几项 ──
    const QString propsPath = QDir(dir).absoluteFilePath(QStringLiteral("server.properties"));
    QVector<QPair<QString, QString>> items = readProperties(propsPath);

    QMap<QString, QString> wanted;
    wanted.insert(QStringLiteral("server-port"), QString::number(m_activePort));
    wanted.insert(QStringLiteral("max-players"), QString::number(qBound(1, m_config.maxPlayers, 200)));
    wanted.insert(QStringLiteral("online-mode"), m_config.onlineMode ? QStringLiteral("true")
                                                                   : QStringLiteral("false"));
    // 离线模式必须关掉安全档案校验，否则客户端会因签名缺失被踢
    wanted.insert(QStringLiteral("enforce-secure-profile"),
                  m_config.onlineMode ? QStringLiteral("true") : QStringLiteral("false"));
    wanted.insert(QStringLiteral("motd"), escapePropertyValue(m_config.motd));

    // 只有文件里还没写过的新建项才补默认值，不覆盖玩家自己的设置
    const QVector<QPair<QString, QString>> defaults = {
        qMakePair(QStringLiteral("level-name"), QStringLiteral("world")),
        qMakePair(QStringLiteral("gamemode"), QStringLiteral("survival")),
        qMakePair(QStringLiteral("difficulty"), QStringLiteral("normal")),
        qMakePair(QStringLiteral("view-distance"), QStringLiteral("8")),
        qMakePair(QStringLiteral("simulation-distance"), QStringLiteral("6")),
        qMakePair(QStringLiteral("spawn-protection"), QStringLiteral("0")),
    };

    // 先更新已有键（保持原顺序），wanted 里剩下的追加到末尾
    for (auto &item : items) {
        const auto it = wanted.find(item.first);
        if (it == wanted.end())
            continue;
        item.second = it.value();
        wanted.erase(it);
    }
    for (const auto &item : defaults) {
        bool exists = false;
        for (const auto &existing : items) {
            if (existing.first == item.first) {
                exists = true;
                break;
            }
        }
        if (!exists)
            items.append(item);
    }
    for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it)
        items.append(qMakePair(it.key(), it.value()));

    if (!writeProperties(propsPath, items)) {
        if (error)
            *error = tr("写入 server.properties 失败，请检查实例目录是否有写入权限。");
        return false;
    }
    return true;
}

void LocalServerManager::prepareModsDir()
{
    // 原版 / Paper 不加载 mods，不用管；Fabric 与 Forge / NeoForge 都要模组
    if (m_config.core != Core::Fabric && m_config.core != Core::Forge
        && m_config.core != Core::NeoForge)
        return;
    if (m_config.instancePath.isEmpty())
        return;

    const QString src = QDir(m_config.instancePath).absoluteFilePath(QStringLiteral("mods"));
    const QString dst = QDir(serverDir()).absoluteFilePath(QStringLiteral("mods"));
    if (!QFileInfo(src).isDir() || QFileInfo(dst).exists())
        return;

#ifdef Q_OS_WIN
    // 建目录联接（junction）：不额外占空间，客户端装/删模组服务端跟着变
    QProcess link;
    makeSilent(&link);
    link.start(QStringLiteral("cmd"),
               {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                QDir::toNativeSeparators(dst), QDir::toNativeSeparators(src)});
    if (link.waitForFinished(8000) && link.exitCode() == 0) {
        log(tr("已把实例的 mods 目录链接给服务端，双端模组保持一致。"));
    } else {
        log(tr("没能自动共享 mods 目录：需要的话把实例里的模组复制到 %1").arg(dst));
    }
#else
    if (::symlink(QFile::encodeName(src).constData(), QFile::encodeName(dst).constData()) == 0) {
        log(tr("已把实例的 mods 目录链接给服务端，双端模组保持一致。"));
    } else {
        log(tr("没能自动共享 mods 目录：需要的话把实例里的模组复制到 %1").arg(dst));
    }
#endif
}

bool LocalServerManager::resolveJava(QString *path, QString *error)
{
    const GameLauncher::JavaRequirement req = GameLauncher::javaRequirementFor(m_config.version, 0);

    struct Candidate
    {
        QString path;
        int major = 0;
    };
    QVector<Candidate> candidates;

    auto addCandidate = [&candidates](const QString &javaPath, const QString &versionText) {
        if (javaPath.isEmpty() || !QFileInfo::exists(javaPath))
            return;
        const QString clean = QDir::cleanPath(javaPath);
        for (const Candidate &existing : candidates) {
            if (existing.path.compare(clean, Qt::CaseInsensitive) == 0)
                return;
        }
        int major = GameLauncher::javaMajorOf(versionText);
        if (major <= 0)
            major = probeJavaMajor(javaPath);
        if (major <= 0)
            return;
        candidates.append(Candidate{clean, major});
    };

    // 1) 启动器缓存的 Java 列表 2) 全局默认 Java
    for (const auto &install : SettingsManager::instance()->getJavaInstallations())
        addCandidate(install.first, install.second);
    addCandidate(SettingsManager::instance()->getJavaPath(), QString());

    // 3) 缓存为空时做一次快速检测（环境变量 / 注册表 / 厂商目录）
    if (candidates.isEmpty()) {
        emit progressChanged(tr("正在检测本机 Java…"), -1);
        JavaScanWorker worker;
        for (const auto &install : worker.runQuickScan())
            addCandidate(install.first, install.second);
    }

    if (candidates.isEmpty()) {
        if (error)
            *error = tr("本机没有找到可用的 Java。请先到「Java管理」页点一次「Java检测」，"
                        "或下载对应版本的 Java。");
        return false;
    }

    // 优先取「满足要求里最低的那个」，避免用过高版本把老版本游戏跑崩
    const Candidate *best = nullptr;
    for (const Candidate &candidate : candidates) {
        if (candidate.major < req.minJava || candidate.major > req.maxJava)
            continue;
        if (!best || candidate.major < best->major)
            best = &candidate;
    }
    if (!best) {
        // 没有完全符合的：只要比要求新也能跑（上限 999 时），否则明确报错
        if (req.maxJava >= 999) {
            for (const Candidate &candidate : candidates) {
                if (candidate.major >= req.minJava && (!best || candidate.major < best->major))
                    best = &candidate;
            }
        }
        if (!best) {
            QStringList have;
            for (const Candidate &candidate : candidates)
                have << QStringLiteral("Java %1").arg(candidate.major);
            if (error)
                *error = tr("%1 需要 %2，本机只找到 %3。请到「Java管理」页下载对应的 Java。")
                             .arg(m_config.version, req.rangeDesc(), have.join(QStringLiteral("、")));
            return false;
        }
    }

    if (path)
        *path = best->path;
    if (best->major > req.maxJava) {
        log(tr("注意：这次用 Java %1 跑 %2，可能不稳定").arg(best->major).arg(m_config.version));
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════
// 世界准备：用实例里的存档开局
// ════════════════════════════════════════════════════════════════════

bool LocalServerManager::needsWorldCopy() const
{
    if (m_config.worldSource.trimmed().isEmpty())
        return false;
    if (!QFileInfo(m_config.worldSource).isDir())
        return false;
    // 服务端已经有一份世界了：继续用那份（要换的话界面会先问、再删掉旧的）
    return !QDir(serverWorldDir()).exists();
}

bool LocalServerManager::copyWorldTree(const QString &sourceDir, const QString &targetDir,
                                       const std::function<void(qint64, qint64)> &onProgress,
                                       const std::atomic_bool *cancel, QString *error)
{
    const QDir source(sourceDir);
    if (!source.exists()) {
        if (error)
            *error = tr("存档目录不存在：%1").arg(sourceDir);
        return false;
    }
    if (!QDir().mkpath(targetDir)) {
        if (error)
            *error = tr("建不出服务端世界目录：%1").arg(targetDir);
        return false;
    }

    // 先过一遍：挑文件、算总大小（存档可能有几万个区块文件）
    QStringList relativeFiles;
    qint64 totalBytes = 0;
    QDirIterator it(sourceDir, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = source.relativeFilePath(path);
        // session.lock 是客户端的占用锁，带过去服务端会以为世界正被别的程序用
        if (relative.compare(QStringLiteral("session.lock"), Qt::CaseInsensitive) == 0)
            continue;
        relativeFiles.append(relative);
        totalBytes += it.fileInfo().size();
    }

    // 空文件世界（不常见）按文件个数报进度，免得百分比一直不动
    const bool byFileCount = (totalBytes <= 0);
    const qint64 total = byFileCount ? qMax<qint64>(1, relativeFiles.size()) : totalBytes;
    if (onProgress)
        onProgress(0, total);

    qint64 done = 0;
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (const QString &relative : relativeFiles) {
        if (cancel && cancel->load())
            return false;

        const QString from = source.absoluteFilePath(relative);
        const QString to = QDir(targetDir).absoluteFilePath(relative);
        QDir().mkpath(QFileInfo(to).absolutePath());

        QFile input(from);
        if (!input.open(QIODevice::ReadOnly)) {
            if (error)
                *error = tr("读不了存档里的文件：%1").arg(relative);
            return false;
        }
        QFile output(to);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error)
                *error = tr("写不进服务端世界目录：%1").arg(relative);
            return false;
        }
        while (!input.atEnd()) {
            if (cancel && cancel->load())
                return false;
            const qint64 read = input.read(buffer.data(), buffer.size());
            if (read <= 0)
                break;
            if (output.write(buffer.constData(), read) != read) {
                if (error)
                    *error = tr("复制世界时写入失败：%1").arg(relative);
                return false;
            }
            if (!byFileCount) {
                done += read;
                if (onProgress)
                    onProgress(done, total);
            }
        }
        output.close();
        input.close();
        if (byFileCount) {
            ++done;
            if (onProgress)
                onProgress(done, total);
        }
    }
    if (onProgress)
        onProgress(total, total);
    return true;
}

void LocalServerManager::beginWorldCopy()
{
    const QString source = m_config.worldSource;
    const QString target = serverWorldDir();
    if (target.isEmpty()) {
        failWith(tr("还没选到实例，没法复制世界。"));
        return;
    }

    emit progressChanged(tr("正在把「%1」复制成服务端世界…").arg(QFileInfo(source).fileName()), -1);
    log(tr("首次用这个存档开服：正在复制存档到服务端（世界大的话要等一会儿）…"));

    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto done = std::make_shared<std::atomic<qint64>>(0);
    auto total = std::make_shared<std::atomic<qint64>>(0);
    auto ok = std::make_shared<std::atomic_bool>(true);
    auto error = std::make_shared<QString>();
    m_worldCopyCancel = cancel;
    m_worldCopyDone = done;
    m_worldCopyTotal = total;
    m_worldCopyOk = ok;
    m_worldCopyError = error;

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            &LocalServerManager::onWorldCopyFinished);
    watcher->setFuture(QtConcurrent::run([source, target, cancel, done, total, ok, error]() {
        QString message;
        const bool result = copyWorldTree(
            source, target,
            [done, total](qint64 copied, qint64 all) {
                done->store(copied);
                total->store(all);
            },
            cancel.get(), &message);
        *error = message;
        ok->store(result);
    }));
    m_worldCopyWatcher = watcher;

    m_worldCopyTimer = new QTimer(this);
    m_worldCopyTimer->setInterval(250);
    connect(m_worldCopyTimer, &QTimer::timeout, this, [this]() {
        const qint64 total = m_worldCopyTotal ? m_worldCopyTotal->load() : 0;
        const qint64 done = m_worldCopyDone ? m_worldCopyDone->load() : 0;
        if (total <= 0) {
            emit progressChanged(tr("正在统计存档体积…"), -1);
            return;
        }
        const int percent = static_cast<int>(
            qBound<qint64>(0, done * 100 / total, static_cast<qint64>(100)));
        emit progressChanged(tr("正在复制世界 %1 / %2")
                                 .arg(DownloadUtils::formatSize(done),
                                      DownloadUtils::formatSize(total)),
                             percent);
    });
    m_worldCopyTimer->start();
}

void LocalServerManager::onWorldCopyFinished()
{
    if (m_worldCopyTimer) {
        m_worldCopyTimer->stop();
        m_worldCopyTimer->deleteLater();
        m_worldCopyTimer = nullptr;
    }
    if (m_worldCopyWatcher) {
        m_worldCopyWatcher->deleteLater();
        m_worldCopyWatcher = nullptr;
    }

    const bool ok = m_worldCopyOk ? m_worldCopyOk->load() : false;
    const QString error = m_worldCopyError ? *m_worldCopyError : QString();
    m_worldCopyDone.reset();
    m_worldCopyTotal.reset();
    m_worldCopyOk.reset();
    m_worldCopyError.reset();
    m_worldCopyCancel.reset();

    if (m_aborted)
        return;

    if (!ok) {
        failWith(error.isEmpty() ? tr("复制存档世界失败了，检查一下磁盘空间再试。") : error);
        return;
    }

    // 记下服务端世界是从哪个存档来的：下次判断要不要覆盖用得上
    QFile marker(QDir(serverDir()).absoluteFilePath(QStringLiteral("world_source.txt")));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        marker.write(m_config.worldSource.toUtf8());
    marker.close();

    emit progressChanged(tr("世界复制完成"), 100);
    log(tr("存档已经复制成服务端世界，接着往下准备服务端本体。"));
    ensureServerProvided();
}

// ════════════════════════════════════════════════════════════════════
// 服务端本体：普通核心下 jar，Forge / NeoForge 跑官方安装器
// ════════════════════════════════════════════════════════════════════

void LocalServerManager::ensureServerProvided()
{
    if (coreUsesInstaller(m_config.core)) {
        ensureInstallerServer();
        return;
    }
    ensureJar();
}

void LocalServerManager::ensureInstallerServer()
{
    if (jarReady()) {
        log(tr("使用已经装好的 %1 服务端。").arg(coreDisplayName(m_config.core)));
        onJarReady();
        return;
    }
    QDir().mkpath(serverDir());
    m_jarDownloading = true;
    fetchInstallerVersion();
}

void LocalServerManager::fetchInstallerVersion()
{
    const bool neo = (m_config.core == Core::NeoForge);
    emit progressChanged(neo ? tr("正在查询 NeoForge 版本…") : tr("正在查询 Forge 版本…"), -1);

    // Forge 的 <version> 是 "1.20.1-47.2.0"，NeoForge 的是 "21.1.77"
    QString prefix;
    if (neo) {
        static const QRegularExpression mcRe(QStringLiteral(R"(^1\.(\d+)(?:\.(\d+))?)"));
        const auto match = mcRe.match(m_config.version.trimmed());
        if (!match.hasMatch()) {
            failWith(tr("认不出 %1 对应的 NeoForge 版本号。").arg(m_config.version));
            return;
        }
        prefix = QStringLiteral("%1.%2.").arg(match.captured(1)).arg(match.captured(2));
    } else {
        prefix = m_config.version.trimmed() + QLatin1Char('-');
    }

    const QString metadataUrl =
        neo ? QStringLiteral(
                  "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml")
            : QStringLiteral(
                  "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml");

    fetchJson(DownloadMirrors::expandFileCandidates(metadataUrl), 0,
              [this, prefix, neo](bool ok, const QByteArray &data) {
        if (!ok) {
            failWith(tr("连不上 %1 的版本清单，检查一下网络再试。")
                         .arg(coreDisplayName(m_config.core)));
            return;
        }
        QString version;
        if (!pickMavenVersion(data, prefix, &version)) {
            failWith(tr("没找到适用于 Minecraft %1 的 %2 版本（可能还没发布）。")
                         .arg(m_config.version, coreDisplayName(m_config.core)));
            return;
        }
        // Forge 的完整标签是 "1.20.1-47.2.0"，真正要用的是后半截
        if (!neo && version.startsWith(prefix))
            version = version.mid(prefix.size());
        m_installerVersion = version;
        log(tr("将安装 %1 %2。").arg(coreDisplayName(m_config.core), version));
        downloadInstaller(version);
    });
}

void LocalServerManager::downloadInstaller(const QString &version)
{
    const bool neo = (m_config.core == Core::NeoForge);
    const QString url =
        neo ? QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/"
                             "neoforge-%1-installer.jar")
                  .arg(version)
            : QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/%1-%2/"
                             "forge-%1-%2-installer.jar")
                  .arg(m_config.version, version);

    emit progressChanged(tr("正在下载 %1 安装器…").arg(coreDisplayName(m_config.core)), -1);
    fetchFile(DownloadMirrors::expandFileCandidates(url), 0, [this, version](bool ok) {
        if (!ok) {
            failWith(tr("安装器下载失败，检查一下网络再试。"));
            return;
        }
        // 只下载模式：安装器下好就算完成，让用户自己（或实例助手）去跑安装
        if (m_config.downloadOnly) {
            if (m_prepareTimer)
                m_prepareTimer->stop();
            m_jarDownloading = false;
            const QString path = jarPath();
            log(tr("安装器已下载：%1").arg(path));
            setState(State::Idle);
            emit downloadFinished(path, true);
            return;
        }
        runInstaller(jarPath(), version);
    });
}

void LocalServerManager::runInstaller(const QString &installerPath, const QString &version)
{
    emit progressChanged(tr("正在用官方安装器安装 %1…").arg(coreDisplayName(m_config.core)), -1);
    m_installerOutput.clear();
    QDir().mkpath(serverDir());

    m_installerProcess = new QProcess(this);
    makeSilent(m_installerProcess);
    m_installerProcess->setWorkingDirectory(serverDir());
    m_installerProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_installerProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_installerProcess)
            return;
        const QString chunk = QString::fromUtf8(m_installerProcess->readAllStandardOutput());
        m_installerOutput += chunk;
        if (m_installerOutput.size() > 30000)
            m_installerOutput = m_installerOutput.right(30000);
        for (const QString &rawLine : chunk.split(QLatin1Char('\n'))) {
            const QString line = stripAnsi(rawLine).trimmed();
            if (!line.isEmpty())
                log(line);
        }
    });
    connect(m_installerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &LocalServerManager::onInstallerFinished);
    connect(m_installerProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !m_aborted)
            failWith(tr("安装器起不来：%1 无法运行，请到「Java管理」页确认 Java。").arg(m_javaPath));
    });

    log(tr("正在运行 %1 %2 安装器（--installServer，首次要下载不少库文件，耐心等一下）…")
            .arg(coreDisplayName(m_config.core), version));
    // 装服务端要下的东西比单纯下个 jar 多得多，把准备阶段的看门狗放宽到 10 分钟
    if (m_prepareTimer)
        m_prepareTimer->start(10 * 60 * 1000);
    QStringList args;
    args << QStringLiteral("-Xmx1024M")
         << QStringLiteral("-Djava.awt.headless=true")
         << QStringLiteral("-jar") << QFileInfo(installerPath).fileName()
         << QStringLiteral("--installServer");
    m_installerProcess->start(m_javaPath, args);
}

void LocalServerManager::onInstallerFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_installerProcess) {
        m_installerProcess->deleteLater();
        m_installerProcess = nullptr;
    }
    if (m_aborted)
        return;

    if (status == QProcess::CrashExit || exitCode != 0) {
        const QStringList outputLines =
            m_installerOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString tail =
            outputLines.mid(qMax(0, outputLines.size() - 6)).join(QLatin1Char('\n'));
        failWith(tr("%1 安装器没跑成功（退出码 %2）。最后几行输出：\n%3")
                     .arg(coreDisplayName(m_config.core))
                     .arg(exitCode)
                     .arg(tail.isEmpty() ? tr("（没有输出）") : tail));
        return;
    }

    QString argsFile;
    QString launcherJar;
    if (!locateInstallerLauncher(&argsFile, &launcherJar)) {
        failWith(tr("%1 安装器跑完了，但没找到启动配置（可能是这个版本太新或太旧）。"
                    "可以删掉实例里的 server 目录重试，或手动装好服务端后用「联机」页做隧道。")
                     .arg(coreDisplayName(m_config.core)));
        return;
    }

    QFile marker(readyMarkerPath());
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        marker.write(QStringLiteral("core=%1\nversion=%2\n")
                         .arg(coreKey(m_config.core), m_installerVersion)
                         .toUtf8());
    }
    marker.close();

    log(tr("%1 服务端装好了，开始启动。").arg(coreDisplayName(m_config.core)));
    onJarReady();
}

bool LocalServerManager::locateInstallerLauncher(QString *argsFile, QString *launcherJar) const
{
    if (argsFile)
        argsFile->clear();
    if (launcherJar)
        launcherJar->clear();

    const QString dir = serverDir();
    if (dir.isEmpty())
        return false;
    const QDir root(dir);

    // 1) 1.17+ 的 Forge / NeoForge：安装器把启动参数写进 @args 文件
    QStringList relativeCandidates;
    if (m_config.core == Core::NeoForge) {
        const QStringList versions = installerVersionCandidates();
        for (const QString &version : versions) {
            relativeCandidates << QStringLiteral("libraries/net/neoforged/neoforge/%1/win_args.txt")
                                      .arg(version);
            relativeCandidates << QStringLiteral("libraries/net/neoforged/neoforge/%1/unix_args.txt")
                                      .arg(version);
        }
    } else {
        const QStringList versions = installerVersionCandidates();
        for (const QString &version : versions) {
            relativeCandidates
                << QStringLiteral("libraries/net/minecraftforge/forge/%1-%2/win_args.txt")
                       .arg(m_config.version, version);
            relativeCandidates
                << QStringLiteral("libraries/net/minecraftforge/forge/%1-%2/unix_args.txt")
                       .arg(m_config.version, version);
        }
    }
#ifdef Q_OS_WIN
    // Windows 优先 win_args.txt：上面两个候选都留着，顺序里 win 在前
#endif
    for (const QString &relative : relativeCandidates) {
        const QString absolute = root.absoluteFilePath(relative);
        if (QFileInfo::exists(absolute)) {
            if (argsFile)
                *argsFile = absolute;
            return true;
        }
    }

    // 2) 1.13 ~ 1.16 的 Forge：安装器留下一枚可执行 jar
    const QStringList patterns = m_config.core == Core::NeoForge
        ? QStringList{QStringLiteral("neoforge-*.jar")}
        : QStringList{QStringLiteral("forge-*.jar")};
    QDirIterator it(dir, patterns, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QStringLiteral("installer.jar"), Qt::CaseInsensitive))
            continue;
        if (launcherJar)
            *launcherJar = path;
        return true;
    }
    return false;
}

QStringList LocalServerManager::installerVersionCandidates() const
{
    QStringList versions;
    if (!m_installerVersion.isEmpty())
        versions << m_installerVersion;

    // 软件重开过：从安装标记里把版本读回来
    QFile marker(readyMarkerPath());
    if (marker.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromUtf8(marker.readAll());
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            if (!line.startsWith(QStringLiteral("version=")))
                continue;
            const QString version = line.mid(8).trimmed();
            if (!version.isEmpty() && !versions.contains(version))
                versions << version;
        }
    }
    return versions;
}

QString LocalServerManager::readyMarkerPath() const
{
    const QString dir = serverDir();
    return dir.isEmpty() ? QString()
                         : QDir(dir).absoluteFilePath(QStringLiteral(".blockbox_ready"));
}

QString LocalServerManager::installerVersion() const
{
    return m_installerVersion;
}

// ════════════════════════════════════════════════════════════════════
// 服务端 jar：本地已有就直接用，没有才下载
// ════════════════════════════════════════════════════════════════════

void LocalServerManager::ensureJar()
{
    if (jarReady()) {
        log(tr("使用已有的服务端：%1").arg(QFileInfo(jarPath()).fileName()));
        onJarReady();
        return;
    }

    QDir().mkpath(serverDir());
    m_jarDownloading = true;

    switch (m_config.core) {
    case Core::Paper:
        fetchPaperServer();
        break;
    case Core::Fabric:
        fetchFabricServer();
        break;
    case Core::Vanilla:
    default:
        fetchVanillaServer();
        break;
    }
}

void LocalServerManager::fetchVanillaServer()
{
    emit progressChanged(tr("正在获取 %1 的服务端信息…").arg(m_config.version), -1);

    const QString manifestUrl =
        QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
    fetchJson(DownloadMirrors::expandFileCandidates(manifestUrl), 0,
              [this](bool ok, const QByteArray &data) {
        if (!ok) {
            failWith(tr("连不上 Mojang 版本清单，检查一下网络或换个下载源再试。"));
            return;
        }
        const QJsonArray versions = QJsonDocument::fromJson(data).object()
                                        .value(QStringLiteral("versions")).toArray();
        QString versionUrl;
        for (const QJsonValue &value : versions) {
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("id")).toString() == m_config.version) {
                versionUrl = item.value(QStringLiteral("url")).toString();
                break;
            }
        }
        if (versionUrl.isEmpty()) {
            failWith(tr("Mojang 官方清单里没有 %1 这个版本，无法下载原版服务端。")
                         .arg(m_config.version));
            return;
        }

        emit progressChanged(tr("正在获取 %1 的服务端信息…").arg(m_config.version), -1);
        fetchJson(DownloadMirrors::expandFileCandidates(versionUrl), 0,
                  [this](bool ok2, const QByteArray &versionData) {
            if (!ok2) {
                failWith(tr("下载服务端信息失败，检查一下网络再试。"));
                return;
            }
            const QJsonObject downloads = QJsonDocument::fromJson(versionData).object()
                                              .value(QStringLiteral("downloads")).toObject();
            const QString serverUrl = downloads.value(QStringLiteral("server")).toObject()
                                          .value(QStringLiteral("url")).toString();
            if (serverUrl.isEmpty()) {
                failWith(tr("%1 这个版本官方没有提供服务端，换 Paper 或 Fabric 试试。")
                             .arg(m_config.version));
                return;
            }
            fetchFile(DownloadMirrors::expandFileCandidates(serverUrl), 0,
                      [this](bool ok3) {
                if (!ok3) {
                    failWith(tr("服务端下载失败，请检查网络后重试。"));
                    return;
                }
                onJarReady();
            });
        });
    });
}

void LocalServerManager::fetchPaperServer()
{
    emit progressChanged(tr("正在获取 Paper 服务端信息…"), -1);

    // PaperMC 现在的下载接口是 fill v3（老的 api.papermc.io/v2 已下线）
    const QString url = QStringLiteral(
                            "https://fill.papermc.io/v3/projects/paper/versions/%1/builds/latest")
                            .arg(m_config.version);
    fetchJson(QStringList{url}, 0, [this](bool ok, const QByteArray &data) {
        if (!ok) {
            failWith(tr("Paper 没有 %1 的构建（或接口连不上），可以换成原版 / Fabric 开服。")
                         .arg(m_config.version));
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(data).object();
        const QJsonObject serverDownload = root.value(QStringLiteral("downloads")).toObject()
                                               .value(QStringLiteral("server:default")).toObject();
        const QString serverUrl = serverDownload.value(QStringLiteral("url")).toString();
        if (serverUrl.isEmpty()) {
            failWith(tr("Paper 没有返回可下载的服务端文件。"));
            return;
        }
        fetchFile(QStringList{serverUrl}, 0, [this](bool ok2) {
            if (!ok2) {
                failWith(tr("Paper 服务端下载失败，请检查网络后重试。"));
                return;
            }
            onJarReady();
        });
    });
}

void LocalServerManager::fetchFabricServer()
{
    emit progressChanged(tr("正在获取 Fabric 服务端信息…"), -1);

    // Fabric 官方服务器 jar：它是启动引导器，首次运行会自己补客户端库与模组加载器
    const QString loaderUrl =
        QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1").arg(m_config.version);
    fetchJson(DownloadMirrors::expandFileCandidates(loaderUrl), 0,
              [this](bool ok, const QByteArray &data) {
        if (!ok) {
            failWith(tr("连不上 Fabric 版本服务，检查一下网络再试。"));
            return;
        }
        const QJsonArray loaders = QJsonDocument::fromJson(data).array();
        QString loaderVersion;
        for (const QJsonValue &value : loaders) {
            const QJsonObject loader = value.toObject().value(QStringLiteral("loader")).toObject();
            if (loader.value(QStringLiteral("stable")).toBool()) {
                loaderVersion = loader.value(QStringLiteral("version")).toString();
                break;
            }
        }
        if (loaderVersion.isEmpty() && !loaders.isEmpty()) {
            loaderVersion = loaders.first().toObject()
                                .value(QStringLiteral("loader")).toObject()
                                .value(QStringLiteral("version")).toString();
        }
        if (loaderVersion.isEmpty()) {
            failWith(tr("Fabric 没有 %1 的加载器版本，无法开模组服。").arg(m_config.version));
            return;
        }

        // Fabric 的服务器 jar 地址必须带安装器版本（少一段会 404），先问一次安装器列表
        emit progressChanged(tr("正在获取 Fabric 安装器版本…"), -1);
        const QString installerUrl =
            QStringLiteral("https://meta.fabricmc.net/v2/versions/installer");
        fetchJson(DownloadMirrors::expandFileCandidates(installerUrl), 0,
                  [this, loaderVersion](bool installerOk, const QByteArray &installerData) {
            QString installerVersion;
            if (installerOk) {
                const QJsonArray installers = QJsonDocument::fromJson(installerData).array();
                for (const QJsonValue &value : installers) {
                    const QJsonObject item = value.toObject();
                    if (item.value(QStringLiteral("stable")).toBool()) {
                        installerVersion = item.value(QStringLiteral("version")).toString();
                        break;
                    }
                }
                if (installerVersion.isEmpty() && !installers.isEmpty()) {
                    installerVersion = installers.first().toObject()
                                           .value(QStringLiteral("version")).toString();
                }
            }
            if (installerVersion.isEmpty()) {
                failWith(tr("拿不到 Fabric 安装器版本，检查一下网络再试。"));
                return;
            }

            const QString jarUrl = QStringLiteral(
                                       "https://meta.fabricmc.net/v2/versions/loader/%1/%2/%3/server/jar")
                                       .arg(m_config.version, loaderVersion, installerVersion);
            fetchFile(DownloadMirrors::expandFileCandidates(jarUrl), 0, [this](bool ok2) {
                if (!ok2) {
                    failWith(tr("Fabric 服务端下载失败，请检查网络后重试。"));
                    return;
                }
                onJarReady();
            });
        });
    });
}

void LocalServerManager::fetchJson(const QStringList &urls, int index,
                                   const std::function<void(bool, const QByteArray &)> &onDone)
{
    if (index >= urls.size()) {
        onDone(false, QByteArray());
        return;
    }

    QNetworkRequest request{QUrl(urls.at(index))};
    request.setRawHeader("User-Agent", "BlockBox");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, urls, index, onDone]() {
        reply->deleteLater();
        if (m_activeReply == reply)
            m_activeReply = nullptr;
        if (m_aborted)   // 已经失败/作废：不再换源继续
            return;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
                        && !data.isEmpty();
        if (ok) {
            onDone(true, data);
            return;
        }
        // 换下一个候选源
        fetchJson(urls, index + 1, onDone);
    });
}

void LocalServerManager::fetchFile(const QStringList &urls, int index,
                                   const std::function<void(bool)> &onDone)
{
    if (index >= urls.size()) {
        onDone(false);
        return;
    }

    const QString target = jarPath();
    const QString partPath = target + QStringLiteral(".part");

    QFile *file = new QFile(partPath, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file->deleteLater();
        failWith(tr("无法写入服务端文件：%1").arg(partPath));
        return;
    }

    QNetworkRequest request{QUrl(urls.at(index))};
    request.setRawHeader("User-Agent", "BlockBox");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);
    m_activeReply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        if (file->isOpen())
            file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { reportDownloadProgress(received, total); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, urls, index, onDone, target, partPath, file]() {
        reply->deleteLater();
        if (m_activeReply == reply)
            m_activeReply = nullptr;

        if (file->isOpen()) {
            file->write(reply->readAll());
            file->close();
        }
        file->deleteLater();

        if (m_aborted)   // 已经失败/作废：清掉半成品，不再换源继续
            QFile::remove(partPath);

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QFileInfo partInfo(partPath);
        // 镜像站偶尔回一个 200 的网页，按文件头挡掉
        bool looksLikeJar = false;
        if (partInfo.isFile() && partInfo.size() > 4096) {
            QFile probe(partPath);
            if (probe.open(QIODevice::ReadOnly)) {
                const QByteArray head = probe.read(2);
                looksLikeJar = head.startsWith("PK") || head.startsWith("\xCA\xFE");
            }
        }

        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
                        && looksLikeJar;
        if (m_aborted)
            return;
        if (!ok) {
            QFile::remove(partPath);
            fetchFile(urls, index + 1, onDone);
            return;
        }

        QFile::remove(target);
        if (!QFile::rename(partPath, target)) {
            QFile::remove(partPath);
            failWith(tr("服务端文件保存失败，请检查磁盘空间与目录权限。"));
            return;
        }
        emit progressChanged(tr("服务端已准备好"), 100);
        onDone(true);
    });
}

void LocalServerManager::reportDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        emit progressChanged(tr("正在下载服务端 %1…").arg(DownloadUtils::formatSize(received)), -1);
        return;
    }
    const int percent = static_cast<int>(received * 100 / total);
    m_progressPercent = percent;
    m_progressText = tr("正在下载服务端 %1 / %2")
                         .arg(DownloadUtils::formatSize(received),
                              DownloadUtils::formatSize(total));
    emit progressChanged(m_progressText, percent);
}

void LocalServerManager::onJarReady()
{
    m_jarDownloading = false;
    if (m_prepareTimer)
        m_prepareTimer->stop();

    // 只下载模式：文件到位就收工，不启动服务端
    if (m_config.downloadOnly) {
        const QString path = jarPath();
        log(tr("服务端已下载：%1").arg(path));
        setState(State::Idle);
        emit downloadFinished(path, false);
        return;
    }

    setState(State::Starting);
    launchProcess(m_javaPath);
}

// ════════════════════════════════════════════════════════════════════
// 启动 / 控制台 / 停止
// ════════════════════════════════════════════════════════════════════

void LocalServerManager::launchProcess(const QString &javaPath)
{
    if (javaPath.isEmpty()) {
        failWith(tr("没有可用的 Java，无法启动服务端。"));
        return;
    }

    QStringList args = buildLaunchArgs();
    if (args.isEmpty()) {
        failWith(tr("没找到 %1 的启动配置：可以删掉实例里的 server 目录重装一次。")
                     .arg(coreDisplayName(m_config.core)));
        return;
    }

    m_process = new QProcess(this);
    makeSilent(m_process);
    m_process->setWorkingDirectory(serverDir());
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &LocalServerManager::onProcessOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &LocalServerManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            failWith(tr("无法运行 Java：%1。请到「Java管理」页确认 Java 是否可用。").arg(m_javaPath));
        }
    });

    log(tr("正在启动服务端：%1，内存 %2 MB…")
            .arg(coreDisplayName(m_config.core))
            .arg(m_config.memoryMb));
    m_startTimer.start();
    m_process->start(m_javaPath, args);
}

QStringList LocalServerManager::buildLaunchArgs() const
{
    const int memory = qBound(1024, m_config.memoryMb, 65536);
    const int initial = qMax(512, memory / 2);

    QStringList args;
    args << QStringLiteral("-Xms%1M").arg(initial)
         << QStringLiteral("-Xmx%1M").arg(memory)
         << QStringLiteral("-Dfile.encoding=UTF-8");

    if (coreUsesInstaller(m_config.core)) {
        QString argsFile;
        QString launcherJar;
        if (!locateInstallerLauncher(&argsFile, &launcherJar))
            return {};
        if (!argsFile.isEmpty()) {
            // 1.17+ 的 Forge / NeoForge：启动参数由安装器写成 @args 文件
            args << QStringLiteral("@%1").arg(QDir(serverDir()).relativeFilePath(argsFile));
            args << QStringLiteral("nogui");
            return args;
        }
        args << QStringLiteral("-jar") << QFileInfo(launcherJar).fileName()
             << QStringLiteral("nogui");
        return args;
    }

    args << QStringLiteral("-jar") << QFileInfo(jarPath()).fileName() << QStringLiteral("nogui");
    return args;
}

void LocalServerManager::onProcessOutput()
{
    if (!m_process)
        return;

    m_outputBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
    QStringList lines = m_outputBuffer.split(QLatin1Char('\n'));
    m_outputBuffer = lines.takeLast();   // 末尾可能是半行，留到下次

    static const QRegularExpression readyRe(QStringLiteral(R"(\bDone\s*\(([^)]*)\)!)"));
    for (const QString &raw : lines) {
        const QString line = stripAnsi(raw).trimmed();
        if (line.isEmpty())
            continue;
        log(line);

        if (m_state == State::Starting && readyRe.match(line).hasMatch()) {
            setState(State::Running);
            m_pendingAddress = address();
            log(tr("服务器已就绪，本机地址：%1").arg(m_pendingAddress));
            emit ready(m_pendingAddress);
            continue;
        }

        // 启动阶段的常见硬故障，直接把原因翻成人话
        if (m_state == State::Starting || m_state == State::Preparing) {
            const QString friendly = friendlyLaunchError(line);
            if (!friendly.isEmpty()) {
                m_lastError = friendly;
                log(friendly);
            }
        }
    }
}

QString LocalServerManager::friendlyLaunchError(const QString &line) const
{
    const QString lower = line.toLower();
    if (lower.contains(QStringLiteral("failed to bind to port"))
        || lower.contains(QStringLiteral("address already in use"))) {
        return tr("端口被占用了：把「端口」换一个（比如 %1）再开服。").arg(m_activePort + 1);
    }
    if (lower.contains(QStringLiteral("you need to agree to the eula")))
        return tr("服务端要求先同意 EULA：重新点一次「一键开服」即可（会自动写好）。");
    if (lower.contains(QStringLiteral("unsupportedclassversionerror"))
        || lower.contains(QStringLiteral("has been compiled by a more recent version"))) {
        return tr("Java 版本太低，跑不了这个服务端：请到「Java管理」页换成更新的 Java。");
    }
    if (lower.contains(QStringLiteral("error: unable to access jarfile")))
        return tr("找不到服务端文件，可能下载不完整：删掉实例里的 server 目录再开一次。");
    return QString();
}

void LocalServerManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_stopTimer)
        m_stopTimer->stop();

    const bool wasStopping = (m_state == State::Stopping);
    const bool neverStarted = (m_state == State::Starting || m_state == State::Preparing);

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_outputBuffer.clear();

    if (wasStopping) {
        log(tr("服务器已停止。"));
        setState(State::Idle);
        return;
    }

    if (neverStarted) {
        const QString reason = m_lastError.isEmpty()
            ? tr("服务端没能启动（退出码 %1）。可以在下面的控制台里看具体报错。").arg(exitCode)
            : m_lastError;
        failWith(reason);
        return;
    }

    if (status == QProcess::CrashExit) {
        const QString reason = tr("服务器意外退出了（退出码 %1），控制台里能看到最后的日志。")
                                   .arg(exitCode);
        log(tr("服务器异常退出了（退出码 %1）。").arg(exitCode));
        setState(State::Idle);
        // 运行中被外部结束 / 自己崩了：让界面明确告诉用户，不要以为还开着
        emit crashed(reason);
        return;
    }

    if (exitCode != 0) {
        log(tr("服务器已关闭（退出码 %1）。").arg(exitCode));
        setState(State::Idle);
        // 正常退出码是 0；非 0 不一定是崩溃，但值得提示一句
        emit crashed(tr("服务器以退出码 %1 结束，如果是被强行结束的可以忽略这条提示。")
                         .arg(exitCode));
        return;
    }

    log(tr("服务器已关闭（退出码 %1）。").arg(exitCode));
    setState(State::Idle);
}
