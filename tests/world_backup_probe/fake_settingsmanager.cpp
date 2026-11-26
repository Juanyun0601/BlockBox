/**
 * @file   fake_settingsmanager.cpp
 * @brief  存档备份验证程序用的伪 SettingsManager：仅实现备份逻辑用到的读写
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "utils/SettingsManager.h"

#include <QSettings>
#include <QStandardPaths>

SettingsManager *SettingsManager::m_instance = nullptr;
QMutex SettingsManager::m_instanceMutex;

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    // 指向临时目录的 INI，避免污染真实用户设置
    m_settings = new QSettings(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                   + "/blockbox_world_backup_probe.ini", QSettings::IniFormat);
    m_settings->clear();
    m_configSettings = nullptr;
}

SettingsManager::~SettingsManager()
{
    delete m_settings;
    delete m_configSettings;
    m_instance = nullptr;
}

SettingsManager *SettingsManager::instance()
{
    if (!m_instance)
        m_instance = new SettingsManager();
    return m_instance;
}

QVariant SettingsManager::getProperty(const QString &key, const QVariant &defaultValue) const
{
    return m_settings->value(key, defaultValue);
}

void SettingsManager::setProperty(const QString &key, const QVariant &value)
{
    m_settings->setValue(key, value);
}

bool SettingsManager::isVersionIsolationEnabled() const
{
    // 验证程序按「版本隔离」跑：存档在实例目录的 saves/ 下
    return true;
}
