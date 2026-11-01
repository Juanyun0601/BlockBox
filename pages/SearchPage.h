#ifndef SEARCHPAGE_H
#define SEARCHPAGE_H

#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QNetworkAccessManager>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "utils/ForumContentService.h"
#include "utils/content/ContentData.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModData.h"
#include "utils/mod/ModrinthAPI.h"

class SearchQueryResolver;

/**
 * @brief 搜索页单条结果：在线资源（CurseForge / Modrinth）或论坛帖子
 *
 * 两种来源共用同一张卡片（模组卡片样式），差异只在图标、类型标签、
 * 右侧统计数字与点击后的去向。
 */
struct SearchResultItem
{
    enum Kind
    {
        OnlineResource,
        ForumThread
    };

    Kind kind = OnlineResource;

    QString title;         ///< 标题（在线资源优先中文名）
    QString summary;       ///< 模组简介 / 帖子命中片段（单行展示）
    QString sourceLabel;   ///< CurseForge / Modrinth / 双源 / 社区
    QString kindLabel;     ///< 模组 / 整合包 / 光影包…（空表示不显示）
    QStringList chips;     ///< 版本 / 加载器 / 板块 / 时间等附加信息

    // ── 在线资源 ──
    ModInfo modInfo;
    ContentType contentType = ContentType::Mod;
    QString iconUrl;
    qint64 downloadCount = -1;

    // ── 论坛帖子 ──
    int threadId = 0;
    int replies = -1;
    int views = -1;
};

/**
 * @brief 全局搜索页（2026-11 改版）
 *
 * 页面上只有一个搜索框：输入关键词后列出在线资源（CurseForge / Modrinth）
 * 与方块盒子论坛的帖子，卡片样式与「下载模组」等页的模组卡片一致。
 * 中文关键词会先解析成英文名再查下载源，论坛则直接用原文检索。
 */
class SearchPage : public QWidget
{
    Q_OBJECT

public:
    explicit SearchPage(QWidget *parent = nullptr);
    ~SearchPage() override;

    /** 进入搜索页时聚焦输入框并全选，方便直接覆盖输入 */
    void setSearchFocus();
    /** 清空关键词与结果，回到未搜索状态 */
    void clearSearch();

signals:
    /** 点开在线资源：跳转资源详情页 */
    void navigateToResourceDetail(const ModInfo &info, ContentType contentType);
    /** 点开论坛帖子：跳转帖子详情页 */
    void navigateToForumThread(int threadId);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void initUI();
    void initAPIs();
    void applyMCIMSetting();
    QWidget *createSearchBar();
    QWidget *createResultsArea();

    // 搜索流程
    void performSearch(const QString &keyword);
    void onQueryResolved(quint64 requestId, const QString &keyword);
    void startOnlineSearch(const QString &query);
    void startForumSearch(const QString &query);
    void onCurseforgeSearchCompleted(const ModSearchResult &result);
    void onModrinthSearchCompleted(const ModSearchResult &result);
    void onCurseforgeSearchFailed();
    void onModrinthSearchFailed();
    void appendMergedOnlineResults();
    void finishSearch();

    // 结果渲染
    void resetResults();
    void clearResultWidgets();
    void appendOnlineResults(const QList<ModInfo> &mods);
    void appendForumResults(const QVector<ForumThreadInfo> &threads);
    QWidget *buildResultCard(const SearchResultItem &item, int index);
    QPixmap makeLetterIcon(const QString &text, const QColor &c1, const QColor &c2, int size) const;
    void loadCardIcon(QLabel *label, const QString &iconUrl);
    void refreshSectionTitles();
    void updateEmptyState();
    void onCardActivated(int index);
    QString highlightKeyword(const QString &text) const;

    // 顶部
    QLineEdit *m_searchInput = nullptr;
    QLabel *m_idleHint = nullptr;

    // 结果区
    QWidget *m_resultsPanel = nullptr;
    QScrollArea *m_resultsScroll = nullptr;
    QWidget *m_resultsContainer = nullptr;
    QVBoxLayout *m_resultsLayout = nullptr;
    QLabel *m_onlineHeader = nullptr;
    QWidget *m_onlineContainer = nullptr;
    QVBoxLayout *m_onlineLayout = nullptr;
    QLabel *m_forumHeader = nullptr;
    QWidget *m_forumContainer = nullptr;
    QVBoxLayout *m_forumLayout = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;

    // 搜索源
    CurseForgeAPI *m_curseforgeAPI = nullptr;
    ModrinthAPI *m_modrinthAPI = nullptr;
    SearchQueryResolver *m_queryResolver = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    QTimer *m_debounceTimer = nullptr;

    // 当前查询
    quint64 m_queryToken = 0;   ///< 每次搜索自增，用于丢弃过期回包
    bool m_searchActive = false;///< 本轮搜索是否还在等回包
    int m_pendingRequests = 0;  ///< 尚未回包的请求数（关键词解析 + 两个下载源 + 论坛）
    QString m_rawKeyword;       ///< 用户输入原文（论坛检索 / 结果高亮）
    QString m_onlineKeyword;    ///< 解析后的英文检索词（下载源用）
    QList<SearchResultItem> m_items;
    QSet<QString> m_seenKeys;   ///< source:id 去重，跨源合并后只显示一张卡片

    // 在线资源
    int m_onlinePending = 0;    ///< 两个下载源还差几个回包（0 表示已收尾）
    bool m_curseforgeFailed = false;
    bool m_modrinthFailed = false;
    QList<ModInfo> m_curseforgeMods;
    QList<ModInfo> m_modrinthMods;
};

#endif // SEARCHPAGE_H
