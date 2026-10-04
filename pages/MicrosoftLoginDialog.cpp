/**
 * @file   MicrosoftLoginDialog.cpp
 * @brief  微软登录对话框实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "MicrosoftLoginDialog.h"

#include "components/AppMessageBox.h"
#include <QTimer>
#include <QUrlQuery>

MicrosoftLoginDialog::MicrosoftLoginDialog(QWidget *parent) : QDialog(parent)
{
    m_authManager = AuthManager::instance();
    m_isLoginInProgress = false;
    setupUI();
    
    // 连接信号和槽（AuthManager 可能为 null）
    if (m_authManager) {
        connect(m_authManager, &AuthManager::microsoftAuthUrlReceived, this, &MicrosoftLoginDialog::onAuthUrlReady);
        connect(m_authManager, &AuthManager::microsoftLoginProgressChanged, this, &MicrosoftLoginDialog::onLoginProgressChanged);
        connect(m_authManager, &AuthManager::microsoftLoginSucceeded, this, &MicrosoftLoginDialog::onLoginSuccess);
        connect(m_authManager, &AuthManager::microsoftLoginFailed, this, &MicrosoftLoginDialog::onLoginFailed);
        connect(m_authManager, &AuthManager::microsoftLoginCanceled, this, &MicrosoftLoginDialog::onLoginCanceled);
    }
    // 在setupUI()之后连接，因为m_cancelButton是在setupUI()中创建的
    connect(m_cancelButton, &QPushButton::clicked, this, &MicrosoftLoginDialog::onCancelButtonClicked);
    connect(m_openBrowserButton, &QPushButton::clicked, [=]() {
        if (!m_authUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(m_authUrl));
        }
    });
}

MicrosoftLoginDialog::~MicrosoftLoginDialog()
{
}

void MicrosoftLoginDialog::setupUI()
{
    setWindowTitle(tr("Microsoft登录"));
    setMinimumSize(520, 380);
    
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(24, 20, 24, 20);
    m_mainLayout->setSpacing(12);
    
    // 状态标签
    m_statusLabel = new QLabel(tr("准备登录..."));
    m_statusLabel->setObjectName(QStringLiteral("loginStatusLabel"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(12);
    m_statusLabel->setFont(statusFont);
    m_mainLayout->addWidget(m_statusLabel);
    
    // 进度条
    m_progressBar = new QProgressBar();
    m_progressBar->setObjectName(QStringLiteral("loginProgressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    m_progressBar->setFixedHeight(8);
    m_progressBar->setTextVisible(false);
    m_mainLayout->addWidget(m_progressBar);
    
    m_mainLayout->addStretch();
    
    // 按钮区域
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    
    m_openBrowserButton = new QPushButton(tr("在浏览器中打开登录页面"));
    m_openBrowserButton->setObjectName(QStringLiteral("openBrowserButton"));
    m_openBrowserButton->setVisible(false);
    m_openBrowserButton->setCursor(Qt::PointingHandCursor);
    
    m_cancelButton = new QPushButton(tr("取消"));
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setFixedWidth(100);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_openBrowserButton);
    buttonLayout->addWidget(m_cancelButton);
    
    m_mainLayout->addLayout(buttonLayout);
}

void MicrosoftLoginDialog::startLogin()
{
    m_isLoginInProgress = true;
    m_statusLabel->setText(tr("正在准备登录..."));
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_openBrowserButton->setVisible(false);
    
    // 开始Microsoft登录
    if (m_authManager)
        m_authManager->microsoftAuthenticate();
}

void MicrosoftLoginDialog::onAuthUrlReady(const QString &authUrl)
{
    m_authUrl = authUrl;
    m_statusLabel->setText(tr("请在浏览器中登录Microsoft账户..."));
    m_progressBar->setValue(20);
    
    // 显示打开浏览器按钮
    m_openBrowserButton->setVisible(true);
    
    // 自动打开系统默认浏览器
    QDesktopServices::openUrl(QUrl(authUrl));
}

void MicrosoftLoginDialog::onLoginProgressChanged(int progress)
{
    m_progressBar->setValue(progress);
    
    if (progress >= 100) {
        m_statusLabel->setText(tr("登录成功！"));
    }
}

void MicrosoftLoginDialog::onLoginSuccess(const QString &username, const QString &accessToken, const QString &refreshToken)
{
    m_isLoginInProgress = false;
    m_statusLabel->setText(tr("登录成功！"));
    m_progressBar->setValue(100);
    m_openBrowserButton->setVisible(false);
    
    // 发送登录成功信号
    emit loginSucceeded(username, accessToken, refreshToken);
    
    // 延迟关闭对话框
    QTimer::singleShot(1000, this, &QDialog::accept);
}

void MicrosoftLoginDialog::onLoginFailed(const QString &errorMessage)
{
    m_isLoginInProgress = false;
    m_statusLabel->setText(tr("登录失败"));
    m_progressBar->setVisible(false);
    m_openBrowserButton->setVisible(false);
    
    // 显示错误信息
    AppMessageBox::warning(this, tr("登录失败"), errorMessage);
    
    // 发送登录失败信号
    emit loginFailed(errorMessage);
    
    // 关闭对话框
    reject();
}

void MicrosoftLoginDialog::onLoginCanceled()
{
    m_isLoginInProgress = false;
    m_statusLabel->setText(tr("登录已取消"));
    m_progressBar->setVisible(false);
    m_openBrowserButton->setVisible(false);
    
    // 发送登录取消信号
    emit loginCanceled();
    
    // 关闭对话框
    reject();
}

void MicrosoftLoginDialog::onCancelButtonClicked()
{
    if (m_isLoginInProgress && m_authManager) {
        // 取消登录
        m_authManager->cancelMicrosoftLogin();
    }
    reject();
}
