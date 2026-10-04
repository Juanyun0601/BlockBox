/**
 * @file   BedrockVersionService.h
 * @brief  基岩版版本信息服务 — 多源（mcappx / mcapks）版本与下载链接获取
 * @author BlockBox Team
 *
 * 基岩版（Minecraft Bedrock）没有统一的官方下载 API，本项目聚合多个第三方源：
 *   1. mcappx.com   ——  Windows UWP（APPX/MSIX）安装包直链数据库
 *                       https://data.mcappx.com/v2/bedrock.json
 *                       请求需携带 User-Agent: mcappx_developer（参考 XMCL 开源实现）
 *   2. mcapks.net   ——  Android APK 版本列表 + 网盘下载链接
 *                       /api/get-vslist.php    （版本列表）
 *                       /api/get-download.php  （网盘链接，夸克/百度/123/迅雷/UC）
 *   3. bbk.endyun.ltd —— Android APK 版本库（MC版本库）
 *                       POST /api/get_version  （按主版本家族拉取，form-urlencoded: v=1.21.x&b=2）
 *                       返回含 123盘/夸克/UC/OneDrive 等网盘链接
 *
 * 各类源返回结构差异较大，本类统一为 BedrockVersionEntry 模型，便于页面展示。
 */

#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

/// 基岩版版本类型
enum class BedrockVersionType {
    Release, ///< 正式版
    Beta,    ///< 测试版
    Preview  ///< 预览版
};

/// 网盘下载链接（mcapks 源：夸克/百度/123/迅雷/UC 等）
struct BedrockCloudLink
{
    QString name;     ///< 网盘名称
    QString url;      ///< 链接地址
    QString password; ///< 提取码（可能为空）
};

/// 基岩版版本条目
struct BedrockVersionEntry
{
    QString version;              ///< 版本号（如 1.21.101.1 / 26.50.25）
    QString source;               ///< 来源标识："mcappx" / "mcapks"
    BedrockVersionType type;      ///< 版本类型
    QString date;                 ///< 发布日期（YYYY-MM-DD）
    QString size;                 ///< 文件大小描述（mcapks 源提供）

    bool hasDirectPackage = false; ///< mcappx：是否存在可直接下载的安装包
    QString packageUrl;            ///< mcappx：安装包直链（.msixvc）
    QString packageArch;           ///< mcappx：安装包架构（x64 / neutral ...）

    QVector<BedrockCloudLink> cloudLinks; ///< mcapks：网盘下载链接
};

class BedrockVersionService : public QObject
{
    Q_OBJECT

public:
    static BedrockVersionService* instance();

    /**
     * @brief 并行拉取两个源的版本列表，完成后统一发射 versionListFetched
     */
    void fetchVersionList();

    /**
     * @brief 拉取指定版本的网盘下载链接（仅 mcapks 源）
     */
    void fetchDownloadLinks(const QString &version);

    /// 源显示名（用于错误提示）
    static QString sourceDisplayName(const QString &source);

signals:
    /// 全部可用源拉取完成后发射（合并、去重、按版本号降序）
    void versionListFetched(const QVector<BedrockVersionEntry> &versions);
    /// 单个源拉取失败（sourceName 为源显示名）
    void fetchError(const QString &sourceName, const QString &error);
    /// mcapks 网盘链接拉取完成
    void downloadLinksFetched(const QString &version, const QVector<BedrockCloudLink> &links);
    /// mcapks 网盘链接拉取失败
    void downloadLinksFailed(const QString &version, const QString &error);

private:
    explicit BedrockVersionService(QObject *parent = nullptr);
    ~BedrockVersionService() override;

    void fetchMcappxList();
    void fetchMcapksList();
    void fetchBbkList();

    void handleMcappxReply(QNetworkReply *reply);
    void handleMcapksListReply(QNetworkReply *reply);
    void handleLinksReply(QNetworkReply *reply, const QString &version);
    void handleBbkReply(QNetworkReply *reply);

    QVector<BedrockVersionEntry> parseMcappx(const QByteArray &data);
    QVector<BedrockVersionEntry> parseMcapks(const QByteArray &data);
    QVector<BedrockVersionEntry> parseBbk(const QByteArray &data);
    void emitCombined();

    /// 看门狗超时：强制结束仍未完成的源，避免界面一直停留在"获取中"
    void onFetchWatchdogTimeout();

    /// 版本号比较（1.21.101.1 < 26.50.25，按数字段比较）
    static bool versionLess(const QString &a, const QString &b);

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_mcappxReply;
    QNetworkReply *m_mcapksReply;
    QNetworkReply *m_linksReply;

    QVector<BedrockVersionEntry> m_mcappxEntries;
    QVector<BedrockVersionEntry> m_mcapksEntries;
    QVector<BedrockVersionEntry> m_bbkEntries;
    QVector<QNetworkReply *> m_bbkReplies; ///< bbk 各主版本家族的并行请求
    int m_bbkPending;                      ///< bbk 剩余未完成请求数
    QTimer *m_fetchWatchdog;               ///< 拉取看门狗（防卡死）
    bool m_listEmitted;                    ///< 本次拉取是否已发射过 versionListFetched
    bool m_mcappxDone;
    bool m_mcapksDone;
    bool m_bbkDone;
    bool m_mcappxOk;
    bool m_mcapksOk;
    bool m_bbkOk;
    QString m_mcappxError;
    QString m_mcapksError;
    QString m_bbkError;
};
