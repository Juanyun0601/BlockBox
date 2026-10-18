#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <cstdio>

#include "utils/Schematic/InstanceBlockNames.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString path = QString::fromLocal8Bit(argc > 1 ? argv[1] : "");
    if (path.isEmpty())
    {
        std::printf("usage: ibn_probe <instancePath>\n");
        return 2;
    }
    const auto map = InstanceBlockNames::loadChineseMap(path);
    std::printf("map size=%d path=%s\n", map.size(), path.toUtf8().constData());
    const QStringList probes = {
        QStringLiteral("minecraft:stone"),
        QStringLiteral("minecraft:oak_log"),
        QStringLiteral("minecraft:dirt"),
        QStringLiteral("minecraft:cobblestone"),
        QStringLiteral("minecraft:glass"),
        QStringLiteral("minecraft:white_wool"),
        QStringLiteral("minecraft:oak_planks"),
        QStringLiteral("minecraft:iron_block"),
        QStringLiteral("minecraft:redstone_block"),
        QStringLiteral("minecraft:hopper"),
    };
    for (const QString &id : probes)
    {
        const QString cn = InstanceBlockNames::lookup(map, id);
        std::printf("  %s -> %s\n", id.toUtf8().constData(),
                    cn.isEmpty() ? "(empty)" : cn.toUtf8().constData());
    }
    // property-stripped lookup
    const QString propCn =
        InstanceBlockNames::lookup(map, QStringLiteral("minecraft:oak_log[axis=y]"));
    std::printf("  oak_log[axis=y] -> %s\n",
                propCn.isEmpty() ? "(empty)" : propCn.toUtf8().constData());
    return 0;
}
