/**
 * @file   InstanceSelectDialog.h
 * @brief  实例选择弹窗：拖入文件时让用户选择目标 Java 实例
 * @author BlockBox Team
 * @date   2026-08-24
 */
#ifndef INSTANCESELECTDIALOG_H
#define INSTANCESELECTDIALOG_H

#include "../components/AppDialogBase.h"

#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QString>

/**
 * @brief 实例选择弹窗
 *
 * 弹窗中列出所有已安装的 Java 版实例，用户选择后返回实例路径。
 * 默认选中当前顶栏显示的实例。无当前实例时强制用户选择。
 */
class InstanceSelectDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 弹出实例选择对话框
     * @param parent              父窗口
     * @param defaultInstancePath 默认选中的实例路径（当前激活实例）
     * @return 选中的实例路径，用户取消返回空
     */
    static QString selectInstance(QWidget *parent,
                                  const QString &defaultInstancePath = QString());

private:
    explicit InstanceSelectDialog(QWidget *parent, const QString &defaultPath);

    void initUI();
    void loadInstances();

    QListWidget *m_listWidget;
    QPushButton *m_confirmBtn;
    QPushButton *m_cancelBtn;
    QLabel *m_hintLabel;
    QString m_selectedPath;
    QString m_defaultPath;
};

#endif // INSTANCESELECTDIALOG_H
