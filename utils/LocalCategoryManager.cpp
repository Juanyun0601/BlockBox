/**
 * @file   LocalCategoryManager.cpp
 * @brief  本地资源分类管理器实现
 * @author BlockBox Team
 * @date   2026-08-18
 */
#include "LocalCategoryManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

QString LocalCategoryManager::normalizeFileName(const QString &fileName)
{
    QString name = QFileInfo(fileName).fileName();
    if (name.endsWith(".disabled", Qt::CaseInsensitive))
        name.chop(9);
    return name;
}

QStringList LocalCategoryManager::suggestedNames(const QString &type)
{
    const QString t = type.toLower();
    if (t == QStringLiteral("mods"))
    {
        return { tr("辅助类"), tr("优化类"), tr("前置类"), tr("玩法类"),
                 tr("汉化类"), tr("地图类"), tr("外观类"), tr("其他") };
    }
    if (t == QStringLiteral("resourcepacks"))
    {
        return { tr("界面"), tr("方块材质"), tr("生物材质"),
                 tr("高清新版"), tr("卡通"), tr("写实") };
    }
    if (t == QStringLiteral("shaderpacks"))
    {
        return { tr("光影"), tr("写实光影"), tr("卡通光影"),
                 tr("低配优化"), tr("原版增强") };
    }
    if (t == QStringLiteral("saves"))
    {
        return { tr("生存"), tr("创造"), tr("建筑"),
                 tr("红石"), tr("地图"), tr("服务器") };
    }
    if (t == QStringLiteral("screenshots"))
    {
        return { tr("场景"), tr("建筑"), tr("皮肤"), tr("教程"), tr("其他") };
    }
    if (t == QStringLiteral("datapacks"))
    {
        return { tr("机制"), tr("配方"), tr("结构"), tr("玩法"), tr("其他") };
    }
    return { tr("常用"), tr("辅助"), tr("美化"), tr("优化"), tr("其他") };
}

LocalCategoryManager::LocalCategoryManager(QObject *parent)
    : QObject(parent)
{
}

void LocalCategoryManager::setInstancePath(const QString &path)
{
    m_instancePath = path;
    load();
}

void LocalCategoryManager::setResourceType(const QString &type)
{
    if (m_resourceType == type)
        return;
    m_resourceType = type;
    emit categoriesChanged();
}

LocalCategoryManager::TypeData &LocalCategoryManager::currentData()
{
    return m_types[m_resourceType];
}

QList<LocalResourceCategory> LocalCategoryManager::categories() const
{
    return m_types.value(m_resourceType).categories;
}

QString LocalCategoryManager::categoryId(const QString &name) const
{
    const QString target = name.trimmed();
    const TypeData data = m_types.value(m_resourceType);
    for (const LocalResourceCategory &c : data.categories)
    {
        if (c.name == target)
            return c.id;
    }
    return QString();
}

QString LocalCategoryManager::categoryName(const QString &categoryId) const
{
    const TypeData data = m_types.value(m_resourceType);
    for (const LocalResourceCategory &c : data.categories)
    {
        if (c.id == categoryId)
            return c.name;
    }
    return QString();
}

QString LocalCategoryManager::categoryOf(const QString &fileName) const
{
    return m_types.value(m_resourceType).assignments.value(normalizeFileName(fileName));
}

QString LocalCategoryManager::categoryNameOf(const QString &fileName) const
{
    return categoryName(categoryOf(fileName));
}

QStringList LocalCategoryManager::filesInCategory(const QString &categoryId) const
{
    QStringList result;
    const QMap<QString, QString> &assignments = m_types.value(m_resourceType).assignments;
    for (auto it = assignments.constBegin(); it != assignments.constEnd(); ++it)
    {
        if (it.value() == categoryId)
            result.append(it.key());
    }
    return result;
}

bool LocalCategoryManager::assignCategory(const QString &fileName, const QString &categoryId)
{
    const QString key = normalizeFileName(fileName);
    if (key.isEmpty())
        return false;

    TypeData &data = currentData();

    if (categoryId.isEmpty())
    {
        if (!data.assignments.contains(key))
            return false;
        data.assignments.remove(key);
        save();
        emit assignmentsChanged();
        return true;
    }

    // 分类必须存在
    if (categoryName(categoryId).isEmpty())
        return false;

    if (data.assignments.value(key) == categoryId)
        return false;

    data.assignments.insert(key, categoryId);
    save();
    emit assignmentsChanged();
    return true;
}

