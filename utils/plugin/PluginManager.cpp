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
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
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
 * @brief 把插件 id 清洗成可安全拼路径的形式
 *
 * 插件 id 来自清单（不可信），非 [A-Za-z0-9_-] 一律替换为下划线，
 * 防止 ../ 之类越出临时目录或数据目录。
 */
QString safePluginIdForPath(const QString &pluginId)
{
    QString safe = pluginId;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                 QStringLiteral("_"));
    if (safe.isEmpty())
        safe = QStringLiteral("plugin");
    return safe;
}

/** 端口是否空闲（能用它 listen 上就算空闲） */
bool isTcpPortFree(int port)
{
    if (port <= 0 || port > 65535)
        return false;
    QTcpServer probe;
    const bool ok = probe.listen(QHostAddress::LocalHost, static_cast<quint16>(port));
    if (ok)
        probe.close();
    return ok;
}

/** 让系统挑一个空闲的本地端口：优先 17891~17999 这段（好记、好配防火墙），
 *  都被占用时退回系统随机分配；失败返回 0 */
int pickFreeTcpPort()
{
    for (int port = 17891; port <= 17999; ++port) {
        if (isTcpPortFree(port))
            return port;
    }
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const int port = int(probe.serverPort());
    probe.close();
    return port;
}

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
    for (const QFileInfo &fi : entries) {
        PluginInfo info = parsePluginFile(fi.absoluteFilePath());
        // 投影方块编辑器已升级为启动器内置功能，历史遗留的插件包不再纳入插件管理
        if (info.id == QLatin1String("projection_editor"))
            continue;
        parsed.append(info);
    }

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

    // 插件私有变量（manifest.vars）：供 aiTools[].http 的 {plugin.<键>} 占位符
    // 与插件脚本的环境变量使用，值不会进入模型上下文
    const QJsonObject varsObj = obj.value(QStringLiteral("vars")).toObject();
    for (auto it = varsObj.constBegin(); it != varsObj.constEnd(); ++it) {
        const QString key = it.key().trimmed();
        if (key.isEmpty())
            continue;
        // 只接受基础类型，对象/数组直接忽略（避免意料之外的嵌套值被当路径用）
        if (it.value().isObject() || it.value().isArray() || it.value().isNull())
            continue;
        info.vars.insert(key, it.value().toVariant().toString());
    }

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
        // 「更多」页入口（ui.moreEntries）：同插件内 id 去重，需声明 ui:page 权限才生效
        const QJsonArray moreEntriesArr = uiObj.value(QStringLiteral("moreEntries")).toArray();
        for (const QJsonValue &v : moreEntriesArr) {
            if (!v.isObject())
                continue;
            PluginPageEntry e = PluginPageEntry::fromJson(v.toObject());
            if (!e.isValid() || info.moreEntry(e.id).isValid())
                continue;
            e.pluginId = info.id;
            info.ui.moreEntries.append(e);
        }
    }

    // 启动器样式贡献（manifest.style：字符串内联 QSS 或 { qss, file } 对象）
    info.style = PluginStyleContribution::fromJson(obj.value(QStringLiteral("style")));

    // AI 工具定义（manifest.aiTools）：需同时声明权限 ai:tool 才会生效
    const QJsonArray aiToolsArr = obj.value(QStringLiteral("aiTools")).toArray();
    for (const QJsonValue &v : aiToolsArr) {
        if (v.isObject()) {
            const PluginAiTool tool = PluginAiTool::fromJson(v.toObject());
            if (tool.isValid())
                info.aiTools.append(tool);
        }
    }

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

