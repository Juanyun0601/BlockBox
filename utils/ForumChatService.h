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
 * 聊天附件（图片/文件）与流量：/api/upload、/api/upload/chat、/api/chat/quota
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

/** 一个群聊（好友页会话列表里的群行） */
struct ForumGroupInfo
{
    int id = 0;
    QString name;
    /** 群图标：内置表情（如 👥）或站内图片地址（/uploads/…）；空 = 按群名首字占位 */
    QString avatar;
    int ownerId = 0;           ///< 群主用户 id（自己退出即解散时用来换提示文案）
    bool official = false;     ///< 官方全用户群（默认群聊，不能退出）
    int memberCount = 0;
    int unread = 0;            ///< 群里别人发的未读消息数
    qint64 lastMessageAt = 0;  ///< 最后一条群消息时间（0 = 还没聊过）
    QString lastMessageBody;   ///< 最后一条群消息内容
    QString lastMessageKind;   ///< text / image / withdrawn（预览用）
    QString lastMessageSender; ///< 最后一条群消息的发送者昵称（预览前缀用）
    bool lastMessageMine = false; ///< 最后一条是不是我发的

    bool isValid() const { return id > 0; }
};

/** 群成员（打开群会话时返回，带群主标记） */
struct ForumGroupMember
{
    ForumFriendInfo user;
    bool owner = false;        ///< 是不是群主

    bool isValid() const { return user.isValid(); }
};

/** 好友页一次拉全的快照（GET /api/friends 的解析结果） */
struct ForumFriendSnapshot
{
    QVector<ForumFriendInfo> friends;
    QVector<ForumFriendRequest> incoming;   ///< 收到的申请
    QVector<ForumFriendRequest> outgoing;   ///< 发出的申请
    QVector<ForumGroupInfo> groups;         ///< 我参与的群聊
    int totalUnread = 0;                    ///< 未读私信总数
};

/** 一条私信 */
struct ForumChatMessage
{
    int id = 0;
    QString kind;          ///< text / image / file / withdrawn（撤回占位）
    QString body;          ///< 文字内容（图片/文件消息的附言；withdrawn 时为空）
    QString image;         ///< 站内图片路径（kind=image 时有效），如 /uploads/img-1-xx.png
    QString fileName;      ///< 原始文件名（kind=file 时有效）
    QString fileUrl;       ///< 站内文件路径（kind=file 时有效），如 /uploads/chat/chat-1-xx.txt
    qint64 fileSize = 0;   ///< 文件字节数（kind=file 时有效）
    QString fileDlUrl;     ///< 群消息的下载地址：配了 CDN 就是 CDN 完整地址，没配是站内相对路径；私信不带
    int replyTo = 0;       ///< 引用的消息 id（0 = 没有引用）
    QString replyBody;     ///< 被引用消息的摘要（服务端处理好：[图片]/[文件]/[已撤回]/截断文字）
    bool replyMine = false;///< 被引用的消息是不是自己发的
    bool mine = false;     ///< 是不是我发的
    qint64 createdAt = 0;
    bool read = false;     ///< 对方是否已读（自己发的消息才有意义）
    int senderId = 0;      ///< 发送者 id（群消息才有，私信可以不看）
    QString senderName;    ///< 发送者昵称（群消息在气泡上方显示）
    QString senderAvatar;  ///< 发送者头像（群消息用）

    bool isImage() const { return kind == QStringLiteral("image") && !image.isEmpty(); }
    bool isFile() const { return kind == QStringLiteral("file") && !fileUrl.isEmpty(); }
    bool isWithdrawn() const { return kind == QStringLiteral("withdrawn"); }

    /**
     * 图片/文件的取回地址：群消息优先用服务端给的 dl_url（配了 CDN 即 CDN 域名，
     * 私聊「不走 CDN」所以服务端不给，走论坛自己的地址）。
     */
    QString mediaUrl() const
    {
        const QString path = isFile() ? fileUrl : image;
        if (path.isEmpty())
            return QString();
        if (!fileDlUrl.isEmpty() && fileDlUrl.startsWith(QStringLiteral("http")))
            return fileDlUrl;
        return ForumAccountService::instance()->absoluteUrl(path);
    }

    bool isValid() const { return id > 0; }
};

/** 提取（下载）一个聊天文件的结果 */
struct ForumChatFileExtract
{
    QString url;          ///< 站内相对路径（私聊从论坛服务器直连下载）
    QString dlUrl;        ///< 建议下载地址：群聊配了 CDN 即 CDN 完整地址，否则与 url 等价
    QString name;         ///< 原始文件名
    qint64 size = 0;      ///< 文件字节数
    qint64 remaining = 0; ///< 扣掉这次之后本周剩余的下载流量（字节）

