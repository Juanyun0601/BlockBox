/**
 * @file   mod_scanner_dep_probe.cpp
 * @brief  模组扫描器依赖解析探针
 *
 * 在临时目录手工构造两个「最小 ZIP」（存储方式，无压缩）：
 *  - fabric 模组：fabric.mod.json 带 depends（含 fabricloader/minecraft 环境依赖）与 suggests
 *  - forge 模组：META-INF/mods.toml 带 mandatory=true、type="required"、type="optional" 三种前置
 * 调 ModScanner::scanModsSync 断言解析结果。退出码 0 = 全部通过。
 */
#include "utils/mod/ModScanner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <QtZlib/zlib.h>

#include <cstdio>
#include <cstring>

namespace
{

int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-56s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void putU16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

void putU32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

/** 最小 ZIP 写入：存储方式（method 0），兼容绝大多数读取器 */
bool writeStoredZip(const QString &path,
                    const QList<QPair<QString, QByteArray>> &entries)
{
    QByteArray central;
    quint32 offset = 0;

    QByteArray local;
    for (const auto &entry : entries)
    {
        const QByteArray nameBytes = entry.first.toUtf8();
        const QByteArray &data = entry.second;
        const quint32 crc = crc32(crc32(0L, Z_NULL, 0),
                                  reinterpret_cast<const Bytef *>(data.constData()),
                                  static_cast<uInt>(data.size()));

        local.clear();
        local.append("PK\x03\x04", 4);
        putU16(local, 20);          // version needed
        putU16(local, 0);           // flags
        putU16(local, 0);           // method: stored
        putU16(local, 0);           // time
        putU16(local, 0x21);        // date (1980-01-01 合法值)
        putU32(local, crc);
        putU32(local, static_cast<quint32>(data.size()));
        putU32(local, static_cast<quint32>(data.size()));
        putU16(local, static_cast<quint16>(nameBytes.size()));
        putU16(local, 0);           // extra len
        local.append(nameBytes);
        local.append(data);
        offset += local.size();

        central.append("PK\x01\x02", 4);
        putU16(central, 20);        // version made by
        putU16(central, 20);        // version needed
        putU16(central, 0);
        putU16(central, 0);
        putU16(central, 0);
        putU16(central, 0x21);
        putU32(central, crc);
        putU32(central, static_cast<quint32>(data.size()));
        putU32(central, static_cast<quint32>(data.size()));
        putU16(central, static_cast<quint16>(nameBytes.size()));
        putU16(central, 0);         // extra
        putU16(central, 0);         // comment
        putU16(central, 0);         // disk
        putU16(central, 0);         // internal attrs
        putU32(central, 0);         // external attrs
        putU32(central, offset - local.size()); // local header offset（写入前已累计，这里修正）
        central.append(nameBytes);
    }

    // 修正：central 里写 offset 需要逐条累计，上面偷懒只能重算——重新构建一遍
    central.clear();
    quint32 running = 0;
    for (const auto &entry : entries)
    {
        const QByteArray nameBytes = entry.first.toUtf8();
        const QByteArray &data = entry.second;
        const quint32 crc = crc32(crc32(0L, Z_NULL, 0),
                                  reinterpret_cast<const Bytef *>(data.constData()),
                                  static_cast<uInt>(data.size()));
        const quint32 localSize = 30 + nameBytes.size() + data.size();

        central.append("PK\x01\x02", 4);
        putU16(central, 20);
        putU16(central, 20);
        putU16(central, 0);
        putU16(central, 0);
        putU16(central, 0);
        putU16(central, 0x21);
        putU32(central, crc);
        putU32(central, static_cast<quint32>(data.size()));
        putU32(central, static_cast<quint32>(data.size()));
        putU16(central, static_cast<quint16>(nameBytes.size()));
        putU16(central, 0);
        putU16(central, 0);
        putU16(central, 0);
        putU16(central, 0);
        putU32(central, 0);
        putU32(central, running);
        central.append(nameBytes);
        running += localSize;
    }

    QByteArray eocd;
    eocd.append("PK\x05\x06", 4);
    putU16(eocd, 0);
    putU16(eocd, 0);
    putU16(eocd, static_cast<quint16>(entries.size()));
    putU16(eocd, static_cast<quint16>(entries.size()));
    putU32(eocd, central.size());
    putU32(eocd, running);
    putU16(eocd, 0);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QByteArray blob;
    for (const auto &entry : entries)
    {
        const QByteArray nameBytes = entry.first.toUtf8();
        const QByteArray &data = entry.second;
        const quint32 crc = crc32(crc32(0L, Z_NULL, 0),
                                  reinterpret_cast<const Bytef *>(data.constData()),
                                  static_cast<uInt>(data.size()));
        blob.append("PK\x03\x04", 4);
        putU16(blob, 20);
        putU16(blob, 0);
        putU16(blob, 0);
        putU16(blob, 0);
        putU16(blob, 0x21);
        putU32(blob, crc);
        putU32(blob, static_cast<quint32>(data.size()));
        putU32(blob, static_cast<quint32>(data.size()));
        putU16(blob, static_cast<quint16>(nameBytes.size()));
        putU16(blob, 0);
        blob.append(nameBytes);
        blob.append(data);
    }
    blob.append(central);
    blob.append(eocd);
    return file.write(blob) > 0;
}

bool hasDep(const ModInfo &info, const QString &name, bool required)
{
    for (const ModDependency &dep : info.dependencies)
        if (dep.name == name && dep.isRequired == required)
            return true;
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return 1;
    const QString modsDir = tempDir.path() + QStringLiteral("/mods");
    QDir().mkpath(modsDir);

    // Fabric 模组：depends 含环境依赖（应过滤）+ 实体前置，suggests 一个可选项
    const QByteArray fabricJson = R"({
        "id": "coolmod",
        "name": "Cool Mod",
        "version": "1.2.3",
        "depends": {
            "fabric-api": "*",
            "fabricloader": ">=0.14.0",
            "minecraft": ">=1.20"
        },
        "suggests": {
            "modmenu": "*"
        }
    })";
    expect("写入 fabric 测试 JAR",
           writeStoredZip(modsDir + QStringLiteral("/coolmod-1.2.3.jar"),
                          {{QStringLiteral("fabric.mod.json"), fabricJson}}));

    // Forge 模组：mandatory=true、type="required"、type="optional" 三种写法 + 环境依赖过滤
    const QByteArray modsToml = R"toml(
