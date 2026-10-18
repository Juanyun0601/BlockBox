/**
 * 探针：从实例路径加载 GameRegistry 中文名并查询常见方块
 */
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QStringList>
#include <cstdio>

#include "utils/CommandAssistant/GameRegistry.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 2)
    {
        std::printf("usage: cn_probe <instancePath>\n");
        return 2;
    }
    const QString instancePath = QString::fromLocal8Bit(argv[1]);
    QDir dir(instancePath);
    if (!dir.exists())
    {
        std::printf("instance path not exists\n");
        return 2;
    }

    const QString base = QFileInfo(instancePath).fileName();
    QString bestJar;
    qint64 bestSize = -1;
    const QFileInfoList jars = dir.entryInfoList(QStringList() << QStringLiteral("*.jar"),
                                                  QDir::Files, QDir::Name);
    std::printf("jars in dir: %d\n", jars.size());
    for (const QFileInfo &fi : jars)
    {
        std::printf("  jar: %s size=%lld baseMatch=%d\n",
                    fi.fileName().toUtf8().constData(),
                    static_cast<long long>(fi.size()),
                    fi.completeBaseName() == base ? 1 : 0);
        if (fi.completeBaseName() == base)
        {
            bestJar = fi.absoluteFilePath();
            break;
        }
        if (fi.size() > bestSize)
        {
            bestSize = fi.size();
            bestJar = fi.absoluteFilePath();
        }
    }
    if (bestJar.isEmpty())
    {
        std::printf("no jar found\n");
        return 2;
    }
    std::printf("using jar: %s\n", bestJar.toUtf8().constData());

    GameRegistry reg;
    const int n = reg.extractFromJar(bestJar);
    std::printf("extracted entries: %d blocks: %d\n", n, reg.blocks().size());

    QHash<QString, QString> cnMap;
    for (const auto &b : reg.blocks())
    {
        if (!b.chinese.isEmpty())
            cnMap.insert(b.english.toLower(), b.chinese.first());
    }
    std::printf("cnMap size: %d\n", cnMap.size());

    const QStringList probes = {
        QStringLiteral("minecraft:stone"),
        QStringLiteral("minecraft:oak_log"),
        QStringLiteral("minecraft:dirt"),
        QStringLiteral("minecraft:glass"),
        QStringLiteral("minecraft:bricks"),
        QStringLiteral("minecraft:cobblestone"),
    };
    for (const QString &id : probes)
    {
        const QString cn = cnMap.value(id);
        std::printf("  %s -> %s\n", id.toUtf8().constData(),
                    cn.isEmpty() ? "(empty)" : cn.toUtf8().constData());
    }

    int i = 0;
    for (auto it = cnMap.constBegin(); it != cnMap.constEnd() && i < 10; ++it, ++i)
        std::printf("  sample: %s => %s\n", it.key().toUtf8().constData(),
                    it.value().toUtf8().constData());

    return 0;
}
