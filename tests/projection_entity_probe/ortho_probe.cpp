/**
 * @file   ortho_probe.cpp
 * @brief  三视图「带生物」渲染探针：造一份带方块 + 生物的投影，输出三张 PNG
 *
 * 只链接三视图渲染那一条链路（WorldMapRenderer::blockColor 在这里用灰色桩函数替代，
 * 免得把整个世界地图模块拖进来）。仅开发期使用，不进正式构建。
 *
 * 用法：ortho_probe <实例 versions 目录> <输出目录>
 */

#include "utils/Schematic/SchematicDocument.h"
#include "utils/Schematic/SchematicOrthoViewRenderer.h"
#include "utils/WorldMap/WorldMapRenderer.h"

#include <QDir>
#include <QGuiApplication>
#include <QStringList>

/** 桩：三视图只用到方块代表色，探针里统一给灰色，看得出形状即可 */
namespace WorldMapRenderer
{
QColor blockColor(const QString &blockName)
{
    Q_UNUSED(blockName)
    return QColor(150, 150, 150);
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    const QStringList args = QCoreApplication::arguments();
    const QString instancePath = args.size() > 1 ? args.at(1) : QString();
    const QString outDir = args.size() > 2 ? args.at(2) : QStringLiteral(".");

    // ── 造一份 12×6×12 的投影：中间一堵墙 + 一圈地板，外面放几只生物 ──
    SchematicDocument doc;
    if (!doc.createEmpty(12, 6, 12))
    {
        qWarning("造不出空投影");
        return 1;
    }
    const SchematicBlockState stone{QStringLiteral("minecraft:stone"), {}};
    const SchematicBlockState planks{QStringLiteral("minecraft:oak_planks"), {}};
    const SchematicBlockState glass{QStringLiteral("minecraft:glass"), {}};
    for (int z = 0; z < 12; ++z)
        for (int x = 0; x < 12; ++x)
            doc.setBlock(x, 0, z, planks);
    for (int y = 1; y <= 3; ++y)
        for (int x = 2; x <= 9; ++x)
            doc.setBlock(x, y, 6, stone);
    for (int x = 2; x <= 9; ++x)
        doc.setBlock(x, 4, 6, glass);

    QList<SchematicEntity> entities;
    auto addEntity = [&entities](const char *id, double x, double y, double z, float yaw) {
        SchematicEntity e;
        e.id = QString::fromLatin1(id);
        e.x = x;
        e.y = y;
        e.z = z;
        e.yaw = yaw;
        entities.append(e);
    };
    addEntity("minecraft:pig", 3.5, 1.0, 3.5, 0.0f);
    addEntity("minecraft:cow", 6.2, 1.0, 2.5, 90.0f);
    addEntity("minecraft:zombie", 8.3, 1.0, 4.2, 180.0f);
    addEntity("minecraft:chicken", 2.3, 1.0, 4.6, 270.0f);
    addEntity("minecraft:villager", 5.0, 1.0, 1.4, 0.0f);
    addEntity("minecraft:pig", 5.0, 1.0, 8.2, 0.0f);   // 在墙后面（应当被挡住）
    doc.setEntities(entities);

    QDir().mkpath(outDir);
    const QString filePath = outDir + QStringLiteral("/probe_entities.litematic");
    QString error;
    if (!doc.saveCopy(filePath, &error))
    {
        qWarning("写盘失败：%s", qPrintable(error));
        return 1;
    }

    {
        SchematicDocument check;
        QString checkError;
        if (!check.load(filePath, &checkError))
        {
            qWarning("回读失败：%s", qPrintable(checkError));
        }
        else
        {
            int nonAir = 0;
            for (int i = 0; i < check.volume(); ++i)
                if (check.paletteIndexAt(i) != 0)
                    ++nonAir;
            qInfo("回读：尺寸 %dx%dx%d 方块 %d 调色板 %d 实体 %d", check.sizeX(), check.sizeY(),
                  check.sizeZ(), nonAir, check.paletteSize(), check.entityCount());
        }
    }

    const SchematicOrthoResult result =
        SchematicOrthoViewRenderer::render(filePath, instancePath, 700);
    if (!result.isValid())
    {
        qWarning("三视图渲染失败：%s", qPrintable(result.error));
        return 1;
    }
    result.front.save(outDir + QStringLiteral("/probe_front.png"));
    result.left.save(outDir + QStringLiteral("/probe_left.png"));
    result.top.save(outDir + QStringLiteral("/probe_top.png"));
    qInfo("已输出三视图到 %s", qPrintable(outDir));
    return 0;
}
