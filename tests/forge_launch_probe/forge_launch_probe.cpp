/**
 * @file   forge_launch_probe.cpp
 * @brief  带加载器实例（Forge / NeoForge）启动链路探针
 *
 * 走真实代码路径验证 GameLauncher::buildLaunchCommand 产出的参数可用：
 *   1) 静态检查：${library_directory} / ${version_name} / --width 重复等
 *      （这三处都是"实例带继承/加载器"时最常见的启动即崩来源）
 *   2) 真机启动：用解析出的参数真的拉起 java，等到游戏窗口出现为止
 *
 * 用法：forge_launch_probe.exe [版本目录] [java路径]
 * 退出码：0=参数校验通过且游戏窗口出现 1=参数校验失败 2=游戏进程退出/超时 3=用法错误
 */

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <cstring>

#define private public
#include "utils/GameLauncher.h"
#undef private

#include "utils/SettingsManager.h"
#include "utils/fabric/FabricInstaller.h"
#include "utils/RuntimeDependencyManager.h"

// ── 依赖空实现 ──
// GameLauncher.o 引用了这两处（与本探针路径无关），这里给空实现，
// 免得为了链接把它们背后的整个 GUI 依赖链（AppMessageBox/ThemeManager/zlib…）都拖进来。
bool FabricInstaller::repairIncompleteVersionJson(const QString &, const std::function<void(const QString &)> &)
{
    return false;
}

bool RuntimeDependencyManager::isSupportedPlatform()
{
    return false;
}

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

int g_failures = 0;

void line(const char *tag, const QString &msg)
{
    std::fprintf(stdout, "[%s][%s] %s\n",
                 qUtf8Printable(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))),
                 tag, qUtf8Printable(msg));
    std::fflush(stdout);
}

void check(bool ok, const QString &what)
{
    std::fprintf(stdout, "  [%s] %s\n", ok ? "PASS" : "FAIL", qUtf8Printable(what));
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

#ifdef Q_OS_WIN
/// 枚举属于指定进程的可见窗口（游戏窗口检测）
HWND findWindowByPid(DWORD pid)
{
    struct EnumData { DWORD pid; HWND result; };
    EnumData data{pid, nullptr};
    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lParam);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(hWnd, &windowPid);
        if (windowPid != d->pid || !IsWindowVisible(hWnd))
            return TRUE;
        wchar_t title[256] = {0};
        GetWindowTextW(hWnd, title, 256);
        if (wcslen(title) > 0) {
            d->result = hWnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));
    return data.result;
}
#endif

