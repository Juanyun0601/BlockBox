/**
 * @file   PluginManager.cpp
 * @brief  插件管理器类实现
 * @author BlockBox Team
 * @date   2026-08-05
 */
#include "PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QTextStream>
#include <QUrl>
#include <QPointer>
#include <QtConcurrent>
#include <QtGlobal>

#include "utils/plugin/PluginZip.h"

namespace {

const QString kManifestName = QStringLiteral("plugin.json");
const QString kPluginSuffix = QStringLiteral("*.BlockBox");
const QString kSettingsOrg   = QStringLiteral("BlockBox");
const QString kSettingsApp   = QStringLiteral("Plugins");

/**
 * @brief 网络下载文件到本地（同步，带 30 秒超时）
 * @param url      下载地址
 * @param destPath 本地保存路径
 * @param error    输出参数：失败原因
 * @return true 成功
 */
bool downloadToFile(const QString &url, const QString &destPath, QString *error)
{
    QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(url)));
    request.setTransferTimeout(30000);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const bool ok = (reply->error() == QNetworkReply::NoError);
    if (!ok) {
        if (error) *error = QObject::tr("网络错误: %1").arg(reply->errorString());
        reply->deleteLater();
        return false;
    }

    QSaveFile file(destPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QObject::tr("无法写入临时文件: %1").arg(file.errorString());
        reply->deleteLater();
        return false;
    }
    file.write(reply->readAll());
    const bool committed = file.commit();
    reply->deleteLater();

    if (!committed) {
        if (error) *error = QObject::tr("写入临时文件失败");
        return false;
    }
    return true;
}

} // namespace

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
    refresh();
}

PluginManager *PluginManager::instance()
{
    static PluginManager s_instance;
    return &s_instance;
}

QString PluginManager::pluginsDir() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/BlockBox");
}

void PluginManager::ensurePluginsDir() const
{
    QDir dir;
    if (!dir.exists(pluginsDir()))
        dir.mkpath(pluginsDir());
}

void PluginManager::refresh()
{
    ensurePluginsDir();

    QList<PluginInfo> parsed;
    QDir dir(pluginsDir());
    const QFileInfoList entries = dir.entryInfoList(QStringList() << kPluginSuffix,
                                                    QDir::Files | QDir::Readable,
                                                    QDir::Name);
    for (const QFileInfo &fi : entries)
        parsed.append(parsePluginFile(fi.absoluteFilePath()));

    m_plugins = parsed;
    emit pluginsChanged();
}

