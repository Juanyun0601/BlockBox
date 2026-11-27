/**
 * @file   CrashReporter.cpp
 * @brief  崩溃自动上报实现
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 处理路径分两段：
 *   1. 崩溃当场（信号处理函数里）：只做最简单的 open/write，把原因、调用栈和
 *      日志尾部写进 crash-reports 目录的 .crash 文件，然后直接结束进程；
 *      不能分配内存、不能加锁。
 *   2. 下次启动：解析这些文本文件，交给界面弹窗询问是否上传。上传走论坛的
 *      /api/app/crash-reports（匿名、免人机验证），上传前统一脱敏。
 */
#include "utils/CrashReporter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <exception>
#include <memory>

#include "components/CrashReportDialog.h"
#include "components/NotificationManager.h"
#include "platform.h"
#include "utils/AppVersion.h"
#include "utils/LauncherLogger.h"
#include "utils/SettingsManager.h"
#include "utils/SystemInfo.h"
#include "utils/trust/LogSanitizer.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <psapi.h>
#include <sys/stat.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unwind.h>
#endif

namespace {

// ───────────────────────── 崩溃现场（信号处理函数里访问） ─────────────────────────

/// 正在崩溃报告里的重入标记：第二次进来说明前一次已经写坏了，直接结束进程
volatile sig_atomic_t g_handlingCrash = 0;

/// 本次进程启动时间（用来算崩溃前运行了多久）
time_t g_startedAt = 0;

/// 平台 / 版本信息：启动时转成固定 char 数组，崩溃时不再碰 Qt
char g_appVersion[64] = "";
char g_platform[32] = "";
char g_os[128] = "";
char g_osVersion[160] = "";
char g_arch[32] = "";
char g_locale[64] = "";
char g_phase[64] = "启动中";
char g_page[64] = "";

#ifdef Q_OS_WIN
wchar_t g_crashDirPath[1024] = {0};
wchar_t g_logPath[1024] = {0};
wchar_t g_markerPath[1024] = {0};
typedef USHORT (WINAPI *RtlCaptureStackBackTraceFn)(ULONG, ULONG, PVOID *, PULONG);
RtlCaptureStackBackTraceFn g_captureStack = nullptr;
#else
char g_crashDirPath[1024] = {0};
char g_logPath[1024] = {0};
char g_markerPath[1024] = {0};
#endif

/// 日志尾部读取缓冲（静态区，避免崩溃时在栈上开大数组）
constexpr qint64 kMaxLogTailBytes = 48 * 1024;
char g_logTailBuffer[kMaxLogTailBytes + 1] = {0};

/// 本地最多保留多少份崩溃报告（包括已发送的）
constexpr int kMaxStoredReports = 20;
/// 已发送报告保留天数
constexpr int kSentReportKeepDays = 14;

/// Qt 之前的消息处理器（LauncherLogger），非致命消息继续转给它
QtMessageHandler g_previousMessageHandler = nullptr;

// ───────────────────────── 最底层写文件 ─────────────────────────

#ifdef Q_OS_WIN

int crashOpenWrite(const wchar_t *path)
{
    return _wopen(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
                  _S_IREAD | _S_IWRITE);
}

void crashWriteFd(int fd, const char *data, size_t size)
{
    if (fd >= 0 && data && size > 0)
        _write(fd, data, static_cast<unsigned int>(size));
}

void crashCloseFd(int fd)
{
    if (fd >= 0) {
        _commit(fd);
        _close(fd);
    }
}

void crashUnlink(const wchar_t *path)
{
    if (path && path[0])
        _wunlink(path);
}

#else

int crashOpenWrite(const char *path)
{
    return ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

void crashWriteFd(int fd, const char *data, size_t size)
{
    if (fd < 0 || !data || size == 0)
        return;
    size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n <= 0)
            break;
        written += static_cast<size_t>(n);
    }
}

void crashCloseFd(int fd)
{
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
}

void crashUnlink(const char *path)
{
    if (path && path[0])
        ::unlink(path);
}

#endif

void writeTextFd(int fd, const char *text)
{
    if (text && *text)
        crashWriteFd(fd, text, std::strlen(text));
}

void writeFieldFd(int fd, const char *key, const char *value)
{
    char line[1024];
    const int n = std::snprintf(line, sizeof(line), "%s=%s\n", key, value ? value : "");
    if (n > 0)
        crashWriteFd(fd, line, std::min<int>(n, static_cast<int>(sizeof(line) - 1)));
}

