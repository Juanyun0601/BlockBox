/**
 * @file   MicrosoftLoginDialog.h
 * @brief  微软登录对话框类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef MICROSOFTLOGINDIALOG_H
#define MICROSOFTLOGINDIALOG_H

#include <QDesktopServices>
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "utils/AuthManager.h"

class MicrosoftLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MicrosoftLoginDialog(QWidget *parent = nullptr);
    ~MicrosoftLoginDialog();

public slots:
    void startLogin();

signals:
    void loginSucceeded(const QString &username, const QString &accessToken, const QString &refreshToken);
    void loginFailed(const QString &errorMessage);
    void loginCanceled();

private slots:
    void onAuthUrlReady(const QString &authUrl);
    void onLoginProgressChanged(int progress);
    void onLoginSuccess(const QString &username, const QString &accessToken, const QString &refreshToken);
    void onLoginFailed(const QString &errorMessage);
    void onLoginCanceled();
    void onCancelButtonClicked();

private:
    QVBoxLayout *m_mainLayout;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QPushButton *m_cancelButton;
    QPushButton *m_openBrowserButton;
    AuthManager *m_authManager;
    bool m_isLoginInProgress;
    QString m_authUrl;

    void setupUI();
};

#endif // MICROSOFTLOGINDIALOG_H
