/**
 * @file   datapack_probe.cpp
 * @brief  数据包管理探针：level.dat DataPacks 读写 + 世界数据包扫描/预览/兼容/冲突
 *
 * 用 QTemporaryDir 手工搭一个世界（NBT + gzip 最小 level.dat、datapacks/ 目录、
 * 文件夹包与 zip 包），验证：
 *  1. 新世界没有 DataPacks 标签时读取返回空列表；写入后结构被创建且其它字段保留
 *  2. 文件夹包与 .zip 包都能扫描：描述、格式、内容分类、实验性内容
 *  3. 兼容性判定：格式偏旧 / 区间匹配 / 无 mcmeta 无效
 *  4. 冲突检测：多个启用包定义同一数据文件
 *  5. 启停、排序写回 level.dat，且原版/模组条目原样保留
 *  6. 导入与删除数据包文件
 *  7. 版本 → 数据包格式映射（含 1.21.9+ 的 x.y 与 26.x）
 * 输出：全部通过返回 0，失败返回 1 并打印原因。
 */
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

#include "utils/Saves/DataPackManager.h"
#include "utils/Saves/LevelDatEditor.h"

// ---------- NBT 手工构造辅助（大端） ----------
static void putU16(QByteArray &out, quint16 v)
{
    out.append(char(v >> 8));
    out.append(char(v & 0xFF));
}
static void putU32(QByteArray &out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}
static void putString(QByteArray &out, const QByteArray &s)
{
    putU16(out, quint16(s.size()));
    out.append(s);
}

// 根 Compound { Data { LevelName, GameType, Version { Name } } }，初始无 DataPacks
static QByteArray buildMinimalNbt(const QByteArray &levelName, const QByteArray &versionName)
{
    QByteArray data;
    data.append(char(8));
    putString(data, "LevelName");
    putString(data, levelName);
    data.append(char(3));
    putString(data, "GameType");
    putU32(data, 1);
    data.append(char(10));
    putString(data, "Version");
    data.append(char(8));
    putString(data, "Name");
    putString(data, versionName);
    data.append(char(0)); // Version 结束
    data.append(char(0)); // Data 结束

    QByteArray root;
    root.append(char(10));
    putString(root, "");   // 根标签名
    root.append(char(10)); // TAG_Compound
    putString(root, "Data");
    root.append(data);
    root.append(char(0));  // 根结束
    return root;
}

static QByteArray gzipBytes(const QByteArray &raw)
{
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(raw.data()));
    strm.avail_in = static_cast<uInt>(raw.size());
    QByteArray out;
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return out;
    char buf[65536];
    int ret = Z_OK;
    do
    {
        strm.next_out = reinterpret_cast<Bytef *>(buf);
        strm.avail_out = sizeof(buf);
        ret = deflate(&strm, Z_FINISH);
        out.append(buf, int(sizeof(buf) - strm.avail_out));
    } while (ret == Z_OK || (ret != Z_STREAM_END && strm.avail_out == 0));
    deflateEnd(&strm);
    return (ret == Z_STREAM_END) ? out : QByteArray();
}

static bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    return ok;
}

static bool writeText(const QString &path, const QString &text)
{
    return writeFile(path, text.toUtf8());
}

