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
 *   登录卡：与论坛登录页同款的「登录 / 注册新账号」表单
 *
 * 这是账户管理页的唯一界面：添加游戏账户走角色页右上角的「添加角色」弹窗，
 * 皮肤在「制作皮肤」里编辑，桌面端不再保留旧的经典账户管理页。
 */
#pragma once

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QWidget>

#include "utils/ForumContentService.h"
#include "utils/FeedbackService.h"

#include "components/Skin3DWidget.h"

class ForumAccountService;
class ForumPostBrowser;
class MasonryLayout;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

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

    /** 未登录想发帖 / 回复时跳进来：直接展示登录表单 */
    void openLoginForm();

    /** 打开某个游戏账户的详情页（角色卡片双击 / 外部跳转） */
    void openAccountDetail(const QString &accountName);

signals:
    void backToMainRequested();
    void accountManagePageOpened();

    /** 角色页「添加角色」/ 空态按钮：弹出添加账户弹窗 */
    void addGameAccountRequested();

    /** 点击自己的主题 / 回复 / 收藏 / 通知：在启动器内打开帖子详情 */
    void threadOpenRequested(int threadId);

    /** 点击「我的收藏 → 资源收藏」里的一条资源：打开资源详情页 */
    void contentFavoriteOpenRequested(const ModInfo &info, ContentType type);

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
    /** 详情页 3D 预览随尺寸自动调整相机距离，保证整只模型完整可见 */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum ViewIndex { OverviewView = 0, AuthView = 1, DetailView = 2 };
    enum TabIndex { CharactersTab = 0, ThreadsTab = 1, RepliesTab = 2, BookmarksTab = 3,
                    NotificationsTab = 4, FeedbackTab = 5, SettingsTab = 6 };
    enum ContentIndex { CharactersContent = 0, ThreadsContent = 1, RepliesContent = 2,
                        BookmarksContent = 3, NotificationsContent = 4, FeedbackContent = 5,
                        SettingsContent = 6, LoggedOutContent = 7 };
    enum AuthMode { LoginMode = 0, RegisterMode = 1 };
    /** 「我的收藏」页签内的子页：资源收藏（在线资源同步） / 帖子收藏（论坛主题） */
    enum BookmarkSubTab { BookmarkResourceTab = 0, BookmarkThreadTab = 1 };

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
    QLabel *m_statThreads = nullptr;
    QLabel *m_statPosts = nullptr;
    QLabel *m_statBookmarks = nullptr;
    QLabel *m_statJoined = nullptr;
    QLabel *m_statGameAccounts = nullptr;
    QWidget *m_profileAuthActions = nullptr;   // 未登录：登录 / 注册
    QWidget *m_profileUserActions = nullptr;   // 已登录：编辑资料 / 退出登录

    // 内容卡
    QList<QPushButton *> m_tabButtons;
    /** 「我的收藏」内层切换按钮（资源收藏 / 帖子收藏） */
    QList<QPushButton *> m_bookmarkSubButtons;
    QLabel *m_tabHint = nullptr;
    QVBoxLayout *m_threadListLayout = nullptr;
    QVBoxLayout *m_replyListLayout = nullptr;
    QVBoxLayout *m_bookmarkListLayout = nullptr;
    QVBoxLayout *m_notificationListLayout = nullptr;
    QPushButton *m_markAllReadButton = nullptr;
    QVBoxLayout *m_feedbackListLayout = nullptr;
    QPushButton *m_feedbackRefreshButton = nullptr;
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
    QString m_detailAccountName;
    QString m_detailAccountType;
    QString m_detailAccountUuid;
    QString m_detailAccountSkinUrl;
    bool m_detailIsDefault = false;
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

    // 登录卡
    QLabel *m_authTitle = nullptr;
    QLabel *m_authSub = nullptr;
    QList<QPushButton *> m_authTabButtons;
    QLabel *m_authError = nullptr;
    QWidget *m_confirmField = nullptr;
    QLineEdit *m_authNameEdit = nullptr;
    QLineEdit *m_authPasswordEdit = nullptr;
    QLineEdit *m_authConfirmEdit = nullptr;
    QPushButton *m_authSubmitBtn = nullptr;

    // 未登录内容页
    QLabel *m_loggedOutHint = nullptr;
    QPushButton *m_loggedOutLoginBtn = nullptr;
    QPushButton *m_loggedOutRegisterBtn = nullptr;

    TabIndex m_activeTab = CharactersTab;
    AuthMode m_authMode = LoginMode;
    bool m_firstShow = true;
    bool m_busy = false;
};

