#ifndef APPFILEDIALOG_H
#define APPFILEDIALOG_H

#include "AppDialogBase.h"

#include "utils/LocalCategoryManager.h"
#include "utils/mod/ModData.h"

#include <QColor>
#include <QDateTime>
#include <QSet>
#include <QStringList>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class CurseForgeAPI;
class ModrinthAPI;

/**
 * @brief 软件内嵌文件浏览弹窗（重构版，替代原生 QFileDialog）
 *
 * 顶部提供两种浏览模式（系统式 / 实例式）：
 *  - 系统式：快速访问侧边栏 + 面包屑导航 + 搜索 + 类型筛选 +
 *             列表/网格视图 + 文件预览。
 *  - 实例式：按「版本(Java版/基岩版) -> 实例 -> 资源类型 -> 文件列表」
 *             分步引导，直接浏览游戏实例内的模组 / 资源包 / 光影包 / 投影 / 存档 / 数据包等
 *             本地资源；Java 版实例来自 .minecraft/versions，基岩版实例来自
 *             BedrockInstanceManager（多实例数据隔离），并支持对实例与本地资源
 *             手动设置分类（实例分类 + 资源分类筛选）。
 *
 * 静态接口 getOpenFileName / getSaveFileName / getOpenFileNames / getExistingDirectory
 * 与旧版保持一致，默认进入系统式。
 */
class AppFileDialog : public AppDialogBase
{
    Q_OBJECT

public:
    enum Mode {
        OpenFile,
        SaveFile,
        OpenFileNames,
        ExistingDirectory
    };

    enum Option {
        DontResolveSymlinks = 0x00000001,
        ShowDirsOnly = 0x00000002,
        DontUseNativeDialog = 0x00000004
    };

    static QString getOpenFileName(QWidget *parent, const QString &caption,
                                   const QString &dir = QString(), const QString &filter = QString(),
                                   QString *selectedFilter = nullptr, int options = 0);
    static QString getSaveFileName(QWidget *parent, const QString &caption,
                                   const QString &dir = QString(), const QString &filter = QString(),
                                   QString *selectedFilter = nullptr, int options = 0);
    static QStringList getOpenFileNames(QWidget *parent, const QString &caption,
                                        const QString &dir = QString(), const QString &filter = QString(),
                                        QString *selectedFilter = nullptr, int options = 0);
    static QString getExistingDirectory(QWidget *parent, const QString &caption,
                                        const QString &dir = QString(), int options = 0);

    /**
     * @brief 选中的网络资源（资源模式）
     */
    struct SelectedResource {
        QString edition;      // "java" / "bedrock"
        QString platform;     // "curseforge" / "modrinth"
        QString resourceType; // 模组 / 资源包 / ...
        QString name;
        QString author;
        QString version;      // 版本显示名
        QString fileName;     // 下载文件名
        QString downloadUrl;  // 实际下载地址
        qint64 fileSize = 0;
        QStringList gameVersions;
        QStringList loaders;
        QString pageUrl;

        bool valid() const { return !downloadUrl.isEmpty(); }
    };

    /**
     * @brief 从 CurseForge / Modrinth 选择资源（Java 版 / 基岩版），返回所选版本下载地址
     * @param out 非空时回填所选资源完整信息
     */
    static QString getResource(QWidget *parent, const QString &caption,
                               SelectedResource *out = nullptr);
    SelectedResource selectedResource() const { return m_resSelected; }

    /**
     * @brief 直接以指定模式构造文件资源管理窗口
     * @param mode OpenFile / SaveFile / OpenFileNames / ExistingDirectory
     */
    explicit AppFileDialog(QWidget *parent, Mode mode);

    /** @brief 设置初始浏览目录（空值/无效路径时忽略） */
    void setInitialDirectory(const QString &path);

    /** @brief 返回确认后的结果字符串（本地文件路径或网络下载 URL） */
    QString resultPath() const { return m_result; }

private:
    // 资源类型定义（实例式）
    struct ResourceType {
        QString id;
        QString name;
        QString subDir;
        QStringList filters;
        bool dirsOnly = false;
    };

    // 实例信息（实例式）
    struct InstItem {
        QString name;
        QString path;          // 可浏览的根目录（Java: 版本目录；基岩版: dataDir/com.mojang）
        QString catPath;       // 实例分类的持久化键（Java: 版本目录；基岩版: dataDir）
        QString version;
        QString loader;
        QString lastPlayed;
    };

    // 文件列表条目
    struct Entry {
        QString name;
        QString absPath;
        bool isDir = false;
        bool isDrive = false;
        QString ext;
        qint64 size = -1;
        QDateTime mod;
        QString cat;
    };

    enum class BrowseMode { System, Instance, Network, Resource };
    enum class InstLevel { Edition, Instance, Type, Files };
    enum class ResLevel { Edition, Type, Search, Version };

protected:
    void keyPressEvent(QKeyEvent *event) override;