/// 取地址所属模块名与偏移；取不到就只写裸地址
#ifdef Q_OS_WIN
void writeModuleLine(int fd, void *address)
{
    HMODULE module = nullptr;
    wchar_t modulePath[MAX_PATH] = {0};
    const bool found =
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module)
        && module != nullptr;
    if (!found) {
        char line[96];
        const int n = std::snprintf(line, sizeof(line), "  %p\n", address);
        if (n > 0)
            crashWriteFd(fd, line, std::min<int>(n, sizeof(line) - 1));
        return;
    }

    GetModuleFileNameW(module, modulePath, MAX_PATH);
    const wchar_t *base = std::wcsrchr(modulePath, L'\\');
    base = base ? base + 1 : modulePath;

    MODULEINFO info{};
    uintptr_t offset = reinterpret_cast<uintptr_t>(address);
    if (GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
        offset -= reinterpret_cast<uintptr_t>(info.lpBaseOfDll);

    char moduleName[260] = {0};
    WideCharToMultiByte(CP_UTF8, 0, base, -1, moduleName, sizeof(moduleName) - 1, nullptr, nullptr);

    char line[512];
    const int n = std::snprintf(line, sizeof(line), "  %s+0x%llx (%p)\n", moduleName,
                                static_cast<unsigned long long>(offset), address);
    if (n > 0)
        crashWriteFd(fd, line, std::min<int>(n, static_cast<int>(sizeof(line) - 1)));
}
#else
void writeModuleLine(int fd, uintptr_t ip)
{
    Dl_info info{};
    char line[640];
    int n = 0;
    if (ip && dladdr(reinterpret_cast<void *>(ip), &info) && info.dli_fname) {
        const char *base = std::strrchr(info.dli_fname, '/');
        base = base ? base + 1 : info.dli_fname;
        const uintptr_t offset =
            ip - reinterpret_cast<uintptr_t>(info.dli_fbase);
        if (info.dli_sname && info.dli_sname[0]) {
            n = std::snprintf(line, sizeof(line), "  %s+0x%lx %s (%p)\n", base,
                              static_cast<unsigned long>(offset), info.dli_sname,
                              reinterpret_cast<void *>(ip));
        } else {
            n = std::snprintf(line, sizeof(line), "  %s+0x%lx (%p)\n", base,
                              static_cast<unsigned long>(offset),
                              reinterpret_cast<void *>(ip));
        }
    } else {
        n = std::snprintf(line, sizeof(line), "  %p\n", reinterpret_cast<void *>(ip));
    }
    if (n > 0)
        crashWriteFd(fd, line, std::min<int>(n, static_cast<int>(sizeof(line) - 1)));
}
#endif

#ifdef Q_OS_WIN
void writeStackTrace(int fd)
{
    if (!g_captureStack) {
        writeTextFd(fd, "  (系统不支持读取调用栈)\n");
        return;
    }
    void *frames[40] = {0};
    const USHORT count = g_captureStack(2, 40, frames, nullptr);
    if (count == 0) {
        writeTextFd(fd, "  (调用栈为空)\n");
        return;
    }
    for (USHORT i = 0; i < count; ++i) {
        if (!frames[i])
            continue;
        char prefix[24];
        const int n = std::snprintf(prefix, sizeof(prefix), "  #%u ", static_cast<unsigned>(i));
        if (n > 0)
            crashWriteFd(fd, prefix, n);
        writeModuleLine(fd, frames[i]);
    }
}
#else
struct UnwindState
{
    int fd = -1;
    int count = 0;
};

_Unwind_Reason_Code unwindCallback(struct _Unwind_Context *context, void *argument)
{
    UnwindState *state = static_cast<UnwindState *>(argument);
    if (!state || state->fd < 0)
        return _URC_END_OF_STACK;
    const uintptr_t ip = static_cast<uintptr_t>(_Unwind_GetIP(context));
    if (ip == 0)
        return _URC_NO_REASON;
    char prefix[24];
    const int n = std::snprintf(prefix, sizeof(prefix), "  #%d ", state->count);
    if (n > 0)
        crashWriteFd(state->fd, prefix, n);
    writeModuleLine(state->fd, ip);
    state->count += 1;
    return state->count >= 40 ? _URC_END_OF_STACK : _URC_NO_REASON;
}

void writeStackTrace(int fd)
{
    UnwindState state;
    state.fd = fd;
    _Unwind_Backtrace(&unwindCallback, &state);
    if (state.count == 0)
        writeTextFd(fd, "  (调用栈为空)\n");
}
#endif

/// 出事前最后一段运行日志（按行截断，尽量从完整的一行开始）
void writeLogTail(int fd)
{
#ifdef Q_OS_WIN
    const int logFd = _wopen(g_logPath, _O_RDONLY | _O_BINARY);
#else
    const int logFd = ::open(g_logPath, O_RDONLY);
#endif
    if (logFd < 0) {
        writeTextFd(fd, "(没有可用的运行日志)\n");
        return;
    }

#ifdef Q_OS_WIN
    const __int64 size = _lseeki64(logFd, 0, SEEK_END);
    __int64 start = size > kMaxLogTailBytes ? size - kMaxLogTailBytes : 0;
    _lseeki64(logFd, start, SEEK_SET);
    const int readBytes = _read(logFd, g_logTailBuffer, static_cast<unsigned int>(kMaxLogTailBytes));
    _close(logFd);
#else
    const off_t size = ::lseek(logFd, 0, SEEK_END);
    off_t start = size > kMaxLogTailBytes ? size - kMaxLogTailBytes : 0;
    ::lseek(logFd, start, SEEK_SET);
    const ssize_t readBytes = ::read(logFd, g_logTailBuffer, static_cast<size_t>(kMaxLogTailBytes));
    ::close(logFd);
#endif

    if (readBytes <= 0) {
        writeTextFd(fd, "(运行日志为空)\n");
        return;
    }
    g_logTailBuffer[readBytes] = '\0';

    const char *text = g_logTailBuffer;
    if (start > 0) {
        const char *newline = std::strchr(text, '\n');
        if (newline && newline - text < readBytes)
            text = newline + 1;
    }
    crashWriteFd(fd, text, std::strlen(text));
}

/// 崩溃现场写盘：只调用 async-signal-safe 的 open/write/close 一类操作
void writeCrashRecord(const char *reason, const char *signalName, const char *exception,
                      const char *faultModule, const char *contextText)
{
    if (g_handlingCrash) {
        std::_Exit(3);
        return;
    }
    g_handlingCrash = 1;

#ifdef Q_OS_WIN
    wchar_t path[1200] = {0};
    std::swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\crash-%lld-%lu.crash",
                  g_crashDirPath, static_cast<long long>(std::time(nullptr)),
                  static_cast<unsigned long>(GetCurrentProcessId()));
    const int fd = crashOpenWrite(path);
