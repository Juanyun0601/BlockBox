/**
 * @file   LoadingOverlay.cpp
 * @brief  加载覆盖层组件实现（性能优化版）
 * @author BlockBox Team
 * @date   2026-05-17
 */
#include "LoadingOverlay.h"

#include <QConicalGradient>
#include <QPainter>
#include <QPaintEvent>

#include "utils/LowConfigMode.h"
#include "utils/ThemeManager.h"

// ========== SpinnerWidget ==========

SpinnerWidget::SpinnerWidget(QWidget *parent)
    : QWidget(parent)
    , m_rotationAngle(0.0)
    , m_timer(new QTimer(this))
    , m_animationsEnabled(true)
{
    setFixedSize(48, 48);

    // 缓存主题色
    updateThemeColor();

    // 监听主题色变化
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, [this]() { updateThemeColor(); });

    // 监听低配置模式变化
    connect(LowConfigMode::instance(), &LowConfigMode::animationsEnabledChanged,
            this, [this]() {
                m_animationsEnabled = LowConfigMode::instance()->areAnimationsEnabled();
            });
    m_animationsEnabled = LowConfigMode::instance()->areAnimationsEnabled();

    // 旋转定时器：普通模式 ~30fps，低配置模式 ~20fps
    m_timer->setInterval(m_animationsEnabled ? 33 : 50);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        // 每帧旋转固定角度（30fps * 12° = 360°/s，20fps * 18° = 360°/s）
        qreal step = m_animationsEnabled ? 12.0 : 18.0;
        qreal angle = m_rotationAngle + step;
        if (angle >= 360.0)
            angle -= 360.0;
        setRotationAngle(angle);
    });
}

qreal SpinnerWidget::rotationAngle() const
{
    return m_rotationAngle;
}

void SpinnerWidget::setRotationAngle(qreal angle)
{
    if (!qFuzzyCompare(m_rotationAngle, angle)) {
        m_rotationAngle = angle;
        update(); // 请求重绘
    }
}

void SpinnerWidget::start()
{
    if (!m_timer->isActive()) {
        m_timer->setInterval(m_animationsEnabled ? 33 : 50);
        m_timer->start();
    }
}

void SpinnerWidget::stop()
{
    m_timer->stop();
}

bool SpinnerWidget::isRunning() const
{
    return m_timer->isActive();
}

void SpinnerWidget::updateThemeColor()
{
    m_cachedThemeColor = QColor(ThemeManager::instance()->currentThemeColor());
    update();
}

void SpinnerWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    // 仅在非低配置模式下启用抗锯齿
    painter.setRenderHint(QPainter::Antialiasing, m_animationsEnabled);

    const int radius = 18;
    const int penWidth = 3;
    QPoint center(width() / 2, height() / 2);

    QPen pen;
    pen.setWidth(penWidth);
    pen.setCapStyle(Qt::RoundCap);

    // 绘制背景圆弧（浅灰色）
    pen.setColor(QColor(220, 220, 220));
    painter.setPen(pen);
    painter.drawEllipse(center, radius, radius);

    // 绘制旋转圆弧（使用缓存的主题色）
    const QColor &tc = m_cachedThemeColor;
    QConicalGradient gradient(center, m_rotationAngle);
    gradient.setColorAt(0.0, tc);
    gradient.setColorAt(0.5, QColor(tc.red(), tc.green(), tc.blue(), 80));
    gradient.setColorAt(1.0, QColor(tc.red(), tc.green(), tc.blue(), 0));

    pen.setBrush(QBrush(gradient));
    painter.setPen(pen);

    QRectF arcRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    int startAngle = static_cast<int>(m_rotationAngle * 16);
    int spanAngle = 270 * 16;
    painter.drawArc(arcRect, startAngle, spanAngle);
}

// ========== LoadingOverlay ==========