    void initUI();
    void initStyle();

    // 分区构建
    QWidget *buildHeader();
    QWidget *buildToolbar();
    QWidget *buildFilterBar();
    QWidget *buildInstCategoryBar();
    QFrame *buildStepperBar();
    QWidget *buildBrowserPage();
    QFrame *buildPreviewPanel();
    QWidget *buildFooter();
    QWidget *buildInstPages();
    QWidget *buildEditionPage();
    QWidget *buildInstancePage();
    QWidget *buildTypePage();
    QWidget *buildNetPage();
    QWidget *buildResPages();
    QWidget *buildResEditionPage();
    QWidget *buildResTypePage();
    QWidget *buildResSearchPage();
    QWidget *buildResVersionPage();

    // 系统式导航
    void loadQuickAccess();
    void goToComputerRoot();
    bool goTo(const QString &path);
    void goUpDir();
    void rebuildBreadcrumbs();
    void populate();
    void sortEntries(QList<Entry> &entries);
    bool entryVisible(const Entry &e) const;
    void pushHistory(const QString &loc);
    void goBack();
    void goForward();
    void updateNavButtons();

    // 地址栏输入
    void enterAddrEditing();
    void exitAddrEditing();
    void addrNavigate();

    // 右键菜单
    void showContextMenu(const QPoint &globalPos, const QString &path);

    // 过滤器
    void parseFilter(const QString &filter);
    void applyFilter(const QString &filter);
    QString currentFilterName() const;

    // 实例式
    void loadInstances();
    void enterSystemMode();
    void enterInstanceMode();
    void enterNetworkMode();
    void updateNetStatus();
    void chooseEdition(bool isJava);
    void setInstLevel(InstLevel lv);
    void renderStepper();
    void renderInstPages();
    void refreshTypeList();
    void openResType(const ResourceType &rt);
    /** 重建当前版本的实例卡片列表（Java / 基岩版共用） */
    void rebuildInstanceList();
    /** 重建实例式文件列表的「分类」筛选条（含管理分类入口） */
    void rebuildInstanceChips();
    /** 将本地资源分类管理器绑定到当前实例 + 资源类型 */
    void refreshInstCategoryManager();
    /** 实例列表项右键「移动分类」子菜单 */
    void addInstanceCategoryMenu(QMenu *menu, const QString &catPath);
    /** 文件列表项右键「设置分类」子菜单 */
    void addFileCategoryMenu(QMenu *menu, const QString &fileName);
    int countFilesIn(const QString &base, const ResourceType &rt) const;
    static bool matchesFilterList(const QString &name, const QStringList &filters);
    static QString resolveResourceDir(const QString &instancePath, const QString &subDir);
    static QString bedrockComMojangDir();

    // 资源模式（在线 CurseForge / Modrinth）
    void enterResourceMode();
    void setResLevel(ResLevel lv);
    void renderResStepper();
    void renderResPages();
    void setupResApis();
    void applyMcimToResApis();
    void resChooseEditionPlatform(const QString &edition, const QString &platform);
    void populateResTypes();
    void performResSearch(int page);
    void onResSearchCompleted(const ModSearchResult &result);
    void onResSearchFailed(const QString &error);
    void onResDetailReceived(const ModInfo &detail);
    void onResDetailFailed(const QString &error);
    void openResVersions(int idx);
    void populateResVersionList();
    void buildResSelected();

    // 通用
    QStringList activePatterns() const;
    bool showOnlyDirs() const;
    void onSelectionChanged();
    void updatePreview();
    void updateSelectionBadge();
    void updateFooterState();
    void updateModeButtons();
    void applySelectionToVisibleWidget();
    QStringList selectedPaths() const;
    QString selectedFilePath() const;
    QString pathEditText() const;
    void acceptResult();
    QString currentLocationPath() const;
    static QString fileTypeText(const Entry &e);
    static QString fileCategory(const QString &ext, bool isDir);
    QColor categoryColor(const QString &cat) const;
    QString categoryIconLetter(const Entry &e) const;
    static QPixmap makeTileIcon(const QString &letter, const QColor &color, int size);

    // 状态
    Mode m_mode;
    int m_options = 0;
    BrowseMode m_browseMode = BrowseMode::System;
    InstLevel m_instLevel = InstLevel::Edition;

    QString m_currentDir;      // 空字符串表示“此电脑”（驱动器列表）
    QStringList m_history;
    int m_historyIdx = -1;
    QStringList m_filterNames;
    QList<QStringList> m_filterPatterns;
    QString m_searchText;
    QString m_activeCat = QStringLiteral("all");
    QString m_sortKey = QStringLiteral("name");
    bool m_sortAsc = true;
    QSet<QString> m_selectedPaths;

    QList<InstItem> m_instances;
    QList<ResourceType> m_resTypes;
    bool m_editionIsJava = true;
    bool m_editionChosen = false;
    QString m_instPath;
    QString m_instName;
    ResourceType m_resType;
    bool m_typeChosen = false;
    QStringList m_scopeFilters;
    bool m_scopeDirsOnly = false;

