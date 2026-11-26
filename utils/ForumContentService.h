/**
 * @file   ForumContentService.h
 * @brief  方块盒子论坛内容服务：帖子 / 回复 / 点赞 / 收藏 / 通知 / 搜索
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 社区页、帖子详情页、发帖页、用户主页等共用这一个入口。
 * 所有请求都转发给 ForumAccountService（自动带上登录 Cookie、
 * 论坛地址和统一的错误解析），本服务只负责拼接口与解析数据结构。
 *
 * 回调风格与 ForumAccountService 一致：onOk 返回解析好的结构体，
 * onFail 的 message 可直接展示给用户（论坛返回的中文错误原样透传）。
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "ForumAccountService.h"
#include "FavoritesManager.h"

/** 论坛接口统一的分页信息 */
struct ForumPagination
{
    int page = 1;
    int perPage = 20;
    int total = 0;
    int pages = 1;
};

/** 板块 */
struct ForumBoard
{
    int id = 0;
    QString slug;
    QString name;
    QString description;
    QString icon;      ///< emoji，网页版同款
    int threads = 0;
    int posts = 0;
};

/** 论坛热门标签（GET /api/tags）：编辑精选的排前面，其余按使用量 */
struct ForumTagInfo
{
    QString tag;
    int count = 0;        ///< 用了这个标签的主题数
    bool featured = false; ///< 是否被管理员设为精选标签
};

/** 插件帖 / 投影帖的相关链接（官网、GitHub 仓库、反馈等，作者发布或编辑时绑定） */
struct ForumResourceLink
{
    QString label;   ///< 展示名（如「官网」「GitHub 仓库」，自定义可随意填）
    QString url;     ///< 完整地址（http:// 或 https://）

    bool isValid() const { return !url.isEmpty(); }
};

/** 主题上挂载的插件包（发帖人上传后由服务端解清单，一帖一个） */
struct ForumPluginInfo
{
    int id = 0;                ///< thread_plugins 行 id（用于下载地址）
    int uploadId = 0;
    QString pluginId;          ///< 清单里的 id（安装判定同款）
    QString name;
    QString version;
    QString author;
    QString description;
    QString icon;              ///< 包内图标站内路径（/uploads/...），可空
    qint64 fileSize = 0;
    int downloads = 0;
    QVector<ForumResourceLink> links;   ///< 作者绑定的相关链接，老帖为空

    bool isValid() const { return id > 0 && !pluginId.isEmpty(); }
};

/** 投影帖里的一个投影文件（一帖可挂主世界 / 下界等好几份，各自命名） */
struct ForumSchematicFileInfo
{
    int id = 0;                ///< thread_schematic_files 行 id（下载时选版本用）
    int uploadId = 0;
    QString label;             ///< 用户起的版本名（空时退回文件名）
    QString fileName;          ///< 原文件名（.litematic / .schem 等）
    qint64 fileSize = 0;
    int downloads = 0;

    bool isValid() const { return id > 0; }
};

/** 主题上挂载的投影（发帖人上传后由服务端登记；主文件 + 全部版本清单） */
struct ForumSchematicInfo
{
    int id = 0;                ///< thread_schematics 行 id（用于下载地址）
    int uploadId = 0;
    QString name;              ///< 展示名
    QString fileName;          ///< 原文件名（.litematic / .schem 等）
    qint64 fileSize = 0;
    int downloads = 0;
    int fileCount = 0;         ///< 版本份数（列表接口给，老帖可能只有主文件）
    QVector<ForumSchematicFileInfo> files;   ///< 全部版本（详情接口给全）
    QVector<ForumResourceLink> links;   ///< 作者绑定的相关链接，老帖为空

    bool isValid() const { return id > 0; }

    /** 版本份数：详情接口拿 files 算，列表接口退回 file_count，都没有就是 1 份 */
    int versionCount() const
    {
        if (!files.isEmpty())
            return files.size();
        if (fileCount > 0)
            return fileCount;
        return isValid() ? 1 : 0;
    }
};

/** 教程帖挂载的资料附件（pdf / zip 等，一帖最多 10 个） */
struct ForumAttachmentInfo
{
    int id = 0;                ///< thread_attachments 行 id（用于下载地址）
    QString name;              ///< 展示名（去扩展名）
    QString fileName;          ///< 原文件名
    qint64 fileSize = 0;
    int downloads = 0;
    QString url;               ///< 站内路径（详情接口带回）

    bool isValid() const { return id > 0; }
};

