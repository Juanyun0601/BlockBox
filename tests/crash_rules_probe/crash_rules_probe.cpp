/**
 * @file   crash_rules_probe.cpp
 * @brief  离线崩溃规则库探针：验证内置规则库加载、典型崩溃样本命中、
 *         正常退出不误报、外部覆盖文件生效与报告文本生成。
 */

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include "platform.h"
#include "utils/ErrorAnalyzer.h"

static int g_failed = 0;
static int g_passed = 0;

static void check(bool ok, const QString &name)
{
    if (ok) {
        ++g_passed;
        qDebug() << "  PASS -" << name;
    } else {
        ++g_failed;
        qDebug() << "  FAIL -" << name;
    }
}

// 典型崩溃样本
struct Sample {
    const char *name;
    QString text;
    int exitCode;
    QString expectId;   // 期望命中的规则 id；空 = 期望不命中
    QString expectCode; // 期望的错误代码
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    auto *analyzer = ErrorAnalyzer::instance();

    // ===== 1. 规则库 JSON 基础校验 =====
    qDebug() << "[1] 规则库加载与 JSON 校验";

    QFile jsonFile(QStringLiteral(":/resources/crash_rules.json"));
    check(jsonFile.open(QIODevice::ReadOnly | QIODevice::Text), "qrc 内规则文件可读");
    const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
    check(doc.isObject(), "规则 JSON 解析成功");
    const QJsonArray ruleArray = doc.object().value(QStringLiteral("rules")).toArray();
    check(ruleArray.size() >= 30, QStringLiteral("规则数量足够（%1 条）").arg(ruleArray.size()));

    QSet<QString> ids;
    int invalidRegex = 0;
    int missingFields = 0;
    for (const QJsonValue &v : ruleArray) {
        const QJsonObject obj = v.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString();
        if (ids.contains(id))
            ++missingFields;
        ids.insert(id);
        if (obj.value(QStringLiteral("name")).toString().isEmpty()
            || obj.value(QStringLiteral("cause")).toString().isEmpty()
            || obj.value(QStringLiteral("solution")).toString().isEmpty()
            || obj.value(QStringLiteral("verify")).toString().isEmpty())
            ++missingFields;
        const QJsonArray patterns = obj.value(QStringLiteral("patterns")).toArray();
        const QJsonArray anti = obj.value(QStringLiteral("antiPatterns")).toArray();
        if (patterns.isEmpty() && obj.value(QStringLiteral("exitCodes")).toArray().isEmpty())
            ++missingFields;
        for (const QJsonValue &p : patterns) {
            QRegularExpression re(p.toString(), QRegularExpression::CaseInsensitiveOption);
            if (!re.isValid())
                ++invalidRegex;
        }
        for (const QJsonValue &p : anti) {
            QRegularExpression re(p.toString(), QRegularExpression::CaseInsensitiveOption);
            if (!re.isValid())
                ++invalidRegex;
        }
    }
    check(missingFields == 0, QStringLiteral("规则字段完整且 id 无重复（异常 %1 个）").arg(missingFields));
    check(invalidRegex == 0, QStringLiteral("所有正则可编译（非法 %1 个）").arg(invalidRegex));

    // ===== 2. 典型崩溃样本命中 =====
    qDebug() << "[2] 典型崩溃样本命中";

