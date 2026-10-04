/**
 * @file   ModSettingsDialog.h
 * @brief  本地模组设置对话框 — 在 AppDialogBase 卡片内承载 LocalModSettingsPage
 * @author BlockBox Team
 * @date   2026-08-29
 *
 * 供实例助手窗口(InstanceAssistantWindow)的资源管理页打开单个模组的
 * 设置页(启用/禁用、config 配置表单编辑)。对话框挂载到主窗口上,
 * 避免在 450px 窄助手窗内放不下表单。
 */

#ifndef MODSETTINGSDIALOG_H
#define MODSETTINGSDIALOG_H

#include "AppDialogBase.h"

#include "../utils/mod/ModData.h"

class LocalModSettingsPage;

class ModSettingsDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit ModSettingsDialog(QWidget *parent = nullptr);
    ~ModSettingsDialog() override;

    /** 指定要查看/编辑的模组与实例路径并加载 */
    void setMod(const ModInfo &info, const QString &instancePath);

signals:
    /** 模组状态发生变化(启用/禁用),由内嵌设置页转发,宿主可刷新列表 */
    void modSettingsChanged();

private:
    LocalModSettingsPage *m_settingsPage;
};

#endif // MODSETTINGSDIALOG_H
