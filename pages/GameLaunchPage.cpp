#include "GameLaunchPage.h"

#include "components/AppMessageBox.h"
#include "components/LaunchLogDialog.h"
#include "utils/ThemeManager.h"

#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

GameLaunchPage::GameLaunchPage(QWidget *parent) : QWidget(parent)
{
    initUI();
}

GameLaunchPage::~GameLaunchPage() = default;

void GameLaunchPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 32);
    mainLayout->setSpacing(0);
    mainLayout->setAlignment(Qt::AlignCenter);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setPixmap(QPixmap(":/Images/logo.png").scaled(
        72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    m_titleLabel = new QLabel(tr("启动游戏"), this);
    m_titleLabel->setObjectName("gameLaunchTitle");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName("gameLaunchProgressBar");
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    m_progressBar->setMinimumWidth(320);
    m_progressBar->setMaximumWidth(560);

    m_percentLabel = new QLabel("0%", this);
    m_percentLabel->setObjectName("gameLaunchPercent");
    m_percentLabel->setAlignment(Qt::AlignCenter);
    m_percentLabel->setMaximumWidth(560);
    m_percentLabel->setMinimumWidth(320);

    m_messageLabel = new QLabel(tr("准备启动游戏..."), this);
    m_messageLabel->setObjectName("gameLaunchMessage");
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setMaximumWidth(560);
    m_messageLabel->setMinimumWidth(320);

    QHBoxLayout *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(14);
    buttonRow->setAlignment(Qt::AlignCenter);

    m_viewLogBtn = new QPushButton(tr("查看日志"), this);
    m_viewLogBtn->setObjectName("gameLaunchViewLogBtn");
    m_viewLogBtn->setCursor(Qt::PointingHandCursor);
    connect(m_viewLogBtn, &QPushButton::clicked, this, &GameLaunchPage::openLogDialog);

    m_cancelBtn = new QPushButton(tr("取消启动"), this);
    m_cancelBtn->setObjectName("gameLaunchCancelBtn");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        GameLauncher::instance()->stopGame();
        appendLog(tr("用户取消了启动"));
    });

    buttonRow->addWidget(m_viewLogBtn);
    buttonRow->addWidget(m_cancelBtn);

    mainLayout->addSpacing(8);
    mainLayout->addWidget(m_iconLabel);
    mainLayout->addSpacing(14);
    mainLayout->addWidget(m_titleLabel);
    mainLayout->addSpacing(30);
    mainLayout->addWidget(m_progressBar, 0, Qt::AlignHCenter);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(m_percentLabel);
    mainLayout->addSpacing(6);
    mainLayout->addWidget(m_messageLabel);
    mainLayout->addSpacing(34);
    mainLayout->addLayout(buttonRow);

    refreshStyle();
    updateButtons();
}

void GameLaunchPage::refreshStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString themeColor = tm->currentThemeColor();
    const QString themeEnd = QColor(themeColor).lighter(130).name();

    const QString titleColor  = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor   = isLight ? "#4a4a4a" : "#c9c9c9";
    const QString mutedColor  = isLight ? "#8a8a8a" : "#909090";
    const QString barBg       = isLight ? "rgba(0, 0, 0, 16)" : "rgba(255, 255, 255, 26)";
    const QString btnBg       = isLight ? "rgba(255, 255, 255, 220)" : "rgba(60, 60, 66, 220)";
    const QString dangerColor = "#F44336";

    const QString style = QString(
        "QLabel#gameLaunchTitle {"
        "    color: %1;"
        "    background-color: transparent;"
        "}"
        ""
        "QLabel#gameLaunchPercent {"
        "    color: %2;"
        "    background-color: transparent;"
        "    font-size: 14px;"
        "}"
        ""
        "QLabel#gameLaunchMessage {"
        "    color: %3;"
        "    background-color: transparent;"
        "    font-size: 14px;"
        "}"
        ""
        "QProgressBar#gameLaunchProgressBar {"
        "    background-color: %4;"
        "    border: none;"
        "    border-radius: 4px;"
        "}"
        ""
        "QProgressBar#gameLaunchProgressBar::chunk {"
        "    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "        stop:0 %5, stop:1 %6);"
        "    border-radius: 4px;"
        "}"
        ""
        "QPushButton#gameLaunchViewLogBtn {"
        "    background-color: %7;"
        "    border: 1px solid %5;"
        "    border-radius: 14px;"
        "    color: %5;"
        "    padding: 6px 22px;"
        "    font-size: 13px;"
        "}"
        ""
        "QPushButton#gameLaunchViewLogBtn:hover {"
        "    background-color: %5;"
        "    color: white;"
        "}"
        ""
        "QPushButton#gameLaunchCancelBtn {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 14px;"
        "    color: %8;"
        "    padding: 6px 22px;"
        "    font-size: 13px;"
        "}"
        ""
        "QPushButton#gameLaunchCancelBtn:hover {"
        "    background-color: %8;"
        "    color: white;"
        "}"
    ).arg(titleColor, mutedColor, textColor, barBg,
          themeColor, themeEnd, btnBg, dangerColor);

    setStyleSheet(style);
}

void GameLaunchPage::beginLaunch()
{
    m_status = GameLauncher::Launching;
    m_logLines.append("====== " + tr("开始新的启动") + " ======");
    setProgress(0, tr("准备启动游戏..."));
    updateButtons();
}

void GameLaunchPage::setProgress(int percent, const QString &message)
{
    m_progressBar->setValue(qBound(0, percent, 100));
    m_percentLabel->setText(QString("%1%").arg(qBound(0, percent, 100)));
    m_messageLabel->setText(message);
}

void GameLaunchPage::setLaunchStatus(GameLauncher::LaunchStatus status)
{
    m_status = status;

    switch (status) {
    case GameLauncher::Idle:
        refreshStyle();
        setProgress(0, tr("准备启动游戏..."));
        break;
    case GameLauncher::Launching:
        refreshStyle();
        break;
    case GameLauncher::Running:
        refreshStyle();
        setProgress(100, tr("游戏启动成功！"));
        break;
    case GameLauncher::Failed: {
        refreshStyle();
        const QString error = GameLauncher::instance()->errorMessage();
        m_messageLabel->setText(error.isEmpty() ? tr("启动失败") : error);
        break;
    }
    case GameLauncher::Stopped:
        refreshStyle();
        m_messageLabel->setText(tr("已取消启动"));
        break;
    }
    updateButtons();
}

void GameLaunchPage::appendLog(const QString &line)
{
    m_logLines.append(line);
    if (m_logDialog)
        m_logDialog->appendLog(line);
}

void GameLaunchPage::openLogDialog()
{
    if (!m_logDialog)
        m_logDialog = new LaunchLogDialog(this);
    m_logDialog->setLogs(m_logLines);
    m_logDialog->exec();
}

void GameLaunchPage::updateButtons()
{
    // 只有正在启动时才显示「取消启动」；失败/取消后靠返回离开页面
    m_cancelBtn->setVisible(m_status == GameLauncher::Launching);
}