QString PluginManager::pluginEntryIconPath(const PluginInfo &info,
                                           const PluginPageEntry &entry) const
{
    if (!info.loaded || entry.icon.isEmpty() || info.filePath.isEmpty())
        return pluginIconPath(info);

    // 保留原扩展名：SVG 要按矢量渲染并跟随主题色，PNG 直接当图片用，
    // 所以这里存原始字节，不做统一转码
    const QString ext = QFileInfo(entry.icon).suffix().toLower();
    const QStringList kIconExts = {
        QStringLiteral("png"), QStringLiteral("svg"), QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("webp"),
    };
    if (!kIconExts.contains(ext))
        return pluginIconPath(info);

    const QFileInfo src(info.filePath);
    const QString cacheDir = pluginsDir() + QStringLiteral("/.icons");
    QDir dir;
    if (!dir.exists(cacheDir))
        dir.mkpath(cacheDir);

    const QString cachePath = cacheDir + QStringLiteral("/")
        + info.id + QStringLiteral("_entry_")
        + QString::number(static_cast<qulonglong>(qHash(entry.icon)))
        + QLatin1Char('_')
        + QString::number(src.lastModified().toSecsSinceEpoch())
        + QLatin1Char('.') + ext;
    if (QFileInfo::exists(cachePath))
        return cachePath;

    QByteArray data;
    if (!PluginZip::extractEntryToMemory(info.filePath, entry.icon, data))
        return pluginIconPath(info);

    QFile out(cachePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || out.write(data) != data.size()) {
        out.close();
        QFile::remove(cachePath);
        return pluginIconPath(info);
    }
    out.close();
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

// ==================== 「更多」页入口 ====================

bool PluginManager::providesMoreEntries(const PluginInfo &info) const
{
    if (!info.loaded || info.ui.moreEntries.isEmpty())
        return false;
    // 声明了入口却没申请 ui:page 权限：不展示，避免插件在用户不知情时往更多页塞入口；
    // 插件详情页会给出「未声明 ui:page 权限，暂不生效」提示
    if (!info.hasPagePermission())
        return false;
    return info.validMoreEntryCount() > 0;
}

QList<PluginPageEntry> PluginManager::enabledMoreEntries() const
{
    QList<PluginPageEntry> entries;
    for (const PluginInfo &info : m_plugins) {
        if (!providesMoreEntries(info) || !isEnabled(info.id))
            continue;
        for (const PluginPageEntry &entry : info.ui.moreEntries) {
            if (!entry.isValid())
                continue;
            PluginPageEntry e = entry;
            e.pluginId = info.id;
            entries.append(e);
        }
    }
    return entries;
}

// ==================== 插件私有变量 ====================

QString PluginManager::pluginDataDir(const QString &pluginId) const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/plugin-data/") + safePluginIdForPath(pluginId);
    QDir().mkpath(base);
    return base;
}

