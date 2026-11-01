/**
 * @file   desert_well_probe.cpp
 * @brief  沙漠水井预览探针：按种子+坐标生成模型，导出方块表供逐块对照
 *
 * 用法：desert_well_probe.exe <seed> <x> <z> <mcVersion> <out.json>
 */

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include "utils/Schematic/SchematicDocument.h"
#include "utils/WorldMap/StructureModelBuilder.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 6)
    {
        QTextStream(stderr) << "usage: desert_well_probe <seed> <x> <z> <version> <out>\n";
        return 2;
    }
    const quint64 seed = QString::fromLocal8Bit(argv[1]).toULongLong();
    const qint64 x = QString::fromLocal8Bit(argv[2]).toLongLong();
    const qint64 z = QString::fromLocal8Bit(argv[3]).toLongLong();
    const QString version = QString::fromLocal8Bit(argv[4]);
    const QString out = QString::fromLocal8Bit(argv[5]);

    StructureModelInfo info;
    SchematicDocument *doc = StructureModelBuilder::build(
        QStringLiteral("desert_well"), x, z, seed, true, version, &info);
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
    root.insert(QStringLiteral("fromTemplate"), info.fromVanillaTemplate);
    root.insert(QStringLiteral("size"), QJsonArray{doc->sizeX(), doc->sizeY(), doc->sizeZ()});
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
