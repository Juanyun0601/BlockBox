/**
 * @file   CommandPackEditor.cpp
 * @brief  指令包制作 / 编辑对话框实现
 * @author BlockBox Team
 * @date   2026-07-17
 */

#include "CommandPackEditor.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include "components/AppMessageBox.h"
#include <QMetaType>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

/// commands 表格列索引
enum CmdCol
{
    CmdCol_Name = 0,
    CmdCol_Chinese,
    CmdCol_MinVersion,
    CmdCol_OpLevel,
    CmdCol_ServerOnly,
    CmdCol_Syntax,
    CmdCol_Description,
    CmdCol_Params, // 按钮列
    CmdCol_Count_
};

/// 物品 / 实体 / 效果 表格列索引
enum TrCol
{
    TrCol_English = 0,
    TrCol_Chinese,
    TrCol_Count_
};

/**
 * @brief 将中文别名列表转换为逗号分隔字符串
 */
QString chineseToString(const QStringList &list)
{
    return list.join(QStringLiteral(","));
}

/**
 * @brief 将逗号分隔字符串转换为中文别名列表
 */
QStringList stringToChinese(const QString &text)
{
    QStringList result;
    const auto parts = text.split(QStringLiteral(","), Qt::SkipEmptyParts);
    for (const QString &part : parts)
    {
        result.append(part.trimmed());
    }
    return result;
}

/**
 * @brief 在对话框中编辑指令的 params JSON 文本
 *
 * 提供一个多行编辑框，初始为 params 数组的格式化 JSON 文本。
 * 接受后返回解析后的 QList<ParamInfo>。
 *
 * @param parent 父窗口
 * @param params 初始参数列表
 * @param ok     输出是否成功解析
 * @return 解析后的参数列表
 */
QList<ParamInfo> editParamsJson(QWidget *parent, const QList<ParamInfo> &params, bool &ok)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(CommandPackEditor::tr("参数编辑（JSON）"));
    dialog.setMinimumSize(560, 400);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *hint = new QLabel(&dialog);
    hint->setText(CommandPackEditor::tr(
        "参数格式（每项含 name/type/required/default/candidates 字段）：\n"
        "  type 可选: selector/item/entity/effect/int/float/string/nbt/enum/pos/block"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // 序列化为 JSON 数组
    QJsonArray arr;
    for (const ParamInfo &p : params)
    {
        QJsonObject obj;
        obj[QStringLiteral("name")] = p.name;
        obj[QStringLiteral("type")] = p.type;
        if (p.defaultValue.has_value())
        {
            obj[QStringLiteral("default")] = p.defaultValue.value();
        }
        obj[QStringLiteral("required")] = p.required;
        if (p.type == QStringLiteral("enum") && !p.candidates.isEmpty())
        {
            QJsonArray cArr;
            for (const QString &c : p.candidates)
            {
                cArr.append(c);
            }
            obj[QStringLiteral("candidates")] = cArr;
        }
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    QString jsonText = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

    auto *edit = new QPlainTextEdit(&dialog);
    edit->setPlainText(jsonText);
    edit->setFont(QFont(QStringLiteral("Consolas"), 10));
    layout->addWidget(edit, 1);

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
    {
        ok = false;
        return {};
    }

    QList<ParamInfo> result;
    QJsonParseError parseError;
    const QJsonDocument newDoc = QJsonDocument::fromJson(edit->toPlainText().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !newDoc.isArray())
    {
        AppMessageBox::warning(parent, CommandPackEditor::tr("解析失败"),
                             CommandPackEditor::tr("JSON 解析失败: %1").arg(parseError.errorString()));
        ok = false;
        return result;
    }

    const QJsonArray newArr = newDoc.array();
    for (const QJsonValue &value : newArr)
    {
        const QJsonObject obj = value.toObject();
        ParamInfo param;
        param.name = obj.value(QStringLiteral("name")).toString();
        param.type = obj.value(QStringLiteral("type")).toString();
        if (obj.contains(QStringLiteral("default")))
        {
            param.defaultValue = obj.value(QStringLiteral("default")).toVariant().toString();
        }
        param.required = obj.value(QStringLiteral("required")).toBool(true);
        if (param.type == QStringLiteral("enum"))
        {
            const QJsonArray cArr = obj.value(QStringLiteral("candidates")).toArray();
            for (const QJsonValue &c : cArr)
            {
                param.candidates.append(c.toString());
            }
        }
        result.append(param);
    }

    ok = true;
    return result;
}

/**
 * @brief 将翻译表（物品/实体/效果）填充到 QTableWidget
 */
template<typename T>
void fillTranslationTable(QTableWidget *table, const QList<T> &list)
{
    table->setRowCount(0);
    table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i)
    {
        auto *engItem = new QTableWidgetItem(list.at(i).english);
        auto *cnItem = new QTableWidgetItem(chineseToString(list.at(i).chinese));
        table->setItem(i, TrCol_English, engItem);
        table->setItem(i, TrCol_Chinese, cnItem);
    }
}

/**
 * @brief 从 QTableWidget 读取数据到翻译列表
 */
template<typename T>
void readTranslationTable(QTableWidget *table, QList<T> &outList)
{
    outList.clear();
    outList.reserve(table->rowCount());
    for (int row = 0; row < table->rowCount(); ++row)
    {
        QTableWidgetItem *engItem = table->item(row, TrCol_English);
        QTableWidgetItem *cnItem = table->item(row, TrCol_Chinese);
        if (!engItem && !cnItem)
        {
            continue;
        }
        T entry;
        entry.english = engItem ? engItem->text().trimmed() : QString();
        entry.chinese = stringToChinese(cnItem ? cnItem->text() : QString());
        if (!entry.english.isEmpty() || !entry.chinese.isEmpty())
        {
            outList.append(entry);
        }
    }
}

} // namespace

