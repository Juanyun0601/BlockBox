/**
 * @file   HongshiLobbyClient.h
 * @brief  红石联机大厅客户端 - 从红石服务器获取公开房间列表
 * @author BlockBox Team
 * @date   2026-08-30
 *
 * 参考 hongshionline/hongshi-shell 项目（Tauri+Rust）的 API 设计：
 *   - 主站 https://hongshi.site / 镜像 https://shithub.site
 *   - 房间列表接口：/api/rooms（主站优先，失败回退镜像）
 *
 * 与 HongshiClient（管理内核进程）分离，本类仅负责大厅数据的网络拉取。
 */

#pragma once

#include <QJsonArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>

/**
 * @brief 红石联机房间信息（从服务器拉取）
 */
struct HongshiRoomInfo
{
    QString roomId;          ///< 房间唯一 ID
    QString roomName;        ///< 房间名称
    QString hostName;        ///< 房主玩家名
    QString gameVersion;     ///< 游戏版本（如 1.21.1）
    QString loaderType;      ///< 加载器类型（Forge/Fabric/Vanilla 等）
    int currentPlayers;      ///< 当前玩家数
    int maxPlayers;          ///< 最大玩家数
    QString status;          ///< 房间状态：waiting / playing / full
    QString nodeRegion;      ///< 中转节点区域
    qint64 createdAt;        ///< 创建时间戳（秒）
    int latencyMs;           ///< 延迟（毫秒），-1 表示未知
    QString tunnelAddress;   ///< 隧道地址（server:port），用于直接加入
};

/**
 * @brief 红石联机大厅客户端（单例）
 *
 * 从 hongshi.site / shithub.site 拉取公开房间列表。
 * 支持主站→镜像自动回退、定时轮询、手动刷新。
 */
class HongshiLobbyClient : public QObject
{
    Q_OBJECT

public:
    static HongshiLobbyClient *instance();

    /// 当前房间列表
    QList<HongshiRoomInfo> rooms() const { return m_rooms; }
    /// 是否正在加载
    bool isLoading() const { return m_loading; }
    /// 最近一次错误信息
    QString lastError() const { return m_lastError; }

    /**
     * @brief 手动刷新房间列表（异步）
     */
    void refreshRooms();

    /**
     * @brief 启动/停止自动轮询
     * @param intervalMs 轮询间隔（毫秒），0 表示停止
     */
    void setAutoRefreshInterval(int intervalMs);

signals:
    /// 房间列表刷新完成
    void roomsRefreshed(const QList<HongshiRoomInfo> &rooms);
    /// 刷新出错
    void refreshError(const QString &error);
    /// 加载状态变化
    void loadingChanged(bool loading);

private slots:
    void onAutoRefresh();

private:
    explicit HongshiLobbyClient(QObject *parent = nullptr);
    ~HongshiLobbyClient() override;
    Q_DISABLE_COPY_MOVE(HongshiLobbyClient)

    void tryFetchRoomList(int siteIndex);
    QList<HongshiRoomInfo> parseRoomList(const QJsonArray &arr);
    HongshiRoomInfo parseRoomObject(const QJsonObject &obj) const;
    void setLoading(bool loading);

    QNetworkAccessManager m_networkManager;
    QList<HongshiRoomInfo> m_rooms;
    bool m_loading = false;
    QString m_lastError;
    QTimer m_autoRefreshTimer;
};
