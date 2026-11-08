/**
 * @file   ForumChatService.h
 * @brief  方块盒子论坛好友与私信服务：好友列表 / 申请 / 私信收发 / 轮询
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 好友页（ForumFriendsPage）共用这一个入口。所有请求都转发给
 * ForumAccountService（自动带上登录 Cookie、论坛地址和统一的错误解析），
 * 本服务只负责拼接口与解析数据结构，接口与论坛网页版一一对应：
 *   /api/friends、/api/friends/requests、/api/messages/:userId、/api/messages/poll
 *
 * 回调风格与 ForumContentService 一致：onOk 返回解析好的结构体，
 * onFail 的 message 可直接展示给用户（论坛返回的中文错误原样透传）。
 */
#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

#include "ForumAccountService.h"

/** 好友 / 搜索到的用户（网页版 peerOut 同款字段） */
struct ForumFriendInfo
{
    int id = 0;
    QString username;
    QString displayName;
    QString avatar;
    bool online = false;       ///< 最近 10 分钟内活跃过
    bool official = false;     ///< 方块盒子官方客服（默认好友，不能删除）
    qint64 lastSeenAt = 0;

    // 好友列表行专属（搜索结果没有这些字段）
    int unread = 0;            ///< 对方发来的未读私信数
    qint64 lastMessageAt = 0;  ///< 最后一条私信时间（0 = 还没聊过）
    QString lastMessageBody;   ///< 最后一条私信内容
    QString lastMessageKind;   ///< text / image / withdrawn（预览用）
    bool lastMessageMine = false; ///< 最后一条是不是我发的

    bool isValid() const { return id > 0; }
};

/** 一条好友申请（收到的 / 发出的共用，user 是对方） */
struct ForumFriendRequest
{
    int id = 0;          ///< 申请记录 id，同意 / 拒绝 / 撤回都用它
    qint64 createdAt = 0;
    ForumFriendInfo user;

    bool isValid() const { return id > 0; }
};

/** 好友页一次拉全的快照（GET /api/friends 的解析结果） */
struct ForumFriendSnapshot
{
    QVector<ForumFriendInfo> friends;
    QVector<ForumFriendRequest> incoming;   ///< 收到的申请
    QVector<ForumFriendRequest> outgoing;   ///< 发出的申请
    int totalUnread = 0;                    ///< 未读私信总数
};

/** 一条私信 */
struct ForumChatMessage
{
    int id = 0;
    QString kind;          ///< text / image / withdrawn（撤回占位）
    QString body;          ///< 文字内容（图片消息的附言；withdrawn 时为空）
    QString image;         ///< 站内图片路径（kind=image 时有效），如 /uploads/img-1-xx.png
    int replyTo = 0;       ///< 引用的消息 id（0 = 没有引用）
    QString replyBody;     ///< 被引用消息的摘要（服务端处理好：[图片]/[已撤回]/截断文字）
    bool replyMine = false;///< 被引用的消息是不是自己发的
    bool mine = false;     ///< 是不是我发的
    qint64 createdAt = 0;
    bool read = false;     ///< 对方是否已读（自己发的消息才有意义）

    bool isImage() const { return kind == QStringLiteral("image") && !image.isEmpty(); }
    bool isWithdrawn() const { return kind == QStringLiteral("withdrawn"); }

    bool isValid() const { return id > 0; }
};

/** 打开会话（GET /api/messages/:userId 的解析结果） */
struct ForumChatHistory
{
    ForumFriendInfo peer;
    QVector<ForumChatMessage> messages;   ///< 按时间升序
    bool hasMore = false;                 ///< 还有更早的消息（用 before_id 往前翻）
};

/** 轻量轮询（GET /api/messages/poll 的解析结果） */
struct ForumPollResult
{
    int totalUnread = 0;
    int incomingRequests = 0;
    QVector<ForumFriendInfo> friends;
    QVector<ForumChatMessage> messages;   ///< 当前会话的新消息（peerId > 0 时才有）
};