CommandPackEditor::CommandPackEditor(QWidget *parent)
    : QDialog(parent)
    , m_nameEdit(nullptr)
    , m_authorEdit(nullptr)
    , m_versionEdit(nullptr)
    , m_mcVersionEdit(nullptr)
    , m_descEdit(nullptr)
    , m_tabs(nullptr)
    , m_commandsTable(nullptr)
    , m_itemsTable(nullptr)
    , m_entitiesTable(nullptr)
    , m_effectsTable(nullptr)
    , m_addCmdBtn(nullptr)
    , m_removeCmdBtn(nullptr)
    , m_addItemBtn(nullptr)
    , m_removeItemBtn(nullptr)
    , m_addEntityBtn(nullptr)
    , m_removeEntityBtn(nullptr)
    , m_addEffectBtn(nullptr)
    , m_removeEffectBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_saveAsBtn(nullptr)
    , m_cancelBtn(nullptr)
{
    setWindowTitle(tr("制作指令包"));
    setMinimumSize(820, 540);
    buildUI();
    loadFromPack();
}

CommandPackEditor::~CommandPackEditor() = default;

void CommandPackEditor::buildUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 元信息表单 ----
    auto *metaForm = new QFormLayout();
    metaForm->setLabelAlignment(Qt::AlignRight);
    metaForm->setSpacing(6);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("如：我的整合包指令"));
    m_authorEdit = new QLineEdit(this);
    m_authorEdit->setPlaceholderText(tr("作者名"));
    m_versionEdit = new QLineEdit(this);
    m_versionEdit->setPlaceholderText(tr("如：1.0.0"));
    m_mcVersionEdit = new QLineEdit(this);
    m_mcVersionEdit->setPlaceholderText(tr("如：1.20.4"));
    m_descEdit = new QPlainTextEdit(this);
    m_descEdit->setFixedHeight(60);
    m_descEdit->setPlaceholderText(tr("指令包描述"));

    metaForm->addRow(tr("名称"), m_nameEdit);
    metaForm->addRow(tr("作者"), m_authorEdit);
    metaForm->addRow(tr("版本"), m_versionEdit);
    metaForm->addRow(tr("目标 MC 版本"), m_mcVersionEdit);
    metaForm->addRow(tr("描述"), m_descEdit);
    layout->addLayout(metaForm);

    // ---- Tab 区 ----
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, 1);

    // 工具函数：创建翻译表（物品 / 实体 / 效果）
    auto createTranslationTab = [this](const QString &title, QTableWidget *&table,
                                       QPushButton *&addBtn, QPushButton *&removeBtn,
                                       int tableIndex) {
        auto *page = new QWidget(this);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(6, 6, 6, 6);
        pageLayout->setSpacing(4);

        table = new QTableWidget(page);
        table->setColumnCount(TrCol_Count_);
        table->setHorizontalHeaderLabels({tr("英文 ID"), tr("中文别名（逗号分隔）")});
        table->horizontalHeader()->setSectionResizeMode(TrCol_English, QHeaderView::Interactive);
        table->horizontalHeader()->setSectionResizeMode(TrCol_Chinese, QHeaderView::Stretch);
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setAlternatingRowColors(true);
        pageLayout->addWidget(table, 1);

        auto *btnRow = new QHBoxLayout();
        addBtn = new QPushButton(tr("新增"), page);
        removeBtn = new QPushButton(tr("删除"), page);
        btnRow->addWidget(addBtn);
        btnRow->addWidget(removeBtn);
        btnRow->addStretch(1);
        pageLayout->addLayout(btnRow);

        connect(addBtn, &QPushButton::clicked, this,
                [this, tableIndex]() { onAddTranslationEntry(tableIndex); });
        connect(removeBtn, &QPushButton::clicked, this,
                [this, tableIndex]() { onRemoveTranslationEntry(tableIndex); });

        m_tabs->addTab(page, title);
    };

    // 指令 Tab
    {
        auto *page = new QWidget(this);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(6, 6, 6, 6);
        pageLayout->setSpacing(4);

        m_commandsTable = new QTableWidget(page);
        m_commandsTable->setColumnCount(CmdCol_Count_);
        m_commandsTable->setHorizontalHeaderLabels(
            {tr("英文"), tr("中文别名（逗号分隔）"), tr("最低版本"), tr("OP等级"),
             tr("仅服务端"), tr("语法"), tr("描述"), tr("参数")});
        m_commandsTable->horizontalHeader()->setSectionResizeMode(CmdCol_Name, QHeaderView::Interactive);
        m_commandsTable->horizontalHeader()->setSectionResizeMode(CmdCol_Chinese, QHeaderView::Stretch);
        m_commandsTable->horizontalHeader()->setSectionResizeMode(CmdCol_MinVersion, QHeaderView::ResizeToContents);
        m_commandsTable->horizontalHeader()->setSectionResizeMode(CmdCol_OpLevel, QHeaderView::ResizeToContents);
        m_commandsTable->horizontalHeader()->setSectionResizeMode(CmdCol_ServerOnly, QHeaderView::ResizeToContents);
        m_commandsTable->horizontalHeader()->setSectionResizeMode(CmdCol_Syntax, QHeaderView::Stretch);
        m_commandsTable->horizontalHeader()->setSectionResizeMode(CmdCol_Description, QHeaderView::Stretch);
        m_commandsTable->horizontalHeader()->setSectionResizeMode(CmdCol_Params, QHeaderView::ResizeToContents);
        m_commandsTable->verticalHeader()->setVisible(false);
        m_commandsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_commandsTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_commandsTable->setAlternatingRowColors(true);
        // 双击"参数"列触发编辑
        connect(m_commandsTable, &QTableWidget::itemDoubleClicked, this,
                &CommandPackEditor::onEditCommandParams);
        pageLayout->addWidget(m_commandsTable, 1);

        auto *btnRow = new QHBoxLayout();
        m_addCmdBtn = new QPushButton(tr("新增指令"), page);
        m_removeCmdBtn = new QPushButton(tr("删除指令"), page);
        btnRow->addWidget(m_addCmdBtn);
        btnRow->addWidget(m_removeCmdBtn);
        btnRow->addStretch(1);
        pageLayout->addLayout(btnRow);

        connect(m_addCmdBtn, &QPushButton::clicked, this, &CommandPackEditor::onAddCommand);
        connect(m_removeCmdBtn, &QPushButton::clicked, this, &CommandPackEditor::onRemoveCommand);

        m_tabs->addTab(page, tr("指令"));
    }

    // 物品 / 实体 / 效果 Tab
    createTranslationTab(tr("物品"), m_itemsTable, m_addItemBtn, m_removeItemBtn, 0);
    createTranslationTab(tr("实体"), m_entitiesTable, m_addEntityBtn, m_removeEntityBtn, 1);
    createTranslationTab(tr("效果"), m_effectsTable, m_addEffectBtn, m_removeEffectBtn, 2);

    // ---- 底部按钮 ----
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_saveBtn = new QPushButton(tr("保存并加入列表"), this);
    m_saveBtn->setObjectName(QStringLiteral("packEditorSaveBtn"));
    m_saveAsBtn = new QPushButton(tr("另存为"), this);
    m_saveAsBtn->setObjectName(QStringLiteral("packEditorSaveAsBtn"));
    m_cancelBtn = new QPushButton(tr("取消"), this);
    m_cancelBtn->setObjectName(QStringLiteral("packEditorCancelBtn"));
    btnRow->addWidget(m_saveAsBtn);
    btnRow->addWidget(m_saveBtn);
    btnRow->addWidget(m_cancelBtn);
    layout->addLayout(btnRow);

    connect(m_saveBtn, &QPushButton::clicked, this, &CommandPackEditor::onAccept);
    connect(m_saveAsBtn, &QPushButton::clicked, this, [this]() { saveAs(); });
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void CommandPackEditor::setPack(const CommandPack &pack)
{
    m_pack = pack;
    loadFromPack();
}

