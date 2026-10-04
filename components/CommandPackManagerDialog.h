/**
 * @file   CommandPackManagerDialog.h
 * @brief  指令包管理对话框（添加 / 移除 / 启用 / 制作指令包）
 * @author BlockBox Team
 * @date   2026-07-17
 *
 * 提供对自定义指令包的集中管理界面：
 *   - 列表显示已注册的指令包及其元信息与启用状态
 *   - 通过"添加"按钮选择外部 .json 指令包文件加入列表
 *   - 通过"制作指令包"按钮调用 CommandPackEditor 创建新指令包
 *   - 通过"编辑"按钮调用 CommandPackEditor 修改已有指令包
 *   - 启用/禁用复选框控制是否合并到当前指令数据库
 *   - 修改后立即持久化并发出 packsChanged 信号
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QPair>

class QTableWidget;
class QTableWidgetItem;
class QPushButton;

/**
 * @brief 指令包管理对话框
 *
 * 显示已注册指令包列表，支持添加/移除/启用/编辑/制作。
 * 修改后会持久化到 SettingsManager 并通过 packsChanged 通知外部重建数据库。
 */
class CommandPackManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CommandPackManagerDialog(QWidget *parent = nullptr);
    ~CommandPackManagerDialog() override;

    /**
     * @brief 重新加载已注册指令包列表（从 SettingsManager）
     *
     * 在对话框显示前调用以同步外部修改。
     */
    void reloadPackList();

signals:
    /**
     * @brief 指令包列表或启用状态发生变化
     *
     * 接收方应清空 CommandDatabase 后重新加载内置数据与所有已启用指令包。
     */
    void packsChanged();

private:
    /** @brief 构建表格列：启用 / 名称 / 版本 / 作者 / 条目数 / 路径 */
    void buildUI();
    /** @brief 刷新表格内容（不重读 SettingsManager，仅刷新显示） */
    void refreshTable();
    /** @brief 持久化当前内存中的指令包列表到 SettingsManager */
    void persist();
    /** @brief 添加指令包文件到列表（自动重载元信息） */
    void onAddPack();
    /** @brief 移除选中行（仅从列表中移除，不删除文件） */
    void onRemovePack();
    /** @brief 编辑选中行的指令包（打开编辑器） */
    void onEditPack();
    /** @brief 创建新指令包（打开空编辑器） */
    void onCreatePack();
    /** @brief 启用状态复选框切换 */
    void onItemChanged(QTableWidgetItem *item);

    /** @brief 内存中的指令包列表（路径 + 启用状态 + 元信息） */
    struct PackEntry
    {
        QString filePath;
        bool enabled = true;
        QString name;
        QString version;
        QString author;
        int commandCount = 0;
        int itemCount = 0;
        int entityCount = 0;
        int effectCount = 0;
    };
    QList<PackEntry> m_packs;
    bool m_loading = false; ///< 防止刷新表格时触发 onItemChanged

    QTableWidget *m_table;
    QPushButton *m_addBtn;
    QPushButton *m_createBtn;
    QPushButton *m_editBtn;
    QPushButton *m_removeBtn;
    QPushButton *m_closeBtn;
};
