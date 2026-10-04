#include "HardwareMonitor.h"

#include <cstdlib>
#include <QtGlobal>
#include <QDateTime>
#include <QDebug>
#include <QStringList>
#include <QFile>
#include <QTextStream>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdhmsg.h>
#include <wchar.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "pdh.lib")
#endif

HardwareMonitor::HardwareMonitor(QObject *parent)
    : QObject(parent)
    , m_timer(nullptr)
    , m_cpuInitialized(false)
    , m_networkInitialized(false)
#ifdef Q_OS_WIN
    , m_gpuQuery(nullptr)
#endif
    , m_gpuInitialized(false)
    , m_gpuEverSampled(false)
    , m_gpuLastEnumMs(0)
{
#ifdef Q_OS_WIN
    m_lastIdleTime.QuadPart = 0;
    m_lastKernelTime.QuadPart = 0;
    m_lastUserTime.QuadPart = 0;
#else
    m_lastIdleTime = 0;
    m_lastTotalTime = 0;
#endif
}

HardwareMonitor::~HardwareMonitor()
{
    stop();
}

void HardwareMonitor::start(int intervalMs)
{
    if (m_timer)
    {
        m_timer->stop();
        delete m_timer;
    }

    refreshCpu();
    refreshMemory();
    refreshNetwork();
    refreshGpu();
    emit dataRefreshed(m_data);

    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    QObject::connect(m_timer, &QTimer::timeout, this, &HardwareMonitor::refresh);
    m_timer->start();
}

void HardwareMonitor::stop()
{
    if (m_timer)
    {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }

#ifdef Q_OS_WIN
    if (m_gpuQuery)
    {
        PdhCloseQuery(m_gpuQuery);
        m_gpuQuery = nullptr;
    }
    m_gpuCounters.clear();
    m_gpuInitialized = false;
    m_gpuEverSampled = false;
    m_gpuLastEnumMs = 0;
#endif
}

bool HardwareMonitor::isRunning() const
{
    return m_timer && m_timer->isActive();
}

void HardwareMonitor::refresh()
{
    refreshCpu();
    refreshMemory();
    refreshNetwork();
    refreshGpu();
    emit dataRefreshed(m_data);
}

// ============================================================================
// CPU
// ============================================================================

void HardwareMonitor::refreshCpu()
{
#ifdef Q_OS_WIN
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
    {
        m_data.cpuAvailable = false;
        return;
    }

    ULARGE_INTEGER idle, kernel, user;
    idle.LowPart = idleTime.dwLowDateTime;
    idle.HighPart = idleTime.dwHighDateTime;
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    if (m_cpuInitialized)
    {
        ULONGLONG idleDiff = idle.QuadPart - m_lastIdleTime.QuadPart;
        ULONGLONG kernelDiff = kernel.QuadPart - m_lastKernelTime.QuadPart;
        ULONGLONG userDiff = user.QuadPart - m_lastUserTime.QuadPart;
        ULONGLONG totalDiff = kernelDiff + userDiff;

        if (totalDiff > 0)
        {
            int usage = static_cast<int>((totalDiff - idleDiff) * 100 / totalDiff);
            if (usage < 0) usage = 0;
            if (usage > 100) usage = 100;
            m_data.cpuUsagePercent = usage;
        }
        m_data.cpuAvailable = true;
    }

    m_lastIdleTime = idle;
    m_lastKernelTime = kernel;
    m_lastUserTime = user;
    m_cpuInitialized = true;
    // Baseline captured: CPU data is available (usage computed from the
    // next refresh onward, but the card must not show "unavailable").
    m_data.cpuAvailable = true;
#elif defined(Q_OS_ANDROID) || defined(Q_OS_LINUX)
    // Read /proc/stat for CPU usage
    QFile statFile("/proc/stat");
    if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_data.cpuAvailable = false;
        return;
    }

    QTextStream stream(&statFile);
    QString line = stream.readLine(); // First line: "cpu user nice system idle iowait irq softirq steal"
    statFile.close();

    QStringList values = line.split(QRegularExpression("\\s+"));
    if (values.size() < 5)
    {
        m_data.cpuAvailable = false;
        return;
    }

    // values[0] is "cpu", values[1..] are the counters
    quint64 user = values[1].toULongLong();
    quint64 nice = values[2].toULongLong();
    quint64 system = values[3].toULongLong();
    quint64 idle = values[4].toULongLong();
    quint64 iowait = (values.size() > 5) ? values[5].toULongLong() : 0;
    quint64 irq = (values.size() > 6) ? values[6].toULongLong() : 0;
    quint64 softirq = (values.size() > 7) ? values[7].toULongLong() : 0;
    quint64 steal = (values.size() > 8) ? values[8].toULongLong() : 0;

    quint64 totalTime = user + nice + system + idle + iowait + irq + softirq + steal;
    quint64 idleTime = idle + iowait;

    if (m_cpuInitialized)
    {
        quint64 totalDiff = totalTime - m_lastTotalTime;
        quint64 idleDiff = idleTime - m_lastIdleTime;

        if (totalDiff > 0)
        {
            int usage = static_cast<int>((totalDiff - idleDiff) * 100 / totalDiff);
            if (usage < 0) usage = 0;
            if (usage > 100) usage = 100;
            m_data.cpuUsagePercent = usage;
        }
        m_data.cpuAvailable = true;
    }

    m_lastIdleTime = idleTime;
    m_lastTotalTime = totalTime;
    m_cpuInitialized = true;
    m_data.cpuAvailable = true;
