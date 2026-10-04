#include "BlurLoadingOverlay.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPaintEvent>
#include <QStyle>

#include "utils/ThemeManager.h"

BlurLoadingOverlay::BlurLoadingOverlay(QWidget *parent)
    : QWidget(parent)
    , m_centerWidget(nullptr)
    , m_centerLayout(nullptr)
    , m_processLabel(nullptr)
    , m_progressBar(nullptr)
    , m_percentLabel(nullptr)
    , m_errorLabel(nullptr)
    , m_diagnoseBtn(nullptr)
    , m_progressArea(nullptr)
    , m_diagnosisNAM(new QNetworkAccessManager(this))
    , m_currentPercent(0)
    , m_hasError(false)
    , m_isCapturing(false)
{
    setObjectName(QStringLiteral("blurLoadingOverlay"));
    initUI();
    initStyle();
    connect(this, &BlurLoadingOverlay::diagnoseRequested, this, &BlurLoadingOverlay::performDiagnosis);
}

void BlurLoadingOverlay::initUI()
{
    setAttribute(Qt::WA_TranslucentBackground, true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_centerWidget = new QWidget(this);
    m_centerWidget->setObjectName(QStringLiteral("blurLoadingCenterWidget"));
    m_centerWidget->setFixedSize(400, 180);

    m_centerLayout = new QVBoxLayout(m_centerWidget);
    m_centerLayout->setSpacing(16);
    m_centerLayout->setContentsMargins(36, 28, 36, 28);

    m_processLabel = new QLabel(m_centerWidget);
    m_processLabel->setObjectName(QStringLiteral("blurLoadingProcessLabel"));
    m_processLabel->setAlignment(Qt::AlignCenter);
    m_processLabel->setWordWrap(true);
    QFont processFont = m_processLabel->font();
    processFont.setPointSize(12);
    m_processLabel->setFont(processFont);

    m_progressArea = new QWidget(m_centerWidget);
    m_progressArea->setObjectName(QStringLiteral("blurLoadingProgressArea"));
    QVBoxLayout *progressLayout = new QVBoxLayout(m_progressArea);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(6);

    QWidget *barRow = new QWidget(m_progressArea);
    barRow->setObjectName(QStringLiteral("blurLoadingBarRow"));
    QHBoxLayout *barRowLayout = new QHBoxLayout(barRow);
    barRowLayout->setContentsMargins(0, 0, 0, 0);
    barRowLayout->setSpacing(10);

    m_progressBar = new QProgressBar(barRow);
    m_progressBar->setObjectName(QStringLiteral("blurLoadingProgressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);

    m_percentLabel = new QLabel(QStringLiteral("0%"), barRow);
    m_percentLabel->setObjectName(QStringLiteral("blurLoadingPercentLabel"));
    m_percentLabel->setFixedWidth(42);
    m_percentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont percentFont = m_percentLabel->font();
    percentFont.setPointSize(10);
    percentFont.setBold(true);
    m_percentLabel->setFont(percentFont);

    barRowLayout->addWidget(m_progressBar, 1);
    barRowLayout->addWidget(m_percentLabel);

    progressLayout->addWidget(barRow);

    m_errorLabel = new QLabel(m_progressArea);
    m_errorLabel->setObjectName(QStringLiteral("blurLoadingErrorLabel"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setWordWrap(true);
    QFont errorFont = m_errorLabel->font();
    errorFont.setPointSize(10);
    m_errorLabel->setFont(errorFont);
    m_errorLabel->setVisible(false);

    progressLayout->addWidget(m_errorLabel);

    m_diagnoseBtn = new QPushButton(tr("诊断网络"), m_progressArea);
    m_diagnoseBtn->setObjectName(QStringLiteral("blurLoadingDiagnoseBtn"));
    m_diagnoseBtn->setCursor(Qt::PointingHandCursor);
    m_diagnoseBtn->setVisible(false);
    connect(m_diagnoseBtn, &QPushButton::clicked, this, &BlurLoadingOverlay::diagnoseRequested);
    progressLayout->addWidget(m_diagnoseBtn, 0, Qt::AlignCenter);

    m_centerLayout->addStretch();
    m_centerLayout->addWidget(m_processLabel);
    m_centerLayout->addWidget(m_progressArea);
    m_centerLayout->addStretch();

    mainLayout->addWidget(m_centerWidget, 0, Qt::AlignCenter);
    setLayout(mainLayout);

    hide();
}

void BlurLoadingOverlay::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();

    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg      = isLight ? "rgba(255, 255, 255, 240)" : "rgba(50, 50, 50, 240)";
    const QString cardBorder  = isLight ? "rgba(200, 200, 200, 200)" : "rgba(80, 80, 80, 200)";
    const QString barBg       = isLight ? "#e8e8e8" : "#3d3d3d";
    const QString processColor = isLight ? "#333333" : "#eeeeee";
    const QString percentColor = themeColor;

    QString style = QString(
        "QWidget#blurLoadingCenterWidget {"
        "    background-color: %3;"
        "    border: 1px solid %4;"
        "    border-radius: 16px;"
        "}"
        ""
        "QWidget#blurLoadingProgressArea {"
        "    background-color: transparent;"
        "}"
        ""
        "QWidget#blurLoadingBarRow {"
        "    background-color: transparent;"
        "}"
        ""
        "QLabel#blurLoadingProcessLabel {"
        "    color: %6;"
        "    background-color: transparent;"
        "    font-weight: bold;"
        "}"
        ""
        "QLabel#blurLoadingPercentLabel {"
        "    color: %1;"
        "    background-color: transparent;"
        "}"
        ""
        "QLabel#blurLoadingErrorLabel {"
        "    color: #f44336;"
        "    background-color: rgba(244, 67, 54, 40);"
        "    border: 1px solid rgba(244, 67, 54, 100);"
        "    border-radius: 8px;"
        "    padding: 10px 14px;"
        "}"
        ""
        "QPushButton#blurLoadingDiagnoseBtn {"
        "    background-color: transparent;"
        "    border: 1px solid %1;"
        "    border-radius: 14px;"
        "    color: %1;"
        "    padding: 5px 18px;"
        "    font-size: 12px;"
        "}"
        ""
        "QPushButton#blurLoadingDiagnoseBtn:hover {"
        "    background-color: %1;"
        "    color: white;"
        "}"
        ""
        "QPushButton#blurLoadingDiagnoseBtn:pressed {"
        "    background-color: %2;"
        "    border-color: %2;"
        "    color: white;"
        "}"
        ""
        "QProgressBar#blurLoadingProgressBar {"
        "    background-color: %5;"
        "    border: none;"
        "    border-radius: 4px;"
        "}"
        ""
        "QProgressBar#blurLoadingProgressBar::chunk {"
        "    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "        stop:0 %1, stop:1 %2);"
        "    border-radius: 4px;"
        "}"
    ).arg(themeColor, themeHover, cardBg, cardBorder, barBg, processColor);

    setStyleSheet(style);
}

void BlurLoadingOverlay::adjustGeometryToFullArea()
{
    QWidget *p = parentWidget();
    if (!p)
        return;

    move(0, 0);
    resize(p->size());
}

void BlurLoadingOverlay::captureBlurBackground()
{
    if (m_isCapturing)
        return;

    QWidget *p = parentWidget();
    if (!p)
        return;

    m_isCapturing = true;

    adjustGeometryToFullArea();

    QWidget *w = window();
    if (!w || w == this)
    {
        m_isCapturing = false;
        return;
    }

    QRect captureRect(mapTo(w, QPoint()), size());
    QPixmap original = w->grab(captureRect);
    if (original.isNull()) {
        m_isCapturing = false;
        return;
    }

    QSize smallSize(original.width() / 6, original.height() / 6);
    if (smallSize.width() < 1) smallSize.setWidth(1);
    if (smallSize.height() < 1) smallSize.setHeight(1);

    m_blurredBackground = original.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .scaled(original.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    m_isCapturing = false;
}

void BlurLoadingOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (m_isCapturing)
        return;
    QPainter painter(this);
    if (!m_blurredBackground.isNull()) {
        painter.drawPixmap(0, 0, m_blurredBackground);
    }
    painter.fillRect(rect(), QColor(0, 0, 0, 80));
}

void BlurLoadingOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    adjustGeometryToFullArea();
    captureBlurBackground();
}

void BlurLoadingOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (isVisible() && parentWidget() && !m_isCapturing) {
        captureBlurBackground();
    }
}

void BlurLoadingOverlay::showOverlay(const QString &processName)
{
    clearError();
    m_currentPercent = 0;
    m_currentProcessName = processName.isEmpty() ? QString() : processName;

    if (m_processLabel) {
        m_processLabel->setText(m_currentProcessName);
    }
    if (m_progressBar) {
        m_progressBar->setValue(0);
    }
    if (m_percentLabel) {
        m_percentLabel->setText(QStringLiteral("0%"));
    }
    if (m_progressArea) {
        m_progressArea->setVisible(true);
    }

    updateLayoutState();
    adjustGeometryToFullArea();
    show();
    raise();
}

void BlurLoadingOverlay::hideOverlay()
{
    hide();
    m_blurredBackground = QPixmap();
}

void BlurLoadingOverlay::updateProgress(int percent, const QString &processName)
{
    m_currentPercent = qBound(0, percent, 100);

    if (!processName.isEmpty()) {
        m_currentProcessName = processName;
    }

    if (m_hasError) {
        clearError();
    }

    if (m_processLabel && !m_currentProcessName.isEmpty()) {
        m_processLabel->setText(m_currentProcessName);
    }
    if (m_progressBar) {
        m_progressBar->setValue(m_currentPercent);
    }
    if (m_percentLabel) {
        m_percentLabel->setText(QStringLiteral("%1%").arg(m_currentPercent));
    }
    if (m_progressArea) {
        m_progressArea->setVisible(true);
    }

    updateLayoutState();
}

void BlurLoadingOverlay::showError(const QString &errorMessage)
{
    m_hasError = true;

    if (m_errorLabel) {
        m_errorLabel->setText(QStringLiteral("\xE2\x9A\xA0 %1").arg(errorMessage));
        m_errorLabel->setVisible(true);
    }
    if (m_diagnoseBtn) {
        m_diagnoseBtn->setVisible(true);
    }
    if (m_progressBar) {
        m_progressBar->setVisible(false);
    }
    if (m_percentLabel) {
        m_percentLabel->setVisible(false);
    }

    updateLayoutState();
}

void BlurLoadingOverlay::showDiagnosisResult(const QString &result)
{
    if (m_errorLabel) {
        m_errorLabel->setText(result);
        m_errorLabel->setVisible(true);
    }
    if (m_diagnoseBtn) {
        m_diagnoseBtn->setVisible(false);
    }
}

void BlurLoadingOverlay::clearError()
{
    m_hasError = false;

    if (m_errorLabel) {
        m_errorLabel->setVisible(false);
        m_errorLabel->clear();
    }
    if (m_diagnoseBtn) {
        m_diagnoseBtn->setVisible(false);
    }
    if (m_progressBar) {
        m_progressBar->setVisible(true);
    }
    if (m_percentLabel) {
        m_percentLabel->setVisible(true);
    }

    updateLayoutState();
}

void BlurLoadingOverlay::performDiagnosis()
{
    if (m_diagnoseBtn) {
        m_diagnoseBtn->setText(tr("诊断中..."));
        m_diagnoseBtn->setEnabled(false);
    }
    if (m_errorLabel) {
        m_errorLabel->setText(tr("正在检测网络连接..."));
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://www.baidu.com")));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(8000);
    QNetworkReply *reply = m_diagnosisNAM->head(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_diagnoseBtn) {
            m_diagnoseBtn->setText(tr("诊断网络"));
            m_diagnoseBtn->setEnabled(true);
        }
        if (reply->error() == QNetworkReply::NoError) {
            showDiagnosisResult(tr("✓ 网络连接正常"));
        } else {
            showDiagnosisResult(tr("✗ 网络无法连接\n请检查网络设置或防火墙"));
        }
    });
}

void BlurLoadingOverlay::updateLayoutState()
{
    if (!m_centerWidget)
        return;

    if (m_hasError) {
        m_centerWidget->setFixedSize(420, 210);
    } else {
        m_centerWidget->setFixedSize(400, 170);
    }
}
