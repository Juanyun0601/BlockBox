/**
 * @file   MemoryOptimizer.cpp
 * @brief  运行内存优化引擎实现（Windows API 部分集中在这里）
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 三步走，和 PCL CE 的做法对得上：
 *   1. 收自己：压缩堆碎片 → 摘掉自己的工作集（图片缓存由调用方在 GUI 线程先清）；
 *   2. 清系统待机缓存：NtSetSystemInformation(SystemMemoryListInformation, 4)，
 *      需要 SeProfileSingleProcessPrivilege（管理员）；
 *   3. 收后台：枚举进程逐个 EmptyWorkingSet，跳过游戏 JVM、前台窗口和系统关键进程。
 */

#include "MemoryOptimizer.h"

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <psapi.h>
#  include <tlhelp32.h>
#endif

#ifdef Q_OS_WIN

namespace {

// NtSetSystemInformation 的内存列表命令：Windows 内部未公开，但多年未变
constexpr ULONG kSystemMemoryListInformation = 80;
constexpr ULONG kMemoryPurgeStandbyList = 4;

// 工作集小于这个数的进程收不出多少东西，扫它们只是白花时间
constexpr qint64 kMinWorkingSetBytes = 16LL * 1024 * 1024;

/** 当前系统可用物理内存（含待机缓存，和 PCL 的统计口径一致） */
qint64 availablePhysicalBytes()
{
    MEMORYSTATUSEX status;
    ZeroMemory(&status, sizeof(status));
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return -1;
    return static_cast<qint64>(status.ullAvailPhys);
}

/** 指定进程当前的工作集大小 */
qint64 workingSetBytes(HANDLE process)
{
    PROCESS_MEMORY_COUNTERS_EX counters;
    ZeroMemory(&counters, sizeof(counters));
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(process,
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters)))
        return -1;
    return static_cast<qint64>(counters.WorkingSetSize);
}

/** 给当前进程打开一个特权（没有管理员权限时必然失败，失败就安静返回 false） */
bool enablePrivilege(const wchar_t *name)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;

    bool enabled = false;
    LUID luid;
    ZeroMemory(&luid, sizeof(luid));
    if (LookupPrivilegeValueW(nullptr, name, &luid))
    {
        TOKEN_PRIVILEGES privileges;
        ZeroMemory(&privileges, sizeof(privileges));
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Luid = luid;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr)
            && GetLastError() == ERROR_SUCCESS)
        {
            enabled = true;
        }
    }

    CloseHandle(token);
    return enabled;
}

/** 清空系统待机缓存（待机页本身没人用了，清掉之后才有真正空闲的物理内存） */
bool purgeStandbyList()
{
    using NtSetSystemInformationFn = LONG(NTAPI *)(ULONG, PVOID, ULONG);

    static NtSetSystemInformationFn setSystemInformation = []() -> NtSetSystemInformationFn {
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll)
            return nullptr;
        return reinterpret_cast<NtSetSystemInformationFn>(
            reinterpret_cast<void *>(GetProcAddress(ntdll, "NtSetSystemInformation")));
    }();

    if (!setSystemInformation)
        return false;

    ULONG command = kMemoryPurgeStandbyList;
    return setSystemInformation(kSystemMemoryListInformation, &command, sizeof(command)) >= 0;
}

/** 这些进程一律不碰：系统关键进程、桌面绘制进程，以及正在跑的游戏 JVM */
bool isProtectedProcessName(const QString &name)
{
    static const char *const kProtected[] = {
        "system", "registry", "[system process]", "memory compression", "secure system",
        "smss.exe", "csrss.exe", "wininit.exe", "winlogon.exe", "services.exe",
        "lsass.exe", "lsaiso.exe", "fontdrvhost.exe", "dwm.exe", "audiodg.exe",
        "java.exe", "javaw.exe"
    };
    for (const char *candidate : kProtected)
    {
        if (name == QLatin1String(candidate))
            return true;
    }
    return false;
}

struct TrimStats
{
    int trimmed = 0; // 成功收回的进程数
    int skipped = 0; // 跳过 / 没权限 / 太小的进程数
};

/** 把后台进程的闲置工作集逐个摘掉（不结束进程，也不改它们的优先级） */
TrimStats trimBackgroundProcesses()
{
    TrimStats stats;

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return stats;

    const DWORD selfPid = GetCurrentProcessId();
    DWORD foregroundPid = 0;
    const HWND foreground = GetForegroundWindow();
    if (foreground)
        GetWindowThreadProcessId(foreground, &foregroundPid);

    PROCESSENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            const DWORD pid = entry.th32ProcessID;
            // 0 / 4 是内核与 System，本进程留给上面单独处理
            if (pid == 0 || pid == 4 || pid == selfPid)
            {
                ++stats.skipped;
                continue;
            }

            const QString name = QString::fromWCharArray(entry.szExeFile).toLower();
            if (isProtectedProcessName(name))
            {
                ++stats.skipped;
                continue;
            }

            // 前台窗口正在用：收掉它马上又要重画，只会让人看着更卡
            if (foregroundPid != 0 && pid == foregroundPid)
            {
                ++stats.skipped;
                continue;
            }

            // 没权限的（系统进程、别的账户）直接跳过，不做任何提权尝试
            HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA,
                                         FALSE, pid);
            if (!process)
            {
                ++stats.skipped;
                continue;
            }

            const qint64 working = workingSetBytes(process);
            if (working < kMinWorkingSetBytes)
            {
                CloseHandle(process);
                continue;
            }

            if (EmptyWorkingSet(process))
            {
                ++stats.trimmed;
            }
            else
            {
                ++stats.skipped;
            }

            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return stats;
}

/** 收自己：先压堆碎片，再摘工作集，让闲置页尽快变成可回收的待机页 */
void trimOwnProcess()
{
    HANDLE heap = GetProcessHeap();
    if (heap)
        HeapCompact(heap, 0);

    EmptyWorkingSet(GetCurrentProcess());
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

} // namespace

#endif // Q_OS_WIN

bool MemoryOptimizer::isElevated()
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation;
    ZeroMemory(&elevation, sizeof(elevation));
    DWORD returned = sizeof(elevation);
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                        sizeof(elevation), &returned);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
#else
    return false;
#endif
}

MemoryOptimizeReport MemoryOptimizer::optimize(bool trimOtherProcesses, bool purgeSystemCache)
{
    MemoryOptimizeReport report;

#ifdef Q_OS_WIN
    report.supported = true;
    report.elevated = isElevated();

    if (report.elevated)
    {
        // 收别人的内存、清系统缓存都要特权；非管理员申请也是白申请，索性不试
        enablePrivilege(SE_DEBUG_NAME);
        enablePrivilege(SE_PROF_SINGLE_PROCESS_NAME);
    }

    report.availableBeforeBytes = availablePhysicalBytes();
    report.launcherBeforeBytes = workingSetBytes(GetCurrentProcess());

    // 1. 先收自己
    trimOwnProcess();

    // 2. 系统待机缓存
    if (purgeSystemCache && report.elevated)
        report.systemCachePurged = purgeStandbyList();

    // 3. 后台进程闲置的那部分
    if (trimOtherProcesses)
    {
        const TrimStats stats = trimBackgroundProcesses();
        report.processesTrimmed = stats.trimmed;
        report.processesSkipped = stats.skipped;
    }

    report.launcherAfterBytes = workingSetBytes(GetCurrentProcess());
    report.availableAfterBytes = availablePhysicalBytes();
#else
    Q_UNUSED(trimOtherProcesses)
    Q_UNUSED(purgeSystemCache)
#endif

    return report;
}
