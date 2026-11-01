/**
 * @file   fortress_probe.cpp
 * @brief  下界要塞预览探针：按种子+起始区块生成要塞模型，导出各段包围盒与方块表
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 用法：fortress_probe.exe <seed> <chunkX> <chunkZ> <out.json>
 * 输出：{"ok":…,"pieces":[{"type":…,"box":[x0,y0,z0,x1,y1,z1]}],
 *        "blocks":{"世界x,世界y,世界z":"minecraft:…"}}
 */

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include "utils/WorldMap/FortressModel.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 5)
    {
        QTextStream(stderr) << "usage: fortress_probe <seed> <chunkX> <chunkZ> <out>\n";
        return 2;
    }
    const quint64 seed = QString::fromLocal8Bit(argv[1]).toULongLong();
    const int cx = QString::fromLocal8Bit(argv[2]).toInt();
    const int cz = QString::fromLocal8Bit(argv[3]).toInt();
    const QString out = QString::fromLocal8Bit(argv[4]);

    const FortressModel::Model model = FortressModel::build(seed, cx, cz);
    if (!model.ok)
    {
        QTextStream(stderr) << "build failed: " << model.error << "\n";
        return 1;
    }

    QJsonObject root;
    root.insert(QStringLiteral("ok"), true);
    root.insert(QStringLiteral("startX"), model.startX);
    root.insert(QStringLiteral("startZ"), model.startZ);
    root.insert(QStringLiteral("blockCount"), model.blockCount);

    QJsonArray pieces;
    for (const FortressModel::PieceBox &p : model.pieces)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), p.type);
        QJsonArray box;
        box.append(p.x0);
        box.append(p.y0);
        box.append(p.z0);
        box.append(p.x1);
        box.append(p.y1);
        box.append(p.z1);
        obj.insert(QStringLiteral("box"), box);
        pieces.append(obj);
    }
    root.insert(QStringLiteral("pieces"), pieces);

    QJsonObject blocks;
    for (auto it = model.blocks.constBegin(); it != model.blocks.constEnd(); ++it)
    {
        int rx, y, rz;
        FortressModel::unpackKey(it.key(), rx, y, rz);
        blocks.insert(QStringLiteral("%1,%2,%3")
                          .arg(rx + model.startX).arg(y).arg(rz + model.startZ),
                      it.value());
    }
    root.insert(QStringLiteral("blocks"), blocks);

    QFile file(out);
    if (!file.open(QIODevice::WriteOnly))
    {
        QTextStream(stderr) << "cannot write " << out << "\n";
        return 1;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    QTextStream(stdout) << "blocks=" << model.blockCount
                        << " pieces=" << model.pieces.size() << "\n";
    return 0;
}
