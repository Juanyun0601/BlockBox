/**
 * @file   ForumPlayerStatsService.h
 * @brief  论坛「人数统计」接口：上报采样 + 拉取聚合后的折线数据
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 两个用法：
 *   - 服务器导航帖的详情页：定时探测服务器在线人数，上报到论坛，
 *     再把论坛按时间桶聚合好的历史人数画成折线；
 *   - 联机大厅房间详情页：把房间里「正在游玩」的人数定时上报，
 *     同样从论坛取回聚合数据。
 *
 * 采样是所有客户端共用的：谁打开过详情页谁就报一次，论坛把同一时段
 * 的采样平均后回给所有人，所以曲线会随着大家的使用慢慢变完整。
 *
 * 接口是公开的（不要求登录）；网络失败静默处理，界面上保留上一次的曲线。
 */

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

/** 聚合后的一个人数采样点（论坛按时间桶平均） */
struct PlayerStatPoint
{
    qint64 t = 0;        ///< 桶起点（unix 秒）
    int online = 0;      ///< 桶内平均人数（四舍五入）
    int maxPlayers = 0;  ///< 桶内出现过的最大人数上限
    int samples = 0;     ///< 桶内采样条数
};

/** 一次查询的结果：曲线点 + 最新人数 */
struct PlayerStatSeries
{
    QString key;                      ///< 服务器地址（host:port）或房间 id
    QVector<PlayerStatPoint> points;
    int latestOnline = 0;
    int latestMax = 0;
    qint64 latestAt = 0;
    int totalSamples = 0;

    bool isEmpty() const { return points.isEmpty(); }

    /** 曲线峰值（画图时算纵轴用） */
    int peak() const
    {
        int value = 0;
        for (const PlayerStatPoint &point : points)
            value = qMax(value, point.online);
        return value;
    }
};

class ForumPlayerStatsService : public QObject
{
    Q_OBJECT

public:
    using SeriesHandler = std::function<void(const PlayerStatSeries &)>;
    using FailHandler = std::function<void()>;

    /** 服务器地址：host + 端口，key 是上报 / 查询时用的规范化地址 */
    struct ServerAddress
    {
        QString host;
        quint16 port = 25565;

        bool isValid() const { return !host.isEmpty() && port > 0; }
        /** 上报 / 查询用的 key：小写 host:port（端口缺省补 25565） */
        QString key() const
        {
            return isValid() ? QStringLiteral("%1:%2").arg(host).arg(port) : QString();
        }
    };

    static ForumPlayerStatsService *instance();

    /**
     * @brief 解析导航帖里填的服务器地址（形如 mc.example.com / 1.2.3.4:25566）
     *
     * 去掉可能的 minecraft:// 前缀与路径、把域名转小写；带端口时按端口走，
     * 不带端口补 25565。解析不出合法主机名时返回空对象（isValid()=false）。
     */
    static ServerAddress parseServerAddress(const QString &raw);

    /** 上报一次服务器在线人数（失败静默） */
    void reportServer(const ServerAddress &address, int online, int maxPlayers);

    /** 拉取服务器最近 hours 小时的人数曲线（失败不回调数据，只回调 onFail 保留旧图） */
    void fetchServer(const ServerAddress &address, int hours, SeriesHandler onOk,
                     FailHandler onFail = nullptr);

    /** 上报一次联机房间里正在玩的人数（失败静默） */
    void reportLobby(int roomId, int players);

    /** 拉取房间最近 hours 小时的人数曲线（失败回调 onFail，界面上保留旧图） */
    void fetchLobby(int roomId, int hours, SeriesHandler onOk, FailHandler onFail = nullptr);

private:
    explicit ForumPlayerStatsService(QObject *parent = nullptr);

    static PlayerStatSeries parseSeries(const QJsonObject &root);
};
