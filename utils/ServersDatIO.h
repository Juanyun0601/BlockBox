/**
 * @file   ServersDatIO.h
 * @brief  Minecraft 多人游戏服务器列表 servers.dat 读写工具
 * @author BlockBox Team
 * @date  2026-10-18
 *
 * servers.dat 为 NBT（Named Binary Tag）格式，外层使用 gzip 压缩。
 * 根标签为 Compound，内含 "servers" List，每项为 Compound，至少含：
 *   - name (String) 服务器显示名
 *   - ip   (String) 地址，形如 "host" / "host:port" / "[v6]:port"
 * 其它字段（如 icon）在改写时原样保留。
 */

#pragma once

#include <QList>
#include <QString>

class ServersDatIO
{
public:
    struct Entry
    {
        QString name;
        QString host;   ///< 不含端口的主机名/IP
        quint16 port = 25565;
    };

    /** 读取 servers.dat；文件不存在或损坏时返回空列表 */
    static QList<Entry> read(const QString &path);

    /** 按 host+port 更新或插入一条服务器；文件不存在时会新建。失败返回 false */
    static bool upsert(const QString &path, const Entry &entry);

    /** 按 host+port 删除一条服务器；文件不存在时返回 true（无需删除） */
    static bool remove(const QString &path, const QString &host, quint16 port);

    /** "host:port" / "[v6]:port" / "host" → host + port；失败返回空 host */
    static bool parseAddress(const QString &ip, QString &hostOut, quint16 &portOut);

    /** host + port → 写入 servers.dat 用的 ip 字符串 */
    static QString formatAddress(const QString &host, quint16 port);

    /** host+port 比较键（忽略大小写；默认端口不拼后缀） */
    static QString addressKey(const QString &host, quint16 port);
};