/** 有奖问答的赏金状态（thread_questions 行；详情的剩余时间 / 中奖人在详情接口补齐） */
struct ForumQuestionInfo
{
    int bounty = 0;            ///< 托管的赏金（方块）
    QString status;            ///< open 托管中 / settled 已发放 / refunded 已退回
    qint64 deadline = 0;       ///< 到期自动按赞数结算的时间
    int winnerPostId = 0;      ///< 中奖楼层 id
    int winnerUserId = 0;
    QString winnerName;        ///< 详情接口带回的中奖人昵称
    QString winnerAvatar;
    int winnerFloor = 0;       ///< 中奖楼层号
    QString settleSource;      ///< author / admin / likes / refund / thread_deleted
    qint64 settledAt = 0;
    qint64 remaining = 0;      ///< 距自动结算还剩多少秒（详情接口才有）
    int daysLeft = 0;          ///< 剩余天数，向上取整
    bool canAccept = false;    ///< 当前登录用户能不能采纳（提问者 / 管理员）

    bool isValid() const { return bounty > 0 || !status.isEmpty(); }
    bool isOpen() const { return status == QStringLiteral("open"); }
    bool isSettled() const { return status == QStringLiteral("settled"); }
};

/** 导航帖分享的地址（thread_navigations 行；kind：website 网站 / server 服务器） */
struct ForumNavigationInfo
{
    QString kind;   ///< website（网站）/ server（服务器）
    QString url;    ///< 发布时填写的网址或服务器地址

    bool isValid() const { return !url.isEmpty(); }
    bool isServer() const { return kind == QStringLiteral("server"); }
};

/** 帖子封面（教程 / 资源 / 导航帖才有，作者发布时上传） */
struct ForumCoverInfo
{
    QString url;    ///< 原图（/uploads/…）
    QString thumb;  ///< 缩略图（/uploads/thumbs/…），瀑布流卡片只加载它
    QString cdn;    ///< 论坛配了 CDN 时服务端给的完整地址，优先用它（封面不经过论坛服务器）

    bool isValid() const { return !url.isEmpty() || !thumb.isEmpty(); }

    /** 列表卡片用的地址：CDN 优先，其次缩略图，最后才回退原图 */
    QString bestUrl() const
    {
        if (!cdn.isEmpty())
            return cdn;
        return !thumb.isEmpty() ? thumb : url;
    }
};

/** 帖子轮播里的一条展示图（资源帖 / 导航帖可带，一帖最多 8 条） */
struct ForumCarouselItem
{
    enum Type
    {
        Image,      ///< 图片：图床上传的（uploadId 有效）或外链（url 为完整 http(s) 地址）
        Bilibili    ///< B 站视频（url 为规范化后的 BV 链接，uploadId 无效）
    };

    Type type = Image;
    int uploadId = 0;    ///< image：图床记录 id（图床上传时有值，外链 / bilibili 为 0）
    QString url;         ///< image：/uploads/xxx 或外链完整地址；bilibili：https://www.bilibili.com/video/BVxxx
    QString thumb;       ///< image：缩略图路径（图床图可能有值，外链为空）；bilibili：空

    bool isImage() const { return type == Image; }
    bool isBilibili() const { return type == Bilibili; }
    bool isExternalImage() const { return isImage() && uploadId <= 0 && !url.isEmpty(); }
    bool isValid() const
    {
        return (isImage() && (uploadId > 0 || !url.isEmpty()))
               || (isBilibili() && !url.isEmpty());
    }
    /** 展示用 BV 号（bilibili 项才有） */
    QString bv() const;
};

/** 作品来源（原创 / 转载）：转载必须带原链接和转载说明 */
struct ForumWorkSource
{
    QString type;   ///< original（原创）/ repost（转载）；空串按原创处理
    QString url;    ///< 转载的原作品链接（转载时有值）
    QString note;   ///< 转载说明：来源、原作者或授权情况（转载时有值）

    bool isRepost() const { return type == QStringLiteral("repost"); }
};

/** 转载认领单：认领人提交，转载作者同意后作品转移给认领人 */
struct ForumClaimInfo
{
    int id = 0;
    int threadId = 0;
    QString threadTitle;
    QString message;   ///< 提交认领时写的说明
    QString status;    ///< pending / approved / rejected / cancelled
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    qint64 handledAt = 0;
    ForumUser claimant;   ///< 提交认领的人（自称原创作者）
    ForumUser author;     ///< 转载帖作者（作品当前持有人）
    int messageCount = 0;
    QString lastMessage;
    qint64 lastMessageAt = 0;

