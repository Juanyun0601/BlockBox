#ifndef CURSEFORGEAPI_H
#define CURSEFORGEAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "ModData.h"
#include "../CfApiKey.h"

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

    /** MCIM 镜像是否启用（use_mcim 设置，未配置时默认启用；镜像端免 API Key） */
    static bool mirrorEnabled();

    /** 本地密钥文件（utils/CfApiKey.h，skip-worktree 保护不入库）中的兜底 key */
    static QString localApiKey() { return QStringLiteral(BLOCKBOX_CF_API_KEY); }

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
    /** 当前 baseUrl 是否指向 MCIM 镜像（镜像端免 API Key） */
    bool usingMirror() const;
    /** 实际生效的 key：setApiKey 传入值优先，其次本地密钥文件兜底 */
    QString effectiveApiKey() const;

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
