#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class GithubAccelerator : public QObject
{
    Q_OBJECT

public:
    static GithubAccelerator* instance();

    bool isEnabled() const;
    void setEnabled(bool enabled);

    QString proxyUrl() const;
    void setProxyUrl(const QString &url);

    int proxyIndex() const;
    void setProxyIndex(int index);

    QStringList availableProxies() const;

    QString accelerateUrl(const QString &originalUrl) const;
    static bool isGithubUrl(const QString &url);

signals:
    void accelerationChanged();

private:
    GithubAccelerator(QObject *parent = nullptr);
    ~GithubAccelerator();

    static GithubAccelerator *m_instance;

    bool m_enabled;
    int m_proxyIndex;
    QString m_customProxyUrl;

    struct ProxyMirror {
        QString name;
        QString url;
        bool isCustom;
    };
    QVector<ProxyMirror> m_proxies;
    void initProxies();
};