#else
    char path[1200] = {0};
    std::snprintf(path, sizeof(path), "%s/crash-%lld-%lu.crash", g_crashDirPath,
                  static_cast<long long>(std::time(nullptr)),
                  static_cast<unsigned long>(::getpid()));
    const int fd = crashOpenWrite(path);
#endif

    if (fd < 0) {
        crashUnlink(g_markerPath);
        std::_Exit(3);
        return;
    }

    const time_t now = std::time(nullptr);
    char value[512];

    writeTextFd(fd, "BB-CRASH 1\n");

    std::snprintf(value, sizeof(value), "%lld-%lu", static_cast<long long>(now),
#ifdef Q_OS_WIN
                  static_cast<unsigned long>(GetCurrentProcessId())
#else
                  static_cast<unsigned long>(::getpid())
#endif
    );
    writeFieldFd(fd, "id", value);
    std::snprintf(value, sizeof(value), "%lld", static_cast<long long>(now));
    writeFieldFd(fd, "time", value);
    writeFieldFd(fd, "reason", reason);
    writeFieldFd(fd, "signal", signalName);
    writeFieldFd(fd, "exception", exception);
    writeFieldFd(fd, "fault_module", faultModule);
    writeFieldFd(fd, "phase", g_phase);
    writeFieldFd(fd, "page", g_page);
    writeFieldFd(fd, "app_version", g_appVersion);
    writeFieldFd(fd, "platform", g_platform);
    writeFieldFd(fd, "os", g_os);
    writeFieldFd(fd, "os_version", g_osVersion);
    writeFieldFd(fd, "arch", g_arch);
    writeFieldFd(fd, "locale", g_locale);

    std::snprintf(value, sizeof(value), "%lld",
                  static_cast<long long>(now - g_startedAt));
    writeFieldFd(fd, "uptime", value);

    std::snprintf(value, sizeof(value), "0x%lx",
#ifdef Q_OS_WIN
                  static_cast<unsigned long>(GetCurrentThreadId())
#else
                  static_cast<unsigned long>(
                      (uintptr_t)pthread_self())
#endif
    );
    writeFieldFd(fd, "thread", value);
    writeFieldFd(fd, "sent", "0");

    writeTextFd(fd, "--STACK--\n");
    writeStackTrace(fd);

    writeTextFd(fd, "--LOG--\n");
    writeLogTail(fd);

    writeTextFd(fd, "--CONTEXT--\n");
    if (contextText && *contextText)
        writeTextFd(fd, contextText);
    writeTextFd(fd, "\n");

    crashCloseFd(fd);
    // 已经留下现场：删掉「正在运行」标记，下次启动就不会再记一条「异常退出」
    crashUnlink(g_markerPath);
    std::_Exit(3);
}

// ───────────────────────── 各平台崩溃入口 ─────────────────────────

#ifdef Q_OS_WIN
LONG WINAPI windowsExceptionFilter(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *record = info ? info->ExceptionRecord : nullptr;
    char exception[256] = "code=unknown";
    char module[320] = "";

    if (record) {
        std::snprintf(exception, sizeof(exception),
                      "code=0x%08lx address=%p flags=0x%lx",
                      static_cast<unsigned long>(record->ExceptionCode),
                      record->ExceptionAddress,
                      static_cast<unsigned long>(record->ExceptionFlags));

        HMODULE modulation = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                   | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(record->ExceptionAddress),
                               &modulation)
            && modulation) {
            wchar_t modulePath[MAX_PATH] = {0};
            GetModuleFileNameW(modulation, modulePath, MAX_PATH);
            const wchar_t *base = std::wcsrchr(modulePath, L'\\');
            base = base ? base + 1 : modulePath;
            char moduleName[260] = {0};
            WideCharToMultiByte(CP_UTF8, 0, base, -1, moduleName,
                                sizeof(moduleName) - 1, nullptr, nullptr);
            MODULEINFO moduleInfo{};
            uintptr_t offset = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
            if (GetModuleInformation(GetCurrentProcess(), modulation, &moduleInfo,
                                     sizeof(moduleInfo)))
                offset -= reinterpret_cast<uintptr_t>(moduleInfo.lpBaseOfDll);
            std::snprintf(module, sizeof(module), "%s+0x%llx", moduleName,
                          static_cast<unsigned long long>(offset));
        }
    }

    writeCrashRecord("unhandled_exception", "EXCEPTION", exception, module, nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
const char *signalName(int signalNumber)
{
    switch (signalNumber) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGABRT:
        return "SIGABRT";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
#ifdef SIGBUS
    case SIGBUS:
        return "SIGBUS";
#endif
#ifdef SIGTRAP
    case SIGTRAP:
        return "SIGTRAP";
#endif
    default:
        return "SIGNAL";
    }
}

void crashSignalHandler(int signalNumber, siginfo_t *info, void *)
{
    char exception[256] = "";
    std::snprintf(exception, sizeof(exception), "signal=%d address=%p", signalNumber,
                  info ? info->si_addr : nullptr);
    writeCrashRecord(signalName(signalNumber), signalName(signalNumber), exception, "", nullptr);
    std::_Exit(128 + signalNumber);
}
#endif

/// std::terminate（未捕获异常、noexcept 违反等）
void terminateHandler()
{
    writeCrashRecord("std::terminate", "TERMINATE", "uncaught exception", "", nullptr);
    std::_Exit(3);
}

/**
 * 接住 qFatal：LauncherLogger 已经装了一层消息处理器，这里装在它外面。
 * 致命日志先落一份崩溃报告再结束进程；普通日志原样转给 LauncherLogger。
 */
