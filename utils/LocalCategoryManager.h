/**
 * @file   LocalCategoryManager.h
 * @brief  本地资源分类管理器 - 按实例、按资源类型独立持久化用户自定义分类
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 功能：用户可为实例内的各类本地资源（模组/资源包/光影包/存档/截图/数据包等）
 * 手动创建分类，并把资源归入指定分类。
 * 持久化文件: <instancePath>/blockbox/categories.json
 * 数据结构:
 * {
 *   "types": {
 *     "mods": {
 *       "categories": [ { "id": "...", "name": "辅助类", "createdAt": 123 } ],
 *       "assignments": [ { "file": "mod.jar", "categoryId": "..." } ]
 *     },
 *     "resourcepacks": { ... }
 *   }
 * }
 */
#ifndef LOCALCATEGORYMANAGER_H
#define LOCALCATEGORYMANAGER_H

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief 本地资源分类
 */
struct LocalResourceCategory
{
    QString id;
    QString name;
    qint64 createdAt = 0;
};

/**
 * @brief 本地资源分类管理器
 *
 * 非单例：每个实例独立实例化。通过 setResourceType() 切换当前资源类型范围
 * （如 "mods" / "resourcepacks" / "shaderpacks" / "saves" / "screenshots"），
 * 各类型的分类与关联互不干扰。
 * 关联键为资源目录下的文件名（忽略 .disabled 后缀），这样模组被禁用
 * （文件重命名）后分类仍能保持。
 */
class LocalCategoryManager : public QObject
{
    Q_OBJECT

public:
    explicit LocalCategoryManager(QObject *parent = nullptr);

    /** 设置实例路径并加载该实例的分类数据 */
    void setInstancePath(const QString &path);
    QString instancePath() const { return m_instancePath; }

    /** 切换当前资源类型范围（mods/resourcepacks/...），切换后自动加载对应数据 */
    void setResourceType(const QString &type);
    QString resourceType() const { return m_resourceType; }

    /** 当前资源类型下的分类列表 */
    QList<LocalResourceCategory> categories() const;
    /** 分类名 → 分类 id（不存在返回空） */
    QString categoryId(const QString &name) const;
    /** 分类 id → 分类名（不存在返回空） */
    QString categoryName(const QString &categoryId) const;

    /** 指定资源文件（资源目录文件名）所属分类 id（未分类返回空） */
    QString categoryOf(const QString &fileName) const;
    /** 指定资源文件所属分类名称（未分类返回空） */
    QString categoryNameOf(const QString &fileName) const;
    /** 指定分类下的资源文件列表 */
    QStringList filesInCategory(const QString &categoryId) const;

    /** 将资源归入分类（categoryId 为空则清除分类） */
    bool assignCategory(const QString &fileName, const QString &categoryId);

    /** 新建分类，返回新分类 id；名称已存在则返回已有分类 id */
    QString addCategory(const QString &name);
    bool renameCategory(const QString &categoryId, const QString &newName);
    bool deleteCategory(const QString &categoryId);

    /** 归一化资源文件名（去掉 .disabled 后缀） */
    static QString normalizeFileName(const QString &fileName);

    /** 按资源类型返回推荐的分类名称预设（mods/resourcepacks/...） */
    static QStringList suggestedNames(const QString &type);

signals:
    void categoriesChanged();
    void assignmentsChanged();

private:
    struct TypeData
    {
        QList<LocalResourceCategory> categories;
        QMap<QString, QString> assignments; // 归一化文件名 → 分类 id
    };

    void load();
    void save();
    TypeData &currentData();

    QString m_instancePath;
    QString m_resourceType;               // 当前资源类型范围
    QMap<QString, TypeData> m_types;      // 各资源类型的数据
};

#endif // LOCALCATEGORYMANAGER_H
