/**
 * @file   LauncherLogger.cpp
 * @brief  方块盒子运行日志实现
 * @author BlockBox Team
 * @date   2026-10-24
 */

#include "utils/LauncherLogger.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QThread>

#include "platform.h"
#include "utils/AppVersion.h"
#include "utils/SystemInfo.h"

namespace {

/// 单份日志上限：超过就轮转，避免长时间挂机把磁盘写满
const qint64 kMaxLogBytes = 4LL * 1024 * 1024;

/// 保留的历史日志份数（launcher.1.log、launcher.2.log）
const int kMaxBackupFiles = 2;

/// 日志相关的运行期状态（加锁后访问；进程退出前一直有效）
struct LoggerState
{
    QMutex mutex;
    QFile file;
    QtMessageHandler previous = nullptr;
    bool installed = false;
    bool enabled = true;
    bool verbose = false;
    qint64 writtenBytes = 0;   ///< 当前日志文件已写字节数（避免每条日志都去查文件大小）
};

LoggerState &loggerState()
{
    static LoggerState state;
    return state;
}

/// 日志级别标签（等宽，方便肉眼对齐）
QString levelTag(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO ");
    case QtWarningMsg:
        return QStringLiteral("WARN ");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO ");
}

/// 线程标识：优先用线程名，没有名字就退回线程指针，方便区分后台线程
QString threadTag()
{
    QThread *thread = QThread::currentThread();
    if (!thread)
        return QStringLiteral("?");
    if (!thread->objectName().isEmpty())
        return thread->objectName();
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(thread), 0, 16);
}

/// 写日志并刷盘（调用方需已持有 state.mutex）
void writeLocked(LoggerState &state, const QByteArray &bytes)
{
    if (!state.file.isOpen())
        return;
    state.file.write(bytes);
    state.file.flush();
    state.writtenBytes += bytes.size();
}

