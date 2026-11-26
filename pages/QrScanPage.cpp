/**
 * @file   QrScanPage.cpp
 * @brief  手机端扫码登录电脑端：整页相机取景 → 识别二维码 → 手机上确认
 * @author BlockBox Team
 * @date   2026-11-25
 */
#include "QrScanPage.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "platform.h"
#include "utils/ForumAccountService.h"
#include "utils/QrCode.h"
#include "utils/ThemeManager.h"

#ifdef BLOCKBOX_HAS_QR_CAMERA
#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPermissions>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#endif

namespace {
/** 取帧识别节流间隔：相机帧率远高于识别需要，约 4 帧/秒足够扫到二维码 */
constexpr int kFrameIntervalMs = 250;
/** 确认 / 取消之后停留多久自动返回上一页 */
constexpr int kAutoBackDelayMs = 1600;
} // namespace

QrScanPage::QrScanPage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
    applyStyle();

    m_autoBackTimer = new QTimer(this);
    m_autoBackTimer->setSingleShot(true);
    connect(m_autoBackTimer, &QTimer::timeout, this, [this]() { emit backRequested(); });

    // 扫码登录的手机端信号：服务是全局单例，进来一次接一次即可（页面随主窗口常驻）
    ForumAccountService *service = ForumAccountService::instance();
    connect(service, &ForumAccountService::qrTicketScanned,
            this, &QrScanPage::onQrTicketScanned);
    connect(service, &ForumAccountService::qrTicketAnswered,
            this, &QrScanPage::onQrTicketAnswered);
    connect(service, &ForumAccountService::qrTicketFailed,
            this, &QrScanPage::onQrTicketFailed);
}

QrScanPage::~QrScanPage()
{
#ifdef BLOCKBOX_HAS_QR_CAMERA
    stopCamera();
#endif
}

// ───────────────────────────── UI ─────────────────────────────

void QrScanPage::initUI()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 14, 20, 14);
    root->setSpacing(10);

    QLabel *title = new QLabel(tr("扫描登录二维码"), this);
    title->setObjectName(QStringLiteral("qrTitle"));
    root->addWidget(title);

    QLabel *desc = new QLabel(
        tr("用这台手机对准电脑上显示的登录二维码，识别后在手机上点「确认登录」，"
           "电脑端就会自动登录。"),
        this);
    desc->setObjectName(QStringLiteral("qrDesc"));
    desc->setWordWrap(true);
    root->addWidget(desc);

    // 主卡片：相机预览 + 状态行 + 确认面板都在这里，样式与其它整页页面一致
    QFrame *card = new QFrame(this);
    card->setObjectName(QStringLiteral("qrCard"));
    m_card = card;
    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    cardLayout->setSpacing(10);

#ifdef BLOCKBOX_HAS_QR_CAMERA
    m_videoWidget = new QVideoWidget(card);
    m_videoWidget->setObjectName(QStringLiteral("qrVideo"));
    m_videoWidget->setMinimumHeight(180);
    m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    m_videoWidget->hide();   // 相机真正起来之后再显示，避免闪一块黑
    cardLayout->addWidget(m_videoWidget, 1);
