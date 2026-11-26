/**
 * @file   QrLoginDialog.cpp
 * @brief  手机扫码登录弹窗实现
 * @author BlockBox Team
 * @date   2026-11-25
 */
#include "QrLoginDialog.h"

#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>

#include "utils/AppVersion.h"
#include "utils/ForumAccountService.h"
#include "utils/QrCode.h"
#include "utils/ThemeManager.h"

namespace
{

/** 二维码的逻辑边长：手机上一眼能对上焦，圆角模块和中间的小方块也看得清 */
constexpr int kQrSize = 268;

/** 二维码四周的白底留白：深色模式下这块白底正好把码和卡片背景分开 */
constexpr int kQrPadding = 8;

/**
 * @brief 等二维码的最长时间
 *
 * 服务端迟迟不给码时不能让弹窗停在「正在获取二维码…」，到点就摆出重试入口。
 */
constexpr int kRequestTimeoutMs = 20000;

/** 服务端没带有效期时按 2 分钟倒计时，免得进度永远显示不出来 */
constexpr int kFallbackExpiresIn = 120;

} // namespace

QrLoginDialog::QrLoginDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_service(ForumAccountService::instance())
{
    setObjectName(QStringLiteral("qrLoginDialog"));
    setWindowTitle(tr("手机扫码登录"));

    m_deviceName = makeDeviceName();

    initUI();
    initStyle();

    // 先连信号再申请二维码：万一服务端（或本地缓存）同步就把码回过来，
    // 也不会漏掉第一条 qrLoginStarted
    if (m_service)
    {
        connect(m_service, &ForumAccountService::qrLoginStarted, this,
                &QrLoginDialog::onQrLoginStarted);
        connect(m_service, &ForumAccountService::qrLoginStateChanged, this,
                &QrLoginDialog::onQrStateChanged);
        connect(m_service, &ForumAccountService::qrLoginSucceeded, this,
                &QrLoginDialog::onQrSucceeded);
        connect(m_service, &ForumAccountService::qrLoginFailed, this,
                &QrLoginDialog::onQrFailed);
    }

    beginQrLogin();
}

QrLoginDialog::~QrLoginDialog()
{
    // 兜底：正常关闭会先走 done()，只有被父窗口连带销毁时才需要在这里收尾
    releaseService();
}

void QrLoginDialog::initUI()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QWidget *card = new QWidget(this);
    // AppDialogBase 靠「直接子控件 objectName 以 Card 结尾」找居中卡片，
    // 名字不能随便改
    card->setObjectName(QStringLiteral("qrLoginCard"));
    card->setMinimumWidth(420);
    card->setMaximumWidth(560);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 24, 28, 22);
    cardLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel(windowTitle(), card);
    titleLabel->setObjectName(QStringLiteral("qrLoginTitle"));
    titleLabel->setWordWrap(true);
    titleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(titleLabel);

    QLabel *subtitleLabel =
        new QLabel(tr("用手机端方块盒子扫一扫这张二维码，在手机上确认后电脑端自动登录。"), card);
    subtitleLabel->setObjectName(QStringLiteral("qrLoginSubtitle"));
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(subtitleLabel);

    // 二维码：固定大小的白底方框，图片按 DPI 出图后居中贴进来
    m_qrLabel = new QLabel(card);
    m_qrLabel->setObjectName(QStringLiteral("qrLoginQr"));
    m_qrLabel->setFixedSize(kQrSize + kQrPadding * 2, kQrSize + kQrPadding * 2);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setWordWrap(true);
    m_qrLabel->setText(tr("正在获取二维码…"));
    cardLayout->addWidget(m_qrLabel, 0, Qt::AlignHCenter);

    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("qrLoginStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_statusLabel);

    // 状态行 / 第二行小字 / 地址行都是「显示之后才填字」的，字数多少不定。
    // 这里先按固定行数把高度占住：否则二维码一到、文字变长，卡片在显示后才变高，
    // 底部的倒计时和「取消」会被挤出可视区域（用户看不到按钮）。
    const int lineHeight = m_statusLabel->fontMetrics().lineSpacing();
    m_statusLabel->setFixedHeight(lineHeight * 2 + 4);

    // 第二行小字：扫码人昵称 / 服务端给的失败原因，没内容时留空（始终占一行高度）
    m_detailLabel = new QLabel(card);
    m_detailLabel->setObjectName(QStringLiteral("qrLoginDetail"));
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setAlignment(Qt::AlignCenter);
    m_detailLabel->setFixedHeight(lineHeight + 4);
    cardLayout->addWidget(m_detailLabel);

    // 二维码里的地址：排查问题（票据过期、域名不对）时能直接看到
    m_urlLabel = new QLabel(card);
    m_urlLabel->setObjectName(QStringLiteral("qrLoginUrl"));
    m_urlLabel->setWordWrap(true);
    m_urlLabel->setAlignment(Qt::AlignCenter);
    m_urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_urlLabel->setCursor(Qt::IBeamCursor);
    m_urlLabel->setFixedHeight(lineHeight * 2 + 4);
    cardLayout->addWidget(m_urlLabel);

    QHBoxLayout *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 4, 0, 0);
    bottomRow->setSpacing(10);

    m_countdownLabel = new QLabel(card);
    m_countdownLabel->setObjectName(QStringLiteral("qrLoginCountdown"));
    bottomRow->addWidget(m_countdownLabel, 1);

    // 「刷新二维码」只在过期 / 失败时露出来，平时不占视觉焦点
    m_refreshButton = new QPushButton(tr("刷新二维码"), card);
    m_refreshButton->setObjectName(QStringLiteral("qrLoginPrimary"));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->hide();
    connect(m_refreshButton, &QPushButton::clicked, this, &QrLoginDialog::beginQrLogin);
    bottomRow->addWidget(m_refreshButton, 0);

    QPushButton *cancelButton = new QPushButton(tr("取消"), card);
    cancelButton->setObjectName(QStringLiteral("qrLoginGhost"));
    cancelButton->setCursor(Qt::PointingHandCursor);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    bottomRow->addWidget(cancelButton, 0);

    cardLayout->addLayout(bottomRow);

    rootLayout->addWidget(card, 0, Qt::AlignCenter);

    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &QrLoginDialog::tickCountdown);

    m_requestWatchdog = new QTimer(this);
    m_requestWatchdog->setSingleShot(true);
    connect(m_requestWatchdog, &QTimer::timeout, this, [this]() {
        if (m_succeeded || !m_scanUrl.isEmpty())
            return;
        showFailure(tr("获取二维码超时，请点「刷新二维码」重试"));
    });

    // 上面几行文字都预留了固定高度，卡片内容的总高度是稳定的；
    // 直接把最小高度按 sizeHint 定住，AppDialogBase 就不会把卡片压得比内容还矮
    // （压矮了底部倒计时和「取消」会跑到可视区域外面）。
    // 屏幕实在太矮时 fitCardToScreen 会把它压回屏内并套一层滚动区，内容仍能看到。
    card->setMinimumHeight(card->sizeHint().height());
}

