/**
 * @file   authlib_injector_probe.cpp
 * @brief  authlib-injector 注入模块探针
 *
 * 覆盖：
 *  - supportsServer 正反例（http/https 通过；空地址、非 http、Mojang 官方域名拒绝）
 *  - jar 未就绪时 jvmArgs 为空
 *  - ensureInstalled 真实下载（官方源 / 镜像 / GitHub 任一可用即可），jar > 100KB、版本标记可读
 *  - jar 就绪后 jvmArgs 恰好 6 条、参数内容与结尾斜杠归一正确
 *  - 第二次 ensureInstalled 直接成功且不重复下载
 * 退出码 0 = 全部通过。
 */
#include "utils/auth/AuthlibInjector.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <cstdio>

namespace {

int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-64s %s\n", label, condition ? "OK" : "FAIL");
    fflush(stderr);
    if (!condition)
    {
        ++g_failures;
    }
}

/** 逐条打印 JVM 参数，失败时方便对照实际值 */
void dumpArgs(const QStringList &args)
{
    for (const QString &arg : args)
    {
        fprintf(stderr, "[probe]   arg: %s\n", qPrintable(arg));
    }
    fflush(stderr);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 用 Qt 的测试模式把数据目录指到隔离位置，并在开头清空，
    // 保证「注入前 jvmArgs 为空」和真实下载两条断言每次运行都成立
    QStandardPaths::setTestModeEnabled(true);

    const QString installDir = AuthlibInjector::installDir();
    fprintf(stderr, "[probe] installDir = %s\n", qPrintable(installDir));
    QDir(installDir).removeRecursively();

    const QString server = QStringLiteral("https://littleskin.cn/api/yggdrasil");
    const QString serverRoot = server + QStringLiteral("/");

    // ---- supportsServer ----
    expect("supportsServer: https 根地址", AuthlibInjector::supportsServer(server));
    expect("supportsServer: http 根地址",
           AuthlibInjector::supportsServer(QStringLiteral("http://localhost:8123/")));
    expect("supportsServer: 无路径域名",
           AuthlibInjector::supportsServer(QStringLiteral("https://skin.example.com")));
    expect("supportsServer: 拒绝空地址", !AuthlibInjector::supportsServer(QString()));
    expect("supportsServer: 拒绝非 http 协议",
           !AuthlibInjector::supportsServer(QStringLiteral("ftp://skin.example.com")));
    expect("supportsServer: 拒绝无协议地址",
           !AuthlibInjector::supportsServer(QStringLiteral("skin.example.com")));
    expect("supportsServer: 拒绝 minecraft.net",
           !AuthlibInjector::supportsServer(QStringLiteral("https://minecraft.net")));
    expect("supportsServer: 拒绝 mojang.com 子域",
           !AuthlibInjector::supportsServer(QStringLiteral("https://sessionserver.mojang.com")));
    expect("supportsServer: 拒绝 api.minecraftservices.com",
           !AuthlibInjector::supportsServer(QStringLiteral("https://api.minecraftservices.com")));

    // ---- 注入前 ----
    expect("isInstalled: 安装前为 false", !AuthlibInjector::isInstalled());
    expect("jvmArgs: jar 未就绪时为空", AuthlibInjector::jvmArgs(server).isEmpty());

    // ---- 真实下载 ----
    fprintf(stderr, "[probe] ---- ensureInstalled 真实下载开始 ----\n");
    QElapsedTimer downloadTimer;
    downloadTimer.start();
    QString error;
    const bool installed = AuthlibInjector::ensureInstalled(
        &error, [](const QString &message) {
            fprintf(stderr, "[progress] %s\n", message.toUtf8().constData());
            fflush(stderr);
        });
    const qint64 downloadMs = downloadTimer.elapsed();
    fprintf(stderr, "[probe] ---- ensureInstalled 结束（%lld ms）----\n",
            static_cast<long long>(downloadMs));

    expect("ensureInstalled: 下载成功", installed);
    if (!installed)
    {
        fprintf(stderr, "[probe] error = %s\n", qPrintable(error));
    }

    expect("ensureInstalled: errorOut 为空", error.isEmpty());
    expect("isInstalled: 安装后为 true", AuthlibInjector::isInstalled());

    const QFileInfo jarInfo(AuthlibInjector::jarPath());
    fprintf(stderr, "[probe] jarPath = %s (%lld bytes)\n",
            qPrintable(jarInfo.absoluteFilePath()), static_cast<long long>(jarInfo.size()));
    expect("jar 存在且 > 100KB", jarInfo.exists() && jarInfo.size() > 100 * 1024);
    expect("jar 是 ZIP（PK\\x03\\x04）",
           jarInfo.size() > 4 && [&jarInfo]() {
               QFile file(jarInfo.absoluteFilePath());
               if (!file.open(QIODevice::ReadOnly))
               {
                   return false;
               }
               return file.read(4) == QByteArray("PK\x03\x04", 4);
           }());

    const QString version = AuthlibInjector::installedVersion();
    fprintf(stderr, "[probe] installedVersion = %s\n", qPrintable(version));
    expect("installedVersion 非空", !version.isEmpty());

    // 版本标记的其它字段顺带检查：sha256 必须是 64 位十六进制、source 非空
    QFile versionFile(installDir + QStringLiteral("/.version.json"));
    bool markOk = versionFile.open(QIODevice::ReadOnly);
    QJsonObject mark;
    if (markOk)
    {
        mark = QJsonDocument::fromJson(versionFile.readAll()).object();
        versionFile.close();
    }
    expect("版本标记: 文件可读", markOk);
    expect("版本标记: sha256 为 64 位十六进制",
           mark.value(QStringLiteral("sha256")).toString().size() == 64);
    expect("版本标记: source 非空", !mark.value(QStringLiteral("source")).toString().isEmpty());
    expect("版本标记: installed_at 非空",
           !mark.value(QStringLiteral("installed_at")).toString().isEmpty());
    fprintf(stderr, "[probe] versionMark: version=%s build=%d source=%s sha256=%s\n",
            mark.value(QStringLiteral("version")).toString().toUtf8().constData(),
            mark.value(QStringLiteral("build_number")).toInt(),
            mark.value(QStringLiteral("source")).toString().toUtf8().constData(),
            mark.value(QStringLiteral("sha256")).toString().toUtf8().constData());

    // ---- jar 就绪后的参数 ----
    const QStringList args = AuthlibInjector::jvmArgs(server);
    dumpArgs(args);
    expect("jvmArgs: 共 6 条", args.size() == 6);
    expect("jvmArgs: javaagent 指向已下载 jar 且补结尾斜杠",
           args.size() == 6
               && args.first()
                      == QStringLiteral("-javaagent:%1=%2")
                             .arg(AuthlibInjector::jarPath(), serverRoot));
    expect("jvmArgs: 环境标记", args.contains(QStringLiteral("-Dminecraft.api.env=custom")));
    expect("jvmArgs: auth.host",
           args.contains(QStringLiteral("-Dminecraft.api.auth.host=%1authserver").arg(serverRoot)));
    expect("jvmArgs: account.host",
           args.contains(
               QStringLiteral("-Dminecraft.api.account.host=%1sessionserver").arg(serverRoot)));
    expect("jvmArgs: session.host",
           args.contains(
               QStringLiteral("-Dminecraft.api.session.host=%1sessionserver").arg(serverRoot)));
    expect("jvmArgs: services.host",
           args.contains(QStringLiteral("-Dminecraft.api.services.host=%1minecraftservices")
                             .arg(serverRoot)));

    // 地址本来就有结尾斜杠时，参数不能出现双斜杠
    const QStringList trailingArgs = AuthlibInjector::jvmArgs(serverRoot);
    expect("jvmArgs: 重复结尾斜杠被归一",
           trailingArgs.size() == 6 && trailingArgs.first() == args.first());

    // 不支持的服务器地址不能注入
    expect("jvmArgs: Mojang 地址返回空",
           AuthlibInjector::jvmArgs(QStringLiteral("https://sessionserver.mojang.com")).isEmpty());
    expect("jvmArgs: 空地址返回空", AuthlibInjector::jvmArgs(QString()).isEmpty());

    // ---- 第二次调用 ----
    QElapsedTimer secondTimer;
    secondTimer.start();
    QString secondError;
    const bool secondInstalled = AuthlibInjector::ensureInstalled(&secondError);
    const qint64 secondMs = secondTimer.elapsed();
    fprintf(stderr, "[probe] 第二次 ensureInstalled 耗时 %lld ms\n",
            static_cast<long long>(secondMs));
    expect("ensureInstalled: 再次调用直接成功", secondInstalled && secondError.isEmpty());
    expect("ensureInstalled: 再次调用未重复下载（< 1s）", secondMs < 1000);

    fprintf(stderr, "[probe] %s (failures=%d)\n",
            g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
