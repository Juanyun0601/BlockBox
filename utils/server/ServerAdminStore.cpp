/**
 * @file   ServerAdminStore.cpp
 * @brief  一键本地开服的「管理」数据层实现
 * @author BlockBox Team
 * @date   2026-11-27
 */
#include "ServerAdminStore.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QPair>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QVector>

namespace {

const char *kPropertiesHeader =
    "# 由方块盒子「一键本地开服」维护：改过的项会保留，开服时只更新端口/人数等必要项\n";

/// server.properties 的值是 Java Properties 编码，中文要写成 \uXXXX
QString escapePropertyValue(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        if (code >= 0x20 && code <= 0x7E)
            out.append(ch);
        else
            out.append(QStringLiteral("\\u%1").arg(code, 4, 16, QLatin1Char('0')));
    }
    return out;
}

/// 读一份 properties 文件，返回有序的键值列表（保留原文件顺序）
QVector<QPair<QString, QString>> readProperties(const QString &path, QString *error)
{
    QVector<QPair<QString, QString>> items;
    QFile file(path);
    if (!file.exists())
        return items;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QObject::tr("读不了 server.properties 文件：%1").arg(path);
        return items;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        if (key.isEmpty() || key.startsWith(QLatin1Char('#')))
            continue;
        items.append(qMakePair(key, line.mid(eq + 1)));
    }
    return items;
}

/**
 * @brief 原子地写一份 properties 文件
 *
 * 用 QSaveFile 先写临时文件再替换，写到一半断电 / 被占用时不会留下半截文件，
 * 服务端也不会读到写坏一半的配置。
 */
bool writeProperties(const QString &path, const QVector<QPair<QString, QString>> &items,
                     QString *error)
{
    const QString dirPath = QFileInfo(path).absolutePath();
    if (!dirPath.isEmpty() && !QDir().mkpath(dirPath)) {
        if (error)
            *error = QObject::tr("创建服务端目录失败：%1").arg(dirPath);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QObject::tr("写入 server.properties 失败，请检查是否有写入权限。");
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << kPropertiesHeader;
    for (const auto &item : items)
        out << item.first << '=' << item.second << '\n';
    out.flush();
    if (!file.commit()) {
        if (error)
            *error = QObject::tr("写入 server.properties 失败，请检查是否有写入权限。");
        return false;
    }
    return true;
}

/**
 * @brief 读名单文件（whitelist.json / ops.json）
 *
 * 文件不存在是正常情况（服务端还没写过），返回空数组；
 * 内容坏了才报错，让界面提示而不是把坏文件覆盖成空表。
 */
QJsonArray readJsonList(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists())
        return QJsonArray();
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("读不了 %1，可能正被占用。").arg(QFileInfo(path).fileName());
        return QJsonArray();
    }
    const QByteArray raw = file.readAll();
    file.close();
    if (raw.trimmed().isEmpty())
        return QJsonArray();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error)
            *error = QObject::tr("%1 内容损坏，不敢直接覆盖，请先备份再删掉它。")
                         .arg(QFileInfo(path).fileName());
        return QJsonArray();
    }
    return doc.array();
}

bool writeJsonList(const QString &path, const QJsonArray &array, QString *error)
{
    const QString dirPath = QFileInfo(path).absolutePath();
    if (!dirPath.isEmpty() && !QDir().mkpath(dirPath)) {
        if (error)
            *error = QObject::tr("创建服务端目录失败：%1").arg(dirPath);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("写入 %1 失败，请检查是否有写入权限。")
                         .arg(QFileInfo(path).fileName());
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QObject::tr("写入 %1 失败，请检查是否有写入权限。")
                         .arg(QFileInfo(path).fileName());
        return false;
    }
    return true;
}

} // namespace

ServerAdminStore::ServerAdminStore(const QString &serverDir)
    : m_serverDir(serverDir)
{
}

QString ServerAdminStore::whitelistPath() const
{
    if (!isValid())
        return QString();
    return QDir(m_serverDir).absoluteFilePath(QStringLiteral("whitelist.json"));
}

QString ServerAdminStore::opsPath() const
{
    if (!isValid())
        return QString();
    return QDir(m_serverDir).absoluteFilePath(QStringLiteral("ops.json"));
}

QString ServerAdminStore::propertiesPath() const
{
    if (!isValid())
        return QString();
    return QDir(m_serverDir).absoluteFilePath(QStringLiteral("server.properties"));
}