    bool isValid() const { return id > 0; }
    bool isPending() const { return status == QStringLiteral("pending"); }
};

/** 认领单里的一条沟通消息 */
struct ForumClaimMessage
{
    int id = 0;
    QString content;
    qint64 createdAt = 0;
    ForumUser sender;

    bool isValid() const { return id > 0; }
};

/** 认领详情（GET /api/claims/:id）：认领单 + 双方沟通记录 + 我的身份 */
struct ForumClaimDetail
{
    ForumClaimInfo claim;
    QVector<ForumClaimMessage> messages;
    QString myRole;   ///< claimant（认领人）/ author（转载作者）/ admin
};

/** 主题详情里的认领状态（只有转载帖才有） */
struct ForumThreadClaim
{
    bool present = false;   ///< 转载帖才会带认领信息
    bool canClaim = false;  ///< 当前登录用户能不能提交认领
    int mineId = 0;         ///< 我最近一次认领的 id（0 = 没认领过）
    QString mineStatus;     ///< 我最近一次认领的状态
    QString mineMessage;    ///< 我最近一次认领填的说明
    qint64 mineCreatedAt = 0;
    int incoming = 0;       ///< 我是本帖作者时：等待我处理的认领数量

    bool hasMine() const { return mineId > 0; }
};

/** 主题（列表行与帖子详情头部共用） */
struct ForumThreadInfo
{
    int id = 0;
    QString title;
    bool pinned = false;
    bool locked = false;
    int views = 0;
    int replies = 0;
    int comments = 0;   ///< 资源帖 / 教程帖的评论数（楼层回复数仍看 replies）
    qint64 createdAt = 0;
    qint64 lastPostAt = 0;
    QString boardSlug;
    QString boardName;
    int authorId = 0;
    QString authorName;
    QString authorAvatar;  ///< 主题作者头像，帖子详情头部用
    QStringList tags;
    bool bookmarked = false;
    QString snippet;   ///< 搜索结果带命中片段，其余场景为空
    ForumPluginInfo plugin;   ///< 插件帖才有（有插件 isValid() 为真）
    ForumSchematicInfo schematic;   ///< 投影帖才有（有投影 isValid() 为真）
    bool tutorial = false;   ///< 教程帖（thread_tutorials 里有行）；附件列表只在详情接口给全
    QVector<ForumAttachmentInfo> attachments;   ///< 教程帖的资料附件（详情接口才有）
    ForumQuestionInfo question;   ///< 有奖问答才有（isValid() 为真）
    ForumNavigationInfo navigation;   ///< 导航帖才有（isValid() 为真）
    ForumCoverInfo cover;   ///< 教程 / 资源 / 导航帖的作者封面（isValid() 为真）
    QVector<ForumCarouselItem> carousel;   ///< 轮播图（资源帖 / 导航帖可带，详情接口才有）
    ForumWorkSource source;   ///< 作品来源（原创 / 转载）；老接口不带 source 时按原创算
    ForumThreadClaim claim;   ///< 转载认领状态（只有转载帖才 present）

    bool isValid() const { return id > 0; }
};

/** 楼层（楼中楼挂在 children 里，论坛固定两层结构） */
struct ForumPost
{
    int id = 0;
    int floor = 0;
    int parentPostId = 0;   ///< 0 表示顶层楼层
    QString content;        ///< Markdown 原文
    int likes = 0;
    bool liked = false;
    bool deleted = false;
    int editCount = 0;      ///< 被编辑过几次，大于 0 时正文头部显示「已编辑」
    qint64 createdAt = 0;
    ForumUser author;
    QVector<ForumPost> children;

    bool isValid() const { return id > 0; }
};

/** 帖子详情（GET /api/threads/:id 的解析结果） */
struct ForumThreadDetail
{
    ForumThreadInfo info;
    QVector<ForumPost> posts;   ///< 顶层楼层，楼中楼在各楼层的 children 里
    ForumPagination pagination;
};

/**
 * 资源帖 / 教程帖的评论（GET /api/threads/:id/comments 的解析结果）
 *
 * 资源详情页与教程详情页都不盖楼：没有楼层号、没有楼中楼，只有一条条平铺的评论，
 * 排序由服务端按「点赞数 + 新鲜度」综合排好，客户端照顺序渲染即可。
 */