PluginInfo PluginManager::parsePluginFile(const QString &filePath) const
{
    PluginInfo info;
    info.filePath = filePath;
    info.loaded = false;

    QString manifest;
    if (!PluginZip::extractEntryToString(filePath, kManifestName, manifest)) {
        info.loadError = QObject::tr("清单文件 plugin.json 缺失或无法读取");
        return info;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(manifest.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        info.loadError = QObject::tr("plugin.json 解析失败: %1").arg(parseErr.errorString());
        return info;
    }

    const QJsonObject obj = doc.object();
    info.id = obj.value(QStringLiteral("id")).toString().trimmed();
    info.name = obj.value(QStringLiteral("name")).toString().trimmed();
    info.version = obj.value(QStringLiteral("version")).toString().trimmed();
    info.author = obj.value(QStringLiteral("author")).toString().trimmed();
    info.description = obj.value(QStringLiteral("description")).toString().trimmed();
    info.icon = obj.value(QStringLiteral("icon")).toString().trimmed();
    info.entry = obj.value(QStringLiteral("entry")).toString().trimmed();
    info.apiVersion = obj.value(QStringLiteral("apiVersion")).toString().trimmed();
    if (info.apiVersion.isEmpty())
        info.apiVersion = QStringLiteral("1.0");

    // 插件类型（script / native）
    info.kind = obj.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (info.kind.isEmpty())
        info.kind = QStringLiteral("script");

    // 扩展元数据
    info.homepage = obj.value(QStringLiteral("homepage")).toString().trimmed();
    info.license = obj.value(QStringLiteral("license")).toString().trimmed();
    info.category = obj.value(QStringLiteral("category")).toString().trimmed();
    info.minApiVersion = obj.value(QStringLiteral("minApiVersion")).toString().trimmed();
    info.updateUrl = obj.value(QStringLiteral("updateUrl")).toString().trimmed();

    const QJsonArray tags = obj.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue &v : tags)
        info.tags.append(v.toString().trimmed());

    const QJsonArray deps = obj.value(QStringLiteral("dependencies")).toArray();
    for (const QJsonValue &v : deps)
        info.dependencies.append(v.toString().trimmed());

    // 能力权限声明（manifest.permissions）
    const QJsonArray perms = obj.value(QStringLiteral("permissions")).toArray();
    for (const QJsonValue &v : perms)
        info.permissions.append(v.toString().trimmed().toLower());

    const QJsonArray settingsArr = obj.value(QStringLiteral("settings")).toArray();
    for (const QJsonValue &v : settingsArr) {
        if (v.isObject()) {
            const PluginSettingItem item = PluginSettingItem::fromJson(v.toObject());
            if (item.isValid())
                info.settings.append(item);
        }
    }

    const QJsonArray commandsArr = obj.value(QStringLiteral("commands")).toArray();
    for (const QJsonValue &v : commandsArr) {
        if (v.isObject()) {
            const PluginCommand cmd = PluginCommand::fromJson(v.toObject());
            if (cmd.isValid())
                info.commands.append(cmd);
        }
    }

    // 内嵌界面定义（ui）
    const QJsonObject uiObj = obj.value(QStringLiteral("ui")).toObject();
    if (!uiObj.isEmpty()) {
        info.ui.style = uiObj.value(QStringLiteral("style")).toString();
        const QJsonArray fieldsArr = uiObj.value(QStringLiteral("fields")).toArray();
        for (const QJsonValue &v : fieldsArr) {
            if (v.isObject()) {
                const PluginUiField f = PluginUiField::fromJson(v.toObject());
                if (f.isValid())
                    info.ui.fields.append(f);
            }
        }
        const QJsonArray actionsArr = uiObj.value(QStringLiteral("actions")).toArray();
        for (const QJsonValue &v : actionsArr) {
            if (v.isObject()) {
                const PluginUiAction a = PluginUiAction::fromJson(v.toObject());
                if (a.isValid())
                    info.ui.actions.append(a);
            }
        }
    }

    // 启动器样式贡献（manifest.style：字符串内联 QSS 或 { qss, file } 对象）
    info.style = PluginStyleContribution::fromJson(obj.value(QStringLiteral("style")));

    if (info.id.isEmpty() || info.name.isEmpty()) {
        info.loadError = QObject::tr("清单缺少 id 或 name 字段");
        return info;
    }

    info.loaded = true;
    return info;
}

PluginInfo PluginManager::pluginAt(int index) const
{
    if (index < 0 || index >= m_plugins.size())
        return PluginInfo();
    return m_plugins[index];
}

PluginInfo PluginManager::pluginById(const QString &id) const
{
    for (const PluginInfo &info : m_plugins) {
        if (info.id == id)
            return info;
    }
    return PluginInfo();
}

bool PluginManager::isNative(const QString &id) const
{
    return pluginById(id).kind == QStringLiteral("native");
}

bool PluginManager::nativeAvailable(const QString &id) const
{
    const PluginInfo info = pluginById(id);
    return info.loaded
        && info.kind == QStringLiteral("native")
        && isEnabled(id);
}

