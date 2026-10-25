/**
 * @file   HomePage.h
 * @brief  主页页面类声明
 * @author BlockBox Team
 * @date   2026-06-14
 */

#pragma once

#include <QDateTime>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

class PerfMonitorCard;
class PerformanceDetailDialog;
class HardwareMonitor;
class RingGauge;
class AnalogClock;
class QVBoxLayout;
class FlowLayout;
class QLabel;
class QShowEvent;
class QStackedWidget;
class Skin3DWidget;
class CarouselView;
class HomeSkinPanel;
class HomeAnnouncementBar;
struct InstanceFolderInfo;

struct RecentPlayEntry
{
  QString name;           // 存档名称（LevelName）或服务器名称
  QString instanceName;   // 所属实例名（版本目录名）
  QString type;           // "服务器" or "存档"
  QString lastPlayed;     // 时间字符串（精确到分）
  QString version;        // 游戏版本
  QString loader;         // 加载器
  QString savePath;       // 存档目录完整路径
  QString instancePath;   // 所属实例（版本）路径
  QString iconPath;       // 存档图标路径（icon.png）
  QDateTime lastPlayedTime;  // 最后游玩时间（用于排序）
  bool pinned = false;    // 是否固定在最近游玩区域
  // 服务器专属字段
  QString serverAddress;  // 服务器地址
  quint16 serverPort = 25565;  // 服务器端口
};

class HomePage : public QWidget
{
  Q_OBJECT

public:
  explicit HomePage(QWidget *parent = nullptr);
  ~HomePage();

  // 刷新最近游玩区域（游戏启动后调用）
  void refreshRecentPlays();

  // 切换游戏版本模式（Java版 / 基岩版），重建「资源下载」快捷入口卡片
  void setBedrockMode(bool bedrock);

  /** 同步当前实例名（MainWindow 在切换/恢复实例时推送），经典模式面板显示用 */
  void setCurrentInstanceName(const QString &name);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;

signals:
  // Quick link signals (Section 2)
  void installNewInstanceClicked();
  void downloadModpackClicked();
  void importModpackClicked();
  void modsClicked();
  void datapacksClicked();
  void resourcepacksClicked();
  void shadersClicked();
  void worldsClicked();
  // 基岩版快捷入口信号（仅基岩版模式使用）
  void installVersionClicked();      // 安装新实例
  void bedrockResourcepacksClicked(); // 资源包（基岩）
  void texturesClicked();            // 材质包
  void bedrockWorldsClicked();       // 地图
  void scriptsClicked();             // 脚本
  // Recent play signals (Section 3)
  void recentPlayLaunchClicked(const QString &name);
  void recentPlaySettingsClicked(const QString &name);
  void recentPlayInstanceSettingsClicked(const QString &name);
  void recentPlayOpenFolderClicked(const QString &savePath);
  void recentPlayCopyNameClicked(const QString &name);
  /** 请求快捷启动并直接进入指定存档 */
  void recentPlayQuickLaunchClicked(const QString &saveName, const QString &instancePath);
  /** 请求快捷启动并自动连接到指定服务器 */
  void recentPlayQuickLaunchServerClicked(const QString &address, quint16 port, const QString &instancePath);
  /** 请求打开服务器管理（实例管理页服务器标签页） */
  void recentPlayServerSettingsClicked(const QString &instancePath);
  // 我的实例卡片
  /** 请求启动指定实例（instancePath 为版本目录完整路径） */
  void instanceLaunchRequested(const QString &instancePath);
  /** 请求打开指定实例的管理页 */
  void instanceManageRequested(const QString &instancePath);
  // 小卡片快捷跳转
  /** 请求打开账号管理页 */
  void accountManageRequested();
  /** 请求打开 AI 助手页 */
  void aiChatRequested();
  /** 请求打开搜索页 */
  void searchRequested();
  /** 请求打开内置「官方新闻」列表页 */
  void newsPageRequested();
  /** 请求打开任务列表页（下载任务） */
  void taskListRequested();
  // 经典模式皮肤面板快捷操作
  /** 请求启动当前实例（无实例时主窗口会引导去实例选择页） */
  void launchRequested();
  /** 请求打开实例选择页 */
  void instanceSelectRequested();
  /** 请求打开当前实例的设置 */
  void instanceSettingsRequested();
  /** 请求打开实例助手窗口 */
  void instanceAssistantRequested();

private:
  void initUI();
  void loadPictures();
  // 触发后台扫描最近游玩（完成后自动重建卡片）
  void loadRecentPlays();
  // 后台线程：遍历实例目录并解析存档信息，不触碰界面
  static QVector<RecentPlayEntry> scanRecentPlays(const QList<InstanceFolderInfo> &folders,
                                                   const QStringList &pinnedKeys);
  void loadInstances();