struct ForumComment
{
    int id = 0;
    int threadId = 0;
    QString content;      ///< Markdown 原文
    int likes = 0;
    bool liked = false;
    bool deleted = false;
    qint64 createdAt = 0;
    ForumUser author;

    bool isValid() const { return id > 0; }
};

/** 通知（同一主题 + 同一类型折叠成一组） */
struct ForumNotificationItem
{
    int id = 0;
    QString type;          ///< reply / mention / system / friend（好友申请）/ friend_accepted
    QString targetKind;    ///< thread（回复我的主题）/ post（回复我的楼层）/ friend / admin
    int threadId = 0;
    int postId = 0;
    QString threadTitle;
    QString excerpt;
    int count = 1;         ///< 折叠的条数
    QStringList actorNames;
    QVector<int> actorIds; ///< 通知里出现的操作人（好友申请用来找人）
    QVector<int> ids;      ///< 组内全部通知 id，点开时一次性标已读
    int requestId = 0;     ///< 好友申请记录 id（0 = 没有待处理的申请）
    bool unread = false;
    int unreadCount = 0;
    qint64 createdAt = 0;
};

/** 用户主页「回复」页签的条目（用户发过的回复，带所属主题） */
struct ForumUserReplyItem
{
    int threadId = 0;
    QString threadTitle;
    QString snippet;
};

/** 用户主页（GET /api/users/:id 的解析结果） */
struct ForumUserProfile
{
    ForumUser user;
    /** 我和TA的好友关系：none / self / friends / outgoing（我申请了）/ incoming（TA申请了我） */
    QString friendship;
    QVector<ForumThreadInfo> recentThreads;   ///< 最近 8 个主题
    QVector<ForumUserReplyItem> recentReplies;
};

/** 关注 / 取关的结果（POST / DELETE /api/users/:id/follow） */
struct ForumFollowResult
{
    bool following = false;    ///< 操作后我是否关注了 TA
    int followerCount = 0;     ///< TA 最新的粉丝数
    int followingCount = 0;    ///< 我最新的关注数
};

/** 插件包上传结果（POST /api/upload/plugin 的解析结果） */
struct ForumPluginUpload
{
    int uploadId = 0;
    QString pluginId;
    QString name;
    QString version;
    QString author;
    QString description;
    QString icon;
    qint64 fileSize = 0;
    QString fileHash;   ///< sha256，安装前比对用
    QString url;        ///< 站内路径，日志用

    bool isValid() const { return uploadId > 0; }
};

/** 插件包下载信息（GET /api/threads/:id/plugin/download 的解析结果） */
struct ForumPluginDownload
{
    QString url;          ///< 完整下载地址（配置了 CDN 时即 CDN 域名）
    qint64 fileSize = 0;
    QString filename;

    bool isValid() const { return !url.isEmpty(); }
};

/** 投影文件上传结果（POST /api/upload/schematic 的解析结果） */
struct ForumSchematicUpload
{
    int uploadId = 0;
    QString name;       ///< 展示名（服务端返回，缺省时客户端用原文件名）
    QString fileName;   ///< 原文件名（客户端在 X-File-Name 头里带给服务端）
    qint64 fileSize = 0;
    QString fileHash;   ///< sha256，服务端算好带回
    QString url;        ///< 站内路径，日志用
    QString label;      ///< 发帖时的版本名（如 主世界 / 下界），由发布页填写

    bool isValid() const { return uploadId > 0; }
};

/** 发帖时随帖绑定的一个投影文件：上传回执 + 版本名（主世界 / 下界…） */
struct ForumSchematicBinding
{
    int uploadId = 0;
    QString label;   ///< 版本名；空则由服务端退回文件名

    bool isValid() const { return uploadId > 0; }
};

/** 教程资料附件上传结果（POST /api/upload/file 的解析结果） */
struct ForumAttachmentUpload
{
    int uploadId = 0;
    QString name;       ///< 展示名（去扩展名）
    QString fileName;   ///< 原文件名
    qint64 fileSize = 0;
    QString fileHash;   ///< sha256，服务端算好带回
    QString url;        ///< 站内路径，日志用

    bool isValid() const { return uploadId > 0; }
};

/** 教程资料附件下载信息（GET /api/threads/:id/attachments/:aid/download 的解析结果） */
struct ForumAttachmentDownload
{
    QString url;          ///< 完整下载地址（配置了 CDN 时即 CDN 域名）
    qint64 fileSize = 0;
    QString filename;

    bool isValid() const { return !url.isEmpty(); }
};