#endif

    m_hintLabel = new QLabel(card);
    m_hintLabel->setObjectName(QStringLiteral("qrHint"));
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setMinimumHeight(140);
    cardLayout->addWidget(m_hintLabel, 1);

    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("qrStatus"));
    m_statusLabel->setWordWrap(true);
    cardLayout->addWidget(m_statusLabel);

    // 确认面板：核验通过后才显示，把「哪台电脑要登录」讲清楚
    m_confirmPanel = new QWidget(card);
    QVBoxLayout *confirmLayout = new QVBoxLayout(m_confirmPanel);
    confirmLayout->setContentsMargins(0, 0, 0, 0);
    confirmLayout->setSpacing(8);

    QLabel *confirmTitle = new QLabel(tr("确认登录方块盒子？"), m_confirmPanel);
    confirmTitle->setObjectName(QStringLiteral("qrConfirmTitle"));
    confirmLayout->addWidget(confirmTitle);

    m_confirmDesc = new QLabel(m_confirmPanel);
    m_confirmDesc->setObjectName(QStringLiteral("qrConfirmDesc"));
    m_confirmDesc->setWordWrap(true);
    confirmLayout->addWidget(m_confirmDesc);

    QHBoxLayout *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);

    m_approveBtn = new QPushButton(tr("确认登录"), m_confirmPanel);
    m_approveBtn->setObjectName(QStringLiteral("qrPrimaryBtn"));
    m_approveBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn = new QPushButton(tr("取消"), m_confirmPanel);
    m_cancelBtn->setObjectName(QStringLiteral("qrGhostBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);

    buttonRow->addWidget(m_approveBtn);
    buttonRow->addWidget(m_cancelBtn);
    buttonRow->addStretch();
    confirmLayout->addLayout(buttonRow);

    m_confirmPanel->hide();
    cardLayout->addWidget(m_confirmPanel);

    m_retryBtn = new QPushButton(tr("重新扫描"), card);
    m_retryBtn->setObjectName(QStringLiteral("qrGhostBtn"));
    m_retryBtn->setCursor(Qt::PointingHandCursor);
    m_retryBtn->hide();
    cardLayout->addWidget(m_retryBtn, 0, Qt::AlignLeft);

    root->addWidget(card, 1);

    // 兜底入口：没有相机 / 权限没给 / 想手动输票据时都能用，桌面端靠它验证整条链路
    QLabel *manualTitle = new QLabel(tr("没扫到？直接输入登录码"), this);
    manualTitle->setObjectName(QStringLiteral("qrManualTitle"));
    root->addWidget(manualTitle);

    QHBoxLayout *manualRow = new QHBoxLayout();
    manualRow->setContentsMargins(0, 0, 0, 0);
    manualRow->setSpacing(8);

    m_manualEdit = new QLineEdit(this);
    m_manualEdit->setObjectName(QStringLiteral("qrManualEdit"));
    m_manualEdit->setPlaceholderText(tr("粘贴电脑端登录二维码里的登录码"));
    m_manualEdit->setClearButtonEnabled(true);

    m_manualSubmitBtn = new QPushButton(tr("提交"), this);
    m_manualSubmitBtn->setObjectName(QStringLiteral("qrPrimaryBtn"));
    m_manualSubmitBtn->setCursor(Qt::PointingHandCursor);

    manualRow->addWidget(m_manualEdit, 1);
    manualRow->addWidget(m_manualSubmitBtn);
    root->addLayout(manualRow);

    connect(m_approveBtn, &QPushButton::clicked, this, &QrScanPage::onApproveClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QrScanPage::onCancelClicked);
    connect(m_retryBtn, &QPushButton::clicked, this, &QrScanPage::onRetryClicked);
    connect(m_manualSubmitBtn, &QPushButton::clicked, this, &QrScanPage::onManualSubmit);
    connect(m_manualEdit, &QLineEdit::returnPressed, this, &QrScanPage::onManualSubmit);
}

void QrScanPage::applyStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString themeColor = tm->currentThemeColor();

    const QString textColor = isLight ? QStringLiteral("#333333") : QStringLiteral("#e8e8e8");
    const QString subColor = isLight ? QStringLiteral("#64748B") : QStringLiteral("#9aa3b2");
    const QString mutedColor = isLight ? QStringLiteral("#94A3B8") : QStringLiteral("#7a8290");
    const QString cardBg = isLight ? QStringLiteral("#ffffff") : QStringLiteral("#2f2f33");
    const QString cardBorder = isLight ? QStringLiteral("#e2e2e6") : QStringLiteral("#47474d");
    const QString fieldBg = isLight ? QStringLiteral("#f4f4f4") : QStringLiteral("#3b3b40");
    const QString fieldBorder = isLight ? QStringLiteral("#d4d4d4") : QStringLiteral("#55555a");
    const QString hoverBg = isLight ? QStringLiteral("rgba(15,23,42,0.06)")
                                    : QStringLiteral("rgba(255,255,255,0.08)");
    const QString primaryHover = tm->getThemeColorHover();
    const QString primaryPressed = tm->getThemeColorPressed();

    const QString style = QStringLiteral(
        "QLabel#qrTitle { color: %1; font-size: 18px; font-weight: 700;"
        " background: transparent; }"
        "QLabel#qrDesc { color: %2; font-size: 12.5px; background: transparent; }"
        "QLabel#qrManualTitle { color: %2; font-size: 12.5px; background: transparent; }"
        "QFrame#qrCard { background-color: %3; border: 1px solid %4; border-radius: 12px; }"
        "QLabel#qrHint { color: %5; font-size: 13px; background: transparent; }"
        "QLabel#qrConfirmTitle { color: %1; font-size: 16px; font-weight: 700;"
        " background: transparent; }"
        "QLabel#qrConfirmDesc { color: %2; font-size: 12.5px; background: transparent; }"
        "QLabel#qrStatus { color: %5; font-size: 12.5px; background: transparent; }"
        "QLabel#qrStatus[error=\"true\"] { color: #e5484d; }"
        "QPushButton#qrPrimaryBtn { background-color: %6; border: none; border-radius: 8px;"
        " color: #ffffff; padding: 8px 18px; font-size: 12.5px; font-weight: 600; }"
        "QPushButton#qrPrimaryBtn:hover { background-color: %7; }"
        "QPushButton#qrPrimaryBtn:pressed { background-color: %8; }"
        "QPushButton#qrPrimaryBtn:disabled { background-color: %4; color: %5; }"
        "QPushButton#qrGhostBtn { background-color: transparent; border: 1px solid %4;"
        " border-radius: 8px; color: %2; padding: 8px 16px; font-size: 12.5px; }"
        "QPushButton#qrGhostBtn:hover { background-color: %9; color: %1; }"
        "QLineEdit#qrManualEdit { background-color: %10; border: 1px solid %11;"
        " border-radius: 8px; color: %1; padding: 7px 10px; font-size: 12.5px; }"
        "QLineEdit#qrManualEdit:focus { border-color: %6; }"
    ).arg(textColor, subColor, cardBg, cardBorder, mutedColor, themeColor,
          primaryHover, primaryPressed, hoverBg, fieldBg, fieldBorder);

    setStyleSheet(style);
}

void QrScanPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("error", error);
    // 动态属性换了要重新 polish 一次，错误色才会生效
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void QrScanPage::showNoCameraHint(const QString &text)
{
    if (m_hintLabel) {
        m_hintLabel->setText(text);
        m_hintLabel->show();
    }
#ifdef BLOCKBOX_HAS_QR_CAMERA
    if (m_videoWidget)
        m_videoWidget->hide();
#endif
}

void QrScanPage::resetToScanningUi()
{
    m_busy = false;
    m_currentTicket.clear();
    if (m_autoBackTimer)
        m_autoBackTimer->stop();
    if (m_confirmPanel)
        m_confirmPanel->hide();
    if (m_retryBtn)
        m_retryBtn->hide();
    if (m_manualEdit)
        m_manualEdit->setEnabled(true);
    if (m_manualSubmitBtn)
        m_manualSubmitBtn->setEnabled(true);
    if (m_approveBtn)
        m_approveBtn->setEnabled(true);
    if (m_cancelBtn)
        m_cancelBtn->setEnabled(true);
}

// ───────────────────────────── 页面生命周期 ─────────────────────────────

void QrScanPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 主题可能在两次进入之间被换过，进来先按当前主题刷一遍样式
    applyStyle();
    startScanning();
}

void QrScanPage::hideEvent(QHideEvent *event)
{
    // 离开页面必须把相机停掉并释放，不让它在后台占着摄像头
    m_autoBackTimer->stop();
#ifdef BLOCKBOX_HAS_QR_CAMERA
    stopCamera();
#endif
    m_busy = false;
    m_currentTicket.clear();
    QWidget::hideEvent(event);
}

