#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <cstdio>

#include "utils/Schematic/InstanceTextureLoader.h"

int main(int argc, char *argv[])
{
    // QIcon/QPixmap 需要 GUI 应用
    QGuiApplication app(argc, argv);
    const QString path = QString::fromLocal8Bit(argc > 1 ? argv[1] : "");
    if (path.isEmpty())
    {
        std::printf("usage: tex_probe <instancePath>\n");
        return 2;
    }
    auto *loader = InstanceTextureLoader::sharedForInstance(path);
    if (!loader)
    {
        std::printf("loader=null path=%s\n", path.toUtf8().constData());
        return 1;
    }
    std::printf("valid=%d tilesPerRow=%d count=%d atlas=%d\n",
                loader->isValid() ? 1 : 0,
                loader->tilesPerRow(),
                loader->textureCount(),
                loader->atlasSize());

    const QStringList probes = {
        QStringLiteral("minecraft:stone"),
        QStringLiteral("minecraft:oak_log"),
        QStringLiteral("minecraft:dirt"),
        QStringLiteral("minecraft:oak_planks"),
        QStringLiteral("minecraft:hopper"),
        QStringLiteral("minecraft:iron_block"),
        QStringLiteral("minecraft:redstone_block"),
        QStringLiteral("minecraft:glass"),
        QStringLiteral("minecraft:white_wool"),
        QStringLiteral("minecraft:cobblestone"),
    };
    int ok = 0;
    for (const QString &id : probes)
    {
        const QIcon icon = loader->blockIcon(id, 20);
        int tile = -1;
        const bool has = loader->textureFor(id, tile);
        std::printf("  %s tile=%d iconNull=%d\n",
                    id.toUtf8().constData(), tile, icon.isNull() ? 1 : 0);
        if (has && !icon.isNull())
            ++ok;
    }
    std::printf("ok=%d/%d\n", ok, probes.size());
    return ok >= 8 ? 0 : 1;
}