/** 投影文件下载信息（GET /api/threads/:id/schematic/download 的解析结果） */
struct ForumSchematicDownload
{
    QString url;          ///< 完整下载地址（配置了 CDN 时即 CDN 域名）
    qint64 fileSize = 0;
    QString filename;
    int fileId = 0;       ///< 下的哪一份版本（多版本时对应 ForumSchematicFileInfo::id）
    QString label;        ///< 该版本的名字（主世界 / 下界…）

    bool isValid() const { return !url.isEmpty(); }
};

/** 插件作品的历史版本（GET /api/threads/:id/plugin/versions 的解析结果） */
struct ForumPluginVersionInfo
{
    int id = 0;
    qint64 uploadId = 0;
    QString version;
    qint64 fileSize = 0;
    int downloads = 0;
    qint64 createdAt = 0;
    bool current = false;   ///< 是否是正在展示 / 下载的最新版

    bool isValid() const { return id > 0; }
};

class ForumContentService : public QObject
{
    Q_OBJECT

public:
    using FailHandler = ForumAccountService::FailHandler;

    static ForumContentService *instance();

    // ── 浏览（匿名可用） ──
    void fetchBoards(std::function<void(const QVector<ForumBoard> &)> onOk,
                     FailHandler onFail = nullptr);

    /** 论坛热门标签：发帖页的标签推荐用（管理员精选的排前面，最多 20 个） */
    void fetchTags(std::function<void(const QVector<ForumTagInfo> &)> onOk,
                   FailHandler onFail = nullptr);

    /** 板块下的主题列表，sort = latest / new / hot */
    void fetchBoardThreads(const QString &slug, const QString &sort, int page,
                           std::function<void(const ForumBoard &, const QVector<ForumThreadInfo> &,
                                              const ForumPagination &)> onOk,
                           FailHandler onFail = nullptr);

    /** 按标签拉全站主题，sort = latest / new / hot */
    void fetchTagThreads(const QString &tag, const QString &sort, int page,
                         std::function<void(const QVector<ForumThreadInfo> &,
                                            const ForumPagination &)> onOk,
                         FailHandler onFail = nullptr);

    /**
     * @brief 全站搜索主题（服务端按标题 / 正文 / 标签匹配）
     * @param query  搜索关键词
     * @param page   页码（从 1 开始）
     * @param onOk   成功回调（回显关键词、结果列表、分页信息），结果带命中片段
     * @param onFail 失败回调
     * @param boardSlug 只看某个板块（空字符串 = 全站）；AI 按板块搜插件 / 投影时用
     */
    void searchThreads(const QString &query, int page,
                       std::function<void(const QString &, const QVector<ForumThreadInfo> &,
                                          const ForumPagination &)> onOk,
                       FailHandler onFail = nullptr,
                       const QString &boardSlug = QString());

    void fetchThread(int threadId, int page,
                     std::function<void(const ForumThreadDetail &)> onOk,
                     FailHandler onFail = nullptr);

    void fetchUser(int userId, std::function<void(const ForumUserProfile &)> onOk,
                   FailHandler onFail = nullptr);

    void fetchUserThreads(int userId, int page,
                          std::function<void(const QVector<ForumThreadInfo> &,
                                             const ForumPagination &)> onOk,
                          FailHandler onFail = nullptr);

    void fetchUserPosts(int userId, int page,
                        std::function<void(const QVector<ForumUserReplyItem> &,
                                           const ForumPagination &)> onOk,
                        FailHandler onFail = nullptr);

    /** 关注 / 取关某个用户：关注是单向的，不需要对方同意 */
    void setUserFollow(int userId, bool follow,
                       std::function<void(const ForumFollowResult &)> onOk,
                       FailHandler onFail = nullptr);

    /**
     * @brief 关注 / 粉丝名单
     * @param followers true = 粉丝（谁关注了 TA），false = 关注（TA 关注了谁）
     */
    void fetchUserFollows(int userId, bool followers, int page,
                          std::function<void(const QVector<ForumUser> &,
                                             const ForumPagination &)> onOk,
                          FailHandler onFail = nullptr);

    // ── 互动（需要登录，失败时论坛会返回中文原因） ──
    void createThread(const QString &boardSlug, const QString &title, const QString &content,
                      const QStringList &tags, std::function<void(int threadId)> onOk,
                      FailHandler onFail = nullptr);

