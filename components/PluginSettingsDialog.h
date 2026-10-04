/**
 * @file   PluginSettingsDialog.h
 * @brief  插件设置对话框类声明
 * @author BlockBox Team
 * @date   2026-08-06
 *
 * 根据插件清单 settings 定义动态生成设置表单：
 *   - text   → QLineEdit
 *   - bool   → QCheckBox
 *   - number → QLineEdit（数字校验）
 *   - select → QComboBox
 *   - color  → QLineEdit（#RRGGBB）
 * 值读写经 PluginManager::pluginSetting / setPluginSetting 持久化。
 */
#ifndef PLUGINSETTINGSDIALOG_H
#define PLUGINSETTINGSDIALOG_H

#include "components/AppDialogBase.h"

#include <QHash>
#include <QString>

#include "utils/plugin/PluginInfo.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QWidget;

class PluginSettingsDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit PluginSettingsDialog(const PluginInfo &plugin, QWidget *parent = nullptr);

private slots:
    void onSave();
    void onReset();

private:
    void initUI();
    void initStyle();
    QWidget *createFieldWidget(const PluginSettingItem &item, const QString &currentValue);
    QString fieldValue(const PluginSettingItem &item) const;

    PluginInfo m_plugin;

    QWidget *m_card;
    QHash<QString, QWidget *> m_fieldWidgets;  // key → 控件
    QPushButton *m_saveBtn;
    QPushButton *m_resetBtn;
    QPushButton *m_cancelBtn;
};

#endif // PLUGINSETTINGSDIALOG_H