QMap<QString, QString> PluginManager::resolvedPluginVars(const PluginInfo &info) const
{
    QMap<QString, QString> vars;
    if (info.id.isEmpty())
        return vars;

    // 1) 清单里写死的键（"auto" 交给保留键处理）
    for (auto it = info.vars.constBegin(); it != info.vars.constEnd(); ++it) {
        if (it.value().trimmed().compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
            continue;
        vars.insert(it.key(), it.value());
    }

    // 2) 保留键：data / app 无状态，直接算
    if (!vars.contains(QStringLiteral("data")))
        vars.insert(QStringLiteral("data"), pluginDataDir(info.id));
    if (!vars.contains(QStringLiteral("app")))
        vars.insert(QStringLiteral("app"), QCoreApplication::applicationDirPath());

    // 3) 保留键：token / port 需要持久化，首次用到时生成
    QSettings settings(kSettingsOrg, kSettingsApp);
    const QString prefix = QStringLiteral("pluginVars/%1/").arg(info.id);

    if (!vars.contains(QStringLiteral("token"))) {
        QString token = settings.value(prefix + QStringLiteral("token")).toString().trimmed();
        if (token.size() < 16) {
            token.clear();
            for (int i = 0; i < 4; ++i) {
                token += QString::number(QRandomGenerator::global()->generate(), 16)
                             .rightJustified(8, QLatin1Char('0'));
            }
            settings.setValue(prefix + QStringLiteral("token"), token);
        }
        vars.insert(QStringLiteral("token"), token);
    }

    if (!vars.contains(QStringLiteral("port"))) {
        // 端口只分配一次并固定下来：插件自己的服务会长期占用这个端口，
        // 所以这里不能"被占用就重挑"（否则每次调用都会漂到新端口，
        // 和正在监听的服务对不上）。清单里想要固定端口时直接写数字。
        int port = settings.value(prefix + QStringLiteral("port")).toInt();
        if (port <= 0) {
            // QTcpServer 需要线程事件分发器，脚本工具却跑在工作线程里，
            // 所以只在主线程做真实探测；工作线程按插件 id 派生一个候选值，
            // 一旦落库就固定，后续调用（无论哪个线程）都读同一个值。
            if (QCoreApplication::instance()
                && QThread::currentThread() == QCoreApplication::instance()->thread()) {
                port = pickFreeTcpPort();
            } else {
                port = 17891 + int(qHash(info.id) % 100u);
            }
            if (port > 0)
                settings.setValue(prefix + QStringLiteral("port"), port);
        }
        if (port > 0)
            vars.insert(QStringLiteral("port"), QString::number(port));
    }

    return vars;
}

QString PluginManager::substitutePluginVars(const QString &text,
                                            const QMap<QString, QString> &vars,
                                            QStringList *unknownKeys)
{
    if (text.isEmpty() || !text.contains(QStringLiteral("{plugin.")))
        return text;

    static const QRegularExpression re(
        QStringLiteral("\\{plugin\\.([A-Za-z0-9_-]+)\\}"));

    QString out;
    out.reserve(text.size());
    int last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString key = m.captured(1);
        out += text.mid(last, m.capturedStart() - last);
        const auto hit = vars.constFind(key);
        if (hit != vars.constEnd()) {
            out += hit.value();
        } else {
            // 未声明的键保留原文，交给调用方决定报错还是忽略
            out += m.captured(0);
            if (unknownKeys && !unknownKeys->contains(key))
                unknownKeys->append(key);
        }
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

// ==================== AI 工具接口 ====================
bool PluginManager::providesAiTools(const PluginInfo &info) const
{
    if (!info.loaded || info.aiTools.isEmpty())
        return false;
    // 声明了工具却没申请 ai:tool 权限：不注入，避免 AI 在用户不知情时调用插件；
    // 插件详情页会给出「未声明 ai:tool 权限，暂不生效」提示
    if (!info.hasAiToolPermission())
        return false;
    return info.validAiToolCount() > 0;
}

QJsonArray PluginManager::enabledAiToolsJson() const
{
    QJsonArray tools;
    QSet<QString> usedNames;
    for (const PluginInfo &info : m_plugins) {
        if (!providesAiTools(info) || !isEnabled(info.id))
            continue;
        for (const PluginAiTool &tool : info.aiTools) {
            if (!tool.isValid() || usedNames.contains(tool.name))
                continue;
            usedNames.insert(tool.name);

            QJsonObject function;
            function["name"] = tool.name;
            function["description"] = tool.description.isEmpty() ? tool.label : tool.description;
            if (tool.parameters.isEmpty()) {
                function["parameters"] = QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("properties"), QJsonObject{}},
                    {QStringLiteral("required"), QJsonArray{}}};
            } else {
                function["parameters"] = tool.parameters;
            }

            QJsonObject def;
            def["type"] = QStringLiteral("function");
            def["function"] = function;
            tools.append(def);
        }
    }
    return tools;
}

QHash<QString, QString> PluginManager::enabledAiToolMap() const
{
    QHash<QString, QString> map;
    QSet<QString> usedNames;
    for (const PluginInfo &info : m_plugins) {
        if (!providesAiTools(info) || !isEnabled(info.id))
            continue;
        for (const PluginAiTool &tool : info.aiTools) {
            if (!tool.isValid() || usedNames.contains(tool.name))
                continue;
            usedNames.insert(tool.name);
            map.insert(tool.name, info.id);
        }
    }
    return map;
}

