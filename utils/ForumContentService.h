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

/** 主题上挂载的投影文件（发帖人上传后由服务端登记，一帖一个） */
struct ForumSchematicInfo
{
    int id = 0;                ///< thread_schematics 行 id（用于下载地址）
    int uploadId = 0;
    QString name;              ///< 展示名
    QString fileName;          ///< 原文件名（.litematic / .schem 等）
    qint64 fileSize = 0;
    int downloads = 0;
    QVector<ForumResourceLink> links;   ///< 作者绑定的相关链接，老帖为空

    bool isValid() const { return id > 0; }
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

/** 主题（列表行与帖子详情头部共用） */
struct ForumThreadInfo
{
    int id = 0;
    QString title;
    bool pinned = false;
    bool locked = false;
    int views = 0;
    int replies = 0;
    int comments = 0;   ///< 资源帖的评论数（楼层回复数仍看 replies）
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
 * 插件帖 / 投影帖的评论（GET /api/threads/:id/comments 的解析结果）
 *
 * 资源详情页不用楼层那一套：没有楼层号、没有楼中楼，只有一条条平铺的评论，
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
    QString type;          ///< reply / mention / system
    QString targetKind;    ///< thread（回复我的主题）/ post（回复我的楼层）
    int threadId = 0;
    int postId = 0;
    QString threadTitle;
    QString excerpt;
    int count = 1;         ///< 折叠的条数
    QStringList actorNames;
    QVector<int> ids;      ///< 组内全部通知 id，点开时一次性标已读
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

    void searchThreads(const QString &query, int page,
                       std::function<void(const QString &, const QVector<ForumThreadInfo> &,
                                          const ForumPagination &)> onOk,
                       FailHandler onFail = nullptr);

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

    // ── 互动（需要登录，失败时论坛会返回中文原因） ──
    void createThread(const QString &boardSlug, const QString &title, const QString &content,
                      const QStringList &tags, std::function<void(int threadId)> onOk,
                      FailHandler onFail = nullptr);

    /** 带插件发帖：pluginUploadId 为 /api/upload/plugin 返回的上传 id；links 为绑定的相关链接 */
    void createPluginThread(const QString &boardSlug, const QString &title, const QString &content,
                            const QStringList &tags, int pluginUploadId,
                            const QVector<ForumResourceLink> &links,
                            std::function<void(int threadId)> onOk, FailHandler onFail = nullptr);

    /** 带投影发帖：schematicUploadId 为 /api/upload/schematic 返回的上传 id；links 为绑定的相关链接 */
    void createSchematicThread(const QString &boardSlug, const QString &title, const QString &content,
                               const QStringList &tags, int schematicUploadId,
                               const QVector<ForumResourceLink> &links,
                               std::function<void(int threadId)> onOk, FailHandler onFail = nullptr);

    /** 发教程帖：固定发进「教程分享」板块，attachmentUploadIds 为 /api/upload/file 返回的上传 id */
    void createTutorialThread(const QString &title, const QString &content, const QStringList &tags,
                              const QVector<int> &attachmentUploadIds,
                              std::function<void(int threadId)> onOk, FailHandler onFail = nullptr);

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

    /** 取投影下载地址（CDN 域名或论坛自供），下载由调用方自己发请求 */
    void fetchSchematicDownload(int threadId,
                                std::function<void(const ForumSchematicDownload &)> onOk,
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
     * 打赏主题：amount 2~10 个方块。方块不转给作者，服务端用它提升该帖推流，
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
    static ForumAttachmentInfo parseAttachmentInfo(const QJsonObject &object);
    static ForumPost parsePost(const QJsonObject &object);
    static ForumComment parseComment(const QJsonObject &object);
    static ForumPagination parsePagination(const QJsonObject &object);
    static QString authorName(const QJsonObject &author);
    static ForumUser parseAuthor(const QJsonObject &author);
};
