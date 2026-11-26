/**
 * @file   SkinStationPage.h
 * @brief  方块皮肤站 · 启动器内整页（完整版）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 账户管理页「皮肤站」入口不再弹独立窗口，而是直接在软件里整页打开：
 *   - 左侧 3D 预览当前皮肤 / 披风，可切视角、保存预览图、上传和改资料；
 *   - 右侧在「我的皮肤 / 披风 / 官方预设 / 收藏 / 最近使用 / 皮肤库」之间切换，
 *     皮肤库支持皮肤 / 披风分类、搜索、排序、模型筛选、翻页和「换一批」；
 *   - 选中作品后可以穿上、收藏、点赞、公开 / 私密、改名、删除、下载 PNG、
 *     举报、复制链接、看评论和同作者作品，也可以跳到网页版详情页；
 *   - 点作者能打开玩家主页，看对方公开的皮肤 / 披风并直接穿上；
 *   - 管理员 / 版主可以在软件里处理皮肤举报（忽略 / 下架 / 删除）。
 *
 * 登录态直接复用 ForumAccountService 的论坛会话令牌（Authorization: Bearer），
 * 论坛登录后进来就是登录状态，不需要再输一次密码。
 */
#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class ForumAccountService;
class QCheckBox;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QTabBar;
class QTimer;
class Skin3DWidget;

class SkinStationPage : public QWidget
{
  Q_OBJECT

public:
  explicit SkinStationPage(QWidget *parent = nullptr);
  ~SkinStationPage() override;

  /** 皮肤站根地址：默认线上域名，可用环境变量 BLOCKBOX_SKIN_SITE_URL 覆盖（本地联调用） */
  static QString skinSiteBaseUrl();

  /** 进入本页：按当前登录态重新拉一次档案 / 收藏 / 最近使用（页面常驻，每次进来都要刷新） */
  void openPage();

  /** 顶栏「刷新」：等同重新进入本页 */
  Q_INVOKABLE void onRefreshClicked();

protected:
  /** 把 PNG 拖进页面就能上传：单张走上传弹窗，多张走批量上传 */
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  struct SkinItem
  {
    int id = -1;
    QString kind;    ///< skin / cape
    QString name;
    QString model;   ///< classic / slim
    QString url;     ///< 站内相对地址，例如 /textures/xxxx.png
    QString ownerName;
    int ownerId = 0;
    QString createdAt;
    int likes = 0;
    int downloads = 0;
    int comments = 0;
    int favoriteCount = 0;
    bool isPublic = false;
    bool favorited = false;
    bool liked = false;
    bool mine = false;
    /** 官方预设专用：预设 key（非空表示这一项来自 /api/me 的 presets，id 为负数） */
    QString presetKey;
    QString presetDesc;
  };

  struct CommentItem
  {
    int id = 0;
    int userId = 0;
    QString author;
    QString content;
    QString createdAt;
    bool canDelete = false;
  };

  enum Tab
  {
    TabMySkins = 0,
    TabCapes = 1,
    TabPresets = 2,
    TabFavorites = 3,
    TabHistory = 4,
    TabLibrary = 5
  };

  void buildUi();
  void loadMe();
  /** 站点统计（全站玩家 / 皮肤 / 下载量），只拉一次，失败不影响衣柜 */
  void loadSiteStats();
  /** 把皮肤站当前角色同步成账户管理页的游戏账户（没有就自动加，改名就跟着更新） */
  void syncGameAccount();
  void loadFavorites();
  void loadHistory();
  void loadLibrary(int page = 1, const QString &sortOverride = QString());
  /** 选中作品后补一次详情（收藏数 / 评论数 / 点赞状态 / 同作者作品） */
  void loadDetail(int id);
  void reloadCurrentTab();
  void fillList();
  void updateDetail();
  void previewCurrentProfile();
  void previewItem(const SkinItem &item);
  void selectFirst();

  void applySelected();
  /** 把某件作品设为当前皮肤 / 披风（预设走 /api/presets/:key/use） */
  void applyItem(const SkinItem &item, const std::function<void(bool)> &done = {});
  void toggleFavorite();
  void toggleLike();
  void togglePublic();
  void downloadSelected();
  void reportSelected();
  void shareSelected();
  void openWebDetail();
  void savePreview();
  void openUploadDialog(const QString &initialPath = QString());
  void openProfileDialog();
  /** 编辑当前作品：改名 / 公开状态 / 删除 */
  void openEditDialog();
  void deleteCurrent();
  void openCommentsDialog();
  /** 玩家公开主页：公开的皮肤和披风都能预览、穿上 */
  void openPlayerDialog(const QString &playerName);
  /** 同作者作品列表 */
  void openRelatedDialog();
  /** 管理员 / 版主：处理皮肤举报 */
  void openAdminDialog();
  /** 批量上传选中 / 拖进来的 PNG（最多 20 张） */
  void batchUpload();
  void uploadBatchFiles(const QStringList &paths);

