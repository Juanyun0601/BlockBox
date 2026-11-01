/**
 * 探针：验证战利品芯片图标的数据源（实例客户端 jar 贴图）确实能命中。
 * 用法：probe_loot_icons.exe <instancePath> <lootIdsFile>
 * 返回 0 表示图集里既有方块贴图也有物品贴图，且绝大多数战利品 id 能取到图标。
 */
#include "utils/Schematic/InstanceTextureLoader.h"

#include <QGuiApplication>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QPainter>
#include <QColor>
#include <QString>
#include <QStringList>
#include <cstdio>

namespace
{
QImage iconImage(const QIcon &icon, int size)
{
    if (icon.isNull())
        return QImage();
    return icon.pixmap(QSize(size, size)).toImage().convertToFormat(QImage::Format_ARGB32);
}

bool hasVisiblePixels(const QImage &im)
{
    if (im.isNull())
        return false;
    for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
            if (qAlpha(im.pixel(x, y)) > 8)
                return true;
    return false;
}
} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 3)
    {
        std::printf("usage: probe_loot_icons <instancePath> <lootIdsFile>\n");
        return 2;
    }
    const QString inst = QString::fromLocal8Bit(argv[1]);
    const QString idsFile = QString::fromLocal8Bit(argv[2]);

    InstanceTextureLoader *loader = InstanceTextureLoader::sharedForInstance(inst);
    if (!loader)
    {
        std::printf("[FAIL] sharedForInstance 返回空\n");
        return 2;
    }
    std::printf("[atlas] tiles=%d items=%d tilesPerRow=%d side=%d valid=%d\n",
                loader->textureCount(), loader->itemTextureCount(), loader->tilesPerRow(),
                loader->atlasSize(), loader->isValid() ? 1 : 0);

    // 方块侧不受影响：常用方块仍要有贴图
    const QStringList blocks = { "minecraft:stone", "minecraft:gold_block",
                                 "minecraft:obsidian", "minecraft:oak_log[axis=y]" };
    int blockHit = 0;
    for (const QString &b : blocks)
    {
        const QImage im = iconImage(loader->blockIcon(b, 16), 16);
        const bool ok = hasVisiblePixels(im);
        if (ok)
            ++blockHit;
        std::printf("[block] %-28s hit=%d size=%dx%d\n", b.toUtf8().constData(),
                    ok ? 1 : 0, im.width(), im.height());
    }

    QFile f(idsFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        std::printf("[FAIL] 读不到 id 列表 %s\n", idsFile.toUtf8().constData());
        return 2;
    }
    QStringList ids;
    while (!f.atEnd())
    {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (!line.isEmpty())
            ids.append(line);
    }

    int itemHit = 0, blockFallback = 0, swatch = 0;
    QStringList missing;
    for (const QString &id : ids)
    {
        const QImage viaItem = iconImage(loader->itemIcon(id, 16), 16);
        if (hasVisiblePixels(viaItem))
        {
            ++itemHit;
            continue;
        }
        const QImage viaBlock = iconImage(loader->blockIcon(id, 16), 16);
        if (hasVisiblePixels(viaBlock))
        {
            ++blockFallback;
            continue;
        }
        ++swatch;
        missing.append(id);
    }

    // 色块兜底也必须至少出一张有效图（不能空白）
    const QImage sw = InstanceTextureLoader::swatchIcon(QColor(0x3c, 0x8c, 0x3c), 16, 2.0)
                          .pixmap(QSize(32, 32))
                          .toImage();
    std::printf("[ids] total=%d itemIcon=%d blockFallback=%d swatch=%d swatchVisible=%d\n",
                ids.size(), itemHit, blockFallback, swatch, hasVisiblePixels(sw) ? 1 : 0);
    if (!missing.isEmpty())
    {
        std::printf("[ids] 无贴图（走色块）：\n");
        for (const QString &m : missing)
            std::printf("   - %s\n", m.toUtf8().constData());
    }

    // 判定：方块贴图可用 + 物品贴图已入库 + 至少九成战利品能出真图标
    const bool ok = blockHit == blocks.size() && loader->itemTextureCount() > 100
                    && ids.size() > 0 && (itemHit + blockFallback) * 10 >= ids.size() * 9;

    // 顺带把几种典型图标拼成一张大图，方便肉眼确认观感
    const QStringList sample = { "minecraft:gold_block", "minecraft:diamond",
                                 "minecraft:clock", "minecraft:bread",
                                 "minecraft:obsidian", "minecraft:golden_apple",
                                 "minecraft:enchanted_golden_apple", "minecraft:arrow" };
    const int cell = 64;
    QImage sheet(cell * 4, cell * 2, QImage::Format_ARGB32);
    sheet.fill(QColor(0x24, 0x24, 0x28));
    for (int i = 0; i < sample.size(); ++i)
    {
        QIcon icon = loader->itemIcon(sample.at(i), 16);
        if (icon.isNull())
            icon = loader->blockIcon(sample.at(i), 16);
        QImage im = iconImage(icon, 16);
        if (im.isNull())
        {
            im = InstanceTextureLoader::swatchPixmap(QColor(0x3c, 0x8c, 0x3c), 16, 2.0)
                     .toImage();
        }
        im = im.scaled(cell - 8, cell - 8, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        QPainter p(&sheet);
        p.drawImage((i % 4) * cell + 4, (i / 4) * cell + 4, im);
    }
    sheet.save(QStringLiteral(".syntax_check/out/loot_icon_sheet.png"));
    std::printf("[%s] blockHit=%d/%d itemTextureCount=%d itemIcon=%d blockFallback=%d swatch=%d\n",
                ok ? "PASS" : "FAIL", blockHit, int(blocks.size()),
                loader->itemTextureCount(), itemHit, blockFallback, swatch);
    return ok ? 0 : 1;
}