    /** 带插件发帖：pluginUploadId 为 /api/upload/plugin 返回的上传 id；links 为绑定的相关链接；
     *  source 为作品来源（转载要带原链接和说明）；carousel 为轮播图（图片 / B 站视频，可空）；
     *  coverUploadId 为封面图上传 id（0 = 不要封面） */
    void createPluginThread(const QString &boardSlug, const QString &title, const QString &content,
                            const QStringList &tags, int pluginUploadId,
                            const QVector<ForumResourceLink> &links,
                            const ForumWorkSource &source,
                            const QVector<ForumCarouselItem> &carousel, int coverUploadId,
                            std::function<void(int threadId)> onOk, FailHandler onFail = nullptr);

    /** 带投影发帖：uploads 是随帖绑定的投影文件（一帖最多 8 份，各带版本名）；
     *  links 为绑定的相关链接；source 为作品来源（转载要带原链接和说明）；
     *  carousel 为轮播图（图片 / B 站视频，可空）；coverUploadId 为封面图上传 id（0 = 不要封面） */
    void createSchematicThread(const QString &boardSlug, const QString &title, const QString &content,
                               const QStringList &tags,
                               const QVector<ForumSchematicBinding> &uploads,
                               const QVector<ForumResourceLink> &links,
                               const ForumWorkSource &source,
                               const QVector<ForumCarouselItem> &carousel, int coverUploadId,
                               std::function<void(int threadId)> onOk, FailHandler onFail = nullptr);

    /** 发教程帖：固定发进「教程分享」板块，attachmentUploadIds 为 /api/upload/file 返回的上传 id；
     *  coverUploadId 为封面图上传 id（0 = 不要封面） */
    void createTutorialThread(const QString &title, const QString &content, const QStringList &tags,
                              const QVector<int> &attachmentUploadIds, int coverUploadId,
                              std::function<void(int threadId)> onOk, FailHandler onFail = nullptr);

    /** 发有奖问答：固定发进「有奖问答」板块，赏金在发布时就从账户里托管 */
    void createQuestionThread(const QString &title, const QString &content, const QStringList &tags,
                              int bounty, std::function<void(int threadId)> onOk,
                              FailHandler onFail = nullptr);

    /** 发导航帖：kind 为 website（网站）/ server（服务器），板块由服务端按类型固定指定；
     *  carousel 为轮播图（图片 / B 站视频，可空）；coverUploadId 为封面图上传 id（0 = 不要封面） */
    void createNavigationThread(const QString &kind, const QString &url, const QString &title,
                                const QString &content, const QStringList &tags,
                                const QVector<ForumCarouselItem> &carousel, int coverUploadId,
                                std::function<void(int threadId)> onOk, FailHandler onFail = nullptr);

    /** 采纳某一层回答：赏金当场发给那一层的作者（只有提问者 / 管理员能调） */
    void acceptQuestionAnswer(int threadId, int postId, std::function<void()> onOk,
                              FailHandler onFail = nullptr);

    // ── 插件帖（发帖人上传插件包；下载匿名可用，门禁在人机验证层） ──

    /** 上传一个 .BlockBox 插件包（服务端解清单为准）。progress 0~100，局域网里大包有用 */
    void uploadPluginPackage(const QString &filePath,
                             std::function<void(int percent)> onProgress,
                             std::function<void(const ForumPluginUpload &)> onOk,
                             FailHandler onFail = nullptr);

    /** 上传一个投影文件（.litematic / .schem 等），原文件名经 X-File-Name 头带给服务端 */
    void uploadSchematicFile(const QString &filePath,
                             std::function<void(int percent)> onProgress,
                             std::function<void(const ForumSchematicUpload &)> onOk,
                             FailHandler onFail = nullptr);

    /** 上传一个教程资料附件（pdf / zip / txt 等），原文件名经 X-File-Name 头带给服务端 */
    void uploadAttachmentFile(const QString &filePath,
                              std::function<void(int percent)> onProgress,
                              std::function<void(const ForumAttachmentUpload &)> onOk,
                              FailHandler onFail = nullptr);

    /** 取教程资料附件下载地址（CDN 域名或论坛自供），下载由调用方自己发请求 */
    void fetchAttachmentDownload(int threadId, int attachmentId,
                                 std::function<void(const ForumAttachmentDownload &)> onOk,
                                 FailHandler onFail = nullptr);

    /** 取投影下载地址（CDN 域名或论坛自供）；fileId > 0 时取该版本，否则第一份 */
    void fetchSchematicDownload(int threadId, int fileId,
                                std::function<void(const ForumSchematicDownload &)> onOk,
                                FailHandler onFail = nullptr);