  QVector<SkinItem> &currentVector();
  SkinItem currentItem() const;
  void showStatus(const QString &text, bool error = false);

  void getJson(const QString &path, const std::function<void(const QJsonObject &)> &onOk);
  void postJson(const QString &path, const QJsonObject &body, const std::function<void(const QJsonObject &)> &onOk);
  void sendJson(const QString &path, const QByteArray &method, const QJsonObject &body,
                const std::function<void(const QJsonObject &)> &onOk);
  void postPng(const QString &path, const QByteArray &png, const std::function<void(const QJsonObject &)> &onOk);
  void fetchImage(const QString &url, const std::function<void(const QImage &)> &onImage);
  QNetworkRequest requestFor(const QString &path) const;
  void handleReply(QNetworkReply *reply, const std::function<void(const QJsonObject &)> &onOk);

  /** 把详情接口拿到的状态写回所有列表里的同 id 作品 */
  void mergeItemState(const SkinItem &updated);
  /** 当前登录账号是不是管理员 / 版主 */
  bool isModerator() const;

  static SkinItem itemFromJson(const QJsonObject &object, bool mine);

  /** 批量上传的进度任务：跨多次网络回调保留，上传结束清空 */
  std::function<void(int)> m_batchStep;
  ForumAccountService *m_account = nullptr;
  QNetworkAccessManager *m_net = nullptr;
  QHash<QString, QImage> m_imageCache;
  bool m_dark = false;

  QVector<SkinItem> m_mySkins;
  QVector<SkinItem> m_capes;
  QVector<SkinItem> m_presets;
  QVector<SkinItem> m_favorites;
  QVector<SkinItem> m_history;
  QVector<SkinItem> m_library;
  QVector<SkinItem> m_related;

  int m_tab = TabMySkins;
  int m_libraryPage = 1;
  int m_libraryPages = 1;
  QString m_librarySort = QStringLiteral("hot");
  QString m_libraryQuery;
  QString m_libraryModel;
  QString m_libraryKind = QStringLiteral("skin");
  bool m_libraryRandom = false;
  int m_selectedId = -1;

  QString m_playerName;
  QString m_playerUuid;
  QString m_model;
  QString m_about;
  int m_userId = 0;
  QString m_role;
  QString m_currentSkinUrl;
  QString m_currentCapeUrl;

  /** 当前选中作品的详情（/api/textures/:id 返回，选中变化时刷新） */
  int m_detailId = -1;
  int m_detailComments = 0;
  int m_detailFavoriteCount = 0;
  bool m_detailLiked = false;

  QLabel *m_playerLabel = nullptr;
  /** 主名字下面那行「玩家名 xxx」：论坛账户名和玩家名不一样时才显示 */
  QLabel *m_playerNameLabel = nullptr;
  QLabel *m_uuidLabel = nullptr;
  QLabel *m_countLabel = nullptr;
  QLabel *m_siteStatsLabel = nullptr;
  QLabel *m_modelChip = nullptr;
  QLabel *m_statusLabel = nullptr;
  QLabel *m_previewHint = nullptr;
  QLabel *m_detailTitle = nullptr;
  QLabel *m_detailMeta = nullptr;
  QLabel *m_pageLabel = nullptr;
  QTabBar *m_tabs = nullptr;
  QListWidget *m_list = nullptr;
  QWidget *m_libraryBar = nullptr;
  QWidget *m_pager = nullptr;
  QLineEdit *m_search = nullptr;
  QComboBox *m_sort = nullptr;
  QComboBox *m_kindFilter = nullptr;
  QComboBox *m_modelFilter = nullptr;
  QPushButton *m_prevPage = nullptr;
  QPushButton *m_nextPage = nullptr;
  QPushButton *m_shuffleBtn = nullptr;
  QPushButton *m_applyBtn = nullptr;
  QPushButton *m_favoriteBtn = nullptr;
  QPushButton *m_likeBtn = nullptr;
  QPushButton *m_publicBtn = nullptr;
  QPushButton *m_renameBtn = nullptr;
  QPushButton *m_deleteBtn = nullptr;
  QPushButton *m_downloadBtn = nullptr;
  QPushButton *m_shareBtn = nullptr;
  QPushButton *m_commentsBtn = nullptr;
  QPushButton *m_reportBtn = nullptr;
  QPushButton *m_ownerBtn = nullptr;
  QPushButton *m_relatedBtn = nullptr;
  QPushButton *m_detailBtn = nullptr;
  QPushButton *m_batchBtn = nullptr;
  QPushButton *m_myProfileBtn = nullptr;
  QPushButton *m_adminBtn = nullptr;
  QPushButton *m_rotateBtn = nullptr;
  QTimer *m_searchTimer = nullptr;
  Skin3DWidget *m_preview = nullptr;
};
