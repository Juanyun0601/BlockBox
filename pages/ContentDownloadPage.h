#ifndef CONTENTDOWNLOADPAGE_H
#define CONTENTDOWNLOADPAGE_H

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QMap>
#include "../utils/mod/CurseForgeAPI.h"
#include "../utils/mod/ModrinthAPI.h"
#include "../components/ContentViewSwitch.h"
#include "../components/MasonryContentCard.h"
#include "../utils/content/ContentData.h"

class BlurLoadingOverlay;
class MCModAPI;
class FlowLayout;

struct ContentInstanceFilterData
{
    QString instanceName;
    QString gameVersion;
    QString loaderType;
};

class ContentDownloadPage : public QWidget
{
    Q_OBJECT

public:
    explicit ContentDownloadPage(ContentType contentType, QWidget *parent = nullptr);
    ~ContentDownloadPage();

    void loadInstances();
    void refreshCurrentSource();
    void loadInitialMods();
    void applyMCIMSetting();
    void setCurrentInstancePath(const QString &path);
    /** 设置搜索框文本并自动触发搜索（用于从本地搜索无结果跳转） */
    void setSearchText(const QString &text);
    QString selectedGameVersion() const;
    QString selectedLoader() const;

    CurseForgeAPI *curseforgeAPI() const { return m_curseforgeAPI; }
    ModrinthAPI *modrinthAPI() const { return m_modrinthAPI; }
    QString mcmodUrlForMod(const QString &lookupKey) const { return m_mcmodUrls.value(lookupKey); }

signals:
    void contentClicked(const ModInfo &info);
    void downloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile);
    void instanceSelectionChanged(const QString &instancePath);
    void modMcmodUrlResolved(const QString &lookupKey, const QString &mcmodUrl);
    /**
     * @brief 用户点击卡片上的下载图标，请求一键下载最新版本及必备前置
     * @param info 资源基本信息（可能尚未包含 versionFiles，由接收方按需拉取）
     */
    void cardDownloadRequested(const ModInfo &info);

private slots:
    void onSearchTriggered();
    void onSourceChanged(int index);
    void onInstanceChanged(int index);
    void onVersionFilterChanged(int index);
    void onLoaderFilterChanged(int index);
    void onSortChanged(int index);
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    void onFirstPage();
    void onPrevPage();
    void onNextPage();
    void onLastPage();
    void onSearchCompleted(const ModSearchResult &result);
    void onSearchFailed(const QString &error);
    void onCategoriesLoaded(const QList<ModCategory> &categories);
    void onChineseNameResolved(const QString &modId, const QString &chineseName, const QString &mcmodUrl);
    void onCardContextMenu(const QPoint &pos);

private:
    void initUI();
    void initAPI();
    void performSearch(bool append = false);
    bool eventFilter(QObject *watched, QEvent *event) override;
    void loadCardIcon(QLabel *iconLabel, const QString &iconUrl);
    void clearCards();
    void addCard(const ModInfo &info);
    void addListCard(const ModInfo &info);
    void addMasonryCard(const ModInfo &info);
    QList<MasonryContentCard::ActionSpec> buildMasonryActions(const ModInfo &info);
    void placeCards();
    void rebuildCards();
    void updatePaginationControls();
    int columnCount() const;
    void showLoading(bool show);
    void setupConnections();
    void populateSortCombo(int source);
    void populateVersionLoaderFilter();
    void setFilterCombosVisible(bool visible);

    static constexpr const char *MCIM_BASE = "https://mod.mcimirror.top";
    static constexpr const char *CF_BASE = "https://api.curseforge.com";
    static constexpr const char *MODRINTH_BASE = "https://api.modrinth.com";

    enum SourceType { CurseForge = 0, Modrinth = 1, AllSources = 2 };
    enum InstanceComboType { InstanceAny = -1, InstanceCustom = -2 };

    QLineEdit *m_searchEdit;
    QComboBox *m_sourceCombo;
    QComboBox *m_instanceCombo;
    QComboBox *m_sortCombo;
    QComboBox *m_versionFilterCombo;
    QComboBox *m_loaderFilterCombo;
    QLabel *m_versionFilterLabel;
    QLabel *m_loaderFilterLabel;

    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    FlowLayout *m_cardFlowLayout;
    ContentViewSwitch *m_viewSwitch;
    int m_viewMode;              // 0=列表式, 1=瀑布流
    QList<ModInfo> m_cardInfos;  // 当前页卡片数据缓存
    BlurLoadingOverlay *m_loadingOverlay;
    QWidget *m_paginationBar;
    QPushButton *m_firstPageBtn;
    QPushButton *m_prevPageBtn;
    QLabel *m_pageLabel;
    QPushButton *m_nextPageBtn;
    QPushButton *m_lastPageBtn;

    CurseForgeAPI *m_curseforgeAPI;
    ModrinthAPI *m_modrinthAPI;
    MCModAPI *m_mcmodAPI;
    int m_currentSource;
    int m_currentPage;
    QString m_currentQuery;
    QList<ModInfo> m_allMods;
    QList<QWidget*> m_cardWidgets;
    bool m_hasMore;
    bool m_isLoading;
    int m_pendingCount;
    int m_totalHits;

    QList<ModCategory> m_categories;
    QMap<int, QList<ModCategory>> m_categoriesCache;
    QMap<int, QList<ModInfo>> m_pageCache;
    QMap<QString, QWidget*> m_chineseNameLookups;
    QMap<QString, QString> m_mcmodUrls;
    QMap<QString, ContentInstanceFilterData> m_instanceDataMap;
    QString m_currentInstancePath;

    ContentType m_contentType;
    ContentTypeConfig m_config;
};

#endif