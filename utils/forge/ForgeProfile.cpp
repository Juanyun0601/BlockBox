#include "ForgeProfile.h"

#include <QDebug>
#include <QRegularExpression>

namespace forge {

ForgeArtifact ForgeArtifact::fromName(const QString &name)
{
    ForgeArtifact artifact;
    artifact.name = name;

    QString pattern = R"(^([^:]+):([^:]+):([^:@]+)(?::([^:@]+))?(?:@([^:]+))?$)";
    QRegularExpression re(pattern);
    auto match = re.match(name);

    if (match.hasMatch())
    {
        artifact.group = match.captured(1);
        artifact.artifact = match.captured(2);
        artifact.version = match.captured(3);
        artifact.classifier = match.captured(4);
        artifact.extension = match.captured(5);
    }

    if (artifact.extension.isEmpty())
    {
        artifact.extension = "jar";
    }

    return artifact;
}

QString ForgeArtifact::toPath() const
{
    QString path = group;
    path.replace('.', '/');
    path += "/" + artifact + "/" + version + "/" + toFileName();
    return path;
}

QString ForgeArtifact::toFileName() const
{
    QString fileName = artifact + "-" + version;
    if (!classifier.isEmpty())
    {
        fileName += "-" + classifier;
    }
    fileName += "." + extension;
    return fileName;
}

ForgeLibrary ForgeLibrary::fromJson(const QJsonObject &obj)
{
    ForgeLibrary lib;
    lib.name = obj["name"].toString();
    lib.url = obj["url"].toString();

    if (obj.contains("downloads") && obj["downloads"].isObject())
    {
        QJsonObject downloads = obj["downloads"].toObject();
        if (downloads.contains("artifact") && downloads["artifact"].isObject())
        {
            QJsonObject artifact = downloads["artifact"].toObject();
            lib.path = artifact["path"].toString();
            lib.sha1 = artifact["sha1"].toString();
            lib.size = artifact["size"].toVariant().toLongLong();
        }
    }

    QJsonArray candidates = obj["candidateUrls"].toArray();
    for (const auto &candidate : candidates)
    {
        lib.candidateUrls.append(candidate.toString());
    }

    QJsonObject natives = obj["natives"].toObject();
    lib.isNative = !natives.isEmpty();

    return lib;
}

ForgeInstallProfile ForgeInstallProfile::fromJson(const QJsonObject &obj)
{
    ForgeInstallProfile profile;
    profile.spec = obj["spec"].toInt();
    profile.minecraft = obj["minecraft"].toString();
    profile.json = obj["json"].toString();
    profile.path = obj["path"].toString();
    profile.version = obj["version"].toString();

    profile.libraries = obj["libraries"].toArray();

    QJsonArray processors = obj["processors"].toArray();
    for (const auto &proc : processors)
    {
        QJsonObject procObj = proc.toObject();
        ForgeProcessor processor;
        processor.jar = procObj["jar"].toString();

        QJsonArray classpath = procObj["classpath"].toArray();
        for (const auto &cp : classpath)
        {
            processor.classpath.append(cp.toString());
        }

        processor.args = procObj["args"].toArray();

        QJsonObject outputs = procObj["outputs"].toObject();
        for (auto it = outputs.begin(); it != outputs.end(); ++it)
        {
            processor.outputs.insert(it.key(), it.value().toString());
        }

        QJsonArray sides = procObj["sides"].toArray();
        for (const auto &side : sides)
        {
            processor.sides.append(side.toString());
        }

        profile.processors.append(processor);
    }

    QJsonObject data = obj["data"].toObject();
    for (auto it = data.begin(); it != data.end(); ++it)
    {
        profile.data.insert(it.key(), it.value());
    }

    return profile;
}

} // namespace forge