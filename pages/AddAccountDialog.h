/**
 * @file   AddAccountDialog.h
 * @brief  「添加角色」弹窗：离线 / 微软正版 / 第三方三种方式添加游戏账户
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 以前这三种登录方式铺在经典账户管理页的「添加账户」整页表单里；
 * 经典账户管理页去掉后，表单独立成这个弹窗，由角色页右上角的「添加角色」拉起。
 */
#pragma once

#include <QDialog>
#include <QString>

class QTabWidget;
class QWidget;

class AddAccountDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddAccountDialog(QWidget *parent = nullptr);
    ~AddAccountDialog() override;

signals:
    /** 账户已经写进本地列表（携带账户名），调用方可据此刷新账户 / 角色页 */
    void accountAdded(const QString &accountName);

private:
    void buildUi();
    /** 按当前登录方式表单的实际高度收缩卡片，避免最短的表单留下大片空白 */
    void updateColumnHeight();

    QWidget *m_page = nullptr;
    QWidget *m_column = nullptr;
    QTabWidget *m_tabs = nullptr;
};