void CommandPackEditor::loadFromPack()
{
    m_nameEdit->setText(m_pack.name());
    m_authorEdit->setText(m_pack.author());
    m_versionEdit->setText(m_pack.version());
    m_mcVersionEdit->setText(m_pack.mcVersion());
    m_descEdit->setPlainText(m_pack.description());

    // 指令表
    m_commandsTable->setRowCount(0);
    m_commandsTable->setRowCount(m_pack.commands().size());
    for (int i = 0; i < m_pack.commands().size(); ++i)
    {
        commandToRow(i, m_pack.commands().at(i));
    }

    // 翻译表：物品 / 实体 / 效果
    fillTranslationTable(m_itemsTable, m_pack.items());
    fillTranslationTable(m_entitiesTable, m_pack.entities());
    fillTranslationTable(m_effectsTable, m_pack.effects());
}

bool CommandPackEditor::writeBackToPack()
{
    m_pack.setName(m_nameEdit->text().trimmed());
    m_pack.setAuthor(m_authorEdit->text().trimmed());
    m_pack.setVersion(m_versionEdit->text().trimmed());
    m_pack.setMcVersion(m_mcVersionEdit->text().trimmed());
    m_pack.setDescription(m_descEdit->toPlainText().trimmed());

    // 指令
    QList<CommandInfo> commands;
    commands.reserve(m_commandsTable->rowCount());
    for (int row = 0; row < m_commandsTable->rowCount(); ++row)
    {
        commands.append(rowToCommand(row));
    }
    m_pack.commands() = commands;

    // 翻译表：物品 / 实体 / 效果
    readTranslationTable(m_itemsTable, m_pack.items());
    readTranslationTable(m_entitiesTable, m_pack.entities());
    readTranslationTable(m_effectsTable, m_pack.effects());

    m_pack.refreshCounts();
    return true;
}