#else
    m_data.cpuAvailable = false;
#endif
}

// ============================================================================
// Memory
// ============================================================================

void HardwareMonitor::refreshMemory()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus))
    {
        m_data.memoryUsagePercent = static_cast<int>(memStatus.dwMemoryLoad);
        m_data.memoryAvailable = true;
    }
    else
    {
        m_data.memoryAvailable = false;
    }
#elif defined(Q_OS_ANDROID) || defined(Q_OS_LINUX)
    // Read /proc/meminfo for memory usage
    QFile memFile("/proc/meminfo");
    if (!memFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_data.memoryAvailable = false;
        return;
    }

    QTextStream stream(&memFile);
    quint64 totalMem = 0;
    quint64 freeMem = 0;
    quint64 availableMem = 0;

    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        if (line.startsWith("MemTotal:"))
        {
            QStringList parts = line.split(QRegularExpression("\\s+"));
            if (parts.size() >= 2)
                totalMem = parts[1].toULongLong() * 1024; // Convert kB to bytes
        }
        else if (line.startsWith("MemFree:"))
        {
            QStringList parts = line.split(QRegularExpression("\\s+"));
            if (parts.size() >= 2)
                freeMem = parts[1].toULongLong() * 1024;
        }
        else if (line.startsWith("MemAvailable:"))
        {
            QStringList parts = line.split(QRegularExpression("\\s+"));
            if (parts.size() >= 2)
                availableMem = parts[1].toULongLong() * 1024;
        }
    }
    memFile.close();

    if (totalMem > 0)
    {
        quint64 usedMem = totalMem - (availableMem > 0 ? availableMem : freeMem);
        int usage = static_cast<int>(usedMem * 100 / totalMem);
        if (usage < 0) usage = 0;
        if (usage > 100) usage = 100;
        m_data.memoryUsagePercent = usage;
        m_data.memoryAvailable = true;
    }
    else
    {
        m_data.memoryAvailable = false;
    }
#else
    m_data.memoryAvailable = false;
#endif
}

// ============================================================================
// Network
// ============================================================================

