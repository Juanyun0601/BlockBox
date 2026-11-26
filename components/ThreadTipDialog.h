/**
 * @file   ThreadTipDialog.h
 * @brief  帖子打赏弹窗：选 2~10 个方块打赏主题
 * @author BlockBox Team
 * @date   2026-11-15
 *
 * 打赏的方块会转给作者，同时提升这个帖子的推流。
 * 确认后先播一段小方块飘升动画再自动关闭，成功与否由调用方提示。
 */
#pragma once

#include <QWidget>

#include "AppDialogBase.h"
#include "utils/ForumContentService.h"

class QLabel;
class QPushButton;

class ThreadTipDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 一站式打赏入口（登录与否由调用方把关）
     * @param parent 宿主页面（提示气泡挂它上面）
     * @param info   帖子头部信息（标题 + 作者，仅用于展示）
     * @return 实际打赏出去的方块数；取消 / 失败返回 0
     */
    static int tip(QWidget *parent, const ForumThreadInfo &info);

    explicit ThreadTipDialog(const ForumThreadInfo &info, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void initUI();
    void initStyle();
    void updateAmountViews();
    void updateBalanceView();
    void confirmTip();
    void playCubeFlight();

    ForumThreadInfo m_info;
    int m_amount = 4;
    int m_points = 0;          ///< 我的方块余额（GET /api/points 刷新后的值）
    bool m_tipping = false;    ///< 请求已发出：禁掉重复点击，也禁掉点遮罩关闭

    QWidget *m_card = nullptr;
    QLabel *m_amountLabel = nullptr;
    QLabel *m_helperLabel = nullptr;
    QLabel *m_balanceLabel = nullptr;
    QPushButton *m_minusButton = nullptr;
    QPushButton *m_plusButton = nullptr;
    QPushButton *m_confirmButton = nullptr;
};
