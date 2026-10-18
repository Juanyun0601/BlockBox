#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <cstdio>

static bool looksChinese(const QString &text)
{
    for (const QChar &ch : text)
    {
        const ushort u = ch.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF)
            || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0x3000 && u <= 0x303F)
            || (u >= 0xFF00 && u <= 0xFFEF))
            return true;
    }
    return false;
}

static void applyLang(const QJsonObject &obj, QHash<QString, QString> &out)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        if (!it.key().startsWith(QLatin1String("block.")))
            continue;
        const QString value = it.value().toString();
        if (value.isEmpty() || !looksChinese(value))
            continue;
        const QString body = it.key().mid(6);
        const int firstDot = body.indexOf(QLatin1Char('.'));
        if (firstDot <= 0)
            continue;
        out.insert((body.left(firstDot) + QLatin1Char(':') + body.mid(firstDot + 1)).toLower(),
                   value);
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString instancePath = QString::fromLocal8Bit(argc > 1 ? argv[1] : "");
    if (instancePath.isEmpty())
    {
        std::printf("usage: cn_assets_probe <instancePath>\n");
        return 2;
    }

    // find assetIndex id
    const QString base = QFileInfo(instancePath).fileName();
    QString assetId;
    for (const QString &p : {instancePath + "/" + base + ".json",
                             instancePath + "/version.json"})
    {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        assetId = root.value("assetIndex").toObject().value("id").toString();
        if (!assetId.isEmpty())
            break;
    }
    std::printf("assetIndexId=%s\n", assetId.toUtf8().constData());

    const QString assetsRoot = instancePath + "/assets";
    const QString indexPath = assetsRoot + "/indexes/" + assetId + ".json";
    std::printf("indexPath exists=%d\n", QFile::exists(indexPath) ? 1 : 0);

    QFile idx(indexPath);
    if (!idx.open(QIODevice::ReadOnly))
        return 1;
    const QJsonObject objects = QJsonDocument::fromJson(idx.readAll()).object()
                                    .value("objects").toObject();
    const QString hash = objects.value("minecraft/lang/zh_cn.json").toObject()
                             .value("hash").toString();
    const QString objPath = assetsRoot + "/objects/" + hash.left(2) + "/" + hash;
    std::printf("zh_cn object exists=%d path=%s\n",
                QFile::exists(objPath) ? 1 : 0, objPath.toUtf8().constData());

    QFile zh(objPath);
    if (!zh.open(QIODevice::ReadOnly))
        return 1;
    QHash<QString, QString> map;
    applyLang(QJsonDocument::fromJson(zh.readAll()).object(), map);
    std::printf("cnMap size=%d\n", map.size());
    for (const QString &id : {QStringLiteral("minecraft:stone"),
                              QStringLiteral("minecraft:oak_log"),
                              QStringLiteral("minecraft:dirt")})
    {
        const QString cn = map.value(id);
        std::printf("%s -> %s\n", id.toUtf8().constData(),
                    cn.isEmpty() ? "(empty)" : cn.toUtf8().constData());
    }
    return 0;
}