QString PluginManager::pluginIconPath(const PluginInfo &info) const
{
    if (!info.loaded || info.icon.isEmpty() || info.filePath.isEmpty())
        return QString();

    const QFileInfo src(info.filePath);
    const QString cacheDir = pluginsDir() + QStringLiteral("/.icons");
    QDir dir;
    if (!dir.exists(cacheDir))
        dir.mkpath(cacheDir);

    const QString cachePath = cacheDir + QStringLiteral("/")
        + info.id + QStringLiteral("_")
        + QString::number(src.lastModified().toSecsSinceEpoch())
        + QStringLiteral(".png");
    if (QFileInfo::exists(cachePath))
        return cachePath;

    QByteArray data;
    if (!PluginZip::extractEntryToMemory(info.filePath, info.icon, data))
        return QString();

    QImage img;
    if (!img.loadFromData(data) || img.isNull())
        return QString();

    if (!img.save(cachePath, "PNG"))
        return QString();
    return cachePath;
}

PluginInfo PluginManager::inspectPluginFile(const QString &srcFile) const
{
    if (srcFile.isEmpty())
        return PluginInfo();
    return parsePluginFile(srcFile);
}

QString PluginManager::collectScriptsContent(const QString &srcFile) const
{
    if (srcFile.isEmpty())
        return QString();

    bool ok = false;
    const QStringList entries = PluginZip::listEntries(srcFile, &ok);
    if (!ok)
        return QString();

    const QStringList kScriptExts = {
        QStringLiteral("ps1"), QStringLiteral("bat"), QStringLiteral("cmd"),
        QStringLiteral("vbs"), QStringLiteral("js"),  QStringLiteral("py"),
        QStringLiteral("sh"),
    };

    QStringList parts;
    for (const QString &entry : entries) {
        const QString ext = QFileInfo(entry).suffix().toLower();
        if (!kScriptExts.contains(ext))
            continue;
        QString content;
        if (PluginZip::extractEntryToString(srcFile, entry, content))
            parts << content;
    }
    return parts.join(QLatin1Char('\n'));
}

bool PluginManager::importPlugin(const QString &srcFile, QString *error)
{
    ensurePluginsDir();

    QFileInfo srcInfo(srcFile);
    if (!srcInfo.exists()) {
        if (error) *error = tr("源文件不存在: %1").arg(srcFile);
        return false;
    }
    const bool isBlockBox = srcInfo.suffix().compare(QStringLiteral("BlockBox"), Qt::CaseInsensitive) == 0;
    if (!isBlockBox) {
        if (error) *error = tr("仅支持 .BlockBox 格式的插件文件");
        return false;
    }

    // 目标文件名：与源文件名一致，冲突时自动加序号
    QString targetName = srcInfo.fileName();
    QString destPath = pluginsDir() + QStringLiteral("/") + targetName;
    int counter = 1;
    while (QFileInfo::exists(destPath)) {
        destPath = pluginsDir() + QStringLiteral("/")
                 + srcInfo.completeBaseName() + QStringLiteral("(%1).BlockBox").arg(counter++);
    }

    if (!QFile::copy(srcFile, destPath)) {
        if (error) *error = tr("复制插件文件失败: %1").arg(destPath);
        return false;
    }

    refresh();
    return true;
}

bool PluginManager::removePlugin(const QString &id, QString *error)
{
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].id != id)
            continue;
        const QString path = m_plugins[i].filePath;
        QFile f(path);
        if (!f.remove()) {
            if (error) *error = tr("删除插件文件失败: %1").arg(f.errorString());
            return false;
        }
        refresh();
        return true;
    }
    if (error) *error = tr("未找到插件: %1").arg(id);
    return false;
}

// ==================== 启用 / 禁用 ====================

bool PluginManager::isEnabled(const QString &id) const
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(QStringLiteral("enabled/%1").arg(id), true).toBool();
}

void PluginManager::setEnabled(const QString &id, bool enabled)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(QStringLiteral("enabled/%1").arg(id), enabled);
    emit pluginEnabledChanged(id, enabled);
}

// ==================== 导出 / 解包 ====================