void crashMessageHandler(QtMsgType type, const QMessageLogContext &context,
                         const QString &message)
{
    if (type == QtFatalMsg) {
        const QByteArray utf8 = message.toUtf8();
        writeCrashRecord("qFatal", "QtFatalMsg", "qFatal", "", utf8.constData());
        std::_Exit(3);
        return;
    }
    if (g_previousMessageHandler)
        g_previousMessageHandler(type, context, message);
}

/// 进程是否还活着（用来区分「上次被强杀」和「另一个实例正在运行」）
bool isProcessAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!handle)
        return false;
    const DWORD result = WaitForSingleObject(handle, 0);
    CloseHandle(handle);
    return result == WAIT_TIMEOUT;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

/// 在崩溃目录里生成一份文本报告（主线程用，允许 Qt）
bool writeTextReport(const QString &dir, const QString &id, qint64 timestamp,
                     const QString &reason, const QString &signalName,
                     const QString &phase, const QString &page, const QString &stack,
                     const QString &logTail, const QString &context, qint64 uptimeSeconds,
                     const QString &appVersion)
{
    const SystemInfoData info = SystemInfo::collect();
    QString header;
    header += QStringLiteral("BB-CRASH 1\n");
    header += QStringLiteral("id=%1\n").arg(id);
    header += QStringLiteral("time=%1\n").arg(timestamp);
    header += QStringLiteral("reason=%1\n").arg(reason);
    header += QStringLiteral("signal=%1\n").arg(signalName);
    header += QStringLiteral("exception=\n");
    header += QStringLiteral("fault_module=\n");
    header += QStringLiteral("phase=%1\n").arg(phase);
    header += QStringLiteral("page=%1\n").arg(page);
    header += QStringLiteral("app_version=%1\n").arg(appVersion);
    header += QStringLiteral("platform=%1\n").arg(CrashReporter::platformKey());
    header += QStringLiteral("os=%1\n").arg(info.osName);
    header += QStringLiteral("os_version=%1\n").arg(info.kernelVersion);
    header += QStringLiteral("arch=%1\n").arg(info.architecture);
    header += QStringLiteral("locale=%1\n").arg(QLocale::system().name());
    header += QStringLiteral("uptime=%1\n").arg(uptimeSeconds);
    header += QStringLiteral("thread=-\n");
    header += QStringLiteral("sent=0\n");
    header += QStringLiteral("--STACK--\n%1\n--LOG--\n%2\n--CONTEXT--\n%3\n")
                  .arg(stack, logTail, context);

    const QString filePath =
        dir + QStringLiteral("/crash-%1-%2.crash").arg(timestamp).arg(id.right(8));
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(header.toUtf8());
    file.flush();
    file.close();
    return true;
}

/// 读日志尾部（主线程用）
QString readLogTailFromFile(const QString &path, int maxBytes = 24 * 1024)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const qint64 size = file.size();
    if (size > maxBytes)
        file.seek(size - maxBytes);
    QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty())
        return QString();
    if (size > maxBytes) {
        const int newline = data.indexOf('\n');
        if (newline >= 0 && newline + 1 < data.size())
            data = data.mid(newline + 1);
    }
    return QString::fromUtf8(data);
}

/// 解析一份 .crash 文本
CrashReport parseReportFile(const QString &path)
{
    CrashReport report;
    report.filePath = path;
    report.sent = path.endsWith(QStringLiteral(".sent"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return report;
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    enum Section { Header, Stack, Log, Context };
    Section section = Header;
    QHash<QString, QString> fields;
    QString stack;
    QString logTail;
    QString contextText;

    const QStringList lines = content.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString &line : lines) {
        if (line == QStringLiteral("--STACK--")) {
            section = Stack;
            continue;
        }
        if (line == QStringLiteral("--LOG--")) {
            section = Log;
            continue;
        }
        if (line == QStringLiteral("--CONTEXT--")) {
            section = Context;
            continue;
        }
        if (section == Header) {
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals > 0)
                fields.insert(line.left(equals), line.mid(equals + 1));
            continue;
        }
        if (section == Stack)
            stack += line + QLatin1Char('\n');
        else if (section == Log)
            logTail += line + QLatin1Char('\n');
        else
            contextText += line + QLatin1Char('\n');
    }

    report.id = fields.value(QStringLiteral("id"));
    report.timestamp = fields.value(QStringLiteral("time")).toLongLong();
    report.reason = fields.value(QStringLiteral("reason"));
    report.signalName = fields.value(QStringLiteral("signal"));
    report.exceptionCode = fields.value(QStringLiteral("exception"));
    report.faultModule = fields.value(QStringLiteral("fault_module"));
    report.thread = fields.value(QStringLiteral("thread"));
    report.phase = fields.value(QStringLiteral("phase"));
    report.page = fields.value(QStringLiteral("page"));
    report.appVersion = fields.value(QStringLiteral("app_version"));
    report.platform = fields.value(QStringLiteral("platform"));
    report.os = fields.value(QStringLiteral("os"));
    report.osVersion = fields.value(QStringLiteral("os_version"));
    report.arch = fields.value(QStringLiteral("arch"));
    report.locale = fields.value(QStringLiteral("locale"));
    report.uptimeSeconds = fields.value(QStringLiteral("uptime")).toLongLong();
    report.stack = stack.trimmed();
    report.logTail = logTail.trimmed();
    report.context = contextText.trimmed();
    report.sent = report.sent || fields.value(QStringLiteral("sent")) == QStringLiteral("1");

    if (report.id.isEmpty())
        report.id = QFileInfo(path).baseName();
    return report;
}

/// 报告按时间从新到旧
bool newerFirst(const CrashReport &a, const CrashReport &b)
{
    return a.timestamp > b.timestamp;
}

