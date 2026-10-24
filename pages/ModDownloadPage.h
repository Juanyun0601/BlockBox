#ifndef MODDOWNLOADPAGE_H
#define MODDOWNLOADPAGE_H

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
#include "../components/TranslateSwitchCard.h"
#include "../components/CardListTranslator.h"
#include "../utils/PageFilter.h"

class BlurLoadingOverlay;
class MCModAPI;
class SearchQueryResolver;
class FlowLayout;

struct InstanceFilterData {
    QString instanceName;
    QString gameVersion;
    QString loaderType;
};

class ModDownloadPage : public QWidget
{
    Q_OBJECT

public:
    explicit ModDownloadPage(QWidget *parent = nullptr);
    ~ModDownloadPage();

    void loadInstances();
    void refreshCurrentSource();
    void loadInitialMods();
    void applyMCIMSetting();
    void setCurrentInstancePath(const QString &path);
    void setSearchText(const QString &text);
    QString selectedGameVersion() const;
    QString selectedLoader() const;

    /**
     * 子导航搜索框分发：本地过滤已加载卡片（按卡片文本匹配），
     * 不发起网络搜索；新结果落卡（placeCards）后自动重新应用。
     */
    Q_INVOKABLE void applyPageFilter(const QString &query);

    CurseForgeAPI *curseforgeAPI() const { return m_curseforgeAPI; }
    ModrinthAPI *modrinthAPI() const { return m_modrinthAPI; }
    QString mcmodUrlForMod(const QString &lookupKey) const { return m_mcmodUrls.value(lookupKey); }

signals:
    void modClicked(const ModInfo &info);
    void instanceSelectionChanged(const QString &instancePath);
    void modMcmodUrlResolved(const QString &lookupKey, const QString &mcmodUrl);

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
    /** 中文关键词解析完成，按解析出的关键词发起搜索 */
    void onQueryResolved(quint64 requestId, const QString &original,
                         const QString &keyword, const QString &matchedName);

private:
    void initUI();
    void initAPI();
    void performSearch(bool append = false);
    /** 关键词可能已由中文解析成英文，这里才真正请求下载源 */
    void issueSearchRequests(const QString &keyword);
    /** 全部来源模式：两源结果齐后按名称合并同名资源并刷出卡片 */
    void flushAllSourceResults();
    bool eventFilter(QObject *watched, QEvent *event) override;
    void loadCardIcon(QLabel *iconLabel, const QString &iconUrl);
    void clearCards();
    /** 按 m_pageFilterQuery 过滤当前已加载卡片（空查询=恢复显示） */
    void applyLocalCardFilter();
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
    QList<ModInfo> m_cardInfos;  // 当前页卡片数据缓存（视图切换重建用）
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
    SearchQueryResolver *m_queryResolver;
    QLabel *m_searchHint;
    quint64 m_queryToken;          // 中文解析请求序号，用于丢弃过期结果
    QString m_effectiveQuery;      // 实际发给下载源的关键词（中文解析之后）
    int m_currentSource;
    int m_currentPage;
    QString m_currentQuery;
    QList<ModInfo> m_allMods;
    QList<ModInfo> m_bufferedMrResults;  // 全部来源：Modrinth 结果缓冲（待合并）
    QList<ModInfo> m_bufferedCfResults;  // 全部来源：CurseForge 结果缓冲（待合并）
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
    QMap<QString, InstanceFilterData> m_instanceDataMap;
    QString m_currentInstancePath;

    // ── 右下角悬浮翻译卡片（EN 原文 / 中 AI 翻译） ──
    void onListTranslateModeChanged(bool chinese);
    void applyListTranslation();
    void restoreListTranslation();
    void syncTranslateStateWithCards();

    TranslateSwitchCard *m_translateSwitch = nullptr;
    CardListTranslator *m_translator = nullptr;
    bool m_applyingListTranslation = false;   // rebuildCards() 期间不重算翻译状态

    // ── 子导航搜索：本地卡片过滤状态（placeCards 后重新应用） ──
    QString m_pageFilterQuery;
    PageFilter::VisibilityFilter m_pageFilter;
};

#endif