QStringList mavenSplitTarget(const QStringList &args)
{
    // -p 的下一个参数就是模块路径列表
    const int idx = args.indexOf(QStringLiteral("-p"));
    if (idx < 0 || idx + 1 >= args.size())
        return QStringList();
#ifdef Q_OS_WIN
    return args.at(idx + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
#else
    return args.at(idx + 1).split(QLatin1Char(':'), Qt::SkipEmptyParts);
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString versionDir = argc > 1 ? QDir::cleanPath(QString::fromLocal8Bit(argv[1]))
                                        : QStringLiteral("D:/Minecraft/.minecraft/versions/1.20.1-Forge_47.4.23");
    const QString javaPath = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                      : QStringLiteral("D:/系统迁移勿删/.minecraft/runtime/java-runtime-gamma-snapshot/bin/java.exe");

    if (!QFileInfo::exists(versionDir))
    {
        line("FATAL", QString("版本目录不存在: %1").arg(versionDir));
        return 3;
    }
    if (!QFileInfo::exists(javaPath))
    {
        line("FATAL", QString("Java 不存在: %1").arg(javaPath));
        return 3;
    }

    line("PROBE", QString("versionDir=%1").arg(versionDir));
    line("PROBE", QString("java=%1").arg(javaPath));
    line("PROBE", QString("版本隔离=%1")
                      .arg(SettingsManager::instance()->isVersionIsolationEnabled() ? "开" : "关"));

    GameLauncher *launcher = GameLauncher::instance();
    const QJsonObject rawJson = launcher->readVersionJson(versionDir);
    const QString versionId = rawJson.value(QStringLiteral("id")).toString();
    const QString parentId = rawJson.value(QStringLiteral("inheritsFrom")).toString();

    GameLauncher::LaunchConfig config;
    config.instancePath = versionDir;
    config.javaPath = javaPath;
    config.accountName = QStringLiteral("Player");
    config.maxMemory = 4096;
    config.minMemory = 1024;

    const QStringList args = launcher->buildLaunchCommand(config);
    line("CMDLINE", QString("共 %1 个参数，mainClass 前的 JVM 段已省略").arg(args.size()));
    if (qEnvironmentVariableIsSet("PROBE_DUMP_ARGS"))
    {
        for (const QString &a : args)
            std::fprintf(stdout, "    ARG: %s\n", qUtf8Printable(a));
    }

    // ── 静态检查 ──
    line("STATIC", "开始参数校验");

    // 1) 窗口尺寸只能出现一次：重复会让 jopt-simple 抛
    //    MultipleArgumentsForOptionException（Forge 早期窗口初始化直接崩）
    check(args.count(QStringLiteral("--width")) <= 1,
          QString("--width 只出现一次（实际 %1）").arg(args.count(QStringLiteral("--width"))));
    check(args.count(QStringLiteral("--height")) <= 1,
          QString("--height 只出现一次（实际 %1）").arg(args.count(QStringLiteral("--height"))));

    // 2) 模块路径（Forge 的 -p）里每个 jar 都必须真实存在。
    //    只有 JSON 里真的写了 ${library_directory} / -p 才有这一项：
    //    新版 Forge（net.minecraftforge.bootstrap.ForgeBootstrap，如 26.3）不走模块路径。
    bool jsonUsesModulePath = false;
    const QJsonArray jsonJvmArgs = rawJson.value(QStringLiteral("arguments")).toObject()
                                       .value(QStringLiteral("jvm")).toArray();
    for (const QJsonValue &v : jsonJvmArgs)
    {
        const QString s = v.isString() ? v.toString() : v.toObject().value(QStringLiteral("value")).toString();
        if (s.contains(QStringLiteral("${library_directory}")))
            jsonUsesModulePath = true;
    }
    bool jsonUsesIgnoreList = false;
    for (const QJsonValue &v : jsonJvmArgs)
    {
        const QString s = v.isString() ? v.toString() : v.toObject().value(QStringLiteral("value")).toString();
        if (s.contains(QStringLiteral("ignoreList")))
            jsonUsesIgnoreList = true;
    }

    if (jsonUsesModulePath)
    {
        const QStringList modulePath = mavenSplitTarget(args);
        check(!modulePath.isEmpty(), QStringLiteral("存在 -p 模块路径"));
        int missingModules = 0;
        for (const QString &jar : modulePath)
            if (!QFileInfo::exists(jar))
                ++missingModules;
        check(missingModules == 0,
              QString("模块路径内 jar 全部存在（%1 条，缺失 %2）").arg(modulePath.size()).arg(missingModules));

        // 3) libraryDirectory 必须指向真实目录
        const QString kLibraryDirArg = QStringLiteral("-DlibraryDirectory=");
        QString libraryDir;
        for (const QString &a : args)
            if (a.startsWith(kLibraryDirArg))
                libraryDir = a.mid(kLibraryDirArg.size());
        check(!libraryDir.isEmpty() && QDir(libraryDir).exists(),
              QString("libraryDirectory 存在: %1").arg(libraryDir));
    }
    else
    {
        line("STATIC", "该实例不使用 -p 模块路径（ForgeBootstrap 形态），跳过模块路径检查");
    }

    // 4) ignoreList 必须包含真正的客户端 JAR 名（否则 Forge 会把版本 JAR 当 mod 扫描）
    if (jsonUsesIgnoreList)
    {
        QString ignoreList;
        for (const QString &a : args)
            if (a.startsWith(QStringLiteral("-DignoreList=")))
                ignoreList = a;
        check(!versionId.isEmpty() && ignoreList.contains(versionId + QStringLiteral(".jar")),
              QString("ignoreList 命中 %1.jar").arg(versionId));
    }
    else
    {
        line("STATIC", "该实例没有 ignoreList 参数，跳过客户端 JAR 命名检查");
    }

    // 5) 占位符必须全部替换完
    QStringList leftover;
    for (const QString &a : args)
        if (a.contains(QStringLiteral("${")))
            leftover << a;
    check(leftover.isEmpty(), QString("无未替换占位符（残留 %1）").arg(leftover.size()));
    for (const QString &a : leftover)
        std::fprintf(stdout, "      LEFTOVER: %s\n", qUtf8Printable(a));

    // 6) 认证参数齐全（继承链断掉时最常见的漏项）
    for (const char *opt : {"--username", "--accessToken", "--version", "--gameDir", "--assetsDir"})
    {
        bool found = false;
        for (int i = 0; i + 1 < args.size(); ++i)
            if (args.at(i) == QLatin1String(opt)) { found = true; break; }
        check(found, QString("%1 已传入").arg(QLatin1String(opt)));
    }

    if (g_failures > 0)
    {
        line("STATIC", QString("参数校验失败：%1 项").arg(g_failures));
        return 1;
    }
    line("STATIC", "参数校验全部通过");

    // ── 真机启动 ──
    QProcess proc;
    proc.setWorkingDirectory(versionDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("INST_NAME"), versionId);
    proc.setProcessEnvironment(env);
    proc.setProgram(javaPath);
    proc.setArguments(args);
    proc.start();

    if (!proc.waitForStarted(30000))
    {
        line("FATAL", QString("java 启动失败: %1").arg(proc.errorString()));
        return 2;
    }
    line("LAUNCH", QString("java 已启动 pid=%1（父版本: %2）").arg(proc.processId()).arg(parentId));

    QString buffer;
    bool soundEngine = false;
    bool windowFound = false;
    bool crashed = false;
    int exitCode = -1;
    QString tail;

    QObject::connect(&proc, &QProcess::readyReadStandardOutput, [&]() {
        const QString chunk = QString::fromLocal8Bit(proc.readAllStandardOutput());
        buffer += chunk;
        if (buffer.contains(QStringLiteral("Sound engine started")))
            soundEngine = true;
    });
    QObject::connect(&proc, &QProcess::readyReadStandardError, [&]() {
        const QString chunk = QString::fromLocal8Bit(proc.readAllStandardError());
        buffer += chunk;
        tail += chunk;
        if (tail.size() > 8000)
            tail = tail.right(8000);
    });
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     [&](int code, QProcess::ExitStatus) {
        crashed = true;
        exitCode = code;
    });

    QElapsedTimer timer;
    timer.start();
    const qint64 timeoutMs = 300000; // Forge 首次启动（含建模）可能要 1 分钟以上
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (!windowFound)
        {
#ifdef Q_OS_WIN
            if (findWindowByPid(DWORD(proc.processId())) != nullptr)
            {
                windowFound = true;
                line("LAUNCH", "检测到游戏窗口");
            }
#endif
        }
        if (crashed)
        {
            line("LAUNCH", QString("游戏进程退出，exit=%1").arg(exitCode));
            QCoreApplication::quit();
        }
        else if (soundEngine && windowFound)
        {
            line("LAUNCH", "游戏已进入主界面（窗口 + 音频引擎就绪）");
            QCoreApplication::quit();
        }
        else if (timer.elapsed() > timeoutMs)
        {
            line("LAUNCH", QString("等待超时（%1ms）").arg(timeoutMs));
            QCoreApplication::quit();
        }
    });
    poll.start(500);

    app.exec();

    if (crashed)
    {
        line("RESULT", "启动失败");
        std::fprintf(stdout, "---- 进程输出尾部 ----\n%s\n", qUtf8Printable(tail.right(4000)));
        if (!buffer.isEmpty() && tail.isEmpty())
            std::fprintf(stdout, "%s\n", qUtf8Printable(buffer.right(4000)));
        return 2;
    }

    if (!windowFound || !soundEngine)
    {
        line("RESULT", QString("未确认启动成功（窗口=%1 音频=%2）")
                           .arg(windowFound ? "有" : "无", soundEngine ? "有" : "无"));
        return 2;
    }

    // 稳定一会儿再收工，确认不是"闪一下就没了"
    proc.waitForFinished(5000);
    if (proc.state() == QProcess::NotRunning)
    {
        line("RESULT", "窗口出现后进程立即退出（启动期崩溃）");
        std::fprintf(stdout, "---- 进程输出尾部 ----\n%s\n", qUtf8Printable(tail.right(4000)));
        return 2;
    }

    proc.kill();
    proc.waitForFinished(10000);
    line("RESULT", "启动成功（已结束测试进程）");
    return 0;
}