QString crashReasonText(const QString &reason)
{
    if (reason == QStringLiteral("unhandled_exception"))
        return QCoreApplication::translate("CrashReporter", "运行中发生了未处理的异常（访问冲突 / 非法指令等）");
    if (reason == QStringLiteral("std::terminate"))
        return QCoreApplication::translate("CrashReporter", "程序内部出现未捕获的异常，被强制结束");
    if (reason == QStringLiteral("qFatal"))
        return QCoreApplication::translate("CrashReporter", "程序主动中止（内部检测到无法继续的错误）");
    if (reason == QStringLiteral("unexpected_exit"))
        return QCoreApplication::translate("CrashReporter", "上次没有正常退出（可能被强制关闭、系统结束进程或断电）");
    if (reason.startsWith(QStringLiteral("SIG")))
        return QCoreApplication::translate("CrashReporter", "程序崩溃（%1）").arg(reason);
    return QCoreApplication::translate("CrashReporter", "程序异常退出");
}

} // namespace

// ───────────────────────── CrashReport 展示 ─────────────────────────

QString CrashReport::summary() const
{
    const QString when =
        QDateTime::fromSecsSinceEpoch(timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return QCoreApplication::translate("CrashReporter", "%1 · %2")
        .arg(when, crashReasonText(reason));
}

QString CrashReport::displayText() const
{
    QString text;
    text += QStringLiteral("报告编号：%1\n").arg(id);
    text += QStringLiteral("时间：%1\n")
                .arg(QDateTime::fromSecsSinceEpoch(timestamp)
                         .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    text += QStringLiteral("原因：%1\n").arg(crashReasonText(reason));
    if (!signalName.isEmpty())
        text += QStringLiteral("信号：%1\n").arg(signalName);
    if (!exceptionCode.isEmpty())
        text += QStringLiteral("异常：%1\n").arg(exceptionCode);
    if (!faultModule.isEmpty())
        text += QStringLiteral("出错模块：%1\n").arg(faultModule);
    text += QStringLiteral("版本：%1\n").arg(appVersion.isEmpty() ? AppVersion::current() : appVersion);
    text += QStringLiteral("平台：%1\n").arg(platform);
    text += QStringLiteral("系统：%1 %2（%3）\n").arg(os, osVersion, arch);
    if (!locale.isEmpty())
        text += QStringLiteral("语言：%1\n").arg(locale);
    text += QStringLiteral("阶段：%1\n").arg(phase);
    if (!page.isEmpty())
        text += QStringLiteral("页面：%1\n").arg(page);
    if (uptimeSeconds > 0)
        text += QStringLiteral("已运行：%1 秒\n").arg(uptimeSeconds);
    if (!context.isEmpty())
        text += QStringLiteral("\n[附加信息]\n%1\n").arg(context);
    if (!stack.isEmpty())
        text += QStringLiteral("\n[调用栈]\n%1\n").arg(stack);
    if (!logTail.isEmpty())
        text += QStringLiteral("\n[最后日志]\n%1\n").arg(logTail);
    return text;
}

// ───────────────────────── CrashReporter ─────────────────────────

CrashReporter *CrashReporter::instance()
{
    static CrashReporter reporter;
    return &reporter;
}

CrashReporter::CrashReporter(QObject *parent)
    : QObject(parent)
{
}

CrashReporter::~CrashReporter() = default;

QString CrashReporter::platformKey()
{
#if defined(Q_OS_WIN)
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    return (arch.contains(QStringLiteral("arm64"))
            || arch.contains(QStringLiteral("aarch64")))
        ? QStringLiteral("winarm64")
        : QStringLiteral("win64");
#elif defined(Q_OS_ANDROID)
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch.contains(QStringLiteral("arm64")) || arch.contains(QStringLiteral("aarch64")))
        return QStringLiteral("android-arm64-v8a");
    if (arch.contains(QStringLiteral("x86_64")))
        return QStringLiteral("android-x86_64");
    if (arch.contains(QStringLiteral("x86")))
        return QStringLiteral("android-x86");
    return QStringLiteral("android-armeabi-v7a");
#elif defined(HARMONY_OS) || defined(Q_OS_HARMONY)
    return QStringLiteral("harmony-arm64-v8a");
#elif defined(Q_OS_LINUX)
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    return (arch.contains(QStringLiteral("arm64"))
            || arch.contains(QStringLiteral("aarch64")))
        ? QStringLiteral("linux-arm64")
        : QStringLiteral("linux-x64");
#elif defined(Q_OS_MACOS)
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    return (arch.contains(QStringLiteral("arm64"))
            || arch.contains(QStringLiteral("aarch64")))
        ? QStringLiteral("macos-arm64")
        : QStringLiteral("macos-x64");
#else
    return QStringLiteral("other");
#endif
}

QString CrashReporter::installId()
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    QString id = settings.value(QStringLiteral("diagnostics/install_id")).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(QStringLiteral("diagnostics/install_id"), id);
    }
    return id;
}

QString CrashReporter::resetInstallId()
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    settings.setValue(QStringLiteral("diagnostics/install_id"), id);
    settings.sync();
    return id;
}

bool CrashReporter::autoSendEnabled()
{
    return QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .value(QStringLiteral("diagnostics/crash_auto_send"), false)
        .toBool();
}

void CrashReporter::setAutoSendEnabled(bool enabled)
{
    QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .setValue(QStringLiteral("diagnostics/crash_auto_send"), enabled);
}

bool CrashReporter::sendConsent()
{
    return QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .value(QStringLiteral("diagnostics/crash_send_consent"), false)
        .toBool();
}

