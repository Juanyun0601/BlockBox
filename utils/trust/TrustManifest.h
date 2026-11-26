/**
 * @file   TrustManifest.h
 * @brief  可信分发公示清单：拉取 / 缓存 / 解析 builds、packages 与安装目录文件清单
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 数据契约见仓库根目录 .tmp/trust-spec.md（官网侧与启动器侧共用，字段名不可单方面改动）：
 *   - 公示清单  https://cdn.mcblockbox.top/trust/manifest.json（CDN 优先）
 *               https://mcblockbox.top/trust/manifest.json（官网兜底）
 *   - 文件清单  manifest 里每个 build 的 filesManifest 地址（缺省时按
 *               /trust/files/<version>-<platform>-<arch>.json 拼）
 *
 * 本类只做「数据 + 网络」：不弹窗、不碰界面，也不做文件哈希比对
 * （比对交给 SelfIntegrityChecker）。拉到的数据会缓存到
 * Platform::getDataDirectory()/trust/ 下，断网时用上次的缓存兜底。
 *
 * 线程约定：实例与信号都在主线程（QNetworkAccessManager 只能主线程用），
 * 但解析后的纯数据结构可以随便拷到后台线程去比对。
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QNetworkAccessManager;

/** 一个安装包条目（公示哈希，可选数字签名信息） */
struct TrustPackage
{
    QString name;              ///< 文件名，如 BlockBox-beta9.1-win64-setup.exe
    qint64 size = 0;           ///< 字节数，0 表示未公示
    QString sha256;            ///< 64 位小写十六进制，空表示未公示
    QString url;               ///< 直链，可为空串（只公示哈希）
    bool hasSignature = false; ///< signature 字段是否是个对象（未签名时为 false）
    QString signatureType;     ///< authenticode 等
    QString signatureStatus;   ///< valid / invalid / unsigned …
    QString signatureSubject;  ///< 签名主体
    QString signatureIssuer;   ///< 颁发者
    QString signatureThumbprint; ///< 指纹（大写无空格）

    bool hasSha256() const { return !sha256.isEmpty(); }
};

/** 一个构建条目（版本 + 平台 + 架构） */
struct TrustBuild
{
    QString version;          ///< beta9.1
    QString channel;          ///< stable / beta
    QString platform;         ///< windows / linux / android / harmony / macos
    QString arch;             ///< x64 / arm64 / x86 / arm64-v8a …
    bool official = false;    ///< 是否官方签名构建
    QString publishedAt;
    QString filesManifestUrl; ///< 安装目录文件清单地址，可为空
    QVector<TrustPackage> packages;

    bool isValid() const { return !version.isEmpty(); }
};

/** 文件清单里的一条：相对程序目录的路径 + 大小 + 哈希 */
struct TrustFileEntry
{
    QString path;   ///< 相对 QCoreApplication::applicationDirPath()，正斜杠
    qint64 size = 0;
    QString sha256; ///< 小写十六进制，空表示只公示了文件名
};

/** 某次构建的安装目录文件清单 */
struct TrustFileManifest
{
    QString version;
    QString platform;
    QString arch;
    QString generatedAt;
    QVector<TrustFileEntry> files;

    bool isValid() const { return !files.isEmpty(); }
};

class TrustManifest : public QObject
{
    Q_OBJECT

public:
    /** 文件清单回调：ok=false 时 manifest 为空，error 可直接展示 */
    using FilesHandler =
        std::function<void(bool ok, const TrustFileManifest &manifest, const QString &error)>;

    static TrustManifest *instance();

    /** 可信校验公示页（浏览器打开用） */
    static QString trustPageUrl();
    /** 缓存目录：数据目录/trust（不存在时由本类创建） */
    static QString cacheDir();
    /** 当前平台键：windows / linux / android / harmony / macos */
    static QString platformKey();
    /** 当前架构键：x64 / arm64 / x86 / arm64-v8a / x86_64 */
    static QString archKey();
    /** 公示清单候选地址：CDN 优先、官网兜底 */
    static QStringList manifestUrlCandidates();
    /** 把清单里的路径统一成正斜杠、去掉开头的 ./ 与 / */
    static QString normalizePath(const QString &path);

    /** 是否正在拉取公示清单 */
    bool isLoading() const { return m_loading; }
    /** 最近一次拉取失败的原因（成功时为空） */
    QString lastError() const { return m_lastError; }
    /** 全部构建条目（缓存 + 网络合并后的结果） */
    const QVector<TrustBuild> &builds() const { return m_builds; }
    /** 数据来源：网络地址或本地缓存文件（用于排查，不展示给用户） */
    QString sourceUrl() const { return m_sourceUrl; }
    /** 清单里的 updated 时间（官网侧生成时刻，可为空） */
    QString updatedAt() const { return m_updatedAt; }

    /** 当前版本 + 当前平台的构建条目（找不到时 isValid() 为 false） */
    TrustBuild currentBuild() const;
    bool hasCurrentBuild() const { return currentBuild().isValid(); }
    /** 当前平台主安装包的 SHA-256（小写十六进制，未公示时为空） */
    QString currentPackageSha256() const;
    /** 当前平台主安装包文件名（未公示时为空） */
    QString currentPackageName() const;

    /**
     * @brief 拉取公示清单
     * @param forceNetwork true 时即使本次已经拉过也重新请求
     *
     * 第一次调用会先把磁盘缓存读进来（保证界面立即有数据），再走网络；
     * 网络成功后覆盖内存数据、刷新缓存并发出 manifestUpdated()。
     */
    void refresh(bool forceNetwork = false);

    /**
     * @brief 异步取「当前版本」的安装目录文件清单
     *
     * 网络优先（CDN → 官网 → manifest 里给的其他地址），失败回退上次缓存；
     * 两者都没有时回调 ok=false。回调保证在主线程执行一次。
     */
    void requestCurrentFilesManifest(FilesHandler handler);

    /** 解析公示清单 JSON（静态，便于离线自测；字段缺失不报错，按缺省处理） */
    static bool parseManifest(const QByteArray &raw, QVector<TrustBuild> *builds,
                              QString *updatedAt, QString *error);
    /** 解析文件清单 JSON */
    static bool parseFilesManifest(const QByteArray &raw, TrustFileManifest *manifest,
                                   QString *error);

signals:
    /** 清单数据有更新（读到缓存或网络拉取成功） */
    void manifestUpdated();
    /** 所有候选地址都失败（此时内存里可能还有上次的缓存数据） */
    void manifestFetchFailed(const QString &error);

private:
    explicit TrustManifest(QObject *parent = nullptr);

    void loadCache();
    void fetchFromNetwork(int candidateIndex);
    static QString filesCachePath(const TrustBuild &build);
    static QStringList filesUrlCandidates(const TrustBuild &build);

    QNetworkAccessManager *m_network = nullptr;
    QVector<TrustBuild> m_builds;
    QString m_updatedAt;
    QString m_lastError;
    QString m_sourceUrl;
    bool m_loading = false;
    bool m_cacheLoaded = false;
    bool m_networkFetched = false;
    QHash<QString, TrustFileManifest> m_filesCache; ///< 本次运行已解析的文件清单
};
