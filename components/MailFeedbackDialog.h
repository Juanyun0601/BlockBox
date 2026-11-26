/**
 * @file   MailFeedbackDialog.h
 * @brief  「邮件反馈」对话框：把问题写进邮件，交给用户自己的邮箱发送
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 反馈页的「邮件反馈」入口使用：不登录论坛账号也能把问题发到官方邮箱。
 * 正文预先填好版本与系统信息，用户点「打开邮件客户端」即可发送；
 * 没装邮件客户端时也能复制正文，粘到网页邮箱里发。
 */
#pragma once

#include <QDialog>

class QLineEdit;
class QTextEdit;

class MailFeedbackDialog : public QDialog
{
    Q_OBJECT

public:
    /** 方块盒子官方反馈邮箱（正式换邮箱只改这一处） */
    static QString feedbackAddress();

    explicit MailFeedbackDialog(QWidget *parent = nullptr);

private:
    void buildUi();
    QString subjectText() const;
    QString bodyText() const;
    void openMailClient();
    void copyAddress();
    void copyBody();

    QLineEdit *m_subjectEdit = nullptr;
    QTextEdit *m_bodyEdit = nullptr;
};