bool PluginManager::exportPlugin(const QString &id, const QString &destDir,
                                 QString *outPath, QString *error)
{
    const PluginInfo info = pluginById(id);
    if (info.filePath.isEmpty()) {
        if (error) *error = tr("未找到插件: %1").arg(id);
        return false;
    }

    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        if (error) *error = tr("无法创建目标目录: %1").arg(destDir);
        return false;
    }

    const QFileInfo src(info.filePath);
    QString destPath = QDir::cleanPath(destDir) + QStringLiteral("/") + src.fileName();
    int counter = 1;
    while (QFileInfo::exists(destPath)) {
        destPath = QDir::cleanPath(destDir) + QStringLiteral("/")
                 + src.completeBaseName() + QStringLiteral("(%1).BlockBox").arg(counter++);
    }

    if (!QFile::copy(info.filePath, destPath)) {
        if (error) *error = tr("导出插件失败");
        return false;
    }
    if (outPath) *outPath = destPath;
    return true;
}

bool PluginManager::extractPlugin(const QString &id, const QString &destDir, QString *error)
{
    const PluginInfo info = pluginById(id);
    if (info.filePath.isEmpty()) {
        if (error) *error = tr("未找到插件: %1").arg(id);
        return false;
    }

    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        if (error) *error = tr("无法创建目标目录: %1").arg(destDir);
        return false;
    }

    bool ok = false;
    const int count = PluginZip::extractAllToDir(info.filePath, destDir, &ok);
    if (!ok || count <= 0) {
        if (error) *error = tr("解包失败，插件包可能已损坏");
        return false;
    }
    return true;
}

// ==================== 校验 / 依赖 ====================

int PluginManager::compareVersions(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int va = (i < pa.size()) ? pa[i].toInt() : 0;
        const int vb = (i < pb.size()) ? pb[i].toInt() : 0;
        if (va != vb)
            return (va > vb) ? 1 : -1;
    }
    return 0;
}

bool PluginManager::isApiCompatible(const PluginInfo &info, QString *reason) const
{
    // 插件要求的宿主 API 最低版本 > 宿主当前版本 → 不兼容
    if (!info.minApiVersion.isEmpty()) {
        if (compareVersions(info.minApiVersion, hostApiVersion()) > 0) {
            if (reason) *reason = tr("插件需要宿主 API ≥ %1，当前为 %2")
                                  .arg(info.minApiVersion, hostApiVersion());
            return false;
        }
    }
    // 插件声明的 apiVersion 高于宿主 → 视为不兼容（防止运行未来 API）
    if (!info.apiVersion.isEmpty()) {
        if (compareVersions(info.apiVersion, hostApiVersion()) > 0) {
            if (reason) *reason = tr("插件基于 API %1 编写，当前宿主为 %2")
                                  .arg(info.apiVersion, hostApiVersion());
            return false;
        }
    }
    return true;
}

QStringList PluginManager::missingDependencies(const PluginInfo &info) const
{
    QStringList missing;
    for (const QString &dep : info.dependencies) {
        // 支持 "id@>=1.2" 写法，取 id 部分
        QString depId = dep;
        const int at = dep.indexOf(QLatin1Char('@'));
        if (at >= 0)
            depId = dep.left(at).trimmed();

        const PluginInfo depInfo = pluginById(depId);
        if (depInfo.filePath.isEmpty() || !depInfo.loaded || !isEnabled(depId))
            missing.append(dep);
    }
    return missing;
}

// ==================== 设置读写 ====================

QString PluginManager::settingKey(const QString &id, const QString &key) const
{
    return QStringLiteral("settings/%1/%2").arg(id, key);
}

QString PluginManager::pluginSetting(const PluginInfo &info, const QString &key,
                                     const QString &defaultValue) const
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    const QString stored = settings.value(settingKey(info.id, key)).toString();
    if (!stored.isNull())
        return stored;
    // 未存储时回退清单中的默认值
    const PluginSettingItem item = info.settingItem(key);
    return item.isValid() ? item.defaultValue : defaultValue;
}

void PluginManager::setPluginSetting(const PluginInfo &info, const QString &key, const QString &value)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(settingKey(info.id, key), value);
}

