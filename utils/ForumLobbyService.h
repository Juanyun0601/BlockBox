/**
 * @file   ForumLobbyService.h
 * @brief  方块盒子论坛「联机大厅」接口：列房间、发布、心跳续期、关闭、记录加入
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 实例助手的联机页用它把本机开好的房间发布到论坛，也用来拉取大厅列表：
 *   - 发布后客户端每 40 秒发一次心跳，房间才不会过期；
 *   - 网页端只能看到邀请码 / 连接地址，真正加入游戏要在客户端里做。
 *
 * 请求统一走 ForumAccountService，自动带上论坛登录 Cookie、处理重试和错误提示。
 */

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "ForumAccountService.h"

/** 联机大厅里的一个房间（对应 GET /api/lobby/rooms 的 rooms 数组项） */
struct ForumLobbyRoom
{
    int id = 0;
    QString title;          ///< 房间名
    QString description;    ///< 简介
    QString core;           ///< terracotta / hongshi / frp / custom
    QString coreLabel;      ///< 核心显示名，例如「陶瓦联机」
    QString inviteCode;     ///< 陶瓦邀请码（网页端展示的就是它）
    QString address;        ///< 红石 / frp / 直连地址（主机:端口）
    QString gameVersion;    ///< 游戏版本
    QString loader;         ///< 加载器名称
    QStringList mods;       ///< 房主实例里的模组文件名
    QString cover;          ///< 站内封面图路径，例如 /uploads/img-1-xxx.png，空 = 没设
    QString md;             ///< Markdown 详细介绍，空 = 没写
    QString modpackMode;    ///< 前置整合包：空 = 没设 / online（在线可下载）/ link（名称+网盘链接）
    QString modpackName;    ///< 前置整合包名称
    QString modpackUrl;     ///< online = 下载地址；link = 网盘地址
    QString modpackMeta;    ///< 在线整合包信息快照 JSON（platform/author/version/file_name 等）
    int maxPlayers = 8;
    QString status;         ///< waiting / playing / full
    int joinCount = 0;
    ForumUser host;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    int expiresIn = 0;      ///< 距离过期还有多少秒，0 表示已经过期
    bool mine = false;      ///< 是不是自己发布的房间

    bool isOnline() const { return expiresIn > 0; }
    bool isTerracotta() const { return core == QStringLiteral("terracotta"); }
    /** 展示 / 复制用的邀请码或地址 */
    QString joinTarget() const { return inviteCode.isEmpty() ? address : inviteCode; }
    /** 这个房间有没有标前置整合包 */
    bool hasModpack() const { return !modpackMode.isEmpty() && !modpackName.isEmpty(); }
};

class ForumLobbyService : public QObject
{
    Q_OBJECT

public:
    using FailHandler = ForumAccountService::FailHandler;
    /** 房间列表回调：房间、总数 */
    using RoomListHandler = std::function<void(const QVector<ForumLobbyRoom> &rooms, int total)>;
    /** 单个房间回调 */
    using RoomHandler = std::function<void(const ForumLobbyRoom &room)>;
    /** 纯成功回调（关闭房间用） */
    using OkHandler = std::function<void()>;
    /**
     * 图片上传回调：url = 图床返回的 /uploads/xxx 路径（失败为空串）；
     * message = 失败时的提示（成功时为空）
     */
    using UploadHandler = std::function<void(const QString &url, const QString &message)>;

    static ForumLobbyService *instance();

    /**
     * @brief 拉取大厅房间列表
     * @param keyword 房间名 / 简介 / 房主关键词，可空
     * @param core    只看某个联机核心，可空
     */
    void fetchRooms(const QString &keyword, const QString &core,
                    RoomListHandler onOk, FailHandler onFail = nullptr);

    /** 发布 / 更新自己的房间（同一账号只保留一条，重复发布即覆盖） */
    void publishRoom(const ForumLobbyRoom &room, RoomHandler onOk, FailHandler onFail = nullptr);

    /** 心跳续期，status 可空（表示沿用服务端当前状态） */
    void sendHeartbeat(int roomId, const QString &status,
                       RoomHandler onOk, FailHandler onFail = nullptr);

    /** 关闭并下架自己的房间 */
    void closeRoom(int roomId, OkHandler onOk, FailHandler onFail = nullptr);

    /** 记录一次「加入」，给房间的热度计数加一，并拿回最新房间信息 */
    void recordJoin(int roomId, RoomHandler onOk, FailHandler onFail = nullptr);

    /**
     * @brief 把本地图片上传到论坛图床（POST /api/upload），房间封面共用这一通道
     * @param filePath 本地图片绝对路径（PNG / JPG / WebP / GIF，论坛侧限 4 MB）
     * @param filePath 原图太大时先用 QImage 缩到长边 2560 以内再发
     */
    void uploadImage(const QString &filePath, UploadHandler onDone);

    /** 核心 key → 中文名 */
    static QString coreDisplayName(const QString &core);
    /** 服务端 JSON → 房间结构（缺字段时给默认值，不会崩） */
    static ForumLobbyRoom parseRoom(const QJsonObject &object);

/** 发布模板：本机保存的房间常填项，下次发布一键套用 */
struct PublishTemplate
{
    QString title;        ///< 房间名
    QString description;  ///< 一句话简介
    int maxPlayers = 8;   ///< 人数上限
    QString cover;        ///< 已上传的封面（/uploads/xxx），换电脑 / 重装后图还在论坛
    QString coverPath;    ///< 本地封面文件路径（重新上传用），文件丢了就只剩 cover
    QString md;           ///< Markdown 介绍
    QString modpackMode;  ///< online / link / 空
    QString modpackName;
    QString modpackUrl;
    QString modpackMeta;  ///< 在线整合包信息快照 JSON

    bool isEmpty() const
    {
        return title.isEmpty() && description.isEmpty() && md.isEmpty()
               && cover.isEmpty() && modpackMode.isEmpty();
    }
};

    /** 从数据目录的 lobby_publish_templates.json 读模板，坏了返回默认结构 */
    static PublishTemplate loadPublishTemplate();
    /** 把模板写回数据目录（单槽覆盖式，保存的永远是「上次的发布设置」） */
    static void savePublishTemplate(const PublishTemplate &tpl);

private:
    explicit ForumLobbyService(QObject *parent = nullptr);
};

Q_DECLARE_METATYPE(ForumLobbyRoom)
