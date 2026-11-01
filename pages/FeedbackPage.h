/**
 * @file   FeedbackPage.h
 * @brief  软件内反馈页：结构化表单提交问题反馈 / 功能建议 + 我的反馈列表
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 从「设置 > 关于」的意见反馈入口进入。提交需要登录论坛账号，
 * 问题反馈的字段与 GitHub Issue 模板（.github/ISSUE_TEMPLATE/bug_report.yml）一致：
 * 启动器版本、运行平台、问题描述、复现步骤为必填，系统版本 / 期望 / 实际 / 日志 / 截图选填；
 * 结构化字段由论坛服务端校验并组装成正文；管理员在论坛后台处理后，
 * 处理状态和管理员回复会出现在下方的反馈列表里。
 */
#pragma once

#include <QWidget>

#include "components/ForumKit.h"
#include "utils/FeedbackService.h"

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
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
    void applyType();
    void loadPage(int page);
    void renderFeedbacks(const QVector<FeedbackItem> &items);
    void loadPublic(int page);
    void renderPublicTabs();
    void renderPublic(const QVector<PublicFeedbackItem> &items);
    void submit();
    void setStatus(const QString &text, bool error = false);

    QWidget *m_loginHost = nullptr;
    QWidget *m_mainHost = nullptr;
    QWidget *m_publicHost = nullptr; ///< 全部反馈（仿 GitHub Issues 列表页，未登录也可见）

    QComboBox *m_typeCombo = nullptr;
    QWidget *m_appVersionField = nullptr;
    QWidget *m_platformField = nullptr;
    QWidget *m_osVersionField = nullptr;
    QWidget *m_stepsField = nullptr;
    QWidget *m_expectedField = nullptr;
    QWidget *m_actualField = nullptr;
    QWidget *m_logsField = nullptr;
    QWidget *m_screenshotsField = nullptr;
    QWidget *m_sceneField = nullptr;
    QLabel *m_descTitle = nullptr;
    QLineEdit *m_appVersionEdit = nullptr;
    QComboBox *m_platformCombo = nullptr;
    QLineEdit *m_osVersionEdit = nullptr;
    QPlainTextEdit *m_descEdit = nullptr;
    QPlainTextEdit *m_stepsEdit = nullptr;
    QPlainTextEdit *m_expectedEdit = nullptr;
    QPlainTextEdit *m_actualEdit = nullptr;
    QPlainTextEdit *m_logsEdit = nullptr;
    QPlainTextEdit *m_screenshotsEdit = nullptr;
    QPlainTextEdit *m_sceneEdit = nullptr;

    QPushButton *m_submitButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    QVBoxLayout *m_listLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;
    QPushButton *m_refreshButton = nullptr;

    int m_page = 1;
    int m_totalPages = 1;
    int m_loadedUserId = 0; ///< 当前列表已加载的账号 id（登录 / 切换账号时自动重拉）

    QHBoxLayout *m_publicTabsLayout = nullptr;
    QVector<QPushButton *> m_publicTabButtons;
    QVBoxLayout *m_publicListLayout = nullptr;
    ForumKit::ForumPager *m_publicPager = nullptr;
    QString m_publicStatus = QStringLiteral("all");
    FeedbackCounts m_publicCounts;
    int m_publicPage = 1;
    int m_publicTotalPages = 1;
};
