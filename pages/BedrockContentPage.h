#ifndef BEDROCKCONTENTPAGE_H
#define BEDROCKCONTENTPAGE_H

#include <QWidget>
#include <QString>
#include <QMap>
#include <QList>
#include <QVector>

#include "components/ContentViewSwitch.h"
#include "components/MasonryContentCard.h"
#include "utils/mod/ModData.h"

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QWidget;
class CurseForgeAPI;
class BlurLoadingOverlay;
class FlowLayout;

struct ModInfo;

/**
 * @brief 基岩版社区资源浏览页（CurseForge 源）
 *
 * 与 Java 版 ModDownloadPage / ContentDownloadPage 同风格：
 * 搜索栏 + 分类筛选 + 排序 + 视图切换 + 分页 + 卡片列表/瀑布流。
 * 数据来自 CurseForge（游戏 ID 78022 = Minecraft Bedrock Edition）。
 */
class BedrockContentPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockContentPage(QWidget *parent = nullptr);
    ~BedrockContentPage() override;

    /** 设置分类并触发搜索；classId 为 0 表示全部分类 */
    void setCategory(int classId);
    void setSearchText(const QString &text);

    static constexpr const char *MCIM_BASE = "https://mod.mcimirror.top";
    static constexpr const char *CF_BASE   = "https://api.curseforge.com";

signals:
    void contentDetailRequested(const ModInfo &info, int classId);

private slots:
    void onSearchTriggered();
    void onCategoryChanged(int index);
    void onSortChanged(int index);
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    void onFirstPage();
    void onPrevPage();
    void onNextPage();
    void onLastPage();
    void onSearchCompleted(const ModSearchResult &result);
    void onSearchFailed(const QString &error);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    void initAPI();
    void setupConnections();
    void applyMCIMSetting();
    void performSearch(bool append);
    void loadInitialContent();

    void addCard(const ModInfo &info);
    void addListCard(const ModInfo &info);
    void addMasonryCard(const ModInfo &info);
    QList<MasonryContentCard::ActionSpec> buildMasonryActions(const ModInfo &info);
    void clearCards();
    void placeCards();
    void rebuildCards();
    void showFavoriteMenu(const ModInfo &info, QWidget *anchor);
    void loadCardIcon(QLabel *iconLabel, const QString &iconUrl);
    void updatePaginationControls();
    void showLoading(bool show);
    void populateCategoryCombo();

    QString selectedClassId() const;
    QString selectedSortField() const;

    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_categoryCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_cardContainer = nullptr;
    QGridLayout *m_cardGridLayout = nullptr;
    FlowLayout *m_cardFlowLayout = nullptr;
    ContentViewSwitch *m_viewSwitch = nullptr;
    int m_viewMode = 0;
    BlurLoadingOverlay *m_loadingOverlay = nullptr;

    QWidget *m_paginationBar = nullptr;
    QPushButton *m_firstPageBtn = nullptr;
    QPushButton *m_prevPageBtn = nullptr;
    QLabel *m_pageLabel = nullptr;
    QPushButton *m_nextPageBtn = nullptr;
    QPushButton *m_lastPageBtn = nullptr;

    CurseForgeAPI *m_curseforgeAPI = nullptr;

    QList<QWidget*> m_cardWidgets;
    QList<ModInfo> m_cardInfos;
    QList<ModInfo> m_allMods;
    QMap<int, QList<ModInfo>> m_pageCache;

    QString m_currentQuery;
    int m_currentCategory = 0;   // 0 = 全部
    int m_currentPage = 0;
    bool m_hasMore = false;
    bool m_isLoading = false;
    int m_totalHits = 0;
};

#endif // BEDROCKCONTENTPAGE_H