void CrashReporter::setSendConsent(bool consent)
{
    QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .setValue(QStringLiteral("diagnostics/crash_send_consent"), consent);
}

QString CrashReporter::crashDirectory() const
{
    if (!crashDirectoryCache.isEmpty())
        return crashDirectoryCache;
    return Platform::getDataDirectory() + QStringLiteral("/crash-reports");
}

void CrashReporter::install()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    g_startedAt = std::time(nullptr);
    crashDirectoryCache = Platform::getDataDirectory() + QStringLiteral("/crash-reports");
    QDir().mkpath(crashDirectoryCache);

    // 平台 / 版本等信息先转成定长 char 数组，崩溃时不再调用 Qt
    const SystemInfoData info = SystemInfo::collect();
    auto copyText = [](char *target, size_t capacity, const QString &value) {
        if (!target || capacity == 0)
            return;
        const QByteArray utf8 = value.toUtf8();
        const size_t length = static_cast<size_t>(std::min<int>(utf8.size(),
                                                               static_cast<int>(capacity - 1)));
        if (length > 0)
            std::memcpy(target, utf8.constData(), length);
        target[length] = '\0';
    };
    copyText(g_appVersion, sizeof(g_appVersion), AppVersion::current());
    copyText(g_platform, sizeof(g_platform), platformKey());
    copyText(g_os, sizeof(g_os), info.osName.isEmpty() ? Platform::getPlatformName() : info.osName);
    copyText(g_osVersion, sizeof(g_osVersion), info.kernelVersion);
    copyText(g_arch, sizeof(g_arch), info.architecture);
    copyText(g_locale, sizeof(g_locale), QLocale::system().name());

    const QString logPath = LauncherLogger::logFilePath();
    // 每个进程一个标记：同时开两个方块盒子时互不覆盖，谁被强杀就记谁
    const QString markerPath = crashDirectoryCache
        + QStringLiteral("/.running-%1").arg(QCoreApplication::applicationPid());

#ifdef Q_OS_WIN
    const std::wstring dirWide = QDir::toNativeSeparators(crashDirectoryCache).toStdWString();
    const std::wstring logWide = QDir::toNativeSeparators(logPath).toStdWString();
    const std::wstring markerWide = QDir::toNativeSeparators(markerPath).toStdWString();
    std::wcsncpy(g_crashDirPath, dirWide.c_str(), sizeof(g_crashDirPath) / sizeof(wchar_t) - 1);
    std::wcsncpy(g_logPath, logWide.c_str(), sizeof(g_logPath) / sizeof(wchar_t) - 1);
    std::wcsncpy(g_markerPath, markerWide.c_str(), sizeof(g_markerPath) / sizeof(wchar_t) - 1);
#else
    copyText(g_crashDirPath, sizeof(g_crashDirPath), crashDirectoryCache);
    copyText(g_logPath, sizeof(g_logPath), logPath);
    copyText(g_markerPath, sizeof(g_markerPath), markerPath);
#endif

    collectUnexpectedExit();
    writeRunningMarker();
    cleanupOldReports();

    // 心跳：定期刷新「正在运行」标记里的阶段 / 页面，也顺带更新标记时间。
    // 被任务管理器强杀或断电时，下次启动就能知道出事时大概在哪个页面。
    m_heartbeat = new QTimer(this);
    m_heartbeat->setInterval(30 * 1000);
    connect(m_heartbeat, &QTimer::timeout, this, [this]() { writeRunningMarker(); });
    m_heartbeat->start();

    // 未处理异常（Windows SEH / POSIX 信号）
#ifdef Q_OS_WIN
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        g_captureStack = reinterpret_cast<RtlCaptureStackBackTraceFn>(
            GetProcAddress(ntdll, "RtlCaptureStackBackTrace"));
    SetUnhandledExceptionFilter(&windowsExceptionFilter);
#else
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = &crashSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    ::sigaction(SIGSEGV, &action, nullptr);
    ::sigaction(SIGABRT, &action, nullptr);
    ::sigaction(SIGFPE, &action, nullptr);
    ::sigaction(SIGILL, &action, nullptr);
#ifdef SIGBUS
    ::sigaction(SIGBUS, &action, nullptr);
#endif
#endif

    std::set_terminate(&terminateHandler);

    // 接在 LauncherLogger 之后：普通日志继续落盘，qFatal 先留崩溃现场
    g_previousMessageHandler = qInstallMessageHandler(&crashMessageHandler);

    qInfo() << "[CrashReporter]" << "崩溃捕获已就绪，报告目录：" << crashDirectoryCache;
}

void CrashReporter::shutdown()
{
    if (m_heartbeat)
        m_heartbeat->stop();
    const QString marker = crashDirectory()
        + QStringLiteral("/.running-%1").arg(QCoreApplication::applicationPid());
    QFile::remove(marker);
}

void CrashReporter::setAppPhase(const QString &phase)
{
    const QByteArray utf8 = phase.toUtf8();
    const size_t length = static_cast<size_t>(
        std::min<int>(utf8.size(), sizeof(g_phase) - 1));
    if (length > 0)
        std::memcpy(g_phase, utf8.constData(), length);
    g_phase[length] = '\0';
    // 启动 / 退出阶段变化时立刻刷新标记：被强杀时下次也能看到大致阶段
    if (!crashDirectoryCache.isEmpty())
        writeRunningMarker();
}

void CrashReporter::setCurrentPage(const QString &page)
{
    const QByteArray utf8 = page.toUtf8();
    const size_t length = static_cast<size_t>(
        std::min<int>(utf8.size(), sizeof(g_page) - 1));
    if (length > 0)
        std::memcpy(g_page, utf8.constData(), length);
    g_page[length] = '\0';
}

