#ifndef MODRINTHAPI_H
#define MODRINTHAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QMap>
#include "ModData.h"

class ModrinthAPI : public QObject
{
    Q_OBJECT

public:
    explicit ModrinthAPI(QObject *parent = nullptr);
    ~ModrinthAPI();

    void setBaseUrl(const QString &url);
    void setProjectType(const QString &projectType);

    void searchMods(const QString &query, const QString &gameVersion = QString(),
                    const QString &categoryId = QString(), int page = 0,
                    int pageSize = 20, const QString &sortField = "relevance",
                    const QString &sortOrder = "desc");

    void getCategories();
    void fetchModDetail(const QString &modId);

    /**
     * @brief 批量按项目 ID 取项目基础信息（供详情页展示前置资源用）
     * GET /v2/projects?ids=["a","b"]
     * @param ids 项目 ID 列表（即依赖项里记录的 project_id）
     */
    void fetchProjectsByIds(const QStringList &ids);

    /**
     * @brief 通过 SHA-1 哈希匹配本地模组文件
     * @param sha1 SHA-1 哈希值（十六进制小写）
     * GET /v2/version_file/{sha1}?algorithm=sha1
     */
    void matchHash(const QString &sha1);

    /**
     * @brief 批量通过 SHA-1 哈希匹配本地模组文件（一次请求，参考 PCL/HMCL 做法）
     * POST /v2/version_files，body: {"hashes":[...], "algorithm":"sha1"}
     * @param sha1s 待匹配的 SHA-1 哈希列表（十六进制小写）
     */
    void matchHashes(const QList<QString> &sha1s);

    /**
     * @brief 批量检查更新：返回每个哈希对应项目中满足 loaders/game_versions 的最新版本
     * POST /v2/version_files/update，body: {"hashes":[...], "algorithm":"sha1",
     *                                         "loaders":[...], "game_versions":[...]}
     * @param sha1s 本地模组文件的 SHA-1 哈希列表
     * @param gameVersions 需要兼容的 Minecraft 版本（可为空=不限）
     * @param loaders 需要兼容的加载器，如 "fabric"/"forge"（可为空=不限）
     */
    void checkModUpdates(const QList<QString> &sha1s,
                         const QStringList &gameVersions,
                         const QStringList &loaders);

    static QStringList sortFields();
    static QStringList sortOrders();

signals:
    void searchCompleted(const ModSearchResult &result);
    void searchFailed(const QString &error);
    void categoriesLoaded(const QList<ModCategory> &categories);
    void categoriesFailed(const QString &error);
    void modDetailReceived(const ModInfo &detail);
    void modDetailFailed(const QString &error);

    /** 批量项目查询完成（只含项目基础信息，不带版本列表） */
    void projectsResolved(const QList<ModInfo> &projects);
    void projectsResolveFailed(const QString &error);

    /** SHA-1 哈希匹配完成，返回匹配到的版本信息和项目 ID */
    void hashMatched(const QString &sha1, const QString &projectId, const ModVersionFile &version);
    void hashMatchFailed(const QString &sha1, const QString &error);

    /** 批量哈希匹配完成：key=sha1，value=匹配到的版本（未匹配到的哈希不在 map 中） */
    void hashesMatched(const QMap<QString, ModVersionFile> &versions);
    void hashesMatchFailed(const QString &error);

    /** 批量更新检查完成：key=sha1，value=该哈希可更新的最新版本（未匹配的哈希不在 map 中） */
    void updatesChecked(const QMap<QString, ModVersionFile> &versions);
    void updatesCheckFailed(const QString &error);

private slots:
    void onSearchReplyFinished();
    void onCategoriesReplyFinished();
    void onDetailReplyFinished();

private:
    void abortReply(QNetworkReply *&reply);
    void fetchModVersions(const QString &modId);
    /** 解析项目对象（/v2/project/{id} 与 /v2/projects 返回的结构一致） */
    void parseProjectObject(const QJsonObject &obj, ModInfo &info);
    ModVersionFile parseVersionObject(const QJsonObject &obj);
    /** 镜像加速不可用（503/超时）时退回官方接口，返回是否真的切换了 */
    bool switchOffMirror();
    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
    QString m_projectType;
    QNetworkReply *m_currentSearchReply;
    QNetworkReply *m_currentCategoriesReply;
    QNetworkReply *m_currentDetailReply;
    QNetworkReply *m_currentVersionsReply;
    QNetworkReply *m_currentProjectsReply;
    ModInfo m_pendingDetailInfo;
    QString m_currentDetailId;      // 当前详情/版本请求的项目 id（镜像回退后重试用）
    bool m_detailFallbackTried;     // 本次详情请求是否已经做过镜像回退

private slots:
    void onVersionsReplyFinished();
    void onProjectsReplyFinished();
    void onHashMatchReplyFinished();
    void onHashBatchReplyFinished();
    void onUpdateCheckReplyFinished();

private:
    QNetworkReply *m_currentHashMatchReply;
    QNetworkReply *m_currentHashBatchReply;
    QNetworkReply *m_currentUpdateCheckReply;
    QString m_pendingHashSha1;  // 当前正在哈希匹配的 SHA-1 值
};

#endif
