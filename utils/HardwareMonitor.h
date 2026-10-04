#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <pdh.h>
#endif

struct HardwareData
{
    int cpuUsagePercent = 0;
    int memoryUsagePercent = 0;
    int networkUsagePercent = 0;
    int gpu0UsagePercent = 0;
    int gpu1UsagePercent = 0;
    bool cpuAvailable = false;
    bool memoryAvailable = false;
    bool networkAvailable = false;
    bool gpu0Available = false;
    bool gpu1Available = false;
};

class HardwareMonitor : public QObject
{
    Q_OBJECT

public:
    explicit HardwareMonitor(QObject *parent = nullptr);
    ~HardwareMonitor();

    void start(int intervalMs = 2000);
    void stop();
    bool isRunning() const;

    const HardwareData &currentData() const { return m_data; }

signals:
    void dataRefreshed(const HardwareData &data);

private slots:
    void refresh();

private:
    void refreshCpu();
    void refreshMemory();
    void refreshNetwork();
    void refreshGpu();

    QTimer *m_timer;
    HardwareData m_data;

    // CPU
#ifdef Q_OS_WIN
    ULARGE_INTEGER m_lastIdleTime;
    ULARGE_INTEGER m_lastKernelTime;
    ULARGE_INTEGER m_lastUserTime;
#else
    quint64 m_lastIdleTime;
    quint64 m_lastTotalTime;
#endif
    bool m_cpuInitialized;

    // Network
    struct NetAdapterInfo
    {
        QByteArray name;
        quint64 lastBytesReceived;
        quint64 lastBytesSent;
    };
    QVector<NetAdapterInfo> m_lastNetAdapters;
    bool m_networkInitialized;

    // GPU
#ifdef Q_OS_WIN
    struct GpuCounterInfo
    {
        int adapterIndex;
        HCOUNTER hCounter;
    };
    HQUERY m_gpuQuery;
    QVector<GpuCounterInfo> m_gpuCounters;
#endif
    bool m_gpuInitialized;
    bool m_gpuEverSampled;   ///< whether a valid formatted GPU sample was ever read
    qint64 m_gpuLastEnumMs;  ///< timestamp (ms since epoch) of last GPU enumeration
};