void CrashReporter::writeRunningMarker()
{
    const QString marker = crashDirectory()
        + QStringLiteral("/.running-%1").arg(QCoreApplication::applicationPid());
    QFile file(marker);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream out(&file);
    out << QStringLiteral("pid=%1\n").arg(QCoreApplication::applicationPid());
    out << QStringLiteral("started=%1\n").arg(QDateTime::currentSecsSinceEpoch());
    out << QStringLiteral("version=%1\n").arg(AppVersion::current());
    out << QStringLiteral("phase=%1\n").arg(QString::fromUtf8(g_phase));
    out << QStringLiteral("page=%1\n").arg(QString::fromUtf8(g_page));
    file.flush();
    file.close();
}

void CrashReporter::collectUnexpectedExit()
{
    QDir dir(crashDirectory());
    if (!dir.exists())
        return;

    // 兼容旧版本留下的单个 .running；新版本每个进程一个 .running-<pid>
    const QStringList markers = dir.entryList(
        {QStringLiteral(".running"), QStringLiteral(".running-*")},
        QDir::Files | QDir::Hidden);
    if (markers.isEmpty())
        return;

    const QString logTail =
        readLogTailFromFile(Platform::getDataDirectory() + QStringLiteral("/logs/launcher.log"));

    for (const QString &name : markers) {
        const QString marker = dir.filePath(name);
        QFile file(marker);
        if (!file.exists())
            continue;

        qint64 pid = 0;
        qint64 started = 0;
        QString phase = QStringLiteral("运行中");
        QString page;
        QString version = AppVersion::current();
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(file.readAll());
            file.close();
            for (const QString &line : content.split(QLatin1Char('\n'))) {
                if (line.startsWith(QStringLiteral("pid=")))
                    pid = line.mid(4).trimmed().toLongLong();
                else if (line.startsWith(QStringLiteral("started=")))
                    started = line.mid(8).trimmed().toLongLong();
                else if (line.startsWith(QStringLiteral("version="))) {
                    const QString value = line.mid(8).trimmed();
                    if (!value.isEmpty())
                        version = value;
                } else if (line.startsWith(QStringLiteral("phase="))) {
                    const QString value = line.mid(6).trimmed();
                    if (!value.isEmpty())
                        phase = value;
                } else if (line.startsWith(QStringLiteral("page="))) {
                    page = line.mid(5).trimmed();
                }
            }
        }

        // 另一个方块盒子还在跑（共用同一份数据目录）：保留它的标记，不算异常退出
        if (isProcessAlive(pid))
            continue;

        const qint64 crashedAt = QFileInfo(marker).lastModified().toSecsSinceEpoch();
        const QString id = QStringLiteral("%1-%2")
                               .arg(crashedAt)
                               .arg(QUuid::createUuid()
                                        .toString(QUuid::WithoutBraces)
                                        .remove(QLatin1Char('-'))
                                        .left(8));
        writeTextReport(crashDirectory(), id, crashedAt > 0 ? crashedAt : started,
                        QStringLiteral("unexpected_exit"), QStringLiteral("UNEXPECTED_EXIT"),
                        phase, page, QString(), logTail,
                        QStringLiteral("上次运行没有留下正常退出记录"),
                        started > 0 && crashedAt > started ? crashedAt - started : 0,
                        version);
        QFile::remove(marker);
    }
}

void CrashReporter::cleanupOldReports()
{
    QDir dir(crashDirectory());
    if (!dir.exists())
        return;

    const QDateTime now = QDateTime::currentDateTime();
    QVector<CrashReport> all;
    const QStringList entries =
        dir.entryList({QStringLiteral("crash-*.crash"), QStringLiteral("crash-*.crash.sent")},
                      QDir::Files, QDir::Time);
    for (const QString &name : entries) {
        const QString path = dir.filePath(name);
        const CrashReport report = parseReportFile(path);
        if (report.sent) {
            const QFileInfo info(path);
            if (info.lastModified().daysTo(now) > kSentReportKeepDays) {
                QFile::remove(path);
                continue;
            }
        }
        all.append(report);
    }

    if (all.size() <= kMaxStoredReports)
        return;

    std::sort(all.begin(), all.end(), newerFirst);
    for (int i = kMaxStoredReports; i < all.size(); ++i)
        QFile::remove(all.at(i).filePath);
}

QVector<CrashReport> CrashReporter::reports(bool includeSent) const
{
    QVector<CrashReport> result;
    QDir dir(crashDirectory());
    if (!dir.exists())
        return result;

    const QStringList entries =
        dir.entryList({QStringLiteral("crash-*.crash"), QStringLiteral("crash-*.crash.sent")},
                      QDir::Files, QDir::Time);
    for (const QString &name : entries) {
        const CrashReport report = parseReportFile(dir.filePath(name));
        if (!report.isValid())
            continue;
        if (!includeSent && report.sent)
            continue;
        result.append(report);
    }
    std::sort(result.begin(), result.end(), newerFirst);
    return result;
}

QVector<CrashReport> CrashReporter::pendingReports() const
{
    QVector<CrashReport> result;
    for (const CrashReport &report : reports(false)) {
        if (!report.sent)
            result.append(report);
    }
    return result;
}

int CrashReporter::pendingCount() const
{
    return pendingReports().size();
}

void CrashReporter::notifyPendingChanged()
{
    emit pendingCountChanged(pendingCount());
}

void CrashReporter::markSent(const QString &filePath)
{
    if (filePath.endsWith(QStringLiteral(".sent")))
        return;
    QFile::rename(filePath, filePath + QStringLiteral(".sent"));
}

