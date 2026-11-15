/**
 * @file   ForumThreadDetailPage.h
 * @brief  论坛帖子详情页：楼层 / 楼中楼 / 点赞 / 收藏 / 回复
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 版面与网页版 thread.html 对齐：
 *   两栏：主栏 = 主题头 + 楼层列表 + 分页 + 回复框；右栏 = 主题信息 / 同版块主题 / 社区提示
 *   主题头：置顶锁定徽章 + 标题 + 板块胶囊 + 作者 + 时间 / 浏览 / 回复 + 收藏按钮 + 标签
 *   插件帖（帖子挂了 .BlockBox 插件包）与投影帖（挂了 .litematic / .schem 等投影文件）都换成
 *   资源详情页的版面：主题头通栏（大图标 + 名称 + 简介），左栏下载，右栏看介绍与原帖讨论
 *   楼层：左侧作者栏（头像 / 昵称 / 身份 / 发帖数）+ 右侧正文（楼主、时间、楼层号 + 正文 + 楼中楼 + 操作行）
 *   回复框：头像 + 回复主题标题，楼中楼回复在框内显示“正在回复谁”，支持 Ctrl + Enter 发送
 *
 * 数据来源：/api/threads/:id（分页，楼中楼嵌在楼层 children 里）。
 * 自己的楼层右键可编辑 / 删除，别人的楼层右键可举报；
 * 自己的主题可通过「管理」改标题标签或删除整帖；管理员还能置顶 / 锁定。
 */
#pragma once

#include <QHash>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include <functional>

#include "components/ForumKit.h"
#include "components/ForumPostBrowser.h"
#include "utils/ForumContentService.h"

class QBoxLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QUrl;
class QVBoxLayout;

class ForumThreadDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumThreadDetailPage(QWidget *parent = nullptr);

    /** 打开一个帖子（从第 1 页开始加载） */
    void openThread(int threadId);

    /** 清空内容，避免切换帖子时短暂看到上一帖的残留 */
    void clearContent();

    /** 当前帖子 id（0 表示尚未打开过） */
    int threadId() const { return m_threadId; }