void QrLoginDialog::initStyle()
{
    // 配色与 AppMessageBox 同一套：卡片半透明深 / 浅底、圆角边框、主按钮用主题色，
    // 这样弹窗和软件里其它系统弹窗放在一起才是一致的
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();

    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg     = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#4a4a4a" : "#c9c9c9";
    const QString infoColor  = isLight ? "#8a8a8a" : "#909090";
    const QString btnBg      = isLight ? "#f5f5f5" : "#3b3b40";
    const QString btnBorder  = isLight ? "#d4d4d4" : "#55555a";
    const QString btnText    = isLight ? "#333333" : "#e8e8e8";

    setStyleSheet(QStringLiteral(R"QSS(
QDialog#qrLoginDialog { background-color: transparent; border: none; }
QWidget#qrLoginCard {
    background-color: %1;
    border: 1px solid %2;
    border-radius: 16px;
}
QLabel#qrLoginTitle {
    color: %3;
    background-color: transparent;
    font-size: 16px;
    font-weight: 700;
}
QLabel#qrLoginSubtitle {
    color: %5;
    background-color: transparent;
    font-size: 12.5px;
}
QLabel#qrLoginQr {
    background-color: #ffffff;
    border: 1px solid %2;
    border-radius: 12px;
    color: %5;
    font-size: 12.5px;
}
QLabel#qrLoginStatus {
    color: %4;
    background-color: transparent;
    font-size: 13px;
}
QLabel#qrLoginDetail {
    color: %5;
    background-color: transparent;
    font-size: 12px;
}
QLabel#qrLoginUrl {
    color: %5;
    background-color: transparent;
    font-size: 11px;
}
QLabel#qrLoginCountdown {
    color: %5;
    background-color: transparent;
    font-size: 12px;
}
QPushButton#qrLoginGhost {
    background-color: %6;
    border: 1px solid %7;
    border-radius: 12px;
    color: %8;
    padding: 7px 22px;
    font-size: 13px;
}
QPushButton#qrLoginGhost:hover { background-color: rgba(0, 0, 0, 28); }
QPushButton#qrLoginPrimary {
    background-color: %9;
    border: 1px solid %9;
    border-radius: 12px;
    color: #ffffff;
    padding: 7px 18px;
    font-size: 13px;
    font-weight: 600;
}
QPushButton#qrLoginPrimary:hover { background-color: %10; border-color: %10; }
)QSS")
                        .arg(cardBg, cardBorder, titleColor, textColor, infoColor, btnBg, btnBorder,
                             btnText, themeColor, themeHover));
}

