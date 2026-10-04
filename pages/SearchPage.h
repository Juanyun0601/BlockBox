#ifndef SEARCHPAGE_H
#define SEARCHPAGE_H

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include <QList>
#include <QGridLayout>
#include <QNetworkAccessManager>
#include <QTimer>

#include "utils/content/ContentData.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModrinthAPI.h"

struct SearchResultItem {
    QString title;
    QString subtitle;
    QString description;
    QString iconText;

    enum Type { TypeInstance, TypeSettings, TypeResource, TypeContentList, TypeAiChat, TypeExternalSearch, TypeOnlineResource, TypeFavorite, TypeTask } type;
    QString instancePath;
    int settingsTabIndex = -1;
    ContentType resourceType = ContentType::Mod;

    // For online resource results
    QString onlineSource;
    QString onlineId;
    QString onlineIconUrl;
    QString onlineAuthor;
    QString onlineVersion;

    // Richer metadata for the card display (prototype-style)
    QString sourceLabel;        // "CurseForge" / "Modrinth" / "本地" / "收藏夹" / "任务" / "设置"
    QString mcVersion;          // 游戏版本，如 1.20.1
    QString loader;             // 加载器，如 Fabric
    QString updatedText;        // 更新时间展示文本
    qint64 downloadCount = -1;  // 下载量（-1 表示不展示）
    double rating = -1.0;       // 评分（-1 表示不展示）
    qint64 followers = -1;      // 关注数（-1 表示不展示）
    QStringList tags;           // 展示用标签

    // For favorite results
    QString favoriteItemId;
    QString favoriteFolderId;

    // For task results
    QString taskId;
};

class SearchPage : public QWidget
{
    Q_OBJECT

public:
    explicit SearchPage(QWidget *parent = nullptr);
    ~SearchPage();

    void setSearchFocus();
    void clearSearch();
    void setFilter(int index);

signals:
    void navigateToInstance(const QString &instancePath);
    void navigateToSettings(int tabIndex);
    void navigateToResource(ContentType type);
    void navigateToAiChat();
    void navigateToResourceDetail(const ModInfo &info, ContentType contentType);
    void navigateToFavorites();
    void navigateToTaskList();
    void navigateToInstallInstance();
    void navigateToModpackImport();

private:
    void initUI();
    void initAPIs();
    void applyMCIMSetting();
    QWidget* createSearchBar();
    QWidget* createHotSearchBar();
    QWidget* createFilterBar();
    QWidget* createResultsArea();
    QWidget* createResultsToolbar();
    void onFilterChanged(int index);
    void onSubFilterChanged(int index);
    void performSearch(const QString &keyword);
    QList<SearchResultItem> searchAll(const QString &keyword);
    QList<SearchResultItem> searchInstances(const QString &keyword);
    QList<SearchResultItem> searchSettings(const QString &keyword);
    QList<SearchResultItem> searchResourceFiles(const QString &keyword, ContentType type);
    QList<SearchResultItem> searchAi(const QString &keyword);
    QList<SearchResultItem> searchExternal(const QString &keyword);
    QList<SearchResultItem> searchFavorites(const QString &keyword);
    QList<SearchResultItem> searchTasks(const QString &keyword);
    QStringList detectInstanceLoaders(const QString &instancePath) const;
    void sortResults(QList<SearchResultItem> &results);
    void showResults(const QList<SearchResultItem> &results);
    void appendResultWidgets(const QList<SearchResultItem> &results, int baseIndex, int insertPos = -1, bool addOnlineSep = true);
    void appendOnlineResults(const QList<SearchResultItem> &newResults, const QString &sourceName);
    void clearResults();
    void onResultClicked(int index);
    bool eventFilter(QObject *obj, QEvent *event) override;
    void openExternalUrl(const QString &url);
    void retranslatePlaceholder();
    QString typeBadgeText(const SearchResultItem &item) const;
    QString sourceLabelFor(const SearchResultItem &item) const;
    QWidget* buildResultCard(const SearchResultItem &item, int index, const QColor &themeColor);

    QLineEdit *m_searchInput;
    QComboBox *m_filterCombo;
    QComboBox *m_subFilterCombo;

    QWidget *m_hotSearchBar;
    QComboBox *m_sortCombo;

    QScrollArea *m_resultsScroll;
    QWidget *m_resultsContainer;
    QVBoxLayout *m_resultsLayout;
    QLabel *m_resultsTitle;
    QLabel *m_noResultsLabel;

    // Online resource search
    CurseForgeAPI *m_curseforgeAPI;
    ModrinthAPI *m_modrinthAPI;
    QNetworkAccessManager *m_networkManager;
    QString m_pendingOnlineKeyword;
    ContentType m_pendingOnlineType;
    int m_onlineSearchPendingCount;
    QTimer *m_onlineSearchTimer;

    QList<SearchResultItem> m_currentResults;
    int m_currentFilter;
    int m_currentSubFilter;
    QString m_currentKeyword;

    static constexpr const char *MCIM_BASE = "https://mod.mcimirror.top";
    static constexpr const char *CF_BASE = "https://api.curseforge.com";
    static constexpr const char *MODRINTH_BASE = "https://api.modrinth.com";
    static const char *const kHotSearches[];
};

#endif // SEARCHPAGE_H