// ==================== 启动器样式贡献 ====================

QString PluginManager::pluginStyleQss(const PluginInfo &info) const
{
    if (!info.loaded || !info.style.isValid() || info.filePath.isEmpty())
        return QString();

    QStringList parts;
    const QString inlineQss = info.style.qss.trimmed();
    if (!inlineQss.isEmpty())
        parts << inlineQss;

    // 读取包内 .qss 样式文件（UTF-8）
    if (!info.style.styleFile.isEmpty()) {
        QString fileContent;
        if (PluginZip::extractEntryToString(info.filePath, info.style.styleFile, fileContent)
            && !fileContent.trimmed().isEmpty()) {
            parts << fileContent.trimmed();
        }
    }

    if (parts.isEmpty())
        return QString();

    return QStringLiteral("\n/* === plugin style: %1 === */\n%2")
            .arg(info.id, parts.join(QLatin1Char('\n')));
}

QStringList PluginManager::activePluginStyles() const
{
    QStringList styles;
    for (const PluginInfo &info : m_plugins) {
        if (!isEnabled(info.id))
            continue;
        const QString qss = pluginStyleQss(info);
        if (!qss.isEmpty())
            styles << qss;
    }
    return styles;
}

// ==================== 更新 ====================

bool PluginManager::updatePlugin(const QString &id, QString *error)
{
    const PluginInfo info = pluginById(id);
    if (info.filePath.isEmpty()) {
        if (error) *error = tr("未找到插件: %1").arg(id);
        return false;
    }
    if (info.updateUrl.isEmpty()) {
        if (error) *error = tr("该插件未提供 updateUrl 更新地址");
        return false;
    }

    // 下载到插件目录的临时文件
    const QString tmpPath = info.filePath + QStringLiteral(".tmp");
    if (!downloadToFile(info.updateUrl, tmpPath, error))
        return false;

    return finalizePluginUpdate(info, tmpPath, error);
}

bool PluginManager::finalizePluginUpdate(const PluginInfo &info, const QString &tmpPath, QString *error)
{
    // 校验下载结果：能解析出清单且 id 一致
    PluginInfo remote = parsePluginFile(tmpPath);
    if (!remote.loaded || remote.id != info.id) {
        QFile::remove(tmpPath);
        if (error) *error = tr("下载的文件不是有效的 %1 插件").arg(info.name);
        return false;
    }
    // 版本比较：新版本不低于当前才替换
    if (compareVersions(remote.version, info.version) < 0) {
        QFile::remove(tmpPath);
        if (error) *error = tr("远程版本（%1）不高于当前版本（%2）")
                              .arg(remote.version.isEmpty() ? tr("未知") : remote.version,
                                   info.version.isEmpty() ? tr("未知") : info.version);
        return false;
    }

    if (!QFile::remove(info.filePath)) {
        QFile::remove(tmpPath);
        if (error) *error = tr("无法替换现有插件文件（可能被占用）");
        return false;
    }
    if (!QFile::rename(tmpPath, info.filePath)) {
        if (error) *error = tr("替换插件文件失败");
        return false;
    }

    refresh();
    return true;
}

