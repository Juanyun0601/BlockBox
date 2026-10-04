/**
 * @file   UninstallDialog.h
 * @brief  卸载方块盒子确认对话框声明
 * @author BlockBox Team
 * @date   2026-09-12
 */
#ifndef UNINSTALLDIALOG_H
#define UNINSTALLDIALOG_H

#include "AppDialogBase.h"

class CustomCheckBox;
class QLabel;

/**
 * @brief 卸载确认对话框：列出将被移除的内容，允许用户选择是否保留游戏数据
 */
class UninstallDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit UninstallDialog(QWidget *parent = nullptr);

    /// 用户是否点击了"确认卸载"
    bool confirmed() const { return m_confirmed; }
    /// 是否连游戏数据（.minecraft / BedrockData）一起删除
    bool deleteGameData() const;

private:
    void initUi();

    QLabel *m_gameDataItemLabel;
    CustomCheckBox *m_keepDataCheck;
    bool m_confirmed;
};

#endif // UNINSTALLDIALOG_H
