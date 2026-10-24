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
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

LocalCategoryDialog::LocalCategoryDialog(LocalCategoryManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_stack(nullptr)
    , m_list(nullptr)
    , m_emptyLabel(nullptr)
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
    setObjectName(QStringLiteral("localCategoryDialog"));
    // 固定宽改为「可缩放 + 合适默认值」，列表内容多时也能拉大查看
    resize(440, 420);
    setMinimumSize(400, 360);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(10);

    QLabel *titleLabel = new QLabel(tr("管理分类"), this);
    titleLabel->setObjectName(QStringLiteral("listTitle"));
    layout->addWidget(titleLabel);

    QLabel *hintLabel = new QLabel(
        tr("分类用于手动整理实例内的本地资源（模组等），可随时修改。"), this);
    hintLabel->setObjectName(QStringLiteral("hintLabel"));
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);
    layout->addSpacing(2);

    // 内容区：空态提示 与 分类列表 二选一，保证空态在框内垂直居中
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("localCatStack"));
    m_stack->setMinimumHeight(160);

    // 空态容器复用列表同一套边框/底色，避免切换时布局跳变
    auto *emptyBox = new QWidget(m_stack);
    emptyBox->setObjectName(QStringLiteral("localCatEmptyBox"));
    auto *emptyBoxLayout = new QVBoxLayout(emptyBox);
    emptyBoxLayout->setContentsMargins(12, 12, 12, 12);
    m_emptyLabel = new QLabel(
        tr("暂无分类\n点击下方「新建分类」开始整理"), emptyBox);
    m_emptyLabel->setObjectName(QStringLiteral("localCatEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    emptyBoxLayout->addWidget(m_emptyLabel, 1);
    m_stack->addWidget(emptyBox);

    m_list = new QListWidget(m_stack);
    m_list->setObjectName(QStringLiteral("localCatList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stack->addWidget(m_list);

    layout->addWidget(m_stack, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    QPushButton *createBtn = new QPushButton(tr("新建分类"), this);
    createBtn->setObjectName("primaryButton");
    createBtn->setCursor(Qt::PointingHandCursor);
    connect(createBtn, &QPushButton::clicked, this, &LocalCategoryDialog::onCreateCategory);
    btnLayout->addWidget(createBtn);

    m_renameBtn = new QPushButton(tr("重命名"), this);
    m_renameBtn->setObjectName(QStringLiteral("localCatSecondaryBtn"));
    m_renameBtn->setEnabled(false);
    m_renameBtn->setCursor(Qt::PointingHandCursor);
    connect(m_renameBtn, &QPushButton::clicked, this, &LocalCategoryDialog::onRenameCategory);
    btnLayout->addWidget(m_renameBtn);

    m_deleteBtn = new QPushButton(tr("删除"), this);
    m_deleteBtn->setObjectName(QStringLiteral("localCatDeleteBtn"));
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
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &LocalCategoryDialog::onItemDoubleClicked);
}

QWidget *LocalCategoryDialog::makeCategoryRow(const QString &name, int count, QWidget *parent)
{
    QWidget *row = new QWidget(parent);
    row->setObjectName(QStringLiteral("localCatRow"));

    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(10, 0, 10, 0);
    h->setSpacing(8);

    QLabel *nameLabel = new QLabel(name, row);
    nameLabel->setObjectName(QStringLiteral("localCatName"));
    nameLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    h->addWidget(nameLabel, 1);

    QLabel *badge = new QLabel(QString::number(count), row);
    badge->setObjectName(QStringLiteral("localCatCountBadge"));
    badge->setAlignment(Qt::AlignCenter);
    h->addWidget(badge);

    return row;
}

void LocalCategoryDialog::reloadList()
{
    // 记住当前选中分类，重建后尽量恢复，避免重命名/外部变更后选中丢失
    QString keepId;
    if (QListWidgetItem *cur = m_list->currentItem())
        keepId = cur->data(Qt::UserRole).toString();

    m_list->blockSignals(true);
    m_list->clear();

    const QList<LocalResourceCategory> cats = m_manager->categories();
    if (cats.isEmpty())
    {
        m_stack->setCurrentIndex(0);
    }
    else
    {
        int restoreRow = -1;
        for (int i = 0; i < cats.size(); ++i)
        {
            const LocalResourceCategory &c = cats.at(i);
            const int count = m_manager->filesInCategory(c.id).size();
            QListWidgetItem *item = new QListWidgetItem(m_list);
            item->setData(Qt::UserRole, c.id);
            // 视觉由 setItemWidget 行控件绘制；item 本身不再画文字，避免叠字
            item->setText(QString());
            item->setToolTip(tr("%1 — %2 项").arg(c.name).arg(count));
            item->setSizeHint(QSize(0, 44));
            m_list->setItemWidget(item, makeCategoryRow(c.name, count, m_list));
            if (restoreRow < 0 && !keepId.isEmpty() && c.id == keepId)
                restoreRow = i;
        }
        m_stack->setCurrentIndex(1);
        if (restoreRow >= 0)
            m_list->setCurrentRow(restoreRow);
    }

    m_list->blockSignals(false);
    updateRowSelectedFlags();
    refreshButtons();
}

void LocalCategoryDialog::updateRowSelectedFlags()
{
    for (int i = 0; i < m_list->count(); ++i)
    {
        QWidget *row = m_list->itemWidget(m_list->item(i));
        if (!row)
            continue;
        const bool selected = (m_list->currentRow() == i);
        if (row->property("selected").toBool() == selected)
            continue;
        row->setProperty("selected", selected);
        row->style()->unpolish(row);
        row->style()->polish(row);
        row->update();
    }
}

void LocalCategoryDialog::refreshButtons()
{
    const bool hasSelection = m_list->currentItem() != nullptr;
    m_renameBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

void LocalCategoryDialog::onSelectionChanged()
{
    updateRowSelectedFlags();
    refreshButtons();
}

void LocalCategoryDialog::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    // 空态占位项不可选中，不会走到这里
    onRenameCategory();
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
