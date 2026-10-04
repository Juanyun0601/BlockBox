/**
 * @file   FabricInstaller.h
 * @brief  Fabric安装器类定义
 * @author BlockBox Team
 * @date   2026-05-28
 */

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QString>

#include <functional>

#include "utils/SettingsManager.h"

/**
 * @brief Fabric版本信息结构
 */
struct FabricVersionInfo {
    QString fabricVersion;
    QString minecraftVersion;
    QString hash;
    QString launcherMetaVersion;
    QString build;
    QString intermediaryMaven;      // net.fabricmc:intermediary:<mc>（版本条目自带）
    QJsonObject launcherMeta;       // Meta 返回的 launcherMeta（libraries/mainClass）
};

/**
 * @brief Fabric安装器类
 *
 * 负责下载和安装Minecraft Fabric模组加载器
 * 支持官方源和BMCLAPI镜像源
 */
class FabricInstaller : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     */
    static FabricInstaller* instance();

    /**
     * @brief 设置下载源
     */
    void setDownloadSource(FabricDownloadSource source);

    /**
     * @brief 获取当前下载源
     */
    FabricDownloadSource downloadSource() const;

    /**
     * @brief 获取Fabric Meta基础URL（版本列表/profile json）
     *        注意: BMCLAPI 的 Fabric Meta 镜像位于 /fabric-meta 前缀下
     */
    QString getBaseUrl() const;

    /**
     * @brief 获取 Maven 仓库基础URL（Fabric 运行库下载地址前缀）
     */
    QString getMavenBaseUrl() const;

    /**
     * @brief 获取Fabric版本列表
     * @param mcVersion Minecraft版本号
     * @return 版本信息列表
     */
    QList<FabricVersionInfo> getFabricVersions(const QString &mcVersion);

    /**
     * @brief 下载并安装Fabric（获取版本信息 → 生成版本JSON → 下载运行库）
     * @param mcVersion Minecraft版本号
     * @param fabricVersion Fabric版本号
     * @param instancePath 实例路径（.minecraft 根目录）
     */
    void downloadFabricInstaller(const QString &mcVersion, const QString &fabricVersion,
                                 const QString &instancePath);

    /**
     * @brief 安装Fabric
     * @param version 目标Fabric版本信息（需含 launcherMeta）
     * @param instancePath 实例路径
     */
    void installFabric(const FabricVersionInfo &version, const QString &instancePath);

    /**
     * @brief 取消安装
     */
    void cancelInstall();

    /**
     * @brief 是否正在安装
     */
    bool isInstalling() const;

    /**
     * @brief 设置当前任务ID
     */
    void setCurrentTaskId(const QString &taskId);

    /**
     * @brief 获取当前任务ID
     */
    QString currentTaskId() const;

    /**
     * @brief 检测并修复旧版安装器生成的残缺 Fabric 版本 JSON
     *
     * 旧版安装器生成的版本 JSON 缺少 Fabric 运行必需的库（intermediary 映射、
     * sponge-mixin、ASM），启动 Knot 时必然崩溃。本方法检测该情况后从 Fabric
     * Meta 拉取官方 profile 数据重写库列表，并同步下载缺失的库文件。
     * 对完整/非 Fabric 版本不做任何改动；失败也不阻塞启动流程。
     *
     * @param instancePath 版本目录路径（versions/<id>/）
     * @param log 日志回调（可为空）
     * @return 版本 JSON 是否完整（原样完整或已修复）
     */
    static bool repairIncompleteVersionJson(const QString &instancePath,
                                            const std::function<void(const QString &)> &log = nullptr);

signals:
    /**
     * @brief 下载进度信号
     * @param bytesReceived 已接收字节数
     * @param bytesTotal 总字节数
     */
    void downloadProgressUpdated(qint64 bytesReceived, qint64 bytesTotal);

    /**
     * @brief 安装进度信号
     * @param progress 进度百分比
     * @param status 当前状态描述
     */
    void installProgressUpdated(int progress, const QString &status);

    /**
     * @brief 安装完成信号
     * @param versionId 安装完成的版本ID
     */
    void installCompleted(const QString &versionId);

    /**
     * @brief 安装失败信号
     * @param error 错误信息
     */
    void installFailed(const QString &error);

    /**
     * @brief 安装取消信号
     */
    void installCancelled();

    /**
     * @brief 状态变化信号
     * @param status 新状态描述
     */
    void statusChanged(const QString &status);

    /**
     * @brief 获取版本列表完成信号
     * @param versions 版本列表
     */
    void versionListFetched(const QList<FabricVersionInfo> &versions);

    /**
     * @brief 获取版本列表失败信号
     * @param error 错误信息
     */
    void versionListFetchFailed(const QString &error);

