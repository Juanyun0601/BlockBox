/**
 * @file   CommandPackManagerDialog.cpp
 * @brief  指令包管理对话框实现
 * @author BlockBox Team
 * @date   2026-07-17
 */

#include "CommandPackManagerDialog.h"

#include <QDir>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include "components/AppMessageBox.h"
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <optional>

#include "components/CommandPackEditor.h"
#include "utils/CommandAssistant/CommandPack.h"
#include "utils/SettingsManager.h"

namespace {

/// 表格列索引
enum ColumnIndex
{
    ColEnabled = 0,
    ColName,
    ColVersion,
    ColAuthor,
    ColCount,
    ColPath,
    ColCount_
};

/**
 * @brief 从指定路径加载指令包元信息
 * @return 加载成功返回元信息，失败返回空 optional
 */
std::optional<CommandPack> tryLoadPackInfo(const QString &filePath)
{
    CommandPack pack;
    if (!pack.loadFromFile(filePath))
    {
        return std::nullopt;
    }
    pack.refreshCounts();
    return pack;
}

} // namespace

CommandPackManagerDialog::CommandPackManagerDialog(QWidget *parent)
    : QDialog(parent)
    , m_table(nullptr)
    , m_addBtn(nullptr)
    , m_createBtn(nullptr)
    , m_editBtn(nullptr)
    , m_removeBtn(nullptr)
    , m_closeBtn(nullptr)
{
    setWindowTitle(tr("指令包管理"));
    setMinimumSize(680, 420);
    buildUI();
    reloadPackList();
}

CommandPackManagerDialog::~CommandPackManagerDialog() = default;

void CommandPackManagerDialog::buildUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("packManagerTable"));
    m_table->setColumnCount(ColCount_);
    m_table->setHorizontalHeaderLabels(
        {tr("启用"), tr("名称"), tr("版本"), tr("作者"), tr("条目数"), tr("文件路径")});
    m_table->horizontalHeader()->setSectionResizeMode(ColEnabled, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColVersion, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColAuthor, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColCount, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColPath, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers
                             | QAbstractItemView::DoubleClicked); // 仅启用列可编辑
    m_table->setAlternatingRowColors(true);
    connect(m_table, &QTableWidget::itemChanged, this, &CommandPackManagerDialog::onItemChanged);

    layout->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    m_addBtn = new QPushButton(tr("添加指令包"), this);
    m_addBtn->setObjectName(QStringLiteral("packAddBtn"));
    m_createBtn = new QPushButton(tr("制作指令包"), this);
    m_createBtn->setObjectName(QStringLiteral("packCreateBtn"));
    m_editBtn = new QPushButton(tr("编辑"), this);
    m_editBtn->setObjectName(QStringLiteral("packEditBtn"));
    m_removeBtn = new QPushButton(tr("移除"), this);
    m_removeBtn->setObjectName(QStringLiteral("packRemoveBtn"));
    m_closeBtn = new QPushButton(tr("关闭"), this);
    m_closeBtn->setObjectName(QStringLiteral("packCloseBtn"));

    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_createBtn);
    btnRow->addWidget(m_editBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(m_closeBtn);
    layout->addLayout(btnRow);

    connect(m_addBtn, &QPushButton::clicked, this, &CommandPackManagerDialog::onAddPack);
    connect(m_createBtn, &QPushButton::clicked, this, &CommandPackManagerDialog::onCreatePack);
    connect(m_editBtn, &QPushButton::clicked, this, &CommandPackManagerDialog::onEditPack);
    connect(m_removeBtn, &QPushButton::clicked, this, &CommandPackManagerDialog::onRemovePack);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void CommandPackManagerDialog::reloadPackList()
{
    m_packs.clear();
    const QList<QPair<QString, bool>> saved = SettingsManager::instance()->commandPackList();
    for (const auto &pair : saved)
    {
        const QString &filePath = pair.first;
        bool enabled = pair.second;
        auto pack = tryLoadPackInfo(filePath);
        if (!pack.has_value())
        {
            // 加载失败的指令包仍保留在列表中但标记为禁用
            PackEntry entry;
            entry.filePath = filePath;
            entry.enabled = false;
            entry.name = QFileInfo(filePath).completeBaseName() + tr("（加载失败）");
            m_packs.append(entry);
            continue;
        }
        PackEntry entry;
        entry.filePath = pack->filePath();
        entry.enabled = enabled;
        entry.name = pack->name();
        entry.version = pack->version();
        entry.author = pack->author();
        entry.commandCount = pack->info().commandCount;
        entry.itemCount = pack->info().itemCount;
        entry.entityCount = pack->info().entityCount;
        entry.effectCount = pack->info().effectCount;
        m_packs.append(entry);
    }
    refreshTable();
}

void CommandPackManagerDialog::refreshTable()
{
    m_loading = true;
    m_table->setRowCount(0);
    m_table->setRowCount(m_packs.size());

    for (int row = 0; row < m_packs.size(); ++row)
    {
        const PackEntry &entry = m_packs.at(row);

        // 启用列：可编辑复选框
        auto *enabledItem = new QTableWidgetItem();
        enabledItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        enabledItem->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        enabledItem->setData(Qt::UserRole, row);
        m_table->setItem(row, ColEnabled, enabledItem);

        // 名称
        auto *nameItem = new QTableWidgetItem(entry.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColName, nameItem);

        // 版本
        auto *versionItem = new QTableWidgetItem(entry.version);
        versionItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColVersion, versionItem);

        // 作者
        auto *authorItem = new QTableWidgetItem(entry.author);
        authorItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColAuthor, authorItem);

        // 条目数（指令 / 物品 / 实体 / 效果）
        QString countText = QStringLiteral("%1 / %2 / %3 / %4")
                                .arg(entry.commandCount)
                                .arg(entry.itemCount)
                                .arg(entry.entityCount)
                                .arg(entry.effectCount);
        auto *countItem = new QTableWidgetItem(countText);
        countItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        countItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColCount, countItem);

        // 文件路径
        auto *pathItem = new QTableWidgetItem(entry.filePath);
        pathItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        pathItem->setToolTip(entry.filePath);
        m_table->setItem(row, ColPath, pathItem);
    }

    m_loading = false;
}

