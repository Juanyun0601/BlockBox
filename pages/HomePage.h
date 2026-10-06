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
class Skin3DWidget;
class CarouselView;
class HomeSkinPanel;

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
  /** 请求打开任务列表页（下载任务） */
  void taskListRequested();

private:
  void initUI();
  void loadPictures();
  void loadRecentPlays();
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
  // 轮播右侧皮肤面板
  HomeSkinPanel *m_skinPanel = nullptr;
  Skin3DWidget *m_skin3D = nullptr;
  QPushButton *m_skinAddBtn = nullptr;

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
  int m_recentPlaysBuiltWidth = -1;          // 上次重建使用的卡片宽度
  int m_recentPlaysBuiltCount = -1;          // 上次重建的卡片数量

  // Section 4: Websites
  QWidget* createWebsitesSection();
  QWidget* createWebsiteCard(const QString &name, const QString &url);

  // Section 5: Performance
  QWidget* createPerformanceSection();
  QVector<PerfMonitorCard*> m_perfCards;
  HardwareMonitor *m_hardwareMonitor;
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
  QWidget* createNewsSection();      // 公告（中）
  QWidget* createDownloadsSection(); // 下载任务（中）
  QWidget* createSystemSection();    // 系统信息（中）
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
  QWidget *m_carouselSlot = nullptr;           // 默认模式轮播容器（全宽无内边距）
  QVBoxLayout *m_carouselSlotLayout = nullptr;
  QWidget *m_contentArea = nullptr;            // 默认模式内容容器（带 24px 边距）
  QVBoxLayout *m_contentLayout = nullptr;
  QMap<QString, QWidget*> m_sectionWidgets;    // 卡片类型 → 已构建的区块控件（缓存复用）

  // 辅助方法：格式化相对时间
  QString formatRelativeTime(const QDateTime &time) const;
};
