/**
 * @file   leveldat_gamerules_probe.cpp
 * @brief  LevelDatEditor 游戏规则（Data.GameRules）读写探针
 *
 * 手工构造一个最小 level.dat（NBT + gzip，初始无 GameRules 复合标签），验证：
 *  1. readSave 能解析基本字段且 gameRules 为空
 *  2. applyChanges 写入一条规则后，GameRules 复合标签被自动创建，规则可读回
 *  3. 再次写入时已有规则可更新、可新增，且其它字段与未知规则原样保留
 *  4. level.dat_old 备份生成
 * 输出：全部通过返回 0，任一步失败返回 1 并打印原因。
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

#include "utils/Saves/LevelDatEditor.h"

// ---------- NBT 手工构造辅助（大端） ----------
static void putU16(QByteArray &out, quint16 v)
{
    out.append(char(v >> 8));
    out.append(char(v & 0xFF));
}
static void putString(QByteArray &out, const QByteArray &s)
{
    putU16(out, quint16(s.size()));
    out.append(s);
}
static void putU32(QByteArray &out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

// 根 Compound { Data { LevelName, GameType, allowCommands, Difficulty } }，无 GameRules
static QByteArray buildMinimalNbt()
{
    QByteArray data;
    // LevelName（TAG_String）
    data.append(char(8));
    putString(data, "LevelName");
    putString(data, QByteArrayLiteral("\xE6\xB5\x8B\xE8\xAF\x95\xE4\xB8\x96\xE7\x95\x8C")); // 测试世界
    // GameType（TAG_Int）
    data.append(char(3));
    putString(data, "GameType");
    putU32(data, 1);
    // allowCommands（TAG_Byte）
    data.append(char(1));
    putString(data, "allowCommands");
    data.append(char(0));
    // Difficulty（TAG_Byte）
    data.append(char(1));
    putString(data, "Difficulty");
    data.append(char(2));
    data.append(char(0)); // Data 结束

    QByteArray root;
    root.append(char(10));
    putString(root, "");            // 根标签名
    root.append(char(10));          // TAG_Compound
    putString(root, "Data");
    root.append(data);
    root.append(char(0));           // 根结束
    return root;
}

static QByteArray gzip(const QByteArray &raw)
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
        out.append(buf, sizeof(buf) - strm.avail_out);
    } while (ret == Z_OK || (ret != Z_STREAM_END && strm.avail_out == 0));
    deflateEnd(&strm);
    return (ret == Z_STREAM_END) ? out : QByteArray();
}

static bool writeLevelDat(const QString &path, const QByteArray &raw)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray gz = gzip(raw);
    const bool ok = !gz.isEmpty() && f.write(gz) == gz.size();
    f.close();
    return ok;
}

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir dir;
    CHECK(dir.isValid(), "临时目录创建失败");
    const QString save = dir.path() + QStringLiteral("/TestWorld");
    CHECK(QDir().mkpath(save), "存档目录创建失败");
    CHECK(writeLevelDat(save + QStringLiteral("/level.dat"), buildMinimalNbt()),
          "level.dat 写入失败");

    // 1. 初始读取：基本字段正常，规则为空
    auto info = LevelDatEditor::readSave(save);
    CHECK(info.has_value(), "readSave 解析失败");
    CHECK(info->levelName == QString::fromUtf8("\xE6\xB5\x8B\xE8\xAF\x95\xE4\xB8\x96\xE7\x95\x8C"),
          "LevelName 解析不符");
    CHECK(info->gameType == 1, "GameType 解析不符");
    CHECK(info->gameRules.isEmpty(), "初始 gameRules 应为空");

    // 2. 写入一条规则（GameRules 复合标签原本不存在，应被自动创建）
    SaveChanges c1;
    c1.gameRules = QMap<QString, QString>{ { QStringLiteral("keepInventory"), QStringLiteral("true") } };
    QString err;
    CHECK(LevelDatEditor::applyChanges(save, c1, &err),
          qPrintable(QStringLiteral("首次写入规则失败：%1").arg(err)));

    info = LevelDatEditor::readSave(save);
    CHECK(info.has_value(), "首次写入后 readSave 失败");
    CHECK(info->gameRules.value(QStringLiteral("keepInventory")) == QStringLiteral("true"),
          "keepInventory 写入后读回不符");
    CHECK(info->levelName == QString::fromUtf8("\xE6\xB5\x8B\xE8\xAF\x95\xE4\xB8\x96\xE7\x95\x8C"),
          "写规则后 LevelName 丢失");
    CHECK(info->gameType == 1 && !info->allowCommands && info->difficulty == 2,
          "写规则后原有字段被破坏");
    CHECK(QFile::exists(save + QStringLiteral("/level.dat_old")), "level.dat_old 备份未生成");

    // 3. 更新已有规则 + 新增一条数值规则
    SaveChanges c2;
    c2.gameRules = QMap<QString, QString>{ { QStringLiteral("keepInventory"), QStringLiteral("false") },
                                           { QStringLiteral("randomTickSpeed"), QStringLiteral("7") } };
    CHECK(LevelDatEditor::applyChanges(save, c2, &err),
          qPrintable(QStringLiteral("二次写入规则失败：%1").arg(err)));
    info = LevelDatEditor::readSave(save);
    CHECK(info.has_value(), "二次写入后 readSave 失败");
    CHECK(info->gameRules.value(QStringLiteral("keepInventory")) == QStringLiteral("false"),
          "keepInventory 更新失败");
    CHECK(info->gameRules.value(QStringLiteral("randomTickSpeed")) == QStringLiteral("7"),
          "randomTickSpeed 新增失败");
    CHECK(info->gameRules.size() == 2, "规则数量不符（可能丢失或多余）");

    // 4. 不传规则时不应触碰 GameRules
    SaveChanges c3;
    c3.difficulty = 3;
    CHECK(LevelDatEditor::applyChanges(save, c3, &err), "difficulty 写入失败");
    info = LevelDatEditor::readSave(save);
    CHECK(info->difficulty == 3, "difficulty 读回不符");
    CHECK(info->gameRules.size() == 2
              && info->gameRules.value(QStringLiteral("randomTickSpeed")) == QStringLiteral("7"),
          "difficulty 写入误改了游戏规则");

    std::printf("ALL PASS\n");
    return 0;
}