private:
    FabricInstaller();
    ~FabricInstaller();

    FabricInstaller(const FabricInstaller&) = delete;
    FabricInstaller& operator=(const FabricInstaller&) = delete;

    /**
     * @brief 依据 Meta 版本条目的 launcherMeta 生成 Fabric 版本 JSON
     *
     * 结构与官方 /profile/json 一致: id/inheritsFrom/mainClass/libraries，
     * 不写 arguments —— 启动时由 mergeInheritsFromJson 继承原版完整的 jvm/game 参数。
     *
     * @param version Meta 版本条目（需含 launcherMeta）
     * @return 版本JSON对象
     */
    QJsonObject generateVersionJson(const FabricVersionInfo &version);

    /**
     * @brief 创建版本目录结构
     * @param instancePath 实例路径
     * @param mcVersion Minecraft版本号
     * @param fabricVersion Fabric版本号
     * @return 版本目录路径
     */
    QString createVersionDirectory(const QString &instancePath,
                                  const QString &mcVersion,
                                  const QString &fabricVersion);

    /**
     * @brief 下载版本 JSON 中的全部 Fabric 运行库到 libraries 目录
     *        （首次启动会跳过通用文件补全，库文件必须在安装时就绪）
     * @param versionJson 版本JSON对象
     * @param librariesPath 库目录路径
     * @param failed 输出下载失败的库名列表
     * @return 是否全部成功
     */
    bool downloadLibraries(const QJsonObject &versionJson, const QString &librariesPath,
                           QStringList *failed);

    /**
     * @brief 下载文件
     * @param url 下载URL
     * @param filePath 保存路径
     * @param expectedSha1 预期SHA1值（可选）
     * @param redirectDepth 重定向深度
     * @return 是否成功
     */
    bool downloadFile(const QString &url, const QString &filePath,
                     const QString &expectedSha1 = QString(), int redirectDepth = 0);

    /**
     * @brief 抓取 URL 内容到内存（用于 Meta 接口）
     */
    QByteArray fetchUrl(const QString &url, bool *ok);

    /**
     * @brief 计算文件的SHA1值
     * @param filePath 文件路径
     * @return SHA1哈希值
     */
    QString calculateSha1(const QString &filePath);

    /**
     * @brief 验证文件SHA1
     * @param filePath 文件路径
     * @param expectedSha1 预期SHA1值
     * @return 是否匹配
     */
    bool verifySha1(const QString &filePath, const QString &expectedSha1);

    /**
     * @brief 更新任务进度
     */
    void updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal);

    /**
     * @brief 更新任务状态
     */
    void updateTaskStatus(const QString &status);

    /**
     * @brief 获取Java可执行文件路径
     */
    QString getJavaPath() const;

    /**
     * @brief 获取Minecraft库目录
     */
    QString getMinecraftLibrariesPath(const QString &instancePath) const;

    /**
     * @brief 解析Fabric版本JSON响应
     * @param jsonResponse JSON响应字符串
     * @return 版本信息列表
     */
    QList<FabricVersionInfo> parseVersionList(const QString &jsonResponse);

    /**
     * @brief Maven 坐标 → 仓库相对路径
     *        "net.fabricmc:fabric-loader:0.15.11" → "net/fabricmc/fabric-loader/0.15.11/fabric-loader-0.15.11.jar"
     */
    static QString mavenArtifactPath(const QString &mavenName);

    /**
     * @brief 将 Meta 格式的库条目（name/url/校验和）转换为 Mojang 标准
     *        downloads.artifact 格式，便于启动器的文件补全与 classpath 构建识别
     */
    static QJsonObject libraryToMojangFormat(const QJsonObject &fabricLib, const QString &mavenBaseUrl);

    // 成员变量
    QNetworkAccessManager *m_networkManager;
    FabricDownloadSource m_downloadSource;
    QNetworkReply *m_currentReply;
    bool m_isInstalling;
    bool m_isCancelled;

    QString m_currentMcVersion;
    QString m_currentFabricVersion;
    QString m_currentInstancePath;
    QString m_currentTaskId;

    qint64 m_totalBytes;
    qint64 m_downloadedBytes;
    QElapsedTimer m_speedTimer;

    static FabricInstaller *m_instance;
    static QMutex m_instanceMutex;
};