QString PluginManager::enabledAiToolsPrompt() const
{
    QStringList sections;
    for (const PluginInfo &info : m_plugins) {
        if (!providesAiTools(info) || !isEnabled(info.id))
            continue;

        QStringList lines;
        for (const PluginAiTool &tool : info.aiTools) {
            if (!tool.isValid())
                continue;
            const QString desc = tool.description.isEmpty() ? tool.label : tool.description;
            lines << QStringLiteral("- %1：%2").arg(tool.name, desc);
        }
        if (lines.isEmpty())
            continue;

        sections << QStringLiteral("## 插件「%1」提供的工具\n%2")
                        .arg(info.name, lines.join(QLatin1Char('\n')));
    }

    if (sections.isEmpty())
        return QString();

    return QStringLiteral("以下工具由已安装并启用的插件提供，可按需调用"
                          "（参数说明见各工具定义）：\n\n") +
           sections.join(QStringLiteral("\n\n"));
}

QString PluginManager::executeAiScriptTool(const PluginInfo &info, const PluginAiTool &tool,
                                           const QJsonObject &args, QString &error)
{
    // 解包到临时目录（与插件页脚本执行的目录分开，避免互相覆盖）；
    // 插件 ID 来自清单，先清洗再拼路径，避免 ../ 逃出临时目录
    const QString safeId = safePluginIdForPath(info.id);
    const QString tmpDir = QDir::tempPath() + QStringLiteral("/BlockBox/plugin_ai_") + safeId;
    QDir(tmpDir).removeRecursively();
    QDir().mkpath(tmpDir);

    bool ok = false;
    PluginZip::extractAllToDir(info.filePath, tmpDir, &ok);
    if (!ok) {
        error = tr("解包插件失败");
        return QString();
    }

    const QString scriptPath = QDir::cleanPath(tmpDir + QStringLiteral("/") + tool.script);
    // 脚本必须落在解包目录内（拒绝 ../ 之类的越界路径）
    if (!scriptPath.startsWith(QDir::cleanPath(tmpDir) + QLatin1Char('/'))) {
        error = tr("脚本路径越界：%1").arg(tool.script);
        return QString();
    }
    if (!QFileInfo::exists(scriptPath)) {
        error = tr("包内未找到脚本：%1").arg(tool.script);
        return QString();
    }

    QStringList procArgs;
    QString program;
    const QString type = tool.scriptType();

    if (type == QLatin1String("ps1")) {
        program = QStringLiteral("powershell");
        procArgs << QStringLiteral("-NoProfile") << QStringLiteral("-NonInteractive")
                 << QStringLiteral("-ExecutionPolicy")
                 << QStringLiteral("Bypass") << QStringLiteral("-WindowStyle")
                 << QStringLiteral("Hidden") << QStringLiteral("-File")
                 << QDir::toNativeSeparators(scriptPath);
    } else if (type == QLatin1String("bat")) {
        program = QStringLiteral("cmd");
        procArgs << QStringLiteral("/c") << QStringLiteral("call")
                 << QDir::toNativeSeparators(scriptPath);
    } else {
        error = tr("不支持的脚本类型：%1").arg(tool.script);
        return QString();
    }

    // 工具参数作为命令行参数传入（-Key Value）；cmd 会重新解析命令行，
    // 带元字符或空值的参数只走 PLUGIN_ARGS 环境变量，不进命令行
    const bool isCmdScript = (type == QLatin1String("bat"));
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        const QString val = it.value().isString()
            ? it.value().toString()
            : QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
        if (val.isEmpty())
            continue;
        if (isCmdScript && val.contains(QRegularExpression(QStringLiteral("[&|<>^%\"\\r\\n]")))) {
            qWarning() << "[PluginManager] 参数包含 cmd 元字符，仅通过 PLUGIN_ARGS 传入："
                       << it.key();
            continue;
        }
        procArgs << QStringLiteral("-%1").arg(it.key()) << val;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PLUGIN_ARGS"),
               QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
    env.insert(QStringLiteral("PLUGIN_ID"), info.id);
    env.insert(QStringLiteral("PLUGIN_TOOL"), tool.name);

    // 新增：插件私有变量与常用目录（BB_ 前缀），省掉脚本里手猜路径
    const QMap<QString, QString> vars = resolvedPluginVars(info);
    QJsonObject varsJson;
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        varsJson.insert(it.key(), it.value());

    env.insert(QStringLiteral("BB_PLUGIN_ID"), info.id);
    env.insert(QStringLiteral("BB_PLUGIN_TOOL"), tool.name);
    env.insert(QStringLiteral("BB_PLUGIN_DIR"), tmpDir);           // 插件包解包目录
    env.insert(QStringLiteral("BB_PLUGIN_DATA"), vars.value(QStringLiteral("data")));
    env.insert(QStringLiteral("BB_APP_DIR"), vars.value(QStringLiteral("app")));
    env.insert(QStringLiteral("BB_PLUGIN_TOKEN"), vars.value(QStringLiteral("token")));
    env.insert(QStringLiteral("BB_PLUGIN_PORT"), vars.value(QStringLiteral("port")));
    env.insert(QStringLiteral("BB_PLUGIN_VARS"),
               QString::fromUtf8(QJsonDocument(varsJson).toJson(QJsonDocument::Compact)));

    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(tmpDir);

    proc.start(program, procArgs);
    if (!proc.waitForStarted(5000)) {
        error = tr("启动脚本失败：%1").arg(proc.errorString());
        return QString();
    }

    int timeoutMs = tool.timeout * 1000;
    if (timeoutMs <= 0)
        timeoutMs = 60000;

    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        error = tr("脚本执行超时（%1 秒）").arg(tool.timeout);
        return QString();
    }

    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    const QString errOutput = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();

    if (proc.exitCode() != 0) {
        error = tr("脚本退出码 %1%2")
                    .arg(proc.exitCode())
                    .arg(errOutput.isEmpty() ? QString() : QStringLiteral("：") + errOutput);
        return output; // 仍返回标准输出（可能含部分结果）
    }

    if (output.isEmpty() && !errOutput.isEmpty())
        output = errOutput;

    return output;
}