  // 首页布局（默认）
  void rebuildContent();
  QWidget* sectionWidget(const QString &type);

  // Section 1: Carousel（轮播 + 右侧当前皮肤预览面板）
  QWidget* createCarouselSection();
  QWidget* createSkinPanel();
  /** 依据账户列表切换皮肤模型 / 「添加首个账户」空态，并同步加载当前皮肤 */
  void refreshSkinPanel();
  CarouselView *m_carouselView;     // 自绘轮播视图（圆角+描边，双层滑动切换）
  QLabel *m_carouselPlaceholder;
  QVector<QPushButton*> m_carouselDots;
  QTimer *m_carouselTimer;
  QStringList m_carouselPaths;
  int m_currentCarouselIndex;
  void updateCarousel();
  void setCarouselIndex(int index);
  void updateCarouselHeight();
  // 公告栏（封面图 + 皮肤模型下方，内容来自官网 announcement.json）
  HomeAnnouncementBar *m_announcementBar = nullptr;
  // 轮播右侧皮肤面板
  HomeSkinPanel *m_skinPanel = nullptr;
  Skin3DWidget *m_skin3D = nullptr;
  QPushButton *m_skinAddBtn = nullptr;
  // 皮肤面板「默认 / 经典」模式切换（选择持久化，重启后生效）
  QStackedWidget *m_skinPageStack = nullptr;  // 0=默认（3D 预览/空态） 1=经典
  QPushButton *m_modeDefaultBtn = nullptr;
  QPushButton *m_modeClassicBtn = nullptr;
  QString m_skinPanelMode = QStringLiteral("default");   // "default" / "classic"
  void loadSkinPanelMode();                              // 启动时读取持久化的选择
  void setSkinPanelMode(const QString &mode);            // 切换并持久化
  void applySkinPanelMode();                             // 按当前模式切换面板内容
  // 经典模式面板内容：头像 / 账户名 / 启动游戏 / 实例图标行 / 当前实例名
  QWidget* createClassicSkinPage();
  void refreshClassicPage();
  void refreshClassicIcons();
  void refreshModeSwitchIcons();
  static QPixmap roundedPixmap(const QPixmap &src, int radius);
  QWidget *m_classicPage = nullptr;
  QLabel *m_classicAvatar = nullptr;
  QLabel *m_classicName = nullptr;
  QPushButton *m_classicLaunchBtn = nullptr;
  QPushButton *m_classicSelectBtn = nullptr;
  QPushButton *m_classicSettingsBtn = nullptr;
  QPushButton *m_classicAssistantBtn = nullptr;
  QLabel *m_classicInstanceName = nullptr;
  QString m_homeInstanceName;                 // 当前实例名（MainWindow 同步推送）

  // Section 2: Quick Links
  QWidget* createQuickLinksSection();
  QWidget* createQuickLinkCard(const QString &iconPath, const QString &label, const char *signal);
  void rebuildQuickLinksContent();
  QWidget *m_quickLinksContainer;   // 「资源下载」快捷入口容器，用于模式切换时重建
  FlowLayout *m_quickLinksLayout;
  bool m_bedrockMode = false;       // 当前是否为基岩版模式

