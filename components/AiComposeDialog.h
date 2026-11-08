/**
 * @file   AiComposeDialog.h
 * @brief  手机端 AI 助手输入窗口：覆盖主窗口的居中卡片，承载页面输入卡片
 * @author BlockBox Team
 * @date   2026-11-07
 */
#ifndef AICOMPOSEDIALOG_H
#define AICOMPOSEDIALOG_H

#include "AppDialogBase.h"

class QLabel;
class QVBoxLayout;

/**
 * @brief 手机端输入弹窗（居中卡片）
 *
 * 手机端 AI 助手页底部只保留一个「输入」按钮，点开后弹出本窗口。
 * 输入卡片（文本域、模型/权限选择、引用与发送按钮）本身仍属于 AiChatPage，
 * 打开时整块搬进本窗口的内容区，关闭时再搬回页面，草稿与状态都不会丢。
 */
class AiComposeDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit AiComposeDialog(QWidget *parent = nullptr);

    /// 内容区布局：调用方把自己要展示的输入控件放进这里
    QVBoxLayout *contentLayout() const;

private:
    void initUI();

    QLabel *m_titleLabel;
    QVBoxLayout *m_contentLayout;
};

#endif // AICOMPOSEDIALOG_H
