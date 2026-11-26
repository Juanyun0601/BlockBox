/**
 * @file   DshPluginInstaller.cpp
 * @brief  「方块盒子桥接」插件装进 DSH 的实现
 * @author BlockBox Team
 * @date   2026-11-26
 */
#include "utils/dsh/DshPluginInstaller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace {

/** 插件包内需要释放的文件，顺序即释放顺序。 */
const QStringList &payloadList()
{
    static const QStringList files = {
        QStringLiteral("package.json"),
        QStringLiteral("cordis.patch.yml"),
        QStringLiteral("index.js"),
        QStringLiteral("icon.svg"),
        QStringLiteral("README.md"),
        QStringLiteral("locale/zh.json"),
        QStringLiteral("locale/en.json"),
        QStringLiteral("docs/DETAIL.md"),
        QStringLiteral("docs/DETAIL.zh.md"),
        QStringLiteral("docs/preview.svg"),
    };
    return files;
}

bool readJsonObject(const QString &path, QJsonObject *object, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *errorMessage = QStringLiteral("读不到 %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        *errorMessage = QStringLiteral("%1 不是合法的 JSON（%2）").arg(path, parseError.errorString());
        return false;
    }

    *object = document.object();
    return true;
}

bool writeJsonObject(const QString &path, const QJsonObject &object, QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        *errorMessage = QStringLiteral("写不了 %1（文件被占用或没有权限）").arg(path);
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        *errorMessage = QStringLiteral("保存 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

/** 删掉 node_modules 里的旧副本；pnpm 建的软链接只拆链接，不碰它指向的目录。 */
bool removeInstalledCopy(const QString &target, QString *errorMessage)
{
    const QFileInfo info(target);
    if (info.isSymLink())
    {
        if (!QFile::remove(target))
        {
            *errorMessage = QStringLiteral("删不掉旧链接：%1").arg(target);
            return false;
        }
        return true;
    }
    if (!info.exists())
    {
        return true;
    }
    if (info.isFile())
    {
        if (!QFile::remove(target))
        {
            *errorMessage = QStringLiteral("删不掉旧文件：%1").arg(target);
            return false;
        }
        return true;
    }
    if (!QDir(target).removeRecursively())
    {
        *errorMessage = QStringLiteral("删不掉旧插件目录：%1").arg(target);
        return false;
    }
    return true;
}

bool copyPayloadFiles(const QString &payloadDir, const QString &targetDir, QString *errorMessage)
{
    for (const QString &relative : payloadList())
    {
        const QString sourcePath = QDir(payloadDir).filePath(relative);
        const QString destinationPath = QDir(targetDir).filePath(relative);
        if (!QFileInfo::exists(sourcePath))
        {
            *errorMessage = QStringLiteral("插件目录缺少 %1").arg(relative);
            return false;
        }
        QDir().mkpath(QFileInfo(destinationPath).absolutePath());
        if (QFileInfo::exists(destinationPath) && !QFile::remove(destinationPath))
        {
            *errorMessage = QStringLiteral("覆盖不了 %1").arg(destinationPath);
            return false;
        }
        if (!QFile::copy(sourcePath, destinationPath))
        {
            *errorMessage = QStringLiteral("复制失败：%1").arg(destinationPath);
            return false;
        }
    }
    return true;
}

/**
 * @brief 改 profile 的 package.json：依赖 + 启用清单
 * @param payloadDir 安装时写进 link: 依赖的插件目录
 */
bool editManifest(const QString &profileDir, bool install, const QString &payloadDir, QString *errorMessage)
{
    const QString manifestPath = QDir(profileDir).filePath(QStringLiteral("package.json"));
    QJsonObject root;
    if (!readJsonObject(manifestPath, &root, errorMessage))
    {
        return false;
    }

    const QString packageName = QString::fromLatin1(DshPluginInstaller::kPackageName);
    QJsonObject dependencies = root.value(QStringLiteral("dependencies")).toObject();
    QJsonObject dsh = root.value(QStringLiteral("dsh")).toObject();
    QJsonObject profile = dsh.value(QStringLiteral("profile")).toObject();
    const QJsonArray bundles = profile.value(QStringLiteral("bundles")).toArray();

    QJsonArray nextBundles;
    bool listed = false;
    for (const QJsonValue &value : bundles)
    {
        if (value.toString() == packageName)
        {
            listed = true;
            if (!install)
            {
                continue; // 卸载：从启用清单里删掉
            }
        }
        nextBundles.append(value);
    }

    if (install)
    {
        dependencies.insert(packageName,
                            QStringLiteral("link:%1").arg(QDir::fromNativeSeparators(payloadDir)));
        if (!listed)
        {
            nextBundles.append(packageName);
        }
    }
    else
    {
        dependencies.remove(packageName);
    }

    profile.insert(QStringLiteral("bundles"), nextBundles);
    dsh.insert(QStringLiteral("profile"), profile);
    root.insert(QStringLiteral("dsh"), dsh);
    root.insert(QStringLiteral("dependencies"), dependencies);

    // 第一次动之前留一份原样备份，便于手动回滚。
    if (install)
    {
        const QString backupPath = manifestPath + QStringLiteral(".blockbox-backup");
        if (!QFileInfo::exists(backupPath))
        {
            QFile::copy(manifestPath, backupPath);
        }
    }

    return writeJsonObject(manifestPath, root, errorMessage);
}

} // namespace

namespace DshPluginInstaller {

QString defaultHome()
{
    const QString fromEnv = qEnvironmentVariable("DSH_HOME").trimmed();
    if (!fromEnv.isEmpty())
    {
        return QDir::fromNativeSeparators(fromEnv);
    }
    return QDir(QDir::homePath()).filePath(QStringLiteral(".dsh"));
}

QStringList payloadFiles()
{
    return payloadList();
}

QVector<Profile> detectProfiles(const QString &home)
{
    const QString root = home.trimmed().isEmpty() ? defaultHome() : home;
    QVector<Profile> profiles;

    QDir profilesDir(QDir(root).filePath(QStringLiteral("profiles")));
    if (!profilesDir.exists())
    {
        return profiles;
    }

    const QFileInfoList entries =
        profilesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries)
    {
        const QString directory = entry.absoluteFilePath();
        if (!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("package.json"))))
        {
            continue;
        }
        Profile profile;
        profile.name = entry.fileName();
        profile.dir = directory;
        profile.installed = isInstalled(directory, &profile.version);
        profiles.append(profile);
    }

    // desktop 是 DSH 桌面端在用的配置，排最前；其余按最近改过的排。
    std::sort(profiles.begin(), profiles.end(),
              [](const Profile &left, const Profile &right) {
                  const bool leftDesktop = left.name == QStringLiteral("desktop");
                  const bool rightDesktop = right.name == QStringLiteral("desktop");
                  if (leftDesktop != rightDesktop)
                  {
                      return leftDesktop;
                  }
                  const QDateTime leftTime =
                      QFileInfo(QDir(left.dir).filePath(QStringLiteral("package.json"))).lastModified();
                  const QDateTime rightTime =
                      QFileInfo(QDir(right.dir).filePath(QStringLiteral("package.json"))).lastModified();
                  if (leftTime != rightTime)
                  {
                      return leftTime > rightTime;
                  }
                  return left.name < right.name;
              });
    return profiles;
}

