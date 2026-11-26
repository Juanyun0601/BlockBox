/**
 * @file   ForumSchematicListPage.h
 * @brief  资源页「投影」子页：论坛投影分享板块的投影列表
 * @author BlockBox Team
 * @date   2026-11-20
 *
 * 数据来自论坛「投影分享」（schematic-share）板块的主题列表，
 * 每行就是一个投影帖：名称、文件大小、下载次数、作者等，
 * 卡片外观与「下载模组」「插件」等列表页一致。
 * 点「下载」直接把投影文件存进当前实例的 schematics 目录，
 * 点「看原帖」跳到原帖看介绍与评论。
 */
#pragma once

#include <QVector>
#include <QWidget>

#include "utils/ForumContentService.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;
class QPushButton;

class ForumSchematicListPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumSchematicListPage(QWidget *parent = nullptr);

    /** 打开页面并从第 1 页重新拉取 */
    void openPage();

    /** 当前实例路径：决定投影下载到哪个实例的 schematics 目录 */
    void setInstancePath(const QString &path);

    /** 子导航搜索框分发：同步到页内搜索框（复用本地过滤） */
    Q_INVOKABLE void applyPageFilter(const QString &query);

signals:
    /** 点了某张投影卡：跳到该投影的原帖 */
    void threadOpenRequested(int threadId);

    /** 网络失败需要登录/人机验证时提示跳账户页 */
    void loginRequested();

    /** 「去发布」：跳到发布中心的投影发布页 */
    void composeRequested();

    /** 有投影下载完成（实例的投影列表可据此刷新） */
    void projectionSaved();

    /** 实例还没有投影目录、也没装投影模组：请求自动装好并把目录建出来 */
    void projectionModMissing();

private:
    void buildUi();
    void loadPage(int page);
    void clearList();
    void renderLoaded();
    /** 下载投影并直接存进当前实例的 schematics 目录 */
    void downloadSchematic(int threadId, const QString &displayName, const QString &fileName);
    /** 筛选按钮：按文件格式筛选当前页的投影 */
    void onFilterClicked();
    /** 根据当前生效的筛选刷新按钮文字 / 高亮 */
    void updateFilterButton();
    /** 当前页筛选结果为空时是否仍显示分页（便于翻页继续找） */
    void updatePager();

    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QPushButton *m_filterBtn = nullptr;
    QWidget *m_paginationBar = nullptr;
    QPushButton *m_firstButton = nullptr;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_lastButton = nullptr;
    QLabel *m_pageLabel = nullptr;

    int m_page = 1;
    int m_totalPages = 1;
    QString m_keyword;                    ///< 客户端过滤关键字（本地已拉取的列表里搜）
    QString m_sort = QStringLiteral("new");   ///< 服务端排序：new / latest / hot
    QString m_format;                     ///< 客户端格式筛选（空 = 全部格式）
    QString m_instancePath;               ///< 当前实例路径（下载落点）
    QVector<ForumThreadInfo> m_threads;   ///< 当前页原始数据（只留挂了投影的帖子）
};
