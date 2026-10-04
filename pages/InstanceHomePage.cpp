/**
 * @file   InstanceHomePage.cpp
 * @brief  实例助手 - 首页实现：性能监控 + 游玩信息
 * @author BlockBox Team
 * @date   2026-07-21
 */
#include "InstanceHomePage.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "components/PerfMonitorCard.h"
#include "components/OutlinedLabel.h"
#include "utils/HardwareMonitor.h"
#include "utils/ThemeManager.h"
#include "utils/CommandAssistant/GameDetector.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#endif

namespace {
/**
 * @brief 查找运行中的 javaw.exe 进程 ID
 * @return 第一个 javaw.exe 的 PID（未找到返回 0）
 */
#ifdef Q_OS_WIN
DWORD findJavaProcessId()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"javaw.exe") == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return pid;
}

/**
 * @brief 获取指定进程的创建时间（毫秒，UTC epoch）
 * @param pid 进程 ID
 * @return 创建时间戳，失败返回 0
 */
qint64 getProcessCreationTimeMs(DWORD pid)
{
    if (pid == 0)
    {
        return 0;
    }
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess)
    {
        return 0;
    }
    FILETIME creationTime = {};
    FILETIME exitTime = {};
    FILETIME kernelTime = {};
    FILETIME userTime = {};
    BOOL ok = GetProcessTimes(hProcess, &creationTime, &exitTime, &kernelTime, &userTime);
    CloseHandle(hProcess);
    if (!ok)
    {
        return 0;
    }
    // FILETIME 是 100ns 间隔，从 1601-01-01 起；转换为 Unix epoch 毫秒
    ULARGE_INTEGER li;
    li.LowPart = creationTime.dwLowDateTime;
    li.HighPart = creationTime.dwHighDateTime;
    const qint64 FILETIME_PER_MS = 10000LL;
    const qint64 EPOCH_DIFF_MS = 11644473600000LL; // 1601→1970
    return static_cast<qint64>(li.QuadPart) / FILETIME_PER_MS - EPOCH_DIFF_MS;
}

/**
 * @brief 枚举所有顶级窗口，查找属于指定进程 ID 的可见窗口
 * @param pid 目标进程 ID
 * @return 找到的窗口句柄（未找到返回 nullptr）
 */
HWND findWindowByPid(DWORD pid)
{
    struct EnumData
    {
        DWORD targetPid;
        HWND result;
    };
    EnumData data{pid, nullptr};

    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lParam);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(hWnd, &windowPid);
        if (windowPid != d->targetPid)
        {
            return TRUE;
        }
        if (!IsWindowVisible(hWnd))
        {
            return TRUE;
        }
        wchar_t title[256] = {0};
        GetWindowTextW(hWnd, title, 256);
        if (wcslen(title) > 0)
        {
            d->result = hWnd;
            return FALSE;
        }
        if (d->result == nullptr)
        {
            d->result = hWnd;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.result;
}
#endif // Q_OS_WIN

/**
 * @brief 将毫秒数格式化为 H:MM:SS 字符串
 */
QString formatDuration(qint64 ms)
{
    qint64 totalSec = ms / 1000;
    qint64 h = totalSec / 3600;
    qint64 m = (totalSec % 3600) / 60;
    qint64 s = totalSec % 60;
    return QString("%1:%2:%3")
        .arg(h)
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}
} // namespace

InstanceHomePage::InstanceHomePage(QWidget *parent)
    : QWidget(parent)
    , m_currentTimeValue(nullptr)
    , m_playTimeValue(nullptr)
    , m_fpsValue(nullptr)
    , m_cpuCard(nullptr)
    , m_memoryCard(nullptr)
    , m_gpuCard(nullptr)
    , m_hardwareMonitor(nullptr)
    , m_tickTimer(nullptr)
    , m_gameStartTimeMs(0)
#ifdef Q_OS_WIN
    , m_gameWnd(nullptr)
    , m_lastDwmFrame(0)
    , m_lastDwmQueryMs(0)
#endif
{
    initUI();
    applyThemeStyles();

    // 监听主题色变化，更新卡片强调色
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &InstanceHomePage::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged,
            this, &InstanceHomePage::applyThemeStyles);

    // 每秒刷新当前时间、游玩时间、帧率与游戏状态
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(1000);
    connect(m_tickTimer, &QTimer::timeout, this, &InstanceHomePage::onSecondTick);
    m_tickTimer->start();
    onSecondTick(); // 立即刷新一次

    // 启动硬件监控（2 秒采样一次）
    m_hardwareMonitor = new HardwareMonitor(this);
    connect(m_hardwareMonitor, &HardwareMonitor::dataRefreshed,
            this, [this](const HardwareData &) { onHardwareDataRefreshed(); });
    m_hardwareMonitor->start(2000);
}

