/**
 * @file   FeedbackComposePage.h
 * @brief  提交新反馈页：结构化表单提交问题反馈 / 功能建议
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 从反馈页顶部的「提交新反馈」按钮进入。提交需要登录论坛账号，
 * 问题反馈的字段与 GitHub Issue 模板（.github/ISSUE_TEMPLATE/bug_report.yml）一致：
 * 启动器版本、运行平台、问题描述、复现步骤为必填，系统版本 / 期望 / 实际 / 日志 / 截图选填；
 * 结构化字段由论坛服务端校验并组装成正文。提交成功后回到反馈列表页。
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

class FeedbackComposePage : public QWidget
{
    Q_OBJECT

public:
    explicit FeedbackComposePage(QWidget *parent = nullptr);

    /** 进入页面时刷新登录状态 */
    void open();

signals:
    /** 未登录想提交反馈：请求跳到论坛账户页登录 */
    void loginRequested();
    /** 提交成功：回到反馈列表页 */
    void submitted();

private:
    void buildUi();
    void updateState();
    void applyType();
    void submit();
    void setStatus(const QString &text, bool error = false);

    QWidget *m_loginHost = nullptr;
    QWidget *m_mainHost = nullptr;

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
};
