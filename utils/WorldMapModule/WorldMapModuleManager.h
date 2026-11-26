/**
 * @file   WorldMapModuleManager.h
 * @brief  存档地图模块（CDN 按需下载）的下载、校验、解压与运行时加载
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 存档地图不随安装包分发。桌面端第一次打开地图页时，本类负责：
 *   1. 读取本地标记，已装好就直接 QLibrary 加载（不联网）；
 *   2. 没装过就拉取官网 CDN 上的模块清单（cdn.mcblockbox.top 优先，
 *      官网源站兜底），按当前系统架构选包；
 *   3. 下载 zip → 校验 SHA-256 → 解压 → 加载；
 *   4. 成功后写 module.json 标记，下次直接加载。
 *
 * 清单路径按启动器版本分目录：
 *   /assets/modules/worldmap/<启动器版本>/manifest.json
 * 这样启动器更新后不会误用旧模块（模块和主程序由同一套 Qt/编译器构建，
 * 只能一对一使用）。
 *
 * 调试 / 离线场景可用环境变量跳过网络：
 *   BLOCKBOX_WORLDMAP_MODULE    直接指定模块动态库路径
 *   BLOCKBOX_WORLDMAP_DIR       覆盖模块安装目录
 *   BLOCKBOX_WORLDMAP_MANIFEST  覆盖清单 URL（本地测试服务）
 */
#ifndef WORLDMAPMODULEMANAGER_H
#define WORLDMAPMODULEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QWidget;

class WorldMapModuleManager : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Idle,        ///< 尚未开始
        Preparing,   ///< 读本地标记 / 拉清单
        Downloading, ///< 下载模块压缩包
        Verifying,   ///< 校验 SHA-256
        Extracting,  ///< 解压到模块目录
        Loading,     ///< 加载动态库并检查接口版本
        Ready,       ///< 可用
        Failed       ///< 失败（errorString 里有面向用户的原因）
    };
    Q_ENUM(State)

    static WorldMapModuleManager *instance();

    /** 当前平台是否支持运行时下载模块（仅桌面端构建开启） */
    static bool isDownloadSupported();
    /** 当前系统架构对应的下载包标识，如 windows-x64 */
    static QString abiTag();
    /** 模块安装根目录（<数据目录>/modules/worldmap） */
    static QString installRoot();

    State state() const { return m_state; }
    bool isReady() const { return m_state == State::Ready; }
    bool isBusy() const;
    QString errorString() const { return m_error; }
    QString moduleVersion() const { return m_moduleVersion; }
    qint64 bytesReceived() const { return m_received; }
    qint64 bytesTotal() const { return m_total; }

    /**
     * @brief 确保模块可用（幂等；本地已装则直接加载，不联网）
     *
     * 完成/失败都会通过 ready() / failed() 通知，状态变化走 stateChanged()。
     */
    void ensureReady();

    /** 失败后重试（清掉错误与半成品重新走一遍） */
    void retry();

    /**
     * @brief 创建地图页（仅 Ready 时可用）
     * @param parent 父窗口
     * @return 地图页；模块未就绪或创建失败返回 nullptr
     */
    QWidget *createPage(QWidget *parent);

signals:
    void stateChanged(WorldMapModuleManager::State state);
    void progressChanged(qint64 received, qint64 total);
    void ready();
    void failed(const QString &message);

private:
    explicit WorldMapModuleManager(QObject *parent = nullptr);
    ~WorldMapModuleManager() override;

    void setState(State state);
    void failWith(const QString &message);

    bool tryLoadLocal();
    bool loadLibraryFile(const QString &path);
    bool readMarker(QString *moduleVersion, QString *abi, QString *sha256,
                    QString *libraryName) const;
    void writeMarker(const QString &moduleVersion, const QString &sha256,
                     const QString &libraryName) const;
    void clearMarker() const;

    void startManifestRequest();
    void requestNextManifest();
    void handleManifestReply(QNetworkReply *reply);
    void beginDownload(const QString &moduleVersion, const QString &zipUrl,
                       const QString &sha256, qint64 expectedSize,
                       const QString &libraryName);
    void handleDownloadFinished();
    void finishWithLibrary(const QString &libraryPath, const QString &moduleVersion,
                           const QString &sha256, const QString &libraryName);
    QString abiLibraryName() const;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    State m_state = State::Idle;
    QString m_error;
    QString m_moduleVersion;

    QStringList m_manifestUrls;
    int m_manifestIndex = 0;
    int m_lastManifestStatus = 0; ///< 最近一次清单请求的 HTTP 状态码（区分 404 与断网）

    QString m_zipUrl;
    QString m_zipPath;
    QString m_pendingSha;
    QString m_pendingLibrary;
    qint64 m_expectedSize = 0;
    qint64 m_received = 0;
    qint64 m_total = 0;

    class QLibrary *m_library = nullptr;
    typedef QWidget *(*CreatePageFn)(QWidget *parent);
    CreatePageFn m_createFn = nullptr;
};

#endif // WORLDMAPMODULEMANAGER_H