bool PluginManager::updatePluginAsync(const QString &id)
{
    const PluginInfo info = pluginById(id);
    if (info.filePath.isEmpty()) {
        emit pluginUpdateFinished(id, false, tr("未找到插件: %1").arg(id));
        return false;
    }
    if (info.updateUrl.isEmpty()) {
        emit pluginUpdateFinished(id, false, tr("该插件未提供 updateUrl 更新地址"));
        return false;
    }
    if (m_updatingIds.contains(id)) {
        return true; // 该插件正在更新中，忽略重复触发
    }
    m_updatingIds.insert(id);

    const QString updateUrl = info.updateUrl;
    const QString tmpPath = info.filePath + QStringLiteral(".tmp");
    QPointer<PluginManager> guard(this);
    (void)QtConcurrent::run([guard, id, updateUrl, tmpPath]() {
        // 仅网络下载在工作线程执行；校验与文件替换回到主线程，
        // 避免与 UI 侧的插件列表读取产生竞争
        QString error;
        const bool downloaded = guard ? downloadToFile(updateUrl, tmpPath, &error) : false;
        if (!guard) {
            QFile::remove(tmpPath);
            return;
        }
        QMetaObject::invokeMethod(guard, [guard, id, tmpPath, downloaded, error]() {
            if (!guard)
                return;
            guard->m_updatingIds.remove(id);
            bool success = downloaded;
            QString finalError = error;
            if (success) {
                const PluginInfo current = guard->pluginById(id);
                if (current.filePath.isEmpty()) {
                    success = false;
                    finalError = guard->tr("未找到插件: %1").arg(id);
                } else {
                    success = guard->finalizePluginUpdate(current, tmpPath, &finalError);
                }
            } else {
                QFile::remove(tmpPath);
            }
            emit guard->pluginUpdateFinished(id, success, finalError);
        }, Qt::QueuedConnection);
    });
    return true;
}

QString PluginManager::sanitizeId(const QString &name) const
{
    QString id;
    for (const QChar &c : name.trimmed()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-'))
            id.append(c.isSpace() ? QLatin1Char('_') : c);
    }
    if (id.isEmpty())
        id = QStringLiteral("my_plugin");
    return id.toLower();
}

