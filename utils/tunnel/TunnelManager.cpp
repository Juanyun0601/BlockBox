/**
 * @file   TunnelManager.cpp
 * @brief  内网穿透门面实现
 * @author BlockBox Team
 * @date   2026-09-26
 */
#include "TunnelManager.h"

#include "EasyTierEngine.h"
#include "utils/LanTransfer.h"
#include "utils/SettingsManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSysInfo>

namespace {

/** 社区共享节点清单（与陶瓦联机同源，返回 EasyTier 发现地址） */
constexpr char kDefaultNodeListUrl[] = "https://terracotta.glavo.site/nodes";
/** 清单不可用时的兜底节点（已实测返回 tcp:// 连接地址） */
constexpr char kFallbackNodeUrl[] = "https://etnode.zkitefly.eu.org/node1";

const QString kKeyNetworkName = QStringLiteral("tunnel/networkName");
const QString kKeyNetworkSecret = QStringLiteral("tunnel/networkSecret");
const QString kKeyPeerUrls = QStringLiteral("tunnel/peerUrls");

QString randomSecret()
{
    QString secret;
    for (int i = 0; i < 4; ++i)
        secret += QString::number(
            QRandomGenerator::global()->generate(), 16).rightJustified(8, QLatin1Char('0'));
    return secret;
}

} // namespace

TunnelManager *TunnelManager::m_instance = nullptr;

TunnelManager *TunnelManager::instance()
{
    if (!m_instance)
        m_instance = new TunnelManager();
    return m_instance;
}

TunnelManager::TunnelManager(QObject *parent)
    : QObject(parent)
{
    auto *easytier = new EasyTierEngine(this);
    m_engine = easytier;

    connect(m_engine, &TunnelEngine::statusChanged, this, &TunnelManager::statusChanged);
    connect(m_engine, &TunnelEngine::peersChanged, this, &TunnelManager::peersChanged);
    connect(m_engine, &TunnelEngine::errorOccurred, this, &TunnelManager::errorOccurred);
    connect(m_engine, &TunnelEngine::installRequired, this,
            [this]() { ensureInstalled(); });
    connect(easytier, &EasyTierEngine::installStateChanged, this,
            &TunnelManager::installStateChanged);
    connect(easytier, &EasyTierEngine::installFinished, this,
            &TunnelManager::installFinished);
    connect(easytier, &EasyTierEngine::downloadProgressChanged, this,
            &TunnelManager::downloadProgressChanged);
    connect(easytier, &EasyTierEngine::downloadInfoChanged, this,
            &TunnelManager::downloadInfoChanged);

    loadOrFetchPeerUrls();
}

TunnelManager::~TunnelManager()
{
    if (m_engine)
        m_engine->stop();
    m_instance = nullptr;
}

bool TunnelManager::isAvailable() const
{
    return m_engine && m_engine->isAvailable();
}

bool TunnelManager::isRunning() const
{
    return m_engine && m_engine->isRunning();
}

TunnelStatus TunnelManager::status() const
{
    return m_engine ? m_engine->status() : TunnelStatus();
}

// ───────────────────────────── 组网参数 ─────────────────────────────

QString TunnelManager::networkName() const
{
    QString name = SettingsManager::instance()->getProperty(kKeyNetworkName).toString();
    if (name.isEmpty())
        name = QStringLiteral("blockbox");
    return name;
}

QString TunnelManager::networkSecret() const
{
    QString secret =
        SettingsManager::instance()->getProperty(kKeyNetworkSecret).toString();
    if (secret.isEmpty())
    {
        secret = randomSecret();
        SettingsManager::instance()->setProperty(kKeyNetworkSecret, secret);
    }
    return secret;
}

void TunnelManager::setNetworkIdentity(const QString &name, const QString &secret)
{
    SettingsManager::instance()->setProperty(
        kKeyNetworkName, name.trimmed().isEmpty() ? QStringLiteral("blockbox")
                                                  : name.trimmed());
    SettingsManager::instance()->setProperty(kKeyNetworkSecret, secret.trimmed());
    if (isRunning())
        statusChanged();
}

QString TunnelManager::inviteCode() const
{
    return networkName() + QLatin1Char('|') + networkSecret();
}

bool TunnelManager::setInviteCode(const QString &code)
{
    const QString trimmed = code.trimmed();
    const int sep = trimmed.indexOf(QLatin1Char('|'));
    if (sep <= 0 || sep >= trimmed.size() - 1)
        return false;
    const QString name = trimmed.left(sep).trimmed();
    const QString secret = trimmed.mid(sep + 1).trimmed();
    if (name.isEmpty() || secret.isEmpty())
        return false;
    setNetworkIdentity(name, secret);
    return true;
}

// ───────────────────────────── 共享节点 ─────────────────────────────

QStringList TunnelManager::peerUrls() const
{
    return m_peerUrls;
}

void TunnelManager::setPeerUrls(const QStringList &urls)
{
    m_peerUrls.clear();
    for (const QString &url : urls)
    {
        if (!url.trimmed().isEmpty())
            m_peerUrls << url.trimmed();
    }
    SettingsManager::instance()->setProperty(kKeyPeerUrls, m_peerUrls.join(QLatin1Char('\n')));
    emit peerUrlsChanged();
}

void TunnelManager::loadOrFetchPeerUrls()
{
    const QString saved =
        SettingsManager::instance()->getProperty(kKeyPeerUrls).toString();
    if (!saved.trimmed().isEmpty())
    {
        m_peerUrls = saved.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        m_peerUrlsLoaded = true;
        return;
    }
    fetchDefaultPeerUrls();
}

void TunnelManager::fetchDefaultPeerUrls()
{
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(QString::fromLatin1(kDefaultNodeListUrl)));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(15000);

    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, nam, reply]() {
        reply->deleteLater();
        nam->deleteLater();

        QStringList urls;
        if (reply->error() == QNetworkReply::NoError)
        {
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
            for (const QJsonValue &value : arr)
            {
                const QString url = value.toObject().value(QStringLiteral("url")).toString();
                if (!url.isEmpty())
                    urls << url;
            }
        }
        if (urls.isEmpty())
            urls << QString::fromLatin1(kFallbackNodeUrl);

        if (!m_peerUrlsLoaded)
        {
            m_peerUrls = urls;
            m_peerUrlsLoaded = true;
            SettingsManager::instance()->setProperty(
                kKeyPeerUrls, m_peerUrls.join(QLatin1Char('\n')));
            emit peerUrlsChanged();
        }
    });
}

// ───────────────────────────── 控制 ─────────────────────────────

void TunnelManager::start()
{
    if (!m_engine)
        return;

    TunnelConfig config;
    config.networkName = networkName();
    config.networkSecret = networkSecret();
    config.hostname = LanTransfer::instance()->deviceName().isEmpty()
        ? QSysInfo::machineHostName()
        : LanTransfer::instance()->deviceName();
    config.peerUrls = peerUrls();
    config.noTun = true;      // 免管理员权限
    config.privateMode = true; // 只允许同邀请码的节点接入

    m_engine->start(config);
}

void TunnelManager::stop()
{
    if (m_engine)
        m_engine->stop();
}

void TunnelManager::refreshPeers()
{
    if (m_engine)
        m_engine->refreshPeers();
}

void TunnelManager::ensureInstalled()
{
    auto *engine = qobject_cast<EasyTierEngine *>(m_engine);
    if (engine && !engine->isAvailable())
        engine->downloadLatest();
}
