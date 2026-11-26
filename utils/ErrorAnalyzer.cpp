/**
 * @file   ErrorAnalyzer.cpp
 * @brief  错误分析器类实现（离线崩溃规则库引擎）
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "ErrorAnalyzer.h"

#include "platform.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMutexLocker>
#include <QRegularExpression>

#include <algorithm>

namespace {
// 匹配前只取日志尾部：崩溃信息一般在末尾，也避免超大日志拖慢正则扫描
constexpr int kMaxAnalyzeChars = 256 * 1024;
// 报告里最多引用几条命中的日志原文行
constexpr int kMaxEvidenceLines = 2;
constexpr int kMaxEvidenceLineChars = 300;
constexpr int kMaxSecondaryRules = 2;
// 置信度低于该值的规则在报告措辞中用“可能”
constexpr double kPossibleConfidence = 0.6;
} // namespace

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
    loadRulesLocked();
}

ErrorAnalyzer::~ErrorAnalyzer()
{
}

void ErrorAnalyzer::reloadRules()
{
    QMutexLocker locker(&m_rulesMutex);
    loadRulesLocked();
}

// 内置规则随应用打包离线可用；数据目录下的外部规则文件存在时按 id 覆盖/追加，
// 损坏或不存在则忽略——这两个路径也是后续“网络热更新”要接的位置。
void ErrorAnalyzer::loadRulesLocked()
{
    m_rules.clear();

    if (!loadRulesFromPathLocked(QStringLiteral(":/resources/crash_rules.json"), false)) {
        qWarning() << "[ErrorAnalyzer] 内置崩溃规则库加载失败";
    }

    loadRulesFromPathLocked(
        Platform::getDataDirectory() + QStringLiteral("/rules/crash_rules.json"), true);
}

bool ErrorAnalyzer::loadRulesFromPathLocked(const QString &path, bool overrideById)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[ErrorAnalyzer] 崩溃规则文件解析失败:" << path << parseError.errorString();
        return false;
    }

    const QJsonArray ruleArray = doc.object().value(QStringLiteral("rules")).toArray();
    int loaded = 0;
    for (const QJsonValue &value : ruleArray) {
        const QJsonObject obj = value.toObject();

        CrashRule rule;
        rule.id = obj.value(QStringLiteral("id")).toString().trimmed();
        rule.name = obj.value(QStringLiteral("name")).toString().trimmed();
        rule.category = obj.value(QStringLiteral("category")).toString().trimmed().toLower();

        const QJsonArray patternArray = obj.value(QStringLiteral("patterns")).toArray();
        for (const QJsonValue &p : patternArray) {
            const QString pattern = p.toString().trimmed();
            if (!pattern.isEmpty())
                rule.patterns.append(pattern);
        }
        const QJsonArray antiPatternArray = obj.value(QStringLiteral("antiPatterns")).toArray();
        for (const QJsonValue &p : antiPatternArray) {
            const QString pattern = p.toString().trimmed();
            if (!pattern.isEmpty())
                rule.antiPatterns.append(pattern);
        }
        const QJsonArray exitCodeArray = obj.value(QStringLiteral("exitCodes")).toArray();
        for (const QJsonValue &c : exitCodeArray) {
            if (c.isDouble())
                rule.exitCodes.append(static_cast<int>(c.toDouble()));
        }

        rule.cause = obj.value(QStringLiteral("cause")).toString().trimmed();
        rule.verify = obj.value(QStringLiteral("verify")).toString().trimmed();
        rule.solution = obj.value(QStringLiteral("solution")).toString().trimmed();
        rule.confidence = obj.value(QStringLiteral("confidence")).toDouble(0.6);
        if (rule.confidence < 0.0)
            rule.confidence = 0.0;
        if (rule.confidence > 1.0)
            rule.confidence = 1.0;
        rule.priority = obj.value(QStringLiteral("priority")).toInt(50);

        // 不完整的规则直接丢弃，避免产生无法解释的命中
        if (rule.id.isEmpty() || rule.name.isEmpty() || rule.patterns.isEmpty())
            continue;

        bool replaced = false;
        if (overrideById) {
            for (int i = 0; i < m_rules.size(); ++i) {
                if (m_rules[i].id == rule.id) {
                    m_rules[i] = rule;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced)
            m_rules.append(rule);
        ++loaded;
    }

    return loaded > 0;
}

ErrorAnalyzer::ErrorInfo ErrorAnalyzer::analyzeError(const QString &errorMessage, const QString &additionalInfo)
{
    ErrorInfo info = analyzeLog(errorMessage, -1);
    if (!additionalInfo.isEmpty())
        info.details = additionalInfo;
    return info;
}

ErrorAnalyzer::ErrorInfo ErrorAnalyzer::analyzeLog(const QString &logTextIn, int exitCode)
{
    // 只分析日志尾部，避免超大日志拖慢匹配
    QString logText = logTextIn;
    if (logText.size() > kMaxAnalyzeChars)
        logText = logText.right(kMaxAnalyzeChars);

    ErrorInfo info;
    info.errorMessage = logText.left(kMaxEvidenceLineChars);
    info.errorType = OtherError;
    info.errorCode = QStringLiteral("UNKNOWN");
    info.solution = tr("请查看详细错误信息，或尝试重新启动游戏。");

    // 逐条规则匹配，收集所有命中的规则
    QList<CrashRule> hits;
    QList<CrashRule> rules;
    {
        QMutexLocker locker(&m_rulesMutex);
        rules = m_rules; // 隐式共享拷贝，正则匹配不再持锁
    }
    for (const CrashRule &rule : rules) {
        // 声明了 exitCodes 的规则仅对匹配的退出码生效
        if (!rule.exitCodes.isEmpty() && !rule.exitCodes.contains(exitCode))
            continue;
        // 退出码为 0（正常退出）时，只允许命中显式声明“退出码 0 也算问题”的规则，
        // 防止正常游玩时被通用规则误报
        if (exitCode == 0 && !rule.exitCodes.contains(0))
            continue;

        bool matched = false;
        if (rule.patterns.isEmpty()) {
            // 纯退出码规则：只按退出码命中
            matched = !rule.exitCodes.isEmpty();
        } else {
            for (const QString &pattern : rule.patterns) {
                if (matchPattern(logText, pattern)) {
                    matched = true;
                    break;
                }
            }
        }
        if (!matched)
            continue;

        bool excluded = false;
        for (const QString &anti : rule.antiPatterns) {
            if (matchPattern(logText, anti)) {
                excluded = true;
                break;
            }
        }
        if (excluded)
            continue;

        hits.append(rule);
    }

    if (hits.isEmpty()) {
        // 未命中规则：给出日志末尾几行作为参考，方便用户或 AI 继续排查
        const QStringList lines = logText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QStringList tail;
        for (int i = qMax(0, lines.size() - 3); i < lines.size(); ++i) {
            QString line = lines[i].trimmed().left(kMaxEvidenceLineChars);
            if (!line.isEmpty())
                tail.append(line);
        }
        if (!tail.isEmpty())
            info.errorMessage = tail.join(QLatin1Char('\n'));
        fillLegacyFields(info);
        return info;
    }

    // 按 priority 降序取最优命中（同优先级保持规则库里的顺序）
    std::stable_sort(hits.begin(), hits.end(),
                     [](const CrashRule &a, const CrashRule &b) { return a.priority > b.priority; });
    const CrashRule &best = hits.first();

    info.ruleId = best.id;
    info.ruleName = best.name;
    info.confidence = best.confidence;
    info.cause = best.cause;
    info.verify = best.verify;
    info.solution = best.solution;
    info.evidence = extractEvidence(logText, best);
    if (!info.evidence.isEmpty())
        info.errorMessage = info.evidence.join(QLatin1Char('\n'));
    for (int i = 1; i < hits.size() && info.secondaryRuleNames.size() < kMaxSecondaryRules; ++i)
        info.secondaryRuleNames.append(hits[i].name);

    ErrorType type = getErrorTypeForCategory(best.category);
    info.errorType = type;
    fillLegacyFields(info);
    return info;
}

ErrorAnalyzer::ErrorType ErrorAnalyzer::getErrorTypeForCategory(const QString &category) const
{
    if (category == QLatin1String("java"))
        return JavaError;
    if (category == QLatin1String("account"))
        return AccountError;
    if (category == QLatin1String("instance"))
        return InstanceError;
    if (category == QLatin1String("launch"))
        return LaunchError;
    if (category == QLatin1String("memory"))
        return MemoryError;
    if (category == QLatin1String("network"))
        return NetworkError;
    if (category == QLatin1String("mod"))
        return ModError;
    if (category == QLatin1String("graphics"))
        return GraphicsError;
    return OtherError;
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
    case ModError:
        return tr("模组错误");
    case GraphicsError:
        return tr("显卡/渲染错误");
    default:
        return tr("其他错误");
    }
}

QString ErrorAnalyzer::generateErrorReport(const ErrorInfo &errorInfo) const
{
    QString report;
    report = tr("错误分析报告:\n\n");
    report += tr("错误类型: %1\n").arg(getErrorTypeName(errorInfo.errorType));

    if (!errorInfo.ruleName.isEmpty()) {
        if (errorInfo.confidence < kPossibleConfidence) {
            report += tr("可能命中规则: %1（把握不大，仅供参考）\n").arg(errorInfo.ruleName);
        } else {
            const QString certainty =
                errorInfo.confidence >= 0.8 ? tr("高") : tr("中");
            report += tr("命中规则: %1（可信度: %2）\n").arg(errorInfo.ruleName, certainty);
        }
    }
    if (!errorInfo.cause.isEmpty())
        report += tr("可能原因: %1\n").arg(errorInfo.cause);
    if (!errorInfo.verify.isEmpty())
        report += tr("验证方法: %1\n").arg(errorInfo.verify);
    if (!errorInfo.solution.isEmpty())
        report += tr("解决方案: %1\n").arg(errorInfo.solution);
    if (!errorInfo.secondaryRuleNames.isEmpty())
        report += tr("其他可能: %1\n").arg(errorInfo.secondaryRuleNames.join(QStringLiteral("、")));
    if (!errorInfo.evidence.isEmpty())
        report += tr("日志证据: %1\n").arg(errorInfo.evidence.join(QStringLiteral("\n           ")));
    if (!errorInfo.details.isEmpty())
        report += tr("详细信息: %1\n").arg(errorInfo.details);
    return report;
}

void ErrorAnalyzer::fillLegacyFields(ErrorInfo &errorInfo) const
{
    switch (errorInfo.errorType) {
    case JavaError:
        errorInfo.errorCode = QStringLiteral("JAVA_ERROR");
        break;
    case AccountError:
        errorInfo.errorCode = QStringLiteral("ACCOUNT_ERROR");
        break;
    case InstanceError:
        errorInfo.errorCode = QStringLiteral("INSTANCE_ERROR");
        break;
    case LaunchError:
        errorInfo.errorCode = QStringLiteral("LAUNCH_ERROR");
        break;
    case MemoryError:
        errorInfo.errorCode = QStringLiteral("MEMORY_ERROR");
        break;
    case NetworkError:
        errorInfo.errorCode = QStringLiteral("NETWORK_ERROR");
        break;
    case ModError:
        errorInfo.errorCode = QStringLiteral("MOD_ERROR");
        break;
    case GraphicsError:
        errorInfo.errorCode = QStringLiteral("GRAPHICS_ERROR");
        break;
    default:
        errorInfo.errorCode = QStringLiteral("OTHER_ERROR");
        break;
    }
}

QStringList ErrorAnalyzer::extractEvidence(const QString &logText, const CrashRule &rule) const
{
    QStringList evidence;
    const QStringList lines = logText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &pattern : rule.patterns) {
        for (const QString &line : lines) {
            if (matchPattern(line, pattern)) {
                QString trimmed = line.trimmed().left(kMaxEvidenceLineChars);
                if (!trimmed.isEmpty())
                    evidence.append(trimmed);
                if (evidence.size() >= kMaxEvidenceLines)
                    return evidence;
            }
        }
    }
    return evidence;
}

bool ErrorAnalyzer::matchPattern(const QString &text, const QString &pattern) const
{
    // 使用静态缓存避免重复编译正则表达式（可能被多线程调用，加锁保护缓存）
    static QMutex s_regexMutex;
    static QHash<QString, QRegularExpression> regexCache;
    QMutexLocker locker(&s_regexMutex);
    auto it = regexCache.find(pattern);
    if (it == regexCache.end()) {
        QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
        if (!regex.isValid()) {
            qWarning() << "[ErrorAnalyzer] 无效的规则正则:" << pattern << regex.errorString();
            regexCache.insert(pattern, QRegularExpression());
            return false;
        }
        it = regexCache.insert(pattern, regex);
    }
    const QRegularExpression &regex = it.value();
    if (!regex.isValid())
        return false;
    return regex.match(text).hasMatch();
}