LoadingOverlay::LoadingOverlay(QWidget *parent)
    : QWidget(parent)
    , m_centerWidget(nullptr)
    , m_centerLayout(nullptr)
    , m_spinner(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_cancelButton(nullptr)
    , m_showCancelButton(true)
    , m_showProgressBar(true)
    , m_animationsEnabled(true)
{
    setObjectName(QStringLiteral("loadingOverlay"));

    initUI();
    initStyle();

    // 监听低配置模式变化
    connect(LowConfigMode::instance(), &LowConfigMode::animationsEnabledChanged,
            this, &LoadingOverlay::updateAnimationState);
    m_animationsEnabled = LowConfigMode::instance()->areAnimationsEnabled();
}

LoadingOverlay::~LoadingOverlay()
{
}

void LoadingOverlay::initUI()
{
    // 覆盖层属性
    setAttribute(Qt::WA_TranslucentBackground, true);

    // 主布局（无边距，覆盖整个父 Widget）
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 居中容器
    m_centerWidget = new QWidget(this);
    m_centerWidget->setObjectName(QStringLiteral("loadingCenterWidget"));
    m_centerWidget->setFixedSize(320, 230);

    m_centerLayout = new QVBoxLayout(m_centerWidget);
    m_centerLayout->setSpacing(12);
    m_centerLayout->setContentsMargins(30, 25, 30, 25);

    // 旋转动画组件
    m_spinner = new SpinnerWidget(m_centerWidget);
    m_spinner->setObjectName(QStringLiteral("loadingSpinner"));

    // 进度条
    m_progressBar = new QProgressBar(m_centerWidget);
    m_progressBar->setObjectName(QStringLiteral("loadingProgressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);

    // 状态文字
    m_statusLabel = new QLabel(QStringLiteral("正在加载..."), m_centerWidget);
    m_statusLabel->setObjectName(QStringLiteral("loadingStatusLabel"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(10);
    m_statusLabel->setFont(statusFont);

    // 取消按钮
    m_cancelButton = new QPushButton(QStringLiteral("取消"), m_centerWidget);
    m_cancelButton->setObjectName(QStringLiteral("loadingCancelButton"));
    m_cancelButton->setFixedSize(80, 30);
    m_cancelButton->setCursor(Qt::PointingHandCursor);

    connect(m_cancelButton, &QPushButton::clicked, this, &LoadingOverlay::onCancelClicked);

    // 组装布局
    m_centerLayout->addStretch();
    m_centerLayout->addWidget(m_spinner, 0, Qt::AlignCenter);
    m_centerLayout->addSpacing(8);
    m_centerLayout->addWidget(m_progressBar);
    m_centerLayout->addSpacing(4);
    m_centerLayout->addWidget(m_statusLabel);
    m_centerLayout->addSpacing(8);
    m_centerLayout->addWidget(m_cancelButton, 0, Qt::AlignCenter);
    m_centerLayout->addStretch();

    // 将居中容器添加到主布局
    mainLayout->addWidget(m_centerWidget, 0, Qt::AlignCenter);

    setLayout(mainLayout);

    // 默认隐藏
    hide();
}

void LoadingOverlay::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();

    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg      = isLight ? "rgba(255, 255, 255, 245)" : "rgba(45, 45, 45, 245)";
    const QString cardBorder  = isLight ? "#e0e0e0" : "#444444";
    const QString barBg       = isLight ? "#e8e8e8" : "#3d3d3d";
    const QString textColor   = isLight ? "#555555" : "#aaaaaa";

    QString style = QString(
        "QWidget#loadingCenterWidget {"
        "    background-color: %3;"
        "    border: 1px solid %4;"
        "    border-radius: 12px;"
        "}"
        ""
        "QProgressBar#loadingProgressBar {"
        "    background-color: %5;"
        "    border: none;"
        "    border-radius: 3px;"
        "}"
        ""
        "QProgressBar#loadingProgressBar::chunk {"
        "    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "        stop:0 %1, stop:1 %2);"
        "    border-radius: 3px;"
        "}"
        ""
        "QLabel#loadingStatusLabel {"
        "    color: %6;"
        "    background-color: transparent;"
        "}"
    ).arg(themeColor).arg(themeHover).arg(cardBg).arg(cardBorder).arg(barBg).arg(textColor);

    setStyleSheet(style);
}

void LoadingOverlay::setShowCancelButton(bool show)
{
    m_showCancelButton = show;
    if (m_cancelButton) {
        m_cancelButton->setVisible(show);
    }
}

void LoadingOverlay::setShowProgressBar(bool show)
{
    m_showProgressBar = show;
    if (m_progressBar) {
        m_progressBar->setVisible(show);
    }
}

void LoadingOverlay::showOverlay(const QString &status)
{
    // 重置状态
    if (m_progressBar) {
        m_progressBar->setValue(0);
    }
    if (!status.isEmpty() && m_statusLabel) {
        m_statusLabel->setText(status);
    }
    if (m_cancelButton) {
        m_cancelButton->setVisible(m_showCancelButton);
    }
    if (m_progressBar) {
        m_progressBar->setVisible(m_showProgressBar);
    }
    if (m_spinner) {
        m_spinner->setRotationAngle(0.0);
    }

    // 显示并调整大小
    if (parentWidget()) {
        resize(parentWidget()->size());
    }
    show();
    raise();

    // 启动旋转动画
    if (m_spinner) {
        m_spinner->start();
    }
}

void LoadingOverlay::hideOverlay()
{
    // 停止旋转动画
    if (m_spinner) {
        m_spinner->stop();
    }

    // 直接隐藏（不使用 QGraphicsOpacityEffect 淡出动画，避免性能开销）
    hide();
}

void LoadingOverlay::updateProgress(int percent)
{
    if (m_progressBar) {
        percent = qBound(0, percent, 100);
        m_progressBar->setValue(percent);
    }
}

void LoadingOverlay::updateStatus(const QString &status)
{
    if (m_statusLabel) {
        m_statusLabel->setText(status);
    }
}

void LoadingOverlay::onCancelClicked()
{
    emit cancelRequested();
}

void LoadingOverlay::updateAnimationState()
{
    m_animationsEnabled = LowConfigMode::instance()->areAnimationsEnabled();
}

void LoadingOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // 绘制半透明背景遮罩（不需要抗锯齿）
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 100));
}

void LoadingOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (parentWidget()) {
        resize(parentWidget()->size());
    }
}

void LoadingOverlay::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // 确保旋转动画停止
    if (m_spinner) {
        m_spinner->stop();
    }
}
