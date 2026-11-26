/**
 * @file   dsh_plugin_install_probe.cpp
 * @brief  「DSH 插件一键安装」文件逻辑探针（无界面）
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 在一个临时目录里造一个假的 DSH 配置（profiles/desktop/package.json），
 * 用真实的插件目录跑一遍：
 *   探测 profile → 安装（复制 + 改 package.json）→ 判断已安装 → 重复安装不重复登记
 *   → 卸载（只摘掉本插件，别的依赖/启用项不动）
 *
 * 通过时输出 DSH-INSTALL-PROBE-OK；失败时输出 DSH-INSTALL-PROBE-FAILED 并以 1 退出。
 *
 * 用法：dsh_plugin_install_probe.exe <插件源目录>
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include "utils/dsh/DshPluginInstaller.h"

namespace {

int gFailures = 0;

void check(const QString &label, bool condition, const QString &detail = QString())
{
    if (!condition)
    {
        ++gFailures;
    }
    QTextStream(stdout) << (condition ? "PASS  " : "FAIL  ") << label
                        << (detail.isEmpty() ? QString() : QStringLiteral(" — %1").arg(detail))
                        << "\n";
}

bool writeText(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QStringList bundleList(const QString &profileDir)
{
    const QJsonObject root = readJson(QDir(profileDir).filePath(QStringLiteral("package.json")));
    QStringList names;
    for (const QJsonValue &value : root.value(QStringLiteral("dsh"))
                                       .toObject()
                                       .value(QStringLiteral("profile"))
                                       .toObject()
                                       .value(QStringLiteral("bundles"))
                                       .toArray())
    {
        names.append(value.toString());
    }
    return names;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QString payloadDir =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("D:/BlockBox/dsh-blockbox-bridge");
    if (!QFileInfo::exists(QDir(payloadDir).filePath(QStringLiteral("index.js"))))
    {
        out << "DSH-INSTALL-PROBE-FAILED 插件源目录不存在：" << payloadDir << "\n";
        return 1;
    }

    QTemporaryDir sandbox;
    if (!sandbox.isValid())
    {
        out << "DSH-INSTALL-PROBE-FAILED 建不了临时目录\n";
        return 1;
    }

    const QString home = QDir(sandbox.path()).filePath(QStringLiteral("home/.dsh"));
    const QString profileDir = QDir(home).filePath(QStringLiteral("profiles/desktop"));
    const QString fakeManifest = QStringLiteral(R"({
  "name": "dsh-profile-desktop",
  "private": true,
  "dependencies": {
    "dsh-free-search": "0.9.4"
  },
  "dsh": {
    "profile": {
      "bundles": ["@deepseek-ai/dsh-base", "dsh-free-search"]
    }
  }
})");
    check("准备假 DSH 配置", writeText(QDir(profileDir).filePath(QStringLiteral("package.json")), fakeManifest));

    // 1. 探测
    QVector<DshPluginInstaller::Profile> profiles = DshPluginInstaller::detectProfiles(home);
    check("探测到 desktop 配置", profiles.size() == 1 && profiles.first().name == QStringLiteral("desktop"),
          QStringLiteral("共 %1 个").arg(profiles.size()));
    check("初始状态是未安装", !profiles.isEmpty() && !profiles.first().installed);

    // 2. 安装
    QString error;
    QStringList log;
    const bool installed = DshPluginInstaller::installInto(profileDir, payloadDir, &error, &log);
    check("安装成功", installed, installed ? QString() : error);
    check("插件文件落到 node_modules",
          QFileInfo::exists(QDir(profileDir).filePath(
              QStringLiteral("node_modules/dsh-blockbox-bridge/index.js"))));
    check("图标和本地化也一起装上",
          QFileInfo::exists(QDir(profileDir).filePath(
              QStringLiteral("node_modules/dsh-blockbox-bridge/icon.svg"))) &&
              QFileInfo::exists(QDir(profileDir).filePath(
                  QStringLiteral("node_modules/dsh-blockbox-bridge/locale/zh.json"))));

    QJsonObject manifest = readJson(QDir(profileDir).filePath(QStringLiteral("package.json")));
    const QString dependency =
        manifest.value(QStringLiteral("dependencies")).toObject()
            .value(QStringLiteral("dsh-blockbox-bridge")).toString();
    check("依赖写成了 link: 指向插件目录",
          dependency.startsWith(QStringLiteral("link:")) &&
              QDir::fromNativeSeparators(dependency.mid(5)) == QDir::fromNativeSeparators(payloadDir),
          dependency);
    check("原有依赖没被动过",
          manifest.value(QStringLiteral("dependencies")).toObject().contains(
              QStringLiteral("dsh-free-search")));
    check("启用清单里加上了插件",
          bundleList(profileDir).contains(QStringLiteral("dsh-blockbox-bridge")));
    check("原有启用项没被动过", bundleList(profileDir).contains(QStringLiteral("dsh-free-search")));
    check("留了 package.json 备份",
          QFileInfo::exists(QDir(profileDir).filePath(
              QStringLiteral("package.json.blockbox-backup"))));

    // 3. 状态判断
    QString version;
    check("isInstalled 认得出来", DshPluginInstaller::isInstalled(profileDir, &version), version);
    check("读到插件版本 1.0.0", version == QStringLiteral("1.0.0"), version);
    profiles = DshPluginInstaller::detectProfiles(home);
    check("探测结果变成已安装", !profiles.isEmpty() && profiles.first().installed);

    // 4. 重复安装不重复登记
    check("重复安装成功", DshPluginInstaller::installInto(profileDir, payloadDir, &error, &log), error);
    int occurrences = 0;
    for (const QString &name : bundleList(profileDir))
    {
        if (name == QStringLiteral("dsh-blockbox-bridge"))
        {
            ++occurrences;
        }
    }
    check("启用清单里只有一条", occurrences == 1, QStringLiteral("出现了 %1 次").arg(occurrences));

    // 5. 卸载
    check("卸载成功", DshPluginInstaller::uninstallFrom(profileDir, &error), error);
    manifest = readJson(QDir(profileDir).filePath(QStringLiteral("package.json")));
    check("依赖已移除",
          !manifest.value(QStringLiteral("dependencies")).toObject().contains(
              QStringLiteral("dsh-blockbox-bridge")));
    check("启用清单已移除", !bundleList(profileDir).contains(QStringLiteral("dsh-blockbox-bridge")));
    check("别的依赖还在",
          manifest.value(QStringLiteral("dependencies")).toObject().contains(
              QStringLiteral("dsh-free-search")));
    check("别的启用项还在", bundleList(profileDir).contains(QStringLiteral("dsh-free-search")));
    check("node_modules 里的副本已删除",
          !QFileInfo::exists(QDir(profileDir).filePath(QStringLiteral("node_modules/dsh-blockbox-bridge"))));
    check("卸载后状态变回未安装", !DshPluginInstaller::isInstalled(profileDir, &version));

    out << (gFailures == 0 ? "DSH-INSTALL-PROBE-OK\n" : "DSH-INSTALL-PROBE-FAILED\n");
    return gFailures == 0 ? 0 : 1;
}