// 一个最小的数据包 zip（pack_format 80，含 zipdemo 函数与 minecraft:load）
static const char *k_zipBase64 =
    "UEsDBBQAAAAIAE1ReV0N9QWNPgAAAEIAAAALAAAAcGFjay5tY21ldGGrVipITM5WslKoBjPi0/KLchNLgHwLAx0FpZTU4uSizIKSzPw8oJDS077u53tWPu1pfTZ1w7PedUCGUm0tAFBLAwQUAAAACABNUXld30eaxQkAAAAHAAAAJQAAAGRhdGEvemlwZGVtby9mdW5jdGlvbi9tYWluLm1jZnVuY3Rpb24rTqxUyMjkAgBQSwMEFAAAAAgATVF5XfldvgsKAAAACAAAACcAAABkYXRhL21pbmVjcmFmdC9mdW5jdGlvbi9sb2FkLm1jZnVuY3Rpb24rTqxUqMos4AIAUEsBAhQAFAAAAAgATVF5XQ31BY0+AAAAQgAAAAsAAAAAAAAAAAAAAIABAAAAAHBhY2subWNtZXRhUEsBAhQAFAAAAAgATVF5Xd9HmsUJAAAABwAAACUAAAAAAAAAAAAAAIABZwAAAGRhdGEvemlwZGVtby9mdW5jdGlvbi9tYWluLm1jZnVuY3Rpb25QSwECFAAUAAAACABNUXld+V2+CwoAAAAIAAAAJwAAAAAAAAAAAAAAgAGzAAAAZGF0YS9taW5lY3JhZnQvZnVuY3Rpb24vbG9hZC5tY2Z1bmN0aW9uUEsFBgAAAAADAAMA4QAAAAIBAAAAAA==";

#define CHECK(cond, msg)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(cond))                                                         \
        {                                                                    \
            std::printf("FAIL: %s\n", msg);                                  \
            return 1;                                                        \
        }                                                                    \
    }                                                                        \
    while (0)