bool CommandPackEditor::validate()
{
    if (m_nameEdit->text().trimmed().isEmpty())
    {
        AppMessageBox::warning(this, tr("校验失败"), tr("请填写指令包名称。"));
        return false;
    }

    // 校验指令英文名非空
    for (int row = 0; row < m_commandsTable->rowCount(); ++row)
    {
        QTableWidgetItem *item = m_commandsTable->item(row, CmdCol_Name);
        if (!item || item->text().trimmed().isEmpty())
        {
            AppMessageBox::warning(this, tr("校验失败"),
                                 tr("第 %1 条指令缺少英文名。").arg(row + 1));
            return false;
        }
    }
    return true;
}

bool CommandPackEditor::saveAs()
{
    if (!writeBackToPack())
    {
        return false;
    }
    if (!validate())
    {
        return false;
    }

    const QString startDir = QDir::homePath() + QStringLiteral("/") + m_pack.name();
    const QString filePath = AppFileDialog::getSaveFileName(
        this, tr("保存指令包"), startDir, tr("指令包文件 (*.json)"));
    if (filePath.isEmpty())
    {
        return false;
    }

    QString finalPath = filePath;
    if (!finalPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
    {
        finalPath += QStringLiteral(".json");
    }

    if (!m_pack.saveToFile(finalPath))
    {
        AppMessageBox::warning(this, tr("保存失败"), tr("无法写入文件: %1").arg(finalPath));
        return false;
    }
    m_pack.setFilePath(QDir(finalPath).absolutePath());
    m_saved = true;
    return true;
}

bool CommandPackEditor::save()
{
    if (m_pack.filePath().isEmpty())
    {
        return saveAs();
    }
    if (!writeBackToPack() || !validate())
    {
        return false;
    }
    if (!m_pack.saveToFile(m_pack.filePath()))
    {
        AppMessageBox::warning(this, tr("保存失败"),
                             tr("无法写入文件: %1").arg(m_pack.filePath()));
        return false;
    }
    m_saved = true;
    return true;
}

void CommandPackEditor::onAccept()
{
    if (!save())
    {
        return;
    }
    accept();
}

void CommandPackEditor::onAddCommand()
{
    const int row = m_commandsTable->rowCount();
    m_commandsTable->setRowCount(row + 1);
    CommandInfo empty;
    empty.opLevel = 0;
    empty.serverOnly = false;
    empty.minVersion = QStringLiteral("1.0.0");
    commandToRow(row, empty);
}

void CommandPackEditor::onRemoveCommand()
{
    const int row = m_commandsTable->currentRow();
    if (row < 0)
    {
        AppMessageBox::information(this, tr("提示"), tr("请先选择要删除的指令行。"));
        return;
    }
    m_commandsTable->removeRow(row);
}

void CommandPackEditor::onEditCommandParams(QTableWidgetItem *item)
{
    if (!item)
    {
        return;
    }
    // 仅在双击"参数"列时触发
    if (item->column() != CmdCol_Params)
    {
        return;
    }
    const int row = item->row();
    if (row < 0 || row >= m_commandsTable->rowCount())
    {
        return;
    }

    CommandInfo cmd = rowToCommand(row);
    bool ok = false;
    QList<ParamInfo> params = editParamsJson(this, cmd.params, ok);
    if (!ok)
    {
        return;
    }
    cmd.params = params;
    commandToRow(row, cmd);
}

void CommandPackEditor::onAddTranslationEntry(int tableIndex)
{
    QTableWidget *table = nullptr;
    switch (tableIndex)
    {
    case 0: table = m_itemsTable; break;
    case 1: table = m_entitiesTable; break;
    case 2: table = m_effectsTable; break;
    default: return;
    }
    if (!table)
    {
        return;
    }
    const int row = table->rowCount();
    table->setRowCount(row + 1);
    table->setItem(row, TrCol_English, new QTableWidgetItem());
    table->setItem(row, TrCol_Chinese, new QTableWidgetItem());
}

void CommandPackEditor::onRemoveTranslationEntry(int tableIndex)
{
    QTableWidget *table = nullptr;
    switch (tableIndex)
    {
    case 0: table = m_itemsTable; break;
    case 1: table = m_entitiesTable; break;
    case 2: table = m_effectsTable; break;
    default: return;
    }
    if (!table)
    {
        return;
    }
    const int row = table->currentRow();
    if (row < 0)
    {
        AppMessageBox::information(this, tr("提示"), tr("请先选择要删除的行。"));
        return;
    }
    table->removeRow(row);
}

CommandInfo CommandPackEditor::rowToCommand(int row) const
{
    CommandInfo cmd;
    auto getText = [this, row](int col) -> QString {
        QTableWidgetItem *item = m_commandsTable->item(row, col);
        return item ? item->text() : QString();
    };

    cmd.name = getText(CmdCol_Name).trimmed();
    cmd.chinese = stringToChinese(getText(CmdCol_Chinese));
    cmd.minVersion = getText(CmdCol_MinVersion).trimmed();
    cmd.opLevel = getText(CmdCol_OpLevel).toInt();
    cmd.serverOnly = (getText(CmdCol_ServerOnly).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                      || getText(CmdCol_ServerOnly) == QStringLiteral("✓"));
    cmd.syntax = getText(CmdCol_Syntax);
    cmd.description = getText(CmdCol_Description);

    // params 从行的 UserRole 中获取（保存编辑结果）
    QTableWidgetItem *paramsItem = m_commandsTable->item(row, CmdCol_Params);
    if (paramsItem)
    {
        const QVariant v = paramsItem->data(Qt::UserRole);
        if (v.isValid() && v.metaType().id() == QMetaType::QString)
        {
            // 反序列化 JSON
            const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
            if (doc.isArray())
            {
                for (const QJsonValue &value : doc.array())
                {
                    const QJsonObject obj = value.toObject();
                    ParamInfo p;
                    p.name = obj.value(QStringLiteral("name")).toString();
                    p.type = obj.value(QStringLiteral("type")).toString();
                    if (obj.contains(QStringLiteral("default")))
                    {
                        p.defaultValue = obj.value(QStringLiteral("default")).toVariant().toString();
                    }
                    p.required = obj.value(QStringLiteral("required")).toBool(true);
                    if (p.type == QStringLiteral("enum"))
                    {
                        const QJsonArray cArr = obj.value(QStringLiteral("candidates")).toArray();
                        for (const QJsonValue &c : cArr)
                        {
                            p.candidates.append(c.toString());
                        }
                    }
                    cmd.params.append(p);
                }
            }
        }
    }
    return cmd;
}

void CommandPackEditor::commandToRow(int row, const CommandInfo &cmd)
{
    auto setItem = [this, row](int col, const QString &text, bool editable = true) {
        QTableWidgetItem *item = m_commandsTable->item(row, col);
        if (!item)
        {
            item = new QTableWidgetItem(text);
            m_commandsTable->setItem(row, col, item);
        }
        else
        {
            item->setText(text);
        }
        if (!editable)
        {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }
    };

    setItem(CmdCol_Name, cmd.name);
    setItem(CmdCol_Chinese, chineseToString(cmd.chinese));
    setItem(CmdCol_MinVersion, cmd.minVersion);
    setItem(CmdCol_OpLevel, QString::number(cmd.opLevel));
    setItem(CmdCol_ServerOnly, cmd.serverOnly ? QStringLiteral("✓") : QString());
    setItem(CmdCol_Syntax, cmd.syntax);
    setItem(CmdCol_Description, cmd.description);

    // 参数列显示按钮文本
    QJsonArray arr;
    for (const ParamInfo &p : cmd.params)
    {
        QJsonObject obj;
        obj[QStringLiteral("name")] = p.name;
        obj[QStringLiteral("type")] = p.type;
        if (p.defaultValue.has_value())
        {
            obj[QStringLiteral("default")] = p.defaultValue.value();
        }
        obj[QStringLiteral("required")] = p.required;
        if (p.type == QStringLiteral("enum") && !p.candidates.isEmpty())
        {
            QJsonArray cArr;
            for (const QString &c : p.candidates)
            {
                cArr.append(c);
            }
            obj[QStringLiteral("candidates")] = cArr;
        }
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    QString paramsJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    QTableWidgetItem *paramsItem = m_commandsTable->item(row, CmdCol_Params);
    if (!paramsItem)
    {
        paramsItem = new QTableWidgetItem(tr("编辑 (%1)").arg(cmd.params.size()));
        paramsItem->setFlags(paramsItem->flags() & ~Qt::ItemIsEditable);
        m_commandsTable->setItem(row, CmdCol_Params, paramsItem);
    }
    else
    {
        paramsItem->setText(tr("编辑 (%1)").arg(cmd.params.size()));
    }
    paramsItem->setData(Qt::UserRole, paramsJson);
    paramsItem->setToolTip(tr("双击编辑参数"));
}
