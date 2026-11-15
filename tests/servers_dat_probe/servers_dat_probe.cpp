/**
 * @file   servers_dat_probe.cpp
 * @brief  ServersDatIO 行为探针：解析地址 / 读写 / 合并去重
 */

#include "utils/ServersDatIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const char *name)
{
    if (ok)
    {
        ++g_pass;
        QTextStream(stdout) << "[PASS] " << name << "\n";
    }
    else
    {
        ++g_fail;
        QTextStream(stdout) << "[FAIL] " << name << "\n";
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // ── 地址解析 ──
    QString host;
    quint16 port = 0;
    check(ServersDatIO::parseAddress(QStringLiteral("play.example.com"), host, port)
              && host == QStringLiteral("play.example.com") && port == 25565,
          "parse host without port");
    check(ServersDatIO::parseAddress(QStringLiteral("192.168.1.5:19132"), host, port)
              && host == QStringLiteral("192.168.1.5") && port == 19132,
          "parse host:port");
    check(ServersDatIO::parseAddress(QStringLiteral("[::1]:25566"), host, port)
              && host == QStringLiteral("::1") && port == 25566,
          "parse [ipv6]:port");
    check(ServersDatIO::addressKey(QStringLiteral("LocalHost"), 25565)
              == ServersDatIO::addressKey(QStringLiteral("localhost"), 25565),
          "addressKey default port");
    check(ServersDatIO::formatAddress(QStringLiteral("::1"), 25565) == QStringLiteral("::1")
              || ServersDatIO::formatAddress(QStringLiteral("::1"), 25565)
                     == QStringLiteral("[::1]:25565"),
          "formatAddress ipv6");

    // ── 读写 roundtrip ──
    QTemporaryDir tmp;
    check(tmp.isValid(), "temp dir");
    const QString path = tmp.filePath(QStringLiteral("servers.dat"));

    ServersDatIO::Entry e1;
    e1.name = QStringLiteral("本地服");
    e1.host = QStringLiteral("127.0.0.1");
    e1.port = 25565;
    check(ServersDatIO::upsert(path, e1), "upsert create file");

    ServersDatIO::Entry e2;
    e2.name = QStringLiteral("局域网");
    e2.host = QStringLiteral("192.168.1.10");
    e2.port = 25566;
    check(ServersDatIO::upsert(path, e2), "upsert second");

    auto list = ServersDatIO::read(path);
    check(list.size() == 2, "read size 2");
    check(list.size() >= 2 && list[0].name == QStringLiteral("本地服")
              && list[0].host == QStringLiteral("127.0.0.1"),
          "read first entry");

    // 更新名称
    e1.name = QStringLiteral("本地服改名");
    check(ServersDatIO::upsert(path, e1), "upsert update");
    list = ServersDatIO::read(path);
    bool renamed = false;
    for (const auto &item : list)
    {
        if (item.host == e1.host && item.port == e1.port
            && item.name == QStringLiteral("本地服改名"))
            renamed = true;
    }
    check(renamed && list.size() == 2, "update preserves count");

    // 删除
    check(ServersDatIO::remove(path, e2.host, e2.port), "remove");
    list = ServersDatIO::read(path);
    check(list.size() == 1, "read after remove");

    // 不存在文件的 remove 返回 true
    check(ServersDatIO::remove(tmp.filePath(QStringLiteral("missing.dat")),
                               QStringLiteral("x"), 25565),
          "remove missing file");

    // 坏文件
    QFile bad(tmp.filePath(QStringLiteral("bad.dat")));
    bad.open(QIODevice::WriteOnly);
    bad.write("not-gzip");
    bad.close();
    check(ServersDatIO::read(tmp.filePath(QStringLiteral("bad.dat"))).isEmpty(),
          "read bad file empty");

    // ── 未压缩 NBT（旧版本 / 第三方工具可能这么写）──
    {
        QByteArray raw;
        auto putByte = [&raw](quint8 v) { raw.append(static_cast<char>(v)); };
        auto putStr = [&raw, &putByte](const QByteArray &s) {
            putByte(static_cast<quint8>((s.size() >> 8) & 0xFF));
            putByte(static_cast<quint8>(s.size() & 0xFF));
            raw.append(s);
        };
        putByte(0x0A);                       // root: Compound
        putStr(QByteArray());                // 根标签无名字
        putByte(0x09);                       // servers: List
        putStr("servers");
        putByte(0x0A);                       // 元素类型 Compound
        putByte(0x00); putByte(0x00); putByte(0x00); putByte(0x01);   // 1 项
        putByte(0x08); putStr("name"); putStr("裸NBT服");
        putByte(0x08); putStr("ip"); putStr("plain.example.com:25566");
        putByte(0x00);                       // 元素 Compound 结束
        putByte(0x00);                       // 根 Compound 结束

        const QString plainPath = tmp.filePath(QStringLiteral("plain.dat"));
        QFile plain(plainPath);
        plain.open(QIODevice::WriteOnly);
        plain.write(raw);
        plain.close();

        const QList<ServersDatIO::Entry> plainEntries = ServersDatIO::read(plainPath);
        check(plainEntries.size() == 1, "read uncompressed nbt count");
        check(plainEntries.size() == 1
                  && plainEntries[0].host == QStringLiteral("plain.example.com")
                  && plainEntries[0].port == 25566
                  && plainEntries[0].name == QStringLiteral("裸NBT服"),
              "read uncompressed nbt content");
    }

    // ── 扫描位置：版本目录 + 共享 .minecraft 目录 ──
    {
        const QString mcRoot = tmp.filePath(QStringLiteral("mc"));
        const QString versionDir = mcRoot + QStringLiteral("/versions/1.20.1");
        QDir().mkpath(versionDir);

        const QStringList candidates = ServersDatIO::candidatePaths(versionDir);
        check(candidates.size() == 2, "candidate paths count");
        check(candidates.size() == 2
                  && QDir::cleanPath(candidates[0])
                         == QDir::cleanPath(versionDir + QStringLiteral("/servers.dat"))
                  && QDir::cleanPath(candidates[1])
                         == QDir::cleanPath(mcRoot + QStringLiteral("/servers.dat")),
              "candidate paths cover version + shared dir");

        // 只有共享目录有 servers.dat（关闭版本隔离的游戏写在这里）
        check(ServersDatIO::existingPath(candidates).isEmpty(),
              "existing path empty when no file");

        const QString sharedDat = mcRoot + QStringLiteral("/servers.dat");
        ServersDatIO::Entry sharedEntry;
        sharedEntry.name = QStringLiteral("共享目录服");
        sharedEntry.host = QStringLiteral("shared.example.com");
        sharedEntry.port = 25565;
        check(ServersDatIO::upsert(sharedDat, sharedEntry), "upsert shared dat");

        check(QDir::cleanPath(ServersDatIO::existingPath(candidates))
                  == QDir::cleanPath(sharedDat),
              "existing path picks shared dat");

        // 版本目录也有一份时读两处都能拿到各自内容
        const QString versionDat = versionDir + QStringLiteral("/servers.dat");
        ServersDatIO::Entry versionEntry;
        versionEntry.name = QStringLiteral("版本目录服");
        versionEntry.host = QStringLiteral("version.example.com");
        versionEntry.port = 25565;
        check(ServersDatIO::upsert(versionDat, versionEntry), "upsert version dat");

        int totalFromBoth = 0;
        for (const QString &path : candidates)
            totalFromBoth += ServersDatIO::read(path).size();
        check(totalFromBoth == 2, "read merges both candidate dirs");
    }

    QTextStream(stdout) << "\n==== result: " << g_pass << " passed, " << g_fail
                         << " failed ====\n";
    return g_fail == 0 ? 0 : 1;
}
