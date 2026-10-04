/**
 * @file   InstanceFolderTree.cpp
 * @brief  实例文件夹树组件实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "InstanceFolderTree.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QMenu>
#include <QStandardPaths>
#include <QUrl>

InstanceFolderTree::InstanceFolderTree(QWidget *parent)
    : QWidget(parent)
    , m_currentFolder(QDir::currentPath())
{
    initUI();
    initFolderTree();
    initBindButton();
}

InstanceFolderTree::~InstanceFolderTree()
{
}

void InstanceFolderTree::setCurrentFolder(const QString &folderPath)
{
    m_currentFolder = folderPath;
    
    // 确保文件夹存在
    QDir dir(folderPath);
    if (dir.exists()) {
        // 将树视图根目录设置为目标文件夹的父目录，确保目标可见
        QString parentPath = dir.absolutePath();
        QDir parentDir(parentPath);
        if (parentDir.cdUp()) {
            QString grandParent = parentDir.absolutePath();
            m_fileSystemModel->setRootPath(grandParent);
            m_folderTree->setRootIndex(m_fileSystemModel->index(grandParent));
        }
        
        // 展开并选中指定文件夹
        QModelIndex index = m_fileSystemModel->index(folderPath);
        m_folderTree->expand(index);
        m_folderTree->setCurrentIndex(index);
        
        // 发出文件夹选中信号
        emit folderSelected(folderPath);
    }
}

QString InstanceFolderTree::currentFolder() const
{
    return m_currentFolder;
}

void InstanceFolderTree::refreshTree()
{
    // 重新展开并选中当前文件夹
    setCurrentFolder(m_currentFolder);
}

void InstanceFolderTree::onTreeItemClicked(const QModelIndex &index)
{
    // 获取选中项的路径
    QString path = m_fileSystemModel->filePath(index);
    
    // 检查是否为目录
    QFileInfo fileInfo(path);
    if (fileInfo.isDir()) {
        m_currentFolder = path;
        emit folderSelected(path);
    }
}

void InstanceFolderTree::onTreeContextMenu(const QPoint &pos)
{
    QModelIndex index = m_folderTree->indexAt(pos);
    if (!index.isValid())
    {
        return;
    }

    QString path = m_fileSystemModel->filePath(index);
    QFileInfo fileInfo(path);
    if (!fileInfo.isDir())
    {
        return;
    }

    QMenu menu(m_folderTree);

    QAction *selectAction = menu.addAction(tr("选择此文件夹"));
    menu.addSeparator();
    QAction *openExplorerAction = menu.addAction(tr("在资源管理器中打开"));
    QAction *copyPathAction = menu.addAction(tr("复制文件夹路径"));
    menu.addSeparator();
    QAction *refreshAction = menu.addAction(tr("刷新"));

    QAction *chosen = menu.exec(m_folderTree->viewport()->mapToGlobal(pos));

    if (chosen == selectAction)
    {
        m_currentFolder = path;
        m_folderTree->setCurrentIndex(index);
        emit folderSelected(path);
    }
    else if (chosen == openExplorerAction)
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    else if (chosen == copyPathAction)
    {
        QApplication::clipboard()->setText(path);
    }
    else if (chosen == refreshAction)
    {
        refreshTree();
    }
}

void InstanceFolderTree::initUI()
{
    // 设置主布局
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(8);
    
    setLayout(m_mainLayout);
}

void InstanceFolderTree::initFolderTree()
{
    // 创建文件系统模型（初始以用户主目录为根，避免索引整个文件系统）
    m_fileSystemModel = new QFileSystemModel(this);
    m_fileSystemModel->setRootPath(QDir::homePath());
    m_fileSystemModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    
    // 创建树视图
    m_folderTree = new QTreeView(this);
    m_folderTree->setModel(m_fileSystemModel);
    m_folderTree->setRootIndex(m_fileSystemModel->index(QDir::homePath()));
    
    // 隐藏不必要的列（只显示名称列）
    m_folderTree->hideColumn(1); // 大小列
    m_folderTree->hideColumn(2); // 类型列
    m_folderTree->hideColumn(3); // 修改时间列
    
    // 设置树视图属性
    m_folderTree->header()->setSectionResizeMode(QHeaderView::Stretch);
    m_folderTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_folderTree->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_folderTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // 连接树视图信号
    connect(m_folderTree, &QTreeView::clicked, this, &InstanceFolderTree::onTreeItemClicked);
    connect(m_folderTree, &QTreeView::customContextMenuRequested, this, &InstanceFolderTree::onTreeContextMenu);
    
    // 添加到布局
    m_mainLayout->addWidget(m_folderTree);
}

void InstanceFolderTree::initBindButton()
{
    // 创建绑定文件夹按钮
    m_bindButton = new QPushButton("绑定文件夹", this);
    m_bindButton->setObjectName("bindFolderBtn");
    
    // 连接按钮信号
    connect(m_bindButton, &QPushButton::clicked, this, &InstanceFolderTree::bindFolderClicked);
    
    // 添加到布局
    m_mainLayout->addWidget(m_bindButton);
}