void HardwareMonitor::refreshNetwork()
{
#ifdef Q_OS_WIN
    ULONG bufSize = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufSize);
    if (bufSize == 0)
    {
        m_data.networkAvailable = false;
        return;
    }

    IP_ADAPTER_ADDRESSES *adapterAddresses =
        reinterpret_cast<IP_ADAPTER_ADDRESSES *>(malloc(bufSize));
    if (!adapterAddresses)
    {
        m_data.networkAvailable = false;
        return;
    }

    ULONG ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX,
                                     nullptr, adapterAddresses, &bufSize);
    if (ret != ERROR_SUCCESS)
    {
        free(adapterAddresses);
        m_data.networkAvailable = false;
        return;
    }

    QVector<NetAdapterInfo> currentAdapters;
    quint64 totalBytesPerSec = 0;

    for (IP_ADAPTER_ADDRESSES *adapter = adapterAddresses;
         adapter != nullptr; adapter = adapter->Next)
    {
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;

        MIB_IF_ROW2 ifRow;
        ifRow.InterfaceIndex = adapter->IfIndex;
        if (GetIfEntry2(&ifRow) == NO_ERROR)
        {
            // AdapterName is a char* C string on MinGW (WCHAR[] on MSVC);
            // on MinGW it is already a readable adapter-name string.
            QByteArray name = QByteArray(adapter->AdapterName);
            quint64 recvBytes = ifRow.InOctets;
            quint64 sentBytes = ifRow.OutOctets;

            // Find previous sample
            for (const auto &prev : m_lastNetAdapters)
            {
                if (prev.name == name)
                {
                    // Guard against counter reset/underflow on the interface
                    quint64 recvDiff = (recvBytes >= prev.lastBytesReceived)
                                           ? recvBytes - prev.lastBytesReceived : 0;
                    quint64 sentDiff = (sentBytes >= prev.lastBytesSent)
                                           ? sentBytes - prev.lastBytesSent : 0;
                    totalBytesPerSec += recvDiff + sentDiff;
                    break;
                }
            }

            NetAdapterInfo info;
            info.name = name;
            info.lastBytesReceived = recvBytes;
            info.lastBytesSent = sentBytes;
            currentAdapters.append(info);
        }
    }

    free(adapterAddresses);

    m_lastNetAdapters = currentAdapters;

    if (m_networkInitialized)
    {
        const quint64 maxRate = 125000000ULL; // ~1 Gbps in bytes/sec
        int pct = static_cast<int>(totalBytesPerSec * 100 / maxRate);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        m_data.networkUsagePercent = pct;
    }
    else
    {
        m_networkInitialized = true;
    }

    m_data.networkAvailable = true;
#elif defined(Q_OS_ANDROID) || defined(Q_OS_LINUX)
    // Read /proc/net/dev for network usage
    QFile netFile("/proc/net/dev");
    if (!netFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_data.networkAvailable = false;
        return;
    }

    QTextStream stream(&netFile);
    QVector<NetAdapterInfo> currentAdapters;
    quint64 totalBytesPerSec = 0;

    // Skip header lines
    stream.readLine();
    stream.readLine();

    while (!stream.atEnd())
    {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty())
            continue;

        // Format: "interface: bytes received packets errors drops fifo frame compressed multicast bytes sent ..."
        int colonIdx = line.indexOf(':');
        if (colonIdx < 0)
            continue;

        QString iface = line.left(colonIdx).trimmed();
        if (iface == "lo") // Skip loopback
            continue;

        QStringList values = line.mid(colonIdx + 1).split(QRegularExpression("\\s+"));
        if (values.size() < 10)
            continue;

        quint64 recvBytes = values[0].toULongLong();
        quint64 sentBytes = values[8].toULongLong();

        QByteArray name = iface.toUtf8();

        // Find previous sample
        for (const auto &prev : m_lastNetAdapters)
        {
            if (prev.name == name)
            {
                quint64 recvDiff = (recvBytes >= prev.lastBytesReceived)
                                       ? recvBytes - prev.lastBytesReceived : 0;
                quint64 sentDiff = (sentBytes >= prev.lastBytesSent)
                                       ? sentBytes - prev.lastBytesSent : 0;
                totalBytesPerSec += recvDiff + sentDiff;
                break;
            }
        }

        NetAdapterInfo info;
        info.name = name;
        info.lastBytesReceived = recvBytes;
        info.lastBytesSent = sentBytes;
        currentAdapters.append(info);
    }
    netFile.close();

    m_lastNetAdapters = currentAdapters;

    if (m_networkInitialized)
    {
        const quint64 maxRate = 125000000ULL; // ~1 Gbps in bytes/sec
        int pct = static_cast<int>(totalBytesPerSec * 100 / maxRate);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        m_data.networkUsagePercent = pct;
    }
    else
    {
        m_networkInitialized = true;
    }

    m_data.networkAvailable = true;
