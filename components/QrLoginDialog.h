/**
 * @file   QrLoginDialog.h
 * @brief  手机扫码登录弹窗：把一次性登录票据画成二维码，等手机端扫码确认
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 从账户页登录卡上的「手机扫码登录」打开。弹窗自己申请二维码、自己收尾：
 * 用户取消 / 点遮罩 / 按 Esc 关掉时负责把票据作废，登录成功后自动关闭；
 * 页面刷新交给账户页现有的 userChanged 逻辑，本类不碰页面状态。
 *
 * 视觉沿用 AppDialogBase（模糊遮罩 + 居中圆角卡片），配色取 AppMessageBox
 * 那套主题色，深色 / 浅色模式都跟着主题走；这里只多一块白底二维码图。
 */
#pragma once

#include "components/AppDialogBase.h"

#include <QColor>
#include <QString>

class QLabel;
class QPushButton;
class QTimer;
class ForumAccountService;

class QrLoginDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit QrLoginDialog(QWidget *parent = nullptr);

    ~QrLoginDialog() override;

protected:
    /**
     * @brief 关闭弹窗的最后一道关：accept / reject / 点遮罩 / Esc 都会走到这里
     *
     * 在这里统一把二维码票据作废（登录成功的除外），保证关窗后服务端不会
     * 一直挂着一张没人会再扫的码在轮询。
     */
    void done(int result) override;

private:
    void initUI();
    void initStyle();

    /** 申请一张新二维码（首次打开与「刷新二维码」共用同一条路） */
    void beginQrLogin();
    /** 把当前地址画成二维码贴上；画不出来时摆出失败态并返回 false */
    bool applyQrImage();
    /** 过期 / 失败：清掉可扫的码，只留下「刷新二维码」入口 */
    void showFailure(const QString &detail);

    void onQrLoginStarted(const QString &ticket, const QString &scanUrl, int expiresIn);
    void onQrStateChanged(const QString &status, const QString &message);
    void onQrSucceeded();
    void onQrFailed(const QString &message);

    void updateCountdownLabel();
    void tickCountdown();

    /**
     * @brief 断开服务的信号并作废当前票据（关闭弹窗时唯一收尾口）
     *
     * 幂等：done() 之后析构还会再走一遍，重复调用不能重复发取消请求。
     */
    void releaseService();

    /** 二维码深色模块的颜色：主题色够深就用主题色，太浅退回近黑（不然扫不出来） */
    QColor qrDarkColor() const;

    /** 二维码渐变的终点色：主色压暗一档，让整张码有一点光感 */
    QColor qrEndColor() const;

    /** 手机上显示的设备名（告诉用户「是不是我自己在登录」） */
    QString makeDeviceName() const;

    QLabel *m_qrLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QLabel *m_urlLabel = nullptr;
    QLabel *m_countdownLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QTimer *m_countdownTimer = nullptr;
    QTimer *m_requestWatchdog = nullptr;

    ForumAccountService *m_service = nullptr; ///< 账号服务（持有期间才接它的扫码信号）
    QString m_deviceName;                     ///< 报给服务端的设备名
    QString m_scanUrl;                        ///< 当前二维码里编码的地址（空 = 还没有码）
    int m_remaining = 0;                      ///< 倒计时剩余秒数
    bool m_succeeded = false;                 ///< 是否已扫码登录成功（只用于关掉超时看门狗）
};
