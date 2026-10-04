#ifndef MCMODAPI_H
#define MCMODAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMap>
#include <QSet>

class MCModAPI : public QObject
{
    Q_OBJECT

public:
    explicit MCModAPI(QObject *parent = nullptr);
    ~MCModAPI();

    void lookupChineseName(const QString &modId, const QString &englishName);
    void setBaseUrl(const QString &url);

signals:
    void nameResolved(const QString &modId, const QString &chineseName, const QString &mcmodUrl);
    void lookupFailed(const QString &modId, const QString &englishName);

private slots:
    void onReplyFinished();

private:
    void loadBundledDatabase();

    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
    struct CacheEntry { QString chineseName; QString mcmodUrl; };
    QMap<QString, CacheEntry> m_cache;
    QMap<QString, QString> m_bundledDb;

    struct PendingRequest {
        QString modId;
        QString englishName;
    };
    QMap<QNetworkReply*, PendingRequest> m_pendingRequests;
    QSet<QString> m_inFlight;
};

#endif