modLoader = "javafml"
loaderVersion = "[40,)"

[[mods]]
modId = "forgemod"
displayName = "Forge Mod"
version = "3.4.5"

[[dependencies.forgemod]]
    modId = "jei"
    mandatory = true
    versionRange = "[39,)"
    ordering = "NONE"

[[dependencies.forgemod]]
    modId = "patchouli"
    type = "optional"
    versionRange = "*"

[[dependencies.forgemod]]
    modId = "forge"
    type = "required"
    versionRange = "[40,)"
)toml";
    expect("写入 forge 测试 JAR",
           writeStoredZip(modsDir + QStringLiteral("/forgemod-3.4.5.jar"),
                          {{QStringLiteral("META-INF/mods.toml"), modsToml}}));

    ModScanner scanner;
    LocalModList result;
    expect("扫描完成", scanner.scanModsSync(tempDir.path(), result));
    expect("扫描到 2 个模组", result.mods.size() == 2);

    for (const ModInfo &mod : result.mods)
    {
        if (mod.id == QStringLiteral("coolmod"))
        {
            expect("fabric 模组解析出 fabric-api 必需前置", hasDep(mod, "fabric-api", true));
            expect("fabric suggests 解析为非必需", hasDep(mod, "modmenu", false));
            expect("fabricloader 环境依赖被过滤",
                   !hasDep(mod, "fabricloader", true) && !hasDep(mod, "fabricloader", false));
            expect("minecraft 环境依赖被过滤",
                   !hasDep(mod, "minecraft", true) && !hasDep(mod, "minecraft", false));
            expect("fabric 共 2 条依赖记录", mod.dependencies.size() == 2);
        }
        else if (mod.id == QStringLiteral("forgemod"))
        {
            expect("forge mandatory=true 解析为必需前置", hasDep(mod, "jei", true));
            expect("forge 版本范围保留", [&]() {
                for (const auto &d : mod.dependencies)
                    if (d.name == QStringLiteral("jei"))
                        return d.version == QStringLiteral("[39,)");
                return false;
            }());
            expect("forge type=optional 解析为非必需", hasDep(mod, "patchouli", false));
            expect("forge 环境依赖 forge 被过滤",
                   !hasDep(mod, "forge", true) && !hasDep(mod, "forge", false));
            expect("forge 共 2 条依赖记录", mod.dependencies.size() == 2);
        }
    }

    fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
            g_failures);
    return g_failures == 0 ? 0 : 1;
}
