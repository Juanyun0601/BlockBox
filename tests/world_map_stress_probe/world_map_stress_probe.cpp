/**
 * @file   world_map_stress_probe.cpp
 * @brief  WorldMapPage 压力探针：反复在渲染过程中销毁/重载/切维度，抓闪退
 *
 * 覆盖场景（按序循环）：
 *   0 渲染进行中直接销毁页面
 *   1 渲染进行中 reset() 后立刻重新进入同一存档，再销毁
 *   2 渲染进行中连续切换维度（触发 setFuture 替换 + 旧 future 迟到完成），再销毁
 *   3 等渲染完成后再次进入（走缓存命中路径），再销毁
 *   4 页面挂在临时父窗口下，渲染中销毁父窗口（模拟实例管理页被销毁）
 *   5 最后一次：启动渲染后只等几毫秒就删除并退出（线程池仍有在途任务）
 *
 * 用法：world_map_stress_probe.exe [iterations]（默认 60）
 * 通过返回 0；崩溃则进程直接消失（退出码非 0）。
 */
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLayout>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <cstdio>
#include <cstring>

#include <zlib.h>

#include "pages/WorldMapPage.h"
#include "utils/Schematic/NbtCodec.h"

namespace {

// ── 合成存档 fixture（与 world_map_page_probe 同款 NBT 布局） ──

QList<qint64> packStates(const QVector<int> &indices, int bits)
{
    const int per = 64 / bits;
    QList<qint64> longs;
    longs.fill(0, (4096 + per - 1) / per);
    for (int i = 0; i < 4096; ++i)
        longs[i / per] |= (qint64(indices[i]) << ((i % per) * bits));
    return longs;
}

NbtTag makePalette()
{
    const char *names[17] = {
        "minecraft:air", "minecraft:grass_block", "minecraft:water",
        "minecraft:sand", "minecraft:stone", "minecraft:dirt",
        "minecraft:cobblestone", "minecraft:gravel", "minecraft:oak_planks",
        "minecraft:oak_log", "minecraft:oak_leaves", "minecraft:andesite",
        "minecraft:diorite", "minecraft:granite", "minecraft:deepslate",
        "minecraft:bedrock", "minecraft:snow_block"
    };
    QList<NbtTag> items;
    for (int i = 0; i < 17; ++i)
    {
        NbtTag item = NbtCodec::makeCompound();
        item.children.append(NbtCodec::makeString(QStringLiteral("Name"),
                                                  QString::fromLatin1(names[i])));
        items.append(item);
    }
    return NbtCodec::makeList(QStringLiteral("palette"), NbtType::Compound, items);
}

QVector<int> buildIndices(int cx, int cz)
{
    QVector<int> indices(4096, 0);
    for (int lz = 0; lz < 16; ++lz)
    {
        for (int lx = 0; lx < 16; ++lx)
        {
            const int wx = cx * 16 + lx;
            const int wz = cz * 16 + lz;
            int surf = 4 + ((wx * 7 + wz * 3) % 8);
            int top = 1;
            if (qAbs(wx - wz) <= 2)
            {
                surf = 4;
                top = 2;
            }
            else if ((wx / 8 + wz / 8) % 3 == 0)
                top = 3;
            else if (((wx * wz) % 13) == 0)
                top = 4;

            for (int yy = 0; yy < 16; ++yy)
            {
                int v = 0;
                if (yy < surf)
                    v = 5;
                else if (yy == surf)
                    v = top;
                indices[yy * 256 + lz * 16 + lx] = v;
            }
        }
    }
    return indices;
}

NbtTag makeChunk(int cx, int cz)
{
    NbtTag states = NbtCodec::makeCompound();
    states.name = QStringLiteral("block_states");
    states.children.append(makePalette());
    states.children.append(NbtCodec::makeLongArray(
        QStringLiteral("data"), packStates(buildIndices(cx, cz), 5)));

    NbtTag section = NbtCodec::makeCompound();
    section.children.append(NbtCodec::makeByte(QStringLiteral("Y"), 4));
    section.children.append(states);

    NbtTag root = NbtCodec::makeCompound();
    root.children.append(NbtCodec::makeInt(QStringLiteral("DataVersion"), 3465));
    root.children.append(NbtCodec::makeInt(QStringLiteral("xPos"), cx));
    root.children.append(NbtCodec::makeInt(QStringLiteral("zPos"), cz));
    root.children.append(NbtCodec::makeList(QStringLiteral("sections"),
                                            NbtType::Compound, { section }));
    return root;
}

QByteArray zlibCompress(const QByteArray &raw)
{
    uLongf bound = compressBound(uLong(raw.size()));
    QByteArray out;
    out.resize(int(bound));
    if (compress2(reinterpret_cast<Bytef *>(out.data()), &bound,
                  reinterpret_cast<const Bytef *>(raw.constData()),
                  uLong(raw.size()), Z_DEFAULT_COMPRESSION) != Z_OK)
        return QByteArray();
    out.resize(int(bound));
    return out;
}

bool writeFullRegion(const QString &path, int regionX, int regionZ, int span)
{
    QByteArray file;
    file.append(QByteArray(8192, '\0'));
    int sector = 2;

    for (int dz = 0; dz < span; ++dz)
    {
        for (int dx = 0; dx < span; ++dx)
        {
            const int cx = regionX * 32 + dx;
            const int cz = regionZ * 32 + dz;
            const QByteArray comp = zlibCompress(NbtCodec::serialize(makeChunk(cx, cz)));
            if (comp.isEmpty())
                return false;

            QByteArray entry;
            const quint32 len = quint32(comp.size()) + 1;
            entry.append(char((len >> 24) & 0xFF));
            entry.append(char((len >> 16) & 0xFF));
            entry.append(char((len >> 8) & 0xFF));
            entry.append(char(len & 0xFF));
            entry.append(char(2));
            entry.append(comp);
            const int sectors = (entry.size() + 4095) / 4096;
            entry.append(QByteArray(sectors * 4096 - entry.size(), '\0'));

            const int idx = (cx & 31) + (cz & 31) * 32;
            file[idx * 4 + 0] = char((sector >> 16) & 0xFF);
            file[idx * 4 + 1] = char((sector >> 8) & 0xFF);
            file[idx * 4 + 2] = char(sector & 0xFF);
            file[idx * 4 + 3] = char(sectors);

            if (file.size() < sector * 4096 + entry.size())
                file.append(QByteArray(sector * 4096 + entry.size() - file.size(), '\0'));
            std::memcpy(file.data() + sector * 4096, entry.constData(), size_t(entry.size()));
            sector += sectors;
        }
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(file);
    return true;
}

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 3);
}