void CommandPackManagerDialog::persist()
{
    QList<QPair<QString, bool>> packs;
    packs.reserve(m_packs.size());
    for (const PackEntry &entry : m_packs)
    {
        packs.append(qMakePair(entry.filePath, entry.enabled));
    }
    SettingsManager::instance()->saveCommandPackList(packs);
}

void CommandPackManagerDialog::onAddPack()
{
    const QString startDir = QDir::homePath();
    const QString filePath = AppFileDialog::getOpenFileName(
        this, tr("选择指令包文件"), startDir, tr("指令包文件 (*.json);;所有文件 (*.*)"));
    if (filePath.isEmpty())
    {
        return;
    }

    const QString absPath = QDir(filePath).absolutePath();
    // 防止重复添加
    for (const PackEntry &entry : m_packs)
    {
        if (entry.filePath == absPath)
        {
            AppMessageBox::information(this, tr("提示"), tr("该指令包已在列表中。"));
            return;
        }
    }

    auto pack = tryLoadPackInfo(absPath);
    if (!pack.has_value())
    {
        AppMessageBox::warning(this, tr("加载失败"),
                             tr("无法加载指令包：%1\n请检查文件格式。").arg(absPath));
        return;
    }

    PackEntry entry;
    entry.filePath = pack->filePath();
    entry.enabled = true;
    entry.name = pack->name();
    entry.version = pack->version();
    entry.author = pack->author();
    entry.commandCount = pack->info().commandCount;
    entry.itemCount = pack->info().itemCount;
    entry.entityCount = pack->info().entityCount;
    entry.effectCount = pack->info().effectCount;
    m_packs.append(entry);

    persist();
    refreshTable();
    emit packsChanged();
}

