/**
 * @file   ForumThreadDetailPage.h
 * @brief  论坛帖子详情页：楼层 / 楼中楼 / 点赞 / 收藏 / 回复，资源帖 / 教程帖 / 导航帖用评论
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
 *   资源帖 / 教程帖 / 导航帖不盖楼：讨论区换成平铺评论（没有楼层号、没有楼中楼），评论框替代回复框
 *   导航帖（分享网址）走资源帖版面：左栏地址卡提供一键打开与复制地址，评论在右侧
 *
 * 数据来源：/api/threads/:id（分页，楼中楼嵌在楼层 children 里）。
 * 自己的楼层右键可编辑 / 删除，别人的楼层右键可举报；
 * 自己的主题可通过「管理」改标题标签或删除整帖；管理员还能置顶 / 锁定。
 */
#pragma once

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <functional>

#include "components/ForumKit.h"
#include "components/ForumPostBrowser.h"
#include "utils/ForumContentService.h"

class QBoxLayout;
class QComboBox;
class QFrame;
class QHideEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QStackedWidget;
class QTimer;
class QUrl;
class QVBoxLayout;
class McServerPinger;
class PlayerCountChart;

class ForumThreadDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumThreadDetailPage(QWidget *parent = nullptr);

    /** 打开一个帖子（从第 1 页开始加载） */
    void openThread(int threadId);

    /** 清空内容，避免切换帖子时短暂看到上一帖的残留 */
    void clearContent();

    /** 当前实例路径：投影帖「投影预览」按它取客户端方块材质（空则用纯色块） */
    void setInstancePath(const QString &instancePath);

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

    /** 投影下载完成：实例的投影列表可据此刷新 */
    void projectionSaved();

    /** 实例还没有投影目录、也没装投影模组：请求自动装好并把目录建出来 */
    void projectionModMissing();

    /** 点了「回到论坛」面包屑 */
    void backRequested();

    /** 右栏点开同版块的其他主题 */
    void threadOpenRequested(int threadId);

    /** 点了「举报」：打开整页举报流程（带上被举报楼层的信息） */
    void reportRequested(int postId, int floor, const QString &authorName, const QString &excerpt);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    /** 进入页面：服务器导航帖恢复人数探测 / 曲线刷新 */
    void showEvent(QShowEvent *event) override;
    /** 离开页面：停掉人数探测，别在后台一直 ping 服务器 */
    void hideEvent(QHideEvent *event) override;

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

    /** 教程帖：thread_tutorials 里有行（详情接口带 tutorial 标志 / 附件） */
    bool isTutorialThread() const { return m_info.tutorial || !m_info.attachments.isEmpty(); }

    /** 导航帖：thread_navigations 里有行（详情接口带 navigation 对象） */
    bool isNavigationThread() const
    {
        return m_info.navigation.isValid()
               || m_info.boardSlug == QLatin1String("website-nav")
               || m_info.boardSlug == QLatin1String("server-nav");
    }

    /** 讨论区走平铺评论（资源帖 / 教程帖 / 导航帖）：没有楼层号、没有楼中楼，也没有楼层回复框 */
    bool usesComments() const
    {
        return isResourceThread() || isTutorialThread() || isNavigationThread();
    }

    /** 展示用的评论数：评论接口拿过就按它算，还没拿到先按主题里带的数 */
    int commentCountForDisplay() const
    {
        return m_commentCountKnown ? m_commentTotal : m_info.comments;
    }

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
    /** 转载作品：主题头里的出处行（转载徽章 + 原链接 + 转载说明 + 认领入口） */
    void appendHeroSource();
    /** 转载认领：提交「认领该内容」 */
    void claimThreadContent();
    /** 转载认领：打开我自己的认领单（查看沟通 / 撤回） */
    void openMyClaim();
    /** 有奖问答：主题头里的赏金状态卡 */
    void appendHeroQuestionCard();
    void appendHeroAdmin();
    /** 插件帖左栏：下载卡片（按钮 / 进度 / 结果提示） */
    void appendPluginDownloadCard();
    /** 投影帖左栏：下载卡片（保存到所选位置） */
    void appendSchematicDownloadCard();
    /** 插件帖左栏：版本 / 插件 ID / 作者 / 大小 / 下载次数 + 浏览回复等 */
    void appendPluginInfoCard();
    /** 左栏「相关链接」卡：作者绑定的官网 / 仓库 / 反馈等入口（无链接不显示） */
    void appendResourceLinksCard(const QVector<ForumResourceLink> &links);
    /** 导航帖左栏：地址卡片（类型徽章 + 网址 + 一键打开 / 复制地址） */
    void appendNavigationCard();
    /** 服务器导航帖左栏：人数统计卡片（当前在线 + 最近 24 小时折线图） */
    void appendPlayerStatsCard();
    /** 按当前帖子解析服务器地址，重置人数统计状态并（可见时）开始探测 */
    void startServerStatsWatcher();
    /** 探测一次服务器人数并补拉曲线（60 秒一次；页面不可见时跳过） */
    void tickServerStats();
    /** 探测结果回填：在线人数 / 上限 / 延迟，在线时顺带上报一条采样 */
    void applyServerPingResult(bool online, int onlinePlayers, int maxPlayers, int latencyMs);
    /** 拉取最近 24 小时的人数曲线（拉取中 / 刚拉过时跳过） */
    void requestServerStatsHistory(bool force = false);
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
    // ── 评论（资源帖：插件 / 投影；教程帖也走这里） ──
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
    /** 主题头元信息行：教程帖显示评论数，普通帖显示楼层回复数 */
    void updateHeroStatsLabel();

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
    /**
     * @brief 下载投影：fileId > 0 时下选中的那个版本，否则第一份
     *        有当前实例时直接存进它的 schematics，没选实例时弹保存框
     */
    void downloadSchematic(int fileId = 0);
    /**
     * @brief 投影预览：先把投影取回到本地缓存，再弹三维预览
     * @param fileId > 0 时预览选中的那个版本，否则第一份
     */
    void previewSchematic(int fileId = 0);
    /**
     * @brief 材料列表：先把投影取回到本地缓存，再弹「材料清单」（备料前看要挖多少）
     * @param fileId > 0 时统计选中的那个版本，否则第一份
     */
    void showSchematicMaterials(int fileId = 0);
    /**
     * @brief 确保某份投影已在本地缓存：没有就按需取回，完成后回调缓存路径
     * @param fileId  > 0 时取选中的那个版本，否则第一份
     * @param purpose 取回过程中状态栏里的用途文案（如「预览」「统计材料」）
     * @param onReady 拿到可用缓存后调用（本来就缓存过也照常回调）；失败不回调
     */
    void ensureSchematicCached(int fileId, const QString &purpose,
                               const std::function<void(const QString &)> &onReady);
    /** 预览缓存路径（按帖 + 版本区分；同一份投影只下一次） */
    QString schematicPreviewCachePath(int fileId) const;
    /** 作者给投影帖补一份新版本（上传后绑定并命名） */
    void addSchematicVersion();
    /** 作者改某个投影版本的名字 */
    void renameSchematicVersion(const ForumSchematicFileInfo &file);
    /** 作者删掉某个投影版本（至少留一份） */
    void deleteSchematicVersion(const ForumSchematicFileInfo &file);
    /** 版本清单变化后同步回 m_info 并重建左栏 */
    void applySchematicFiles(const QVector<ForumSchematicFileInfo> &files);
    void startEditPost(const ForumPost &post);
    /** 有奖问答：采纳某一层回答，赏金当场发放 */
    void acceptAnswer(const ForumPost &post);
    void confirmDeletePost(const ForumPost &post);
    void confirmDeleteThread();
    void editThreadMeta();
    /** 轮播卡：按 m_info.carousel 重建（无轮播时整卡隐藏） */
    void renderCarousel();
    /** 翻到轮播第 index 页（循环），同步计数标签与缩略图高亮并重缩放当前图 */
    void showCarouselAt(int index);
    /** 按当前卡宽重缩放第 index 页的图片（图太大会撑出横向滚动条） */
    void rescaleCarouselSlide(int index);
    /** 舞台高度按卡宽换算（16:9，和资源详情页的截图区一样限制在 180~360） */
    void applyCarouselStageHeight();
    /** 缩略图高亮跟着当前页刷新 */
    void updateCarouselThumbs();
    /** 自动播放开关：只有多张、而且卡片真的露出来时才转 */
    void setCarouselAutoPlay(bool on);
    /** 手动翻页后先停一分钟再恢复，别刚点完就被自动翻走 */
    void noteCarouselInteraction();
    /** 作者编辑轮播：增删调序后整体提交，成功后重新拉取详情 */
    void editThreadCarousel();
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
    QPointer<QLabel> m_heroStatsLabel;   ///< 「发布于… · N 次浏览 · N 条回复 / 评论」那一行

    // 轮播卡（资源帖 / 导航帖可带）：和资源详情页的「游戏截图」轮播同一套样式
    // （标题 + 圆角大图 + 两侧圆形箭头 + 下方缩略图条 + 5 秒自动翻页）
    QFrame *m_carouselCard = nullptr;
    QWidget *m_carouselStage = nullptr;         ///< 16:9 圆角舞台（高度随卡宽换算）
    QStackedWidget *m_carouselStack = nullptr;
    QPushButton *m_carouselPrev = nullptr;      ///< 浮在舞台左右两侧的圆形箭头
    QPushButton *m_carouselNext = nullptr;
    QLabel *m_carouselCounter = nullptr;        ///< 标题行右侧的「1 / 3」
    QWidget *m_carouselThumbStrip = nullptr;    ///< 舞台下方的缩略图条
    QHBoxLayout *m_carouselThumbStripLayout = nullptr;
    QScrollArea *m_carouselThumbScroll = nullptr;
    QPushButton *m_carouselThumbLeft = nullptr;
    QPushButton *m_carouselThumbRight = nullptr;
    QTimer *m_carouselTimer = nullptr;
    QTimer *m_carouselResumeTimer = nullptr;    ///< 手动翻页后暂停，过一会儿再恢复自动播放
    QVector<QImage> m_carouselSlideImages;      ///< 图片项原图（按栈页序号存，翻页 / 缩放用）
    QVector<QLabel *> m_carouselThumbs;         ///< 缩略图（与栈页一一对应）
    QSet<QWidget *> m_carouselSlideLabels;      ///< 可点击看大图的图片页（eventFilter 按 it 识别）

    // 服务器导航帖左栏的「人数统计」卡（appendPlayerStatsCard 填充；侧栏重建后指针作废）
    QFrame *m_navStatsCard = nullptr;
    QLabel *m_navStatsValue = nullptr;
    QLabel *m_navStatsHint = nullptr;
    PlayerCountChart *m_navStatsChart = nullptr;
    McServerPinger *m_navPinger = nullptr;     ///< 服务器状态探测（懒创建时也复用同一个）
    QTimer *m_navStatsTimer = nullptr;         ///< 60 秒探测 + 补拉曲线
    QString m_navServerHost;
    quint16 m_navServerPort = 25565;
    QString m_navServerKey;                    ///< host:port（上报 / 查询用）
    qint64 m_navLastReportMs = 0;
    qint64 m_navLastFetchMs = 0;
    bool m_navFetching = false;
    bool m_navPinged = false;                  ///< 本次进入页面是否已经探测过一轮

    // 插件帖左栏的下载卡（appendPluginDownloadCard 填充）
    QPushButton *m_pluginInstallButton = nullptr;
    QLabel *m_pluginStatusLabel = nullptr;
    bool m_pluginBusy = false;
    /** 信息卡里的历史版本行（异步回填；侧栏重建后指针作废） */
    QVBoxLayout *m_pluginVersionsLayout = nullptr;
    bool m_pluginUploadingVersion = false;

    // 投影帖左栏的下载卡（appendSchematicDownloadCard 填充）
    QPushButton *m_schematicDownloadButton = nullptr;
    QPushButton *m_schematicPreviewButton = nullptr;   ///< 「投影预览」按钮（下载卡里）
    QPushButton *m_schematicMaterialButton = nullptr;  ///< 「材料列表」按钮（下载卡里）
    QComboBox *m_schematicVersionCombo = nullptr;   ///< 一帖多份投影时的版本下拉
    QLabel *m_schematicStatusLabel = nullptr;
    QVBoxLayout *m_schematicManageLayout = nullptr; ///< 作者版本管理行（改名 / 删除）
    bool m_schematicBusy = false;
    QString m_instancePath;   ///< 当前实例（预览时取其客户端材质）

    // 楼层
    QVBoxLayout *m_floorsLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;

    // 评论（资源帖用它替代楼层列表；评论分页沿用同一个 m_pager）
    int m_commentPage = 1;
    int m_commentPages = 1;
    int m_commentTotal = 0;
    bool m_commentCountKnown = false;   ///< 评论数是否已从评论接口拿到（拿到了就按它算，别用主题里的旧值）

    // 评论输入卡（资源帖 / 教程帖）
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
