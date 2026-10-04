/**
 * @file   NeoForgeInstaller.h
 * @brief  NeoForge安装器类定义
 * @author BlockBox Team
 * @date   2026-05-29
 */

#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QString>

#include "SettingsManager.h"

/**
 * @brief NeoForge安装器类
 *
 * 负责下载和安装Minecraft NeoForge模组加载器
 * 支持官方源和BMCLAPI镜像源
 */
class NeoForgeInstaller : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     */
    static NeoForgeInstaller* instance();

    /**
     * @brief 设置下载源
     */
    void setDownloadSource(NeoForgeDownloadSource source);

    /**
     * @brief 获取当前下载源
     */
    NeoForgeDownloadSource downloadSource() const;

    /**
     * @brief 下载NeoForge安装包
     * @param mcVersion Minecraft版本号
     * @param forgeVersion NeoForge版本号
     * @param instancePath 实例路径
     * @param instanceName 实例名称（可选）
     */
    void downloadNeoForgeInstaller(const QString &mcVersion, const QString &forgeVersion,
                                   const QString &instancePath, const QString &instanceName = QString());

    /**
     * @brief 安装NeoForge
     * @param installerPath 安装包路径
     * @param instancePath 实例路径
     */
    void installNeoForge(const QString &installerPath, const QString &instancePath);

    /**
     * @brief 取消安装
     */
    void cancelInstall();

    /**
     * @brief 是否正在安装
     */
    bool isInstalling() const;

    /**
     * @brief 构建NeoForge下载URL
     * @param forgeVersion NeoForge版本号
     * @return 下载URL
     */
    QString getNeoForgeDownloadUrl(const QString &forgeVersion);

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
    NeoForgeInstaller();
    ~NeoForgeInstaller();

    NeoForgeInstaller(const NeoForgeInstaller&) = delete;
    NeoForgeInstaller& operator=(const NeoForgeInstaller&) = delete;

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

    QNetworkAccessManager *m_networkManager;
    NeoForgeDownloadSource m_downloadSource;
    QNetworkReply *m_currentReply;
    QProcess *m_currentProcess;
    bool m_isInstalling;
    bool m_isCancelled;
    bool m_isDownloadPhase;

    QString m_currentMcVersion;
    QString m_currentForgeVersion;
    QString m_currentInstancePath;
    QString m_currentTaskId;
    QString m_tempDir;

    qint64 m_totalBytes;
    qint64 m_downloadedBytes;
    QElapsedTimer m_speedTimer;

    static NeoForgeInstaller *m_instance;
    static QMutex m_instanceMutex;
};