    bool isValid() const { return !url.isEmpty(); }
    /** 实际下载用的地址（dlUrl 是完整 http 地址时直接用，否则拼论坛地址） */
    QString downloadUrl() const
    {
        if (dlUrl.startsWith(QStringLiteral("http")))
            return dlUrl;
        return ForumAccountService::instance()->absoluteUrl(url);
    }
};

/** 我本周的聊天文件下载流量（30MB/周，周一 0 点重置） */
struct ForumChatQuota
{
    qint64 used = 0;
    qint64 limit = 0;
    qint64 remaining = 0;

    bool isValid() const { return limit > 0; }
};

/** 打开会话（GET /api/messages/:userId 的解析结果） */
struct ForumChatHistory
{
    ForumFriendInfo peer;
    QVector<ForumChatMessage> messages;   ///< 按时间升序
    bool hasMore = false;                 ///< 还有更早的消息（用 before_id 往前翻）
};

/** 打开群会话（GET /api/groups/:id/messages 的解析结果） */
struct ForumGroupDetail
{
    ForumGroupInfo group;
    QVector<ForumGroupMember> members;    ///< 按入群先后，群主在前
    QVector<ForumChatMessage> messages;   ///< 按时间升序
    bool hasMore = false;
};

/** 轻量轮询（GET /api/messages/poll 的解析结果） */
struct ForumPollResult
{
    int totalUnread = 0;
    int incomingRequests = 0;
    QVector<ForumFriendInfo> friends;
    QVector<ForumGroupInfo> groups;
    QVector<ForumChatMessage> messages;       ///< 当前私聊会话的新消息（peerId > 0 时才有）
    QVector<ForumChatMessage> groupMessages;  ///< 当前群会话的新消息（groupId > 0 时才有）
};

class ForumChatService : public QObject
{
    Q_OBJECT

public:
    using FailHandler = ForumAccountService::FailHandler;
    /** 上传/下载进度回调：sent / total 字节（total 可能为 0 表示未知） */
    using ProgressHandler = std::function<void(qint64 sent, qint64 total)>;

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

    // ── 群聊 ──
    /**
     * 建群：从自己的好友里挑人（memberIds 1–49 个，不是好友的会被服务端跳过），
     * 自己自动进群并成为群主。
     * @param avatar 群图标：内置表情或已上传的站内图片地址，空 = 用群名首字
     */
    void createGroup(const QString &name, const QString &avatar, const QVector<int> &memberIds,
                     std::function<void(const ForumGroupInfo &)> onOk,
                     FailHandler onFail = nullptr);

    /**
     * 上传一张本地图片当群图标：先缩到 512px 以内再传图床，
     * 成功后回调站内地址（可直接作为 createGroup 的 avatar）。
     */
    void uploadGroupIcon(const QString &filePath, std::function<void(const QString &url)> onOk,
                         FailHandler onFail = nullptr);

    /** 邀请好友进已有的群：群里任何成员都能拉自己的好友（群总人数上限 50，服务端自动截断） */
    void inviteGroupMembers(int groupId, const QVector<int> &memberIds,
                            std::function<void(int added, int memberCount)> onOk,
                            FailHandler onFail = nullptr);

    /** 打开群会话：群信息 + 全量成员 + 一页历史；服务端顺手把我的未读清零 */
    void fetchGroupMessages(int groupId, int beforeId,
                            std::function<void(const ForumGroupDetail &)> onOk,
                            FailHandler onFail = nullptr);

    /** 发一条群文字消息；replyTo > 0 表示引用本群里的某条消息 */
    void sendGroupMessage(int groupId, const QString &body, int replyTo,
                          std::function<void(const ForumChatMessage &)> onOk,
                          FailHandler onFail = nullptr);

    /** 发一张群图片消息（与私信同款：先传图床再发消息，超大图先本地压缩） */
    void sendGroupImageMessage(int groupId, const QString &filePath, const QString &caption,
                               int replyTo, std::function<void(const ForumChatMessage &)> onOk,
                               FailHandler onFail = nullptr);

    /** 发一个群文件消息（先传 /api/upload/chat 再发消息，onProgress 汇报上传进度） */
    void sendGroupFileMessage(int groupId, const QString &filePath, const QString &caption,
                              int replyTo, const ProgressHandler &onProgress,
                              std::function<void(const ForumChatMessage &)> onOk,
                              FailHandler onFail = nullptr);

