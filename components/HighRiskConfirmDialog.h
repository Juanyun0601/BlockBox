/**
 * @file   HighRiskConfirmDialog.h
 * @brief  高风险插件信任确认弹窗（内嵌居中样式）
 * @author BlockBox Team
 * @date   2026-10-11
 *
 * 用于导入高风险插件时替换系统 QDialog：基于 AppDialogBase，
 * 全屏暗色模糊遮罩 + 居中圆角卡片，与 AppMessageBox 等内嵌弹窗观感一致。
 * 卡片内含红色警示图标、标题、正文、红色「我已了解风险」复选与
 * 「信任并… / 拒绝」按钮；未勾选复选框时主按钮不可点击。
 */

#pragma once

#include "components/AppDialogBase.h"

class QCheckBox;
class QLabel;
class QPushButton;

class HighRiskConfirmDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param title       弹窗标题（如「⚠ 你信任该插件吗？」）
     * @param message     正文（插件元数据 / 权限 / 风险 / 扫描结果）
     * @param trustAction 信任后的动作描述（如「添加此插件」）
     * @param parent      父窗口
     */
    HighRiskConfirmDialog(const QString &title, const QString &message,
                          const QString &trustAction, QWidget *parent = nullptr);

    /// 用户是否已勾选「我已了解风险」并点击「信任并…」
    bool confirmed() const { return m_confirmed; }

private:
    void initUI();
    /// 绘制红色圆形「!」警告图标
    QPixmap makeWarningPixmap() const;

    QString m_message;
    QString m_trustAction;

    QCheckBox *m_confirmBox = nullptr;
    QPushButton *m_okBtn = nullptr;
    bool m_confirmed = false;
};