    /** 作者给已发布的投影帖补一份新版本（先上传拿 uploadId，再绑到帖子里） */
    void addSchematicFile(int threadId, int uploadId, const QString &label,
                          std::function<void(const QVector<ForumSchematicFileInfo> &)> onOk,
                          FailHandler onFail = nullptr);

    /** 作者改某个投影版本的名字 */
    void renameSchematicFile(int threadId, int fileId, const QString &label,
                             std::function<void(const QVector<ForumSchematicFileInfo> &)> onOk,
                             FailHandler onFail = nullptr);

    /** 作者删掉一个投影版本（帖子里至少留一份） */
    void deleteSchematicFile(int threadId, int fileId,
                             std::function<void(const QVector<ForumSchematicFileInfo> &)> onOk,
                             FailHandler onFail = nullptr);

    /** 取插件下载地址（CDN 域名或论坛自供），下载由调用方自己发请求 */
    void fetchPluginDownload(int threadId,
                             std::function<void(const ForumPluginDownload &)> onOk,
                             FailHandler onFail = nullptr);

    /** 取插件作品的历史版本（含当前版，新的在前） */
    void fetchPluginVersions(int threadId,
                             std::function<void(const QVector<ForumPluginVersionInfo> &)> onOk,
                             FailHandler onFail = nullptr);

