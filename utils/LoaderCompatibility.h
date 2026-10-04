/**
 * @file   LoaderCompatibility.h
 * @brief  加载器兼容性检查类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef LOADERCOMPATIBILITY_H
#define LOADERCOMPATIBILITY_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>

struct LoaderCompatibilityResult
{
    bool canInstall;
    bool hasConflict;
    bool needsDependency;
    QString conflictLoaderName;
    QString dependencyLoaderName;
    QString message;
};

class LoaderCompatibility
{
public:
    static LoaderCompatibility* instance();

    LoaderCompatibilityResult checkCompatibility(const QString& loaderName, const QStringList& installedLoaders) const;
    QStringList getIncompatibleLoaders(const QString& loaderName) const;
    QStringList getRequiredDependencies(const QString& loaderName) const;
    bool hasConflict(const QString& loader1, const QString& loader2) const;

private:
    LoaderCompatibility();
    void initCompatibilityRules();
    void initDependencyRules();

    QMap<QString, QSet<QString>> m_incompatibleMap;
    QMap<QString, QStringList> m_dependencyMap;

    static LoaderCompatibility* m_instance;
};

#endif // LOADERCOMPATIBILITY_H
