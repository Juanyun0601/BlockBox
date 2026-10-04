/**
 * @file   GameLauncher.cpp
 * @brief  游戏启动器类实现
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "GameLauncher.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QWindow>

#include "utils/ErrorAnalyzer.h"
#include "utils/SettingsManager.h"
#include "utils/MemoryAllocator.h"

GameLauncher* GameLauncher::m_instance = nullptr;
QMutex GameLauncher::m_instanceMutex;

const int GameLauncher::MAX_COMPLETION_CONCURRENT;

GameLauncher* GameLauncher::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new GameLauncher();
        }
    }
    return m_instance;
}

GameLauncher::GameLauncher(QObject *parent)
    : QObject(parent),
      m_gameProcess(nullptr),
      m_status(Idle),
      m_errorAnalyzer(ErrorAnalyzer::instance()),
      m_processOutput(""),
      m_networkManager(new QNetworkAccessManager(this)),
      m_windowCheckTimer(nullptr),
      m_dynamicMaxConcurrent(MAX_COMPLETION_CONCURRENT),
      m_consecutiveErrors(0),
      m_downloadBytesSinceLastMeasure(0),
      m_currentDownloadSpeedBps(0.0),
      m_averageDownloadSpeedBps(0.0),
      m_speedMeasureCount(0),
      m_completionActivePriority(0)
{
    m_downloadSpeedTimer.start();
    m_lastProgressEmit.start();
}

GameLauncher::~GameLauncher()
{
    stopGame();
    // m_gameProcess 以 this 为 parent 创建（new QProcess(this)），由 Qt 父子机制自动释放，
    // 手动 delete 会与父对象销毁时的释放造成双重释放，故不在此删除。
}

GameLauncher::LaunchStatus GameLauncher::status() const
{
    return m_status;
}

QString GameLauncher::errorMessage() const
{
    return m_errorMessage;
}

bool GameLauncher::launchGame(const LaunchConfig& config, bool skipFileCompletion)
{
    if (m_status == Running || m_status == Launching) {
        m_errorMessage = "游戏已经在运行或正在启动中";
        return false;
    }

    m_currentConfig = config;
    // 每次新启动重置窗口出现标记
    m_gameStartedSuccessfully = false;
    // 每次新启动重置用户取消标记
    m_userCancelled = false;

    // 自动内存分配: 如果启用了自动分配，根据系统内存覆盖配置中的内存值
    if (SettingsManager::instance()->isAutoMemoryEnabled())
    {
        MemoryAllocationMode mode = SettingsManager::instance()->getMemoryAllocationMode();
        MemoryAllocation alloc = MemoryAllocator::instance()->calculateRecommendedAllocation(mode);
        m_currentConfig.minMemory = alloc.minMemoryMb;
        m_currentConfig.maxMemory = alloc.maxMemoryMb;
        emit launchDetailAdded(
            QString("自动内存分配 [%1]: %2MB ~ %3MB (系统总内存: %4GB)")
                .arg(MemoryAllocator::modeDisplayName(mode))
                .arg(alloc.minMemoryMb)
                .arg(alloc.maxMemoryMb)
                .arg(alloc.totalSystemMemoryMb / 1024));
    }

    m_status = Launching;
    emit launchStatusChanged(m_status);
    emit launchProgressChanged(0, "准备启动游戏...");
    emit launchDetailAdded("开始启动游戏流程");

    // ── 缓存: 预计算路径和 JSON，避免启动流程中重复 I/O ──
    // 参考 HMCL/ProjBobcat: 一次读取 version JSON，后续步骤复用
    {
        const QString& ip = config.instancePath;
        m_cachedGameDir = QDir(QFileInfo(ip).dir().absolutePath() + "/..").absolutePath();
        m_cachedBasePath = QFileInfo(ip).dir().absolutePath() + "/..";
        QJsonObject rawJson = readVersionJson(ip);
        m_cachedMergedJson = mergeInheritsFromJson(ip, rawJson);
    }

    // Pre-check
    emit launchProgressChanged(5, "执行预检查...");
    if (!preCheck(config)) {
        m_status = Failed;
        emit launchStatusChanged(m_status);
        emit gameCrashed(m_errorMessage);
        return false;
    }
    emit launchProgressChanged(15, "预检查完成");

    // Handle Java path
    emit launchProgressChanged(20, "获取Java...");
    QString javaPath = config.javaPath;
    qDebug() << "[GameLauncher]" << "GameLauncher received Java path:" << javaPath;

    if (javaPath == "auto") {
        emit launchDetailAdded("正在自动检测Java...");

        // ── 从版本 JSON 中读取实际版本号（而非靠路径正则猜测）──
        QString gameVersion = resolveVersionId(config.instancePath);

        // Find best Java version for the game version
        JavaInfo bestJavaInfo = findBestJavaVersion(gameVersion);

        if (bestJavaInfo.valid) {
            javaPath = bestJavaInfo.path;
            emit launchProgressChanged(25, QString("自动选择了Java: %1 (%2)").arg(bestJavaInfo.version).arg(bestJavaInfo.path));
            emit launchDetailAdded(QString("检测到Java版本: %1").arg(bestJavaInfo.version));
            emit launchDetailAdded(QString("Java路径: %1").arg(bestJavaInfo.path));
        } else {
            // Fallback to basic Java detection
            emit launchDetailAdded("未找到合适的Java版本，尝试基本检测...");
            JavaInfo basicJavaInfo = detectJava();
            if (basicJavaInfo.valid) {
                javaPath = basicJavaInfo.path;
                emit launchProgressChanged(25, QString("使用基本检测到的Java: %1 (%2)").arg(basicJavaInfo.version).arg(basicJavaInfo.path));
                emit launchDetailAdded(QString("检测到Java版本: %1").arg(basicJavaInfo.version));
                emit launchDetailAdded(QString("Java路径: %1").arg(basicJavaInfo.path));
            } else {
                m_errorMessage = "无法自动检测到Java，请在设置中手动指定Java路径";
                m_status = Failed;
                emit launchStatusChanged(m_status);
                emit gameCrashed(m_errorMessage);
                emit launchDetailAdded("错误: 未检测到Java");
                return false;
            }
        }
    } else {
        emit launchDetailAdded(QString("使用指定的Java路径: %1").arg(javaPath));
    }

    // Validate Java path
    emit launchDetailAdded("验证Java路径...");
    JavaInfo javaInfo;
    if (!validateJavaPath(javaPath, javaInfo)) {
        m_errorMessage = "无效的Java路径，请检查设置";
        m_status = Failed;
        emit launchStatusChanged(m_status);
        emit gameCrashed(m_errorMessage);
        emit launchDetailAdded("错误: Java路径无效");
        return false;
    }
    emit launchDetailAdded(QString("Java版本验证成功: %1").arg(javaInfo.version));
    emit launchProgressChanged(30, "Java验证完成");

    // ── 根据策略决定是否执行文件完整性检查与补全 ──
    if (skipFileCompletion) {
        // 首次启动: 跳过文件完整性检查与补全，直接进入后续启动步骤
        emit launchDetailAdded("首次启动: 跳过文件完整性检查与补全");
        emit launchProgressChanged(50, "跳过文件补全");
        proceedWithLaunch(config, javaPath);
        return true;
    }

    // 启动失败重试: 异步执行文件完整性检查并补全缺失文件，完成后再继续启动
    emit launchDetailAdded("启动失败重试: 将检查并补全缺失文件后重新启动");
    connect(this, &GameLauncher::fileCompletionFinished, this,
            [this, config, javaPath](bool success) -> void {
        // 断开连接，防止多次触发
        disconnect(this, &GameLauncher::fileCompletionFinished, this, nullptr);

        if (!success) {
            m_errorMessage = "下载缺失文件失败";
            m_status = Failed;
            emit launchStatusChanged(m_status);
            emit gameCrashed(m_errorMessage);
            return;
        }
        emit launchProgressChanged(50, "文件补全完成");
        proceedWithLaunch(config, javaPath);
    });

    // Start async file completion
    emit launchProgressChanged(35, "补全文件...");
    downloadMissingFiles(config.instancePath);
    return true;
}

void GameLauncher::proceedWithLaunch(const LaunchConfig& config, const QString& javaPath)
{
    // Extract natives (内部复用 m_cachedMergedJson)
    emit launchProgressChanged(50, "解压Natives文件...");
    if (!extractNatives(config.instancePath)) {
        m_errorMessage = "解压Natives文件失败";
        m_status = Failed;
        emit launchStatusChanged(m_status);
        emit gameCrashed(m_errorMessage);
        return;
    }
    emit launchProgressChanged(60, "Natives处理完成");

    // Execute custom commands
    emit launchProgressChanged(65, "执行自定义命令...");
    if (!executeCustomCommands()) {
        m_errorMessage = "执行自定义命令失败";
        m_status = Failed;
        emit launchStatusChanged(m_status);
        emit gameCrashed(m_errorMessage);
        return;
    }
    emit launchProgressChanged(70, "自定义命令执行完成");

    emit launchProgressChanged(75, "执行预启动处理...");
    if (!preLaunchProcessing(config)) {
        m_errorMessage = "预启动处理失败";
        m_status = Failed;
        emit launchStatusChanged(m_status);
        emit gameCrashed(m_errorMessage);
        return;
    }
    emit launchProgressChanged(80, "预启动处理完成");

    emit launchDetailAdded("构建启动命令...");
    QStringList args = buildLaunchCommand(config);
    emit launchProgressChanged(85, "构建启动命令...");
    emit launchDetailAdded(QString("内存分配: %1MB - %2MB").arg(config.minMemory).arg(config.maxMemory));

    // Output the actual launch command
    emit launchDetailAdded("启动命令:");
    QString command = javaPath + " " + args.join(" ");
    emit launchDetailAdded(command);

    // Clear previous process output
    m_processOutput.clear();

    // Prepare process
    if (!m_gameProcess) {
        m_gameProcess = new QProcess(this);
        connect(m_gameProcess, &QProcess::started, this, &GameLauncher::onProcessStarted);
        connect(m_gameProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, &GameLauncher::onProcessFinished);
        connect(m_gameProcess, &QProcess::errorOccurred, this, &GameLauncher::onProcessError);
        connect(m_gameProcess, &QProcess::readyReadStandardOutput, this, &GameLauncher::onProcessOutput);
        connect(m_gameProcess, &QProcess::readyReadStandardError, this, &GameLauncher::onProcessOutput);
    }

    // Set working directory to .minecraft (Minecraft 需要工作目录为 .minecraft 根目录)
    m_gameProcess->setWorkingDirectory(m_cachedGameDir);
    emit launchDetailAdded(QString("工作目录: %1").arg(m_cachedGameDir));

    // ── 设置环境变量（HMCL/PCL2 兼容，复用外层 gameDir）──
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString versionName = resolveJarName(config.instancePath, m_cachedMergedJson);

#ifndef Q_OS_ANDROID
        env.insert("APPDATA",       QDir::toNativeSeparators(QFileInfo(m_cachedGameDir).absolutePath()));
#endif
        env.insert("INST_NAME",     versionName);
        env.insert("INST_ID",       resolveVersionId(config.instancePath));
        env.insert("INST_DIR",      QDir::toNativeSeparators(config.instancePath));
        env.insert("INST_MC_DIR",   QDir::toNativeSeparators(m_cachedGameDir));
        env.insert("INST_JAVA",     QDir::toNativeSeparators(javaPath));

        m_gameProcess->setProcessEnvironment(env);
    }

    // Start process
    emit launchProgressChanged(85, "启动游戏进程...");
    emit launchDetailAdded("启动游戏进程...");
    m_gameProcess->start(javaPath, args);

    emit launchDetailAdded("游戏启动命令已发送，正在启动...");
}

bool GameLauncher::wasGameStartedSuccessfully() const
{
    return m_gameStartedSuccessfully;
}

bool GameLauncher::wasUserCancelled() const
{
    return m_userCancelled;
}

QString GameLauncher::currentInstancePath() const
{
    return m_currentConfig.instancePath;
}

void GameLauncher::stopGame()
{
    if (m_gameProcess && m_gameProcess->state() == QProcess::Running) {
        // 标记本次进程结束由用户手动取消触发，供 onProcessFinished/onProcessError
        // 以及 UI 层区分"用户取消"与"游戏自然退出/崩溃"，避免误报错误信息或触发自动重试
        m_userCancelled = true;
        m_gameProcess->kill();
        m_gameProcess->waitForFinished(3000);
        cleanup();
        m_status = Stopped;
        emit launchStatusChanged(m_status);
        emit gameStopped(0);
    }
}

bool GameLauncher::isGameRunning()
{
    return m_gameProcess && m_gameProcess->state() == QProcess::Running;
}

void GameLauncher::cleanup()
{
    if (m_gameProcess) {
        m_gameProcess->close();
    }
}

void GameLauncher::logError(const QString& errorType, const QString& errorMessage, const QString& details)
{
    QString logMessage = QString("[ERROR] [%1] %2").arg(errorType).arg(errorMessage);
    if (!details.isEmpty()) {
        logMessage += " - " + details;
    }
    
    qDebug() << "[GameLauncher]" << logMessage;
    emit launchDetailAdded(logMessage);
}

void GameLauncher::logInfo(const QString& message)
{
    qDebug() << "[GameLauncher]" << "[INFO]" << message;
    emit launchDetailAdded(QString("[INFO] %1").arg(message));
}

void GameLauncher::analyzeError(const QString& errorOutput)
{
    if (!errorOutput.isEmpty() && m_errorAnalyzer) {
        emit launchDetailAdded("开始分析错误...");
        ErrorAnalyzer::ErrorInfo errorInfo = m_errorAnalyzer->analyzeError(errorOutput);
        QString report = m_errorAnalyzer->generateErrorReport(errorInfo);
        emit errorReportGenerated(report);
        emit launchDetailAdded("错误分析完成");
        
        // 记录错误分析结果
        logError("Analysis", "错误分析完成", report.left(200) + (report.length() > 200 ? "..." : ""));
    }
}

void GameLauncher::waitForGameWindow()
{
    emit launchDetailAdded("等待游戏窗口出现...");
    
    const int checkInterval = 100;
    
    if (!m_windowCheckTimer) {
        m_windowCheckTimer = new QTimer(this);
    }
    
    m_windowCheckTimer->setInterval(checkInterval);
    
    disconnect(m_windowCheckTimer, &QTimer::timeout, this, nullptr);
    
    connect(m_windowCheckTimer, &QTimer::timeout, this, [this, checkInterval]() {
        static int elapsedTime = 0;
        elapsedTime += checkInterval;
        
        if (elapsedTime >= 30000) {
            m_windowCheckTimer->stop();
            emit launchDetailAdded("等待游戏窗口超时");
            elapsedTime = 0;
            return;
        }
        
        if (!isGameRunning()) {
            m_windowCheckTimer->stop();
            emit launchDetailAdded("游戏进程已结束");
            elapsedTime = 0;
            return;
        }
        
        bool foundGameWindow = false;
        for (QWindow* window : QApplication::allWindows()) {
            if (window->title().contains("Minecraft", Qt::CaseInsensitive)) {
                foundGameWindow = true;
                break;
            }
        }
        
        if (foundGameWindow) {
            m_windowCheckTimer->stop();
            // 标记游戏窗口已成功出现，用于区分启动期崩溃与运行期崩溃
            m_gameStartedSuccessfully = true;
            emit launchDetailAdded("游戏窗口已出现");
            emit launchProgressChanged(100, "游戏启动成功！");
            emit gameStarted();
            elapsedTime = 0;
            return;
        }
        
        int progress = (elapsedTime * 100) / 30000;
        emit launchProgressChanged(95 + (progress / 20), QString("等待游戏窗口出现 (%1%)").arg(progress));
    });
    
    m_windowCheckTimer->start();
}

void GameLauncher::onProcessStarted()
{
    m_status = Running;
    emit launchStatusChanged(m_status);
    emit launchProgressChanged(95, "游戏进程已启动，等待窗口出现...");
    
    waitForGameWindow();
}

void GameLauncher::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // 用户手动取消启动时不视为崩溃，不发射 gameCrashed 信号、不进行错误分析，
    // 避免误报错误信息或触发自动重试。stopGame() 会随后统一发射 gameStopped(0)。
    if (m_userCancelled) {
        m_status = Stopped;
        emit launchStatusChanged(m_status);
        cleanup();
        return;
    }

    if (exitStatus == QProcess::CrashExit) {
        m_status = Failed;
        m_errorMessage = "游戏崩溃";
        emit gameCrashed(m_errorMessage);
        analyzeError(m_processOutput);
    } else {
        m_status = Stopped;
        emit gameStopped(exitCode);
        if (exitCode != 0) {
            analyzeError(m_processOutput);
        }
    }

    emit launchStatusChanged(m_status);
    cleanup();
}

void GameLauncher::onProcessError(QProcess::ProcessError error)
{
    // 用户手动取消启动时不视为错误，不发射 gameCrashed 信号、不进行错误分析
    if (m_userCancelled) {
        m_status = Stopped;
        emit launchStatusChanged(m_status);
        cleanup();
        return;
    }

    switch (error) {
    case QProcess::FailedToStart:
        m_errorMessage = "无法启动游戏进程，请检查Java路径";
        break;
    case QProcess::Crashed:
        m_errorMessage = "游戏进程崩溃";
        break;
    case QProcess::Timedout:
        m_errorMessage = "游戏进程超时";
        break;
    default:
        m_errorMessage = "游戏进程发生错误";
        break;
    }

    m_status = Failed;
    emit launchStatusChanged(m_status);
    emit gameCrashed(m_errorMessage);
    analyzeError(m_processOutput);
    cleanup();
}

void GameLauncher::onProcessOutput()
{
    // Handle process output if needed
    if (m_gameProcess) {
        QByteArray output = m_gameProcess->readAllStandardOutput();
        QByteArray error = m_gameProcess->readAllStandardError();

        // 游戏日志自带的时间戳（如 [20:48:45]），启动器在显示时会统一添加自己的时间戳，
        // 这里删除游戏日志行首的时间戳，避免时间戳重复显示。
        static const QRegularExpression gameTimestampRE("^\\[\\d{1,2}:\\d{2}:\\d{2}\\]\\s*");

        auto emitLines = [this](const QByteArray &data) {
            QString dataStr = QString::fromLocal8Bit(data);
            m_processOutput += dataStr;
            QStringList lines = dataStr.split('\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                QString trimmed = line.trimmed();
                trimmed.remove(gameTimestampRE);
                emit launchDetailAdded(trimmed);
            }
        };

        if (!output.isEmpty()) {
            emitLines(output);
        }
        if (!error.isEmpty()) {
            emitLines(error);
        }
    }
}