/// 打开日志文件（追加写入），返回值表示是否成功
bool openLogLocked(LoggerState &state)
{
    if (state.file.isOpen())
        return true;
    state.file.setFileName(LauncherLogger::logFilePath());
    if (!state.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    state.writtenBytes = state.file.size();
    return true;
}

/**
 * @brief 轮转日志：launcher.log → launcher.1.log → launcher.2.log（更老的丢弃）
 *
 * 启动时超限轮转一次；运行期再超限也会调用，调用方需已持有锁。
 */
void rotateLocked(LoggerState &state)
{
    const QString base = LauncherLogger::logFilePath();
    if (!QFile::exists(base))
        return;

    state.file.close();

    QFile::remove(base + QStringLiteral(".%1").arg(kMaxBackupFiles));
    for (int i = kMaxBackupFiles - 1; i >= 1; --i) {
        const QString from = base + QStringLiteral(".%1").arg(i);
        const QString to = base + QStringLiteral(".%1").arg(i + 1);
        if (QFile::exists(from)) {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }

    QFile::remove(base + QStringLiteral(".1"));
    QFile::rename(base, base + QStringLiteral(".1"));

    openLogLocked(state);
}

/// 本次启动的分隔行：一眼能看出日志对应哪一版、什么系统
QString sessionHeader()
{
    // 注意：这个函数会在安装消息处理器之前调用，内部的 QSettings / 系统探测即使
    // 产生日志也不会回到这里，不会和自己抢锁
    const SystemInfoData info = SystemInfo::collect();
    return QStringLiteral("\n==================== 方块盒子启动 %1 ====================\n"
                          "版本 %2 · %3 %4 · %5 · 平台 %6\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
             AppVersion::current(),
             info.osName.isEmpty() ? Platform::getPlatformName() : info.osName,
             info.kernelVersion,
             info.architecture,
             Platform::getPlatformName());
}

} // namespace

void LauncherLogger::install()
{
    LoggerState &state = loggerState();
    QMutexLocker locker(&state.mutex);

    if (state.installed)
        return;
    state.installed = true;

    // 设置页写入的是同一份 QSettings("BlockBox", "BlockBox")，这里直接读，避免
    // 依赖 SettingsManager 单例在启动早期是否已经构造
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    state.enabled = settings.value(QStringLiteral("runtime_log_enabled"), true).toBool();
    state.verbose = settings.value(QStringLiteral("runtime_log_verbose"), false).toBool();

    const QString header = sessionHeader();

    QDir().mkpath(logDirectory());
    if (QFileInfo(LauncherLogger::logFilePath()).size() >= kMaxLogBytes)
        rotateLocked(state);

    // 打开失败（比如目录不可写）就静默降级：启动器照常运行，只是没有日志文件
    if (openLogLocked(state) && state.enabled)
        writeLocked(state, header.toUtf8());

    // 接管 Qt 消息：落盘的同时仍然转给原来的处理器，调试器里照样能看到输出
    state.previous = qInstallMessageHandler(&LauncherLogger::handleMessage);
}

void LauncherLogger::shutdown()
{
    LoggerState &state = loggerState();
    QMutexLocker locker(&state.mutex);
    if (!state.file.isOpen())
        return;
    state.file.flush();
    state.file.close();
}

bool LauncherLogger::isEnabled()
{
    LoggerState &state = loggerState();
    QMutexLocker locker(&state.mutex);
    return state.enabled;
}

void LauncherLogger::setEnabled(bool enabled)
{
    {
        LoggerState &state = loggerState();
        QMutexLocker locker(&state.mutex);
        state.enabled = enabled;
        if (enabled)
            openLogLocked(state);
    }
    QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .setValue(QStringLiteral("runtime_log_enabled"), enabled);
}

bool LauncherLogger::isVerbose()
{
    LoggerState &state = loggerState();
    QMutexLocker locker(&state.mutex);
    return state.verbose;
}

void LauncherLogger::setVerbose(bool verbose)
{
    {
        LoggerState &state = loggerState();
        QMutexLocker locker(&state.mutex);
        state.verbose = verbose;
    }
    QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .setValue(QStringLiteral("runtime_log_verbose"), verbose);
}

QString LauncherLogger::logDirectory()
{
    return Platform::getDataDirectory() + QStringLiteral("/logs");
}

QString LauncherLogger::logFilePath()
{
    return logDirectory() + QStringLiteral("/launcher.log");
}

qint64 LauncherLogger::logSize()
{
    LoggerState &state = loggerState();
    QMutexLocker locker(&state.mutex);
    if (state.file.isOpen())
        return state.writtenBytes;
    return QFileInfo(logFilePath()).size();
}

bool LauncherLogger::exportLog(const QString &destPath, QString *errorMessage)
{
    if (destPath.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("目标路径为空");
        return false;
    }

    // 先把内存里还没落盘的部分刷出去，再整份读出来
    QByteArray body;
    {
        LoggerState &state = loggerState();
        QMutexLocker locker(&state.mutex);
        if (state.file.isOpen())
            state.file.flush();

        QFile source(LauncherLogger::logFilePath());
        if (source.open(QIODevice::ReadOnly)) {
            body = source.readAll();
            source.close();
        }
    }

    const SystemInfoData info = SystemInfo::collect();
    const QString title = QStringLiteral("方块盒子运行日志");
    const QString rule(60, QLatin1Char('='));

    QString header;
    header += title + QLatin1Char('\n') + rule + QLatin1Char('\n');
    header += QStringLiteral("导出时间：%1\n")
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    header += QStringLiteral("启动器版本：%1\n").arg(AppVersion::current());
    header += QStringLiteral("运行平台：%1\n").arg(Platform::getPlatformName());
    header += QStringLiteral("系统版本：%1 %2 · %3\n")
                  .arg(info.osFullName.isEmpty() ? info.osName : info.osFullName,
                       info.kernelVersion,
                       info.architecture);
    header += QStringLiteral("日志文件：%1\n").arg(QDir::toNativeSeparators(LauncherLogger::logFilePath()));
    header += QStringLiteral("说明：日志按时间从旧到新排列，最后几行通常就是出错的位置。\n");
    header += rule + QLatin1Char('\n') + QLatin1Char('\n');

    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = out.errorString();
        return false;
    }

    const QByteArray bytes = header.toUtf8() + body;
    const qint64 written = out.write(bytes);
    out.flush();
    out.close();

    if (written != bytes.size()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("写入不完整");
        return false;
    }
    return true;
}

void LauncherLogger::handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    LoggerState &state = loggerState();
    QtMessageHandler previous = nullptr;

    {
        QMutexLocker locker(&state.mutex);
        previous = state.previous;

        if (state.enabled && state.file.isOpen()) {
            if (state.writtenBytes >= kMaxLogBytes)
                rotateLocked(state);

            QString line = QStringLiteral("%1 [%2]")
                               .arg(QDateTime::currentDateTime()
                                        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                    levelTag(type));

            // 只保留非默认分类，日志更干净一些
            if (context.category && qstrcmp(context.category, "default") != 0)
                line += QStringLiteral(" [%1]").arg(QString::fromUtf8(context.category));

            if (state.verbose) {
                if (context.file) {
                    line += QStringLiteral(" (%1:%2)")
                                .arg(QFileInfo(QString::fromUtf8(context.file)).fileName())
                                .arg(context.line);
                }
                line += QStringLiteral(" {tid %1}").arg(threadTag());
            }

            line += QLatin1Char(' ');
            line += message;
            line += QLatin1Char('\n');

            writeLocked(state, line.toUtf8());
        }
    }

    // 转给原来的处理器（默认实现会输出到 stderr / OutputDebugString）
    if (previous)
        previous(type, context, message);
}
