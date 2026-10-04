/**
 * @file   fake_settingsmanager.cpp
 * @brief  QuickLaunch 独立验证用伪 SettingsManager：仅实现属性读写（INI 存储）
 * @author BlockBox Team
 * @date   2026-09-06
 */
#include "utils/SettingsManager.h"

#include <QDir>
#include <QStandardPaths>

SettingsManager *SettingsManager::m_instance = nullptr;
QMutex SettingsManager::m_instanceMutex;

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    // 指向临时目录的 INI，避免污染真实用户设置
    m_settings = new QSettings(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                   + "/blockbox_ql_test_settings.ini", QSettings::IniFormat);
    m_configSettings = nullptr;
}

SettingsManager::~SettingsManager()
{
    delete m_settings;
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

void SettingsManager::removeProperty(const QString &key)
{
    m_settings->remove(key);
}