QString PluginManager::executeAiHttpTool(const PluginInfo &info, const PluginAiTool &tool,
                                         const QJsonObject &args, QString &error)
{
    QString urlStr = tool.http.url;
    if (urlStr.isEmpty()) {
        error = tr("HTTP 工具未定义 url");
        return QString();
    }

    // 1) 先替换插件私有变量 {plugin.<键>}（在模型参数之前，参数里的同形文本不会被当成变量）
    const QMap<QString, QString> vars = resolvedPluginVars(info);
    QStringList unknownVars;
    urlStr = substitutePluginVars(urlStr, vars, &unknownVars);
    if (!unknownVars.isEmpty()) {
        error = tr("HTTP 工具引用了未声明的插件变量：%1"
                   "（在清单 vars 里声明，或改用保留键 token/port/data/app）")
                    .arg(unknownVars.join(QStringLiteral(", ")));
        return QString();
    }

    // 2) URL 模板替换：{key} → args[key]
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
        urlStr.replace(QStringLiteral("{%1}").arg(it.key()), it.value().toString());

    // 用了 http 工具却没声明 network 权限：只提示不阻断，兼容早期插件
    if (!info.hasNetworkPermission())
        qWarning() << "[PluginManager] 插件未声明 network 权限却使用了 HTTP 工具：" << info.id;

    QUrl url(urlStr);
    if (!url.isValid()) {
        error = tr("无效的 URL：%1").arg(urlStr);
        return QString();
    }
    // 只允许 http/https，避免插件借 file:// 读取本机文件后回传给模型
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) {
        error = tr("仅支持 http/https 地址：%1").arg(urlStr);
        return QString();
    }

    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    for (auto it = tool.http.headers.constBegin(); it != tool.http.headers.constEnd(); ++it) {
        // 请求头值同样支持 {plugin.<键>}（Authorization: Bearer {plugin.token}）
        const QString headerValue = substitutePluginVars(it.value(), vars, &unknownVars);
        request.setRawHeader(it.key().toUtf8(), headerValue.toUtf8());
    }
    if (!unknownVars.isEmpty()) {
        error = tr("HTTP 工具的请求头引用了未声明的插件变量：%1")
                    .arg(unknownVars.join(QStringLiteral(", ")));
        return QString();
    }

    QString method = tool.http.method.toUpper();
    if (method.isEmpty())
        method = QStringLiteral("GET");

    QByteArray bodyData;
    if (method == QLatin1String("POST") || method == QLatin1String("PUT") ||
        method == QLatin1String("PATCH")) {
        if (tool.http.body.isEmpty()) {
            bodyData = QJsonDocument(args).toJson(QJsonDocument::Compact);
        } else {
            QString bodyText = substitutePluginVars(tool.http.body, vars);
            for (auto it = args.constBegin(); it != args.constEnd(); ++it)
                bodyText.replace(QStringLiteral("{%1}").arg(it.key()), it.value().toString());
            bodyData = bodyText.toUtf8();
        }
    } else {
        // GET/DELETE：未用于占位符替换的参数追加为 query
        QUrlQuery query(url);
        for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
            if (!tool.http.url.contains(QStringLiteral("{%1}").arg(it.key())))
                query.addQueryItem(it.key(), it.value().toString());
        }
        url.setQuery(query);
        request.setUrl(url);
    }

    QNetworkReply *reply = nullptr;
    if (method == QLatin1String("GET"))
        reply = nam.get(request);
    else if (method == QLatin1String("POST"))
        reply = nam.post(request, bodyData);
    else if (method == QLatin1String("PUT"))
        reply = nam.put(request, bodyData);
    else if (method == QLatin1String("DELETE"))
        reply = nam.deleteResource(request);
    else
        reply = nam.sendCustomRequest(request, method.toUtf8(), bodyData);

    if (!reply) {
        error = tr("创建 HTTP 请求失败");
        return QString();
    }

    int timeoutMs = tool.timeout * 1000;
    if (timeoutMs <= 0)
        timeoutMs = 60000;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    QString result;
    if (timer.isActive()) {
        timer.stop();
        if (reply->error() != QNetworkReply::NoError) {
            error = tr("HTTP 请求失败：%1").arg(reply->errorString());
            result = QString::fromUtf8(reply->readAll());
        } else {
            result = QString::fromUtf8(reply->readAll());
        }
    } else {
        reply->abort();
        error = tr("HTTP 请求超时（%1 秒）").arg(tool.timeout);
    }

    reply->deleteLater();
    return result;
}

