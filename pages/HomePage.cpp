/**
 * @file   HomePage.cpp
 * @brief  主页页面实现
 * @author BlockBox Team
 * @date   2026-06-14
 */

#include "HomePage.h"
#include "components/MasonryContentCard.h"
#include "components/OutlinedLabel.h"
#include "components/HomeDataVisuals.h"
#include "components/NewsCard.h"
#include "utils/DownloadTaskManager.h"
#include "utils/DownloadUtils.h"
#include "utils/SystemInfo.h"
#include "utils/mod/ModData.h"

#include <algorithm>
#include <QAbstractAnimation>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QEasingCurve>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonArray>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QStorageInfo>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include "components/PerfMonitorCard.h"
#include "components/PerformanceDetailDialog.h"
#include "layouts/FlowLayout.h"
#include "utils/SettingsManager.h"
#include "utils/SystemTools.h"
#include "utils/CommandAssistant/LevelDatReader.h"
#include "utils/HardwareMonitor.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

// 首页区块类型标识
namespace HomeCardType {
inline const QString Carousel    = QStringLiteral("carousel");    // 轮播图
inline const QString QuickLinks  = QStringLiteral("quicklinks");  // 快捷入口
inline const QString Recent      = QStringLiteral("recent");      // 最近游玩
inline const QString Websites    = QStringLiteral("websites");    // 常用网站
inline const QString Instances   = QStringLiteral("instances");   // 我的实例
inline const QString Performance = QStringLiteral("performance"); // 性能监控
inline const QString Clock       = QStringLiteral("clock");       // 时钟
inline const QString Account     = QStringLiteral("account");     // 账号
inline const QString Ai          = QStringLiteral("ai");          // AI 助手
inline const QString Search      = QStringLiteral("search");      // 快捷搜索
inline const QString News        = QStringLiteral("news");        // 公告
inline const QString Downloads   = QStringLiteral("downloads");   // 下载任务
inline const QString System      = QStringLiteral("system");      // 系统信息
}

// ============================================================================
// 构造 / 析构
// ============================================================================

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
    , m_carouselStack(nullptr)
    , m_carouselImage(nullptr)
    , m_carouselOverlay(nullptr)
    , m_carouselAnimGroup(nullptr)
    , m_carouselPlaceholder(nullptr)
    , m_carouselTimer(nullptr)
    , m_currentCarouselIndex(0)
    , m_carouselAnimating(false)
    , m_recentPlaysContainer(nullptr)
    , m_recentPlaysLayout(nullptr)
    , m_hardwareMonitor(nullptr)
    , m_perfDetailDialog(nullptr)
{
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  setMinimumWidth(0);
  loadPictures();
  loadRecentPlays();
  loadInstances();
  initUI();
}

void HomePage::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  updateCarouselHeight();
}

void HomePage::updateCarouselHeight()
{
  // 轮播图可能位于默认模式（全宽）或自定义网格中，以其实际宽度计算高度
  QWidget *cw = m_sectionWidgets.value(HomeCardType::Carousel);
  int carouselWidth = (cw && cw->width() > 0) ? cw->width() : width();
  int carouselHeight = carouselWidth * 9 / 16;
  if (carouselHeight < 160)
  {
    carouselHeight = 160;
  }

  if (m_carouselStack)
  {
    m_carouselStack->setFixedHeight(carouselHeight);
    updateCarouselLayerGeometry();
  }
  if (m_carouselPlaceholder)
  {
    m_carouselPlaceholder->setFixedHeight(carouselHeight);
  }
}

void HomePage::updateCarouselLayerGeometry()
{
  if (!m_carouselStack)
  {
    return;
  }
  // 防止动画正在播放时被 resize 打断
  if (m_carouselAnimating)
  {
    return;
  }
  QRect r = m_carouselStack->rect();
  if (m_carouselImage)
  {
    m_carouselImage->setGeometry(r);
  }
  if (m_carouselOverlay)
  {
    m_carouselOverlay->setGeometry(r);
  }
}

HomePage::~HomePage()
{
  if (m_carouselTimer)
  {
    m_carouselTimer->stop();
  }
  if (m_hardwareMonitor)
  {
    m_hardwareMonitor->stop();
  }
  if (m_systemMonitor)
  {
    m_systemMonitor->stop();
  }
}

