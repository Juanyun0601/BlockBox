/**
 * @file   QrScanPage.h
 * @brief  手机端扫码登录电脑端：整页相机取景 → 识别二维码 → 手机上确认
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 电脑端登录页显示登录二维码，手机端点顶栏的「扫描」进到这一页，用相机对准二维码：
 *   - 识别到登录码后立刻停掉相机，向论坛核验这张码（顺带把电脑端标成「已扫码」）；
 *   - 核验通过后在页面上展示「确认登录 / 取消」，用户点完把结果提交给服务端；
 *   - 完成或取消后自动返回进入前的页面（发 backRequested，由主窗口处理）。
 *
 * 相机只在带 BLOCKBOX_HAS_QR_CAMERA 的构建（安卓）里编译；桌面端这一页仍然
 * 能打开，只是没有取景，改为「直接输入登录码」的兜底入口，方便验证整条链路。
 */
#ifndef QRSCANPAGE_H
#define QRSCANPAGE_H

#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QShowEvent;
class QHideEvent;

#ifdef BLOCKBOX_HAS_QR_CAMERA
// 相机相关类型只在安卓构建里出现，桌面端不包含任何 QtMultimedia 头文件
class QCamera;
class QMediaCaptureSession;
class QVideoSink;
class QVideoWidget;
class QElapsedTimer;
class QVideoFrame;
#endif

class QrScanPage : public QWidget
{
    Q_OBJECT

public:
    explicit QrScanPage(QWidget *parent = nullptr);
    ~QrScanPage() override;

    /**
     * @brief 重新开始一次扫码（进入页面时、点「重新扫描」时、已在页面上再次点顶栏扫描时）
     *
     * 会清掉上一次的票据与确认面板、重新申请相机并开始取景。
     */
    void startScanning();

signals:
    /** 流程结束（已确认 / 已取消）或用户主动离开：由主窗口切回进入前的页面 */
    void backRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    /** 服务端核验通过：展示电脑设备名与 IP，等用户确认 */
    void onQrTicketScanned(const QString &ticket, const QJsonObject &info);
    /** 确认 / 取消已提交 */
    void onQrTicketAnswered(const QString &ticket, bool approved);
    /** 核验或提交失败：显示原因并给「重新扫描」 */
    void onQrTicketFailed(const QString &message);
    void onApproveClicked();
    void onCancelClicked();
    void onRetryClicked();
    /** 兜底入口：手动粘贴登录码后提交 */
    void onManualSubmit();

private:
    void initUI();
    void applyStyle();
    void setStatus(const QString &text, bool error = false);
    /** 相机不可用 / 权限被拒：中央提示 + 保留手动输入入口 */
    void showNoCameraHint(const QString &text);
    /** 识别出来的文本：判断是不是登录码，是就进入核验流程 */
    void handleDecodedText(const QString &text);
    /** 拿到票据后的统一入口（相机扫到的、手动输入的都走这里） */
    void beginTicket(const QString &ticket);
    /** 恢复成「可以扫码」的界面状态 */
    void resetToScanningUi();

#ifdef BLOCKBOX_HAS_QR_CAMERA
    /** 检查 / 申请相机权限，拿到权限再真正打开相机 */
    void startCamera();
    /** 权限已就绪：建相机与会话、开始取景 */
    void openCamera();
    /** 停掉并释放相机（离开页面、识别成功后都要调） */
    void stopCamera();
    /** 取一帧交给识别器（内部按约 250ms 节流） */
    void onCameraFrame(const QVideoFrame &frame);

    QCamera *m_camera = nullptr;
    QMediaCaptureSession *m_captureSession = nullptr;
    /** 取帧用的 sink：复用预览控件内部的 sink（会话只支持一个预览输出） */
    QVideoSink *m_videoSink = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
    /** 取帧节流计时器：相机帧率远高于识别需要，约 250ms 取一帧就够 */
    QElapsedTimer *m_frameClock = nullptr;
#endif

    QWidget *m_card = nullptr;
    QLabel *m_hintLabel = nullptr;       ///< 预览区中央提示（相机不可用 / 请确认）
    QLabel *m_statusLabel = nullptr;     ///< 状态行：正在核验 / 不是登录码 / 失败原因
    QWidget *m_confirmPanel = nullptr;   ///< 确认面板（核验通过后出现）
    QLabel *m_confirmDesc = nullptr;
    QPushButton *m_approveBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QLineEdit *m_manualEdit = nullptr;
    QPushButton *m_manualSubmitBtn = nullptr;
    QTimer *m_autoBackTimer = nullptr;   ///< 完成后 1~2 秒自动返回上一页

    /** 当前正在核验 / 等待确认的票据（空 = 没在流程里） */
    QString m_currentTicket;
    /** 是否已经进入核验 / 确认流程（期间不再识别新帧） */
    bool m_busy = false;
};

#endif // QRSCANPAGE_H