QString LocalCategoryManager::addCategory(const QString &name)
{
    QString finalName = name.trimmed();
    if (finalName.isEmpty())
        return QString();

    // 名称去重：已存在则直接返回已有分类 id
    QString existing = categoryId(finalName);
    if (!existing.isEmpty())
        return existing;

    LocalResourceCategory category;
    category.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    category.name = finalName;
    category.createdAt = QDateTime::currentMSecsSinceEpoch();
    currentData().categories.append(category);

    save();
    emit categoriesChanged();
    return category.id;
}

bool LocalCategoryManager::renameCategory(const QString &categoryId, const QString &newName)
{
    QString finalName = newName.trimmed();
    if (finalName.isEmpty())
        return false;

    for (LocalResourceCategory &c : currentData().categories)
    {
        if (c.id == categoryId)
        {
            if (c.name == finalName)
                return false;
            c.name = finalName;
            save();
            emit categoriesChanged();
            return true;
        }
    }
    return false;
}

bool LocalCategoryManager::deleteCategory(const QString &categoryId)
{
    TypeData &data = currentData();

    for (int i = 0; i < data.categories.size(); ++i)
    {
        if (data.categories[i].id == categoryId)
        {
            data.categories.removeAt(i);

            // 清除该分类下的资源关联
            bool assignmentRemoved = false;
            auto it = data.assignments.begin();
            while (it != data.assignments.end())
            {
                if (it.value() == categoryId)
                {
                    it = data.assignments.erase(it);
                    assignmentRemoved = true;
                }
                else
                {
                    ++it;
                }
            }

            save();
            emit categoriesChanged();
            if (assignmentRemoved)
                emit assignmentsChanged();
            return true;
        }
    }
    return false;
}

void LocalCategoryManager::load()
{
    m_types.clear();

    const QString filePath = m_instancePath + "/blockbox/categories.json";
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    const QJsonObject typesObj = root.value("types").toObject();

    for (auto it = typesObj.constBegin(); it != typesObj.constEnd(); ++it)
    {
        if (!it.value().isObject())
            continue;

        const QJsonObject typeObj = it.value().toObject();
        TypeData data;

        const QJsonArray catArr = typeObj.value("categories").toArray();
        for (const QJsonValue &v : catArr)
        {
            if (!v.isObject())
                continue;
            const QJsonObject obj = v.toObject();
            LocalResourceCategory category;
            category.id = obj["id"].toString();
            category.name = obj["name"].toString();
            category.createdAt = static_cast<qint64>(obj["createdAt"].toDouble());
            if (!category.id.isEmpty() && !category.name.isEmpty())
                data.categories.append(category);
        }

        const QJsonArray assignArr = typeObj.value("assignments").toArray();
        for (const QJsonValue &v : assignArr)
        {
            if (!v.isObject())
                continue;
            const QJsonObject obj = v.toObject();
            const QString file = normalizeFileName(obj["file"].toString());
            const QString categoryId = obj["categoryId"].toString();
            if (!file.isEmpty() && !categoryId.isEmpty())
                data.assignments.insert(file, categoryId);
        }

        m_types.insert(it.key(), data);
    }

    emit categoriesChanged();
    emit assignmentsChanged();
}

void LocalCategoryManager::save()
{
    QJsonObject root;
    QJsonObject typesObj;

    for (auto it = m_types.constBegin(); it != m_types.constEnd(); ++it)
    {
        const TypeData &data = it.value();

        QJsonObject typeObj;

        QJsonArray catArr;
        for (const LocalResourceCategory &c : data.categories)
        {
            QJsonObject obj;
            obj["id"] = c.id;
            obj["name"] = c.name;
            obj["createdAt"] = static_cast<double>(c.createdAt);
            catArr.append(obj);
        }
        typeObj["categories"] = catArr;

        QJsonArray assignArr;
        for (auto aIt = data.assignments.constBegin(); aIt != data.assignments.constEnd(); ++aIt)
        {
            QJsonObject obj;
            obj["file"] = aIt.key();
            obj["categoryId"] = aIt.value();
            assignArr.append(obj);
        }
        typeObj["assignments"] = assignArr;

        typesObj[it.key()] = typeObj;
    }
    root["types"] = typesObj;

    const QString filePath = m_instancePath + "/blockbox/categories.json";
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}
