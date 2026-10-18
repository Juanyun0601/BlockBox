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

    QTextStream(stdout) << "\n==== result: " << g_pass << " passed, " << g_fail
                         << " failed ====\n";
    return g_fail == 0 ? 0 : 1;
}