/** 等渲染结束（带超时），期间保持事件循环 */
bool waitIdle(WorldMapPage &page, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (page.isRendering() && t.elapsed() < timeoutMs)
        QApplication::processEvents(QEventLoop::AllEvents, 3);
    pump(30);
    return !page.isRendering();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const int iterations = (argc > 1) ? QString::fromLocal8Bit(argv[1]).toInt() : 60;
    if (iterations <= 0)
    {
        std::fprintf(stderr, "bad iteration count\n");
        return 2;
    }

    // ── fixture：一个满区域 + 一个 4 区块的小区域，覆盖多区域并行路径 ──
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::fprintf(stderr, "temp dir failed\n");
        return 2;
    }
    const QString saveA = tmp.path() + QStringLiteral("/saves/WorldA");
    const QString saveB = tmp.path() + QStringLiteral("/saves/WorldB");
    if (!writeFullRegion(saveA + "/region/r.0.0.mca", 0, 0, 32)
        || !writeFullRegion(saveA + "/region/r.1.1.mca", 1, 1, 2)
        || !writeFullRegion(saveB + "/region/r.0.0.mca", 0, 0, 8))
    {
        std::fprintf(stderr, "fixture failed\n");
        return 2;
    }

    int crashes = 0; // 不会真正用到：崩溃时进程直接消失
    for (int i = 0; i < iterations; ++i)
    {
        const int scene = i % 6;
        const QString save = (i & 1) ? saveB : saveA;
        const QString name = (i & 1) ? QStringLiteral("WorldB") : QStringLiteral("WorldA");

        switch (scene)
        {
        case 0: // 渲染进行中直接销毁
        {
            auto *page = new WorldMapPage();
            page->resize(1000, 640);
            page->show();
            pump(8);
            page->setCurrentSave(save, name);
            pump(5 + int(QRandomGenerator::global()->bounded(40)));
            delete page;
            break;
        }
        case 1: // 渲染中 reset → 立刻重新进入 → 销毁
        {
            auto *page = new WorldMapPage();
            page->resize(1000, 640);
            page->show();
            pump(8);
            page->setCurrentSave(save, name);
            pump(8);
            page->reset();
            page->setCurrentSave(save, name);
            pump(3);
            delete page;
            break;
        }
        case 2: // 渲染中连续切维度（setFuture 替换 + 旧 future 迟到完成）
        {
            auto *page = new WorldMapPage();
            page->resize(1000, 640);
            page->show();
            pump(8);
            page->setCurrentSave(save, name);
            pump(5);
            const auto combos = page->findChildren<QComboBox *>();
            if (!combos.isEmpty())
            {
                for (int k = 0; k < 5; ++k)
                {
                    combos.first()->setCurrentIndex(k % 3);
                    pump(int(QRandomGenerator::global()->bounded(12)));
                }
            }
            delete page;
            break;
        }
        case 3: // 等完成 → 再次进入（缓存命中）→ 销毁
        {
            auto *page = new WorldMapPage();
            page->resize(1000, 640);
            page->show();
            pump(8);
            page->setCurrentSave(save, name);
            waitIdle(*page, 4000);
            page->setCurrentSave(save, name);
            pump(15);
            delete page;
            break;
        }
        case 4: // 挂在父窗口下，渲染中销毁父窗口
        {
            auto *host = new QWidget();
            host->resize(1000, 640);
            auto *lay = new QVBoxLayout(host);
            lay->setContentsMargins(0, 0, 0, 0);
            auto *page = new WorldMapPage(host);
            lay->addWidget(page);
            host->show();
            pump(8);
            page->setCurrentSave(save, name);
            pump(6 + int(QRandomGenerator::global()->bounded(30)));
            delete host; // 连带删除页面
            break;
        }
        default: // 5：等完成后立刻连续重载（放大 finished 与重入的交叠）
        {
            auto *page = new WorldMapPage();
            page->resize(1000, 640);
            page->show();
            pump(8);
            page->setCurrentSave(save, name);
            waitIdle(*page, 4000);
            for (int k = 0; k < 3; ++k)
            {
                page->setCurrentSave(save, name);
                pump(2);
            }
            delete page;
            break;
        }
        }

        pump(15);
        if ((i + 1) % 10 == 0)
            std::fprintf(stderr, "  ... %d/%d iterations\n", i + 1, iterations);
    }

    // 最后一击：启动渲染后只等几毫秒就删除并退出，线程池里仍有在途任务
    {
        auto *page = new WorldMapPage();
        page->resize(1000, 640);
        page->show();
        pump(8);
        page->setCurrentSave(saveA, QStringLiteral("WorldA"));
        pump(4);
        delete page;
        std::fprintf(stderr, "  exit while render in flight...\n");
    }

    std::fprintf(stderr, "stress OK (iterations=%d, survived=%d)\n", iterations, crashes + iterations);
    return 0;
}
