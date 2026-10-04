/**
 * @file   SettingsManager.cpp
 * @brief  设置管理器类实现
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "SettingsManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "../platform.h"

SettingsManager *SettingsManager::m_instance = nullptr;
QMutex SettingsManager::m_instanceMutex;

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    m_settings = new QSettings("BlockBox", "BlockBox");
    m_configSettings = new QSettings(Platform::getConfigFilePath(), QSettings::IniFormat);
    initDefaultAuthServers();
    initDefaultInstanceFolders();
}

SettingsManager::~SettingsManager()
{
    delete m_settings;
    delete m_configSettings;
    m_instance = nullptr;
}

SettingsManager *SettingsManager::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new SettingsManager();
        }
    }
    return m_instance;
}

bool SettingsManager::isHomeButtonVisible() const
{
    return m_settings->value("navigation/homeButtonVisible", true).toBool();
}

void SettingsManager::setHomeButtonVisible(bool visible)
{
    m_settings->setValue("navigation/homeButtonVisible", visible);
}

bool SettingsManager::isResourcesButtonVisible() const
{
    return m_settings->value("navigation/resourcesButtonVisible", true).toBool();
}

void SettingsManager::setResourcesButtonVisible(bool visible)
{
    m_settings->setValue("navigation/resourcesButtonVisible", visible);
}

bool SettingsManager::isSettingsButtonVisible() const
{
    return m_settings->value("navigation/settingsButtonVisible", true).toBool();
}

void SettingsManager::setSettingsButtonVisible(bool visible)
{
    m_settings->setValue("navigation/settingsButtonVisible", visible);
}

QString SettingsManager::getAccountsFilePath() const
{
    return Platform::getDataDirectory() + "/accounts.json";
}

QList<AccountInfo> SettingsManager::loadAccountsFromFile() const
{
    QList<AccountInfo> accounts;
    QFile file(getAccountsFilePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return accounts;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray())
    {
        return accounts;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue &val : array)
    {
        QJsonObject obj = val.toObject();
        AccountInfo info;
        info.username = obj["username"].toString();
        info.type = obj["type"].toString();
        info.serverUrl = obj["serverUrl"].toString();
        info.isDefault = obj["isDefault"].toBool();
        info.accessToken = obj["accessToken"].toString();
        info.refreshToken = obj["refreshToken"].toString();
        info.uuid = obj["uuid"].toString();
        info.skinUrl = obj["skinUrl"].toString();
        accounts.append(info);
    }
    return accounts;
}

void SettingsManager::saveAccountsToFile(const QList<AccountInfo>& accounts) const
{
    QJsonArray array;
    for (const AccountInfo &info : accounts)
    {
        QJsonObject obj;
        obj["username"] = info.username;
        obj["type"] = info.type;
        obj["serverUrl"] = info.serverUrl;
        obj["isDefault"] = info.isDefault;
        obj["accessToken"] = info.accessToken;
        obj["refreshToken"] = info.refreshToken;
        obj["uuid"] = info.uuid;
        obj["skinUrl"] = info.skinUrl;
        array.append(obj);
    }

    QJsonDocument doc(array);
    QFile file(getAccountsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[SettingsManager] 账户文件写入失败:" << getAccountsFilePath()
                   << file.errorString();
        return;
    }
    file.write(doc.toJson());
    file.close();
}

QList<AccountInfo> SettingsManager::getAccounts() const
{
    QList<AccountInfo> accounts = loadAccountsFromFile();

    // 如果文件不存在，尝试从旧版 QSettings 迁移
    if (accounts.isEmpty())
    {
        int count = m_settings->beginReadArray("accounts");
        for (int i = 0; i < count; ++i) {
            m_settings->setArrayIndex(i);
            AccountInfo info;
            info.username = m_settings->value("username").toString();
            info.type = m_settings->value("type").toString();
            info.serverUrl = m_settings->value("serverUrl").toString();
            info.isDefault = m_settings->value("isDefault", false).toBool();
            info.accessToken = m_settings->value("accessToken").toString();
            info.refreshToken = m_settings->value("refreshToken").toString();
            info.uuid = m_settings->value("uuid").toString();
            info.skinUrl = m_settings->value("skinUrl").toString();
            accounts.append(info);
        }
        m_settings->endArray();
        // 迁移后保存到文件
        if (!accounts.isEmpty())
        {
            const_cast<SettingsManager *>(this)->saveAccountsToFile(accounts);
        }
    }

    return accounts;
}

void SettingsManager::saveAccountsToSettings(const QList<AccountInfo>& accounts)
{
    saveAccountsToFile(accounts);
    emit accountsChanged();
}

void SettingsManager::addAccount(const QString &username, const QString &type, const QString &serverUrl, bool isDefault)
{
    // 先获取现有账户
    QList<AccountInfo> accounts = getAccounts();
    
    // 检查是否已存在同名账户
    for (int i = 0; i < accounts.size(); ++i) {
        if (accounts[i].username == username) {
            return; // 账户已存在
        }
    }
    
    // 如果是默认账户，将其他账户的默认状态取消
    if (isDefault) {
        for (int i = 0; i < accounts.size(); ++i) {
            accounts[i].isDefault = false;
        }
    }
    
    // 添加新账户
    AccountInfo newAccount;
    newAccount.username = username;
    newAccount.type = type;
    newAccount.serverUrl = serverUrl;
    newAccount.isDefault = isDefault;
    accounts.append(newAccount);
    
    // 保存账户列表
    saveAccountsToSettings(accounts);
}

// 添加Microsoft账户的重载方法
void SettingsManager::addMicrosoftAccount(const QString &username, const QString &accessToken, const QString &refreshToken, const QString &uuid, const QString &skinUrl, bool isDefault)
{
    // 先获取现有账户
    QList<AccountInfo> accounts = getAccounts();
    
    // 检查是否已存在同名账户
    for (int i = 0; i < accounts.size(); ++i) {
        if (accounts[i].username == username) {
            return; // 账户已存在
        }
    }
    
    // 如果是默认账户，将其他账户的默认状态取消
    if (isDefault) {
        for (int i = 0; i < accounts.size(); ++i) {
            accounts[i].isDefault = false;
        }
    }
    
    // 添加新Microsoft账户
    AccountInfo newAccount;
    newAccount.username = username;
    newAccount.type = "Microsoft";
    newAccount.serverUrl = "";
    newAccount.isDefault = isDefault;
    newAccount.accessToken = accessToken;
    newAccount.refreshToken = refreshToken;
    newAccount.uuid = uuid;
    newAccount.skinUrl = skinUrl;
    accounts.append(newAccount);
    
    // 保存账户列表
    saveAccountsToSettings(accounts);
}

void SettingsManager::removeAccount(const QString &username)
{
    QList<AccountInfo> accounts = getAccounts();
    for (int i = 0; i < accounts.size(); ++i) {
        if (accounts[i].username == username) {
            accounts.removeAt(i);
            break;
        }
    }
    
    // 保存更新后的账户列表
    saveAccountsToSettings(accounts);
}

QList<AuthServerInfo> SettingsManager::getAuthServers() const
{
    QList<AuthServerInfo> servers;
    int count = m_settings->beginReadArray("authServers");
    servers.reserve(count);
    for (int i = 0; i < count; ++i) {
        m_settings->setArrayIndex(i);
        AuthServerInfo server;
        server.url = m_settings->value("url").toString();
        server.name = m_settings->value("name").toString();
        servers.append(server);
    }
    m_settings->endArray();
    return servers;
}

void SettingsManager::addAuthServer(const QString &url, const QString &name)
{
    QList<AuthServerInfo> servers = getAuthServers();
    
    // 检查是否已存在相同URL的服务器
    for (const AuthServerInfo &server : servers) {
        if (server.url == url) {
            return; // 服务器已存在
        }
    }
    
    // 添加新服务器
    AuthServerInfo newServer;
    newServer.url = url;
    newServer.name = name;
    servers.append(newServer);
    
    // 保存服务器列表
    m_settings->beginWriteArray("authServers");
    for (int i = 0; i < servers.size(); ++i) {
        m_settings->setArrayIndex(i);
        m_settings->setValue("url", servers[i].url);
        m_settings->setValue("name", servers[i].name);
    }
    m_settings->endArray();
}

void SettingsManager::removeAuthServer(const QString &url)
{
    QList<AuthServerInfo> servers = getAuthServers();
    for (int i = 0; i < servers.size(); ++i) {
        if (servers[i].url == url) {
            servers.removeAt(i);
            break;
        }
    }
    
    // 保存更新后的服务器列表
    m_settings->beginWriteArray("authServers");
    for (int i = 0; i < servers.size(); ++i) {
        m_settings->setArrayIndex(i);
        m_settings->setValue("url", servers[i].url);
        m_settings->setValue("name", servers[i].name);
    }
    m_settings->endArray();
}

void SettingsManager::initDefaultAuthServers()
{
    // 检查是否已经初始化过
    if (m_settings->value("authServersInitialized", false).toBool()) {
        return;
    }
    
    // 添加默认服务器
    addAuthServer("https://littleskin.cn/api/yggdrasil/", "LittleSkin");
    
    // 标记为已初始化
    m_settings->setValue("authServersInitialized", true);
}

QString SettingsManager::getJavaPath() const
{
    return m_settings->value("java/path", "auto").toString();
}

void SettingsManager::setJavaPath(const QString &path)
{
    m_settings->setValue("java/path", path);
}

QList<QPair<QString, QString>> SettingsManager::getJavaInstallations() const
{
    QList<QPair<QString, QString>> installations;

    QByteArray data = m_configSettings->value("java/installations").toByteArray();
    if (data.isEmpty())
    {
        return installations;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray())
    {
        return installations;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue& val : array)
    {
        QJsonObject obj = val.toObject();
        installations.append(qMakePair(obj["path"].toString(), obj["version"].toString()));
    }

    return installations;
}

void SettingsManager::setJavaInstallations(const QList<QPair<QString, QString>>& installations)
{
    QJsonArray array;
    for (const auto& inst : installations)
    {
        QJsonObject obj;
        obj["path"] = inst.first;
        obj["version"] = inst.second;
        array.append(obj);
    }

    QJsonDocument doc(array);
    m_configSettings->setValue("java/installations", doc.toJson(QJsonDocument::Compact));
    m_configSettings->sync();  // 立即刷新到磁盘确保配置持久化
}

bool SettingsManager::getAutoSelectJava() const
{
    return m_settings->value("java/autoSelect", true).toBool();
}

void SettingsManager::setAutoSelectJava(bool enabled)
{
    m_settings->setValue("java/autoSelect", enabled);
}

void SettingsManager::setProperty(const QString &key, const QVariant &value)
{
    m_settings->setValue(key, value);
}

QVariant SettingsManager::getProperty(const QString &key, const QVariant &defaultValue) const
{
    return m_settings->value(key, defaultValue);
}

void SettingsManager::removeProperty(const QString &key)
{
    m_settings->remove(key);
}

void SettingsManager::setDefaultAccount(const QString &username)
{
    QList<AccountInfo> accounts = getAccounts();
    for (int i = 0; i < accounts.size(); ++i) {
        accounts[i].isDefault = (accounts[i].username == username);
    }
    
    // 保存更新后的账户列表
    saveAccountsToSettings(accounts);
}

AccountInfo SettingsManager::getDefaultAccount() const
{
    QList<AccountInfo> accounts = getAccounts();
    for (const AccountInfo &info : accounts) {
        if (info.isDefault) {
            return info;
        }
    }
    
    // 如果没有默认账户，返回第一个账户
    if (!accounts.isEmpty()) {
        return accounts.first();
    }
    
    // 没有账户时返回空账户
    AccountInfo empty;
    return empty;
}

bool SettingsManager::accountExists(const QString &username) const
{
    QList<AccountInfo> accounts = getAccounts();
    for (const AccountInfo &info : accounts) {
        if (info.username == username) {
            return true;
        }
    }
    return false;
}

QList<InstanceFolderInfo> SettingsManager::getInstanceFolders() const
{
    QList<InstanceFolderInfo> folders;
    int count = m_settings->beginReadArray("instanceFolders");
    folders.reserve(count);
    for (int i = 0; i < count; ++i) {
        m_settings->setArrayIndex(i);
        InstanceFolderInfo info;
        info.name = m_settings->value("name").toString();
        info.path = m_settings->value("path").toString();
        info.isDefault = m_settings->value("isDefault", false).toBool();
        folders.append(info);
    }
    m_settings->endArray();
    return folders;
}

// 实例文件夹路径比较：统一为绝对路径，Windows 上文件系统不区分大小写，
// 忽略大小写以免同一文件夹因盘符/目录大小写差异被重复添加或无法移除
static bool sameInstanceFolderPath(const QString &a, const QString &b)
{
    const QString ka = QDir(a).absolutePath();
    const QString kb = QDir(b).absolutePath();
#ifdef Q_OS_WIN
    return ka.compare(kb, Qt::CaseInsensitive) == 0;
#else
    return ka == kb;
#endif
}

void SettingsManager::addInstanceFolder(const QString &name, const QString &path, bool isDefault)
{
    QList<InstanceFolderInfo> folders = getInstanceFolders();

    for (const InstanceFolderInfo &folder : folders) {
        if (sameInstanceFolderPath(folder.path, path)) {
            return;
        }
    }
    
    if (isDefault) {
        for (int i = 0; i < folders.size(); ++i) {
            folders[i].isDefault = false;
        }
    }
    
    InstanceFolderInfo newFolder;
    newFolder.name = name;
    newFolder.path = path;
    newFolder.isDefault = isDefault;
    folders.append(newFolder);
    
    m_settings->beginWriteArray("instanceFolders");
    for (int i = 0; i < folders.size(); ++i) {
        m_settings->setArrayIndex(i);
        m_settings->setValue("name", folders[i].name);
        m_settings->setValue("path", folders[i].path);
        m_settings->setValue("isDefault", folders[i].isDefault);
    }
    m_settings->endArray();
}

void SettingsManager::removeInstanceFolder(const QString &path)
{
    QList<InstanceFolderInfo> folders = getInstanceFolders();
    for (int i = 0; i < folders.size(); ++i) {
        if (sameInstanceFolderPath(folders[i].path, path)) {
            folders.removeAt(i);
            break;
        }
    }
    
    m_settings->beginWriteArray("instanceFolders");
    for (int i = 0; i < folders.size(); ++i) {
        m_settings->setArrayIndex(i);
        m_settings->setValue("name", folders[i].name);
        m_settings->setValue("path", folders[i].path);
        m_settings->setValue("isDefault", folders[i].isDefault);
    }
    m_settings->endArray();
}

void SettingsManager::setDefaultInstanceFolder(const QString &path)
{
    QList<InstanceFolderInfo> folders = getInstanceFolders();
    for (int i = 0; i < folders.size(); ++i) {
        folders[i].isDefault = sameInstanceFolderPath(folders[i].path, path);
    }
    
    m_settings->beginWriteArray("instanceFolders");
    for (int i = 0; i < folders.size(); ++i) {
        m_settings->setArrayIndex(i);
        m_settings->setValue("name", folders[i].name);
        m_settings->setValue("path", folders[i].path);
        m_settings->setValue("isDefault", folders[i].isDefault);
    }
    m_settings->endArray();
}

InstanceFolderInfo SettingsManager::getDefaultInstanceFolder() const
{
    QList<InstanceFolderInfo> folders = getInstanceFolders();
    for (const InstanceFolderInfo &info : folders) {
        if (info.isDefault) {
            return info;
        }
    }
    
    if (!folders.isEmpty()) {
        return folders.first();
    }
    
    InstanceFolderInfo empty;
    return empty;
}

QString SettingsManager::getDefaultInstancePath() const
{
    InstanceFolderInfo defaultFolder = getDefaultInstanceFolder();
    if (!defaultFolder.path.isEmpty()) {
        return defaultFolder.path;
    }
    QString appDir = QCoreApplication::applicationDirPath();
    return appDir + "/.minecraft";
}

QStringList SettingsManager::getInstanceCategories() const
{
    return m_settings->value("instanceCategories").toStringList();
}

void SettingsManager::addInstanceCategory(const QString &name)
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == tr("全部") || trimmed == tr("未分类")) {
        return;
    }
    QStringList cats = getInstanceCategories();
    if (cats.contains(trimmed)) {
        return;
    }
    cats.append(trimmed);
    m_settings->setValue("instanceCategories", cats);
}

void SettingsManager::removeInstanceCategory(const QString &name)
{
    QStringList cats = getInstanceCategories();
    if (!cats.removeAll(name)) {
        return;
    }
    m_settings->setValue("instanceCategories", cats);

    // 清理该分类下所有实例的归属映射
    QJsonObject map = QJsonDocument::fromJson(
        m_settings->value("instanceCategoryMap").toByteArray()).object();
    bool changed = false;
    for (const QString &key : map.keys()) {
        if (map.value(key).toString() == name) {
            map.remove(key);
            changed = true;
        }
    }
    if (changed) {
        m_settings->setValue("instanceCategoryMap",
            QJsonDocument(map).toJson(QJsonDocument::Compact));
    }
}

void SettingsManager::renameInstanceCategory(const QString &oldName, const QString &newName)
{
    QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || oldName == trimmed || trimmed == tr("全部") || trimmed == tr("未分类")) {
        return;
    }
    QStringList cats = getInstanceCategories();
    if (!cats.contains(oldName)) {
        return;
    }
    if (cats.contains(trimmed)) {
        return;
    }
    cats[cats.indexOf(oldName)] = trimmed;
    m_settings->setValue("instanceCategories", cats);

    // 同步更新实例归属映射
    QJsonObject map = QJsonDocument::fromJson(
        m_settings->value("instanceCategoryMap").toByteArray()).object();
    bool changed = false;
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it.value().toString() == oldName) {
            it.value() = trimmed;
            changed = true;
        }
    }
    if (changed) {
        m_settings->setValue("instanceCategoryMap",
            QJsonDocument(map).toJson(QJsonDocument::Compact));
    }
}

QString SettingsManager::getInstanceCategory(const QString &instancePath) const
{
    if (instancePath.isEmpty()) {
        return QString();
    }
    QJsonObject map = QJsonDocument::fromJson(
        m_settings->value("instanceCategoryMap").toByteArray()).object();
    return map.value(instancePath).toString();
}

void SettingsManager::setInstanceCategory(const QString &instancePath, const QString &category)
{
    if (instancePath.isEmpty()) {
        return;
    }
    QJsonObject map = QJsonDocument::fromJson(
        m_settings->value("instanceCategoryMap").toByteArray()).object();
    if (category.trimmed().isEmpty() || !getInstanceCategories().contains(category)) {
        map.remove(instancePath);
    } else {
        map[instancePath] = category;
    }
    m_settings->setValue("instanceCategoryMap",
        QJsonDocument(map).toJson(QJsonDocument::Compact));
}

bool SettingsManager::isVersionIsolationEnabled() const
{
    return m_settings->value("game/isolationEnabled", true).toBool();
}

void SettingsManager::setVersionIsolationEnabled(bool enabled)
{
    m_settings->setValue("game/isolationEnabled", enabled);
}

ForgeDownloadSource SettingsManager::getForgeDownloadSource() const
{
    return static_cast<ForgeDownloadSource>(m_settings->value("forgeDownloadSource", static_cast<int>(ForgeDownloadSource::BMCL)).toInt());
}

void SettingsManager::setForgeDownloadSource(ForgeDownloadSource source)
{
    m_settings->setValue("forgeDownloadSource", static_cast<int>(source));
}

FabricDownloadSource SettingsManager::getFabricDownloadSource() const
{
    return static_cast<FabricDownloadSource>(m_settings->value("fabricDownloadSource", static_cast<int>(FabricDownloadSource::BMCL)).toInt());
}

void SettingsManager::setFabricDownloadSource(FabricDownloadSource source)
{
    m_settings->setValue("fabricDownloadSource", static_cast<int>(source));
}

OptiFineDownloadSource SettingsManager::getOptiFineDownloadSource() const
{
    return static_cast<OptiFineDownloadSource>(m_settings->value("optiFineDownloadSource", static_cast<int>(OptiFineDownloadSource::BMCL)).toInt());
}

void SettingsManager::setOptiFineDownloadSource(OptiFineDownloadSource source)
{
    m_settings->setValue("optiFineDownloadSource", static_cast<int>(source));
}

NeoForgeDownloadSource SettingsManager::getNeoForgeDownloadSource() const
{
    return static_cast<NeoForgeDownloadSource>(m_settings->value("neoForgeDownloadSource", static_cast<int>(NeoForgeDownloadSource::BMCL)).toInt());
}

void SettingsManager::setNeoForgeDownloadSource(NeoForgeDownloadSource source)
{
    m_settings->setValue("neoForgeDownloadSource", static_cast<int>(source));
}

DownloadSource SettingsManager::getDownloadSource() const
{
    return static_cast<DownloadSource>(m_settings->value("download/source", 0).toInt());
}

void SettingsManager::setDownloadSource(DownloadSource source)
{
    m_settings->setValue("download/source", static_cast<int>(source));
}

int SettingsManager::getDownloadThreadCount() const
{
    return m_settings->value("download/threadCount", 8).toInt();
}

void SettingsManager::setDownloadThreadCount(int count)
{
    m_settings->setValue("download/threadCount", qBound(1, count, 128));
}

bool SettingsManager::isGithubAccelerationEnabled() const
{
    return getProperty("github_accel_enabled", false).toBool();
}

void SettingsManager::setGithubAccelerationEnabled(bool enabled)
{
    setProperty("github_accel_enabled", enabled);
}

int SettingsManager::getGithubAccelerationProxyIndex() const
{
    return getProperty("github_accel_proxy_index", 0).toInt();
}

void SettingsManager::setGithubAccelerationProxyIndex(int index)
{
    setProperty("github_accel_proxy_index", index);
}

QString SettingsManager::getGithubAccelerationCustomUrl() const
{
    return getProperty("github_accel_custom_url", "").toString();
}

void SettingsManager::setGithubAccelerationCustomUrl(const QString &url)
{
    setProperty("github_accel_custom_url", url);
}

bool SettingsManager::isAutoMemoryEnabled() const
{
    return m_settings->value("memory/autoAllocate", true).toBool();
}

void SettingsManager::setAutoMemoryEnabled(bool enabled)
{
    m_settings->setValue("memory/autoAllocate", enabled);
}

int SettingsManager::getMaxMemory() const
{
    return m_settings->value("memory/maxMemoryMb", DEFAULT_MAX_MEMORY_MB).toInt();
}

void SettingsManager::setMaxMemory(int maxMemoryMb)
{
    int clamped = qBound(MIN_MEMORY_MB, maxMemoryMb, MAX_MEMORY_MB);
    m_settings->setValue("memory/maxMemoryMb", clamped);
}

int SettingsManager::getMinMemory() const
{
    return m_settings->value("memory/minMemoryMb", DEFAULT_MIN_MEMORY_MB).toInt();
}

void SettingsManager::setMinMemory(int minMemoryMb)
{
    int clamped = qBound(MIN_MEMORY_MB, minMemoryMb, MAX_MEMORY_MB);
    m_settings->setValue("memory/minMemoryMb", clamped);
}

MemoryAllocationMode SettingsManager::getMemoryAllocationMode() const
{
    return static_cast<MemoryAllocationMode>(
        m_settings->value("memory/allocationMode",
            static_cast<int>(MemoryAllocationMode::Optimized)).toInt());
}

void SettingsManager::setMemoryAllocationMode(MemoryAllocationMode mode)
{
    m_settings->setValue("memory/allocationMode", static_cast<int>(mode));
}

QList<QPair<QString, QString>> SettingsManager::commandHistory() const
{
    QList<QPair<QString, QString>> history;
    int count = m_settings->beginReadArray("commandHistory");
    history.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        m_settings->setArrayIndex(i);
        QString input = m_settings->value("input").toString();
        QString output = m_settings->value("output").toString();
        history.append(qMakePair(input, output));
    }
    m_settings->endArray();
    return history;
}

void SettingsManager::saveCommandHistory(const QList<QPair<QString, QString>> &history)
{
    // 限制最多 20 条，最新在前；超出部分截断
    const int maxEntries = 20;
    int count = qMin(history.size(), maxEntries);

    m_settings->beginWriteArray("commandHistory", count);
    for (int i = 0; i < count; ++i)
    {
        m_settings->setArrayIndex(i);
        m_settings->setValue("input", history[i].first);
        m_settings->setValue("output", history[i].second);
    }
    m_settings->endArray();
}

QList<QPair<QString, bool>> SettingsManager::commandPackList() const
{
    QList<QPair<QString, bool>> packs;
    int count = m_settings->beginReadArray("commandPacks");
    packs.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        m_settings->setArrayIndex(i);
        QString filePath = m_settings->value("filePath").toString();
        bool enabled = m_settings->value("enabled", true).toBool();
        if (!filePath.isEmpty())
        {
            packs.append(qMakePair(filePath, enabled));
        }
    }
    m_settings->endArray();
    return packs;
}

void SettingsManager::saveCommandPackList(const QList<QPair<QString, bool>> &packs)
{
    int count = packs.size();
    m_settings->beginWriteArray("commandPacks", count);
    for (int i = 0; i < count; ++i)
    {
        m_settings->setArrayIndex(i);
        m_settings->setValue("filePath", packs[i].first);
        m_settings->setValue("enabled", packs[i].second);
    }
    m_settings->endArray();
}

void SettingsManager::initDefaultInstanceFolders()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString defaultPath = appDir + "/.minecraft";
    
    if (m_settings->value("instanceFoldersInitialized", false).toBool()) {
        QList<InstanceFolderInfo> folders = getInstanceFolders();
        int defaultIndex = -1;
        
        for (int i = 0; i < folders.size(); ++i) {
            if (folders[i].isDefault) {
                defaultIndex = i;
                break;
            }
        }
        
        if (defaultIndex >= 0 && folders[defaultIndex].path != defaultPath) {
            folders[defaultIndex].path = defaultPath;
            
            m_settings->beginWriteArray("instanceFolders");
            for (int i = 0; i < folders.size(); ++i) {
                m_settings->setArrayIndex(i);
                m_settings->setValue("name", folders[i].name);
                m_settings->setValue("path", folders[i].path);
                m_settings->setValue("isDefault", folders[i].isDefault);
            }
            m_settings->endArray();
        }
        return;
    }
    
    addInstanceFolder(tr("默认实例文件夹"), defaultPath, true);
    
    m_settings->setValue("instanceFoldersInitialized", true);
}

// ---------------------------------------------------------------------------
// Translation settings (翻译设置)
// ---------------------------------------------------------------------------

TranslationConfig SettingsManager::getTranslationConfig() const
{
    TranslationConfig config;
    config.source = static_cast<TranslationSource>(
        m_settings->value("translation/source", static_cast<int>(TranslationSource::AI)).toInt());
    config.deeplApiKey = m_settings->value("translation/deeplApiKey").toString();
    config.deeplFree = m_settings->value("translation/deeplFree", true).toBool();
    config.baiduAppId = m_settings->value("translation/baiduAppId").toString();
    config.baiduSecretKey = m_settings->value("translation/baiduSecretKey").toString();
    config.googleApiKey = m_settings->value("translation/googleApiKey").toString();
    config.customApiUrl = m_settings->value("translation/customApiUrl").toString();
    config.customApiKey = m_settings->value("translation/customApiKey").toString();
    config.customModel = m_settings->value("translation/customModel").toString();
    config.targetLanguage = m_settings->value("translation/targetLanguage", "zh").toString();
    config.sourceLanguage = m_settings->value("translation/sourceLanguage", "en").toString();
    return config;
}

void SettingsManager::setTranslationConfig(const TranslationConfig &config)
{
    m_settings->setValue("translation/source", static_cast<int>(config.source));
    m_settings->setValue("translation/deeplApiKey", config.deeplApiKey);
    m_settings->setValue("translation/deeplFree", config.deeplFree);
    m_settings->setValue("translation/baiduAppId", config.baiduAppId);
    m_settings->setValue("translation/baiduSecretKey", config.baiduSecretKey);
    m_settings->setValue("translation/googleApiKey", config.googleApiKey);
    m_settings->setValue("translation/customApiUrl", config.customApiUrl);
    m_settings->setValue("translation/customApiKey", config.customApiKey);
    m_settings->setValue("translation/customModel", config.customModel);
    m_settings->setValue("translation/targetLanguage", config.targetLanguage);
    m_settings->setValue("translation/sourceLanguage", config.sourceLanguage);
}