  // Section 3: Recent Plays
  QWidget* createRecentPlaysSection();
  void rebuildRecentPlaysContent();
  void scheduleRecentPlaysRebuild();   // 延迟合并的重建（避免在布局激活过程中改布局）
  void reloadRecentPlaysIfChanged();   // 重新扫描数据，仅在内容变化时强制重建
  QWidget* buildRecentPlayCard(const RecentPlayEntry &entry, QWidget *parent, int cardWidth);
  int calculateRecentPlayCardWidth() const;
  QVector<RecentPlayEntry> m_recentPlays;
  QWidget *m_recentPlaysContainer;  // 最近游玩区域的容器，用于刷新时替换内容
  FlowLayout *m_recentPlaysLayout;  // 最近游玩区域的布局（横排卡片，一行四个）
  bool m_recentPlaysResizing = false;  // 防止 resize 时递归重建
  bool m_recentPlaysRebuildPending = false;  // 已有延迟重建排队（合并连续 resize）
  bool m_recentPlaysRefreshPending = false;  // 已有延迟数据刷新排队
  bool m_recentPlaysForceRebuild = false;    // 数据有变化，下次重建不可跳过
  bool m_recentPlaysScanPending = false;     // 后台扫描进行中（防止重复触发）
  int m_recentPlaysBuiltWidth = -1;          // 上次重建使用的卡片宽度
  int m_recentPlaysBuiltCount = -1;          // 上次重建的卡片数量
  // 最近游玩固定（置顶）功能
  static QString recentPlayKey(const RecentPlayEntry &entry); // 稳定的记录标识
  void toggleRecentPlayPin(const QString &key);              // 固定/取消固定
  void sortRecentPlays();                                    // 固定置顶后按时间降序
  QStringList m_pinnedRecentKeys;                            // 已固定的记录标识（持久化）

  // Section 4: Websites
  QWidget* createWebsitesSection();
  QWidget* createWebsiteCard(const QString &name, const QString &url);

  // Section 5: Performance
  QWidget* createPerformanceSection();
  QVector<PerfMonitorCard*> m_perfCards;
  HardwareMonitor *m_hardwareMonitor;
  bool m_perfMonitorStarted = false;  // 硬件采样是否已启动（延迟到首页显示后）
  void refreshPerformanceData();
  void showPerformanceDetail(const QString &cardName);
  PerformanceDetailDialog *m_perfDetailDialog;

  // Section 6: Instances（我的实例）
  QWidget* createInstancesSection();
  void rebuildInstancesContent();
  QVector<RecentPlayEntry> m_instances;
  QWidget *m_instancesContainer;
  FlowLayout *m_instancesLayout;

  // 更多卡片（大小不一）
  QWidget* createClockSection();     // 时钟（小）
  QWidget* createAccountSection();   // 账号（小）
  QWidget* createAiSection();        // AI 助手（小）
  QWidget* createSearchSection();    // 快捷搜索（小）
  QWidget* createNewsSection();      // 官方新闻（中）
  QWidget* createDownloadsSection(); // 下载任务（中）
  QWidget* createSystemSection();    // 系统信息（中）
  // 官方新闻：数据来自 NewsFeed，卡片按容器宽度重排
  void rebuildNewsContent();
  void updateNewsStatus();
  void scheduleNewsRebuild();
  int calculateNewsCardWidth() const;
  QWidget *m_newsContainer = nullptr;
  FlowLayout *m_newsLayout = nullptr;
  QLabel *m_newsStatusLabel = nullptr;
  QPushButton *m_newsRefreshBtn = nullptr;
  int m_newsBuiltWidth = -1;      // 上次构建使用的卡片宽度
  bool m_newsRebuildPending = false;  // 已有延迟重建排队（合并连续 resize）
  void rebuildDownloadsContent();
  QWidget *m_downloadsContainer = nullptr;
  FlowLayout *m_downloadsLayout = nullptr;
  QTimer *m_clockTimer = nullptr;
  QLabel *m_clockTimeLabel = nullptr;
  QLabel *m_clockDateLabel = nullptr;
  AnalogClock *m_clockAnalog = nullptr;
  HardwareMonitor *m_systemMonitor = nullptr;
  RingGauge *m_sysDiskRing = nullptr;
  RingGauge *m_sysMemRing = nullptr;
  QLabel *m_sysDiskLabel = nullptr;
  QLabel *m_sysMemLabel = nullptr;

  // 布局骨架
  QVBoxLayout *m_mainLayout = nullptr;         // 顶层布局
  QWidget *m_carouselSlot = nullptr;           // 默认模式轮播容器（24px 侧边距 + 24px 顶部间距）
  QVBoxLayout *m_carouselSlotLayout = nullptr;
  QWidget *m_contentArea = nullptr;            // 默认模式内容容器（带 24px 边距）
  QVBoxLayout *m_contentLayout = nullptr;
  QMap<QString, QWidget*> m_sectionWidgets;    // 卡片类型 → 已构建的区块控件（缓存复用）

  // 辅助方法：格式化相对时间
  QString formatRelativeTime(const QDateTime &time) const;
};
