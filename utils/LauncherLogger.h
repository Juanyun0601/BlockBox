/**
 * @file   LauncherLogger.h
 * @brief  方块盒子运行日志（launcher.log）
 * @author BlockBox Team
 * @date   2026-10-24
 *
 * 启动器自己的运行日志：接管 qDebug / qInfo / qWarning / qCritical，
 * 统一追加写入 <数据目录>/logs/launcher.log，超过上限自动轮转，
 * 设置 → 高级设置里可以一键导出，反馈问题时方便附上。
 */

#pragma once

#include <QDebug>
#include <QString>
#include <QtGlobal>

class LauncherLogger
{
public:
    /// 安装日志接管（创建日志文件 + 安装消息处理器），需在 QApplication 之后调用
    static void install();

    /// 刷盘并关闭日志文件（退出时调用，之后的消息不再落盘）
    static void shutdown();

    /// 是否记录运行日志（设置项 runtime_log_enabled，默认开启）
    static bool isEnabled();
    static void setEnabled(bool enabled);

    /// 是否记录详细调试信息（源码位置、线程等，设置项 runtime_log_verbose，默认关闭）
    static bool isVerbose();
    static void setVerbose(bool verbose);

    /// 日志目录（<数据目录>/logs）
    static QString logDirectory();

    /// 当前日志文件路径
    static QString logFilePath();

    /// 当前日志文件大小（字节）
    static qint64 logSize();

    /**
     * @brief 把运行日志整理成一份可直接发出去的文本文件
     * @param destPath     目标文件路径
     * @param errorMessage 失败时写入原因（可为空）
     * @return 是否导出成功
     */
    static bool exportLog(const QString &destPath, QString *errorMessage = nullptr);

private:
    /// 消息处理器：格式化后落盘，再转给原来的处理器（保留调试器输出）
    static void handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &message);
};
