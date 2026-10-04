/**
 * @file   InstanceFolderTree.h
 * @brief  实例文件夹树组件类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef INSTANCEFOLDERTREE_H
#define INSTANCEFOLDERTREE_H

#include <QWidget>
#include <QFileSystemModel>
#include <QTreeView>
#include <QPushButton>
#include <QVBoxLayout>

class InstanceFolderTree : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceFolderTree(QWidget *parent = nullptr);
    ~InstanceFolderTree();

    void setCurrentFolder(const QString &folderPath);
    QString currentFolder() const;
    void refreshTree();

signals:
    void folderSelected(const QString &folderPath);
    void bindFolderClicked();

private slots:
    void onTreeItemClicked(const QModelIndex &index);
    void onTreeContextMenu(const QPoint &pos);

private:
    void initUI();
    void initFolderTree();
    void initBindButton();

    QFileSystemModel *m_fileSystemModel;
    QTreeView *m_folderTree;
    QPushButton *m_bindButton;
    QVBoxLayout *m_mainLayout;
    QString m_currentFolder;
};

#endif // INSTANCEFOLDERTREE_H
