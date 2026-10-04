/**
 * @file   CommandPackEditor.h
 * @brief  指令包制作 / 编辑对话框
 * @author BlockBox Team
 * @date   2026-07-17
 *
 * 提供 GUI 编辑界面，让用户制作或修改指令包：
 *   - 顶部表单编辑元信息（名称 / 描述 / 作者 / 版本 / 目标 MC 版本）
 *   - 下方 4 个 Tab 分别编辑 commands / items / entities / effects
 *   - "保存"按钮保存到文件，"保存并加入列表"会触发 accepted 状态
 *
 * 参数（params）通过 JSON 文本对话框编辑，避免过度复杂的子表格。
 */

#pragma once

#include <QDialog>

#include "utils/CommandAssistant/CommandPack.h"

class QTabWidget;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QPushButton;
class QTableWidgetItem;

class CommandPackEditor : public QDialog
{
    Q_OBJECT

public:
    explicit CommandPackEditor(QWidget *parent = nullptr);
    ~CommandPackEditor() override;

    /**
     * @brief 载入已有指令包用于编辑
     */
    void setPack(const CommandPack &pack);

    /**
     * @brief 获取当前编辑的指令包（包含未保存的修改）
     */
    const CommandPack &pack() const { return m_pack; }

    /**
     * @brief 是否在退出前已保存到文件
     */
    bool saved() const { return m_saved; }

private:
    /** @brief 构建 UI */
    void buildUI();
    /** @brief 从 m_pack 填充 UI */
    void loadFromPack();
    /** @brief 从 UI 写回 m_pack */
    bool writeBackToPack();
    /** @brief 选择文件并保存当前指令包 */
    bool saveAs();
    /** @brief 若已有 filePath 则直接保存，否则调用 saveAs */
    bool save();
    /** @brief 校验当前编辑内容（名称非空、commands 中 name 非空等） */
    bool validate();
    /** @brief 提交（保存 + accept） */
    void onAccept();

    // ---- 指令表 ----
    void onAddCommand();
    void onRemoveCommand();
    void onEditCommandParams(QTableWidgetItem *item);

    // ---- 物品 / 实体 / 效果表 ----
    void onAddTranslationEntry(int tableIndex);
    void onRemoveTranslationEntry(int tableIndex);

    // ---- 工具 ----
    /**
     * @brief 提取某行 commands 表格的数据为 CommandInfo
     */
    CommandInfo rowToCommand(int row) const;
    /**
     * @brief 将 CommandInfo 写入指定行
     */
    void commandToRow(int row, const CommandInfo &cmd);

    CommandPack m_pack;
    bool m_saved = false;

    // 元信息编辑控件
    QLineEdit *m_nameEdit;
    QLineEdit *m_authorEdit;
    QLineEdit *m_versionEdit;
    QLineEdit *m_mcVersionEdit;
    QPlainTextEdit *m_descEdit;

    QTabWidget *m_tabs;
    QTableWidget *m_commandsTable;
    QTableWidget *m_itemsTable;
    QTableWidget *m_entitiesTable;
    QTableWidget *m_effectsTable;

    QPushButton *m_addCmdBtn;
    QPushButton *m_removeCmdBtn;
    QPushButton *m_addItemBtn;
    QPushButton *m_removeItemBtn;
    QPushButton *m_addEntityBtn;
    QPushButton *m_removeEntityBtn;
    QPushButton *m_addEffectBtn;
    QPushButton *m_removeEffectBtn;
    QPushButton *m_saveBtn;
    QPushButton *m_saveAsBtn;
    QPushButton *m_cancelBtn;
};
