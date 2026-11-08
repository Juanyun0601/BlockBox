/**
 * @file   ForumReportPage.h
 * @brief  举报楼层页：选择分类 + 补充说明的整页表单
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 从帖子详情页楼层操作行 / 右键菜单的「举报」进入，需要登录论坛账号。
 * 页面展示被举报楼层的信息，先选举报分类（8 类，选「其他」必须补充说明），
 * 提交成功后回到帖子详情页。
 */
#pragma once

#include <QList>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QVBoxLayout;

class ForumReportPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumReportPage(QWidget *parent = nullptr);

    /** 进入页面：带上被举报楼层的信息并刷新登录状态 */
    void open(int postId, int floor, const QString &authorName, const QString &excerpt);

signals:
    /** 未登录想提交举报：请求跳到论坛账户页登录 */
    void loginRequested();
    /** 提交成功：回到帖子详情页 */
    void submitted();

private:
    void buildUi();
    void updateState();
    int checkedCategory() const;
    void submit();
    void setStatus(const QString &text, bool error = false);

    QWidget *m_loginHost = nullptr;
    QWidget *m_mainHost = nullptr;

    QLabel *m_targetLabel = nullptr;
    QLabel *m_excerptLabel = nullptr;
    QList<QRadioButton *> m_categoryButtons;   ///< 与 kCategories 一一对应
    QPlainTextEdit *m_detailEdit = nullptr;

    int m_postId = 0;
    QPushButton *m_submitButton = nullptr;
    QLabel *m_statusLabel = nullptr;
};
