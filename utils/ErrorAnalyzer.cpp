/**
 * @file   ErrorAnalyzer.cpp
 * @brief  错误分析器类实现
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "ErrorAnalyzer.h"

#include <QRegularExpression>

ErrorAnalyzer* ErrorAnalyzer::m_instance = nullptr;
QMutex ErrorAnalyzer::m_instanceMutex;

ErrorAnalyzer* ErrorAnalyzer::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new ErrorAnalyzer();
        }
    }
    return m_instance;
}

ErrorAnalyzer::ErrorAnalyzer(QObject *parent) : QObject(parent)
{
    initErrorPatterns();
}

ErrorAnalyzer::~ErrorAnalyzer()
{
}

ErrorAnalyzer::ErrorInfo ErrorAnalyzer::analyzeError(const QString &errorMessage, const QString &additionalInfo)
{
    ErrorInfo errorInfo;
    errorInfo.errorMessage = errorMessage;
    errorInfo.errorType = OtherError;
    errorInfo.errorCode = "UNKNOWN";
    errorInfo.solution = tr("请查看详细错误信息，或尝试重新启动游戏。");
    errorInfo.details = additionalInfo;

    // Try to match error patterns
    for (auto it = m_errorPatterns.begin(); it != m_errorPatterns.end(); ++it) {
        ErrorType type = it.key();
        const QList<QPair<QString, QString>> &patterns = it.value();

        for (const auto &patternPair : patterns) {
            const QString &pattern = patternPair.first;
            const QString &solution = patternPair.second;

            if (matchPattern(errorMessage, pattern)) {
                errorInfo.errorType = type;
                errorInfo.solution = solution;
                break;
            }
        }

        if (errorInfo.errorType != OtherError) {
            break;
        }
    }

    // Set error code based on error type
    switch (errorInfo.errorType) {
    case JavaError:
        errorInfo.errorCode = "JAVA_ERROR";
        break;
    case AccountError:
        errorInfo.errorCode = "ACCOUNT_ERROR";
        break;
    case InstanceError:
        errorInfo.errorCode = "INSTANCE_ERROR";
        break;
    case LaunchError:
        errorInfo.errorCode = "LAUNCH_ERROR";
        break;
    case MemoryError:
        errorInfo.errorCode = "MEMORY_ERROR";
        break;
    case NetworkError:
        errorInfo.errorCode = "NETWORK_ERROR";
        break;
    default:
        errorInfo.errorCode = "OTHER_ERROR";
        break;
    }

    return errorInfo;
}

QString ErrorAnalyzer::getErrorTypeName(ErrorType type) const
{
    switch (type) {
    case JavaError:
        return tr("Java错误");
    case AccountError:
        return tr("账户错误");
    case InstanceError:
        return tr("实例错误");
    case LaunchError:
        return tr("启动错误");
    case MemoryError:
        return tr("内存错误");
    case NetworkError:
        return tr("网络错误");
    default:
        return tr("其他错误");
    }
}

QString ErrorAnalyzer::generateErrorReport(const ErrorInfo &errorInfo) const
{
    QString report;
    report = tr("错误分析报告:\n\n");
    report += tr("错误类型: %1\n").arg(getErrorTypeName(errorInfo.errorType));
    report += tr("错误代码: %1\n").arg(errorInfo.errorCode);
    report += tr("错误信息: %1\n").arg(errorInfo.errorMessage);
    if (!errorInfo.details.isEmpty()) {
        report += tr("详细信息: %1\n").arg(errorInfo.details);
    }
    report += tr("解决方案: %1\n").arg(errorInfo.solution);
    return report;
}

void ErrorAnalyzer::addErrorPattern(ErrorType type, const QString &pattern, const QString &solution)
{
    m_errorPatterns[type].append(qMakePair(pattern, solution));
}

void ErrorAnalyzer::initErrorPatterns()
{
    // Java errors
    m_errorPatterns[JavaError].append(qMakePair("Could not find Java|java\\.exe not found|java not found|Unable to locate Java", tr("请在设置中正确配置 Java 路径，确保 Java 已正确安装。")));
    m_errorPatterns[JavaError].append(qMakePair("Unsupported Java version|Invalid Java version|Java version mismatch", tr("请安装与游戏版本兼容的 Java 版本，通常 Minecraft 1.17+ 需要 Java 16+。")));

    // Account errors
    m_errorPatterns[AccountError].append(qMakePair("Login failed|Authentication failed|Invalid session", tr("请检查您的账户信息是否正确，网络连接是否正常。")));
    m_errorPatterns[AccountError].append(qMakePair("Account not found|User not found", tr("请确保您已正确登录账户，并且账户信息完整。")));

    // Instance errors
    m_errorPatterns[InstanceError].append(qMakePair("Instance not found|World not found", tr("请确保您选择的游戏实例存在，并且路径正确。")));
    m_errorPatterns[InstanceError].append(qMakePair("Corrupted world|World corrupted", tr("您的游戏世界可能已损坏，尝试使用备份或创建新的游戏实例。")));

    // Launch errors
    m_errorPatterns[LaunchError].append(qMakePair("Failed to launch|Could not start game|Launcher error", tr("尝试重启启动器，或检查启动器文件是否完整。")));
    m_errorPatterns[LaunchError].append(qMakePair("Could not initialize game|Failed to initialize game", tr("检查游戏文件是否完整，或尝试重新安装游戏。")));

    // Memory errors
    m_errorPatterns[MemoryError].append(qMakePair("Out of memory|Java heap space|GC overhead limit exceeded", tr("尝试增加分配给游戏的内存，或关闭其他占用内存的程序。")));
    m_errorPatterns[MemoryError].append(qMakePair("Memory allocation failed|Could not allocate memory", tr("检查您的系统内存是否充足，或尝试减少分配给游戏的内存。")));

    // Network errors
    m_errorPatterns[NetworkError].append(qMakePair("Network error|Connection timed out|Failed to connect", tr("检查您的网络连接是否正常，或尝试使用 VPN。")));
    m_errorPatterns[NetworkError].append(qMakePair("Could not reach server|Server not found", tr("检查服务器地址是否正确，或服务器是否在线。")));
}

bool ErrorAnalyzer::matchPattern(const QString &errorMessage, const QString &pattern) const
{
    // 使用静态缓存避免重复编译正则表达式
    static QHash<QString, QRegularExpression> regexCache;
    auto it = regexCache.find(pattern);
    if (it == regexCache.end()) {
        it = regexCache.insert(pattern, QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption));
    }
    return it.value().match(errorMessage).hasMatch();
}