QList<ServerWhitelistEntry> ServerAdminStore::readWhitelist(QString *error) const
{
    QList<ServerWhitelistEntry> entries;
    const QString path = whitelistPath();
    if (path.isEmpty())
        return entries;

    const QJsonArray array = readJsonList(path, error);
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        ServerWhitelistEntry entry;
        entry.uuid = object.value(QStringLiteral("uuid")).toString().trimmed();
        entry.name = object.value(QStringLiteral("name")).toString().trimmed();
        if (entry.name.isEmpty() && entry.uuid.isEmpty())
            continue;
        entries.append(entry);
    }
    return entries;
}

bool ServerAdminStore::writeWhitelist(const QList<ServerWhitelistEntry> &entries,
                                      QString *error) const
{
    const QString path = whitelistPath();
    if (path.isEmpty()) {
        if (error)
            *error = QObject::tr("还没选到实例，不知道服务端目录在哪。");
        return false;
    }
    QJsonArray array;
    for (const auto &entry : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("uuid"), entry.uuid);
        object.insert(QStringLiteral("name"), entry.name);
        array.append(object);
    }
    return writeJsonList(path, array, error);
}

QList<ServerOpEntry> ServerAdminStore::readOps(QString *error) const
{
    QList<ServerOpEntry> entries;
    const QString path = opsPath();
    if (path.isEmpty())
        return entries;

    const QJsonArray array = readJsonList(path, error);
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        ServerOpEntry entry;
        entry.uuid = object.value(QStringLiteral("uuid")).toString().trimmed();
        entry.name = object.value(QStringLiteral("name")).toString().trimmed();
        entry.level = object.value(QStringLiteral("level")).toInt(4);
        entry.bypassesPlayerLimit =
            object.value(QStringLiteral("bypassesPlayerLimit")).toBool(false);
        if (entry.name.isEmpty() && entry.uuid.isEmpty())
            continue;
        entries.append(entry);
    }
    return entries;
}

bool ServerAdminStore::writeOps(const QList<ServerOpEntry> &entries, QString *error) const
{
    const QString path = opsPath();
    if (path.isEmpty()) {
        if (error)
            *error = QObject::tr("还没选到实例，不知道服务端目录在哪。");
        return false;
    }
    QJsonArray array;
    for (const auto &entry : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("uuid"), entry.uuid);
        object.insert(QStringLiteral("name"), entry.name);
        object.insert(QStringLiteral("level"), qBound(1, entry.level, 4));
        object.insert(QStringLiteral("bypassesPlayerLimit"), entry.bypassesPlayerLimit);
        array.append(object);
    }
    return writeJsonList(path, array, error);
}

QString ServerAdminStore::property(const QString &key, const QString &fallback) const
{
    const QString path = propertiesPath();
    if (path.isEmpty())
        return fallback;
    const QVector<QPair<QString, QString>> items = readProperties(path, nullptr);
    for (const auto &item : items) {
        if (item.first == key)
            return item.second;
    }
    return fallback;
}

bool ServerAdminStore::setProperties(const QMap<QString, QString> &values, QString *error) const
{
    const QString path = propertiesPath();
    if (path.isEmpty()) {
        if (error)
            *error = QObject::tr("还没选到实例，不知道服务端目录在哪。");
        return false;
    }

    QVector<QPair<QString, QString>> items = readProperties(path, error);
    QMap<QString, QString> pending = values;
    for (auto &item : items) {
        const auto it = pending.find(item.first);
        if (it == pending.end())
            continue;
        item.second = escapePropertyValue(it.value());
        pending.erase(it);
    }
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
        items.append(qMakePair(it.key(), escapePropertyValue(it.value())));
    return writeProperties(path, items, error);
}

bool ServerAdminStore::setProperty(const QString &key, const QString &value, QString *error) const
{
    QMap<QString, QString> values;
    values.insert(key, value);
    return setProperties(values, error);
}

QString ServerAdminStore::offlineUuid(const QString &playerName)
{
    // 与服务端一致：UUID.nameUUIDFromBytes("OfflinePlayer:<名字>".getBytes(UTF_8))
    // —— MD5 之后还要按 UUID v3 改写版本号与变体位，否则算出来的不是服务端认的那串。
    QByteArray digest = QCryptographicHash::hash(
        QStringLiteral("OfflinePlayer:%1").arg(playerName).toUtf8(), QCryptographicHash::Md5);
    if (digest.size() != 16)
        return QString();
    digest[6] = static_cast<char>((digest[6] & 0x0F) | 0x30);   // version 3
    digest[8] = static_cast<char>((digest[8] & 0x3F) | 0x80);   // IETF variant

    QString hex = QString::fromLatin1(digest.toHex());
    hex.insert(8, QLatin1Char('-'));
    hex.insert(13, QLatin1Char('-'));
    hex.insert(18, QLatin1Char('-'));
    hex.insert(23, QLatin1Char('-'));
    return hex;
}

bool ServerAdminStore::isValidPlayerName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_]{3,16}$"));
    return re.match(name).hasMatch();
}