bool HomePage::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::Resize && watched == m_carouselStack)
  {
    // 轮播图层容器尺寸变化时同步图片/遮罩层几何，
    // 确保标签铺满整个 stack（修复首页打开时图片停留在旧宽度、右侧空白的问题）。
    updateCarouselLayerGeometry();
  }
  if (event->type() == QEvent::Resize && watched == m_recentPlaysContainer)
  {
    // 最近游玩卡片宽度自适应容器宽度
    rebuildRecentPlaysContent();
  }
  if (event->type() == QEvent::MouseButtonPress)
  {
    auto *mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() == Qt::LeftButton)
    {
      QVariant taskVar = watched->property("openTaskList");
      if (taskVar.isValid())
      {
        emit taskListRequested();
        return true;
      }
      QVariant instVar = watched->property("instancePath");
      if (instVar.isValid())
      {
        emit instanceManageRequested(instVar.toString());
        return true;
      }
      QVariant nameVar = watched->property("recentPlayName");
      if (nameVar.isValid())
      {
        emit recentPlayLaunchClicked(nameVar.toString());
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

// ============================================================================
// 数据加载
// ============================================================================

void HomePage::loadPictures()
{
  QDir dir(":/Images/Pictures/");
  QStringList filters;
  filters << "*.jpg" << "*.png";
  QStringList entries = dir.entryList(filters, QDir::Files, QDir::Name);
  m_carouselPaths.clear();
  for (const QString &entry : entries)
  {
    const QString path = dir.absoluteFilePath(entry);
    QPixmap probe(path);
    if (probe.isNull())
    {
      qWarning() << "[HomePage] 轮播图片无法加载，已跳过:" << path;
      continue;
    }
    m_carouselPaths.append(path);
  }
}

void HomePage::loadRecentPlays()
{
  m_recentPlays.clear();

  // 遍历所有实例文件夹（游戏根目录 .minecraft）
  QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
  for (const auto &folder : folders)
  {
    QDir versionsDir(folder.path + "/versions");
    if (!versionsDir.exists())
    {
      continue;
    }

    // 遍历每个版本目录，查找其下的 saves/ 子目录
    QDirIterator verIt(versionsDir.path(), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
    while (verIt.hasNext())
    {
      QString versionPath = verIt.next();
      QString versionName = QFileInfo(versionPath).fileName();

      // 读取版本 JSON 获取版本号和加载器信息
      QString detectedLoader = tr("未知");
      QString detectedVersion = versionName;
      QString jsonPath = versionPath + "/" + versionName + ".json";
      QFile versionJsonFile(jsonPath);
      if (versionJsonFile.open(QIODevice::ReadOnly))
      {
        QByteArray jsonData = versionJsonFile.readAll();
        versionJsonFile.close();

        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);
        if (parseError.error == QJsonParseError::NoError)
        {
          QJsonObject rootObj = jsonDoc.object();

          if (rootObj.contains("inheritsFrom"))
            detectedVersion = rootObj["inheritsFrom"].toString();
          else if (rootObj.contains("clientVersion"))
            detectedVersion = rootObj["clientVersion"].toString();
          else if (rootObj.contains("id"))
            detectedVersion = rootObj["id"].toString();

          // 检测加载器类型
          if (rootObj.contains("fabricLoader")) { detectedLoader = "Fabric"; }
          else if (rootObj.contains("forge")) { detectedLoader = "Forge"; }
          else if (rootObj.contains("quiltLoader")) { detectedLoader = "Quilt"; }
          else if (rootObj.contains("neoForge")) { detectedLoader = "NeoForge"; }
          else
          {
            for (auto keyIter = rootObj.constBegin(); keyIter != rootObj.constEnd(); ++keyIter)
            {
              QString lower = keyIter.key().toLower();
              if (lower.contains("neoforge")) { detectedLoader = "NeoForge"; break; }
              if (lower.contains("forge")) { detectedLoader = "Forge"; break; }
              if (lower.contains("fabric")) { detectedLoader = "Fabric"; break; }
              if (lower.contains("quilt")) { detectedLoader = "Quilt"; break; }
            }
          }
        }
      }

      // 在版本目录下查找 saves/ 子目录
      QDir savesDir(versionPath + "/saves");
      if (!savesDir.exists())
      {
        continue;
      }

      // 扫描 saves/ 下的每个存档目录
      QDirIterator saveIt(savesDir.path(), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
      while (saveIt.hasNext())
      {
        QString savePath = saveIt.next();
        QString saveDirName = QFileInfo(savePath).fileName();

        // 必须存在 level.dat 才认为是有效存档
        QString levelDatPath = savePath + "/level.dat";
        if (!QFile::exists(levelDatPath))
        {
          continue;
        }

        RecentPlayEntry entry;
        entry.savePath = savePath;
        entry.instancePath = versionPath;
        entry.instanceName = versionName;
        entry.type = tr("存档");
        entry.name = saveDirName;  // 默认使用目录名
        entry.version = detectedVersion;
        entry.loader = detectedLoader;

        // 存档图标路径（icon.png）
        entry.iconPath = savePath + "/icon.png";

        // 读取 level.dat 获取存档名、版本、最后游玩时间
        auto levelInfo = LevelDatReader::readLevelDat(levelDatPath);
        if (levelInfo)
        {
          if (!levelInfo->levelName.isEmpty())
          {
            entry.name = levelInfo->levelName;
          }
          if (!levelInfo->versionName.isEmpty())
          {
            entry.version = levelInfo->versionName;
          }
          if (levelInfo->lastPlayed > 0)
          {
            // LastPlayed 是毫秒时间戳
            entry.lastPlayedTime = QDateTime::fromMSecsSinceEpoch(levelInfo->lastPlayed);
          }
        }

        // 回退：使用 level.dat 文件的最后修改时间
        if (!entry.lastPlayedTime.isValid())
        {
          entry.lastPlayedTime = QFileInfo(levelDatPath).lastModified();
        }

        // 格式化时间字符串（精确到分）
        if (entry.lastPlayedTime.isValid())
        {
          entry.lastPlayed = entry.lastPlayedTime.toString("yyyy-MM-dd HH:mm");
        }
        else
        {
          entry.lastPlayed = tr("从未游玩");
        }

        m_recentPlays.append(entry);
      }

      // 加载该版本目录下的服务器列表（servers.json）
      QString serversJsonPath = versionPath + "/servers.json";
      if (QFile::exists(serversJsonPath))
      {
        QFile serversFile(serversJsonPath);
        if (serversFile.open(QIODevice::ReadOnly))
        {
          QJsonDocument serversDoc = QJsonDocument::fromJson(serversFile.readAll());
          serversFile.close();

          QJsonArray serversArr = serversDoc.array();
          QDateTime serversFileTime = QFileInfo(serversJsonPath).lastModified();
          for (const QJsonValue &sval : serversArr)
          {
            QJsonObject sobj = sval.toObject();
            RecentPlayEntry srvEntry;
            srvEntry.type = tr("服务器");
            srvEntry.name = sobj["name"].toString();
            if (srvEntry.name.isEmpty())
            {
              srvEntry.name = sobj["address"].toString();
            }
            srvEntry.serverAddress = sobj["address"].toString();
            srvEntry.serverPort = static_cast<quint16>(sobj["port"].toInt(25565));
            srvEntry.instancePath = versionPath;
            srvEntry.instanceName = versionName;
            srvEntry.version = detectedVersion;
            srvEntry.loader = detectedLoader;
            // 服务器无游玩记录时间，使用 servers.json 的最后修改时间作为近似值
            srvEntry.lastPlayedTime = serversFileTime;
            srvEntry.lastPlayed = serversFileTime.toString("yyyy-MM-dd HH:mm");
            m_recentPlays.append(srvEntry);
          }
        }
      }
    }
  }

  // 按最后游玩时间降序排序
  std::sort(m_recentPlays.begin(), m_recentPlays.end(),
            [](const RecentPlayEntry &a, const RecentPlayEntry &b)
            {
              return a.lastPlayedTime > b.lastPlayedTime;
            });
}

QString HomePage::formatRelativeTime(const QDateTime &time) const
{
  if (!time.isValid())
  {
    return tr("从未游玩");
  }
  // 精确到分钟
  return time.toString("yyyy-MM-dd HH:mm");
}

// ============================================================================
// 主界面布局
// ============================================================================

void HomePage::initUI()
{
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);

  // 默认模式轮播槽：全宽、无外边距
  m_carouselSlot = new QWidget();
  m_carouselSlotLayout = new QVBoxLayout(m_carouselSlot);
  m_carouselSlotLayout->setContentsMargins(0, 0, 0, 0);
  m_carouselSlotLayout->setSpacing(0);
  m_mainLayout->addWidget(m_carouselSlot);

  // 内容区域：带 24px 侧边距
  m_contentArea = new QWidget();
  m_contentLayout = new QVBoxLayout(m_contentArea);
  m_contentLayout->setContentsMargins(24, 16, 24, 24);
  m_contentLayout->setSpacing(20);
  m_mainLayout->addWidget(m_contentArea);

  rebuildContent();
}

// ============================================================================
// 首页布局（默认）
// ============================================================================

void HomePage::rebuildContent()
{
  if (!m_contentLayout)
    return;

  // 清空内容区（控件缓存复用，不删除）
  QLayoutItem *item;
  while ((item = m_contentLayout->takeAt(0)) != nullptr)
    delete item;

  // 清空轮播槽
  if (m_carouselSlotLayout)
  {
    while ((item = m_carouselSlotLayout->takeAt(0)) != nullptr)
      delete item;
  }

  // ── 默认布局：轮播 + 预设区块 ──
  QSet<QString> usedTypes;
  usedTypes << HomeCardType::Carousel << HomeCardType::QuickLinks
            << HomeCardType::Recent << HomeCardType::Websites
            << HomeCardType::Performance;

  QWidget *cw = sectionWidget(HomeCardType::Carousel);
  if (m_carouselSlotLayout && cw)
    m_carouselSlotLayout->addWidget(cw);

  const QStringList order = {
      HomeCardType::QuickLinks,
      HomeCardType::Recent,
      HomeCardType::Websites,
      HomeCardType::Performance,
  };
  for (const QString &type : order)
    m_contentLayout->addWidget(sectionWidget(type));
  m_contentLayout->addStretch();

  // 隐藏当前布局未使用的区块，避免残留叠放
  for (auto it = m_sectionWidgets.constBegin(); it != m_sectionWidgets.constEnd(); ++it)
    it.value()->setVisible(usedTypes.contains(it.key()));

  // 布局稳定后按实际宽度刷新轮播高度
  QTimer::singleShot(0, this, [this]() { updateCarouselHeight(); });
}

QWidget* HomePage::sectionWidget(const QString &type)
{
  if (m_sectionWidgets.contains(type))
    return m_sectionWidgets.value(type);

  QWidget *w = nullptr;
  if (type == HomeCardType::Carousel)
    w = createCarouselSection();
  else if (type == HomeCardType::QuickLinks)
    w = createQuickLinksSection();
  else if (type == HomeCardType::Recent)
    w = createRecentPlaysSection();
  else if (type == HomeCardType::Websites)
    w = createWebsitesSection();
  else if (type == HomeCardType::Instances)
    w = createInstancesSection();
  else if (type == HomeCardType::Performance)
    w = createPerformanceSection();
  else if (type == HomeCardType::Clock)
    w = createClockSection();
  else if (type == HomeCardType::Account)
    w = createAccountSection();
  else if (type == HomeCardType::Ai)
    w = createAiSection();
  else if (type == HomeCardType::Search)
    w = createSearchSection();
  else if (type == HomeCardType::News)
    w = createNewsSection();
  else if (type == HomeCardType::Downloads)
    w = createDownloadsSection();
  else if (type == HomeCardType::System)
    w = createSystemSection();

  if (w)
    m_sectionWidgets.insert(type, w);
  return w;
}

// ============================================================================
// Section 1: 轮播图
// ============================================================================

QWidget* HomePage::createCarouselSection()
{
  auto *container = new QWidget();
  auto *layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  if (m_carouselPaths.isEmpty())
  {
    auto *placeholder = new QLabel(tr("暂无风景图片"));
    placeholder->setObjectName("carouselPlaceholder");
    placeholder->setAlignment(Qt::AlignCenter);
    m_carouselPlaceholder = placeholder;
    layout->addWidget(placeholder);
    return container;
  }

  // 图片显示（使用双层叠加实现向左滚动切换）
  m_carouselStack = new QWidget();
  m_carouselStack->setObjectName("carouselStack");
  m_carouselStack->installEventFilter(this);

  m_carouselImage = new QLabel(m_carouselStack);
  m_carouselImage->setObjectName("carouselImage");
  m_carouselImage->setScaledContents(true);

  m_carouselOverlay = new QLabel(m_carouselStack);
  m_carouselOverlay->setObjectName("carouselOverlay");
  m_carouselOverlay->setScaledContents(true);

  layout->addWidget(m_carouselStack);

  // 指示点
  auto *dotsLayout = new QHBoxLayout();
  dotsLayout->setAlignment(Qt::AlignCenter);
  dotsLayout->setSpacing(6);
  for (int i = 0; i < m_carouselPaths.size(); ++i)
  {
    auto *dot = new QPushButton();
    dot->setObjectName("carouselDot");
    dot->setFixedSize(4, 4);
    dot->setCursor(Qt::PointingHandCursor);
    int index = i;
    QObject::connect(dot, &QPushButton::clicked, [this, index]()
    {
      setCarouselIndex(index);
    });
    dotsLayout->addWidget(dot);
    m_carouselDots.append(dot);
  }
  layout->addLayout(dotsLayout);

  // 初始显示第一张
  setCarouselIndex(0);

  // 自动轮播定时器
  m_carouselTimer = new QTimer(this);
  m_carouselTimer->setInterval(5000);
  QObject::connect(m_carouselTimer, &QTimer::timeout, [this]()
  {
    updateCarousel();
  });
  m_carouselTimer->start();

  return container;
}

void HomePage::updateCarousel()
{
  if (m_carouselPaths.isEmpty())
  {
    return;
  }
  m_currentCarouselIndex = (m_currentCarouselIndex + 1) % m_carouselPaths.size();
  setCarouselIndex(m_currentCarouselIndex);
}

void HomePage::setCarouselIndex(int index)
{
  if (index < 0 || index >= m_carouselPaths.size())
  {
    return;
  }
  m_currentCarouselIndex = index;

  QPixmap pixmap(m_carouselPaths[index]);
  if (pixmap.isNull() || !m_carouselStack)
  {
    return;
  }

  // 更新指示点
  for (int i = 0; i < m_carouselDots.size(); ++i)
  {
    bool active = (i == index);
    m_carouselDots[i]->setProperty("active", active ? "true" : "false");
    m_carouselDots[i]->setFixedSize(4, 4);
    m_carouselDots[i]->style()->unpolish(m_carouselDots[i]);
    m_carouselDots[i]->style()->polish(m_carouselDots[i]);
  }

  // 首次显示没有上一张，直接显示，不做切换动画
  const bool isFirst = m_carouselImage->pixmap().isNull();
  if (isFirst || !m_carouselOverlay)
  {
    m_carouselImage->setPixmap(pixmap);
    return;
  }

  // 终止上一次进行中的动画
  if (m_carouselAnimGroup)
  {
    m_carouselAnimGroup->stop();
    m_carouselAnimGroup->deleteLater();
    m_carouselAnimGroup = nullptr;
  }
  m_carouselAnimating = true;

  QRect base = m_carouselStack->rect();
  if (base.isEmpty())
  {
    base = QRect(0, 0, m_carouselStack->width(), m_carouselStack->height());
  }

  int step = base.width();

  // 当前图停留在底层，新图放在右侧屏幕外，共同向左滑动
  m_carouselImage->setGeometry(base);
  m_carouselImage->raise();
  m_carouselOverlay->setPixmap(pixmap);
  m_carouselOverlay->setGeometry(base.translated(step, 0));
  m_carouselOverlay->raise();
  m_carouselOverlay->show();

  auto *group = new QParallelAnimationGroup(this);

  // 底层旧图向左滑出
  auto *oldAnim = new QPropertyAnimation(m_carouselImage, "geometry");
  oldAnim->setDuration(700);
  oldAnim->setStartValue(base);
  oldAnim->setEndValue(base.translated(-step, 0));
  oldAnim->setEasingCurve(QEasingCurve::InOutCubic);

  // 上层新图从右侧滑入
  auto *newAnim = new QPropertyAnimation(m_carouselOverlay, "geometry");
  newAnim->setDuration(700);
  newAnim->setStartValue(base.translated(step, 0));
  newAnim->setEndValue(base);
  newAnim->setEasingCurve(QEasingCurve::InOutCubic);

  group->addAnimation(oldAnim);
  group->addAnimation(newAnim);

  QObject::connect(group, &QParallelAnimationGroup::finished, this,
    [this, pixmap, base]()
    {
      // 动画结束，把 overlay 内容同步到底层并复位
      if (m_carouselOverlay)
      {
        m_carouselOverlay->setGeometry(base);
        m_carouselOverlay->lower();
      }
      m_carouselImage->setPixmap(pixmap);
      m_carouselImage->setGeometry(base);
      m_carouselAnimating = false;
      if (m_carouselAnimGroup)
      {
        m_carouselAnimGroup->deleteLater();
        m_carouselAnimGroup = nullptr;
      }
    });

  m_carouselAnimGroup = group;
  group->start();
}

// ============================================================================
// Section 2: 快捷入口
// ============================================================================

QWidget* HomePage::createQuickLinksSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(10);

  auto *titleLabel = new OutlinedLabel(tr("资源下载"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  m_quickLinksContainer = new QWidget();
  m_quickLinksContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_quickLinksLayout = new FlowLayout(m_quickLinksContainer, 0, 12, 12);
  rebuildQuickLinksContent();
  vlay->addWidget(m_quickLinksContainer);

  return section;
}

void HomePage::setBedrockMode(bool bedrock)
{
  m_bedrockMode = bedrock;
  rebuildQuickLinksContent();
}

void HomePage::rebuildQuickLinksContent()
{
  if (!m_quickLinksLayout)
  {
    return;
  }

  // 清除现有卡片
  QLayoutItem *item;
  while ((item = m_quickLinksLayout->takeAt(0)) != nullptr)
  {
    if (item->widget())
    {
      item->widget()->deleteLater();
    }
    delete item;
  }

  struct QuickLinkEntry
  {
    QString icon;
    QString label;
    void (HomePage::*signal)();
  };

  if (m_bedrockMode)
  {
    // 基岩版模式：只展示基岩版资源入口
    QList<QuickLinkEntry> entries = {
      {"nav_install",       tr("安装新实例"), &HomePage::installVersionClicked},
      {"nav_resourcepacks", tr("资源包"),     &HomePage::bedrockResourcepacksClicked},
      {"nav_textures",      tr("材质包"),     &HomePage::texturesClicked},
      {"nav_worlds",        tr("地图"),       &HomePage::bedrockWorldsClicked},
      {"nav_scripts",       tr("脚本"),       &HomePage::scriptsClicked},
    };

    for (const auto &entry : entries)
    {
      QString iconPath = QString(":/Images/Icons/%1.svg").arg(entry.icon);
      auto *card = createQuickLinkCard(iconPath, entry.label, nullptr);
      QObject::connect(static_cast<QPushButton*>(card), &QPushButton::clicked,
                       this, entry.signal);
      m_quickLinksLayout->addWidget(card);
    }
    return;
  }

  // Java 版模式：原有入口
  QList<QuickLinkEntry> entries = {
    {"nav_install",      tr("安装新实例"),  &HomePage::installNewInstanceClicked},
    {"nav_download",     tr("下载整合包"),  &HomePage::downloadModpackClicked},
    {"nav_import",       tr("导入整合包"),  &HomePage::importModpackClicked},
    {"nav_mods",         tr("模组"),        &HomePage::modsClicked},
    {"nav_datapacks",    tr("数据包"),      &HomePage::datapacksClicked},
    {"nav_resourcepacks", tr("资源包"),     &HomePage::resourcepacksClicked},
    {"nav_shaders",      tr("光影包"),      &HomePage::shadersClicked},
    {"nav_worlds",       tr("世界"),        &HomePage::worldsClicked},
  };

  for (const auto &entry : entries)
  {
    QString iconPath = QString(":/Images/Icons/%1.svg").arg(entry.icon);
    auto *card = createQuickLinkCard(iconPath, entry.label, nullptr);
    QObject::connect(static_cast<QPushButton*>(card), &QPushButton::clicked,
                     this, entry.signal);
    m_quickLinksLayout->addWidget(card);
  }
}

QWidget* HomePage::createQuickLinkCard(const QString &iconPath, const QString &label,
                                       const char * /*signal*/)
{
  auto *card = new QPushButton();
  card->setObjectName("quickLinkCard");
  card->setMinimumSize(100, 90);
  card->setCursor(Qt::PointingHandCursor);
  card->setFlat(true);
  card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(8, 12, 8, 8);
  cardLayout->setSpacing(6);
  cardLayout->setAlignment(Qt::AlignCenter);

  auto *iconLabel = new QLabel();
  iconLabel->setObjectName("quickLinkIcon");
  iconLabel->setPixmap(QPixmap(iconPath).scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  iconLabel->setAlignment(Qt::AlignCenter);
  cardLayout->addWidget(iconLabel);

  auto *textLabel = new QLabel(label);
  textLabel->setObjectName("quickLinkLabel");
  textLabel->setAlignment(Qt::AlignCenter);
  textLabel->setWordWrap(true);
  cardLayout->addWidget(textLabel);

  return card;
}

// ============================================================================
// Section 3: 最近游玩
// ============================================================================

QWidget* HomePage::createRecentPlaysSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(10);

  auto *titleLabel = new OutlinedLabel(tr("最近游玩"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  m_recentPlaysContainer = new QWidget();
  m_recentPlaysLayout = new FlowLayout(m_recentPlaysContainer, 0, 12, 12);
  m_recentPlaysLayout->setContentsMargins(0, 0, 0, 0);
  m_recentPlaysContainer->installEventFilter(this);
  vlay->addWidget(m_recentPlaysContainer);

  rebuildRecentPlaysContent();

  return section;
}

int HomePage::calculateRecentPlayCardWidth() const
{
  if (!m_recentPlaysContainer)
    return 220;

  const int containerW = m_recentPlaysContainer->width();
  if (containerW <= 0)
    return 220;

  const int margins = m_recentPlaysLayout ? m_recentPlaysLayout->contentsMargins().left() + m_recentPlaysLayout->contentsMargins().right() : 0;
  const int spacing = m_recentPlaysLayout ? m_recentPlaysLayout->horizontalSpacing() : 12;
  const int availW = containerW - margins;
  const int cols = 4;   // 一行固定四个
  const int cardW = (availW - (cols - 1) * spacing) / cols;
  return qMax(cardW, 120);
}

void HomePage::rebuildRecentPlaysContent()
{
  if (!m_recentPlaysLayout)
  {
    return;
  }

  if (m_recentPlaysResizing)
    return;
  m_recentPlaysResizing = true;

  // 清除现有内容
  QLayoutItem *item;
  while ((item = m_recentPlaysLayout->takeAt(0)) != nullptr)
  {
    if (item->widget())
    {
      item->widget()->deleteLater();
    }
    delete item;
  }

  if (m_recentPlays.isEmpty())
  {
    auto *placeholder = new QLabel(tr("暂无游玩记录"));
    placeholder->setObjectName("homeEmptyLabel");
    placeholder->setAlignment(Qt::AlignCenter);
    m_recentPlaysLayout->addWidget(placeholder);
    m_recentPlaysResizing = false;
    return;
  }

  const int count = qMin(m_recentPlays.size(), 4);
  const int cardW = calculateRecentPlayCardWidth();

  // ── 横排卡片：一行四个（banner 渐变 + logo + 名称 + 最近游玩时间） ──
  for (int i = 0; i < count; ++i)
  {
    const RecentPlayEntry &entry = m_recentPlays[i];

    QWidget *card = buildRecentPlayCard(entry, m_recentPlaysContainer, cardW);
    m_recentPlaysLayout->addWidget(card);
  }

  m_recentPlaysResizing = false;
}

/* 最近游玩卡片构建（一行四个横排；parent 为挂载图层） */
QWidget* HomePage::buildRecentPlayCard(const RecentPlayEntry &entry, QWidget *parent, int cardWidth)
{
    ModInfo seed;
    seed.id = entry.name;
    seed.source = QStringLiteral("local");
    QColor c1, c2;
    MasonryContentCard::paletteFor(seed, c1, c2);

    const int bannerH = 110;
    const int kCardW = cardWidth;
    auto *card = new QWidget(parent);
    card->setObjectName("recentPlayCard");
    card->setFixedWidth(kCardW);
    card->setCursor(Qt::PointingHandCursor);
    card->setToolTip(tr("所属实例：%1").arg(entry.instanceName));
    MasonryContentCard::applyShadow(card);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(1, 1, 1, 1);   // banner 内缩，露出卡片边框
    cardLayout->setSpacing(0);

    auto *banner = new QLabel(card);
    banner->setObjectName("recentPlayBanner");
    banner->setFixedHeight(bannerH);
    QPixmap iconPix;
    if (!entry.iconPath.isEmpty() && iconPix.load(entry.iconPath) && !iconPix.isNull())
    {
      // 存档图标直接作为封面（顶部圆角裁剪，对齐卡片 16px 边框圆角）
      const int bw = kCardW - 2;
      QPixmap cover(bw, bannerH);
      cover.fill(Qt::transparent);
      QPainter p(&cover);
      p.setRenderHint(QPainter::Antialiasing);
      p.setRenderHint(QPainter::SmoothPixmapTransform);
      QPainterPath clip;
      clip.addRoundedRect(0, 0, bw, bannerH, 16, 16);
      p.setClipPath(clip);
      QPixmap scaled = iconPix.scaled(bw, bannerH,
                                      Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      p.drawPixmap((bw - scaled.width()) / 2, (bannerH - scaled.height()) / 2, scaled);
      p.end();
      banner->setPixmap(cover);
    }
    else
    {
      banner->setPixmap(MasonryContentCard::makeBannerPixmap(c1, c2, kCardW - 2, bannerH));
    }
    cardLayout->addWidget(banner);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(12, 12, 12, 10);
    bv->setSpacing(6);

    auto *nameLabel = new QLabel(entry.name, body);
    nameLabel->setObjectName("recentPlayName");
    bv->addWidget(nameLabel);

    auto *timeLabel = new QLabel(body);
    timeLabel->setObjectName("recentPlayTime");
    timeLabel->setText(tr("上次游玩 %1").arg(entry.lastPlayed.isEmpty() ? tr("从未游玩") : entry.lastPlayed));
    bv->addWidget(timeLabel);
    bv->addStretch();

    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);
    btnLayout->setContentsMargins(0, 4, 0, 0);
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    auto createActionBtn = [&](const QString &iconPath, const QString &tooltip) -> QPushButton* {
      auto *btn = new QPushButton();
      btn->setObjectName("recentPlayActionBtn");
      btn->setFixedSize(28, 28);
      btn->setToolTip(tooltip);
      btn->setCursor(Qt::PointingHandCursor);
      btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 16));
      btn->setIconSize(QSize(16, 16));
      return btn;
    };

    // 启动按钮（主色填充圆角，图标 + 文字）
    auto *playBtn = new QPushButton();
    playBtn->setObjectName("recentPlayLaunchBtn");
    playBtn->setToolTip(tr("快捷启动"));
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setText(tr("启动"));
    playBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/quick_launch.svg",
                     QColor(255, 255, 255), 14));
    playBtn->setIconSize(QSize(14, 14));
    QObject::connect(playBtn, &QPushButton::clicked, [this, entry]() {
      if (entry.type == tr("服务器"))
        emit recentPlayQuickLaunchServerClicked(entry.serverAddress, entry.serverPort, entry.instancePath);
      else
        emit recentPlayQuickLaunchClicked(entry.name, entry.instancePath);
    });

    auto *moreBtn = createActionBtn(":/Images/Icons/list.svg", tr("更多操作"));
    QObject::connect(moreBtn, &QPushButton::clicked, [this, entry, moreBtn]() {
      QMenu menu(moreBtn);
      QAction *launchAct = menu.addAction(tr("快捷启动"));
      QAction *openFolderAct = nullptr;
      if (entry.type != tr("服务器"))
        openFolderAct = menu.addAction(tr("打开存档文件夹"));
      QAction *copyAct = menu.addAction(entry.type == tr("服务器") ? tr("复制服务器地址") : tr("复制存档名"));
      QAction *manageAct = menu.addAction(entry.type == tr("服务器") ? tr("服务器管理") : tr("存档管理"));
      QAction *settingsAct = menu.addAction(tr("实例设置"));
      QAction *chosen = menu.exec(moreBtn->mapToGlobal(QPoint(0, moreBtn->height() + 4)));
      if (!chosen) return;
      if (chosen == launchAct)
      {
        if (entry.type == tr("服务器"))
          emit recentPlayQuickLaunchServerClicked(entry.serverAddress, entry.serverPort, entry.instancePath);
        else
          emit recentPlayQuickLaunchClicked(entry.name, entry.instancePath);
      }
      else if (chosen == openFolderAct)
      {
        emit recentPlayOpenFolderClicked(entry.savePath);
      }
      else if (chosen == copyAct)
      {
        emit recentPlayCopyNameClicked(entry.type == tr("服务器") ? entry.serverAddress : entry.name);
      }
      else if (chosen == manageAct)
      {
        if (entry.type == tr("服务器"))
          emit recentPlayServerSettingsClicked(entry.instancePath);
        else
          emit recentPlaySettingsClicked(entry.instancePath);
      }
      else if (chosen == settingsAct)
      {
        emit recentPlayInstanceSettingsClicked(entry.instancePath);
      }
    });
    btnLayout->addStretch();
    btnLayout->addWidget(moreBtn);
    btnLayout->addWidget(playBtn);
    bv->addLayout(btnLayout);

    cardLayout->addWidget(body);
    return card;
}

void HomePage::refreshRecentPlays()
{
  loadRecentPlays();
  rebuildRecentPlaysContent();
  loadInstances();
  rebuildInstancesContent();
}

// ============================================================================
// Section 4: 常用网站
// ============================================================================

QWidget* HomePage::createWebsitesSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(10);

  auto *titleLabel = new OutlinedLabel(tr("常用网站"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  auto *container = new QWidget();
  container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *layout = new FlowLayout(container, 0, 10, 10);

  struct WebsiteEntry
  {
    QString name;
    QString url;
  };

  QList<WebsiteEntry> websites = {
    {"Minecraft官网",    "https://www.minecraft.net"},
    {"Minecraft Wiki",  "https://minecraft.wiki"},
    {"MC百科",          "https://www.mcmod.cn"},
    {"CurseForge",      "https://www.curseforge.com/minecraft"},
    {"Modrinth",        "https://modrinth.com"},
    {"苦力怕论坛",       "https://klpbbs.com"},
    {"MineBBS",         "https://www.minebbs.com"},
    {"Chunkbase",       "https://www.chunkbase.com"},
  };

  for (const auto &site : websites)
  {
    auto *card = createWebsiteCard(site.name, site.url);
    layout->addWidget(card);
  }

  vlay->addWidget(container);
  return section;
}

QWidget* HomePage::createWebsiteCard(const QString &name, const QString &url)
{
  auto *card = new QPushButton();
  card->setObjectName("websiteCard");
  card->setMinimumSize(120, 60);
  card->setCursor(Qt::PointingHandCursor);
  card->setFlat(true);
  card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(10, 8, 10, 8);
  cardLayout->setSpacing(2);

  auto *nameLabel = new QLabel(name);
  nameLabel->setObjectName("websiteName");
  nameLabel->setAlignment(Qt::AlignCenter);
  cardLayout->addWidget(nameLabel);

  // 显示时去掉协议前缀（https://），点击仍使用完整 URL 打开
  QString displayUrl = url;
  displayUrl.replace(QStringLiteral("https://"), QString());
  displayUrl.replace(QStringLiteral("http://"), QString());

  auto *urlLabel = new QLabel(displayUrl);
  urlLabel->setObjectName("websiteUrl");
  urlLabel->setAlignment(Qt::AlignCenter);
  urlLabel->setWordWrap(true);
  cardLayout->addWidget(urlLabel);

  QObject::connect(card, &QPushButton::clicked, [url]()
  {
    QDesktopServices::openUrl(QUrl(url));
  });

  return card;
}

// ============================================================================
// Section 5: Instances（我的实例）
// ============================================================================

void HomePage::loadInstances()
{
  m_instances.clear();

  const QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
  for (const auto &folder : folders)
  {
    QDir versionsDir(folder.path + "/versions");
    if (!versionsDir.exists())
      continue;

    QDirIterator verIt(versionsDir.path(), QDir::Dirs | QDir::NoDotAndDotDot,
                       QDirIterator::NoIteratorFlags);
    while (verIt.hasNext())
    {
      QString versionPath = verIt.next();
      QString versionName = QFileInfo(versionPath).fileName();
      if (!QFile::exists(versionPath + "/" + versionName + ".json"))
        continue;

      RecentPlayEntry entry;
      entry.type = tr("实例");
      entry.name = versionName;
      entry.instancePath = versionPath;
      entry.instanceName = versionName;

      QFile versionJsonFile(versionPath + "/" + versionName + ".json");
      if (versionJsonFile.open(QIODevice::ReadOnly))
      {
        QJsonDocument jsonDoc = QJsonDocument::fromJson(versionJsonFile.readAll());
        versionJsonFile.close();
        if (jsonDoc.isObject())
        {
          const QJsonObject rootObj = jsonDoc.object();
          QString v = rootObj["inheritsFrom"].toString();
          if (v.isEmpty()) v = rootObj["clientVersion"].toString();
          if (v.isEmpty()) v = rootObj["id"].toString();
          entry.version = v;

          if (rootObj.contains("fabricLoader"))       entry.loader = "Fabric";
          else if (rootObj.contains("quiltLoader"))   entry.loader = "Quilt";
          else if (rootObj.contains("neoForge"))      entry.loader = "NeoForge";
          else if (rootObj.contains("forge"))         entry.loader = "Forge";
          else if (rootObj.contains("optifine"))      entry.loader = "OptiFine";
        }
      }
      m_instances.append(entry);
    }
  }

  std::sort(m_instances.begin(), m_instances.end(),
            [](const RecentPlayEntry &a, const RecentPlayEntry &b)
            {
              return a.name < b.name;
            });
}

QWidget* HomePage::createInstancesSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(10);

  auto *titleLabel = new OutlinedLabel(tr("我的实例"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  // ── 概览：加载器分布分段条 ──
  auto *summary = new QWidget();
  summary->setObjectName("homeMiniCard");
  auto *sl = new QVBoxLayout(summary);
  sl->setContentsMargins(12, 10, 12, 10);
  sl->setSpacing(6);

  auto *countRow = new QHBoxLayout();
  countRow->setContentsMargins(0, 0, 0, 0);
  auto *countLabel = new QLabel(tr("共 %1 个实例").arg(m_instances.size()), summary);
  countLabel->setObjectName("homeInstCount");
  countRow->addWidget(countLabel);
  countRow->addStretch();
  sl->addLayout(countRow);

  // 统计各加载器数量
  int fabric = 0, forge = 0, neoforge = 0, quilt = 0, other = 0;
  for (const RecentPlayEntry &entry : m_instances)
  {
    const QString loader = entry.loader;
    if (loader.contains("NeoForge"))      ++neoforge;
    else if (loader.contains("Fabric"))   ++fabric;
    else if (loader.contains("Forge"))    ++forge;
    else if (loader.contains("Quilt"))    ++quilt;
    else                                  ++other;
  }
  const int total = qMax(1, m_instances.size());
  QList<HomeSegment> segments;
  HomeSegment fs, fo, nf, qu, ot;
  fs  = { QColor("#10B981"), fabric   ? double(fabric) / total : 0.0, "Fabric" };
  fo  = { QColor("#F59E0B"), forge    ? double(forge) / total : 0.0, "Forge" };
  nf  = { QColor("#F97316"), neoforge ? double(neoforge) / total : 0.0, "NeoForge" };
  qu  = { QColor("#8B5CF6"), quilt    ? double(quilt) / total : 0.0, "Quilt" };
  ot  = { QColor("#94A3B8"), other    ? double(other) / total : 0.0, tr("其他") };
  segments << fs << fo << nf << qu << ot;

  auto *segBar = new SegmentedBar(summary);
  segBar->setBarHeight(12);
  segBar->setSegments(segments);
  sl->addWidget(segBar);

  // 图例
  auto *legend = new QHBoxLayout();
  legend->setContentsMargins(0, 0, 0, 0);
  legend->setSpacing(8);
  auto addLegend = [&](const HomeSegment &seg, int count) {
    if (count <= 0)
      return;
    auto *dot = new QLabel(summary);
    dot->setFixedSize(9, 9);
    dot->setStyleSheet(QString("background-color: %1; border-radius: 5px;").arg(seg.color.name()));
    legend->addWidget(dot);
    auto *text = new QLabel(QString("%1 %2").arg(seg.label).arg(count), summary);
    text->setObjectName("homeLegendText");
    legend->addWidget(text);
  };
  addLegend(fs, fabric); addLegend(fo, forge); addLegend(nf, neoforge);
  addLegend(qu, quilt); addLegend(ot, other);
  legend->addStretch();
  sl->addLayout(legend);

  vlay->addWidget(summary);

  m_instancesContainer = new QWidget();
  m_instancesContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_instancesLayout = new FlowLayout(m_instancesContainer, 0, 10, 10);
  rebuildInstancesContent();
  vlay->addWidget(m_instancesContainer);

  return section;
}

void HomePage::rebuildInstancesContent()
{
  if (!m_instancesLayout)
    return;

  QLayoutItem *item;
  while ((item = m_instancesLayout->takeAt(0)) != nullptr)
  {
    if (item->widget())
      item->widget()->deleteLater();
    delete item;
  }

  if (m_instances.isEmpty())
  {
    auto *placeholder = new QLabel(tr("暂无实例，可前往「安装新实例」创建"));
    placeholder->setObjectName("homeEmptyLabel");
    placeholder->setAlignment(Qt::AlignCenter);
    m_instancesLayout->addWidget(placeholder);
    return;
  }

  const int count = qMin(m_instances.size(), 6);
  for (int i = 0; i < count; ++i)
  {
    const RecentPlayEntry &entry = m_instances[i];

    auto *tile = new QWidget();
    tile->setObjectName("instanceTile");
    tile->setCursor(Qt::PointingHandCursor);
    tile->setFixedWidth(178);
    tile->installEventFilter(this);
    tile->setProperty("instancePath", entry.instancePath);

    auto *hlay = new QHBoxLayout(tile);
    hlay->setContentsMargins(10, 6, 6, 6);
    hlay->setSpacing(6);

    auto *info = new QVBoxLayout();
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(2);

    auto *nameLabel = new QLabel(entry.name);
    nameLabel->setObjectName("instanceTileName");
    nameLabel->setToolTip(entry.name);
    info->addWidget(nameLabel);

    QString meta = entry.version;
    if (!entry.loader.isEmpty())
      meta = meta.isEmpty() ? entry.loader : meta + " · " + entry.loader;
    auto *metaLabel = new QLabel(meta);
    metaLabel->setObjectName("instanceTileMeta");
    info->addWidget(metaLabel);
    hlay->addLayout(info, 1);

    auto *playBtn = new QPushButton();
    playBtn->setObjectName("instanceTilePlay");
    playBtn->setFixedSize(28, 28);
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setToolTip(tr("启动"));
    playBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/play.svg",
                       QColor(ThemeManager::instance()->currentThemeColor()), 14));
    playBtn->setIconSize(QSize(14, 14));
    QObject::connect(playBtn, &QPushButton::clicked, [this, entry]()
    {
      emit instanceLaunchRequested(entry.instancePath);
    });
    hlay->addWidget(playBtn);

    m_instancesLayout->addWidget(tile);
  }
}

// ============================================================================
// Section 5.5: 更多卡片（大小不一）
// ============================================================================

QWidget* HomePage::createClockSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(6);

  auto *titleLabel = new OutlinedLabel(tr("时钟"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  auto *wrap = new QWidget();
  wrap->setObjectName("homeMiniCard");
  auto *wl = new QVBoxLayout(wrap);
  wl->setContentsMargins(12, 12, 12, 10);
  wl->setSpacing(4);
  wl->setAlignment(Qt::AlignHCenter);

  // 模拟表盘
  m_clockAnalog = new AnalogClock(wrap);
  m_clockAnalog->setFixedSize(112, 112);
  m_clockAnalog->setForeground(ThemeManager::instance()->currentTextColor());
  m_clockAnalog->setHandColor(ThemeManager::instance()->currentTextColor());
  wl->addWidget(m_clockAnalog, 0, Qt::AlignHCenter);

  m_clockTimeLabel = new QLabel(wrap);
  m_clockTimeLabel->setObjectName("homeClockTime");
  m_clockTimeLabel->setAlignment(Qt::AlignCenter);
  m_clockDateLabel = new QLabel(wrap);
  m_clockDateLabel->setObjectName("homeClockDate");
  m_clockDateLabel->setAlignment(Qt::AlignCenter);
  wl->addWidget(m_clockTimeLabel);
  wl->addWidget(m_clockDateLabel);

  auto updateClock = [this]() {
    const QDateTime now = QDateTime::currentDateTime();
    if (m_clockTimeLabel)
      m_clockTimeLabel->setText(now.toString("HH:mm"));
    if (m_clockDateLabel)
      m_clockDateLabel->setText(now.toString("yyyy年MM月dd日 dddd"));
  };
  updateClock();

  m_clockTimer = new QTimer(this);
  m_clockTimer->setInterval(1000);
  connect(m_clockTimer, &QTimer::timeout, this, updateClock);
  m_clockTimer->start();

  vlay->addWidget(wrap);
  return section;
}

QWidget* HomePage::createAccountSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(6);

  auto *titleLabel = new OutlinedLabel(tr("账号"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  const AccountInfo acc = SettingsManager::instance()->getDefaultAccount();

  auto *btn = new QPushButton(section);
  btn->setObjectName("homeMiniCardBtn");
  btn->setCursor(Qt::PointingHandCursor);
  btn->setFlat(true);
  auto *bl = new QHBoxLayout(btn);
  bl->setContentsMargins(10, 8, 10, 8);
  bl->setSpacing(8);

  auto *avatar = new QLabel(btn);
  avatar->setObjectName("homeAvatar");
  avatar->setFixedSize(34, 34);
  avatar->setAlignment(Qt::AlignCenter);
  const QString letter = acc.username.isEmpty() ? tr("?") : acc.username.trimmed().left(1).toUpper();
  avatar->setPixmap(MasonryContentCard::makeLogoPixmap(QColor("#10B981"), QColor("#047857"), letter));
  bl->addWidget(avatar);

  auto *info = new QVBoxLayout();
  info->setContentsMargins(0, 0, 0, 0);
  info->setSpacing(2);
  auto *name = new QLabel(acc.username.isEmpty() ? tr("未登录") : acc.username);
  name->setObjectName("homeAccountName");
  info->addWidget(name);
  auto *type = new QLabel(acc.type.isEmpty() ? tr("点击登录") : acc.type);
  type->setObjectName("homeAccountType");
  info->addWidget(type);
  bl->addLayout(info, 1);

  auto *arrow = new QLabel(QStringLiteral("›"));
  arrow->setObjectName("homeAccountArrow");
  bl->addWidget(arrow);

  connect(btn, &QPushButton::clicked, this, &HomePage::accountManageRequested);
  vlay->addWidget(btn);
  return section;
}

QWidget* HomePage::createAiSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(6);

  auto *titleLabel = new OutlinedLabel(tr("AI 助手"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  auto *btn = new QPushButton(section);
  btn->setObjectName("homeMiniCardBtn");
  btn->setCursor(Qt::PointingHandCursor);
  btn->setFlat(true);
  auto *bl = new QHBoxLayout(btn);
  bl->setContentsMargins(10, 8, 10, 8);
  bl->setSpacing(8);

  auto *icon = new QLabel(btn);
  icon->setObjectName("homeIconChip");
  icon->setFixedSize(34, 34);
  icon->setAttribute(Qt::WA_StyledBackground, true);
  icon->setAlignment(Qt::AlignCenter);
  icon->setPixmap(IconHelper::loadColoredIcon(":/Images/Icons/brain.svg",
                    QColor(ThemeManager::instance()->currentThemeColor()), 20).pixmap(20, 20));
  bl->addWidget(icon);

  auto *info = new QVBoxLayout();
  info->setContentsMargins(0, 0, 0, 0);
  info->setSpacing(2);
  auto *t = new QLabel(tr("智能对话 · 指令翻译"));
  t->setObjectName("homeAccountName");
  info->addWidget(t);
  auto *d = new QLabel(tr("探索世界，AI 相伴"));
  d->setObjectName("homeAccountType");
  info->addWidget(d);
  bl->addLayout(info, 1);

  auto *arrow = new QLabel(QStringLiteral("›"));
  arrow->setObjectName("homeAccountArrow");
  bl->addWidget(arrow);

  connect(btn, &QPushButton::clicked, this, &HomePage::aiChatRequested);
  vlay->addWidget(btn);
  return section;
}

QWidget* HomePage::createSearchSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(6);

  auto *titleLabel = new OutlinedLabel(tr("快捷搜索"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  auto *btn = new QPushButton(section);
  btn->setObjectName("homeMiniCardBtn");
  btn->setCursor(Qt::PointingHandCursor);
  btn->setFlat(true);
  auto *bl = new QHBoxLayout(btn);
  bl->setContentsMargins(10, 8, 10, 8);
  bl->setSpacing(8);

  auto *icon = new QLabel(btn);
  icon->setObjectName("homeIconChip");
  icon->setFixedSize(34, 34);
  icon->setAttribute(Qt::WA_StyledBackground, true);
  icon->setAlignment(Qt::AlignCenter);
  icon->setPixmap(IconHelper::loadColoredIcon(":/Images/Icons/search.svg",
                    QColor(ThemeManager::instance()->currentThemeColor()), 20).pixmap(20, 20));
  bl->addWidget(icon);

  auto *info = new QVBoxLayout();
  info->setContentsMargins(0, 0, 0, 0);
  info->setSpacing(2);
  auto *t = new QLabel(tr("搜索模组 · 资源 · 存档"));
  t->setObjectName("homeAccountName");
  info->addWidget(t);
  auto *d = new QLabel(tr("海量内容，一触即达"));
  d->setObjectName("homeAccountType");
  info->addWidget(d);
  bl->addLayout(info, 1);

  auto *arrow = new QLabel(QStringLiteral("›"));
  arrow->setObjectName("homeAccountArrow");
  bl->addWidget(arrow);

  connect(btn, &QPushButton::clicked, this, &HomePage::searchRequested);
  vlay->addWidget(btn);
  return section;
}

QWidget* HomePage::createNewsSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(6);

  auto *titleLabel = new OutlinedLabel(tr("公告"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  struct NewsItem
  {
    QString title;
    QString date;
    QString summary;
    QString url;
  };
  QList<NewsItem> items = {
      {tr("BlockBox 1.0 正式发布"),       "2026-06-01", tr("全新的启动器体验，支持 Java 与基岩版双模式。"), QStringLiteral("https://www.minecraft.net/")},
      {tr("多线程下载引擎上线"),           "2026-05-20", tr("资源下载大幅提速，断点续传更稳定。"),             QStringLiteral("https://www.minecraft.net/")},
      {tr("AI 助手功能预览"),             "2026-05-01", tr("内置智能对话与指令翻译，探索新世界。"),             QStringLiteral("https://www.minecraft.net/")},
  };

  for (const auto &item : items)
  {
    auto *card = new NewsCard(item.title, item.date, item.summary, section);
    QObject::connect(card, &NewsCard::clicked, [item]() {
      QDesktopServices::openUrl(QUrl(item.url));
    });
    vlay->addWidget(card);
  }
  return section;
}

QWidget* HomePage::createDownloadsSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(6);

  auto *header = new QHBoxLayout();
  header->setContentsMargins(0, 0, 0, 0);
  header->setSpacing(8);
  auto *titleLabel = new OutlinedLabel(tr("下载任务"), section);
  titleLabel->setObjectName("homeSectionTitle");
  header->addWidget(titleLabel);
  header->addStretch();
  auto *moreBtn = new QPushButton(tr("查看全部"), section);
  moreBtn->setObjectName("homeLinkBtn");
  moreBtn->setCursor(Qt::PointingHandCursor);
  QObject::connect(moreBtn, &QPushButton::clicked, this, &HomePage::taskListRequested);
  header->addWidget(moreBtn);
  vlay->addLayout(header);

  m_downloadsContainer = new QWidget();
  m_downloadsLayout = new FlowLayout(m_downloadsContainer, 0, 8, 8);
  vlay->addWidget(m_downloadsContainer);

  auto *dtm = DownloadTaskManager::instance();
  connect(dtm, &DownloadTaskManager::taskAdded, this, &HomePage::rebuildDownloadsContent);
  connect(dtm, &DownloadTaskManager::taskRemoved, this, &HomePage::rebuildDownloadsContent);
  connect(dtm, &DownloadTaskManager::taskStatusChanged, this, &HomePage::rebuildDownloadsContent);
  connect(dtm, &DownloadTaskManager::taskProgressUpdated, this, &HomePage::rebuildDownloadsContent);

  rebuildDownloadsContent();
  return section;
}

void HomePage::rebuildDownloadsContent()
{
  if (!m_downloadsLayout)
    return;

  QLayoutItem *item;
  while ((item = m_downloadsLayout->takeAt(0)) != nullptr)
  {
    if (item->widget())
      item->widget()->deleteLater();
    delete item;
  }

  QList<DownloadTask> active;
  const QList<DownloadTask> tasks = DownloadTaskManager::instance()->getAllTasks();
  for (const DownloadTask &task : tasks)
  {
    if (task.status == DownloadTaskStatus::Downloading ||
        task.status == DownloadTaskStatus::Queued ||
        task.status == DownloadTaskStatus::Paused)
      active.append(task);
  }

  if (active.isEmpty())
  {
    auto *placeholder = new QLabel(tr("暂无进行中的下载任务"));
    placeholder->setObjectName("homeEmptyLabel");
    placeholder->setAlignment(Qt::AlignCenter);
    m_downloadsLayout->addWidget(placeholder);
    return;
  }

const int count = qMin(active.size(), 4);
  for (int i = 0; i < count; ++i)
  {
    const DownloadTask &task = active[i];

    auto *row = new QWidget();
    row->setObjectName("homeDownloadRow");
    row->setCursor(Qt::PointingHandCursor);
    auto *rl = new QVBoxLayout(row);
    rl->setContentsMargins(10, 8, 10, 8);
    rl->setSpacing(4);

    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(8);
    auto *name = new QLabel(task.instanceName);
    name->setObjectName("homeDownloadName");
    name->setToolTip(task.currentStep.isEmpty() ? task.instanceName : task.currentStep);
    top->addWidget(name, 1);

    // 环形进度代替朴素进度条
    auto *ring = new RingGauge(row);
    ring->setFixedSize(40, 40);
    ring->setThickness(5);
    QColor ringColor = QColor(ThemeManager::instance()->currentThemeColor());
    if (task.status == DownloadTaskStatus::Paused)
      ringColor = QColor("#F59E0B");
    ring->setColor(ringColor);
    ring->setCenterText(QString::number(qBound(0, task.progress, 100)) + "%");
    ring->setValue(task.progress);
    top->addWidget(ring);
    rl->addLayout(top);

    // 底部元信息：状态 + 速度 + 当前步骤
    QString statusText;
    switch (task.status)
    {
    case DownloadTaskStatus::Paused:   statusText = tr("已暂停"); break;
    case DownloadTaskStatus::Queued:   statusText = tr("排队中"); break;
    default:                           statusText = tr("下载中"); break;
    }
    QString meta = DownloadUtils::formatSpeed(task.downloadSpeed);
    if (!meta.isEmpty())
      meta = statusText + " · " + meta;
    else
      meta = statusText;
    if (!task.currentStep.isEmpty())
      meta = meta + " · " + task.currentStep;
    auto *metaLabel = new QLabel(meta);
    metaLabel->setObjectName("homeDownloadMeta");
    rl->addWidget(metaLabel);

    row->installEventFilter(this);
    row->setProperty("openTaskList", true);
    m_downloadsLayout->addWidget(row);
  }
}

QWidget* HomePage::createSystemSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(6);

  auto *titleLabel = new OutlinedLabel(tr("系统信息"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  auto *wrap = new QWidget();
  wrap->setObjectName("homeMiniCard");
  auto *wl = new QVBoxLayout(wrap);
  wl->setContentsMargins(12, 12, 12, 10);
  wl->setSpacing(8);

  // ── 环形仪表：磁盘 / 内存 ──
  auto *ringsRow = new QHBoxLayout();
  ringsRow->setContentsMargins(0, 0, 0, 0);
  ringsRow->setSpacing(20);
  ringsRow->setAlignment(Qt::AlignCenter);

  auto makeGauge = [this, wrap](RingGauge *&ring, QLabel *&caption,
                                const QColor &color) {
    auto *col = new QWidget(wrap);
    auto *cl = new QVBoxLayout(col);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(2);
    cl->setAlignment(Qt::AlignHCenter);
    ring = new RingGauge(col);
    ring->setFixedSize(72, 72);
    ring->setThickness(8);
    ring->setColor(color);
    ring->setValue(0);
    cl->addWidget(ring, 0, Qt::AlignHCenter);
    caption = new QLabel(col);
    caption->setObjectName("homeGaugeCaption");
    caption->setAlignment(Qt::AlignCenter);
    cl->addWidget(caption);
    return col;
  };

  const QColor themeColor(ThemeManager::instance()->currentThemeColor());
  ringsRow->addWidget(makeGauge(m_sysDiskRing, m_sysDiskLabel, QColor("#10B981")));
  ringsRow->addWidget(makeGauge(m_sysMemRing, m_sysMemLabel, themeColor));
  wl->addLayout(ringsRow);

  // 磁盘用量（静态计算）
  QStorageInfo storage(QCoreApplication::applicationDirPath());
  storage.refresh();
  if (m_sysDiskRing && storage.isReady() && storage.bytesTotal() > 0)
  {
    const int usedPct = qRound((storage.bytesTotal() - storage.bytesAvailable())
                               * 100.0 / storage.bytesTotal());
    m_sysDiskRing->setValue(usedPct);
    m_sysDiskRing->setCenterText(QString::number(usedPct) + "%");
    if (m_sysDiskLabel)
      m_sysDiskLabel->setText(tr("磁盘 · 可用 %1 GB")
                                  .arg(QString::number(storage.bytesAvailable() / (1024.0 * 1024.0 * 1024.0), 'f', 1)));
  }
  else if (m_sysDiskLabel)
  {
    m_sysDiskLabel->setText(tr("磁盘"));
  }

  // 内存用量（定时采样）
  m_systemMonitor = new HardwareMonitor(this);
  connect(m_systemMonitor, &HardwareMonitor::dataRefreshed, this,
          [this](const HardwareData &data) {
    if (!m_sysMemRing)
      return;
    m_sysMemRing->setValue(qBound(0, data.memoryUsagePercent, 100));
    m_sysMemRing->setCenterText(QString::number(qBound(0, data.memoryUsagePercent, 100)) + "%");
    if (m_sysMemLabel)
      m_sysMemLabel->setText(data.memoryAvailable
                             ? tr("内存 · 已用 %1%").arg(data.memoryUsagePercent)
                             : tr("内存"));
  });
  m_systemMonitor->start(2000);

  // ── 文字信息行 ──
  const SystemInfoData info = SystemInfo::collect();
  struct SysRow { QString label; QString value; };
  QList<SysRow> rows;
  rows.append({tr("系统"), info.osFullName.isEmpty() ? info.osName : info.osFullName});
  rows.append({tr("架构"), info.architecture});
  if (storage.isReady() && storage.bytesTotal() > 0)
  {
    const double freeGb = storage.bytesAvailable() / (1024.0 * 1024.0 * 1024.0);
    const double totalGb = storage.bytesTotal() / (1024.0 * 1024.0 * 1024.0);
    rows.append({tr("磁盘"), tr("%1 GB 可用 / %2 GB").arg(QString::number(freeGb, 'f', 1),
                                                          QString::number(totalGb, 'f', 1))});
  }

  for (const auto &row : rows)
  {
    auto *rl = new QHBoxLayout();
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(8);
    auto *lbl = new QLabel(row.label);
    lbl->setObjectName("homeSysLabel");
    rl->addWidget(lbl);
    auto *val = new QLabel(row.value);
    val->setObjectName("homeSysValue");
    val->setWordWrap(true);
    rl->addWidget(val, 1);
    wl->addLayout(rl);
  }

  vlay->addWidget(wrap);
  return section;
}

// ============================================================================
// Section 6: 性能监控
// ============================================================================

QWidget* HomePage::createPerformanceSection()
{
  auto *section = new QWidget();
  section->setObjectName("homeCard");
  auto *vlay = new QVBoxLayout(section);
  vlay->setContentsMargins(0, 0, 0, 0);
  vlay->setSpacing(10);

  auto *titleLabel = new OutlinedLabel(tr("性能监控"), section);
  titleLabel->setObjectName("homeSectionTitle");
  vlay->addWidget(titleLabel);

  auto *container = new QWidget();
  container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *layout = new FlowLayout(container, 0, 10, 10);

  struct PerfCardInfo
  {
    QString name;
    QColor color;
    bool available;
  };

  QList<PerfCardInfo> cardInfos = {
    {"CPU",  QColor("#4CAF50"), true},
    {tr("内存"), QColor(ThemeManager::instance()->currentInfoAccentColor()), true},
    {"WiFi", QColor("#FF9800"), true},
    {"GPU 0", QColor("#9C27B0"), true},
    {"GPU 1", QColor("#F44336"), false},
  };

  for (const auto &info : cardInfos)
  {
    auto *card = new PerfMonitorCard(info.name);
    card->setChartColor(info.color);
    card->setAvailable(info.available);
    layout->addWidget(card);
    m_perfCards.append(card);

    QObject::connect(card, &PerfMonitorCard::zoomRequested,
                     this, &HomePage::showPerformanceDetail);
  }

  m_hardwareMonitor = new HardwareMonitor(this);
  QObject::connect(m_hardwareMonitor, &HardwareMonitor::dataRefreshed,
                   this, [this](const HardwareData &)
  {
    refreshPerformanceData();
  });
  m_hardwareMonitor->start(2000);

  vlay->addWidget(container);
  return section;
}

void HomePage::refreshPerformanceData()
{
  const HardwareData &data = m_hardwareMonitor->currentData();

  for (auto *card : m_perfCards)
  {
    const QString &name = card->cardName();

    if (name == "CPU")
    {
      card->setAvailable(data.cpuAvailable);
      card->setPercent(data.cpuUsagePercent);
    }
    else if (name == tr("内存"))
    {
      card->setAvailable(data.memoryAvailable);
      card->setPercent(data.memoryUsagePercent);
    }
    else if (name == "WiFi")
    {
      card->setAvailable(data.networkAvailable);
      card->setPercent(data.networkUsagePercent);
    }
    else if (name == "GPU 0")
    {
      card->setAvailable(data.gpu0Available);
      card->setPercent(data.gpu0UsagePercent);
    }
    else if (name == "GPU 1")
    {
      card->setAvailable(data.gpu1Available);
      card->setPercent(data.gpu1UsagePercent);
    }
  }
}

void HomePage::showPerformanceDetail(const QString &cardName)
{
  for (auto *card : m_perfCards)
  {
    if (card->cardName() == cardName)
    {
      if (m_perfDetailDialog)
      {
        m_perfDetailDialog->close();
        m_perfDetailDialog->deleteLater();
        m_perfDetailDialog = nullptr;
      }
      m_perfDetailDialog = new PerformanceDetailDialog(
          cardName,
          card->dataPoints(),
          card->currentPercent(),
          card->chartColor(),
          this);
      m_perfDetailDialog->show();
      break;
    }
  }
}

