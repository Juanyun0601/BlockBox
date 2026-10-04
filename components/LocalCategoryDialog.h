/**
 * @file   LocalCategoryDialog.h
 * @brief  本地资源分类管理对话框：新建/重命名/删除分类
 * @author BlockBox Team
 * @date   2026-08-18
 */
#ifndef LOCALCATEGORYDIALOG_H
#define LOCALCATEGORYDIALOG_H

#include <QDialog>

class QListWidget;
class QPushButton;
class LocalCategoryManager;

class LocalCategoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LocalCategoryDialog(LocalCategoryManager *manager, QWidget *parent = nullptr);

private slots:
    void onCreateCategory();
    void onRenameCategory();
    void onDeleteCategory();
    void onSelectionChanged();

private:
    void initUI();
    void reloadList();
    void refreshButtons();

    LocalCategoryManager *m_manager;
    QListWidget *m_list;
    QPushButton *m_renameBtn;
    QPushButton *m_deleteBtn;
};

#endif // LOCALCATEGORYDIALOG_H
