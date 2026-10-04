#ifndef CURSEFORGEAPI_H
#define CURSEFORGEAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "ModData.h"

class CurseForgeAPI : public QObject
{
    Q_OBJECT

public:
    explicit CurseForgeAPI(QObject *parent = nullptr);
    ~CurseForgeAPI();

    void setApiKey(const QString &key);
    void setBaseUrl(const QString &url);
    void setClassId(const QString &classId);
    /** 设置游戏 ID（默认 432=Minecraft Java；基岩版为 78022） */
    void setGameId(const QString &gameId);

    void searchMods(const QString &query, const QString &gameVersion = QString(),
                    const QString &categoryId = QString(), int page = 0,
                    int pageSize = 20, const QString &sortField = "popularity",
                    const QString &sortOrder = "desc");

    void getCategories();
    void fetchModDetail(const QString &modId);

    /**
     * @brief 通过 MurmurHash2 指纹匹配本地模组（单次单个指纹）
     * @param fingerprint MurmurHash2 指纹
     * POST /v1/fingerprints/432
     */
    void fingerprintMod(quint32 fingerprint);

    static QStringList sortFields();
    static QStringList sortOrders();

signals:
    void searchCompleted(const ModSearchResult &result);
    void searchFailed(const QString &error);
    void categoriesLoaded(const QList<ModCategory> &categories);
    void categoriesFailed(const QString &error);
    void modDetailReceived(const ModInfo &detail);
    void modDetailFailed(const QString &error);

    /** 指纹匹配完成 */
    void fingerprintCompleted(quint32 fingerprint, const QString &modId, const QString &pageUrl);
    /** 指纹匹配失败（未找到匹配或无 API Key） */
    void fingerprintFailed(const QString &error);

private slots:
    void onSearchReplyFinished();
    void onCategoriesReplyFinished();
    void onDetailReplyFinished();
    void onFingerprintReplyFinished();

private:
    void parseAddon(const QJsonObject &obj, ModInfo &info);
    void abortReply(QNetworkReply *&reply);

    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;
    QString m_baseUrl;
    QString m_classId;
    QString m_gameId = QStringLiteral("432");

    QNetworkReply *m_currentSearchReply;
    QNetworkReply *m_currentCategoriesReply;
    QNetworkReply *m_currentDetailReply;
    QNetworkReply *m_currentFingerprintReply;
    quint32 m_pendingFingerprint = 0;
};

#endif
