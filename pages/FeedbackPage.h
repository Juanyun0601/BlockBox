/**
 * @file   FeedbackPage.h
 * @brief  软件内反馈页：反馈列表（仿 GitHub Issues）
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 从左侧导航栏底部的「反馈」入口进入，未登录也能看。
 * 页面只显示反馈列表：状态页签（全部 / 待处理 / 已处理 / 已忽略）+ 分页，
 * 点顶部「提交新反馈」进入提交新反馈页；管理员处理的回复在详情里展开可见。
 */
#pragma once

#include <QWidget>

#include "components/ForumKit.h"
#include "utils/FeedbackService.h"

class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

class FeedbackPage : public QWidget
{
    Q_OBJECT

public:
    explicit FeedbackPage(QWidget *parent = nullptr);

    /** 进入页面时刷新反馈列表 */
    void open();

signals:
    /** 点了「提交新反馈」：请求跳到提交新反馈页 */
    void composeRequested();

private:
    void buildUi();
    void loadPublic(int page);
    void renderPublicTabs();
    void renderPublic(const QVector<PublicFeedbackItem> &items);

    QWidget *m_publicHost = nullptr; ///< 全部反馈（仿 GitHub Issues 列表页，未登录也可见）

    QPushButton *m_composeButton = nullptr;

    QHBoxLayout *m_publicTabsLayout = nullptr;
    QVector<QPushButton *> m_publicTabButtons;
    QVBoxLayout *m_publicListLayout = nullptr;
    ForumKit::ForumPager *m_publicPager = nullptr;
    QString m_publicStatus = QStringLiteral("all");
    FeedbackCounts m_publicCounts;
    int m_publicPage = 1;
    int m_publicTotalPages = 1;
};
