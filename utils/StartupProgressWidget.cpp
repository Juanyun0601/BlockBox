/**
 * @file   StartupProgressWidget.cpp
 * @brief  启动进度窗口实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "StartupProgressWidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QScreen>

#include "utils/ThemeManager.h"

// 静态成员初始化
StartupProgressWidget* StartupProgressWidget::m_instance = nullptr;
QMutex StartupProgressWidget::m_mutex;

StartupProgressWidget::StartupProgressWidget(QWidget *parent)
    : QWidget(parent)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_logoLabel(nullptr)
    , m_mainLayout(nullptr)
    , m_progressAnimation(nullptr)
    , m_fadeOutAnimation(nullptr)
    , m_opacityEffect(nullptr)
    , m_progressValue(0)
{
    m_animationsEnabled.store(true);
    initUI();
    initStyle();
    initAnimations();
}

StartupProgressWidget::~StartupProgressWidget()
{
}

StartupProgressWidget* StartupProgressWidget::instance()
{
    if (m_instance == nullptr) {
        QMutexLocker locker(&m_mutex);
        if (m_instance == nullptr) {
            m_instance = new StartupProgressWidget();
        }
    }
    return m_instance;
}

void StartupProgressWidget::initUI()
{
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFixedSize(420, 220);

    // 创建主布局
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(15);
    m_mainLayout->setContentsMargins(30, 25, 30, 25);

    // 创建Logo标签（先用默认浅色主题的图，主题就绪后由 applyTheme 切换）
    m_logoLabel = new QLabel(this);
    m_logoLabel->setObjectName(QStringLiteral("startupLogoLabel"));
    m_logoLabel->setAlignment(Qt::AlignCenter);
    updateLogo(false);

    // 创建状态标签
    m_statusLabel = new QLabel(QStringLiteral("正在初始化..."), this);
    m_statusLabel->setObjectName(QStringLiteral("startupStatusLabel"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(11);
    m_statusLabel->setFont(statusFont);
    m_statusLabel->setWordWrap(true);

    // 创建进度条
    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("startupProgressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);

    // 添加到布局
    m_mainLayout->addStretch();
    m_mainLayout->addWidget(m_logoLabel, 0, Qt::AlignHCenter);
    m_mainLayout->addSpacing(10);
    m_mainLayout->addWidget(m_progressBar);
    m_mainLayout->addSpacing(5);
    m_mainLayout->addWidget(m_statusLabel);
    m_mainLayout->addStretch();

    setLayout(m_mainLayout);
}

void StartupProgressWidget::initStyle()
{
    // 主题尚未就绪时先用默认浅色配色：构造阶段不再触碰 ThemeManager，
    // 让启动窗口能尽快显示（主题样式表的套用会明显拖慢首个窗口出现）
    applyStyleWith(QStringLiteral("#2E7D32"), QStringLiteral("#256428"), false);
}

void StartupProgressWidget::applyTheme()
{
    ThemeManager* tm = ThemeManager::instance();
    const bool isDark = (tm->currentTheme() == ThemeManager::DarkTheme);
    applyStyleWith(tm->currentThemeColor(), tm->getThemeColorMediumAccent(), isDark);
    updateLogo(isDark);
}

void StartupProgressWidget::updateLogo(bool isDark)
{
    const QString logoPath = isDark
        ? QStringLiteral(":/Images/blockbox_icon_dark.png")
        : QStringLiteral(":/Images/logo.png");
    QPixmap logoPix(logoPath);
    m_logoLabel->setPixmap(logoPix.scaled(QSize(160, 56), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_logoLabel->setFixedSize(160, 56);
}

void StartupProgressWidget::applyStyleWith(const QString& themeColor, const QString& themeHover, bool isDark)
{
    QString style = QString(
        "StartupProgressWidget {"
        "    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "        stop:0 %4, stop:1 %5);"
        "    border: 1px solid %6;"
        "    border-radius: 12px;"
        "}"
        ""
        "QLabel#startupLogoLabel {"
        "    background-color: transparent;"
        "}"
        ""
        "QLabel#startupStatusLabel {"
        "    color: %7;"
        "    background-color: transparent;"
        "}"
        ""
        "QProgressBar#startupProgressBar {"
        "    background-color: %3;"
        "    border: none;"
        "    border-radius: 4px;"
        "}"
        ""
        "QProgressBar#startupProgressBar::chunk {"
        "    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "        stop:0 %1, stop:1 %2);"
        "    border-radius: 4px;"
        "}"
    ).arg(themeColor, themeHover, isDark ? "#3d3d3d" : "#e8e8e8",
          isDark ? "#262626" : "#ffffff",
          isDark ? "#1a1a1a" : "#f5f5f5",
          isDark ? "#3a3a3a" : "#e0e0e0",
          isDark ? "#b0b0b0" : "#666666");

    setStyleSheet(style);
}

void StartupProgressWidget::initAnimations()
{
    // 进度动画
    m_progressAnimation = new QPropertyAnimation(this, "progressValue", this);
    m_progressAnimation->setDuration(300);  // 300ms动画时长
    m_progressAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // 淡出用的透明度效果延迟到 hideStartup() 再创建：
    // 透明度效果会让启动窗口的首次绘制走离屏渲染路径，拖慢启动窗口出现速度
}

int StartupProgressWidget::progressValue() const
{
    return m_progressValue;
}

void StartupProgressWidget::setProgressValue(int value)
{
    if (m_progressValue != value) {
        m_progressValue = value;
        if (m_progressBar) {
            m_progressBar->setValue(value);
        }
        emit progressValueChanged(value);
    }
}

void StartupProgressWidget::setAnimationsEnabled(bool enabled)
{
    m_animationsEnabled.store(enabled);
}

bool StartupProgressWidget::animationsEnabled() const
{
    return m_animationsEnabled.load();
}

void StartupProgressWidget::updateProgress(int progress, const QString& status)
{
    // 线程安全更新
    QMetaObject::invokeMethod(this, [this, progress, status]() {
        setProgress(progress);
        setStatus(status);
    }, Qt::QueuedConnection);
}

void StartupProgressWidget::setProgress(int progress)
{
    // 确保进度在有效范围内
    progress = qBound(0, progress, 100);

    // 使用动画平滑过渡（仅在动画启用时）
    if (m_animationsEnabled.load() && m_progressAnimation) {
        m_progressAnimation->stop();
        m_progressAnimation->setStartValue(m_progressValue);
        m_progressAnimation->setEndValue(progress);
        m_progressAnimation->start();
    } else {
        setProgressValue(progress);
    }
}

void StartupProgressWidget::setStatus(const QString& status)
{
    m_currentStatus = status;
    if (m_statusLabel) {
        m_statusLabel->setText(status);
    }
}

void StartupProgressWidget::showStartup()
{
    // 居中显示
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }

    // 重置透明度
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(1.0);
    }

    // 重置进度
    m_progressValue = 0;
    if (m_progressBar) {
        m_progressBar->setValue(0);
    }

    show();
    raise();
    activateWindow();

    // 强制刷新UI
    QApplication::processEvents();
}

void StartupProgressWidget::hideStartup()
{
    // 根据动画启用状态决定是否执行淡出动画
    if (m_animationsEnabled.load())
    {
        // 淡出用的透明度效果按需创建（见 initAnimations 注释）
        if (!m_opacityEffect)
        {
            m_opacityEffect = new QGraphicsOpacityEffect(this);
            m_opacityEffect->setOpacity(1.0);
            setGraphicsEffect(m_opacityEffect);

            m_fadeOutAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
            m_fadeOutAnimation->setDuration(200);  // 200ms淡出时长
            m_fadeOutAnimation->setStartValue(1.0);
            m_fadeOutAnimation->setEndValue(0.0);
            m_fadeOutAnimation->setEasingCurve(QEasingCurve::OutCubic);
            connect(m_fadeOutAnimation, &QPropertyAnimation::finished, this, &QWidget::hide);
        }
        m_fadeOutAnimation->start();
    }
    else
    {
        hide();
    }
}

void StartupProgressWidget::closeEvent(QCloseEvent *event)
{
    // 阻止用户手动关闭启动窗口
    event->ignore();
}
