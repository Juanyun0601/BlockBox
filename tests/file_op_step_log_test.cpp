/**
 * @file   file_op_step_log_test.cpp
 * @brief  运行日志 → 文件操作步骤 解析逻辑自测（控制台，不依赖 UI）
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include <QCoreApplication>
#include <QDebug>

#include "utils/FileOpStepLog.h"

namespace {

int g_failures = 0;

void expectTrue(bool cond, const char *name)
{
    if (!cond)
        ++g_failures;
    qInfo() << (cond ? "[PASS]" : "[FAIL]") << name;
}

void expectEqual(const QString &actual, const QString &expected, const char *name)
{
    const bool ok = actual == expected;
    if (!ok)
        ++g_failures;
    qInfo() << (ok ? "[PASS]" : "[FAIL]") << name
            << "| expected:" << expected << "| actual:" << actual;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString sample = QStringLiteral(
        "\n==================== 方块盒子启动 2026-10-24 20:11:30 ====================\n"
        "版本 beta5.1 · Windows 10.0.22631 · x86_64 · 平台 Windows\n"
        "2026-10-24 20:11:33.045 [DEBUG] Downloading  \"https://example.com/lib/guava.jar\"\n"
        "2026-10-24 20:11:35.948 [DEBUG] Download succeeded: \"https://example.com/lib/guava.jar\"\n"
        "2026-10-24 20:11:36.100 [WARN ] [ForgeNewInstallTask] Failed to copy library: libraries/foo/bar.jar\n"
        "2026-10-24 20:11:36.200 [DEBUG] [ForgeNewInstallTask] Copied 12 embedded libraries\n"
        "2026-10-24 20:11:36.300 [DEBUG] [NeoForgeInstaller] 下载失败: https://example.com/bad.jar 超时\n"
        "2026-10-24 20:11:36.400 [DEBUG] [JarUtils] Unsafe entry path rejected: ../../evil.dll\n"
        "2026-10-24 20:11:36.500 [DEBUG] [ForgeNewInstallTask] Executing processor: processor.jar\n"
        "2026-10-24 20:11:36.600 [DEBUG] [JavaDownloader] Downloaded: /tmp/java-21.zip\n"
        "2026-10-24 20:11:37.000 [DEBUG] 正在下载 \"https://example.com/cn/start.jar\"\n"
        "2026-10-24 20:11:38.000 [DEBUG] 下载完成：\"https://example.com/cn/start.jar\"\n"
        "2026-10-24 20:11:38.500 [DEBUG] 缓存命中：\"https://example.com/cn/lib.jar\"\n"
        "2026-10-24 20:11:39.000 [ERROR] 写入失败：/tmp/cn-out.bin\n"
        "\n==================== 方块盒子启动 2026-10-24 21:00:00 ====================\n"
        "版本 beta5.1 · Windows 10.0.22631 · x86_64 · 平台 Windows\n"
        "2026-10-24 21:00:01.000 [DEBUG] Downloading  \"https://example.com/only-start.jar\"\n");

    const QList<FileOpStep> steps = FileOpStepLog::parseLogText(sample);

    qInfo() << "parsed steps:" << steps.size();
    for (const FileOpStep &s : steps) {
        qInfo() << " " << s.sessionIndex << s.kind << s.title << "ok=" << s.success
                << "detail=" << s.detail;
    }

    expectTrue(steps.size() >= 6, "at least 6 steps parsed");

    // 下载配对：guava 应合并为一步
    int guavaMerged = 0;
    int onlyStart = 0;
    int copyFail = 0;
    int copyLibs = 0;
    int downloadFail = 0;
    int extractFail = 0;
    int processor = 0;
    int javaDownloaded = 0;
    for (const FileOpStep &s : steps) {
        if (s.detail.contains(QStringLiteral("guava.jar"))) {
            ++guavaMerged;
            expectTrue(s.success, "guava step success");
            expectTrue(s.title.startsWith(QStringLiteral("下载")), "guava title is download");
        }
        if (s.detail.contains(QStringLiteral("only-start.jar")))
            ++onlyStart;
        if (s.title.startsWith(QStringLiteral("复制失败")))
            ++copyFail;
        if (s.kind == QStringLiteral("复制") && s.success)
            ++copyLibs;
        if (s.title.startsWith(QStringLiteral("下载失败")))
            ++downloadFail;
        if (s.kind == QStringLiteral("解压") && !s.success)
            ++extractFail;
        if (s.kind == QStringLiteral("安装") && s.title.contains(QStringLiteral("执行安装处理器")))
            ++processor;
        if (s.title.contains(QStringLiteral("java-21.zip")))
            ++javaDownloaded;
    }

    expectTrue(guavaMerged == 1, "guava start+succeeded merged into one step");
    expectTrue(onlyStart == 1, "unpaired start download kept");
    expectTrue(copyFail == 1, "failed copy library detected");
    expectTrue(copyLibs == 1, "Copied libraries counted as copy success");
    expectTrue(downloadFail == 1, "Chinese download failure detected");
    expectTrue(extractFail == 1, "unsafe extract rejected detected");
    expectTrue(processor == 1, "executing processor detected");
    expectTrue(javaDownloaded == 1, "JavaDownloader Downloaded detected");

    // 中文日志模式：正在下载 + 下载完成 应合并为一步；缓存命中、写入失败应各自识别
    int cnMerged = 0, cnCache = 0, cnWriteFail = 0;
    for (const FileOpStep &s : steps) {
        if (s.detail.contains(QStringLiteral("cn/start.jar")))
            ++cnMerged;
        if (s.title.startsWith(QStringLiteral("命中缓存")) && s.detail.contains(QStringLiteral("cn/lib.jar")))
            ++cnCache;
        if (s.title.startsWith(QStringLiteral("写入失败")) && s.detail.contains(QStringLiteral("cn-out.bin")))
            ++cnWriteFail;
    }
    expectTrue(cnMerged == 1, "Chinese start+complete merged into one step");
    expectTrue(cnCache == 1, "Chinese cache hit detected");
    expectTrue(cnWriteFail == 1, "Chinese write failure detected");

    // 会话划分
    expectTrue(steps.first().sessionIndex == 0, "first step in session 0");
    bool hasSession1 = false;
    for (const FileOpStep &s : steps) {
        if (s.sessionIndex == 1)
            hasSession1 = true;
    }
    expectTrue(hasSession1, "second session present");

    // 噪音行不应入选
    for (const FileOpStep &s : steps) {
        expectTrue(!s.title.contains(QStringLiteral("PerformanceMonitor")),
                   "no performance monitor noise");
    }

    if (g_failures == 0) {
        qInfo() << "ALL PASSED";
        return 0;
    }
    qWarning() << "FAILURES:" << g_failures;
    return 1;
}