    QList<Sample> samples;
    samples.append({ "内存不足",
                     "java.lang.OutOfMemoryError: Java heap space\n\tat java.base/java.util.HashMap.resize(HashMap.java)\n[22:11:03] [main/INFO]: Stopping!",
                     1, QStringLiteral("memory_heap_space"), QStringLiteral("MEMORY_ERROR") });
    samples.append({ "缺少前置模组",
                     "Missing or unsupported mandatory dependencies:\n- Mod 'ironchest' requires 'fabric-api' version 0.100.0\n\tat net.fabricmc.loader.impl.FormattedException",
                     1, QStringLiteral("mod_missing_dependency"), QStringLiteral("MOD_ERROR") });
    samples.append({ "模组重复安装",
                     "[main/ERROR] [fm]: Mod file ironchest-1.0.jar is already present\nCaused by: java.lang.IllegalStateException: duplicate",
                     1, QStringLiteral("mod_duplicate"), QStringLiteral("MOD_ERROR") });
    samples.append({ "模组注入失败",
                     "org.spongepowered.asm.mixin.transformer.throwables.MixinTargetNotFound: Mixin apply failed for mixinmod:net.minecraft.client.Minecraft",
                     1, QStringLiteral("mod_mixin_failed"), QStringLiteral("MOD_ERROR") });
    samples.append({ "显卡驱动崩溃",
                     "A fatal error has been detected by the Java Runtime Environment.\nEXCEPTION_ACCESS_VIOLATION (0xc0000005) at pc=0x00007ffabc123456\n# C [nvoglv64.dll+0x1a2b3c]",
                     -1073741819, QStringLiteral("gfx_access_violation"), QStringLiteral("GRAPHICS_ERROR") });
    samples.append({ "Java 版本不匹配",
                     "Unsupported class file major version 65.0\nException in thread \"main\" java.lang.UnsupportedClassVersionError",
                     1, QStringLiteral("java_version_mismatch"), QStringLiteral("JAVA_ERROR") });
    samples.append({ "退出码 0 但生成崩溃报告",
                     "---- Minecraft Crash Report ----\n// I let you down. Sorry :(\nCrash report saved to: #!CRASH! D:\\instances\\.minecraft\\crash-reports\\crash-20261122.txt",
                     0, QStringLiteral("exit_zero_crash_report"), QStringLiteral("OTHER_ERROR") });
    samples.append({ "退出码 0 但渲染线程报错",
                     "[22:00:00] [main/INFO]: Stopping!\nException in thread \"Render thread\" java.lang.IllegalStateException: shader compile",
                     0, QStringLiteral("exit_zero_error_signature"), QStringLiteral("OTHER_ERROR") });
    samples.append({ "正常退出不误报",
                     "[22:00:01] [main/INFO]: Stopping!\n[22:00:01] [Sound engine/INFO]: Sound engine shut down\n[22:00:02] [main/INFO]: Saved the world",
                     0, QString(), QString() });

    for (const Sample &s : samples) {
        const ErrorAnalyzer::ErrorInfo info = analyzer->analyzeLog(s.text, s.exitCode);
        check(info.ruleId == s.expectId,
              QStringLiteral("%1 → 命中「%2」（期望 %3）")
                  .arg(s.name, info.ruleId.isEmpty() ? QStringLiteral("（未命中）") : info.ruleId,
                       s.expectId.isEmpty() ? QStringLiteral("（未命中）") : s.expectId));
        if (!s.expectCode.isEmpty())
            check(info.errorCode == s.expectCode,
                  QStringLiteral("%1 → 错误代码 %2（期望 %3）").arg(s.name, info.errorCode, s.expectCode));
    }

    // ===== 3. 长日志尾部保留 + 次要命中 + 报告文本 =====
    qDebug() << "[3] 长日志尾部保留与报告生成";

    QString longLog;
    for (int i = 0; i < 400; ++i)
        longLog += QStringLiteral("[22:%1:%2] [Render thread/INFO]: Loading chunk %3\n").arg(i / 60, 2, 10, QLatin1Char('0')).arg(i % 60, 2, 10, QLatin1Char('0')).arg(i);
    longLog += QStringLiteral("java.lang.OutOfMemoryError: Java heap space\n");
    const ErrorAnalyzer::ErrorInfo info = analyzer->analyzeLog(longLog, 1);
    check(info.ruleId == QStringLiteral("memory_heap_space"), "长日志末尾的崩溃特征仍可命中");
    check(!info.evidence.isEmpty() && info.evidence.first().contains(QStringLiteral("OutOfMemoryError")),
          "日志证据提取了命中行原文");
    check(info.errorCode == QStringLiteral("MEMORY_ERROR"), "分类映射为 MEMORY_ERROR");

    // 显卡崩溃样本：多个规则同时命中，主诊断之外应有次要命中
    const QString nativeLog =
        QStringLiteral("A fatal error has been detected by the Java Runtime Environment.\n"
                       "EXCEPTION_ACCESS_VIOLATION (0xc0000005) at pc=0x00007ffabc123456\n"
                       "# C [nvoglv64.dll+0x1a2b3c]\n");
    const ErrorAnalyzer::ErrorInfo nativeInfo = analyzer->analyzeLog(nativeLog, -1073741819);
    check(nativeInfo.ruleId == QStringLiteral("gfx_access_violation"), "多规则命中时按优先级取主诊断");
    check(nativeInfo.secondaryRuleNames.size() >= 1, "保留次要命中规则名");

    const QString report = analyzer->generateErrorReport(info);
    check(report.contains(QStringLiteral("命中规则")), "报告包含命中规则");
    check(report.contains(QStringLiteral("验证方法")), "报告包含验证方法");
    check(report.contains(QStringLiteral("日志证据")), "报告包含日志证据");
    check(!report.contains(QStringLiteral("java.lang.OutOfMemoryError")) || info.evidence.isEmpty()
              || report.contains(info.evidence.first()),
          "报告证据行与命中证据一致");

