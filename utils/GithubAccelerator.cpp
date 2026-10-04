#include "GithubAccelerator.h"

#include <QDebug>
#include <QMutex>
#include <QUrl>

#include "SettingsManager.h"

GithubAccelerator* GithubAccelerator::m_instance = nullptr;
static QMutex s_ghInstanceMutex;

GithubAccelerator::GithubAccelerator(QObject *parent)
    : QObject(parent)
    , m_enabled(false)
    , m_proxyIndex(0)
{
    initProxies();

    m_enabled = SettingsManager::instance()->getProperty("github_accel_enabled", false).toBool();
    m_proxyIndex = SettingsManager::instance()->getProperty("github_accel_proxy_index", 0).toInt();
    m_customProxyUrl = SettingsManager::instance()->getProperty("github_accel_custom_url", "").toString();
}

GithubAccelerator::~GithubAccelerator()
{
}

GithubAccelerator* GithubAccelerator::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&s_ghInstanceMutex);
        if (!m_instance) {
            m_instance = new GithubAccelerator();
        }
    }
    return m_instance;
}

void GithubAccelerator::initProxies()
{
    m_proxies.clear();
    m_proxies.append({"ghproxy.com", "https://ghproxy.com/", false});
    m_proxies.append({"ghps.cc", "https://ghps.cc/", false});
    m_proxies.append({"gh.api.99988866.xyz", "https://gh.api.99988866.xyz/", false});
    m_proxies.append({"hub.fgit.ml", "https://hub.fgit.ml/", false});
    m_proxies.append({"mirror.ghproxy.com", "https://mirror.ghproxy.com/", false});
    m_proxies.append({"自定义", "", true});
}

bool GithubAccelerator::isEnabled() const
{
    return m_enabled;
}

void GithubAccelerator::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        SettingsManager::instance()->setProperty("github_accel_enabled", enabled);
        emit accelerationChanged();
    }
}

QString GithubAccelerator::proxyUrl() const
{
    if (m_proxyIndex >= 0 && m_proxyIndex < m_proxies.size()) {
        const ProxyMirror &proxy = m_proxies[m_proxyIndex];
        if (proxy.isCustom) {
            QString url = m_customProxyUrl.trimmed();
            if (!url.isEmpty() && !url.endsWith('/')) {
                url += '/';
            }
            return url;
        }
        return proxy.url;
    }
    return m_proxies[0].url;
}

void GithubAccelerator::setProxyUrl(const QString &url)
{
    if (m_customProxyUrl != url) {
        m_customProxyUrl = url;
        SettingsManager::instance()->setProperty("github_accel_custom_url", url);
        if (m_proxyIndex >= 0 && m_proxyIndex < m_proxies.size() && m_proxies[m_proxyIndex].isCustom) {
            emit accelerationChanged();
        }
    }
}

int GithubAccelerator::proxyIndex() const
{
    return m_proxyIndex;
}

void GithubAccelerator::setProxyIndex(int index)
{
    if (m_proxyIndex != index && index >= 0 && index < m_proxies.size()) {
        m_proxyIndex = index;
        SettingsManager::instance()->setProperty("github_accel_proxy_index", index);
        emit accelerationChanged();
    }
}

QStringList GithubAccelerator::availableProxies() const
{
    QStringList list;
    for (const ProxyMirror &proxy : m_proxies) {
        list.append(proxy.name);
    }
    return list;
}

bool GithubAccelerator::isGithubUrl(const QString &url)
{
    return url.contains("github.com") || url.contains("raw.githubusercontent.com")
           || url.contains("gist.githubusercontent.com")
           || url.contains("objects.githubusercontent.com");
}

QString GithubAccelerator::accelerateUrl(const QString &originalUrl) const
{
    if (!m_enabled || originalUrl.isEmpty()) {
        return originalUrl;
    }

    if (!isGithubUrl(originalUrl)) {
        return originalUrl;
    }

    QString proxy = proxyUrl();
    if (proxy.isEmpty()) {
        return originalUrl;
    }

    QString accelerated = proxy + originalUrl;
    qDebug() << "[GithubAccelerator] Accelerated:" << originalUrl << "->" << accelerated;
    return accelerated;
}