QString PluginManager::executeAiTool(const QString &pluginId, const QString &toolName,
                                     const QJsonObject &args, QString &error)
{
    const PluginInfo info = pluginById(pluginId);
    if (!info.isValid()) {
        error = QStringLiteral("PLUGIN_NOT_FOUND");
        return QString();
    }
    if (!isEnabled(pluginId)) {
        error = QStringLiteral("PLUGIN_DISABLED");
        return QString();
    }
    if (!info.hasAiToolPermission()) {
        error = QStringLiteral("PLUGIN_AI_TOOL_NOT_ALLOWED");
        return QString();
    }

    const PluginAiTool tool = info.aiTool(toolName);
    if (!tool.isValid()) {
        error = QStringLiteral("PLUGIN_TOOL_NOT_FOUND: ") + toolName;
        return QString();
    }

    if (tool.http.isValid())
        return executeAiHttpTool(info, tool, args, error);
    return executeAiScriptTool(info, tool, args, error);
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

    // 能力权限示例：向 AI 助手提供工具必须声明 ai:tool
    {
        QJsonArray permissions;
        permissions.append(QStringLiteral("ai:tool"));
        manifest.insert(QStringLiteral("permissions"), permissions);
    }

    // AI 工具示例（宿主把工具注入 AI 助手，模型可自主调用 scripts/ai_tool.ps1）
    {
        // 工具名需符合函数名规则（ASCII 字母/数字/下划线/连字符），从插件 ID 清洗得到
        QString toolPrefix;
        for (const QChar &c : dirName) {
            if ((c.isLetterOrNumber() && c.unicode() < 128) || c == QLatin1Char('_')
                || c == QLatin1Char('-'))
                toolPrefix.append(c);
            else
                toolPrefix.append(QLatin1Char('_'));
        }
        if (toolPrefix.isEmpty())
            toolPrefix = QStringLiteral("plugin");
        if (toolPrefix.size() > 48)
            toolPrefix = toolPrefix.left(48);

        QJsonObject parameters;
        parameters.insert(QStringLiteral("type"), QStringLiteral("object"));
        QJsonObject textProp;
        textProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        textProp.insert(QStringLiteral("description"), QStringLiteral("要回显的文字"));
        QJsonObject properties;
        properties.insert(QStringLiteral("text"), textProp);
        parameters.insert(QStringLiteral("properties"), properties);
        parameters.insert(QStringLiteral("required"), QJsonArray());

        QJsonObject tool;
        tool.insert(QStringLiteral("name"), toolPrefix + QStringLiteral("_hello"));
        tool.insert(QStringLiteral("label"), QStringLiteral("示例工具"));
        tool.insert(QStringLiteral("description"),
                    QStringLiteral("示例 AI 工具：回显传入的文字，用于演示插件如何为 AI 助手提供工具"));
        tool.insert(QStringLiteral("parameters"), parameters);
        tool.insert(QStringLiteral("script"), QStringLiteral("scripts/ai_tool.ps1"));
        tool.insert(QStringLiteral("timeout"), 30);

        QJsonArray aiTools;
        aiTools.append(tool);
        manifest.insert(QStringLiteral("aiTools"), aiTools);
    }

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

    // 3. scripts/ai_tool.ps1：AI 工具示例脚本
    dir.mkpath(templateRoot + QStringLiteral("/scripts"));
    QFile aiScriptFile(templateRoot + QStringLiteral("/scripts/ai_tool.ps1"));
    if (aiScriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream ts(&aiScriptFile);
        ts << "# BlockBox plugin AI tool demo.\n"
           << "# Host passes tool args as -Key Value; full JSON is in $env:PLUGIN_ARGS.\n"
           << "# Standard output is returned to the AI as the tool result.\n"
           << "param([string]$text = \"hello\")\n\n"
           << "Write-Output (\"plugin \" + $env:PLUGIN_ID + \" hello: \" + $text)\n";
        aiScriptFile.close();
    }

    // 4. README.md 说明
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
           << "## AI 工具\n\n"
           << "plugin.json 里的 aiTools 会把工具注入 AI 助手（工作模式），"
              "模型可自主调用；同时需要在 permissions 中声明 ai:tool 权限。\n"
           << "示例工具执行 scripts/ai_tool.ps1，可改成自己的脚本，"
              "脚本标准输出会作为结果回填给 AI。\n\n"
           << "## 目录结构\n\n"
           << "```\n"
           << dirName << "/\n"
           << "├── plugin.json       # 插件清单（包内必需，保存插件信息）\n"
           << "├── main.js           # 插件入口脚本\n"
           << "├── README.md         # 说明文档\n"
           << "├── scripts/\n"
           << "│   └── ai_tool.ps1   # AI 工具示例脚本\n"
           << "└── assets/\n"
           << "    └── icon.png      # 插件图标\n"
           << "```\n";
        readmeFile.close();
    }

    // 5. 默认图标 assets/icon.png（128x128 主题色块 + 字母）
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
