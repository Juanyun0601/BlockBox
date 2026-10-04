/**
 * @file   SystemTools.h
 * @brief  系统工具类 - 提供内存优化、磁盘清理、定时关机、多线程下载等系统工具
 * @author BlockBox Team
 * @date   2026-06-27
 */

#pragma once

#include <QObject>
#include <QString>
#include <QWidget>
#include <QVector>

class QNetworkAccessManager;
class MultiThreadDownloader;

/**
 * @brief 清理类别信息
 */
struct CleanCategory
{
    QString id;          // 唯一标识
    QString displayName; // 显示名称
    QString description; // 描述
    qint64 estimatedSize; // 预估大小（字节），-1 表示未知
    bool checked;        // 默认是否选中
};

/**
 * @brief 系统工具类
 *
 * 提供一系列系统级工具功能，包括：
 *   - 运行内存优化（释放工作集）
 *   - 硬盘临时文件清理（支持用户选择清理项）
 *   - 定时关机
 *   - 多线程下载
 */
class SystemTools : public QObject
{
    Q_OBJECT

public:
    explicit SystemTools(QObject* parent = nullptr);
    ~SystemTools();

    /**
     * @brief 获取单例实例
     */
    static SystemTools* instance();

    /**
     * @brief 执行内存优化
     *
     * 通过 Windows API EmptyWorkingSet 释放当前进程的工作集，
     * 同时清理系统缓存以释放更多可用内存。
     *
     * @param parentWidget 用于显示消息对话框的父控件（可选）
     */
    void optimizeMemory(QWidget* parentWidget = nullptr);

    /**
     * @brief 打开磁盘清理对话框（支持用户选择清理项）
     *
     * 显示一个对话框列出所有可清理的类别，用户勾选后执行清理。
     *
     * @param parentWidget 父控件
     */
    void showCleanupDialog(QWidget* parentWidget);

    /**
     * @brief 执行磁盘清理（旧版直接清理，保留兼容）
     *
     * 清理 Windows 临时文件、用户临时文件、DNS 缓存等。
     *
     * @param parentWidget 用于显示消息对话框的父控件（可选）
     */
    void cleanDisk(QWidget* parentWidget = nullptr);

    /**
     * @brief 打开定时关机对话框
     *
     * 显示一个对话框让用户选择关机时间，然后调用系统 shutdown 命令。
     *
     * @param parentWidget 父控件
     */
    void scheduleShutdown(QWidget* parentWidget);

    /**
     * @brief 取消已计划的定时关机
     *
     * @param parentWidget 用于显示消息对话框的父控件（可选）
     */
    void cancelShutdown(QWidget* parentWidget = nullptr);

    /**
     * @brief 打开多线程下载对话框
     *
     * 显示一个对话框让用户输入下载 URL 和保存路径，使用多线程下载文件。
     *
     * @param parentWidget 父控件
     */
    void startMultiThreadDownload(QWidget* parentWidget);

    /**
     * @brief 打开 GitHub 加速对话框
     *
     * 通过修改本地 hosts 文件，将 GitHub 相关域名指向更快的 IP 地址，
     * 从而提升访问速度。需要管理员权限写入 hosts 文件。
     *
     * @param parentWidget 父控件
     */
    void accelerateGithub(QWidget* parentWidget);

signals:
    /**
     * @brief 操作完成信号
     * @param message 结果消息
     * @param success 是否成功
     */
    void operationFinished(const QString& message, bool success);

private:
    /**
     * @brief 计算并格式化文件大小
     * @param bytes 字节数
     * @return 格式化后的大小字符串
     */
    static QString formatSize(qint64 bytes);

    /**
     * @brief 使用 Windows API 清理工作集
     * @return 释放的内存大小（字节），-1 表示失败
     */
    static qint64 emptyWorkingSet();

    /**
     * @brief 清理指定目录的临时文件
     * @param dirPath 目录路径
     * @return 释放的空间大小（字节）
     */
    static qint64 cleanTempDirectory(const QString& dirPath);

    /**
     * @brief 获取所有可清理类别及其预估大小
     */
    QVector<CleanCategory> getCleanCategories();

    /**
     * @brief 预估指定目录大小
     */
    static qint64 estimateDirSize(const QString& dirPath);

    /**
     * @brief 清理 Windows 临时目录
     */
    static qint64 cleanWindowsTemp();

    /**
     * @brief 清理用户临时目录
     */
    static qint64 cleanUserTemp();

    /**
     * @brief 清理 Prefetch 文件
     */
    static qint64 cleanPrefetch();

    /**
     * @brief 清理 DNS 缓存
     */
    static void cleanDnsCache();

    /**
     * @brief 清空回收站
     * @return 成功返回 true
     */
    static bool cleanRecycleBin();

    /**
     * @brief 清理浏览器缓存
     * @return 释放的空间大小
     */
    static qint64 cleanBrowserCache();

    /**
     * @brief 清理最近文档历史
     * @return 释放的空间大小
     */
    static qint64 cleanRecentDocs();

    /**
     * @brief 清理缩略图缓存
     * @return 释放的空间大小
     */
    static qint64 cleanThumbnailCache();

    /**
     * @brief 清理 Windows 日志文件
     * @return 释放的空间大小
     */
    static qint64 cleanWindowsLogs();

    /**
     * @brief 清理系统内存转储文件
     * @return 释放的空间大小
     */
    static qint64 cleanMemoryDumps();

    /**
     * @brief 获取浏览器缓存路径列表
     */
    static QStringList getBrowserCachePaths();

    /**
     * @brief 获取系统 hosts 文件路径
     */
    static QString hostsFilePath();

    /**
     * @brief 读取 hosts 文件全部内容
     */
    static QString readHostsFile();

    /**
     * @brief 从 hosts 内容中移除 BlockBox 标记段
     */
    static QString removeBlockBoxHostsSection(const QString& content);

    /**
     * @brief 以管理员权限写入 hosts 文件并刷新 DNS 缓存
     * @return 成功返回 true，失败时填充 errorMsg
     */
    static bool applyHostsElevated(const QString& newContent, QString& errorMsg);

    static SystemTools* m_instance;

    MultiThreadDownloader *m_multiDownloader;
};