/**
 * 探针用 SettingsManager 桩：只提供 InstanceOverviewPage 用到的接口，
 * 避免拉入 MemoryAllocator / TranslationService / platform 依赖链。
 */
#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QHash>
#include <QString>
#include <QVariant>

class SettingsManager
{
public:
    static SettingsManager *instance()
    {
        static SettingsManager m;
        return &m;
    }

    void setProperty(const QString &key, const QVariant &value) { m_values.insert(key, value); }

    QVariant getProperty(const QString &key, const QVariant &defaultValue = QVariant()) const
    {
        return m_values.value(key, defaultValue);
    }

private:
    QHash<QString, QVariant> m_values;
};

#endif // SETTINGSMANAGER_H
