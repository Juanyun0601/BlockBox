/**
 * @file   HumanCheckDialog.h
 * @brief  进社区前的滑块拼图人机验证弹窗
 * @author BlockBox Team
 * @date   2026-11-06
 */
#ifndef HUMANCHECKDIALOG_H
#define HUMANCHECKDIALOG_H

#include <QJsonObject>
#include <QString>

#include "components/AppDialogBase.h"

class QLabel;
class QPushButton;

class HumanPuzzleView;
class HumanSlideBar;

/**
 * @brief 滑块拼图人机验证弹窗
 *
 * 论坛开了人机验证之后，进社区前要先证明一下是真人：服务端现场画一张图，
 * 抠掉一块拼图，用户把滑块拖到缺口位置松手即可。图片和判定都在服务端，
 * 这里只负责显示和收集滑动位置；通过后令牌存在 ForumAccountService 里，
 * 到期前（默认 8 小时）再进社区不会重复弹。
 *
 * 拖得比人手还快、或者没对准缺口时，弹窗会自己换一张新图让用户重来。
 */
class HumanCheckDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit HumanCheckDialog(QWidget *parent = nullptr);

    /**
     * @brief 便捷入口：弹出验证框并等结果
     * @return 通过返回 true；用户点遮罩 / 按 Esc 关掉算没通过
     */
    static bool verify(QWidget *parent);

    /**
     * @brief 当前是否已经有一道验证弹窗开着（跨页面 / 跨窗口统一去重）
     *
     * 主窗口的社区请求和 AI 助手工作模式的提前验证可能同时想弹滑块，
     * 两边都先看这里，避免叠出第二个窗口。
     */
    static bool isChecking();

private slots:
    void onChallengeReady(const QJsonObject &challenge);
    void onCheckPassed();
    void onCheckFailed(const QString &message);

private:
    void initUI();
    void initStyle();
    void fetchChallenge();
    void submitAnswer(int answer, int durationMs);
    void setStatus(const QString &text, const QString &color = QString());

    HumanPuzzleView *m_puzzle = nullptr;
    HumanSlideBar *m_slide = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;

    QString m_challengeId;   ///< 当前这道题的题号
    int m_piecePad = 0;      ///< 拼图图自带的透明边（像素）
    int m_maxAnswer = 0;     ///< 拼图最右能拖到哪（图片像素）
    bool m_busy = false;     ///< 正在交卷，期间不接受新的拖动
    bool m_passed = false;   ///< 已经通过，别让后续信号再改状态
    bool m_awaitingChallenge = false; ///< 正在等题（用来区分网络错误和答错）

    // 主题色（构造时取自 ThemeManager）
    QString m_accentColor;
    QString m_cardBg;
    QString m_cardBorder;
    QString m_textColor;
    QString m_subColor;
    QString m_okColor;
    QString m_badColor;
};

#endif // HUMANCHECKDIALOG_H