bool writePayload(const QString &payloadDir, QString *errorMessage, QStringList *log)
{
    if (payloadDir.trimmed().isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("插件目录为空");
        }
        return false;
    }

    QDir target(payloadDir);
    if (!target.exists() && !target.mkpath(QStringLiteral(".")))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("建不了目录：%1").arg(payloadDir);
        }
        return false;
    }

    for (const QString &relative : payloadList())
    {
        QFile source(QStringLiteral(":/resources/dsh-plugin/") + relative);
        if (!source.open(QIODevice::ReadOnly))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("安装包缺少文件：%1").arg(relative);
            }
            return false;
        }

        const QString destinationPath = target.filePath(relative);
        QDir().mkpath(QFileInfo(destinationPath).absolutePath());

        QSaveFile destination(destinationPath);
        if (!destination.open(QIODevice::WriteOnly))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("写不了 %1").arg(destinationPath);
            }
            return false;
        }
        destination.write(source.readAll());
        if (!destination.commit())
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("保存 %1 失败").arg(destinationPath);
            }
            return false;
        }
        if (log)
        {
            log->append(QStringLiteral("释放 %1").arg(relative));
        }
    }
    return true;
}

bool installInto(const QString &profileDir, const QString &payloadDir,
                 QString *errorMessage, QStringList *log)
{
    if (profileDir.trimmed().isEmpty() || payloadDir.trimmed().isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("profile 目录或插件目录为空");
        }
        return false;
    }

    QDir profile(profileDir);
    if (!QFileInfo::exists(profile.filePath(QStringLiteral("package.json"))))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("这里不是 DSH 配置目录：%1").arg(profileDir);
        }
        return false;
    }

    const QString target = profile.filePath(
        QStringLiteral("node_modules/%1").arg(QString::fromLatin1(kPackageName)));
    if (!removeInstalledCopy(target, errorMessage))
    {
        return false;
    }
    if (!QDir().mkpath(target))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("建不了 %1").arg(target);
        }
        return false;
    }
    if (!copyPayloadFiles(payloadDir, target, errorMessage))
    {
        return false;
    }
    if (log)
    {
        log->append(QStringLiteral("复制插件到 %1").arg(target));
    }

    if (!editManifest(profileDir, true, payloadDir, errorMessage))
    {
        return false;
    }
    if (log)
    {
        log->append(QStringLiteral("更新 %1").arg(profile.filePath(QStringLiteral("package.json"))));
    }
    return true;
}

bool uninstallFrom(const QString &profileDir, QString *errorMessage)
{
    QDir profile(profileDir);
    if (!QFileInfo::exists(profile.filePath(QStringLiteral("package.json"))))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("这里不是 DSH 配置目录：%1").arg(profileDir);
        }
        return false;
    }

    const QString target = profile.filePath(
        QStringLiteral("node_modules/%1").arg(QString::fromLatin1(kPackageName)));
    if (!removeInstalledCopy(target, errorMessage))
    {
        return false;
    }
    return editManifest(profileDir, false, QString(), errorMessage);
}

bool isInstalled(const QString &profileDir, QString *installedVersion)
{
    if (profileDir.trimmed().isEmpty())
    {
        return false;
    }

    const QDir profile(profileDir);
    QJsonObject root;
    QString ignored;
    if (!readJsonObject(profile.filePath(QStringLiteral("package.json")), &root, &ignored))
    {
        return false;
    }
    if (!root.value(QStringLiteral("dependencies")).toObject().contains(
            QString::fromLatin1(kPackageName)))
    {
        return false;
    }

    const QString target = profile.filePath(
        QStringLiteral("node_modules/%1").arg(QString::fromLatin1(kPackageName)));
    QJsonObject pluginManifest;
    if (!readJsonObject(QDir(target).filePath(QStringLiteral("package.json")), &pluginManifest, &ignored))
    {
        return false;
    }
    if (installedVersion)
    {
        *installedVersion = pluginManifest.value(QStringLiteral("version")).toString();
    }
    return true;
}

} // namespace DshPluginInstaller
