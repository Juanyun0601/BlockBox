/**
 * @file   ForumAccountPage.h
 * @brief  账户管理页（论坛账户版）
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 与「方块盒子论坛」账号打通的账户管理界面，视觉风格对齐论坛网页版
 * （品牌绿、圆角卡片、胶囊按钮、个人资料卡 + 内容分栏）。
 *
 * 页面结构（顶部那条品牌 / 导航栏已去掉，直接复用启动器自己的顶栏）：
 *   资料卡：封面横幅 + 头像 + 名称（未登录时显示「未登录」）+ 简介 + 数据 + 右侧登录、注册
 * 简介按 Markdown 渲染（账号设置里可切到预览），和帖子正文同一套排版能力。
 *   内容卡：角色（游戏账户瀑布流）/ 我的主题 / 我的回复 / 我的收藏 / 账号设置
 *   登录卡：与论坛登录页同款的「登录 / 注册新账号 / 找回账号」表单
 *
 * 这是账户管理页的唯一界面：添加游戏账户走角色页右上角的「添加角色」整页，
 * 皮肤在「制作皮肤」里编辑，桌面端不再保留旧的经典账户管理页。
 */
#pragma once

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QList>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QWidget>

#include "utils/ForumContentService.h"
#include "utils/FeedbackService.h"
#include "utils/LocalFavoritesManager.h"

#include "components/Skin3DWidget.h"

class ForumAccountService;
class ForumPostBrowser;
class PlaytimeStatsPage;
class MasonryLayout;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QHideEvent;
class QShowEvent;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

class FlowLayout;

namespace ForumKit
{
class CoverBanner;
}

class ForumAccountPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumAccountPage(QWidget *parent = nullptr);
    ~ForumAccountPage() override;

    /** 当前展示的账号名（未登录时是「未登录」） */
    QString currentAccountName() const;

    /**
     * @brief 关闭登录 / 注册子视图，退回账户总览
     * @return 是否确实在子视图里（true 表示这次返回被本页消化，顶栏不用退出页面）
     */
    bool returnToOverview();

public slots:
    /** 顶栏返回键：从登录 / 资料设置等子视图回到账户总览 */
    void onBackToAccountList();

    /** 重新拉取论坛账号资料与各项数量 */
    void refreshAccount();

public:
    /** 从社区页「通知」按钮跳进来：切到通知页签 */
    void openNotificationsTab();

    /** 打开「游玩」页签（年度报告页返回、首页/侧边栏入口都用它） */
    void openPlaytimeTab();

    /** 未登录想发帖 / 回复时跳进来：直接展示登录表单 */
    void openLoginForm();

    /** 打开某个游戏账户的详情页（角色卡片双击 / 外部跳转） */
    void openAccountDetail(const QString &accountName);

