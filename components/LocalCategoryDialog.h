/**
 * @file   LocalCategoryDialog.h
 * @brief  本地资源分类管理对话框：新建/重命名/删除分类
 * @author BlockBox Team
 * @date   2026-08-18
 */
#ifndef LOCALCATEGORYDIALOG_H
#define LOCALCATEGORYDIALOG_H

#include <QDialog>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
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
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    void initUI();
    void reloadList();
    void refreshButtons();
    /** 按当前选中行刷新各行 selected 属性（setItemWidget 下 QSS 无法自动区分） */
    void updateRowSelectedFlags();
    /** 生成一行「分类名 + 数量徽章」的列表项控件 */
    QWidget *makeCategoryRow(const QString &name, int count, QWidget *parent);

    LocalCategoryManager *m_manager;
    QStackedWidget *m_stack;      // 0=空态提示 1=分类列表
    QListWidget *m_list;
    QLabel *m_emptyLabel;
    QPushButton *m_renameBtn;
    QPushButton *m_deleteBtn;
};

#endif // LOCALCATEGORYDIALOG_H