    // 低置信度规则按“可能”措辞
    const QString cnfLog = QStringLiteral("java.lang.NoClassDefFoundError: com/example/mod/Foo\n");
    const QString cnfReport = analyzer->generateErrorReport(analyzer->analyzeLog(cnfLog, 1));
    check(cnfReport.contains(QStringLiteral("可能命中规则")) || cnfReport.contains(QStringLiteral("可能原因")),
          "低置信度命中按“可能”措辞");

    // ===== 4. 旧接口兼容（启动器自身错误消息） =====
    qDebug() << "[4] 旧接口兼容";

    const ErrorAnalyzer::ErrorInfo legacy = analyzer->analyzeError(QStringLiteral("Could not find Java"));
    check(legacy.errorCode == QStringLiteral("JAVA_ERROR"), "analyzeError 旧接口仍返回 JAVA_ERROR");
    const ErrorAnalyzer::ErrorInfo unknown = analyzer->analyzeError(QStringLiteral("some random message"));
    check(unknown.errorCode == QStringLiteral("OTHER_ERROR"), "未命中规则时返回 OTHER_ERROR");

    // ===== 5. 外部覆盖文件（数据目录 rules/crash_rules.json） =====
    qDebug() << "[5] 外部覆盖文件";

    const QString overridePath =
        Platform::getDataDirectory() + QStringLiteral("/rules/crash_rules.json");
    const bool overrideFilePreExisting = QFile::exists(overridePath);
    check(true, QStringLiteral("覆盖文件路径: %1%2").arg(overridePath,
                                                       overrideFilePreExisting ? QStringLiteral("（已存在，跳过写入测试）") : QStringLiteral("（尚未创建）")));
    if (!overrideFilePreExisting) {
        QDir().mkpath(QFileInfo(overridePath).absolutePath());
        QFile out(overridePath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            out.write("{\n  \"schema\": 1,\n  \"rules\": [\n    {\n"
                      "      \"id\": \"memory_heap_space\",\n"
                      "      \"name\": \"内存不足(覆盖测试)\",\n"
                      "      \"category\": \"memory\",\n"
                      "      \"patterns\": [\"java\\\\.lang\\\\.OutOfMemoryError: Java heap space\"],\n"
                      "      \"antiPatterns\": [],\n"
                      "      \"exitCodes\": [],\n"
                      "      \"cause\": \"覆盖测试原因\",\n"
                      "      \"verify\": \"覆盖测试验证\",\n"
                      "      \"solution\": \"覆盖测试方案\",\n"
                      "      \"confidence\": 0.9,\n"
                      "      \"priority\": 200\n    },\n"
                      "    {\n"
                      "      \"id\": \"probe_appended_rule\",\n"
                      "      \"name\": \"探针追加规则\",\n"
                      "      \"category\": \"other\",\n"
                      "      \"patterns\": [\"PROBE_UNIQUE_MARKER_STRING\"],\n"
                      "      \"antiPatterns\": [],\n"
                      "      \"exitCodes\": [],\n"
                      "      \"cause\": \"追加\",\n"
                      "      \"verify\": \"追加\",\n"
                      "      \"solution\": \"追加\",\n"
                      "      \"confidence\": 0.9,\n"
                      "      \"priority\": 300\n    }\n  ]\n}\n");
            out.close();

            analyzer->reloadRules();
            const ErrorAnalyzer::ErrorInfo overridden = analyzer->analyzeLog(longLog, 1);
            check(overridden.ruleName == QStringLiteral("内存不足(覆盖测试)"), "外部文件按 id 覆盖内置规则");
            const ErrorAnalyzer::ErrorInfo appended =
                analyzer->analyzeLog(QStringLiteral("PROBE_UNIQUE_MARKER_STRING in log"), 1);
            check(appended.ruleId == QStringLiteral("probe_appended_rule"), "外部新规则被追加");

            QFile::remove(overridePath);
            analyzer->reloadRules();
            const ErrorAnalyzer::ErrorInfo restored = analyzer->analyzeLog(longLog, 1);
            check(restored.ruleName == QStringLiteral("内存不足"), "删除覆盖文件后恢复内置规则");
        } else {
            check(false, "覆盖文件写入失败（跳过覆盖测试）");
        }
    }

    qDebug() << "==========";
    qDebug() << "通过" << g_passed << "项，失败" << g_failed << "项";
    return g_failed == 0 ? 0 : 1;
}