class ForumChatService : public QObject
{
    Q_OBJECT

public:
    using FailHandler = ForumAccountService::FailHandler;

    static ForumChatService *instance();

    // ── 好友 ──
    void fetchFriends(std::function<void(const ForumFriendSnapshot &)> onOk,
                      FailHandler onFail = nullptr);

    /** 发好友申请；对方已先申请我时论坛会直接结为好友（status = friends） */
    void sendFriendRequest(int userId, std::function<void(const QString &status)> onOk,
                           FailHandler onFail = nullptr);

    /** 按用户名发好友申请（添加好友页搜索不到精确 id 时用） */
    void sendFriendRequestByName(const QString &username,
                                 std::function<void(const QString &status)> onOk,
                                 FailHandler onFail = nullptr);

    void acceptFriendRequest(int requestId, std::function<void()> onOk,
                             FailHandler onFail = nullptr);
    void declineFriendRequest(int requestId, std::function<void()> onOk,
                              FailHandler onFail = nullptr);
    void withdrawFriendRequest(int requestId, std::function<void()> onOk,
                               FailHandler onFail = nullptr);
    void removeFriend(int userId, std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 按用户名 / 昵称搜用户（添加好友页） */
    void searchUsers(const QString &keyword,
                     std::function<void(const QVector<ForumFriendInfo> &)> onOk,
                     FailHandler onFail = nullptr);

    // ── 私信 ──
    /**
     * 打开会话：论坛会顺手把对方发来的消息标成已读。
     * @param beforeId > 0 时往前翻更早的一页
     */
    void fetchMessages(int userId, int beforeId,
                       std::function<void(const ForumChatHistory &)> onOk,
                       FailHandler onFail = nullptr);

    /** 发一条文字消息；replyTo > 0 表示引用会话里的某条消息 */
    void sendMessage(int userId, const QString &body, int replyTo,
                     std::function<void(const ForumChatMessage &)> onOk,
                     FailHandler onFail = nullptr);

    /**
     * 发一张图片消息：先传到站内图床再发消息，两步都走完才回调 onOk。
     * 超大图会先在本地缩到 1600px、压到 4 MB 以内。
     */
    void sendImageMessage(int userId, const QString &filePath, const QString &caption, int replyTo,
                          std::function<void(const ForumChatMessage &)> onOk,
                          FailHandler onFail = nullptr);

    /** 撤回自己 10 分钟内发出的消息（服务端会变成「已撤回」占位） */
    void withdrawMessage(int messageId,
                         std::function<void()> onOk,
                         FailHandler onFail = nullptr);

    /**
     * 轻量轮询：角标数 + 会话列表 + 当前聊天的新消息。
     * 刻意不标已读——已读只发生在 fetchMessages 打开会话时。
     * @param peerId 当前打开的会话对象（0 表示没有打开任何会话）
     * @param afterId 当前会话已拿到的最大消息 id（0 表示从头拉）
     */
    void poll(int peerId, int afterId,
              std::function<void(const ForumPollResult &)> onOk,
              FailHandler onFail = nullptr);

private:
    explicit ForumChatService(QObject *parent = nullptr);

    static ForumFriendInfo parsePeer(const QJsonObject &object);
    static ForumChatMessage parseMessage(const QJsonObject &object);

    /** 把本地图片压缩到论坛图床的限制内（≤4MB、最长边 1600px），返回编码好的字节 */
    static bool prepareImagePayload(const QString &filePath, QByteArray &data, QString &mime, QString &error);
    void postMessage(int userId, const QJsonObject &payload,
                     std::function<void(const ForumChatMessage &)> onOk, FailHandler onFail);
    void uploadImage(const QByteArray &data, const QString &mime,
                     std::function<void(const QString &url)> onOk, FailHandler onFail);

    QNetworkAccessManager *m_network = nullptr;
};