    /** 给已发布的插件作品上传新版本：先传包再绑定到主题（作者本人或管理员） */
    void uploadPluginVersion(int threadId, const QString &filePath,
                             std::function<void(int percent)> onProgress,
                             std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 回复主题；replyToPostId > 0 时是楼中楼（挂到该楼层下） */
    void replyToThread(int threadId, const QString &content, int replyToPostId,
                       std::function<void()> onOk, FailHandler onFail = nullptr);

    void togglePostLike(int postId, std::function<void(bool liked, int likes)> onOk,
                        FailHandler onFail = nullptr);

    void toggleThreadBookmark(int threadId, std::function<void(bool bookmarked)> onOk,
                              FailHandler onFail = nullptr);

    /**
     * 打赏主题：amount 2~10 个方块。方块转给作者，同时提升该帖推流，
     * onOk 带回打赏后的余额（服务端没给时为 -1）
     */
    void tipThread(int threadId, int amount, std::function<void(int points)> onOk,
                   FailHandler onFail = nullptr);

    // ── 资源收藏同步（需要登录；收藏启动器在线资源到论坛账号） ──

    /** 上报一条资源收藏（云端已存在则刷新内容） */
    void addContentFavorite(const FavoriteItem &item,
                            std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 取消一条资源收藏（source + remoteId 定位） */
    void removeContentFavorite(const QString &source, const QString &remoteId,
                               std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 资源收藏列表（按收藏时间倒序），total 为该账号收藏总数 */
    void fetchContentFavorites(int page, int perPage,
                               std::function<void(const QVector<FavoriteItem> &, int total)> onOk,
                               FailHandler onFail = nullptr);

    /** 批量补传本地收藏（只插入云端缺失的），返回新增数量 */
    void pushContentFavoritesBulk(const QVector<FavoriteItem> &items,
                                  std::function<void(int added)> onOk,
                                  FailHandler onFail = nullptr);

    void editPost(int postId, const QString &content, std::function<void()> onOk,
                  FailHandler onFail = nullptr);

    void deletePost(int postId, std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 改主题标题 / 标签 / 相关链接（作者本人或管理员）；links 仅插件帖、投影帖生效 */
    void editThread(int threadId, const QString &title, const QStringList &tags,
                    const QVector<ForumResourceLink> &links,
                    std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 整体替换主题的轮播图（作者本人或管理员）：空数组 = 清空；成功后重新拉详情刷新页面 */
    void updateThreadCarousel(int threadId, const QVector<ForumCarouselItem> &items,
                              std::function<void()> onOk, FailHandler onFail = nullptr);

    void deleteThread(int threadId, std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 举报楼层：category 为分类 slug（服务端白名单校验），reason 为补充说明（≤200 字） */
    void reportPost(int postId, const QString &category, const QString &reason,
                    std::function<void()> onOk, FailHandler onFail = nullptr);

    // ── 资源帖评论（插件帖 / 投影帖：没有楼层与楼中楼） ──

    /** 评论列表（服务端按「点赞 + 新鲜度」综合排好序） */
    void fetchComments(int threadId, int page,
                       std::function<void(const QVector<ForumComment> &, const ForumPagination &)> onOk,
                       FailHandler onFail = nullptr);

    /** 发表一条评论（需要登录 + 已通过入站答题） */
    void postComment(int threadId, const QString &content,
                     std::function<void(const ForumComment &)> onOk, FailHandler onFail = nullptr);

    void toggleCommentLike(int commentId, std::function<void(bool liked, int likes)> onOk,
                           FailHandler onFail = nullptr);

    /** 删除自己的评论（管理员也能删别人的） */
    void deleteComment(int commentId, std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 举报评论：category 为分类 slug，reason 为补充说明（≤200 字） */
    void reportComment(int commentId, const QString &category, const QString &reason,
                       std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 管理员：置顶 / 取消置顶主题 */
    void setThreadPinned(int threadId, bool pinned, std::function<void()> onOk,
                         FailHandler onFail = nullptr);

    /** 管理员：锁定 / 解锁主题（锁定后不能再回复） */
    void setThreadLocked(int threadId, bool locked, std::function<void()> onOk,
                         FailHandler onFail = nullptr);

    // ── 转载认领（原创作者认领被转载的作品） ──

    /** 提交认领：message 为给转载作者的说明（≤300 字） */
    void createClaim(int threadId, const QString &message,
                     std::function<void(const ForumClaimInfo &)> onOk, FailHandler onFail = nullptr);

    /**
     * @brief 认领列表
     * @param incoming true = 别人认领我的作品（创作中心），false = 我认领的作品
     * @param threadId > 0 时只看这个帖子；0 = 全部
     * @param status   pending 只看待处理 / all 全部（默认 all，待处理的排前面）
     */
    void fetchClaims(bool incoming, int threadId, const QString &status, int page,
                     std::function<void(const QVector<ForumClaimInfo> &, const ForumPagination &)> onOk,
                     FailHandler onFail = nullptr);

    /** 认领详情 + 双方沟通记录（只有认领人 / 转载作者 / 管理员能看） */
    void fetchClaim(int claimId, std::function<void(const ForumClaimDetail &)> onOk,
                    FailHandler onFail = nullptr);

    /** 在认领单里发一条消息（双方都能发，认领处理完后不能再发） */
    void sendClaimMessage(int claimId, const QString &content, std::function<void()> onOk,
                          FailHandler onFail = nullptr);

    /** 同意把作品转移给认领人（只有转载作者能调） */
    void approveClaim(int claimId, std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 拒绝认领：reply 可空（≤300 字，会作为一条消息记进沟通记录） */
    void rejectClaim(int claimId, const QString &reply, std::function<void()> onOk,
                     FailHandler onFail = nullptr);

    /** 认领人撤回认领 */
    void cancelClaim(int claimId, std::function<void()> onOk, FailHandler onFail = nullptr);

    // ── 通知 ──
    void fetchNotifications(int page,
                            std::function<void(const QVector<ForumNotificationItem> &, int unread,
                                               const ForumPagination &)> onOk,
                            FailHandler onFail = nullptr);

    /** ids 为空表示全部标为已读 */
    void markNotificationsRead(const QVector<int> &ids, std::function<void(int unread)> onOk,
                               FailHandler onFail = nullptr);

private:
    explicit ForumContentService(QObject *parent = nullptr);

    static ForumBoard parseBoard(const QJsonObject &object);
    static ForumThreadInfo parseThreadInfo(const QJsonObject &object);
    static ForumPluginInfo parsePluginInfo(const QJsonObject &object);
    static ForumSchematicInfo parseSchematicInfo(const QJsonObject &object);
    static QVector<ForumSchematicFileInfo> parseSchematicFiles(const QJsonValue &value);
    static ForumAttachmentInfo parseAttachmentInfo(const QJsonObject &object);
    static ForumQuestionInfo parseQuestionInfo(const QJsonObject &object);
    static ForumNavigationInfo parseNavigationInfo(const QJsonObject &object);
    static ForumPost parsePost(const QJsonObject &object);
    static ForumComment parseComment(const QJsonObject &object);
    static ForumClaimInfo parseClaim(const QJsonObject &object);
    static ForumClaimMessage parseClaimMessage(const QJsonObject &object);
    static ForumPagination parsePagination(const QJsonObject &object);
    static QString authorName(const QJsonObject &author);
    static ForumUser parseAuthor(const QJsonObject &author);
};
