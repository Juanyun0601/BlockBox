/**
 * @file   HomeHotThreadsPanel.h
 * @brief  首页论坛热门帖栏：好友栏下面的一排热门帖子卡片
 * @author BlockBox Team
 * @date   2026-11-15
 *
 * 首页好友栏下方的独立一栏：
 *   - 拉论坛全站的热门主题（sort=hot，匿名可看，不要求登录）；
 *   - 卡片显示标题、板块、回复数和最后回复时间，点卡片进帖子详情；
 *   - 卡片按窗口宽度自动分列排布，只摆前几张，其余点「查看全部」去社区页看；
 *   - 加载失败 / 没有帖子都有对应的空态提示。
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class FlowLayout;
class QLabel;
class QPushButton;
class QTimer;

class HomeHotThreadsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HomeHotThreadsPanel(QWidget *parent = nullptr);
    ~HomeHotThreadsPanel() override;

    /** 首页显示 / 隐藏时启停轮询 */
    void setActive(bool active);

    /** 重新拉取热门帖子（切回首页 / 顶栏刷新 / 定时轮询） */
    void refresh();

signals:
    /** 点击帖子卡片：打开该帖子的详情页 */
    void threadOpenRequested(int threadId);
    /** 标题栏「查看全部」：打开社区论坛页 */
    void viewAllRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** 一条热门帖的展示信息（只取列表渲染需要的字段） */
    struct HotThread
    {
        int id = 0;
        QString title;
        QString boardName;
        int replies = 0;
        int views = 0;
        qint64 lastPostAt = 0;
    };

    void buildUi();
    void loadThreads();
    void render();
    /** 清空当前卡片（空态切换 / 重新分列时用） */
    void clearCards();
    /** 空态：一行提示 + 可选按钮，占住卡片流的位置 */
    void showState(const QString &text, const QString &actionText = QString(),
                   const std::function<void()> &onAction = nullptr);
    void hideState();
    /** 卡片流容器宽度变化后延迟一拍重排，合并连续 resize */
    void scheduleRebuild();
    /** 按容器宽度算出每张卡片的宽度（一排 1~6 张） */
    int calculateCardWidth() const;
    QWidget *createThreadCard(const HotThread &info, int cardWidth);
    /** 当前数据指纹：内容没变就不重建列表，避免轮询时闪烁 */
    QString dataSignature() const;

    QLabel *m_titleLabel = nullptr;
    QPushButton *m_openButton = nullptr;
    QWidget *m_listHost = nullptr;       // 帖子卡片流容器
    FlowLayout *m_listLayout = nullptr;
    // 空态行（加载中 / 加载失败 / 论坛还没有帖子）
    QWidget *m_stateBar = nullptr;
    QLabel *m_stateLabel = nullptr;
    QPushButton *m_stateBtn = nullptr;
    std::function<void()> m_stateAction;
    QTimer *m_pollTimer = nullptr;

    QVector<HotThread> m_threads;
    bool m_loading = false;
    bool m_failed = false;
    bool m_loadedOnce = false;
    bool m_active = false;
    bool m_rebuildPending = false;
    QString m_builtSignature;
    int m_builtCardWidth = -1;   // 上次建卡片用的宽度
};