// ───────────────────────────── 扫码主流程 ─────────────────────────────

void QrScanPage::startScanning()
{
    resetToScanningUi();
    if (m_manualEdit)
        m_manualEdit->clear();
    if (m_hintLabel)
        m_hintLabel->setText(tr("正在准备相机…"));

#ifdef BLOCKBOX_HAS_QR_CAMERA
    // 只有手机端才有取景可用；桌面端构建即便定义了宏也不会走到这里（Platform::isMobile 为 false）
    if (Platform::isMobile()) {
        setStatus(tr("把电脑上的登录二维码放进取景框，识别到就会自动停下。"));
        startCamera();
        return;
    }
    showNoCameraHint(tr("这台设备没有可用的相机，可以在下面直接输入登录码。"));
    setStatus(QString());
#else
    showNoCameraHint(tr("当前构建不支持相机扫码，可以在下面直接输入登录码。"));
    setStatus(QString());
#endif
}

void QrScanPage::handleDecodedText(const QString &text)
{
    if (text.isEmpty() || m_busy)
        return;

    if (!QrCode::isLoginPayload(text)) {
        // 不是登录码：提示一句继续扫，不打断相机
        setStatus(tr("这不是方块盒子的登录二维码"), true);
        return;
    }

    const QString ticket = QrCode::ticketFromPayload(text);
    if (ticket.isEmpty()) {
        setStatus(tr("这不是方块盒子的登录二维码"), true);
        return;
    }
    beginTicket(ticket);
}

void QrScanPage::beginTicket(const QString &ticket)
{
    if (m_busy || ticket.isEmpty())
        return;

    m_busy = true;
    m_currentTicket = ticket;

    // 已经拿到票据：先停相机（省电、也避免重复识别），再交给服务端核验
#ifdef BLOCKBOX_HAS_QR_CAMERA
    stopCamera();
    if (m_videoWidget)
        m_videoWidget->hide();
#endif
    if (m_confirmPanel)
        m_confirmPanel->hide();
    if (m_retryBtn)
        m_retryBtn->hide();
    if (m_manualEdit)
        m_manualEdit->setEnabled(false);
    if (m_manualSubmitBtn)
        m_manualSubmitBtn->setEnabled(false);
    if (m_hintLabel) {
        m_hintLabel->setText(tr("正在核对这张登录码…"));
        m_hintLabel->show();
    }
    setStatus(tr("正在核验，请稍等…"));

    ForumAccountService::instance()->scanQrTicket(ticket);
}

void QrScanPage::onQrTicketScanned(const QString &ticket, const QJsonObject &info)
{
    if (!m_busy || ticket != m_currentTicket)
        return;

    QString device = info.value(QStringLiteral("device")).toString().trimmed();
    const QString ip = info.value(QStringLiteral("ip")).toString().trimmed();
    if (device.isEmpty())
        device = tr("未知设备");

    // 说清是哪台电脑要登录，并提醒不是本人就取消
    if (ip.isEmpty()) {
        m_confirmDesc->setText(
            tr("这台电脑将以你的账号登录：%1。\n如果这不是你本人的操作，请点取消").arg(device));
    } else {
        m_confirmDesc->setText(
            tr("这台电脑将以你的账号登录：%1（IP: %2）。\n如果这不是你本人的操作，请点取消")
                .arg(device, ip));
    }

    if (m_hintLabel) {
        m_hintLabel->setText(tr("请确认是不是你本人在登录"));
        m_hintLabel->show();
    }
    setStatus(tr("确认后，这台电脑就会用你的账号登录。"));
    if (m_confirmPanel)
        m_confirmPanel->show();
}

