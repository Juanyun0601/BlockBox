/**
 * @file   LegacyForgeInstaller.cpp
 * @brief  老版本Forge（MC 1.5.2 及更早）的下载/合并逻辑 — 移植自 MultiMC
 *         来源（Apache License 2.0）：
 *         - api/logic/minecraft/legacy/LegacyUpdate.cpp     FML 依赖库下载
 *         - api/logic/minecraft/VersionFilterData.cpp       fmlLibsMapping 数据
 *         - api/logic/MMCZip.cpp                            createModdedJar jar 合并
 *         - api/logic/net/URLConstants.h                    FMLLIBS 基础地址
 *         jar 读写用 zlib 手写（raw deflate），与项目 RuntimeArchive 的做法一致，
 *         不依赖 Qt 私有头，桌面/安卓/鸿蒙均可编译。
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include "LegacyForgeInstaller.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QVector>

#include <tuple>
#include <utility>

#include <zlib.h>

namespace legacyforge {

namespace {

// ───────────────────────────── ZIP 读取（中央目录） ─────────────────────────────

struct ZipEntry
{
    QString name;
    quint16 method = 0;      // 0=stored 8=deflate
    quint32 crc32 = 0;
    quint32 compSize = 0;
    quint32 uncompSize = 0;
    quint32 localOffset = 0;
};

template <typename T>
T readLE(const QByteArray &data, qint64 offset)
{
    if (offset + static_cast<qint64>(sizeof(T)) > data.size())
        return 0;
    T value = 0;
    memcpy(&value, data.constData() + offset, sizeof(T));
    return value; // x86/ARM 小端平台直接 memcpy 即可
}

QByteArray inflateRaw(const QByteArray &compressed, quint32 expectedSize)
{
    if (expectedSize == 0)
        return QByteArray();
    QByteArray out(int(expectedSize), Qt::Uninitialized);
    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    zs.avail_in = uInt(compressed.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    // ZIP 里是 raw deflate（无 zlib 头），windowBits 取负值
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return QByteArray();
    const int ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (ret != Z_STREAM_END)
        return QByteArray();
    return out;
}

bool readZipEntries(const QString &zipPath, QVector<ZipEntry> *entries, QString *errorOut)
{
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorOut)
            *errorOut = QObject::tr("无法打开 %1").arg(QFileInfo(zipPath).fileName());
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    if (data.size() < 22)
    {
        if (errorOut)
            *errorOut = QObject::tr("%1 不是有效的压缩包").arg(QFileInfo(zipPath).fileName());
        return false;
    }

    // 从尾部向前找 EOCD（0x06054b50），注释区最长 64K
    qint64 eocdPos = -1;
    const qint64 scanStart = qMax<qint64>(0, data.size() - 65557);
    for (qint64 i = data.size() - 22; i >= scanStart; --i)
    {
        if (readLE<quint32>(data, i) == 0x06054b50u)
        {
            eocdPos = i;
            break;
        }
    }
    if (eocdPos < 0)
    {
        if (errorOut)
            *errorOut = QObject::tr("%1 缺少中央目录").arg(QFileInfo(zipPath).fileName());
        return false;
    }

    const quint16 count = readLE<quint16>(data, eocdPos + 10);
    const qint64 cdOffset = readLE<quint32>(data, eocdPos + 16);

    qint64 pos = cdOffset;
    entries->reserve(count);
    for (quint16 i = 0; i < count; ++i)
    {
        if (pos + 46 > data.size() || readLE<quint32>(data, pos) != 0x02014b50u)
        {
            if (errorOut)
                *errorOut = QObject::tr("%1 中央目录损坏").arg(QFileInfo(zipPath).fileName());
            return false;
        }
        ZipEntry entry;
        entry.method = readLE<quint16>(data, pos + 10);
        entry.crc32 = readLE<quint32>(data, pos + 16);
        entry.compSize = readLE<quint32>(data, pos + 20);
        entry.uncompSize = readLE<quint32>(data, pos + 24);
        const quint16 nameLen = readLE<quint16>(data, pos + 28);
        const quint16 extraLen = readLE<quint16>(data, pos + 30);
        const quint16 commentLen = readLE<quint16>(data, pos + 32);
        entry.localOffset = readLE<quint32>(data, pos + 42);
        entry.name = QString::fromUtf8(data.mid(pos + 46, nameLen));
        entries->append(entry);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

QByteArray readEntryData(const QByteArray &fileData, const ZipEntry &entry)
{
    const qint64 local = entry.localOffset;
    if (local + 30 > fileData.size() || readLE<quint32>(fileData, local) != 0x04034b50u)
        return QByteArray();
    const quint16 nameLen = readLE<quint16>(fileData, local + 26);
    const quint16 extraLen = readLE<quint16>(fileData, local + 28);
    const qint64 dataStart = local + 30 + nameLen + extraLen;
    if (dataStart + entry.compSize > fileData.size())
        return QByteArray();

    const QByteArray compressed = fileData.mid(dataStart, int(entry.compSize));
    if (entry.method == 0)
        return compressed;
    if (entry.method == 8)
        return inflateRaw(compressed, entry.uncompSize);
    return QByteArray();
}

// ───────────────────────────── ZIP 写入（raw deflate） ─────────────────────────────

QByteArray deflateRaw(const QByteArray &data, bool *ok)
{
    *ok = false;
    uLongf bound = compressBound(uLong(data.size()));
    QByteArray out(int(bound), Qt::Uninitialized);
    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    zs.avail_in = uInt(data.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return QByteArray();
    const int ret = deflate(&zs, Z_FINISH);
    const bool finished = (ret == Z_STREAM_END);
    const uLong written = zs.total_out;
    deflateEnd(&zs);
    if (!finished)
        return QByteArray();
    *ok = true;
    out.resize(int(written));
    return out;
}

void appendLE(QByteArray *out, quint16 value) { out->append(reinterpret_cast<const char *>(&value), 2); }
void appendLE(QByteArray *out, quint32 value) { out->append(reinterpret_cast<const char *>(&value), 4); }

class ZipWriter
{
public:
    ZipWriter()
    {
        m_time = 0;                // DOS 时间：00:00:00
        m_date = (1 << 5) | 1;     // DOS 日期：1980-01-01（合法最小日期，Java 不校验具体值）
    }

    void addEntry(const QString &name, const QByteArray &data)
    {
        const bool isDir = name.endsWith(QLatin1Char('/'));
        const quint32 crc = crc32(0L, Z_NULL, 0);
        const quint32 crcValue = crc32(crc,
            reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size()));

        const QByteArray nameBytes = name.toUtf8();
        // 名字含非 ASCII 时置 UTF-8 位（bit 11），与 quazip 行为一致
        const quint16 flags = (name.toLatin1() == name) ? quint16(0) : quint16(0x0800);

        QByteArray compressed;
        quint16 method = 0;
        if (!isDir && data.size() > 0)
        {
            bool ok = false;
            compressed = deflateRaw(data, &ok);
            if (ok && compressed.size() < data.size())
                method = 8;
            else
                compressed = data; // 压不动就原样存
        }

        const quint32 localOffset = quint32(m_local.size());
        appendLE(&m_local, 0x04034b50u);   // 签名
        appendLE(&m_local, quint16(20));   // 版本
        appendLE(&m_local, flags);
        appendLE(&m_local, method);
        appendLE(&m_local, m_time);
        appendLE(&m_local, m_date);
        appendLE(&m_local, crcValue);
        appendLE(&m_local, quint32(compressed.size()));
        appendLE(&m_local, quint32(data.size()));
        appendLE(&m_local, quint16(nameBytes.size()));
        appendLE(&m_local, quint16(0));    // extra 长度
        m_local.append(nameBytes);
        m_local.append(compressed);

        appendLE(&m_central, 0x02014b50u);
        appendLE(&m_central, quint16(20)); // 制作版本
        appendLE(&m_central, quint16(20)); // 需要版本
        appendLE(&m_central, flags);
        appendLE(&m_central, method);
        appendLE(&m_central, m_time);
        appendLE(&m_central, m_date);
        appendLE(&m_central, crcValue);
        appendLE(&m_central, quint32(compressed.size()));
        appendLE(&m_central, quint32(data.size()));
        appendLE(&m_central, quint16(nameBytes.size()));
        appendLE(&m_central, quint16(0));  // extra
        appendLE(&m_central, quint16(0));  // comment
        appendLE(&m_central, quint16(0));  // 起始盘号
        appendLE(&m_central, quint16(0));  // 内部属性
        appendLE(&m_central, quint32(0));  // 外部属性
        appendLE(&m_central, localOffset);
        m_central.append(nameBytes);
        ++m_count;
    }

    bool finalize(const QString &outPath)
    {
        QByteArray out;
        out.reserve(m_local.size() + m_central.size() + 22);
        out.append(m_local);
        out.append(m_central);
        appendLE(&out, 0x06054b50u);       // EOCD
        appendLE(&out, quint16(0));
        appendLE(&out, quint16(0));
        appendLE(&out, quint16(m_count));
        appendLE(&out, quint16(m_count));
        appendLE(&out, quint32(m_central.size()));
        appendLE(&out, quint32(m_local.size()));
        appendLE(&out, quint16(0));        // 注释长度
        QFile file(outPath);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(out) == out.size();
    }

private:
    QByteArray m_local;
    QByteArray m_central;
    quint16 m_count = 0;
    quint16 m_time;
    quint16 m_date;
};

// ───────────────────────────── FML 库清单（MultiMC VersionFilterData） ─────────────────────────────

// MultiMC 源码中 ours=false 表示 Forge 官方源也有，true 表示只有 MultiMC 镜像可靠。
// files.minecraftforge.net/fmllibs 现已下线，全部以 files.multimc.org 为主源。
QList<FmlLib> libsForRange(const QString &mcVersion)
{
    QList<FmlLib> libs;
    auto add = [&libs](const char *name, const char *sha1) {
        FmlLib lib;
        lib.filename = QLatin1String(name);
        lib.sha1 = QLatin1String(sha1);
        lib.multimcHosted = false;
        libs.append(lib);
    };
    auto addMultiMcOnly = [&libs](const char *name, const char *sha1) {
        FmlLib lib;
        lib.filename = QLatin1String(name);
        lib.sha1 = QLatin1String(sha1);
        lib.multimcHosted = true;
        libs.append(lib);
    };

    // 1.3.*
    if (mcVersion == QLatin1String("1.3.2"))
    {
        add("argo-2.25.jar", "bb672829fde76cb163004752b86b0484bd0a7f4b");
        add("guava-12.0.1.jar", "b8e78b9af7bf45900e14c6f958486b6ca682195f");
        add("asm-all-4.0.jar", "98308890597acb64047f7e896638e0d98753ae82");
        return libs;
    }

    // 1.4.*
    if (mcVersion == QLatin1String("1.4") || mcVersion == QLatin1String("1.4.1") ||
        mcVersion == QLatin1String("1.4.2") || mcVersion == QLatin1String("1.4.3") ||
        mcVersion == QLatin1String("1.4.4") || mcVersion == QLatin1String("1.4.5") ||
        mcVersion == QLatin1String("1.4.6") || mcVersion == QLatin1String("1.4.7"))
    {
        add("argo-2.25.jar", "bb672829fde76cb163004752b86b0484bd0a7f4b");
        add("guava-12.0.1.jar", "b8e78b9af7bf45900e14c6f958486b6ca682195f");
        add("asm-all-4.0.jar", "98308890597acb64047f7e896638e0d98753ae82");
        add("bcprov-jdk15on-147.jar", "b6f5d9926b0afbde9f4dbe3db88c5247be7794bb");
        return libs;
    }

    // 1.5 / 1.5.1：只有 deobfuscation_data 文件名不同，其余共用
    if (mcVersion == QLatin1String("1.5") || mcVersion == QLatin1String("1.5.1"))
    {
        add("argo-small-3.2.jar", "58912ea2858d168c50781f956fa5b59f0f7c6b51");
        add("guava-14.0-rc3.jar", "931ae21fa8014c3ce686aaa621eae565fefb1a6a");
        add("asm-all-4.1.jar", "054986e962b88d8660ae4566475658469595ef58");
        addMultiMcOnly("bcprov-jdk15on-148.jar", "960dea7c9181ba0b17e8bab0c06a43f0a5f04e65");
        add(mcVersion == QLatin1String("1.5") ? "deobfuscation_data_1.5.zip"
                                              : "deobfuscation_data_1.5.1.zip",
            mcVersion == QLatin1String("1.5") ? "5f7c142d53776f16304c0bbe10542014abad6af8"
                                              : "22e221a0d89516c1f721d6cab056a7e37471d0a6");
        addMultiMcOnly("scala-library.jar", "458d046151ad179c85429ed7420ffb1eaf6ddf85");
        return libs;
    }

    // 1.5.2
    if (mcVersion == QLatin1String("1.5.2"))
    {
        add("argo-small-3.2.jar", "58912ea2858d168c50781f956fa5b59f0f7c6b51");
        add("guava-14.0-rc3.jar", "931ae21fa8014c3ce686aaa621eae565fefb1a6a");
        add("asm-all-4.1.jar", "054986e962b88d8660ae4566475658469595ef58");
        addMultiMcOnly("bcprov-jdk15on-148.jar", "960dea7c9181ba0b17e8bab0c06a43f0a5f04e65");
        add("deobfuscation_data_1.5.2.zip", "446e55cd986582c70fcf12cb27bc00114c5adfd9");
        addMultiMcOnly("scala-library.jar", "458d046151ad179c85429ed7420ffb1eaf6ddf85");
        return libs;
    }

    return libs;
}

} // namespace

// ───────────────────────────── 公开 API ─────────────────────────────

bool isLegacyMcVersion(const QString &mcVersion)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
    const auto match = pattern.match(mcVersion.trimmed());
    if (!match.hasMatch())
        return false;
    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    const int patch = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
    // 1.5.2 及更早（MultiMC：forgeInstallerBlacklist 含 1.5.2，1.6.1 起才有可用的安装器流程）
    return std::tie(major, minor, patch) < std::make_tuple(1, 5, 3);
}

QList<FmlLib> fmlLibsFor(const QString &mcVersion)
{
    return libsForRange(mcVersion.trimmed());
}

QStringList forgeZipCandidateUrls(const QString &mcVersion, const QString &forgeVersion,
                                  bool preferBmcl)
{
    const QString full = mcVersion + QLatin1Char('-') + forgeVersion;
    auto mavenUrls = [&](const QString &category) {
        const QString path = QStringLiteral("net/minecraftforge/forge/%1/forge-%1-%2.zip").arg(full, category);
        return QStringList{
            QStringLiteral("https://maven.minecraftforge.net/") + path,
            QStringLiteral("https://bmclapi2.bangbang93.com/maven/") + path,
        };
    };
    auto bmclApiUrl = [&](const QString &category) {
        return QStringLiteral("https://bmclapi2.bangbang93.com/forge/download?mcversion=%1&version=%2&category=%3&format=zip")
            .arg(mcVersion, forgeVersion, category);
    };

    // 1.3+ 的包叫 universal，1.2.x 及更早叫 client，依次尝试
    QStringList urls;
    urls << mavenUrls(QStringLiteral("universal"))
         << bmclApiUrl(QStringLiteral("universal"))
         << mavenUrls(QStringLiteral("client"))
         << bmclApiUrl(QStringLiteral("client"));

    if (!preferBmcl)
        return urls;

    // 首选镜像：把 BMCL 地址整体挪到前面，保持各自相对顺序
    QStringList reordered;
    for (const QString &url : urls)
        if (url.contains(QLatin1String("bmclapi2")))
            reordered << url;
    for (const QString &url : urls)
        if (!url.contains(QLatin1String("bmclapi2")))
            reordered << url;
    return reordered;
}

QStringList fmlLibCandidateUrls(const FmlLib &lib)
{
    // MultiMC 自建镜像（files.multimc.org/fmllibs，URLConstants::FMLLIBS_OUR_BASE_URL）
    // 至今可用；Forge 官方 /fmllibs 目录已下线，仅作为最后的回退
    QStringList urls;
    urls << QStringLiteral("https://files.multimc.org/fmllibs/") + lib.filename;
    if (!lib.multimcHosted)
        urls << QStringLiteral("http://files.minecraftforge.net/fmllibs/") + lib.filename;
    return urls;
}

bool mergeForgeIntoJar(const QString &forgeZipPath, const QString &vanillaJarPath,
                       const QString &outJarPath, QString *errorOut)
{
    QVector<ZipEntry> forgeEntries;
    if (!readZipEntries(forgeZipPath, &forgeEntries, errorOut))
        return false;
    QVector<ZipEntry> vanillaEntries;
    if (!readZipEntries(vanillaJarPath, &vanillaEntries, errorOut))
        return false;

    QFile forgeFile(forgeZipPath);
    if (!forgeFile.open(QIODevice::ReadOnly))
        return false;
    const QByteArray forgeData = forgeFile.readAll();
    forgeFile.close();
    QFile vanillaFile(vanillaJarPath);
    if (!vanillaFile.open(QIODevice::ReadOnly))
        return false;
    const QByteArray vanillaData = vanillaFile.readAll();
    vanillaFile.close();

    ZipWriter writer;

    // MultiMMC MMCZip::createModdedJar 的顺序：先写 Forge 包（与原版重名的文件
    // 以 Forge 为准），再并入原版 jar 的剩余条目，并过滤原版的 META-INF 签名
    QSet<QString> added;
    for (const ZipEntry &entry : std::as_const(forgeEntries))
    {
        if (added.contains(entry.name) || entry.name.isEmpty())
            continue;
        added.insert(entry.name);
        const QByteArray content = readEntryData(forgeData, entry);
        if (entry.uncompSize > 0 && content.size() != int(entry.uncompSize))
            continue; // 条目读不出来时跳过，避免写坏整个 jar
        writer.addEntry(entry.name, content);
    }
    for (const ZipEntry &entry : std::as_const(vanillaEntries))
    {
        if (added.contains(entry.name) || entry.name.isEmpty())
            continue;
        if (entry.name.contains(QLatin1String("META-INF")))
            continue; // 去除 Mojang 签名
        added.insert(entry.name);
        const QByteArray content = readEntryData(vanillaData, entry);
        if (entry.uncompSize > 0 && content.size() != int(entry.uncompSize))
            continue;
        writer.addEntry(entry.name, content);
    }

    if (!writer.finalize(outJarPath))
    {
        if (errorOut)
            *errorOut = QObject::tr("无法写入合并后的 jar：%1").arg(outJarPath);
        return false;
    }
    return true;
}

QJsonObject buildLegacyVersionJson(const QString &mcVersion, const QString &forgeVersion,
                                   const QList<FmlLib> &fmlLibs)
{
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("%1-forge-%2").arg(mcVersion, forgeVersion);
    json[QStringLiteral("inheritsFrom")] = mcVersion;
    json[QStringLiteral("type")] = QStringLiteral("release");
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    json[QStringLiteral("releaseTime")] = now;
    json[QStringLiteral("time")] = now;
    // mainClass / minecraftArguments / 资源索引都继承原版 JSON（mergeInheritsFromJson 补齐）

    QJsonArray libraries;
    for (const FmlLib &lib : fmlLibs)
    {
        QJsonObject libObj;
        libObj[QStringLiteral("name")] = QStringLiteral("fmllibs:%1:%2")
                                             .arg(QFileInfo(lib.filename).completeBaseName(), mcVersion);
        QJsonObject artifact;
        artifact[QStringLiteral("path")] = QStringLiteral("fmllibs/") + lib.filename;
        if (!lib.sha1.isEmpty())
            artifact[QStringLiteral("sha1")] = lib.sha1;
        QJsonObject downloads;
        downloads[QStringLiteral("artifact")] = artifact;
        libObj[QStringLiteral("downloads")] = downloads;
        libraries.append(libObj);
    }
    json[QStringLiteral("libraries")] = libraries;
    return json;
}

} // namespace legacyforge
