/**
 * @file   ErrorAnalyzer.h
 * @brief  错误分析器类定义（离线崩溃规则库引擎）
 * @author BlockBox Team
 * @date   2026-05-09
 */

#pragma once

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

class ErrorAnalyzer : public QObject
{
    Q_OBJECT

public:
    enum ErrorType {
        JavaError,
        AccountError,
        InstanceError,
        LaunchError,
        MemoryError,
        NetworkError,
        ModError,
        GraphicsError,
        OtherError
    };

    struct ErrorInfo {
        QString errorMessage;
        ErrorType errorType = OtherError;
        QString errorCode;
        QString solution;
        QString details;

        // 离线崩溃规则库的命中结果（未命中任何规则时 ruleId/ruleName 为空）
        QString ruleId;
        QString ruleName;
        double confidence = 0.0;        // 0~1，低于 0.6 的在报告中按“可能”措辞
        QString cause;                  // 可能原因
        QString verify;                 // 可以怎么验证
        QStringList evidence;           // 命中的日志原文行
        QStringList secondaryRuleNames; // 次要命中的规则名
    };

    // 规则库中的一条规则（与 resources/crash_rules.json 的字段一一对应）
    struct CrashRule {
        QString id;
        QString name;
        QString category;
        QStringList patterns;     // 任意一条正则命中即算命中；为空时仅按 exitCodes 命中
        QStringList antiPatterns; // 任意一条命中则放弃本规则
        QList<int> exitCodes;     // 非空时仅进程退出码在列表内才允许命中
        QString cause;
        QString verify;
        QString solution;
        double confidence = 0.6;
        int priority = 50;
    };

    static ErrorAnalyzer* instance();
    ~ErrorAnalyzer();

    // 兼容旧接口：对单条错误消息做分析（退出码未知）
    ErrorInfo analyzeError(const QString &errorMessage, const QString &additionalInfo = "");

    // 离线规则库入口：对整段游戏日志/错误输出做规则匹配；exitCode 传进程退出码，未知传 -1
    // 退出码为 0 的正常退出只允许命中显式声明包含退出码 0 的规则，避免正常游玩被误报
    ErrorInfo analyzeLog(const QString &logText, int exitCode = -1);

    // Get error type name
    QString getErrorTypeName(ErrorType type) const;

    // 规则库 category 字段映射到 ErrorType
    ErrorType getErrorTypeForCategory(const QString &category) const;

    // Generate error report
    QString generateErrorReport(const ErrorInfo &errorInfo) const;

    // 重新加载规则库：先内置 resources/crash_rules.json，再用数据目录
    // rules/crash_rules.json 覆盖同名 id / 追加新规则（为后续热更新预留的入口）
    void reloadRules();

private:
    ErrorAnalyzer(QObject *parent = nullptr);
    static ErrorAnalyzer* m_instance;
    static QMutex m_instanceMutex;

    QList<CrashRule> m_rules;
    QMutex m_rulesMutex;

    // loadRulesFromPathLocked 假定调用方已持有 m_rulesMutex
    void loadRulesLocked();
    bool loadRulesFromPathLocked(const QString &path, bool overrideById);

    // Match error pattern（大小写不敏感，内部缓存已编译正则）
    bool matchPattern(const QString &text, const QString &pattern) const;

    // 从日志中提取命中规则的原文行作为证据
    QStringList extractEvidence(const QString &logText, const CrashRule &rule) const;

    // 按 errorType 填充旧字段 errorCode
    void fillLegacyFields(ErrorInfo &errorInfo) const;
};
