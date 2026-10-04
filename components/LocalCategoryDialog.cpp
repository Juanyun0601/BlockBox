/**
 * @file   LocalCategoryDialog.cpp
 * @brief  本地资源分类管理对话框实现
 * @author BlockBox Team
 * @date   2026-08-18
 */
#include "LocalCategoryDialog.h"
#include "utils/LocalCategoryManager.h"
#include "components/AppMessageBox.h"
#include "components/FavoriteFolderDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

LocalCategoryDialog::LocalCategoryDialog(LocalCategoryManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_list(nullptr)
    , m_renameBtn(nullptr)
    , m_deleteBtn(nullptr)
{
    initUI();
    reloadList();
    connect(m_manager, &LocalCategoryManager::categoriesChanged,
            this, &LocalCategoryDialog::reloadList);
}

void LocalCategoryDialog::initUI()
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("管理分类"));
    setFixedSize(400, 380);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(14);

    QLabel *titleLabel = new QLabel(tr("管理分类"), this);
    layout->addWidget(titleLabel);

    QLabel *hintLabel = new QLabel(tr("分类用于手动整理实例内的本地资源（模组等），可随时修改。"), this);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    QPushButton *createBtn = new QPushButton(tr("新建分类"), this);
    createBtn->setObjectName("primaryButton");
    createBtn->setCursor(Qt::PointingHandCursor);
    connect(createBtn, &QPushButton::clicked, this, &LocalCategoryDialog::onCreateCategory);
    btnLayout->addWidget(createBtn);

    m_renameBtn = new QPushButton(tr("重命名"), this);
    m_renameBtn->setEnabled(false);
    m_renameBtn->setCursor(Qt::PointingHandCursor);
    connect(m_renameBtn, &QPushButton::clicked, this, &LocalCategoryDialog::onRenameCategory);
    btnLayout->addWidget(m_renameBtn);

    m_deleteBtn = new QPushButton(tr("删除"), this);
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_deleteBtn, &QPushButton::clicked, this, &LocalCategoryDialog::onDeleteCategory);
    btnLayout->addWidget(m_deleteBtn);

    btnLayout->addStretch();

    QPushButton *closeBtn = new QPushButton(tr("关闭"), this);
    closeBtn->setObjectName("cancelButton");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);

    layout->addLayout(btnLayout);

    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &LocalCategoryDialog::onSelectionChanged);
}

void LocalCategoryDialog::reloadList()
{
    m_list->blockSignals(true);
    m_list->clear();

    const QList<LocalResourceCategory> cats = m_manager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        const int count = m_manager->filesInCategory(c.id).size();
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1（%2 项）").arg(c.name).arg(count), m_list);
        item->setData(Qt::UserRole, c.id);
    }
    m_list->blockSignals(false);
    refreshButtons();
}

void LocalCategoryDialog::refreshButtons()
{
    const bool hasSelection = m_list->currentItem() != nullptr;
    m_renameBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

void LocalCategoryDialog::onSelectionChanged()
{
    refreshButtons();
}

void LocalCategoryDialog::onCreateCategory()
{
    FavoriteFolderDialog dialog(FavoriteFolderDialog::CreateMode, this);
    dialog.setWindowTitle(tr("新建分类"));
    dialog.setPresets(LocalCategoryManager::suggestedNames(m_manager->resourceType()));
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString name = dialog.folderName();
    if (name.isEmpty())
        return;

    m_manager->addCategory(name);
    // 选中新建的分类
    const QString newId = m_manager->categoryId(name);
    for (int i = 0; i < m_list->count(); ++i)
    {
        if (m_list->item(i)->data(Qt::UserRole).toString() == newId)
        {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void LocalCategoryDialog::onRenameCategory()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;

    const QString categoryId = item->data(Qt::UserRole).toString();
    FavoriteFolderDialog dialog(FavoriteFolderDialog::RenameMode, this);
    dialog.setWindowTitle(tr("重命名分类"));
    dialog.setInitialName(m_manager->categoryName(categoryId));
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString newName = dialog.folderName();
    if (newName.isEmpty())
        return;

    m_manager->renameCategory(categoryId, newName);
}

void LocalCategoryDialog::onDeleteCategory()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;

    const QString categoryId = item->data(Qt::UserRole).toString();
    const QString name = m_manager->categoryName(categoryId);
    const int count = m_manager->filesInCategory(categoryId).size();

    QString message = tr("确定要删除分类 \"%1\" 吗？").arg(name);
    if (count > 0)
        message += tr("\n其中 %1 项资源将被移出该分类（资源本身不会被删除）。").arg(count);

    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("删除分类"), message,
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);
    if (reply == AppMessageBox::Yes)
        m_manager->deleteCategory(categoryId);
}