void CrashReporter::deleteReport(const CrashReport &report)
{
    if (report.filePath.isEmpty())
        return;
    QFile::remove(report.filePath);
    notifyPendingChanged();
}

QJsonObject CrashReporter::buildPayload(const CrashReport &report) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("report_id"), report.id);
    payload.insert(QStringLiteral("install_id"), installId());
    payload.insert(QStringLiteral("app_version"),
                   report.appVersion.isEmpty() ? AppVersion::current() : report.appVersion);
    payload.insert(QStringLiteral("platform"),
                   report.platform.isEmpty() ? platformKey() : report.platform);
    payload.insert(QStringLiteral("os"), report.os);
    payload.insert(QStringLiteral("os_version"), report.osVersion);
    payload.insert(QStringLiteral("arch"), report.arch);
    payload.insert(QStringLiteral("locale"), report.locale);
    payload.insert(QStringLiteral("reason"), report.reason);
    payload.insert(QStringLiteral("signal"), report.signalName);
    payload.insert(QStringLiteral("exception_code"), report.exceptionCode);
    payload.insert(QStringLiteral("fault_module"), report.faultModule);
    payload.insert(QStringLiteral("thread"), report.thread);
    payload.insert(QStringLiteral("phase"), report.phase);
    payload.insert(QStringLiteral("page"), report.page);
    payload.insert(QStringLiteral("uptime_seconds"), report.uptimeSeconds);
    payload.insert(QStringLiteral("crash_time"), report.timestamp);
    // 上传的都是诊断文本：统一脱敏，去掉用户名 / 用户目录 / 邮箱 / IP / 令牌
    payload.insert(QStringLiteral("stack"), LogSanitizer::sanitize(report.stack));
    payload.insert(QStringLiteral("log_tail"), LogSanitizer::sanitize(report.logTail));
    payload.insert(QStringLiteral("context"), LogSanitizer::sanitize(report.context));
    return payload;
}

void CrashReporter::uploadReport(const CrashReport &report, UploadHandler done)
{
    if (!report.isValid()) {
        if (done)
            done(false, tr("崩溃报告文件不存在"));
        return;
    }

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    const QUrl url(SettingsManager::instance()->forumBaseUrl()
                   + QStringLiteral("/api/app/crash-reports"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/json; charset=utf-8"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BlockBoxLauncher/%1").arg(AppVersion::current()));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-Install-Id", installId().toUtf8());
    request.setTransferTimeout(20000);

    const QByteArray body = QJsonDocument(buildPayload(report)).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, reply,
            [this, reply, report, done = std::move(done)]() mutable {
                reply->deleteLater();
                const QByteArray raw = reply->readAll();
                if (reply->error() == QNetworkReply::NoError) {
                    markSent(report.filePath);
                    notifyPendingChanged();
                    if (done)
                        done(true, QString());
                    return;
                }

                QJsonParseError parseError{};
                const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
                QString message = parseError.error == QJsonParseError::NoError
                    ? doc.object().value(QStringLiteral("error")).toString()
                    : QString();
                if (message.isEmpty()) {
                    const int status =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    message = status > 0
                        ? tr("上传失败（HTTP %1）").arg(status)
                        : tr("网络连接失败，报告已保存在本地");
                }
                if (done)
                    done(false, message);
            });
}

void CrashReporter::uploadPendingReports(std::function<void(int, int)> done)
{
    const QVector<CrashReport> pending = pendingReports();
    if (pending.isEmpty()) {
        if (done)
            done(0, 0);
        return;
    }

    struct State
    {
        int remaining = 0;
        int ok = 0;
        int failed = 0;
        std::function<void(int, int)> done;
    };
    auto state = std::make_shared<State>();
    state->remaining = pending.size();
    state->done = std::move(done);

    for (const CrashReport &report : pending) {
        uploadReport(report, [this, state](bool ok, const QString &) {
            if (ok)
                state->ok += 1;
            else
                state->failed += 1;
            state->remaining -= 1;
            if (state->remaining <= 0 && state->done)
                state->done(state->ok, state->failed);
        });
    }
}

void CrashReporter::processPendingReports(QWidget *parent, std::function<void()> done)
{
    const QVector<CrashReport> pending = pendingReports();
    if (pending.isEmpty()) {
        if (done)
            done();
        return;
    }

    const CrashReport report = pending.first();
    if (autoSendEnabled() && sendConsent()) {
        // 已经明确勾选过「自动发送」：静默上传，不再打扰用户
        uploadReport(report, [this, parent, done](bool ok, const QString &) {
            // 其余积压的报告一并补传
            const QVector<CrashReport> rest = pendingReports();
            for (const CrashReport &item : rest)
                uploadReport(item);
            if (ok && parent) {
                NotificationManager::showInfo(
                    parent, tr("上次的崩溃报告已自动发送，感谢反馈"));
            }
            if (done)
                done();
        });
        return;
    }

    CrashReportDialog dialog(report, parent);
    dialog.exec();

    if (dialog.choice() == CrashReportDialog::Send) {
        if (dialog.autoSendChecked()) {
            setAutoSendEnabled(true);
            setSendConsent(true);
        }
        if (dialog.consentChecked()) {
            setSendConsent(true);
            uploadReport(report, [this, parent](bool ok, const QString &message) {
                if (parent) {
                    if (ok)
                        NotificationManager::showInfo(parent, tr("崩溃报告已发送，感谢反馈"));
                    else
                        NotificationManager::showError(
                            parent, tr("崩溃报告暂时没发出去：%1").arg(message));
                }
            });
        }
    } else if (dialog.choice() == CrashReportDialog::Delete) {
        deleteReport(report);
    }

    if (done)
        done();
}
