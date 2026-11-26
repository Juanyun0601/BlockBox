/**
 * @file   ForumThreadListPage.h
 * @brief  论坛主题列表页：板块 / 搜索结果 / 标签 三种模式
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 同一个列表 UI 服务三个入口：
 *   openBoard(slug, name) —— 板块下的主题，带排序（最新回复/最新发布/最热）和「发帖」按钮
 *   openSearch(query)     —— 站内搜索结果，带可回车的搜索框
 *   openTag(tag)          —— 按标签筛选全站主题
 * 帖子行点击后发 threadOpenRequested，由主窗口切到帖子详情页。
 *
 * 「服务器导航」板块额外带来源设置：全部源 / 方块盒子源 / Modrinth 源，
 * 论坛帖与 Modrinth 的 Java 服务器项目会合并进同一份在线服务器列表。
 */
#pragma once

#include <QHash>
#include <QWidget>

#include "components/ForumKit.h"
#include "utils/ForumContentService.h"
#include "utils/mod/ModData.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class ModrinthAPI;
class ContentViewSwitch;
class MasonryLayout;
class QWidget;

class ForumThreadListPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumThreadListPage(QWidget *parent = nullptr);

    void openBoard(const QString &slug, const QString &boardName);
    void openSearch(const QString &query);
    void openTag(const QString &tag);

signals:
    /** 点击某个主题行 */
    void threadOpenRequested(int threadId);

    /** 板块模式下点「发帖」，boardSlug 为预选板块 */
    void composeRequested(const QString &boardSlug);

    /** 搜索结果里点击的作者 / 板块等（预留） */
    void boardOpenRequested(const QString &slug, const QString &boardName);

private:
    enum Mode
    {
        BoardMode = 0,
        SearchMode = 1,
        TagMode = 2
    };

    /** 服务器导航板块的来源：0 全部源 / 1 方块盒子源 / 2 Modrinth 源（记住上次选择） */
    enum ServerSource
    {
        SourceAll = 0,
        SourceBlockBox = 1,
        SourceModrinth = 2
    };

    void buildUi();
    void loadPage(int page);
    /** 按当前模式 / 来源拉取方块盒子侧的列表（板块 / 搜索 / 标签） */
    void fetchForumPage(int page);
    /** 拉取 Modrinth 的 Java 服务器项目（仅服务器导航板块） */
    void fetchModrinthPage(int page);
    /** 两个来源都回来（或都失败）后统一渲染 */
    void finishLoad();
    void renderHeader();
    void renderThreads(const QVector<ForumThreadInfo> &threads);
    /** 服务器导航板块：把论坛帖和 Modrinth 服务器合并成一份列表 */
    void renderServerNavList();
    /** 只往列表里追加主题行（清空 / 空状态由调用方负责） */
    void appendThreadRows(const QVector<ForumThreadInfo> &threads);

    // ── 教程 / 资源 / 导航板块：瀑布流封面卡 ──
    /** 当前板块的内容是不是「封面类」内容（这五类帖子会排成瀑布流） */
    bool boardSupportsCoverFlow() const;
    /** 现在是不是瀑布流视图（用户在列表 / 瀑布流之间切换过） */
    bool coverFlowActive() const;
    /** 把瀑布流容器从列表布局里摘掉并清空卡片（切回列表 / 重新渲染前） */
    void detachCoverFlow();
    /** 把主题渲染成瀑布流封面卡（m_flowLayout 里一张张排） */
    void renderCoverFlow(const QVector<ForumThreadInfo> &threads);
    /** 一张封面卡：封面上是作者传的小图或类型渐变字，下面是标题 / 简介 / 作者与计数 */
    QWidget *createThreadCoverCard(const ForumThreadInfo &thread);
    /** 视图切换后按当前数据立即重排，不重新请求 */
    void applyViewMode();
    /** 合并列表里的来源小标题（「方块盒子源 · 本页 12 台」这种） */
    void appendSectionHeader(const QString &title, const QString &note);
    /** Modrinth 服务器行（图标 / 描述 / 版本 / 地址 / 复制与打开按钮） */
    QWidget *createModrinthServerRow(const ModInfo &server);
    void copyServerAddress(const QString &address);
    void setSort(const QString &sort);
    void setStatus(const QString &text, bool error = false);
    void updatePager();

    /** 当前板块是不是「服务器导航」（只有它有来源设置） */
    bool isServerNavBoard() const;
    /** 本轮要不要拉方块盒子源 / Modrinth 源 */
    bool sourceWantsForum() const;
    bool sourceWantsModrinth() const;

    /** 从项目介绍里尽力抠出服务器地址（IP / 域名:端口），抠不到返回空 */
    static QString extractServerAddress(const QString &text);
    /** 页面排序按钮 → Modrinth 搜索排序 */
    static QString modrinthSortFor(const QString &sort);
    /** 大数字缩成「1.2 万」这种，列表里不占地方 */
    static QString formatCount(qint64 count);

    Mode m_mode = BoardMode;
    QString m_boardSlug;
    QString m_boardName;
    QString m_query;
    QString m_tag;
    QString m_sort = QStringLiteral("latest");
    int m_page = 1;
    int m_totalPages = 1;

    // ── 双来源（仅服务器导航板块） ──
    int m_serverSource = SourceAll;
    ModrinthAPI *m_modrinthApi = nullptr;
    QVector<ForumThreadInfo> m_forumThreads;   ///< 方块盒子源本轮结果
    QVector<ModInfo> m_modrinthServers;        ///< Modrinth 源本轮结果
    QHash<QString, QString> m_modrinthAddresses; ///< 项目 id → 从介绍里抠出的服务器地址
    QString m_forumError;
    QString m_modrinthError;
    bool m_forumDone = false;      ///< 本轮方块盒子源是否已经回来
    bool m_modrinthDone = false;   ///< 本轮 Modrinth 源是否已经回来
    int m_forumPages = 1;
    int m_modrinthPages = 1;
    int m_modrinthTotal = 0;
    int m_modrinthRequestGen = -1; ///< Modrinth 请求属于第几轮刷新，旧回包直接丢弃
    int m_fetchGen = 0;            ///< 刷新轮次：来源切换 / 翻页后旧回调作废

    // UI
    QScrollArea *m_scroll = nullptr;
    QFrame *m_headerCard = nullptr;
    QVBoxLayout *m_headerBody = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QComboBox *m_sourceCombo = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    /** 瀑布流容器：按需插进 m_listLayout（服务器导航两来源合并时排在方块盒子源标题下） */
    QWidget *m_flowHost = nullptr;
    MasonryLayout *m_flowLayout = nullptr;
    bool m_flowAttached = false;
    /** 列表 / 瀑布流切换（只有封面类板块显示），偏好记住 */
    ContentViewSwitch *m_viewSwitch = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;
    QPushButton *m_composeButton = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QVector<QPushButton *> m_sortButtons;
};
