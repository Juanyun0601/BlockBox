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
     * @brief 获取基础URL
     */
    QString getBaseUrl() const;

    /**
     * @brief 获取Fabric版本列表
     * @param mcVersion Minecraft版本号
     * @return 版本信息列表
     */
    QList<FabricVersionInfo> getFabricVersions(const QString &mcVersion);

    /**
     * @brief 下载Fabric安装包
     * @param mcVersion Minecraft版本号
     * @param fabricVersion Fabric版本号
     * @param instancePath 实例路径
     */
    void downloadFabricInstaller(const QString &mcVersion, const QString &fabricVersion,
                                 const QString &instancePath);

    /**
     * @brief 安装Fabric
     * @param installerPath 安装包路径
     * @param instancePath 实例路径
     */
    void installFabric(const QString &installerPath, const QString &instancePath);

    /**
     * @brief 取消安装
     */
    void cancelInstall();

    /**
     * @brief 是否正在安装
     */
    bool isInstalling() const;

    /**
     * @brief 构建Fabric下载URL
     * @param mcVersion Minecraft版本号
     * @param fabricVersion Fabric版本号
     * @param hash 文件哈希值
     * @return 下载URL
     */
    QString getFabricDownloadUrl(const QString &mcVersion, const QString &fabricVersion,
                                const QString &hash);

    /**
     * @brief 设置当前任务ID
     */
    void setCurrentTaskId(const QString &taskId);

    /**
     * @brief 获取当前任务ID
     */
    QString currentTaskId() const;

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
     * @brief 生成Fabric版本JSON
     * @param installerPath 安装包路径
     * @param mcVersion Minecraft版本号
     * @param fabricVersion Fabric版本号
     * @return 版本JSON对象
     */
    QJsonObject generateVersionJson(const QString &installerPath,
                                   const QString &mcVersion,
                                   const QString &fabricVersion);

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
     * @brief 复制安装jar到版本目录
     * @param installerPath 安装包路径
     * @param versionPath 版本目录路径
     * @param fabricVersion Fabric版本号
     * @return 是否成功
     */
    bool copyInstallerJar(const QString &installerPath,
                         const QString &versionPath,
                         const QString &fabricVersion);

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
     * @brief 清理临时文件
     */
    void cleanupTempFiles();

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
    QString m_tempDir;

    qint64 m_totalBytes;
    qint64 m_downloadedBytes;
    QElapsedTimer m_speedTimer;

    static FabricInstaller *m_instance;
    static QMutex m_instanceMutex;
};