static const DataPackInfo *findPack(const DataPackWorld &world, const QString &fileName)
{
    for (const DataPackInfo &info : world.packs)
    {
        if (info.fileName.compare(fileName, Qt::CaseInsensitive) == 0)
            return &info;
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    CHECK(tempDir.isValid(), "临时目录创建失败");

    const QString worldDir = tempDir.path() + QStringLiteral("/TestWorld");
    CHECK(QDir().mkpath(worldDir), "世界目录创建失败");
    CHECK(writeFile(worldDir + QStringLiteral("/level.dat"),
                    gzipBytes(buildMinimalNbt(QByteArrayLiteral("\xE6\xB5\x8B\xE8\xAF\x95\xE4\xB8\x96\xE7\x95\x8C"),
                                              QByteArrayLiteral("1.21.6")))),
          "level.dat 写入失败");

    // ---------- 1. 新世界没有 DataPacks ----------
    {
        const auto lists = LevelDatEditor::readDataPacks(worldDir);
        CHECK(lists.has_value(), "readDataPacks 解析新世界失败");
        CHECK(!lists->hasDataPacks, "新世界不应有 DataPacks 标签");
        CHECK(lists->enabled.isEmpty() && lists->disabled.isEmpty(), "新世界数据包列表应为空");
    }

    // ---------- 2. 首次写入启停列表（创建 DataPacks） ----------
    {
        QString err;
        CHECK(LevelDatEditor::writeDataPacks(worldDir,
                                             QStringList{QStringLiteral("vanilla"),
                                                         QStringLiteral("mod:testmod")},
                                             QStringList{QStringLiteral("mod:disabledpack")},
                                             &err),
              qPrintable(QStringLiteral("首次写入 DataPacks 失败：%1").arg(err)));
        const auto lists = LevelDatEditor::readDataPacks(worldDir);
        CHECK(lists.has_value() && lists->hasDataPacks, "写入后应存在 DataPacks 标签");
        CHECK(lists->enabled == QStringList({QStringLiteral("vanilla"), QStringLiteral("mod:testmod")}),
              "Enabled 列表读回不符");
        CHECK(lists->disabled == QStringList({QStringLiteral("mod:disabledpack")}),
              "Disabled 列表读回不符");

        const auto info = LevelDatEditor::readSave(worldDir);
        CHECK(info.has_value(), "写入数据包列表后 readSave 失败");
        CHECK(info->levelName == QString::fromUtf8("\xE6\xB5\x8B\xE8\xAF\x95\xE4\xB8\x96\xE7\x95\x8C"),
              "写数据包列表后 LevelName 丢失");
        CHECK(info->gameType == 1, "写数据包列表后 GameType 被破坏");
        CHECK(info->versionName == QStringLiteral("1.21.6"), "Version.Name 丢失");
    }

    // ---------- 3. 搭 4 个数据包 + 1 个损坏包 ----------
    const QString dataPackDir = worldDir + QStringLiteral("/datapacks");
    CHECK(QDir().mkpath(dataPackDir), "datapacks 目录创建失败");

    const QString oldPack = dataPackDir + QStringLiteral("/OldPack");
    CHECK(QDir().mkpath(oldPack + QStringLiteral("/data/minecraft/function")), "旧包目录创建失败");
    CHECK(QDir().mkpath(oldPack + QStringLiteral("/data/minecraft/advancement")), "旧包目录创建失败");
    CHECK(writeText(oldPack + QStringLiteral("/pack.mcmeta"),
                    QStringLiteral("{\"pack\":{\"pack_format\":48,\"description\":\"旧版本数据包\"}}")),
          "旧包 pack.mcmeta 写入失败");
    CHECK(writeText(oldPack + QStringLiteral("/data/minecraft/function/load.mcfunction"),
                    QStringLiteral("say old\n")), "旧包函数写入失败");
    CHECK(writeText(oldPack + QStringLiteral("/data/minecraft/advancement/root.json"),
                    QStringLiteral("{}")), "旧包进度写入失败");

    const QString newPack = dataPackDir + QStringLiteral("/NewPack");
    CHECK(QDir().mkpath(newPack + QStringLiteral("/data/minecraft/function")), "新包目录创建失败");
    CHECK(QDir().mkpath(newPack + QStringLiteral("/data/demo/recipe")), "新包目录创建失败");
    CHECK(QDir().mkpath(newPack + QStringLiteral("/data/demo/worldgen/biome")), "新包目录创建失败");
    CHECK(writeText(newPack + QStringLiteral("/pack.mcmeta"),
                    QString::fromUtf8("{\"pack\":{\"description\":{\"text\":\"新包\"},\"min_format\":80,\"max_format\":80}}")),
          "新包 pack.mcmeta 写入失败");
    CHECK(writeText(newPack + QStringLiteral("/data/minecraft/function/load.mcfunction"),
                    QStringLiteral("say new\n")), "新包函数写入失败");
    CHECK(writeText(newPack + QStringLiteral("/data/demo/recipe/craft.json"),
                    QStringLiteral("{}")), "新包配方写入失败");
    CHECK(writeText(newPack + QStringLiteral("/data/demo/worldgen/biome/test.json"),
                    QStringLiteral("{}")), "新包世界生成写入失败");

    const QString brokenPack = dataPackDir + QStringLiteral("/BrokenPack");
    CHECK(QDir().mkpath(brokenPack + QStringLiteral("/data/x/function")), "损坏包目录创建失败");
    CHECK(writeText(brokenPack + QStringLiteral("/data/x/function/a.mcfunction"),
                    QStringLiteral("say broken\n")), "损坏包内容写入失败");

    const QString zipPath = dataPackDir + QStringLiteral("/ZipPack.zip");
    const QByteArray zipBytes = QByteArray::fromBase64(QByteArray(k_zipBase64));
    CHECK(!zipBytes.isEmpty(), "内置 zip 数据包解码失败");
    CHECK(writeFile(zipPath, zipBytes), "zip 数据包写入失败");

    // ---------- 4. 扫描世界 ----------
    DataPackWorld world = DataPackManager::scanWorld(worldDir, QStringLiteral("1.21.6"));
    CHECK(world.ok, qPrintable(QStringLiteral("scanWorld 失败：%1").arg(world.error)));
    CHECK(world.worldVersion == QStringLiteral("1.21.6"), "世界版本解析不符");
    CHECK(world.packs.size() == 4, "应扫描到 4 个数据包");
    CHECK(world.builtinCount == 3, "内置/模组条目计数应为 2");

    const DataPackInfo *oldInfo = findPack(world, QStringLiteral("OldPack"));
    const DataPackInfo *newInfo = findPack(world, QStringLiteral("NewPack"));
    const DataPackInfo *brokenInfo = findPack(world, QStringLiteral("BrokenPack"));
    const DataPackInfo *zipInfo = findPack(world, QStringLiteral("ZipPack.zip"));
    CHECK(oldInfo && newInfo && brokenInfo && zipInfo, "数据包列表缺少条目");

    CHECK(oldInfo->isFile && oldInfo->enabled, "旧包应为文件型且默认启用");
    CHECK(oldInfo->metaOk && oldInfo->description == QString::fromUtf8("旧版本数据包"),
          "旧包描述解析不符");
    CHECK(oldInfo->hasPackFormat && oldInfo->packFormat == 48, "旧包 pack_format 解析不符");
    CHECK(oldInfo->compat == DataPackCompat::VersionMismatch, "旧包应判定为版本不匹配");
    CHECK(oldInfo->compatDetail.contains(QString::fromUtf8("偏旧")), "旧包兼容性说明应提示偏旧");
    CHECK(oldInfo->contentFileCount == 2, "旧包数据文件数应为 2");
    CHECK(oldInfo->contents.value(QString::fromUtf8("函数")).size() == 1
              && oldInfo->contents.value(QString::fromUtf8("进度")).size() == 1,
          "旧包内容分类不符");

    CHECK(newInfo->metaOk && newInfo->description == QString::fromUtf8("新包"), "新包描述解析不符");
    CHECK(newInfo->hasFormatRange && newInfo->minFormat == 80 && newInfo->maxFormat == 80,
          "新包 min/max_format 解析不符");
    CHECK(newInfo->compat == DataPackCompat::Compatible, "新包应判定为兼容");
    CHECK(newInfo->hasExperimental, "新包包含世界生成，应标记实验性");
    CHECK(newInfo->contents.contains(QString::fromUtf8("配方"))
              && newInfo->contents.contains(QString::fromUtf8("世界生成")),
          "新包内容分类缺少配方/世界生成");

    CHECK(!brokenInfo->metaOk && brokenInfo->compat == DataPackCompat::Invalid,
          "损坏包应判定为无效");

    CHECK(zipInfo->metaOk && zipInfo->hasPackFormat && zipInfo->packFormat == 80,
          "zip 包元数据解析失败");
    CHECK(zipInfo->compat == DataPackCompat::Compatible, "zip 包应判定为兼容");
    CHECK(zipInfo->contentFileCount == 2, "zip 包数据文件数应为 2");
    CHECK(zipInfo->contents.value(QString::fromUtf8("函数")).size() == 2,
          "zip 包函数数量不符");
    CHECK(zipInfo->contents.value(QString::fromUtf8("函数")).join(QLatin1Char('|'))
              .contains(QStringLiteral("zipdemo:main")),
          "zip 包函数名解析不符");

    // ---------- 5. 冲突检测 ----------
    CHECK(!oldInfo->conflictPaths.isEmpty(), "旧包与新包/zip 包同名，应检测到冲突");
    CHECK(!newInfo->conflictPaths.isEmpty(), "新包应检测到冲突");
    CHECK(oldInfo->conflictPaths.join(QLatin1Char('|')).contains(QStringLiteral("function/load")),
          "冲突路径应包含 minecraft:load 函数");

    // ---------- 6. 启停 + 排序写回 ----------
    // 期望顺序：NewPack、ZipPack 启用；OldPack、BrokenPack 禁用
    QList<DataPackInfo> ordered;
    for (const DataPackInfo &info : world.packs)
    {
        if (info.fileName == QLatin1String("NewPack"))
            ordered.append(info);
    }
    for (const DataPackInfo &info : world.packs)
    {
        if (info.fileName == QLatin1String("ZipPack.zip"))
            ordered.append(info);
    }
    for (const DataPackInfo &info : world.packs)
    {
        if (info.fileName == QLatin1String("OldPack"))
        {
            DataPackInfo copy = info;
            copy.enabled = false;
            ordered.append(copy);
        }
    }
    for (const DataPackInfo &info : world.packs)
    {
        if (info.fileName == QLatin1String("BrokenPack"))
        {
            DataPackInfo copy = info;
            copy.enabled = false;
            ordered.append(copy);
        }
    }
    CHECK(ordered.size() == 4, "构造排序列表失败");

    QString applyError;
    CHECK(DataPackManager::applyStates(worldDir, ordered, &applyError),
          qPrintable(QStringLiteral("applyStates 失败：%1").arg(applyError)));

    {
        const auto lists = LevelDatEditor::readDataPacks(worldDir);
        CHECK(lists.has_value(), "写回后读取 DataPacks 失败");
        const QStringList expectedEnabled{
            QStringLiteral("vanilla"),
            QStringLiteral("mod:testmod"),
            QStringLiteral("file/NewPack"),
            QStringLiteral("file/ZipPack.zip")};
        const QStringList expectedDisabled{
            QStringLiteral("mod:disabledpack"),
            QStringLiteral("file/OldPack"),
            QStringLiteral("file/BrokenPack")};
        CHECK(lists->enabled == expectedEnabled,
              qPrintable(QStringLiteral("Enabled 写回不符：%1").arg(lists->enabled.join(QLatin1Char(',')))));
        CHECK(lists->disabled == expectedDisabled,
              qPrintable(QStringLiteral("Disabled 写回不符：%1").arg(lists->disabled.join(QLatin1Char(',')))));
    }

    // 重新扫描：启用的按写回顺序在前
    world = DataPackManager::scanWorld(worldDir, QStringLiteral("1.21.6"));
    CHECK(world.ok, "重新扫描失败");
    CHECK(world.packs.size() == 4, "重新扫描数据包数量不符");
    CHECK(world.packs.at(0).fileName == QLatin1String("NewPack")
              && world.packs.at(1).fileName == QLatin1String("ZipPack.zip"),
          "启用包的加载顺序不符");
    CHECK(world.packs.at(0).enabled && world.packs.at(1).enabled, "前两个包应为启用");
    CHECK(!world.packs.at(2).enabled && !world.packs.at(3).enabled, "后两个包应为禁用");
    CHECK(world.builtinCount == 3, "重新扫描后内置条目计数不符");

    // 再调整顺序：OldPack 启用并排到最前
    {
        QList<DataPackInfo> reordered;
        for (const DataPackInfo &info : world.packs)
        {
            if (info.fileName == QLatin1String("OldPack"))
            {
                DataPackInfo copy = info;
                copy.enabled = true;
                reordered.append(copy);
            }
        }
        for (const DataPackInfo &info : world.packs)
        {
            if (info.fileName == QLatin1String("NewPack") || info.fileName == QLatin1String("ZipPack.zip"))
                reordered.append(info);
        }
        for (const DataPackInfo &info : world.packs)
        {
            if (info.fileName == QLatin1String("BrokenPack"))
            {
                DataPackInfo copy = info;
                copy.enabled = false;
                reordered.append(copy);
            }
        }
        CHECK(reordered.size() == 4, "构造第二次排序列表失败");
        CHECK(DataPackManager::applyStates(worldDir, reordered, &applyError),
              qPrintable(QStringLiteral("第二次 applyStates 失败：%1").arg(applyError)));
        const auto lists = LevelDatEditor::readDataPacks(worldDir);
        CHECK(lists.has_value(), "第二次写回后读取失败");
        CHECK(lists->enabled.size() == 5, "第二次写回 Enabled 数量不符");
        CHECK(lists->enabled.at(2) == QStringLiteral("file/OldPack"),
              "OldPack 应排在启用文件包最前");
        CHECK(lists->enabled.at(3) == QStringLiteral("file/NewPack")
                  && lists->enabled.at(4) == QStringLiteral("file/ZipPack.zip"),
              "启用文件包顺序不符");
    }

    // ---------- 7. 导入 / 删除 ----------
    const QString importSource = tempDir.path() + QStringLiteral("/Imported.zip");
    CHECK(writeFile(importSource, zipBytes), "准备导入文件失败");
    QString importedName;
    QString importError;
    CHECK(DataPackManager::importPack(worldDir, importSource, &importedName, &importError),
          qPrintable(QStringLiteral("importPack 失败：%1").arg(importError)));
    CHECK(!importedName.isEmpty(), "导入后文件名不应为空");
    CHECK(QFile::exists(dataPackDir + QLatin1Char('/') + importedName), "导入后的文件不存在");

    DataPackInfo importedInfo;
    importedInfo.isFile = true;
    importedInfo.exists = true;
    importedInfo.fileName = importedName;
    importedInfo.filePath = dataPackDir + QLatin1Char('/') + importedName;
    importedInfo.id = QStringLiteral("file/") + importedName;
    QString removeError;
    CHECK(DataPackManager::removePack(worldDir, importedInfo, &removeError),
          qPrintable(QStringLiteral("removePack 失败：%1").arg(removeError)));
    CHECK(!QFile::exists(importedInfo.filePath), "删除后文件仍存在");

    // 不允许删除 datapacks 之外的文件
    DataPackInfo outside;
    outside.isFile = true;
    outside.exists = true;
    outside.fileName = QStringLiteral("level.dat");
    outside.filePath = worldDir + QStringLiteral("/level.dat");
    CHECK(!DataPackManager::removePack(worldDir, outside, &removeError), "应拒绝删除 datapacks 之外的文件");

    // ---------- 8. 版本 → 格式映射 ----------
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.13")) == QStringLiteral("4"),
          "1.13 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.16.4")) == QStringLiteral("6"),
          "1.16.4 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.20.1")) == QStringLiteral("15"),
          "1.20.1 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.21.1")) == QStringLiteral("48"),
          "1.21.1 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.21.6")) == QStringLiteral("80"),
          "1.21.6 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.21.8")) == QStringLiteral("81"),
          "1.21.8 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.21.9")) == QStringLiteral("88.0"),
          "1.21.9 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.21.11")) == QStringLiteral("94.1"),
          "1.21.11 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("26.1.2")) == QStringLiteral("101.1"),
          "26.1.2 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("26.3")) == QStringLiteral("121.0"),
          "26.3 格式映射不符");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("26w14a")).isEmpty(),
          "快照版本不应猜格式");
    CHECK(DataPackManager::packFormatForVersion(QStringLiteral("1.21.9-pre1")).isEmpty(),
          "预发布版本不应猜格式");

    CHECK(DataPackManager::versionRangeForFormat(48) == QString::fromUtf8("1.21–1.21.1"),
          "格式 48 的版本区间不符");
    CHECK(DataPackManager::versionRangeForFormat(80) == QStringLiteral("1.21.6"),
          "格式 80 的版本区间不符");
    CHECK(DataPackManager::formatDisplayName(4) == QStringLiteral("4"), "格式显示不符");
    CHECK(DataPackManager::formatDisplayName(88, 0) == QStringLiteral("88.0"), "格式 88.0 显示不符");

    // ---------- 9. 分类名 ----------
    CHECK(DataPackManager::categoryDisplayName(QStringLiteral("function")) == QString::fromUtf8("函数"),
          "function 分类名不符");
    CHECK(DataPackManager::categoryDisplayName(QStringLiteral("loot_table")) == QString::fromUtf8("战利品表"),
          "loot_table 分类名不符");
    CHECK(!DataPackManager::categoryOrder().isEmpty(), "分类顺序为空");

    std::printf("ALL PASS\n");
    return 0;
}

