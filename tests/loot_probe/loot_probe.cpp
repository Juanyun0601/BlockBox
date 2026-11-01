/**
 * @file   loot_probe.cpp
 * @brief  战利品概率探针：打印每种结构的预测结果，并导出 TSV 供校准脚本比对
 */

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <cstdio>

#include "../../utils/WorldMap/StructureLoot.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QStringList keys = {
        QStringLiteral("village"),        QStringLiteral("outpost"),
        QStringLiteral("mansion"),        QStringLiteral("stronghold"),
        QStringLiteral("desert_pyramid"), QStringLiteral("jungle_temple"),
        QStringLiteral("igloo"),          QStringLiteral("ocean_ruin"),
        QStringLiteral("shipwreck"),      QStringLiteral("ruined_portal"),
        QStringLiteral("ancient_city"),   QStringLiteral("buried_treasure"),
        QStringLiteral("mineshaft"),      QStringLiteral("trial_chambers"),
        QStringLiteral("trail_ruins"),    QStringLiteral("desert_well"),
        QStringLiteral("fortress"),       QStringLiteral("bastion"),
        QStringLiteral("ruined_portal_nether"), QStringLiteral("end_city"),
        QStringLiteral("end_ship"),       QStringLiteral("monument"),
        QStringLiteral("swamp_hut"),
    };

    QFile tsv(QStringLiteral("D:/BlockBox/_out/loot_probe.tsv"));
    tsv.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&tsv);

    QElapsedTimer timer;
    timer.start();

    for (const QString &key : keys)
    {
        const StructureLoot::Result r =
            StructureLoot::rollLoot(key, 123456789ULL, true, 0, 0, 21);
        std::printf("== %s  chests=%d  chestKinds=%d  anyItems=%d\n",
                    key.toUtf8().constData(), r.chests, int(r.chestLoots.size()),
                    int(r.stacks.size()));
        for (const StructureLoot::ChestLoot &cl : r.chestLoots)
        {
            std::printf("  -- %s x%d (%s)\n", cl.label.toUtf8().constData(),
                        cl.count, cl.tableId.toUtf8().constData());
            for (const StructureLoot::Stack &s : cl.stacks)
            {
                out << key << '\t' << cl.tableId << '\t' << s.itemId << '\t'
                    << s.note << '\t' << QString::number(s.chance, 'f', 5) << '\t'
                    << s.count << '\t' << (s.guaranteed ? 1 : 0) << '\t'
                    << (s.enchanted ? 1 : 0) << '\t' << s.name << '\n';
            }
            // 控制台只打前 8 条，避免刷屏
            for (int i = 0; i < cl.stacks.size() && i < 8; ++i)
            {
                const StructureLoot::Stack &s = cl.stacks.at(i);
                std::printf("     %-24s p=%5.1f%%  x%d%s\n",
                            s.name.toUtf8().constData(), s.chance * 100.0, s.count,
                            s.guaranteed ? "  [必出]" : "");
            }
        }
        out.flush();
    }

    std::printf("\nelapsed %lld ms\n", static_cast<long long>(timer.elapsed()));
    return 0;
}
