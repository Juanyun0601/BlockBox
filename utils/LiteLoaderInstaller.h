/**
 * @file   LiteLoaderInstaller.h
 * @brief  LiteLoader安装器类定义
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
 * @brief LiteLoader版本信息结构
 */
struct LiteLoaderVersionInfo {
    QString version;
    QString mcVersion;
    QString downloadUrl;
    QString md5;
};

/**
 * @brief LiteLoader安装器类
 *
 * 负责下载和安装Minecraft LiteLoader模组加载器
 * 支持官方源下载
 */
class LiteLoaderInstaller : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     */
    static LiteLoaderInstaller* instance();

    /**
     * @brief 获取LiteLoader版本列表
     * @param mcVersion Minecraft版本号
     * @return 版本信息列表
     */
    QList<LiteLoaderVersionInfo> getLiteLoaderVersions(const QString &mcVersion);

    /**
     * @brief 下载LiteLoader安装包
     * @param mcVersion Minecraft版本号
     * @param liteLoaderVersion LiteLoader版本号
     * @param instancePath 实例路径
     */
    void downloadLiteLoader(const QString &mcVersion, const QString &liteLoaderVersion,
                            const QString &instancePath);

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
    LiteLoaderInstaller();
    ~LiteLoaderInstaller();

    LiteLoaderInstaller(const LiteLoaderInstaller&) = delete;
    LiteLoaderInstaller& operator=(const LiteLoaderInstaller&) = delete;

    /**
     * @brief 下载文件
     * @param url 下载URL
     * @param filePath 保存路径
     * @param expectedMd5 预期MD5值（可选）
     * @param redirectDepth 重定向深度
     * @return 是否成功
     */
    bool downloadFile(const QString &url, const QString &filePath,
                     const QString &expectedMd5 = QString(), int redirectDepth = 0);

    /**
     * @brief 计算文件的MD5值
     * @param filePath 文件路径
     * @return MD5哈希值
     */
    QString calculateMd5(const QString &filePath);

    /**
     * @brief 验证文件MD5
     * @param filePath 文件路径
     * @param expectedMd5 预期MD5值
     * @return 是否匹配
     */
    bool verifyMd5(const QString &filePath, const QString &expectedMd5);

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
    QString m_currentLiteLoaderVersion;
    QString m_currentInstancePath;
    QString m_currentTaskId;
    QString m_tempDir;

    qint64 m_totalBytes;
    qint64 m_downloadedBytes;
    QElapsedTimer m_speedTimer;

    static LiteLoaderInstaller *m_instance;
    static QMutex m_instanceMutex;
};
