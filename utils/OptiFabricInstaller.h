/**
 * @file   OptiFabricInstaller.h
 * @brief  OptiFabric安装器类定义
 * @author BlockBox Team
 * @date   2026-08-29
 */

#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

/**
 * @brief OptiFabric版本信息结构
 */
struct OptiFabricVersionInfo {
    QString version;
    QString mcVersion;
    QString downloadUrl;
    QString fileName;
    QString datePublished;
};

/**
 * @brief OptiFabric安装器类
 *
 * 负责下载和安装Minecraft OptiFabric模组
 * 从Modrinth API下载
 */
class OptiFabricInstaller : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     */
    static OptiFabricInstaller* instance();

    /**
     * @brief 获取OptiFabric版本列表
     * @param mcVersion Minecraft版本号
     * @return 版本信息列表
     */
    QList<OptiFabricVersionInfo> getOptiFabricVersions(const QString &mcVersion);

    /**
     * @brief 下载OptiFabric安装包
     * @param mcVersion Minecraft版本号
     * @param optiFabricVersion OptiFabric版本号
     * @param downloadUrl 下载URL
     * @param instancePath 实例路径
     */
    void downloadOptiFabric(const QString &mcVersion, const QString &optiFabricVersion,
                            const QString &downloadUrl, const QString &instancePath);

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
    OptiFabricInstaller();
    ~OptiFabricInstaller();

    OptiFabricInstaller(const OptiFabricInstaller&) = delete;
    OptiFabricInstaller& operator=(const OptiFabricInstaller&) = delete;

    /**
     * @brief 下载文件
     * @param url 下载URL
     * @param filePath 保存路径
     * @param redirectDepth 重定向深度
     * @return 是否成功
     */
    bool downloadFile(const QString &url, const QString &filePath, int redirectDepth = 0);

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

    // 成员变量
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;
    bool m_isInstalling;
    bool m_isCancelled;

    QString m_currentMcVersion;
    QString m_currentOptiFabricVersion;
    QString m_currentInstancePath;
    QString m_currentTaskId;
    QString m_tempDir;

    qint64 m_totalBytes;
    qint64 m_downloadedBytes;
    QElapsedTimer m_speedTimer;

    static OptiFabricInstaller *m_instance;
    static QMutex m_instanceMutex;
};
