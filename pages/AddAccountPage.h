/**
 * @file   AddAccountPage.h
 * @brief  「添加角色」页：离线 / 微软正版 / 第三方三种方式添加游戏账户
 * @author BlockBox Team
 * @date   2026-11-15
 *
 * 以前这三种登录方式铺在经典账户管理页的「添加账户」整页表单里，
 * 后来收进一个弹窗；现在再回到整页：作为账户管理页的内层页，
 * 由角色页右上角的「添加角色」/ 空态按钮拉起，返回键回到角色页。
 */
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QTabWidget;

class AddAccountPage : public QWidget
{
    Q_OBJECT

public:
    explicit AddAccountPage(QWidget *parent = nullptr);

    /** 进入页面：回到默认登录方式，清掉上一次填过的账号密码 */
    void open();

signals:
    /** 账户已经写进本地列表（携带账户名），调用方可据此刷新账户 / 角色页 */
    void accountAdded(const QString &accountName);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    /** 卡片宽度跟着窗口走：宽屏固定 560，窄屏（手机）收缩到可用宽度 */
    void updateColumnWidth();
    /** 按当前登录方式表单的实际高度收缩卡片，避免最短的表单留下大片空白 */
    void updateColumnHeight();
    /** 清空表单与临时状态（成功 / 失败提示、进度条、服务器连通徽标） */
    void resetForm();

    QWidget *m_page = nullptr;
    QWidget *m_column = nullptr;
    QTabWidget *m_tabs = nullptr;
    QScrollArea *m_scroll = nullptr;

    // 离线
    QLineEdit *m_offlineNameEdit = nullptr;
    // 微软正版
    QPushButton *m_microsoftLoginBtn = nullptr;
    QProgressBar *m_microsoftProgress = nullptr;
    QLabel *m_microsoftStatus = nullptr;
    // 第三方
    QComboBox *m_serverCombo = nullptr;
    QLabel *m_serverPill = nullptr;
    QLineEdit *m_thirdPartyUserEdit = nullptr;
    QLineEdit *m_thirdPartyPasswordEdit = nullptr;
    QPushButton *m_thirdPartyLoginBtn = nullptr;
};