    /** 撤回自己 10 分钟内发出的群消息 */
    void withdrawGroupMessage(int messageId, std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 退出群聊；自己是群主时服务端会解散（onOk 的 dissolved = true） */
    void leaveGroup(int groupId, std::function<void(bool dissolved)> onOk,
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
     * 超大图会先在本地缩到 1600px、压到 5 MB 以内。
     */
    void sendImageMessage(int userId, const QString &filePath, const QString &caption, int replyTo,
                          std::function<void(const ForumChatMessage &)> onOk,
                          FailHandler onFail = nullptr);

    /** 发一个文件消息（先传 /api/upload/chat 再发消息，onProgress 汇报上传进度） */
    void sendFileMessage(int userId, const QString &filePath, const QString &caption, int replyTo,
                         const ProgressHandler &onProgress,
                         std::function<void(const ForumChatMessage &)> onOk,
                         FailHandler onFail = nullptr);

    /**
     * 提取（下载）一个聊天文件：先向论坛登记这次提取（扣本周 30MB 流量，下载自己发的不扣），
     * 成功后拿下载地址，再配合 downloadChatFile() 存到本地。
     * @param group true = 群文件消息，false = 私聊文件消息
     */
    void extractFile(bool group, int messageId,
                     std::function<void(const ForumChatFileExtract &)> onOk,
                     FailHandler onFail = nullptr);

    /** 把一个聊天文件下载到本地（urlOrPath 可为站内路径或 CDN 完整地址），带进度回调 */
    void downloadChatFile(const QString &urlOrPath, const QString &savePath,
                          const ProgressHandler &onProgress,
                          std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 查我本周剩余的下载流量（界面上提示「还能下载多少」用） */
    void fetchChatQuota(std::function<void(const ForumChatQuota &)> onOk, FailHandler onFail = nullptr);

    /**
     * 发送前的本地预检：文件存在、不超过 5MB、不是可执行类型。
     * 返回空串表示通过，否则是可直接展示给用户的错误说明。
     */
    static QString checkChatFile(const QString &filePath);

    /** 撤回自己 10 分钟内发出的消息（服务端会变成「已撤回」占位） */
    void withdrawMessage(int messageId,
                         std::function<void()> onOk,
                         FailHandler onFail = nullptr);

    /**
     * 轻量轮询：角标数 + 会话列表（好友与群）+ 当前聊天的新消息。
     * 刻意不标已读——已读只发生在 fetchMessages / fetchGroupMessages 打开会话时。
     * @param peerId 当前打开的私聊对象（0 表示没开）
     * @param groupId 当前打开的群聊 id（0 表示没开）
     * @param afterId 私聊已拿到的最大消息 id（0 表示从头拉）
     * @param groupAfterId 群聊已拿到的最大消息 id（0 表示从头拉）
     */
    void poll(int peerId, int groupId, int afterId, int groupAfterId,
              std::function<void(const ForumPollResult &)> onOk,
              FailHandler onFail = nullptr);

private:
    explicit ForumChatService(QObject *parent = nullptr);

    static ForumFriendInfo parsePeer(const QJsonObject &object);
    static ForumChatMessage parseMessage(const QJsonObject &object);
    static ForumGroupInfo parseGroup(const QJsonObject &object);
    /** 好友行（带未读 / 最后一条）与群行共用的解析 */
    static ForumFriendInfo parseFriendRow(const QJsonObject &object);

    /** 把本地图片压缩到论坛图床的限制内（≤5MB、最长边 1600px），返回编码好的字节 */
    static bool prepareImagePayload(const QString &filePath, QByteArray &data, QString &mime, QString &error);
    void postMessage(int userId, const QJsonObject &payload,
                     std::function<void(const ForumChatMessage &)> onOk, FailHandler onFail);
    void postGroupMessage(int groupId, const QJsonObject &payload,
                          std::function<void(const ForumChatMessage &)> onOk, FailHandler onFail);
    void uploadImage(const QByteArray &data, const QString &mime,
                     std::function<void(const QString &url)> onOk, FailHandler onFail);
    /** 传一个任意文件到 /api/upload/chat，成功后回调站内路径与原始文件名 */
    void uploadChatFile(const QString &filePath, const ProgressHandler &onProgress,
                        std::function<void(const QString &url, const QString &name)> onOk,
                        FailHandler onFail);

    QNetworkAccessManager *m_network = nullptr;
};
