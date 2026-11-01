/**
 * @file   launcher_importer_probe.cpp
 * @brief  独立探针：只依赖 QtCore，打印 LauncherImporter::detectAll()/importFrom() 的真实结果
 *
 * 用法（在构建目录外任意位置）：
 *   launcher_importer_probe.exe            # 读真实 %APPDATA%
 *   set BLOCKBOX_APPDATA=D:\fake && ...    # 指向假数据目录测试解析
 *
 * 结果同时打印到控制台并写入 <exe目录>/report.txt（UTF-8，避免控制台代码页把中文显示成乱码）。
 */
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include "utils/LauncherImporter.h"

namespace {

QStringList g_report;

void add(const QString &line)
{
    g_report << line;
    qInfo().noquote() << line;
}

void printDetect(const LauncherDetect &d)
{
    add(QStringLiteral("==== %1 / %2").arg(d.key, d.displayName));
    add(QStringLiteral("  detected   : %1").arg(d.detected ? QStringLiteral("true") : QStringLiteral("false")));
    add(QStringLiteral("  dataDir    : %1").arg(d.dataDir));
    add(QStringLiteral("  minecraft  : %1").arg(d.minecraftDir));
    for (const QString &g : d.gameDirs)
        add(QStringLiteral("  gameDir    : %1").arg(g));
    add(QStringLiteral("  accounts   : %1").arg(d.accountCount));
    add(QStringLiteral("  versions   : %1  %2").arg(d.versionCount).arg(d.versionNames.mid(0, 3).join(QStringLiteral(", "))));
    add(QStringLiteral("  java       : %1").arg(d.javaPath));
    for (const QString &j : d.javaPaths)
        add(QStringLiteral("  javas      : %1").arg(j));
    for (const LauncherAuthServer &s : d.authServers)
        add(QStringLiteral("  authServer : %1  (%2)").arg(s.name, s.url));
    add(QStringLiteral("  download   : source=%1 threads=%2").arg(d.downloadSource).arg(d.downloadThreads));
    add(QStringLiteral("  autoMemory : %1 (known=%2)").arg(d.autoMemory).arg(d.autoMemoryKnown));
    add(QStringLiteral("  memory     : %1 ~ %2").arg(d.javaMinMemoryMb).arg(d.javaMaxMemoryMb));
    add(QStringLiteral("  detail     : %1").arg(d.detail));
}

void printImport(const LauncherImportResult &r)
{
    add(QStringLiteral("  ok         : %1").arg(r.ok ? QStringLiteral("true") : QStringLiteral("false")));
    add(QStringLiteral("  accounts   : %1").arg(r.accountCount));
    add(QStringLiteral("  versions   : %1  %2").arg(r.versionCount).arg(r.versionNames.mid(0, 3).join(QStringLiteral(", "))));
    add(QStringLiteral("  minecraft  : %1").arg(r.minecraftDir));
    for (const QString &g : r.instanceDirs)
        add(QStringLiteral("  instanceDir: %1").arg(g));
    add(QStringLiteral("  javas      : %1").arg(r.javaPaths.join(QStringLiteral(" | "))));
    for (const LauncherAuthServer &s : r.authServers)
        add(QStringLiteral("  authServer : %1  (%2)").arg(s.name, s.url));
    add(QStringLiteral("  download   : source=%1 threads=%2").arg(r.downloadSource).arg(r.downloadThreads));
    add(QStringLiteral("  autoMemory : %1 (known=%2)").arg(r.autoMemory).arg(r.autoMemoryKnown));
    add(QStringLiteral("  memory     : %1 ~ %2").arg(r.javaMinMemoryMb).arg(r.javaMaxMemoryMb));
    add(QStringLiteral("  detail     : %1").arg(r.detail));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    add(QStringLiteral("APPDATA(env)      = %1").arg(qEnvironmentVariable("APPDATA")));
    add(QStringLiteral("BLOCKBOX_APPDATA  = %1").arg(qEnvironmentVariable("BLOCKBOX_APPDATA")));
    add(QStringLiteral("appDataDir()      = %1").arg(LauncherImporter::appDataDir()));

    const QList<LauncherDetect> all = LauncherImporter::detectAll();
    for (const LauncherDetect &d : all)
        printDetect(d);

    for (const LauncherDetect &d : all) {
        if (!d.detected)
            continue;
        add(QStringLiteral("---- importFrom(%1)").arg(d.key));
        printImport(LauncherImporter::importFrom(d.key));
    }

    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/report.txt");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(g_report.join(QLatin1Char('\n')).toUtf8());
    return 0;
}
