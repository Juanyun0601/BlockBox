/**
 * @file   ServerAdminStore.h
 * @brief  一键本地开服的「管理」数据层：白名单 / OP / server.properties 常用项
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 服务端把玩家名单放在两个 JSON 里（whitelist.json / ops.json），把规则放在
 * server.properties 里。这里只做「读写这几个文件」，不管进程；开服运行中
 * 要改名单时由界面那边发服务端指令，让服务端自己写文件（UUID 才是对的）。
 *
 * 白名单 / OP 的 UUID：
 *   - 离线模式（本软件默认）服务端用 "OfflinePlayer:<名字>" 的 MD5 算 UUID，
 *     这里用同一套算法，停服时也能把人加进名单；
 *   - 正版验证开着时 UUID 是玩家的正版 UUID，算不出来（要查 Mojang），
 *     所以这种时候界面会让用户先开服再加。
 */
#ifndef SERVERADMINSTORE_H
#define SERVERADMINSTORE_H

#include <QList>
#include <QMap>
#include <QString>

/** 白名单里的一条（与服务端 whitelist.json 对应） */
struct ServerWhitelistEntry
{
    QString uuid;
    QString name;
};

/** 管理员里的一条（与服务端 ops.json 对应） */
struct ServerOpEntry
{
    QString uuid;
    QString name;
    int level = 4;
    bool bypassesPlayerLimit = false;
};

class ServerAdminStore
{
public:
    explicit ServerAdminStore(const QString &serverDir = QString());

    void setServerDir(const QString &dir) { m_serverDir = dir; }
    QString serverDir() const { return m_serverDir; }
    /// 服务端目录还没确定（没选实例）时为 false
    bool isValid() const { return !m_serverDir.trimmed().isEmpty(); }

    QString whitelistPath() const;
    QString opsPath() const;
    QString propertiesPath() const;

    /**
     * @brief 读白名单
     *
     * 文件不存在当成「还没有人」；内容坏了时返回空表并在 error 里说明，
     * 不会把坏文件当成空表悄悄覆盖掉。
     */
    QList<ServerWhitelistEntry> readWhitelist(QString *error = nullptr) const;
    bool writeWhitelist(const QList<ServerWhitelistEntry> &entries, QString *error = nullptr) const;

    QList<ServerOpEntry> readOps(QString *error = nullptr) const;
    bool writeOps(const QList<ServerOpEntry> &entries, QString *error = nullptr) const;

    /// 读 server.properties 里的一个键（文件不存在 / 没这个键时返回 fallback）
    QString property(const QString &key, const QString &fallback = QString()) const;
    /**
     * @brief 一次写回多个键
     *
     * 保留文件里其它键和原来的顺序，只改传进来的这几个；文件不存在时新建。
     */
    bool setProperties(const QMap<QString, QString> &values, QString *error = nullptr) const;
    bool setProperty(const QString &key, const QString &value, QString *error = nullptr) const;

    /// 离线模式的玩家 UUID（与服务端 "OfflinePlayer:<名字>" 的算法一致）
    static QString offlineUuid(const QString &playerName);
    /// Java 版玩家名：3~16 位字母 / 数字 / 下划线
    static bool isValidPlayerName(const QString &name);

private:
    QString m_serverDir;
};

#endif // SERVERADMINSTORE_H