void CommandPackManagerDialog::onRemovePack()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_packs.size())
    {
        AppMessageBox::information(this, tr("提示"), tr("请先选择要移除的指令包。"));
        return;
    }

    const auto reply = AppMessageBox::question(
        this, tr("确认移除"),
        tr("确定要从列表中移除指令包「%1」吗？\n（不会删除文件本身）")
            .arg(m_packs.at(row).name));
    if (reply != AppMessageBox::Yes)
    {
        return;
    }

    m_packs.removeAt(row);
    persist();
    refreshTable();
    emit packsChanged();
}

void CommandPackManagerDialog::onEditPack()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_packs.size())
    {
        AppMessageBox::information(this, tr("提示"), tr("请先选择要编辑的指令包。"));
        return;
    }

    const QString filePath = m_packs.at(row).filePath;
    CommandPack pack;
    if (!pack.loadFromFile(filePath))
    {
        AppMessageBox::warning(this, tr("加载失败"), tr("无法加载指令包文件。"));
        return;
    }

    CommandPackEditor editor(this);
    editor.setPack(pack);
    if (editor.exec() != QDialog::Accepted)
    {
        return;
    }

    // 编辑器接受后已自行保存到文件，这里仅刷新条目元信息
    CommandPack updated = editor.pack();
    PackEntry entry;
    entry.filePath = updated.filePath().isEmpty() ? filePath : updated.filePath();
    entry.enabled = m_packs.at(row).enabled; // 保留原启用状态
    entry.name = updated.name();
    entry.version = updated.version();
    entry.author = updated.author();
    entry.commandCount = updated.info().commandCount;
    entry.itemCount = updated.info().itemCount;
    entry.entityCount = updated.info().entityCount;
    entry.effectCount = updated.info().effectCount;
    m_packs[row] = entry;

    persist();
    refreshTable();
    emit packsChanged();
}

void CommandPackManagerDialog::onCreatePack()
{
    CommandPackEditor editor(this);
    if (editor.exec() != QDialog::Accepted)
    {
        return;
    }

    CommandPack created = editor.pack();
    if (created.filePath().isEmpty())
    {
        return; // 编辑器未保存
    }

    // 防止重复添加
    for (const PackEntry &entry : m_packs)
    {
        if (entry.filePath == created.filePath())
        {
            // 已存在则更新元信息
            PackEntry updated;
            updated.filePath = created.filePath();
            updated.enabled = entry.enabled;
            updated.name = created.name();
            updated.version = created.version();
            updated.author = created.author();
            updated.commandCount = created.info().commandCount;
            updated.itemCount = created.info().itemCount;
            updated.entityCount = created.info().entityCount;
            updated.effectCount = created.info().effectCount;
            // 替换原条目
            for (int i = 0; i < m_packs.size(); ++i)
            {
                if (m_packs[i].filePath == created.filePath())
                {
                    m_packs[i] = updated;
                    break;
                }
            }
            persist();
            refreshTable();
            emit packsChanged();
            return;
        }
    }

    PackEntry entry;
    entry.filePath = created.filePath();
    entry.enabled = true;
    entry.name = created.name();
    entry.version = created.version();
    entry.author = created.author();
    entry.commandCount = created.info().commandCount;
    entry.itemCount = created.info().itemCount;
    entry.entityCount = created.info().entityCount;
    entry.effectCount = created.info().effectCount;
    m_packs.append(entry);

    persist();
    refreshTable();
    emit packsChanged();
}

void CommandPackManagerDialog::onItemChanged(QTableWidgetItem *item)
{
    if (m_loading || !item)
    {
        return;
    }
    if (item->column() != ColEnabled)
    {
        return;
    }

    bool ok = false;
    const int row = item->data(Qt::UserRole).toInt(&ok);
    if (!ok || row < 0 || row >= m_packs.size())
    {
        return;
    }

    m_packs[row].enabled = (item->checkState() == Qt::Checked);
    persist();
    emit packsChanged();
}