QString QrLoginDialog::makeDeviceName() const
{
    // 手机上会原样显示这句话，用来让用户确认「是不是我自己在这台电脑上登录」。
    // 只报系统名 + 软件名 + 版本号，不把主机名之类更私密的信息传出去。
    QString system = QSysInfo::prettyProductName().trimmed();
    if (system.isEmpty())
        system = QSysInfo::kernelType().trimmed();   // 精简发行版上可能拿不到完整产品名
    if (system.isEmpty())
        system = tr("电脑");
    return tr("%1 · 方块盒子 %2").arg(system, AppVersion::current());
}

void QrLoginDialog::beginQrLogin()
{
    if (!m_service)
        return;

    m_succeeded = false;
    m_scanUrl.clear();
    m_remaining = 0;
    m_countdownTimer->stop();

    // 先把上一张码清掉（刷新时尤甚）：旧图留着容易被再去扫一遍
    m_qrLabel->setPixmap(QPixmap());
    m_qrLabel->setText(tr("正在获取二维码…"));
    m_statusLabel->setText(tr("正在获取二维码…"));
    m_detailLabel->clear();
    m_urlLabel->clear();
    m_countdownLabel->clear();
    m_refreshButton->hide();

    m_requestWatchdog->start(kRequestTimeoutMs);
    m_service->startQrLogin(m_deviceName);
}

void QrLoginDialog::onQrLoginStarted(const QString &ticket, const QString &scanUrl, int expiresIn)
{
    Q_UNUSED(ticket);   // 票据由服务自己拿去轮询，界面不需要

    m_requestWatchdog->stop();
    m_scanUrl = scanUrl;
    m_remaining = expiresIn > 0 ? expiresIn : kFallbackExpiresIn;

    if (!applyQrImage())
        return;   // 画不出来时 applyQrImage 已经把失败态摆好了

    m_statusLabel->setText(tr("请打开手机端方块盒子，点击右上角扫描图标扫描二维码"));
    m_detailLabel->clear();

    m_urlLabel->setText(scanUrl);
    m_urlLabel->setToolTip(scanUrl);

    m_refreshButton->hide();
    updateCountdownLabel();
    m_countdownTimer->start();
}

bool QrLoginDialog::applyQrImage()
{
    if (m_scanUrl.isEmpty())
    {
        showFailure(tr("这张二维码没有加载成功"));
        return false;
    }

    // 出图比逻辑尺寸大一档：圆角模块和中间那枚小方块要在小图上照样清楚，
    // 相机取景时也更容易对上模块边界。低分屏也至少按 420px 画，再缩到卡片上。
    const qreal ratio = qMax<qreal>(1.0, devicePixelRatioF());
    const int pixelSize = qMax(420, qRound(kQrSize * ratio));

    QrCode::Style style;
    style.light = QColor(0xFF, 0xFF, 0xFF);
    // 白底深模块是扫码的硬要求，不跟随深色模式：卡片配色变了也不该影响识别率
    style.dark = qrDarkColor();
    style.dark2 = qrEndColor();
    style.moduleRadius = 0.34;
    style.eyeRadius = 0.36;
    style.emblemModules = 7; // 中间放一枚方块盒子的小方块（码太小 / 版本太低时自动不加）

    QString error;
    const QImage image = QrCode::renderStyled(m_scanUrl, pixelSize, style, &error);
    if (image.isNull())
    {
        // 编码失败（地址太长等）：把技术原因留在提示里，便于排查
        m_qrLabel->setToolTip(error);
        showFailure(tr("这张二维码没有加载成功"));
        return false;
    }

    m_qrLabel->setToolTip(QString());
    QPixmap pixmap = QPixmap::fromImage(image);
    // 逻辑尺寸仍是 kQrSize：多出来的像素按比例声明成高分屏素材，贴上去不会变形
    pixmap.setDevicePixelRatio(double(pixelSize) / double(kQrSize));
    m_qrLabel->setPixmap(pixmap);
    return true;
}

