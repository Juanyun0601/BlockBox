/**
 * @file   ErrorAnalyzer.h
 * @brief  错误分析器类定义
 * @author BlockBox Team
 * @date   2026-05-09
 */

#pragma once

#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>

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
        OtherError
    };

    struct ErrorInfo {
        QString errorMessage;
        ErrorType errorType;
        QString errorCode;
        QString solution;
        QString details;
    };

    static ErrorAnalyzer* instance();
    ~ErrorAnalyzer();

    // Analyze error
    ErrorInfo analyzeError(const QString &errorMessage, const QString &additionalInfo = "");
    
    // Get error type name
    QString getErrorTypeName(ErrorType type) const;
    
    // Generate error report
    QString generateErrorReport(const ErrorInfo &errorInfo) const;
    
    // Add custom error pattern
    void addErrorPattern(ErrorType type, const QString &pattern, const QString &solution);

private:
    ErrorAnalyzer(QObject *parent = nullptr);
    static ErrorAnalyzer* m_instance;
    static QMutex m_instanceMutex;

    // Error patterns
    QMap<ErrorType, QList<QPair<QString, QString>>> m_errorPatterns;
    
    // Initialize error patterns
    void initErrorPatterns();
    
    // Match error pattern
    bool matchPattern(const QString &errorMessage, const QString &pattern) const;
};