#else
    m_data.networkAvailable = false;
#endif
}

// ============================================================================
// GPU (via PDH "GPU Engine" counters, Windows 10+)
// ============================================================================

#ifdef Q_OS_WIN
/**
 * @brief Checks whether a double-null-terminated PDH name list contains GPU
 *        engine instance names (they embed a "phys_" / "pid_" token), as
 *        opposed to plain counter names.
 */
static bool containsInstanceToken(const wchar_t *list)
{
    for (const wchar_t *it = list; *it; it += wcslen(it) + 1)
    {
        if (wcsstr(it, L"phys_") || wcsstr(it, L"pid_"))
            return true;
    }
    return false;
}
#endif

void HardwareMonitor::refreshGpu()
{
#ifdef Q_OS_WIN
    static const qint64 kGpuReenumMs = 30000; // 30s

    // Periodically re-enumerate GPU engine counters so adapters that only
    // expose instances once they become active (e.g. hybrid laptop discrete
    // GPUs) are picked up instead of staying "unavailable".
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_gpuInitialized && (nowMs - m_gpuLastEnumMs) >= kGpuReenumMs)
    {
        if (m_gpuQuery)
        {
            PdhCloseQuery(m_gpuQuery);
            m_gpuQuery = nullptr;
        }
        m_gpuCounters.clear();
        m_gpuInitialized = false;
    }

    if (!m_gpuInitialized)
    {
        // One-time init: enumerate GPU engines and add counters.
        // On any failure the query is closed and m_gpuInitialized stays false
        // so initialization is retried on the next refresh (avoid permanently
        // leaving the GPU marked "unavailable" after a transient failure).
        HQUERY query = nullptr;
        if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS)
        {
            m_data.gpu0Available = false;
            m_data.gpu1Available = false;
            return;
        }

        // Query required buffer sizes. Note: the two output lists of
        // PdhEnumObjectItems are declared in opposite order by the MinGW
        // and MSVC headers, so sizes/buffers are handled generically and
        // the instance list is identified by content afterwards.
        DWORD listSizeA = 0;
        DWORD listSizeB = 0;
        DWORD ret = PdhEnumObjectItems(nullptr, nullptr, L"GPU Engine",
                                       nullptr, &listSizeA,
                                       nullptr, &listSizeB,
                                       PERF_DETAIL_WIZARD, 0);
        if (ret != PDH_MORE_DATA || (listSizeA == 0 && listSizeB == 0))
        {
            // GPU Engine object not present (e.g. no WDDM driver)
            PdhCloseQuery(query);
            m_data.gpu0Available = false;
            m_data.gpu1Available = false;
            return;
        }

        const DWORD bufSize = qMax(listSizeA, listSizeB) + 1;
        QVector<wchar_t> bufA(bufSize);
        QVector<wchar_t> bufB(bufSize);
        DWORD capA = bufSize;
        DWORD capB = bufSize;

        ret = PdhEnumObjectItems(nullptr, nullptr, L"GPU Engine",
                                 bufA.data(), &capA,
                                 bufB.data(), &capB,
                                 PERF_DETAIL_WIZARD, 0);
        if (ret != ERROR_SUCCESS)
        {
            PdhCloseQuery(query);
            m_data.gpu0Available = false;
            m_data.gpu1Available = false;
            return;
        }

        // Instance names look like "pid_1234_luid_..._phys_0_eng_0_engtype_3D";
        // the other list holds only counter names ("Utilization Percentage").
        const wchar_t *instanceList = bufA.constData();
        if (!containsInstanceToken(instanceList))
            instanceList = bufB.constData();

        int maxAdapterIndex = -1;
        QVector<GpuCounterInfo> counters;
        for (const wchar_t *inst = instanceList; *inst; inst += wcslen(inst) + 1)
        {
            // Instance format (Windows 10+):
            //   pid_<pid>_luid_<hi>_<lo>_phys_<adapterIdx>_eng_<engineIdx>_engtype_<type>
            // Older format (pre-1903): pid_<pid>_phys_<adapterIdx>_eng_<engineIdx>_engtype_<type>
            // Extract the adapter index from the "phys" token.
            QString instStr = QString::fromWCharArray(inst);
            QStringList parts = instStr.split('_');
            int adapterIdx = -1;
            for (int i = 0; i + 1 < parts.size(); ++i)
            {
                if (parts[i] == QLatin1String("phys"))
                {
                    bool ok = false;
                    int idx = parts[i + 1].toInt(&ok);
                    adapterIdx = ok ? idx : -1;
                    break;
                }
            }
            if (adapterIdx < 0)
                continue;

            wchar_t fullPath[1024];
            swprintf_s(fullPath, L"\\GPU Engine(%s)\\Utilization Percentage", inst);

            HCOUNTER hCounter = nullptr;
            if (PdhAddCounter(query, fullPath, 0, &hCounter) == ERROR_SUCCESS)
            {
                GpuCounterInfo info;
                info.adapterIndex = adapterIdx;
                info.hCounter = hCounter;
                counters.append(info);
                if (adapterIdx > maxAdapterIndex)
                    maxAdapterIndex = adapterIdx;
            }
        }

        if (counters.isEmpty())
        {
            PdhCloseQuery(query);
            m_data.gpu0Available = false;
            m_data.gpu1Available = false;
            return;
        }

        m_gpuQuery = query;
        m_gpuCounters = counters;
        m_data.gpu0Available = (maxAdapterIndex >= 0);
        m_data.gpu1Available = (maxAdapterIndex >= 1);
        m_gpuInitialized = true;
        m_gpuLastEnumMs = nowMs;
    }

    if (!m_gpuQuery || m_gpuCounters.isEmpty())
        return;

    PdhCollectQueryData(m_gpuQuery);

    double gpu0Sum = 0;
    double gpu1Sum = 0;
    bool gpu0Valid = false;
    bool gpu1Valid = false;

    for (const auto &counter : m_gpuCounters)
    {
        PDH_FMT_COUNTERVALUE fmtValue;
        if (PdhGetFormattedCounterValue(counter.hCounter,
                                        PDH_FMT_DOUBLE,
                                        nullptr, &fmtValue) == ERROR_SUCCESS)
        {
            if (fmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                fmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
            {
                double val = fmtValue.doubleValue;
                if (val < 0) val = 0;
                if (counter.adapterIndex == 0)
                {
                    gpu0Sum += val;
                    gpu0Valid = true;
                }
                else if (counter.adapterIndex == 1)
                {
                    gpu1Sum += val;
                    gpu1Valid = true;
                }
            }
        }
    }

    if (gpu0Sum > 100) gpu0Sum = 100;
    if (gpu1Sum > 100) gpu1Sum = 100;

    // Only publish valid samples; the refresh right after a re-enumeration
    // has no second data point yet, so carry the previous value over to
    // avoid a spurious dip to 0%.
    if (gpu0Valid)
    {
        m_data.gpu0UsagePercent = static_cast<int>(gpu0Sum);
        m_gpuEverSampled = true;
    }
    else if (!m_gpuEverSampled)
    {
        m_data.gpu0UsagePercent = 0;
    }
    if (gpu1Valid)
    {
        m_data.gpu1UsagePercent = static_cast<int>(gpu1Sum);
        m_gpuEverSampled = true;
    }
    else if (!m_gpuEverSampled)
    {
        m_data.gpu1UsagePercent = 0;
    }

    if (m_gpuCounters.isEmpty())
    {
        m_data.gpu0Available = false;
        m_data.gpu1Available = false;
    }
#elif defined(Q_OS_ANDROID) || defined(Q_OS_LINUX)
    // Android/Linux: GPU usage monitoring is not directly available via /proc
    // The GPU usage is typically reported by the vendor-specific drivers
    // For now, mark GPU as unavailable
    m_data.gpu0Available = false;
    m_data.gpu1Available = false;
#else
    m_data.gpu0Available = false;
    m_data.gpu1Available = false;
#endif
}