InstanceHomePage::~InstanceHomePage()
{
    if (m_hardwareMonitor)
    {
        m_hardwareMonitor->stop();
    }
}

void InstanceHomePage::setInstancePath(const QString &path)
{
    m_instancePath = path;
}

void InstanceHomePage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // ---- 游玩信息卡片：3 个并列子项 ----
    auto *infoContainer = new QWidget(this);
    infoContainer->setObjectName("homeInfoContainer");
    auto *infoLayout = new QHBoxLayout(infoContainer);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(6);

    auto buildInfoCell = [this](const QString &title, QLabel *&valueLabel) -> QWidget * {
        auto *cell = new QWidget(this);
        cell->setObjectName("homeInfoCell");
        auto *v = new QVBoxLayout(cell);
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(4);

        auto *titleLabel = new QLabel(title, cell);
        titleLabel->setObjectName("homeInfoTitle");
        titleLabel->setAlignment(Qt::AlignCenter);

        valueLabel = new QLabel(QStringLiteral("--"), cell);
        valueLabel->setObjectName("homeInfoValue");
        valueLabel->setAlignment(Qt::AlignCenter);
        QFont valueFont = valueLabel->font();
        valueFont.setPointSize(14);
        valueFont.setBold(true);
        valueLabel->setFont(valueFont);

        v->addWidget(titleLabel);
        v->addWidget(valueLabel);
        return cell;
    };

    infoLayout->addWidget(buildInfoCell(tr("当前时间"), m_currentTimeValue), 1);
    infoLayout->addWidget(buildInfoCell(tr("游玩时间"), m_playTimeValue), 1);
    infoLayout->addWidget(buildInfoCell(tr("帧率"), m_fpsValue), 1);

    mainLayout->addWidget(infoContainer);

    // ---- 性能监控区标题 ----
    auto *perfTitle = new OutlinedLabel(tr("性能监控"), this);
    perfTitle->setObjectName("homeSectionTitle");
    mainLayout->addWidget(perfTitle);

    // ---- 性能监控卡片 ----
    auto addPerfCard = [this](const QString &name, const QColor &color, bool available) -> PerfMonitorCard * {
        auto *card = new PerfMonitorCard(name, this);
        card->setChartColor(color);
        card->setAvailable(available);
        return card;
    };

    m_cpuCard = addPerfCard("CPU", QColor("#4CAF50"), true);
    m_memoryCard = addPerfCard(tr("内存"), QColor(ThemeManager::instance()->currentInfoAccentColor()), true);
    m_gpuCard = addPerfCard("GPU", QColor("#9C27B0"), true);

    mainLayout->addWidget(m_cpuCard);
    mainLayout->addWidget(m_memoryCard);
    mainLayout->addWidget(m_gpuCard);

    mainLayout->addStretch(1);
}

void InstanceHomePage::applyThemeStyles()
{
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    QString textColor = ThemeManager::instance()->currentTextColor();
    QString borderColor = ThemeManager::instance()->currentBorderColor();

    // 游玩信息单元格样式
    QString infoStyle = QString(
        "#homeInfoCell {"
        "  background: palette(base);"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "}"
        "#homeInfoTitle {"
        "  color: %2;"
        "  font-size: 11px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#homeInfoValue {"
        "  color: %3;"
        "  background: transparent;"
        "  border: none;"
        "}"
    ).arg(borderColor, textColor, themeColor);

    // 应用到 infoContainer 的所有子 cell
    auto infoContainer = findChild<QWidget *>("homeInfoContainer");
    if (infoContainer)
    {
        auto cells = infoContainer->findChildren<QWidget *>("homeInfoCell");
        for (QWidget *cell : cells)
        {
            cell->setStyleSheet(infoStyle);
        }
    }
}

qint64 InstanceHomePage::detectGameStartTime()
{
#ifdef Q_OS_WIN
    // 优先使用 GameDetector 拿到运行中游戏的 PID
    auto gameInfo = GameDetector::detectRunningGame();
    DWORD pid = 0;
    if (gameInfo.has_value() && gameInfo->pid > 0)
    {
        pid = static_cast<DWORD>(gameInfo->pid);
    }
    else
    {
        pid = findJavaProcessId();
    }
    if (pid == 0)
    {
        return 0;
    }
    return getProcessCreationTimeMs(pid);
#else
    return 0;
#endif
}

#ifdef Q_OS_WIN
HWND InstanceHomePage::findGameWindow()
{
    auto gameInfo = GameDetector::detectRunningGame();
    DWORD pid = 0;
    if (gameInfo.has_value() && gameInfo->pid > 0)
    {
        pid = static_cast<DWORD>(gameInfo->pid);
    }
    else
    {
        pid = findJavaProcessId();
    }
    if (pid == 0)
    {
        return nullptr;
    }
    return findWindowByPid(pid);
}
#endif