signals:
    /** 点了板块名：打开该板块的主题列表 */
    void boardOpenRequested(const QString &slug, const QString &boardName);

    /** 点了标签：打开该标签的主题列表 */
    void tagOpenRequested(const QString &tag);

    /** 点了作者：打开用户主页 */
    void userOpenRequested(int userId);

    /** 未登录时想回复 / 点赞：请求跳到论坛账户页登录 */
    void loginRequested();

    /** 自己删除了主题，通知列表页等刷新 */
    void threadDeleted();

    /** 从帖子下载安装了一个插件：侧边栏插件列表需要刷新 */
    void pluginInstalled();

    /** 点了「回到论坛」面包屑 */
    void backRequested();

    /** 右栏点开同版块的其他主题 */
    void threadOpenRequested(int threadId);

    /** 点了「举报」：打开整页举报流程（带上被举报楼层的信息） */
    void reportRequested(int postId, int floor, const QString &authorName, const QString &excerpt);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct CardRef
    {
        QPointer<QWidget> card;
        QPointer<ForumPostBrowser> browser;
    };

    /**
     * 资源帖类型：插件帖 / 投影帖的详情页是「资源详情页」，
     * 讨论区换成平铺的评论（没有楼层号、没有楼中楼）。
     * 帖子本身没挂资源、但发在对应分享板块里的，也算这一类。
     */
    enum class ResourceKind
    {
        None,
        Plugin,
        Projection
    };
    ResourceKind resourceKind() const;
    bool isResourceThread() const { return resourceKind() != ResourceKind::None; }

    /** 教程帖：thread_tutorials 里有行（详情接口带 tutorial 标志 / 附件），回复走普通楼层 */
    bool isTutorialThread() const { return m_info.tutorial || !m_info.attachments.isEmpty(); }

    // ── 搭骨架 ──
    void buildUi();
    void buildComposerCard();
    void buildRail();
    void applyLayoutMode();
    /** 宽屏下把两栏收在一条阅读宽度里居中 */
    void applyPageWidth();
    /** 主题头放在整页顶部（通栏，资源详情页式）还是主栏内（论坛版面式） */
    void placeHero(bool atPageLevel);

    // ── 取数据 ──
    void loadPage(int page, bool scrollToBottomAfter = false);
    void reload(bool scrollToBottomAfter = false);
    void loadRailThreads(const QString &slug);

    // ── 渲染 ──
    void renderHero();
    void applyHeroSkin(bool resourceMode);
    void appendHeroFlags();
    void appendHeroTitleRow(bool resourceMode);
    void appendHeroMeta();
    void appendHeroTags();
    void appendHeroAdmin();
    /** 插件帖左栏：下载卡片（按钮 / 进度 / 结果提示） */
    void appendPluginDownloadCard();
    /** 投影帖左栏：下载卡片（保存到所选位置） */
    void appendSchematicDownloadCard();
    /** 插件帖左栏：版本 / 插件 ID / 作者 / 大小 / 下载次数 + 浏览回复等 */
    void appendPluginInfoCard();
    /** 左栏「相关链接」卡：作者绑定的官网 / 仓库 / 反馈等入口（无链接不显示） */
    void appendResourceLinksCard(const QVector<ForumResourceLink> &links);
    /** 作者给这个作品上传新版本插件包（旧包自动进历史版本） */
    void uploadNewPluginVersion();
    /** 拉历史版本列表并渲染到信息卡（失败静默：附加信息不打扰正文） */
    void loadPluginVersions();
    void renderPosts(const QVector<ForumPost> &posts);
    QWidget *buildPostRow(const ForumPost &post);
    QWidget *buildChildRow(const ForumPost &child);    void appendPostActions(QHBoxLayout *row, const ForumPost &post, bool isChild);
    void appendActButton(QHBoxLayout *row, const QString &text, bool danger,
                         std::function<void()> onClick);
    void setupCardMenu(QWidget *card, const ForumPost &post);
    /** 资源帖右栏的「详细信息」卡：作者发帖时写的介绍正文，摆在评论前面 */
    void renderBodyCard();
    // ── 教程帖右栏 ──
    /** 教程目录卡：正文一 / 二 / 三级标题自动生成，点击滚到对应章节 */
    void appendTutorialTocCard();
    /** 教程资料附件卡：pdf / 压缩包等，点一下取地址保存到本地 */
    void appendTutorialAttachmentsCard();
    // ── 评论（资源帖：插件 / 投影） ──
    /** 拉评论列表：服务端已按「点赞 + 新鲜度」综合排序 */
    void loadComments(int page);
    void renderComments(const QVector<ForumComment> &comments);
    QWidget *buildCommentRow(const ForumComment &comment);
    void buildCommentComposerCard();
    void renderCommentComposer();
    void submitComment();
    void confirmDeleteComment(const ForumComment &comment);
    void reportComment(const ForumComment &comment);
    /** 评论区的「共 N 条评论」小标题 */
    void updateCommentHeader();

    void renderReplyTarget();
    void renderComposer();
    void renderRail();
    /** 往侧栏加一张卡：返回卡片内的竖排布局，调用方往里填内容 */
    QVBoxLayout *addRailCard(const QString &title);
    /** 侧栏「同版块主题」卡（列表异步回填） */
    void appendRailThreadsCard();

    // ── 交互 ──
    void setReplyTarget(const ForumPost &post);
    void clearReplyTarget();
    void loadQuote(const ForumPost &post);
    void submitReply();
    void copyThreadLink();
    void copyPostLink(const ForumPost &post);
    void shareThread();
    void toggleBookmark();
    void tipThread();
    void togglePin();
    void toggleLock();
    void downloadAndInstallPlugin();
    void downloadSchematic();
    void startEditPost(const ForumPost &post);
    void confirmDeletePost(const ForumPost &post);
    void confirmDeleteThread();
    void editThreadMeta();
    void reportPost(const ForumPost &post);
    void openQuizDialog();
    void updateReplyBoxState();
    void updateReplyCount();
    void handleContentLink(const QUrl &url);
    void scrollToComposer();
    void setStatus(const QString &text, bool error = false);

    int m_threadId = 0;
    ForumThreadInfo m_info;
    int m_page = 1;
    int m_totalPages = 1;
    bool m_loaded = false;

    // 回复目标（楼中楼）
    int m_replyTargetPostId = 0;
    int m_replyTargetFloor = 0;
    QString m_replyTargetName;
    QString m_replyDraft;   ///< 重建回复框时保留已写内容
    int m_editingPostId = 0;
    QHash<int, CardRef> m_postCards;

    // ── UI ──
    QScrollArea *m_scroll = nullptr;
    QWidget *m_content = nullptr;
    QLabel *m_statusLabel = nullptr;

    // 两栏
    QBoxLayout *m_bodyLayout = nullptr;
    QVBoxLayout *m_pageLayout = nullptr;   ///< 整页竖排（主题头可提到这一层通栏）
    QWidget *m_mainColumn = nullptr;
    QWidget *m_railHost = nullptr;
    QVBoxLayout *m_mainLayout = nullptr;
    QVBoxLayout *m_railLayout = nullptr;
    QWidget *m_discussionHead = nullptr;   ///< 资源帖右栏「介绍与讨论」小标题（插件 / 投影）
    QLabel *m_discussionTitle = nullptr;
    QLabel *m_discussionCount = nullptr;
    // 资源帖的「详细信息」卡：作者发帖时写的介绍正文，摆评论前面
    QLabel *m_bodyTitle = nullptr;
    QFrame *m_bodyCard = nullptr;
    ForumPostBrowser *m_bodyBrowser = nullptr;
    QString m_bodyMarkdown;                ///< 正文缓存：翻评论页时楼层接口不再带 1 楼，别把介绍弄丢
    int m_layoutMode = -1;                 ///< 上次套用的版面（资源帖 / 折叠），避免 resize 时重复挪控件

    // 主题头
    QFrame *m_heroCard = nullptr;
    QVBoxLayout *m_heroBody = nullptr;
    QPushButton *m_bookmarkButton = nullptr;

    // 插件帖左栏的下载卡（appendPluginDownloadCard 填充）
    QPushButton *m_pluginInstallButton = nullptr;
    QLabel *m_pluginStatusLabel = nullptr;
    bool m_pluginBusy = false;
    /** 信息卡里的历史版本行（异步回填；侧栏重建后指针作废） */
    QVBoxLayout *m_pluginVersionsLayout = nullptr;
    bool m_pluginUploadingVersion = false;

    // 投影帖左栏的下载卡（appendSchematicDownloadCard 填充）
    QPushButton *m_schematicDownloadButton = nullptr;
    QLabel *m_schematicStatusLabel = nullptr;
    bool m_schematicBusy = false;

    // 楼层
    QVBoxLayout *m_floorsLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;

    // 评论（资源帖用它替代楼层列表；评论分页沿用同一个 m_pager）
    int m_commentPage = 1;
    int m_commentPages = 1;
    int m_commentTotal = 0;

    // 评论输入卡（资源帖）
    QFrame *m_commentComposerCard = nullptr;
    QVBoxLayout *m_commentComposerBody = nullptr;
    QPlainTextEdit *m_commentEdit = nullptr;
    QPushButton *m_commentSend = nullptr;
    QLabel *m_commentCharCount = nullptr;
    QString m_commentDraft;

    // 回复框
    QFrame *m_composerCard = nullptr;
    QVBoxLayout *m_composerBody = nullptr;
    QVBoxLayout *m_replyTargetHost = nullptr;
    QPlainTextEdit *m_replyEdit = nullptr;
    QPushButton *m_replySend = nullptr;
    QLabel *m_replyCount = nullptr;

    // 右栏同版块主题列表（异步回填用）
    QWidget *m_railThreadHost = nullptr;
    QVBoxLayout *m_railThreadList = nullptr;
    QHash<QWidget *, int> m_railThreadIds;   ///< 侧栏主题行 -> 主题 id（整行点击）
};