void QrScanPage::onQrTicketAnswered(const QString &ticket, bool approved)
{
    if (!m_busy)
        return;
    if (!ticket.isEmpty() && ticket != m_currentTicket)
        return;

    m_busy = false;
    m_currentTicket.clear();
    if (m_confirmPanel)
        m_confirmPanel->hide();
    if (m_retryBtn)
        m_retryBtn->hide();
#ifdef BLOCKBOX_HAS_QR_CAMERA
    if (m_videoWidget)
        m_videoWidget->hide();
#endif
    if (m_hintLabel) {
        // 结果直接大字显示在卡片中央，状态行不再重复一遍
        m_hintLabel->setText(approved ? tr("已确认，电脑端马上就会登录") : tr("已取消"));
        m_hintLabel->show();
    }
    setStatus(QString());

    // 停留一会儿让用户看清结果，然后自动回到进入前的页面
    m_autoBackTimer->start(kAutoBackDelayMs);
}

void QrScanPage::onQrTicketFailed(const QString &message)
{
    if (!m_busy)
        return;

    m_busy = false;
    m_currentTicket.clear();
    if (m_confirmPanel)
        m_confirmPanel->hide();
    if (m_manualEdit)
        m_manualEdit->setEnabled(true);
    if (m_manualSubmitBtn)
        m_manualSubmitBtn->setEnabled(true);
    if (m_approveBtn)
        m_approveBtn->setEnabled(true);
    if (m_cancelBtn)
        m_cancelBtn->setEnabled(true);
    if (m_hintLabel) {
        m_hintLabel->setText(tr("没能完成这次扫码登录"));
        m_hintLabel->show();
    }
    setStatus(message.isEmpty() ? tr("扫码登录失败，请重试") : message, true);
    if (m_retryBtn)
        m_retryBtn->show();
}

void QrScanPage::onApproveClicked()
{
    if (!m_busy || m_currentTicket.isEmpty())
        return;
    m_approveBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);
    setStatus(tr("正在提交…"));
    ForumAccountService::instance()->answerQrTicket(m_currentTicket, true);
}

void QrScanPage::onCancelClicked()
{
    if (!m_busy || m_currentTicket.isEmpty())
        return;
    m_approveBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);
    setStatus(tr("正在提交…"));
    ForumAccountService::instance()->answerQrTicket(m_currentTicket, false);
}

void QrScanPage::onRetryClicked()
{
    startScanning();
}

void QrScanPage::onManualSubmit()
{
    if (m_busy)
        return;

    const QString input = m_manualEdit->text().trimmed();
    if (input.isEmpty()) {
        setStatus(tr("先粘贴登录码再提交"), true);
        return;
    }

    // 允许直接贴整段登录码（含协议头），也允许只贴票据本身
    QString ticket = input;
    if (QrCode::isLoginPayload(input))
        ticket = QrCode::ticketFromPayload(input);
    if (ticket.isEmpty()) {
        setStatus(tr("这不是方块盒子的登录二维码"), true);
        return;
    }
    beginTicket(ticket);
}

// ───────────────────────────── 相机（仅安卓构建） ─────────────────────────────

#ifdef BLOCKBOX_HAS_QR_CAMERA