    // 手动分类（实例分类 + 本地资源分类）
    LocalCategoryManager *m_catManager = nullptr;   // 资源手动分类（按实例/资源类型）
    QString m_instCategoryPath;                     // 当前实例分类持久化根目录
    QString m_activeInstCategoryId;                 // 文件列表「分类」筛选（空=全部）
    QWidget *m_instCatBar = nullptr;                // 实例式文件列表分类筛选条
    QHBoxLayout *m_instCatLayout = nullptr;
    QLabel *m_instCountLabel = nullptr;
    QList<QPushButton *> m_instChips;

    // 控件
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QPushButton *m_systemBtn = nullptr;
    QPushButton *m_instanceBtn = nullptr;
    QPushButton *m_netBtn = nullptr;
    QPushButton *m_resBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;

    QToolButton *m_backBtn = nullptr;
    QToolButton *m_forwardBtn = nullptr;
    QToolButton *m_upBtn = nullptr;
    QFrame *m_crumbBar = nullptr;
    QHBoxLayout *m_crumbLayout = nullptr;
    QToolButton *m_addrBtn = nullptr;
    QLineEdit *m_addrEdit = nullptr;
    bool m_addrEditing = false;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QToolButton *m_listViewBtn = nullptr;
    QToolButton *m_gridViewBtn = nullptr;
    QToolButton *m_previewBtn = nullptr;

    QWidget *m_chipBar = nullptr;
    QHBoxLayout *m_chipLayout = nullptr;
    QLabel *m_countLabel = nullptr;

    QFrame *m_stepperBar = nullptr;
    QHBoxLayout *m_stepperLayout = nullptr;
    QFrame *m_resStepper = nullptr;
    QHBoxLayout *m_resStepperLayout = nullptr;

    QStackedWidget *m_bodyStack = nullptr;
    QWidget *m_browserPage = nullptr;
    QListWidget *m_sidebar = nullptr;
    QTreeWidget *m_tree = nullptr;
    QListWidget *m_grid = nullptr;
    QFrame *m_previewPanel = nullptr;
    QLabel *m_pvIcon = nullptr;
    QLabel *m_pvName = nullptr;
    QLabel *m_pvType = nullptr;
    QLabel *m_pvRows = nullptr;
    QLabel *m_pvEmpty = nullptr;

    QStackedWidget *m_instStack = nullptr;
    QWidget *m_editionPage = nullptr;
    QWidget *m_instancePage = nullptr;
    QWidget *m_typePage = nullptr;
    QListWidget *m_instanceList = nullptr;
    QListWidget *m_typeList = nullptr;

    // 网络链接模式
    QWidget *m_netPage = nullptr;
    QLineEdit *m_netEdit = nullptr;
    QLabel *m_netStatus = nullptr;

    // 资源模式（在线 CurseForge / Modrinth）
    QStackedWidget *m_resStack = nullptr;
    QWidget *m_resEditionPage = nullptr;
    QWidget *m_resTypePage = nullptr;
    QWidget *m_resSearchPage = nullptr;
    QWidget *m_resVersionPage = nullptr;
    QListWidget *m_resEditionList = nullptr;
    QListWidget *m_resTypeList = nullptr;
    QLineEdit *m_resSearchEdit = nullptr;
    QPushButton *m_resSearchBtn = nullptr;
    QPushButton *m_resPrevBtn = nullptr;
    QPushButton *m_resNextBtn = nullptr;
    QLabel *m_resPageLabel = nullptr;
    QLabel *m_resStatusLabel = nullptr;
    QListWidget *m_resResultList = nullptr;
    QLabel *m_resVersionHeader = nullptr;
    QListWidget *m_resVersionList = nullptr;

    ResLevel m_resLevel = ResLevel::Edition;
    bool m_resEditionChosen = false;
    bool m_resEditionIsJava = true;
    QString m_resPlatform;    // "curseforge" / "modrinth"
    QString m_resTypeCf;      // 当前资源类型的 CurseForge classId
    QString m_resTypeMr;      // 当前资源类型的 Modrinth project_type
    QString m_resTypeLabel;   // 显示名（模组 / 资源包…）
    QString m_resQuery;
    int m_resPage = 0;
    bool m_resLoading = false;
    QList<ModInfo> m_resResults;
    ModInfo m_resInfo;
    ModVersionFile m_resVersion;
    SelectedResource m_resSelected;
    CurseForgeAPI *m_cfApi = nullptr;
    ModrinthAPI *m_mrApi = nullptr;

    QLabel *m_selBadge = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_fileNameLabel = nullptr;
    QLineEdit *m_fileNameEdit = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_okBtn = nullptr;

    QString m_result;
    QStringList m_resultList;
};

#endif // APPFILEDIALOG_H