bool PluginManager::createTemplate(const QString &name, const QString &id, const QString &version,
                                   const QString &author, const QString &description,
                                   const QString &parentDir, QString &outDir, QString *error)
{
    const QString dirName = id.trimmed().isEmpty() ? sanitizeId(name) : id.trimmed();
    const QString templateRoot = QDir::cleanPath(parentDir) + QStringLiteral("/") + dirName;

    QDir dir;
    if (QFileInfo::exists(templateRoot)) {
        if (error) *error = tr("目标目录已存在: %1").arg(templateRoot);
        return false;
    }
    if (!dir.mkpath(templateRoot + QStringLiteral("/assets"))) {
        if (error) *error = tr("创建模板目录失败");
        return false;
    }

    // 1. plugin.json 清单
    QJsonObject manifest;
    manifest.insert(QStringLiteral("id"), dirName);
    manifest.insert(QStringLiteral("name"), name.trimmed().isEmpty() ? dirName : name.trimmed());
    manifest.insert(QStringLiteral("version"), version.trimmed().isEmpty()
                        ? QStringLiteral("1.0.0") : version.trimmed());
    manifest.insert(QStringLiteral("author"), author.trimmed());
    manifest.insert(QStringLiteral("description"), description.trimmed());
    manifest.insert(QStringLiteral("icon"), QStringLiteral("assets/icon.png"));
    manifest.insert(QStringLiteral("entry"), QStringLiteral("main.js"));
    manifest.insert(QStringLiteral("apiVersion"), QStringLiteral("1.0"));
    // 扩展字段示例（可按需填写）
    manifest.insert(QStringLiteral("category"), QStringLiteral("功能"));
    manifest.insert(QStringLiteral("homepage"), QString());
    manifest.insert(QStringLiteral("license"), QStringLiteral("MIT"));
    manifest.insert(QStringLiteral("minApiVersion"), QStringLiteral("1.0"));
    manifest.insert(QStringLiteral("updateUrl"), QString());
    manifest.insert(QStringLiteral("tags"), QJsonArray());
    manifest.insert(QStringLiteral("dependencies"), QJsonArray());

    // 设置项示例（宿主据此生成设置表单）
    {
        QJsonObject settingObj;
        settingObj.insert(QStringLiteral("key"), QStringLiteral("greeting"));
        settingObj.insert(QStringLiteral("label"), QStringLiteral("启用问候"));
        settingObj.insert(QStringLiteral("type"), QStringLiteral("bool"));
        settingObj.insert(QStringLiteral("default"), true);
        QJsonArray settingsArr;
        settingsArr.append(settingObj);
        manifest.insert(QStringLiteral("settings"), settingsArr);
    }

    QFile manifestFile(templateRoot + QStringLiteral("/plugin.json"));
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = tr("写入 plugin.json 失败");
        return false;
    }
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    manifestFile.close();

    // 2. main.js 入口示例
    QFile jsFile(templateRoot + QStringLiteral("/main.js"));
    if (jsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream ts(&jsFile);
        ts << "// " << manifest.value("name").toString()
           << " - BlockBox 插件入口示例\n"
           << "// 插件在启动时加载本文件，API 由宿主启动器提供。\n"
           << "function onEnable(api) {\n"
           << "    api.log('插件已启用: " << dirName << "');\n"
           << "}\n\n"
           << "function onDisable(api) {\n"
           << "    api.log('插件已停用: " << dirName << "');\n"
           << "}\n";
        jsFile.close();
    }

    // 3. README.md 说明
    QFile readmeFile(templateRoot + QStringLiteral("/README.md"));
    if (readmeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream ts(&readmeFile);
        ts << "# " << manifest.value("name").toString() << "\n\n"
           << "- **ID**: " << dirName << "\n"
           << "- **版本**: " << manifest.value("version").toString() << "\n"
           << "- **作者**: " << manifest.value("author").toString() << "\n"
           << "- **描述**: " << manifest.value("description").toString() << "\n\n"
           << "## 打包为 .BlockBox\n\n"
           << "在启动器「插件 - 制作插件」中点击「打包为 BlockBox 文件」，"
              "即可将本目录压缩为 .BlockBox 插件包（本质是 zip，后缀不同）。\n\n"
           << "## 目录结构\n\n"
           << "```\n"
           << dirName << "/\n"
           << "├── plugin.json   # 插件清单（包内必需，保存插件信息）\n"
           << "├── main.js       # 插件入口脚本\n"
           << "├── README.md     # 说明文档\n"
           << "└── assets/\n"
           << "    └── icon.png  # 插件图标\n"
           << "```\n";
        readmeFile.close();
    }

    // 4. 默认图标 assets/icon.png（128x128 主题色块 + 字母）
    QImage icon(128, 128, QImage::Format_ARGB32);
    icon.fill(Qt::transparent);
    {
        QPainter p(&icon);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(0x3B, 0x82, 0xF6));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(4, 4, 120, 120, 24, 24);
        p.setPen(Qt::white);
        QFont font;
        font.setPixelSize(72);
        font.setBold(true);
        p.setFont(font);
        const QString letter = dirName.left(1).toUpper();
        p.drawText(icon.rect(), Qt::AlignCenter, letter);
    }
    if (!icon.save(templateRoot + QStringLiteral("/assets/icon.png"), "PNG")) {
        if (error) *error = tr("生成默认图标失败");
        return false;
    }

    outDir = templateRoot;
    return true;
}

bool PluginManager::packageToBlockBox(const QString &templateDir, QString &outPath, QString *error)
{
    ensurePluginsDir();

    QFileInfo tplInfo(templateDir);
    if (!tplInfo.isDir()) {
        if (error) *error = tr("模板目录不存在: %1").arg(templateDir);
        return false;
    }

    // 输出到插件目录，文件名取模板目录名
    const QString baseName = tplInfo.fileName();
    QString destPath = pluginsDir() + QStringLiteral("/") + baseName + QStringLiteral(".BlockBox");
    int counter = 1;
    while (QFileInfo::exists(destPath)) {
        destPath = pluginsDir() + QStringLiteral("/")
                 + baseName + QStringLiteral("(%1).BlockBox").arg(counter++);
    }

    bool ok = false;
    const int count = PluginZip::zipDirectory(templateDir, destPath, &ok);
    if (!ok || count < 0) {
        if (error) *error = tr("打包失败，请检查模板目录");
        QFile::remove(destPath);
        return false;
    }

    outPath = destPath;
    refresh();
    return true;
}
