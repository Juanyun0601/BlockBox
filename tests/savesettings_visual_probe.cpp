/**
 * @file   savesettings_visual_probe.cpp
 * @brief  SaveSettingsPage（存档设置页）视觉探针：游戏规则卡片
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），加载真实 style.qss，
 * 合成一个含 GameRules 的 level.dat，渲染真实 SaveSettingsPage：
 *  - 游戏规则卡片可见，按玩家/世界/生物/界面与命令分组
 *  - 文件中的规则值正确映射到控件（keepInventory 勾选、randomTickSpeed=7）
 *  - 搜索框过滤规则（输入 "keep" 后其余复选框隐藏）
 *  - 「恢复默认值」把全部规则还原为默认（keepInventory 取消勾选）
 *  - 全页截图 2 个宽度 + 过滤态 + 复位态，输出到 D:/BlockBox/_out/
 * 任一断言失败返回 1。
 */
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QScrollArea>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

#include "pages/SaveSettingsPage.h"

// ── 与 world_map_page_probe 同族的 token 表（探针精简版） ──
static QString resolveTokens(QString style)
{
    struct KV { const char *k; const char *v; };
    const QVector<KV> t = {
        {"@BG_BASE@", "#f5f5f5"}, {"@BG_CARD@", "#ffffff"},
        {"@BG_CONTENT@", "#fafafa"}, {"@BG_MUTED@", "#f8f9fa"},
        {"@BG_HOVER@", "#eaeaea"}, {"@BG_PRESSED@", "#e0e0e0"},
        {"@BG_TRACK@", "#e8e8e8"}, {"@BG_TAB@", "#f0f0f0"},
        {"@BG_DISABLED@", "#d5d7db"}, {"@BG_MSG@", "#edf0f4"},
        {"@BG_GLASS@", "rgba(255, 255, 255, 0.72)"},
        {"@BG_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.42)"},
        {"@BORDER_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.55)"},
        {"@BORDER@", "#e8eaed"}, {"@BORDER_STRONG@", "#dadce0"},
        {"@BORDER_HOVER@", "#bdbdbd"}, {"@BORDER_DASHED@", "#c4c7c5"},
        {"@BORDER_DISABLED@", "#b0b0b0"}, {"@BORDER_LIGHT@", "#f0f1f3"},
        {"@TEXT_SECONDARY@", "#666666"}, {"@TEXT_TERTIARY@", "#888888"},
        {"@TEXT_DISABLED@", "#bbbbbb"}, {"@TEXT_PRIMARY@", "#333333"},
        {"@TEXT_BORDER@", "#e8eaed"}, {"@TEXT_ACCENT@", "#1b5e20"},
        {"@PRIMARY_BG@", "#e8f5e9"},
        {"@BLACK_RGBA_0.06@", "rgba(0, 0, 0, 0.06)"},
        {"@BLACK_RGBA_0.08@", "rgba(0, 0, 0, 0.08)"},
        {"@BLACK_RGBA_0.10@", "rgba(0, 0, 0, 0.10)"},
        {"@BLACK_RGBA_0.13@", "rgba(0, 0, 0, 0.13)"},
        {"@BLACK_RGBA_0.25@", "rgba(0, 0, 0, 0.25)"},
        {"@PRIMARY@", "#2E7D32"},
        {"@PRIMARY_HOVER@", "#256428"},
        {"@PRIMARY_PRESSED@", "#1B5E20"},
        {"@PRIMARY_DARK@", "#1B5E20"},
        {"@PRIMARY_RGBA_12@", "rgba(46, 125, 50, 0.12)"},
        {"@PRIMARY_RGBA_20@", "rgba(46, 125, 50, 0.2)"},
        {"@PRIMARY_RGBA_38@", "rgba(46, 125, 50, 0.38)"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@SUCCESS@", "#10B981"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#D32F2F"},
        {"@WARNING_TEXT_DARK@", "#856404"},
        {"@WARNING_RGBA_26@", "rgba(255, 152, 0, 0.26)"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));
    static const QRegularExpression re(QStringLiteral("@([A-Za-z0-9_.\\-]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

// ── NBT 手工构造（大端），带 GameRules ──
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
static void putU64(QByteArray &out, quint64 v)
{
    for (int i = 7; i >= 0; --i)
        out.append(char((v >> (i * 8)) & 0xFF));
}
static void putString(QByteArray &out, const QByteArray &s)
{
    putU16(out, quint16(s.size()));
    out.append(s);
}
static void addString(QByteArray &out, const char *name, const QByteArray &v)
{
    out.append(char(8));
    putString(out, name);
    putString(out, v);
}
static void addInt(QByteArray &out, const char *name, qint32 v)
{
    out.append(char(3));
    putString(out, name);
    putU32(out, quint32(v));
}
static void addByte(QByteArray &out, const char *name, qint8 v)
{
    out.append(char(1));
    putString(out, name);
    out.append(char(v));
}
static void addLong(QByteArray &out, const char *name, qint64 v)
{
    out.append(char(4));
    putString(out, name);
    putU64(out, quint64(v));
}

static QByteArray buildLevelDatNbt()
{
    QByteArray rules;
    addString(rules, "keepInventory", "true");
    addString(rules, "randomTickSpeed", "7");
    addString(rules, "maxEntityCramming", "24");
    addString(rules, "spawnRadius", "10");
    addString(rules, "myCustomRule", "hello"); // 未知规则，用于确认展示不影响保存

    QByteArray data;
    addString(data, "LevelName", QByteArrayLiteral("\xE6\xB5\x8B\xE8\xAF\x95\xE4\xB8\x96\xE7\x95\x8C"));
    addInt(data, "GameType", 1);
    addByte(data, "allowCommands", 0);
    addByte(data, "Difficulty", 2);
    addByte(data, "DifficultyLocked", 0);
    addByte(data, "hardcore", 0);
    addLong(data, "LastPlayed", QDateTime::currentMSecsSinceEpoch());
    addLong(data, "DayTime", 6000);
    addInt(data, "SpawnX", 0);
    addInt(data, "SpawnY", 64);
    addInt(data, "SpawnZ", 0);
    // GameRules（TAG_Compound）
    data.append(char(10));
    putString(data, "GameRules");
    data.append(rules);
    data.append(char(0));
    data.append(char(0)); // Data 结束

    QByteArray root;
    root.append(char(10));
    putString(root, "");
    root.append(char(10));
    putString(root, "Data");
    root.append(data);
    root.append(char(0));
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

#define CHECK(cond, msg)                                  \
    do                                                    \
    {                                                     \
        if (!(cond))                                      \
        {                                                 \
            std::printf("FAIL: %s\n", msg);               \
            std::fflush(stdout);                          \
            return 1;                                     \
        }                                                 \
    }                                                     \
    while (0)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("savesettings_visual_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly))
    {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    app.setStyleSheet(resolveTokens(QString::fromUtf8(f.readAll())));
    f.close();

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    // 合成存档
    QTemporaryDir dir;
    CHECK(dir.isValid(), "临时目录创建失败");
    const QString save = dir.path() + QStringLiteral("/TestWorld");
    CHECK(QDir().mkpath(save), "存档目录创建失败");
    {
        QFile out(save + QStringLiteral("/level.dat"));
        const QByteArray gz = gzip(buildLevelDatNbt());
        CHECK(!gz.isEmpty() && out.open(QIODevice::WriteOnly | QIODevice::Truncate)
                  && out.write(gz) == gz.size(),
              "合成 level.dat 写入失败");
        out.close();
    }

    SaveSettingsPage page;
    page.setCurrentSave(save, QStringLiteral("TestWorld"));

    // ── 断言 1：文件中的规则值映射到控件 ──
    QCheckBox *keepInv = page.findChild<QCheckBox *>(
        QStringLiteral("keepInventory visual probe"));
    // 直接按文本查找
    keepInv = nullptr;
    const QList<QCheckBox *> allChecks = page.findChildren<QCheckBox *>();
    for (QCheckBox *cb : allChecks)
    {
        if (cb->text() == QString::fromUtf8("\xE6\xAD\xBB\xE4\xBA\xA1\xE4\xB8\x8D\xE6\x8E\x89\xE8\x90\xBD")) // 死亡不掉落
            keepInv = cb;
    }
    CHECK(keepInv, "未找到「死亡不掉落」复选框");
    CHECK(keepInv->isChecked(), "keepInventory 应按 level.dat 勾选");
    CHECK(keepInv->isEnabled(), "规则控件应已启用");

    QSpinBox *tickSpeed = nullptr;
    const QList<QSpinBox *> allSpins = page.findChildren<QSpinBox *>();
    for (QSpinBox *sb : allSpins)
    {
        if (sb->value() == 7)
            tickSpeed = sb;
    }
    CHECK(tickSpeed, "未找到 randomTickSpeed 输入框（应读回 7）");

    // ── 断言 2：搜索过滤 ──
    QLineEdit *search = nullptr;
    const QList<QLineEdit *> edits = page.findChildren<QLineEdit *>();
    for (QLineEdit *le : edits)
    {
        if (le->placeholderText().contains(QString::fromUtf8("\xE6\x90\x9C\xE7\xB4\xA2"))) // 搜索
            search = le;
    }
    CHECK(search, "未找到规则搜索框");
    int visibleBefore = 0;
    for (QCheckBox *cb : allChecks)
        if (cb->isVisibleTo(&page))
            ++visibleBefore;
    search->setText(QStringLiteral("keep"));
    QApplication::processEvents();
    int visibleAfter = 0;
    for (QCheckBox *cb : allChecks)
        if (cb->isVisibleTo(&page) && cb->isEnabled())
            ++visibleAfter;
    CHECK(visibleAfter < visibleBefore, "搜索后可见规则数应减少");
    CHECK(keepInv->isVisibleTo(&page), "搜索 keep 后死亡不掉落应仍可见");

    // ── 断言 3：恢复默认值 ──
    QPushButton *resetBtn = nullptr;
    const QList<QPushButton *> btns = page.findChildren<QPushButton *>();
    for (QPushButton *b : btns)
    {
        if (b->text() == QString::fromUtf8("\xE6\x81\xA2\xE5\xA4\x8D\xE9\xBB\x98\xE8\xAE\xA4\xE5\x80\xBC")) // 恢复默认值
            resetBtn = b;
    }
    CHECK(resetBtn, "未找到「恢复默认值」按钮");
    resetBtn->click();
    QApplication::processEvents();
    CHECK(!keepInv->isChecked(), "恢复默认后 keepInventory 应取消勾选");
    CHECK(tickSpeed->value() == 3, "恢复默认后 randomTickSpeed 应为 3");

    // ── 断言 4：其他规则（模组/数据包添加）展示与不受恢复默认影响 ──
    QLabel *otherHeader = nullptr;
    for (QLabel *lab : page.findChildren<QLabel *>())
    {
        if (lab->text() == QString::fromUtf8("\xE5\x85\xB6\xE4\xBB\x96\xE8\xA7\x84\xE5\x88\x99")) // 其他规则
            otherHeader = lab;
    }
    CHECK(otherHeader, "未找到「其他规则」分组标题");

    QLineEdit *customEdit = nullptr;
    for (QLineEdit *le : page.findChildren<QLineEdit *>())
    {
        if (le->text() == QStringLiteral("hello"))
            customEdit = le;
    }
    CHECK(customEdit, "未找到其他规则 myCustomRule 的值输入框");
    CHECK(customEdit->isEnabled(), "其他规则输入框应已启用");
    CHECK(customEdit->text() == QStringLiteral("hello"), "恢复默认不应影响其他规则的值");

    // 清空搜索，恢复完整视图
    search->clear();
    QApplication::processEvents();

    // ── 截图 ──
    auto shoot = [&page](int w, int h, const char *name) {
        page.resize(w, h);
        page.show();
        QApplication::processEvents();
        const QString path = QStringLiteral("D:/BlockBox/_out/savesettings_probe_%1.png").arg(name);
        page.grab().save(path);
        std::printf("saved %s (%dx%d)\n", qPrintable(path), page.width(), page.height());
    };
    shoot(560, 1060, "narrow");
    shoot(900, 1120, "wide");

    // 滚动到规则卡片底部，截取其余分组
    QScrollArea *scroll = page.findChild<QScrollArea *>();
    if (scroll)
    {
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        shoot(900, 1060, "bottom");
    }

    std::printf("ALL PASS\n");
    std::fflush(stdout);
    return 0;
}