signals:
    void backToMainRequested();
    /** 「游玩」页签里点了年度报告卡片：主窗口打开我的 MC 年度报告整页 */
    void annualReportRequested(int year);
    void accountManagePageOpened();

    /** 资料卡点「创作中心」：打开自己发布作品的管理页 */
    void creationCenterRequested();

    /** 资料卡点「皮肤站」：在启动器内整页打开方块皮肤站（不再弹独立窗口） */
    void skinStationRequested();

    /** 角色页「添加角色」/ 空态按钮：打开账户管理页内层的「添加角色」页 */
    void addGameAccountRequested();

    /** 点击自己的主题 / 回复 / 收藏 / 通知：在启动器内打开帖子详情 */
    void threadOpenRequested(int threadId);

    /** 点击「我的收藏 → 资源收藏」里的一条资源：打开资源详情页 */
    void contentFavoriteOpenRequested(const ModInfo &info, ContentType type);

    /** 点击「我的收藏 → 本地收藏」里的一条资源：模组打开本地详情，其余打开所在文件夹 */
    void localFavoriteOpenRequested(const LocalFavoriteEntry &entry);

    /**
     * @brief 在「角色」页切换了游戏账户
     * @param accountName 账户名
     * @param avatar      由该账户皮肤裁出的头像（供顶栏同步）
     */
    void gameAccountSelected(const QString &accountName, const QPixmap &avatar);

    /** 账户详情页里点「打开皮肤编辑器」：带上该账户，应用皮肤时不会落到别的账户上 */
    void skinEditorRequested(const QString &accountName, const QString &accountType);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    /** 详情页 3D 预览随尺寸自动调整相机距离，保证整只模型完整可见 */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum ViewIndex { OverviewView = 0, AuthView = 1, DetailView = 2 };
    enum TabIndex { CharactersTab = 0, ThreadsTab = 1, RepliesTab = 2, BookmarksTab = 3,
                    NotificationsTab = 4, FeedbackTab = 5, PointsTab = 6, MedalsTab = 7,
                    PlaytimeTab = 8, MemberTab = 9, WalletTab = 10, BillsTab = 11,
                    SettingsTab = 12 };
    enum ContentIndex { CharactersContent = 0, ThreadsContent = 1, RepliesContent = 2,
                        BookmarksContent = 3, NotificationsContent = 4, FeedbackContent = 5,
                        PointsContent = 6, MedalsContent = 7, PlaytimeContent = 8,
                        MemberContent = 9, WalletContent = 10, BillsContent = 11,
                        SettingsContent = 12, LoggedOutContent = 13 };
    enum AuthMode { LoginMode = 0, RegisterMode = 1, RecoverMode = 2 };
    /** 「我的收藏」页签内的子页：资源收藏（在线资源同步） / 帖子收藏（论坛主题） / 本地收藏（实例资源） */
    enum BookmarkSubTab { BookmarkResourceTab = 0, BookmarkThreadTab = 1, BookmarkLocalTab = 2 };

    struct Palette
    {
        bool dark = false;
        QString bg;
        QString bgTint;
        QString card;
        QString line;
        QString line2;
        QString ink;
        QString ink2;
        QString ink3;
        QString brand;
        QString brandHover;
        QString brandSoft;
        QString brandLine;
        QString onBrand;
        QString danger;
        QString dangerSoft;
        QString warn;
        QString warnSoft;
    };

    // ── 构建 ──
    void buildUi();
    QWidget *buildOverview(QWidget *parent);
    QWidget *buildProfileCard(QWidget *parent);
    QWidget *buildTabsCard(QWidget *parent);
    QWidget *buildAuthCard(QWidget *parent);
    QWidget *buildLoggedOutContent(QWidget *parent);
    QWidget *buildSettingsContent(QWidget *parent);
    QWidget *buildListContent(QWidget *parent, QVBoxLayout **outLayout);
    /** 「我的收藏」页：资源收藏 / 帖子收藏 双子页 */
    QWidget *buildBookmarksContent(QWidget *parent);
    /** 「通知」页：谁回复 / 提到了我，点击直达帖子并标已读 */
    QWidget *buildNotificationsContent(QWidget *parent);
    /** 「我的反馈」页：启动器 / 网页提交的反馈和处理进度 */
    QWidget *buildFeedbackContent(QWidget *parent);
    /** 「积分」页：方块余额、7 天签到进度与签到按钮 */
    QWidget *buildPointsContent(QWidget *parent);
    /** 「会员」页：支持者 / 体验会员 / 标准会员 × 月 / 季 / 年 */
    QWidget *buildMemberContent(QWidget *parent);
    /** 「余额」页：余额、充值档位、最近账单 */
    QWidget *buildWalletContent(QWidget *parent);
    /** 「账单」页：充值 / 消费 / 创作分成明细 */
    QWidget *buildBillsContent(QWidget *parent);
    /** 「勋章」页：和方块盒子账号绑定的勋章墙（服务端判定，本地只负责展示） */
    QWidget *buildMedalsContent(QWidget *parent);
    /** 「游玩」页：本机游玩时长统计 + 年度报告入口（嵌的是 PlaytimeStatsPage，不带自己的滚动区） */
    QWidget *buildPlaytimeContent(QWidget *parent);
    /** 「角色」页：游戏账户瀑布流卡片 */
    QWidget *buildCharactersContent(QWidget *parent);
    /** 账户详情页：左侧 3D 皮肤预览 + 右侧资料 / 模型动作 / 账户操作 */
    QWidget *buildAccountDetailView(QWidget *parent);

    /** 按当前主题（亮 / 暗）生成与论坛网页一致的配色和 QSS */
    void applyTheme();
    Palette currentPalette() const;

    // ── 视图切换 ──
    void showOverview();
    void openAuth(AuthMode mode);
    void setAuthMode(AuthMode mode);
    void submitAuth();

    // ── 登录 / 注册 / 找回卡上的邮箱验证 ──
    /** 给卡上填的邮箱发验证码（注册走注册发码接口，找回走找回发码接口） */
    void sendAuthEmailCode();
    /** 发码按钮「重新发送」倒计时每秒走一下 */
    void tickRegisterCodeCooldown();
    /** 按发码状态刷新卡上的按钮文案与可点状态 */
    void updateRegisterCodeUi();
    /** 在登录 / 注册卡顶部显示一条错误，并把视图栈高度重新收紧 */
    void showAuthError(const QString &message);

    // ── 注册卡的密码强度 ──
    /** 粗算密码强度：0 = 还没输入，1 弱 / 2 一般 / 3 强 / 4 很强 */
    int passwordStrengthScore(const QString &password) const;
    /** 按密码框里的内容刷新注册卡上的四段强度条和提示文字 */
    void updatePasswordStrengthUi();

    // ── 第三方登录（GitHub / LittleSkin） ──
    /** 按服务端返回的启用状态显示 / 隐藏登录卡上的第三方入口 */
    void renderOAuthProviders(const QJsonArray &providers);
    /** 点第三方按钮：确认协议后向论坛申请设备码并打开浏览器 */
    void startOAuthLogin(const QString &provider);
    /** 进入 / 退出「等待浏览器授权」状态，按钮和提示跟着换 */
    void setOAuthWaiting(bool waiting);

    // ── 手机扫码登录 ──
    /**
     * @brief 点「手机扫码登录」：确认协议后弹出二维码弹窗
     *
     * 二维码的申请、轮询与作废都在弹窗里，本页只管把弹窗叫起来；
     * 登录成功后由服务端的 userChanged 走本页原有刷新逻辑。
     */
    void openQrLoginDialog();
    /** 更新账号设置里的「修改密码 / 设置密码」文案与旧密码栏 */
    void updatePasswordUi();
    void setActiveTab(TabIndex tab);
    /** 登录 / 注册卡按内容高度收紧视图栈，窗口不够高时也不冒出竖向滚动条 */
    void syncAuthViewHeight();
    /** 退出登录卡：取消收紧，让总览 / 详情页恢复各自的高度 */
    void releaseAuthViewHeight();

    // ── 资料渲染 ──
    void renderProfile();
    /** 资料卡简介里的链接：站内帖子在启动器内打开，其余交给系统浏览器 */
    void handleBioLink(const QUrl &url);
    void setAvatarPlaceholder(const QString &name);
    void setAvatarPixmap(const QPixmap &pixmap);
    void loadRemoteAvatar(const QString &avatarPath);

    // ── 内容渲染 ──
    void renderContentArea();
    /** 切换到页签内容区的某一页（-1 = 什么都不显示） */
    void showContentPage(int index);

    // ── 角色页（游戏账户） ──
    /** 重建瀑布流卡片（账户增删、切换后调用） */
    void reloadCharacters();
    /** 把某个游戏账户设为当前账户 */
    void switchGameAccount(const QString &accountName);
    /** 取账户皮肤：内存缓存 > 离线 / 缓存文件 > 官方默认皮肤 */
    QImage skinForAccount(const QString &accountName, const QString &uuid);
    /** 本地没有皮肤时，按 UUID / 皮肤地址异步下载一次 */
    void requestRemoteSkin(const QString &accountName, const QString &uuid, const QString &skinUrl);
    /** 画一张封面：品牌绿渐变 + 正面皮肤模型 */
    QPixmap renderCharacterCover(const QImage &skin, int width, int height);

    // ── 账户详情页 ──
    /** 重新读取当前详情账户的资料并刷新右侧面板 / 3D 皮肤 */
    void refreshAccountDetail();
    void setDetailPose(SkinPose pose, const QString &tip = QString());
    void refreshDetailPoseIcons();
    QColor detailPoseIconColor(bool checked) const;
    void executeDetailAction();
    void deleteDetailAccount();
    /** 当前详情账户是不是离线账户（只有离线账户能自定义皮肤 / 披风） */
    bool detailAccountIsOffline() const;
    /** 当前详情账户是不是方块皮肤站角色（服务器地址对上皮肤站就是） */
    bool detailAccountIsSkinStation() const;
    /** 刷新「皮肤与披风」两行的状态文字 / 清除按钮显隐 */
    void refreshDetailOutfit();
    /** 选一张本地 PNG 作为这个离线账户的皮肤 */
    void pickDetailSkin();
    /** 选一张本地 PNG 作为这个离线账户的披风 */
    void pickDetailCape();
    /** 删掉自定义皮肤，恢复默认皮肤 */
    void clearDetailSkin();
    /** 删掉自定义披风 */
    void clearDetailCape();
    /** 皮肤站角色详情：拉一次 /api/me，同步当前皮肤披风状态和预览 */
    void refreshStationProfile();
    /** 处理皮肤站返回的最新档案：状态文字、披风预览、皮肤地址对齐 */
    void handleStationProfile(const QJsonObject &profile);
    /** 给详情页的皮肤站角色换装：选本地 PNG → 传到皮肤站 → 立刻穿上 */
    void uploadStationTexture(const QString &kind);
    /** 皮肤站换装成功后清掉本地旧皮肤缓存，按新地址重新下载皮肤并刷新预览 */
    void refreshStationAppearance(const QString &skinUrl);
    /** 皮肤站接口请求头（论坛登录令牌直连皮肤站） */
    QNetworkRequest stationRequestFor(const QString &path) const;
    /** 皮肤 / 披风文件变了以后，3D 预览和角色卡片封面跟着换 */
    void refreshDetailAppearance();
    void useDetailAccountAsCurrent();
    void requestDetailSkin(const QString &accountName, const QString &uuid, const QString &skinUrl);
    void updateDetailBackground();
    /** 按视口纵横比反推相机距离，让 3D 预览里的模型完整可见 */
    void fitDetailCamera();
    void renderThreads();
    void renderReplies();
    void renderBookmarks();
    /** 「我的收藏」子页切换（资源收藏 / 帖子收藏） */
    void setBookmarkSubTab(int subTab);
    void renderContentFavorites();
    void loadContentFavorites();
    /** 「本地收藏」子页：实例内收藏的模组/存档/资源包等（LocalFavoritesManager，仅本地） */
    void renderLocalFavorites();
    /** 移除一条资源收藏：云端删除 + 本地所有收藏夹同步移除 */
    void removeAccountContentFavorite(const FavoriteItem &item);
    /** 异步下载资源图标，成功后缓存并回填到 label */
    void requestContentIcon(const QString &url, QLabel *label);
    /** 「收藏」统计 = 论坛帖子收藏 + 资源收藏 之和 */
    void refreshBookmarkStat();
    void renderNotifications();
    void loadNotifications();
    void markAllNotificationsRead();
    /** 「我的反馈」：拉取当前账号的反馈列表并渲染 */
    void loadMyFeedbacks(bool force = false);
    void renderMyFeedbacks();
    /** 「积分」：拉取方块余额与签到进度、渲染、签到 */
    void loadPoints(bool force = false);    void renderPoints();
    /** 「勋章」：拉取账号勋章墙，结算新勋章并提示 */
    void loadMedals(bool force = false);
    void renderMedals();
    /** 「勋章」：把当前勋章墙排成一张竖版分享图并弹出预览 */
    void openMedalShareImage();
    /** 清空勋章墙上的卡片（只删卡片，不动承载布局） */
    void clearMedalTiles();
    void checkInPoints();
    /** 用方块购买补签卡 / 用一张补签卡补上昨天 */
    void buyMakeupCard();
    void useMakeupCard();
    /** 「积分」：购买方块（档位列表 / 下单 / 打开支付页 / 轮询到账） */
    void loadPointsShop();
    void renderPointsShop(const QJsonObject &shop);
    /** 「积分」：会员档位（支持者 / 体验 / 标准），用同一份购买数据渲染 */
    void renderPointsMembers(const QJsonObject &shop);
    void buyPointsPackage(const QString &packageId, QPushButton *button);
    /** 「会员 / 余额」：弹支付方式（微信 / 支付宝 / 余额），选定后下单
     *  @param upgrade      true = 会员升级补差价（服务端按剩余价值折算）
     *  @param customAmount 自定义充值金额（元），填了就按金额下单 */
    void startCheckout(const QString &productId, const QString &title, int priceCents,
                       bool upgrade = false, const QString &customAmount = QString());
    /** 会员页：拉档位和当前身份 */
    void loadMemberPlans(bool force = false);
    void renderMemberPlans();
    /** 余额页：拉余额、充值档位、最近账单 */
    void loadWallet(bool force = false);
    void renderWallet();
    /** 账单页：拉流水和创作池汇总 */
    void loadBills(bool force = false);
    void renderBills();
    void startPayPolling(const QString &outTradeNo, int points, const QString &title,
                         const QString &payUrl);
    void stopPayPolling();
    void checkPayOrder(bool manual);
    /** 按签到状态刷新 7 张日期卡的选中 / 已领样式 */
    void refreshPointsDayCards();
    void renderLoading(QVBoxLayout *layout);
    void renderRows(QVBoxLayout *layout, const QJsonArray &items, int kind);
    void loadTabData();
    void fillSettingsForm();

    // ── 账号操作 ──
    void saveProfileForm();
    void savePasswordForm();
    void pickAvatarFile();
    void pickCoverFile();
    void confirmLogout();

    // ── 邮箱验证 ──
    void sendEmailCode();
    void verifyEmailCode();
    /** 按登录状态 / 验证状态 / 冷却进度刷新徽标和两个按钮 */
    void updateEmailVerifyUi();
    void tickCodeCooldown();

    void clearLayout(QVBoxLayout *layout);
    void setTabHint(const QString &text);
    void scrollToTop();

    // ── 首次进入的聚光灯引导 ──
    /** 进页面时排一次引导（只有没看过才排，且会等入场动画落定） */
    void scheduleAccountTour();
    void startAccountTourIfNeeded();
    /** 走完 / 跳过：记下「已看过」，以后不再打扰 */
    void finishAccountTour();
    /** 中途离开本页：收掉遮罩，不记「已看过」，下次进来重讲 */
    void cancelAccountTour();

    ForumAccountService *m_service = nullptr;
    QNetworkAccessManager *m_network = nullptr;   // 仅用于下载论坛头像

    // 页面骨架
    QScrollArea *m_scroll = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    /** 登录卡上方的弹性留白：只在登录 / 注册视图显示，把卡片竖直居中 */
    QWidget *m_authTopSpacer = nullptr;
    /** 页签内容区：手工换页，避免 QStackedWidget 用最长页面把卡片撑高 */
    QWidget *m_contentHost = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QList<QWidget *> m_contentPages;
    int m_contentPage = -1;

    // 资料卡
    QFrame *m_profileCard = nullptr;
    /** 资料卡顶部的封面横幅（没有封面时画主题色渐变） */
    ForumKit::CoverBanner *m_coverBanner = nullptr;
    QLabel *m_avatarLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_usernameLabel = nullptr;
    /** 个人简介：按 Markdown 渲染（加粗、代码、列表、链接、图片都认） */
    ForumPostBrowser *m_bioBrowser = nullptr;
    QLabel *m_roleChip = nullptr;
    /** 会员标识（支持者 / 体验会员 / 标准会员），没有会员时隐藏 */
    QLabel *m_memberChip = nullptr;
    QLabel *m_statThreads = nullptr;
    QLabel *m_statPosts = nullptr;
    QLabel *m_statBookmarks = nullptr;
    QLabel *m_statPoints = nullptr;
    QLabel *m_statJoined = nullptr;
    QLabel *m_statGameAccounts = nullptr;
    QWidget *m_profileAuthActions = nullptr;   // 未登录：登录 / 注册
    QWidget *m_profileUserActions = nullptr;   // 已登录：编辑资料 / 退出登录

    // 内容卡
    QList<QPushButton *> m_tabButtons;
    /** 「我的收藏」内层切换按钮（资源收藏 / 帖子收藏） */
    QList<QPushButton *> m_bookmarkSubButtons;
    QLabel *m_tabHint = nullptr;
    /** 页签卡（角色页 + 各列表页的外框）：首次引导高亮这块用 */
    QWidget *m_tabsCard = nullptr;
    /** 首次引导遮罩（AccountTourOverlay，定义在 .cpp 里） */
    QWidget *m_tourOverlay = nullptr;
    bool m_tourScheduled = false;
    QVBoxLayout *m_threadListLayout = nullptr;
    QVBoxLayout *m_replyListLayout = nullptr;
    QVBoxLayout *m_bookmarkListLayout = nullptr;
    QVBoxLayout *m_notificationListLayout = nullptr;
    QPushButton *m_markAllReadButton = nullptr;
    QVBoxLayout *m_feedbackListLayout = nullptr;
    QPushButton *m_feedbackRefreshButton = nullptr;
    // 积分页（方块余额 + 每日签到）
    QWidget *m_pointsPage = nullptr;
    QLabel *m_pointsBalance = nullptr;
    QLabel *m_pointsStreak = nullptr;
    QLabel *m_pointsReward = nullptr;
    QPushButton *m_pointsCheckinButton = nullptr;
    QLabel *m_pointsGift = nullptr;
    QLabel *m_makeupCount = nullptr;
    QLabel *m_makeupDesc = nullptr;
    QPushButton *m_buyMakeupButton = nullptr;
    QPushButton *m_useMakeupButton = nullptr;
    QList<QFrame *> m_pointsDayCards;
    QList<QLabel *> m_pointsDayRewards;
    QList<QLabel *> m_pointsDayStates;
    QVBoxLayout *m_pointsHistoryLayout = nullptr;
    QJsonObject m_pointsState;
    bool m_pointsLoaded = false;
    bool m_pointsBusy = false;
    // 勋章页（和方块盒子账号绑定的勋章墙）
    QWidget *m_medalsPage = nullptr;
    QLabel *m_medalsCount = nullptr;
    QLabel *m_medalsTotal = nullptr;
    QLabel *m_medalsSummary = nullptr;
    QLabel *m_medalsNotice = nullptr;
    QLabel *m_medalsShowcase = nullptr;
    QProgressBar *m_medalsWallBar = nullptr;
    QWidget *m_medalsHost = nullptr;
    FlowLayout *m_medalFlow = nullptr;
    /** 「游玩」页签里的统计页（嵌入模式，滚动交给账户页） */
    PlaytimeStatsPage *m_playtimePage = nullptr;
    QJsonArray m_medals;
    int m_medalEarned = 0;
    int m_medalTotal = 0;
    bool m_medalsLoaded = false;
    bool m_medalsBusy = false;
    /** 勋章页「生成分享图」按钮（勋章读回来之后才允许生成） */
    QPushButton *m_medalsShareButton = nullptr;
    /** 点分享图时勋章墙还没读回来：读完自动弹一次 */
    bool m_medalSharePending = false;
    /** 论坛头像原图（下载后留一份，分享图里按原图尺寸裁圆，不用 76px 的小图放大） */
    QPixmap m_avatarRaw;
    // 勋章页 · 游戏成就（和论坛勋章放在同一个卡片瀑布流里）
    QJsonArray m_gameAchievements;
    int m_gameAchievementTotal = 0;
    // 积分页 · 会员（支持者 / 体验会员 / 标准会员）
    QWidget *m_pointsMember = nullptr;
    QHBoxLayout *m_pointsMemberGrid = nullptr;
    QLabel *m_pointsMemberHint = nullptr;
    // 会员页
    QWidget *m_memberPage = nullptr;
    QWidget *m_memberPlansHost = nullptr;
    QHBoxLayout *m_memberPlansLayout = nullptr;
    QLabel *m_memberStatus = nullptr;
    QList<QPushButton *> m_memberCycleButtons;
    QString m_memberCycle = QStringLiteral("month");
    QJsonObject m_memberPlans;      ///< /api/member/plans 的结果
    bool m_memberLoaded = false;
    // 余额页
    QWidget *m_walletPage = nullptr;
    QLabel *m_walletBalance = nullptr;
    QLabel *m_walletHint = nullptr;
    QHBoxLayout *m_walletTopupLayout = nullptr;
    QVBoxLayout *m_walletRecentLayout = nullptr;
    QJsonObject m_walletState;
    bool m_walletLoaded = false;
    // 账单页
    QWidget *m_billsPage = nullptr;
    QLabel *m_billsBalance = nullptr;
    QLabel *m_billsRecharge = nullptr;
    QLabel *m_billsPurchase = nullptr;
    QLabel *m_billsPool = nullptr;
    QLabel *m_billsPoolText = nullptr;
    QVBoxLayout *m_billsListLayout = nullptr;
    QJsonObject m_billsState;
    bool m_billsLoaded = false;
    // 积分页 · 购买方块
    QWidget *m_pointsShop = nullptr;
    QHBoxLayout *m_pointsShopGrid = nullptr;
    QLabel *m_pointsShopNote = nullptr;
    QWidget *m_pointsShopPayPanel = nullptr;
    QLabel *m_pointsShopPayTitle = nullptr;
    QLabel *m_pointsShopPayText = nullptr;
    QPushButton *m_pointsShopPayOpen = nullptr;
    QPushButton *m_pointsShopPayCheck = nullptr;
    QTimer *m_payPollTimer = nullptr;
    QHash<QString, QPushButton *> m_pointsShopButtons;   ///< 档位 id → 购买按钮
    QString m_payOrderNo;          ///< 正在等待到账的订单号（空 = 没有在等）
    QString m_payOrderTitle;       ///< 订单金额 / 档位文案，支付面板上用
    QString m_payUrl;              ///< 最近一次打开的支付页地址
    int m_payOrderPoints = 0;
    int m_payPollTries = 0;
    bool m_pointsShopLoaded = false;
    // 角色页（游戏账户瀑布流）
    QWidget *m_charactersPage = nullptr;
    MasonryLayout *m_charactersLayout = nullptr;
    QHash<QString, QLabel *> m_characterCovers;   ///< 账户名 → 封面控件（异步换皮肤时只重画这张）
    QHash<QString, int> m_characterCoverHeights;  ///< 账户名 → 封面高度
    QHash<QString, QImage> m_skinImages;          ///< 账户名 → 下载到的皮肤纹理
    QSet<QString> m_pendingSkinRequests;          ///< 正在下载皮肤的账户名

    // 账户详情页
    QWidget *m_detailPage = nullptr;
    Skin3DWidget *m_detailSkinWidget = nullptr;
    QLabel *m_detailName = nullptr;
    QLabel *m_detailTypeDot = nullptr;
    QLabel *m_detailTypeLabel = nullptr;
    QLabel *m_detailDefaultChip = nullptr;
    QLabel *m_detailMeta = nullptr;
    QList<QPushButton *> m_detailPoseButtons;
    SkinPose m_detailPose = SkinPose::Idle;
    QPushButton *m_detailUseBtn = nullptr;
    QPushButton *m_detailDeleteBtn = nullptr;
    QComboBox *m_detailActionCombo = nullptr;
    QPushButton *m_detailExecuteBtn = nullptr;
    QWidget *m_detailOutfitSection = nullptr;   ///< 「皮肤与披风」整块（离线账户 / 皮肤站角色显示）
    QLabel *m_detailSkinState = nullptr;        ///< 皮肤状态：默认皮肤 / 自定义 / 已穿
    QLabel *m_detailCapeState = nullptr;        ///< 披风状态：未设置 / 自定义 / 已穿
    QPushButton *m_detailSkinClearBtn = nullptr;
    QPushButton *m_detailCapeClearBtn = nullptr;
    QPushButton *m_detailSkinPickBtn = nullptr; ///< 皮肤站角色换装用的「选择图片」
    QPushButton *m_detailCapePickBtn = nullptr;
    QString m_detailAccountName;
    QString m_detailAccountType;
    QString m_detailAccountUuid;
    QString m_detailAccountSkinUrl;
    QString m_detailAccountServerUrl;
    bool m_detailIsDefault = false;
    // ── 皮肤站详情（/api/me 拉回来的当前档案）──
    bool m_detailStationLoaded = false;         ///< 档案拉回来过
    bool m_detailStationMatch = false;          ///< 详情角色就是当前论坛账号的皮肤站角色
    QString m_detailStationSkinUrl;             ///< 当前穿的皮肤纹理（站内相对地址）
    QString m_detailStationCapeUrl;             ///< 当前穿的披风纹理
    QSet<QString> m_pendingStationCape;         ///< 正在下载的披风地址，防重复
    QJsonArray m_threads;
    QJsonArray m_replies;
    QJsonArray m_bookmarks;
    QVector<FavoriteItem> m_contentFavorites;   // 资源收藏（云端同步的在线资源）
    QVector<ForumNotificationItem> m_notifications;
    QVector<FeedbackItem> m_myFeedbacks;
    bool m_threadsLoaded = false;
    bool m_repliesLoaded = false;
    bool m_bookmarksLoaded = false;
    bool m_contentFavoritesLoaded = false;
    bool m_notificationsLoaded = false;
    bool m_myFeedbacksLoaded = false;
    int m_threadTotal = 0;
    int m_replyTotal = 0;
    int m_bookmarkTotal = 0;
    int m_contentFavoriteTotal = 0;
    int m_bookmarkSubTab = BookmarkResourceTab;
    QHash<QString, QPixmap> m_contentIconCache;   // 资源图标 URL → 缩略图缓存
    QString m_loadedAvatarPath;

    // 账号设置
    QLineEdit *m_displayNameEdit = nullptr;
    QLineEdit *m_emailEdit = nullptr;
    QLabel *m_emailVerifiedChip = nullptr;   ///< 邮箱旁的「已验证 / 未验证」徽标
    QLineEdit *m_emailCodeEdit = nullptr;
    QPushButton *m_sendCodeButton = nullptr;
    QPushButton *m_verifyCodeButton = nullptr;
    QLabel *m_emailNote = nullptr;
    QTimer *m_codeTimer = nullptr;
    int m_codeCooldown = 0;                  ///< 重发验证码剩余秒数
    bool m_emailBusy = false;                ///< 发码 / 验证请求进行中
    QPlainTextEdit *m_bioEdit = nullptr;
    /** 简介编辑 / 预览切换（预览按 Markdown 渲染，看到的就是别人看到的样子） */
    QWidget *m_bioHost = nullptr;
    ForumPostBrowser *m_bioPreview = nullptr;
    QPushButton *m_bioPreviewButton = nullptr;
    QLabel *m_settingsAvatar = nullptr;
    /** 设置页里的小封面预览 */
    ForumKit::CoverBanner *m_settingsCover = nullptr;
    QLabel *m_settingsAccountLine = nullptr;
    QLineEdit *m_currentPasswordEdit = nullptr;
    QLineEdit *m_newPasswordEdit = nullptr;
    QLineEdit *m_confirmPasswordEdit = nullptr;
    QLabel *m_passwordSectionTitle = nullptr;
    QWidget *m_currentPasswordField = nullptr;
    QPushButton *m_savePasswordBtn = nullptr;

    // 登录卡 · 第三方登录
    QWidget *m_oauthSection = nullptr;
    QPushButton *m_oauthGithubBtn = nullptr;
    QPushButton *m_oauthLittleSkinBtn = nullptr;
    QWidget *m_oauthWaiting = nullptr;
    QLabel *m_oauthStatus = nullptr;
    QPushButton *m_oauthReopenBtn = nullptr;
    QPushButton *m_oauthCancelBtn = nullptr;
    QString m_oauthProviderName;
    QString m_oauthVerificationUrl;
    bool m_oauthHasProviders = false;
    bool m_oauthGithubEnabled = false;
    bool m_oauthLittleSkinEnabled = false;
    bool m_oauthWaitingActive = false;

    // 登录卡 · 手机扫码登录（只在登录模式出现；扫码弹窗是独立弹窗，
    // 与第三方登录的等待态互不干扰）
    QWidget *m_qrLoginSection = nullptr;
    QPushButton *m_qrLoginButton = nullptr;
    QLabel *m_qrLoginHint = nullptr;

    // 登录卡
    QLabel *m_authTitle = nullptr;
    QLabel *m_authSub = nullptr;
    QList<QPushButton *> m_authTabButtons;
    QLabel *m_authError = nullptr;
    QWidget *m_confirmField = nullptr;
    QWidget *m_authNameField = nullptr;
    QLineEdit *m_authNameEdit = nullptr;
    QLineEdit *m_authPasswordEdit = nullptr;
    QLabel *m_authPasswordLabel = nullptr;
    QLineEdit *m_authConfirmEdit = nullptr;
    QLabel *m_recoverNote = nullptr;
    // 注册卡的密码强度显示（四段小条 + 等级文字 + 一句建议），登录时不显示
    QWidget *m_pwStrengthField = nullptr;
    QList<QFrame *> m_pwStrengthSegs;
    QLabel *m_pwStrengthLabel = nullptr;
    QLabel *m_pwStrengthNote = nullptr;
    // 注册卡的邮箱 + 验证码（论坛要求注册就验证邮箱时才显示）
    QWidget *m_regEmailField = nullptr;
    QLineEdit *m_regEmailEdit = nullptr;
    QWidget *m_regCodeField = nullptr;
    QLineEdit *m_regCodeEdit = nullptr;
    QPushButton *m_regSendCodeBtn = nullptr;
    QLabel *m_regCodeNote = nullptr;
    QTimer *m_regCodeTimer = nullptr;
    int m_regCodeCooldown = 0;              ///< 注册卡上「重新发送」的剩余秒数
    bool m_regEmailBusy = false;            ///< 注册卡发码请求进行中
    bool m_registerEmailRequired = true;    ///< 论坛是否要求注册时验证邮箱
    QPushButton *m_authSubmitBtn = nullptr;
    QLabel *m_authLegalNote = nullptr;

    // 未登录内容页
    QLabel *m_loggedOutHint = nullptr;
    QPushButton *m_loggedOutLoginBtn = nullptr;
    QPushButton *m_loggedOutRegisterBtn = nullptr;

    TabIndex m_activeTab = CharactersTab;
    AuthMode m_authMode = LoginMode;
    bool m_firstShow = true;
    bool m_busy = false;
};