void QrScanPage::startCamera()
{
    if (!m_videoWidget) {
        showNoCameraHint(tr("这台设备没有可用的相机，可以在下面直接输入登录码。"));
        return;
    }

    const QCameraPermission permission;
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Granted:
        openCamera();
        break;
    case Qt::PermissionStatus::Denied:
        // 已经被拒绝过：不能再弹系统框，只能引导用户去设置里打开
        showNoCameraHint(tr("请在系统设置里允许方块盒子使用相机"));
        setStatus(tr("没有相机权限，扫码不可用；也可以在下面直接输入登录码。"), true);
        break;
    case Qt::PermissionStatus::Undetermined:
        setStatus(tr("正在请求相机权限…"));
        if (m_hintLabel)
            m_hintLabel->setText(tr("正在请求相机权限…"));
        qApp->requestPermission(permission, this, [this](const QPermission &result) {
            if (result.status() == Qt::PermissionStatus::Granted) {
                openCamera();
            } else {
                showNoCameraHint(tr("请在系统设置里允许方块盒子使用相机"));
                setStatus(tr("没有相机权限，扫码不可用；也可以在下面直接输入登录码。"), true);
            }
        });
        break;
    }
}

void QrScanPage::openCamera()
{
    const QCameraDevice device = QMediaDevices::defaultVideoInput();
    if (device.isNull()) {
        showNoCameraHint(tr("没找到可用的相机，可以在下面直接输入登录码。"));
        return;
    }

    stopCamera();   // 先收掉上一次的会话，避免重复打开

    if (!m_frameClock)
        m_frameClock = new QElapsedTimer();

    m_camera = new QCamera(device, this);
    m_captureSession = new QMediaCaptureSession(this);
    m_captureSession->setCamera(m_camera);

    // 预览走 QVideoWidget；取帧复用同一个视频流——会话只有一个预览输出，
    // QVideoWidget 内部本来就有 QVideoSink，直接接它的 sink 拿原始帧最省事
    m_captureSession->setVideoOutput(m_videoWidget);
    m_videoSink = m_videoWidget->videoSink();
    if (m_videoSink) {
        connect(m_videoSink, &QVideoSink::videoFrameChanged,
                this, [this](const QVideoFrame &frame) { onCameraFrame(frame); });
    }

    connect(m_camera, &QCamera::errorOccurred, this,
            [this](QCamera::Error error, const QString &message) {
                if (error == QCamera::NoError)
                    return;
                // 相机打不开（被别的应用占用 / 设备异常）：给出原因，兜底入口照常可用
                showNoCameraHint(tr("相机打不开：%1").arg(message));
                setStatus(tr("相机打不开，可以在下面直接输入登录码。"), true);
            });

    m_frameClock->start();
    if (m_videoWidget)
        m_videoWidget->show();
    if (m_hintLabel)
        m_hintLabel->hide();
    m_camera->start();
    setStatus(tr("把电脑上的登录二维码放进取景框，识别到就会自动停下。"));
}

void QrScanPage::stopCamera()
{
    if (m_videoSink) {
        // 页面常驻、相机会反复起停，先断掉上一轮的取帧连接，避免重复触发
        disconnect(m_videoSink, nullptr, this, nullptr);
        m_videoSink = nullptr;
    }
    if (m_captureSession) {
        m_captureSession->setCamera(nullptr);
        m_captureSession->setVideoOutput(nullptr);
        // 用 deleteLater：可能在相机自己的回调里走到这里，直接 delete 会销毁发信号的源头
        m_captureSession->deleteLater();
        m_captureSession = nullptr;
    }
    if (m_camera) {
        m_camera->stop();
        m_camera->deleteLater();
        m_camera = nullptr;
    }
    if (m_videoWidget)
        m_videoWidget->hide();
    if (m_frameClock)
        m_frameClock->invalidate();
}

void QrScanPage::onCameraFrame(const QVideoFrame &frame)
{
    if (m_busy || !m_frameClock)
        return;
    // 节流：约每 250ms 才做一次识别，省电也避免识别跟不上把界面卡住
    if (m_frameClock->elapsed() < kFrameIntervalMs)
        return;
    m_frameClock->restart();

    if (!frame.isValid())
        return;
    const QImage image = frame.toImage();
    if (image.isNull())
        return;

    const QString text = QrCode::decode(image);
    if (text.isEmpty())
        return;
    handleDecodedText(text);
}

#endif // BLOCKBOX_HAS_QR_CAMERA
