/**
 * @file   village_probe.cpp
 * @brief  结构预览探针：按种子+坐标生成结构模型，导出方块表供逐块对照
 * @author BlockBox Team
 *
 * 用法：village_probe.exe <structKey> <seed> <x> <z> <mcVersion> <out.json>
 * 输出：{"variant":"平原村庄","size":[sx,sy,sz],"blocks":{"x,y,z":"minecraft:..."}}
 */

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include "utils/Schematic/SchematicDocument.h"
#include "utils/WorldMap/StructureModelBuilder.h"
#include "utils/WorldMap/StructureFinder.h"
#include "utils/WorldMap/BiomeCatalog.h"
#include "util.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 7)
    {
        QTextStream(stderr) << "usage: village_probe <structKey> <seed> <x> <z> <version> <out>\n";
        return 2;
    }
    const QString structKey = QString::fromLocal8Bit(argv[1]);
    const quint64 seed = QString::fromLocal8Bit(argv[2]).toULongLong();
    const qint64 x = QString::fromLocal8Bit(argv[3]).toLongLong();
    const qint64 z = QString::fromLocal8Bit(argv[4]).toLongLong();
    const QString version = QString::fromLocal8Bit(argv[5]);
    const QString out = QString::fromLocal8Bit(argv[6]);

    if (structKey == QLatin1String("biome"))
    {
        const int mc = StructureFinder::versionToInt(version);
        const int id = BiomeCatalog::biomeAt(seed, mc, 0, x, z);
        const char *name = biome2str(mc, id);
        QTextStream(stdout) << "biome=" << (name ? name : "?") << " id=" << id << "\n";
        return 0;
    }

    StructureModelInfo info;
    SchematicDocument *doc = StructureModelBuilder::build(
        structKey, x, z, seed, true, version, &info);
    if (!doc)
    {
        QTextStream(stderr) << "build failed: " << info.error << "\n";
        return 1;
    }

    QJsonObject blocks;
    for (int bx = 0; bx < doc->sizeX(); ++bx)
        for (int by = 0; by < doc->sizeY(); ++by)
            for (int bz = 0; bz < doc->sizeZ(); ++bz)
            {
                const SchematicBlockState state = doc->block(bx, by, bz);
                if (state.name.isEmpty() || state.name == QLatin1String("minecraft:air"))
                    continue;
                blocks.insert(QStringLiteral("%1,%2,%3").arg(bx).arg(by).arg(bz),
                              state.toString());
            }

    QJsonObject root;
    root.insert(QStringLiteral("variant"), info.variant);
    root.insert(QStringLiteral("rotation"), info.rotation);
    root.insert(QStringLiteral("blocks"), blocks);
    QFile file(out);
    if (!file.open(QIODevice::WriteOnly))
    {
        QTextStream(stderr) << "cannot write " << out << "\n";
        return 1;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    QTextStream(stdout) << "variant=" << info.variant
                        << " size=" << doc->sizeX() << "x" << doc->sizeY() << "x"
                        << doc->sizeZ() << " blocks=" << blocks.size() << "\n";
    delete doc;
    return 0;
}
