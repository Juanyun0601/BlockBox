/**
 * @file   OptiFineInstaller.h
 * @brief  OptiFine安装器类定义
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
 * @brief OptiFine安装器类
 *
 * 负责下载和安装Minecraft OptiFine优化模组
 * 支持官方源和BMCLAPI镜像源
 */
class OptiFineInstaller : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     */
    static OptiFineInstaller* instance();

    /**
     * @brief 设置下载源
     */
    void setDownloadSource(OptiFineDownloadSource source);

    /**
     * @brief 获取当前下载源
     */
    OptiFineDownloadSource downloadSource() const;

    /**
     * @brief 获取基础URL
     */
    QString getBaseUrl() const;

    /**
     * @brief 下载OptiFine安装包
     * @param mcVersion Minecraft版本号
     * @param optiFineVersion OptiFine版本号（如 "HD_U_G5"）
     * @param instancePath 实例路径
     */
    void downloadOptiFineInstaller(const QString &mcVersion, const QString &optiFineVersion,
                                   const QString &installerFileName, const QString &instancePath);

    /**
     * @brief 安装OptiFine
     * @param installerPath 安装包路径
     * @param instancePath 实例路径
     */
    void installOptiFine(const QString &installerPath, const QString &instancePath);

    /**
     * @brief 取消安装
     */
    void cancelInstall();

    /**
     * @brief 是否正在安装
     */
    bool isInstalling() const;

    /**
     * @brief 构建OptiFine下载URL
     * @param mcVersion Minecraft版本号
     * @param type OptiFine类型（如 "HD_U"）
     * @param patch 补丁版本（如 "G5"）
     * @return 下载URL
     */
    QString getOptiFineDownloadUrl(const QString &mcVersion, const QString &type,
                                   const QString &patch);

    /**
     * @brief 规范化MC版本号
     * @param mcVersion Minecraft版本号
     * @return 规范化后的版本号
     */
    QString normalizeMcVersion(const QString &mcVersion);

    /**
     * @brief 解析官方下载URL
     * @param fileName OptiFine文件名
     * @return 真实下载URL
     */
    QString resolveOfficialDownloadUrl(const QString &fileName);

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

private:
    OptiFineInstaller();
    ~OptiFineInstaller();

    OptiFineInstaller(const OptiFineInstaller&) = delete;
    OptiFineInstaller& operator=(const OptiFineInstaller&) = delete;

    /**
     * @brief 运行OptiFine修补器
     * @param installerPath 安装包路径
     * @param gameJarPath 游戏JAR路径
     * @param outputLibPath 输出库路径
     * @return 是否成功
     */
    bool runOptiFinePatcher(const QString &installerPath, const QString &gameJarPath,
                            const QString &outputLibPath);

    /**
     * @brief 生成版本JSON
     * @param mcVersion Minecraft版本号
     * @param type OptiFine类型
     * @param patch 补丁版本
     * @param launchWrapperVersion LaunchWrapper版本
     * @return 版本JSON对象
     */
    QJsonObject generateVersionJson(const QString &mcVersion, const QString &type,
                                    const QString &patch, const QString &launchWrapperVersion);

    /**
     * @brief 创建版本目录
     * @param instancePath 实例路径
     * @param mcVersion Minecraft版本号
     * @param type OptiFine类型
     * @param patch 补丁版本
     * @return 版本目录路径
     */
    QString createVersionDirectory(const QString &instancePath, const QString &mcVersion,
                                   const QString &type, const QString &patch);

    /**
     * @brief 复制LaunchWrapper库
     * @param installerPath 安装包路径
     * @param instancePath 实例路径
     * @param launchWrapperVersion LaunchWrapper版本
     */
    void copyLaunchWrapper(const QString &installerPath, const QString &instancePath,
                          const QString &launchWrapperVersion);

    /**
     * @brief 复制OptiFine库
     * @param installerPath 安装包路径
     * @param instancePath 实例路径
     * @param mcVersion Minecraft版本号
     * @param type OptiFine类型
     * @param patch 补丁版本
     */
    void copyOptiFineLibrary(const QString &installerPath, const QString &instancePath,
                             const QString &mcVersion, const QString &type, const QString &patch);

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
     * @brief 从JAR中通过jar工具读取文件内容
     * @param jarPath JAR文件路径
     * @param entryPath 条目路径
     * @return 文件内容
     */
    QString readJarEntry(const QString &jarPath, const QString &entryPath);

    /**
     * @brief 从JAR中解压文件到目标路径
     * @param jarPath JAR文件路径
     * @param entryPath 条目路径
     * @param destPath 目标路径
     * @return 是否成功
     */
    bool extractJarEntry(const QString &jarPath, const QString &entryPath, const QString &destPath);

    /**
     * @brief 检查JAR中是否存在指定条目
     * @param jarPath JAR文件路径
     * @param entryPath 条目路径
     * @return 是否存在
     */
    bool jarEntryExists(const QString &jarPath, const QString &entryPath);

    // 成员变量
    QNetworkAccessManager *m_networkManager;
    OptiFineDownloadSource m_downloadSource;
    QNetworkReply *m_currentReply;
    bool m_isInstalling;
    bool m_isCancelled;

    QString m_currentMcVersion;
    QString m_currentOptiFineVersion;
    QString m_currentType;
    QString m_currentPatch;
    QString m_currentInstancePath;
    QString m_currentTaskId;
    QString m_tempDir;
    QString m_installerFileName;

    qint64 m_totalBytes;
    qint64 m_downloadedBytes;
    QElapsedTimer m_speedTimer;

    static OptiFineInstaller *m_instance;
    static QMutex m_instanceMutex;
};