int InstanceHomePage::queryGameFps()
{
#ifdef Q_OS_WIN
    // 通过 DWM 合成计时信息估算帧率：
    // 两次采样间 cFrame 的增量 × 1000 / 时间间隔(ms) ≈ 桌面合成器每秒帧数
    // 当游戏窗口是活动更新源时，此值近似游戏帧率。
    DWM_TIMING_INFO timing = {};
    timing.cbSize = sizeof(timing);
    HRESULT hr = DwmGetCompositionTimingInfo(nullptr, &timing);
    if (FAILED(hr))
    {
        return 0;
    }
    quint64 currentFrame = timing.cFrame;
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    int fps = 0;
    if (m_lastDwmQueryMs > 0 && currentFrame >= m_lastDwmFrame)
    {
        qint64 deltaMs = nowMs - m_lastDwmQueryMs;
        if (deltaMs > 0)
        {
            quint64 frameDelta = currentFrame - m_lastDwmFrame;
            fps = static_cast<int>(frameDelta * 1000LL / static_cast<quint64>(deltaMs));
        }
    }

    m_lastDwmFrame = currentFrame;
    m_lastDwmQueryMs = nowMs;
    return fps;
#else
    return 0;
#endif
}

void InstanceHomePage::refreshGameDetection()
{
#ifdef Q_OS_WIN
    // 深度检测：调用 GameDetector 查询运行中游戏的命令行（启动 PowerShell，耗时），
    // 仅在「无游戏 → 有游戏」状态跳变时由 onSecondTick 调用。
    auto gameInfo = GameDetector::detectRunningGame();
    DWORD pid = 0;
    if (gameInfo.has_value() && gameInfo->pid > 0)
    {
        pid = static_cast<DWORD>(gameInfo->pid);
    }
    else
    {
        pid = findJavaProcessId();
    }
    m_cachedGamePid = pid;
    m_gameStartTimeMs = (pid > 0) ? getProcessCreationTimeMs(pid) : 0;
#else
    m_cachedGamePid = 0;
    m_gameStartTimeMs = 0;
#endif
}

void InstanceHomePage::onSecondTick()
{
    // 1. 当前时间
    if (m_currentTimeValue)
    {
        m_currentTimeValue->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
    }

    // 2. 轻量检测：仅枚举进程快照判断游戏是否在运行（约 1ms，不启动 PowerShell）。
    //    GameDetector::detectRunningGame() 会为每个 javaw.exe 启动 PowerShell 查询
    //    命令行（最坏阻塞 5 秒/进程），不能每秒调用；仅在状态跳变时做一次深度检测并缓存。
    const bool running = GameDetector::isJavaGameRunning();

    if (running && !m_gameRunning)
    {
        // 无游戏 → 有游戏：深度检测一次并缓存 PID / 启动时间
        refreshGameDetection();
    }
    else if (!running)
    {
        // 游戏已退出：清空缓存
        m_cachedGamePid = 0;
        m_gameStartTimeMs = 0;
    }
    m_gameRunning = running;

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool gameRunning = (m_gameStartTimeMs > 0);

    // 3. 游玩时间
    if (m_playTimeValue)
    {
        if (gameRunning && nowMs > m_gameStartTimeMs)
        {
            m_playTimeValue->setText(formatDuration(nowMs - m_gameStartTimeMs));
        }
        else
        {
            m_playTimeValue->setText(QStringLiteral("--"));
        }
    }

    // 4. 帧率
    if (m_fpsValue)
    {
        if (gameRunning)
        {
            int fps = queryGameFps();
            m_fpsValue->setText(fps > 0 ? QString::number(fps) : QStringLiteral("--"));
        }
        else
        {
            m_fpsValue->setText(QStringLiteral("--"));
            // 重置 DWM 增量基线，避免下次启动游戏后首秒计算偏大
#ifdef Q_OS_WIN
            m_lastDwmFrame = 0;
            m_lastDwmQueryMs = 0;
#endif
        }
    }
}

void InstanceHomePage::onHardwareDataRefreshed()
{
    if (!m_hardwareMonitor)
    {
        return;
    }
    const HardwareData &data = m_hardwareMonitor->currentData();

    if (m_cpuCard)
    {
        m_cpuCard->setAvailable(data.cpuAvailable);
        m_cpuCard->setPercent(data.cpuUsagePercent);
    }
    if (m_memoryCard)
    {
        m_memoryCard->setAvailable(data.memoryAvailable);
        m_memoryCard->setPercent(data.memoryUsagePercent);
    }
    if (m_gpuCard)
    {
        // 优先使用 GPU0，缺失时标记不可用
        bool gpuOk = data.gpu0Available;
        int gpuPct = gpuOk ? data.gpu0UsagePercent : 0;
        m_gpuCard->setAvailable(gpuOk);
        m_gpuCard->setPercent(gpuPct);
    }
}
