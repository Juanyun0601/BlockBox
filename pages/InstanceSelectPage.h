/**
 * @file   InstanceSelectPage.h
 * @brief  实例选择页面类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef INSTANCESELECTPAGE_H
#define INSTANCESELECTPAGE_H

#include <QButtonGroup>
#include <QCache>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include "../components/ContentViewSwitch.h"
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMap>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWidget>
class FlowLayout;
class QStackedWidget;
#include <QtConcurrent/QtConcurrentRun>

class GameLauncher;

// 实例信息结构体，用于缓存实例的详细信息
struct InstanceInfo {
    QString loaderType;
    QString versionType;
    QString versionNumber;
    QString displayName;
    QString iconPath;
    QDateTime lastModified;
    qint64 sizeBytes = 0;
    QDateTime lastPlayed;
};

class InstanceSelectPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceSelectPage(QWidget *parent = nullptr);
    ~InstanceSelectPage();

    void initUI();
    void loadInstances();
    void updateInstanceList();
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

signals:
    void backToMainRequested();
    void instanceSelected(const QString &instancePath);
    void launchGameRequested(const QString &instancePath);
    void installNewInstanceRequested();
    void downloadModpackRequested();
    void importModpackRequested();
    void exportModpackRequested(const QString& instancePath);

public slots:
    void onInstanceItemClicked(QListWidgetItem *item);
    void onInstanceContextMenu(const QPoint &pos);
    void onBindFolderClicked();
    void onInstallNewInstanceClicked();
    void onDownloadModpackClicked();
    void onImportModpackClicked();
    void onAddCategoryClicked();
    void onRenameCategory(const QString &category);
    void onDeleteCategory(const QString &category);
    
    // Launcher related slots
    void onLaunchStatusChanged(int status);
    void onGameStarted();
    void onGameStopped(int exitCode);
    void onGameCrashed(const QString &error);
    void onLaunchProgressChanged(int progress, const QString &message);
    void onLaunchGameClicked(const QString &instancePath);
    void onDeleteInstanceClicked(const QString &instancePath);
    void onAsyncSizesCalculated();

private:
    void initLeftSidebar();
    void initRightMainBar();
    void initInstanceFolders();
    void initLauncher();
    void addInstanceCard(const QFileInfo &instanceInfo, QListWidget *listWidget);
    void addMasonryInstanceCard(const QFileInfo &instanceInfo);
    void rebuildMasonryCards();
    QWidget *buildMasonryInstanceCard(const QFileInfo &instanceInfo, QWidget *parent);
    void rebuildInstanceCards();
    InstanceInfo getInstanceInfo(const QFileInfo &instanceInfo);
    static QMap<QString, qint64> calculateSizesStatic(const QStringList &instancePaths);
    void startAsyncSizeCalculation();

    // 左侧导航栏：统一选中逻辑（含图标颜色切换 + 滑动高亮）
    void setSelectedFolder(QPushButton *target);
    void slideHighlightTo(QPushButton *target);
    // 统一创建"实例文件夹"导航按钮（初始化与绑定文件夹共用）
    QPushButton *createFolderNavButton(const QString &name, const QString &path, bool checked);
    // 从文件夹路径推导侧边栏显示名（.minecraft 用其父目录名）
    static QString folderDisplayName(const QString &path);
    void animateFolderEntrance();
    QIcon loadColoredIcon(const QString &path, const QColor &color) const;

    // 左侧导航栏分类区
    void initCategorySection();
    void rebuildCategoryButtons();
    void setSelectedCategory(QPushButton *target);
    void slideCategoryHighlightTo(QPushButton *target);
    void buildCategoryMenu(QMenu *menu, const QString &instancePath);
    bool matchesCategory(const QString &instancePath) const;

    // 左侧导航栏悬浮气泡：重新计算几何（页面尺寸变化时调用）
    void updateLeftBubbleGeometry();

    // 左侧导航悬浮气泡参数
    static constexpr int kBubbleWidth = 200;   // 气泡固定宽度
    static constexpr int kBubbleMargin = 10;   // 气泡与页面边缘间距

    // UI components
    QHBoxLayout *m_mainLayout;

    // Left sidebar
    QVBoxLayout *m_leftLayout;
    QWidget *m_leftWidget;
    QWidget *m_folderHighlight;
    QColor m_iconNormalColor;
    QColor m_iconSelectedColor;

    QList<QPushButton *> m_instanceFolderButtons;
    QPushButton *m_bindFolderBtn;

    // 分类区
    QWidget *m_folderButtonsWidget;
    QVBoxLayout *m_folderButtonsLayout;
    QWidget *m_categoryButtonsWidget;
    QVBoxLayout *m_categoryButtonsLayout;
    QPushButton *m_allCategoryBtn;
    QPushButton *m_addCategoryBtn;
    QWidget *m_categoryHighlight;
    QList<QPushButton *> m_categoryButtons;
    QString m_currentCategory;

    // Right main bar
    QVBoxLayout *m_rightLayout;
    
    // Right top section
    QHBoxLayout *m_topLayout;
    QPushButton *m_installNewInstanceBtn;
    QPushButton *m_downloadModpackBtn;
    QPushButton *m_importModpackBtn;

    // Right bottom section
    QScrollArea *m_instanceScrollArea;
    QWidget *m_instanceContainer;
    QListWidget *m_instanceListWidget;
    QWidget *m_masonryContainer;
    FlowLayout *m_masonryLayout;
    QStackedWidget *m_viewStack;
    ContentViewSwitch *m_viewSwitch;
    int m_viewMode;   // 0=列表式, 1=瀑布流
    
    // Data
    QString m_currentFolderPath;
    bool m_instancesLoaded = false;
    QString m_currentAccountName = "Player";
    QList<QFileInfo> m_instances;
    GameLauncher *m_gameLauncher;
    
    // 缓存
    QCache<QString, InstanceInfo> m_instanceInfoCache; // 缓存实例信息
    QMap<QString, QList<QFileInfo>> m_folderInstancesMap; // 缓存文件夹对应的实例列表
    QMap<QString, QDateTime> m_folderLastModifiedMap; // 缓存文件夹的最后修改时间

    // 异步大小计算相关
    QMap<QString, QLabel*> m_instanceSizeLabels; // 实例路径 -> 大小标签, 用于异步更新
    QFutureWatcher<QMap<QString, qint64>> m_sizeWatcher;
};

#endif // INSTANCESELECTPAGE_H