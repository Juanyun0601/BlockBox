/**
 * @file   FeedbackPage.h
 * @brief  软件内反馈页：提交问题反馈 + 我的反馈列表（含处理状态与管理员回复）
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 从「设置 > 关于」的意见反馈入口进入。提交需要登录论坛账号，
 * 反馈统一存到论坛服务器，管理员在论坛后台处理后，
 * 处理状态和管理员回复会出现在下方的反馈列表里。
 */
#pragma once

#include <QWidget>

#include "components/ForumKit.h"
#include "utils/FeedbackService.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

class FeedbackPage : public QWidget
{
    Q_OBJECT

public:
    explicit FeedbackPage(QWidget *parent = nullptr);

    /** 进入页面时刷新登录状态与反馈列表 */
    void open();

signals:
    /** 未登录想提交反馈：请求跳到论坛账户页登录 */
    void loginRequested();

private:
    void buildUi();
    void updateState();
    void loadPage(int page);
    void renderFeedbacks(const QVector<FeedbackItem> &items);
    void submit();
    void applyTemplate(const QString &tpl);
    void setStatus(const QString &text, bool error = false);

    QWidget *m_loginHost = nullptr;
    QWidget *m_mainHost = nullptr;

    QPlainTextEdit *m_contentEdit = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_submitButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    QVBoxLayout *m_listLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;
    QPushButton *m_refreshButton = nullptr;

    int m_page = 1;
    int m_totalPages = 1;
    int m_loadedUserId = 0; ///< 当前列表已加载的账号 id（登录 / 切换账号时自动重拉）
};