void QrLoginDialog::onQrStateChanged(const QString &status, const QString &message)
{
    if (status == QLatin1String("pending"))
    {
        m_statusLabel->setText(tr("请打开手机端方块盒子，点击右上角扫描图标扫描二维码"));
        m_detailLabel->clear();
        m_refreshButton->hide();
        // 服务端每次轮询回报 pending 都会进来，计时器已经在走就别重开，
        // 否则倒计时会被一轮轮重启拖慢
        if (m_remaining > 0 && !m_countdownTimer->isActive())
            m_countdownTimer->start();
        return;
    }

    if (status == QLatin1String("scanned"))
    {
        m_statusLabel->setText(tr("已扫码，请在手机上确认登录"));
        // message 是扫码人的昵称：一起显示，方便确认是不是自己扫的
        const QString nickname = message.trimmed();
        if (nickname.isEmpty())
        {
            m_detailLabel->clear();
        }
        else
        {
            m_detailLabel->setText(tr("扫码人：%1").arg(nickname));
        }
        return;
    }

    if (status == QLatin1String("expired"))
    {
        showFailure(message);
        return;
    }

    // 其它未知状态先不动界面：文案停在上一次的状态，总比突然变空白好
}

void QrLoginDialog::onQrSucceeded()
{
    m_succeeded = true;
    m_countdownTimer->stop();
    m_requestWatchdog->stop();
    // 登录成功直接关窗；账户页靠现有的 userChanged 逻辑自己刷新，这里不插手
    accept();
}

void QrLoginDialog::onQrFailed(const QString &message)
{
    showFailure(message);
}

void QrLoginDialog::showFailure(const QString &detail)
{
    m_countdownTimer->stop();
    m_remaining = 0;
    m_countdownLabel->clear();

    // 已经作废的码不能留着让人扫：清掉图，换成一句「去哪儿点」的提示
    m_qrLabel->setPixmap(QPixmap());
    m_qrLabel->setText(tr("点「刷新二维码」重新获取"));

    m_statusLabel->setText(tr("二维码已过期"));
    const QString reason = detail.trimmed();
    if (reason.isEmpty())
    {
        m_detailLabel->clear();
    }
    else
    {
        m_detailLabel->setText(reason);
    }

    m_refreshButton->show();
}

void QrLoginDialog::updateCountdownLabel()
{
    if (m_remaining <= 0)
    {
        m_countdownLabel->clear();
        return;
    }

    // 分:秒，两位对齐，倒计时越长也不会跳宽度
    const QString remain = QStringLiteral("%1:%2")
                               .arg(m_remaining / 60, 2, 10, QLatin1Char('0'))
                               .arg(m_remaining % 60, 2, 10, QLatin1Char('0'));
    m_countdownLabel->setText(tr("二维码有效期还剩 %1").arg(remain));
}

void QrLoginDialog::tickCountdown()
{
    if (m_remaining <= 0)
    {
        m_countdownTimer->stop();
        return;
    }

    --m_remaining;
    if (m_remaining > 0)
    {
        updateCountdownLabel();
        return;
    }

    // 本地倒计时归零就先摆出过期态，不等服务端下一次轮询回报：
    // 用户点「刷新二维码」马上就能拿到新码
    m_countdownTimer->stop();
    showFailure(QString());
}

void QrLoginDialog::releaseService()
{
    if (!m_service)
        return;

    // 用 this 当上下文连的信号，这里统一断开：弹窗关了就不该再收扫码状态
    disconnect(m_service, nullptr, this, nullptr);
    m_countdownTimer->stop();
    m_requestWatchdog->stop();

    // 关窗即放弃：不论用户是点取消、点遮罩、按 Esc，还是刚登录成功，
    // 都让服务端把这张码收掉，别留着一轮没人关心的轮询
    m_service->cancelQrLogin();

    m_service = nullptr;   // 只收一次尾
}

QColor QrLoginDialog::qrDarkColor() const
{
    // 二维码要「深色模块 + 白底」才扫得稳。主题色够深就直接用它，颜色跟软件一致；
    // 主题色偏浅（黄 / 浅青）时退回近黑，否则相机对比度不够，扫码成功率会掉
    const QColor accent(ThemeManager::instance()->currentThemeColor());
    if (accent.isValid())
    {
        const qreal luminance =
            0.2126 * accent.redF() + 0.7152 * accent.greenF() + 0.0722 * accent.blueF();
        if (luminance <= 0.42)
            return accent;
    }
    return QColor(0x1A, 0x1A, 0x1A);
}

QColor QrLoginDialog::qrEndColor() const
{
    // 渐变终点：主色压暗一档、色相稍微往青绿偏一点，
    // 整张码看起来像「有光从左上方打过来」，但又不会花到影响识别
    QColor end = qrDarkColor();
    end = end.darker(140);
    if (end.hue() >= 0)
    {
        int hue = (end.hue() + 10) % 360;
        end.setHsv(hue, end.saturation(), end.value());
    }
    return end;
}

void QrLoginDialog::done(int result)
{
    // 无论从哪条路关掉（取消按钮 / 点遮罩 / Esc / 登录成功 accept），
    // 都要在这里把票据与信号一起收掉
    releaseService();
    AppDialogBase::done(result);
}
