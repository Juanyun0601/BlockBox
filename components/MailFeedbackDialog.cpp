/**
 * @file   MailFeedbackDialog.cpp
 * @brief  「邮件反馈」对话框实现
 * @author BlockBox Team
 * @date   2026-11-25
 */

#include "MailFeedbackDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSysInfo>
#include <QTextEdit>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "NotificationManager.h"
#include "utils/AppVersion.h"
#include "utils/StyleKit.h"

QString MailFeedbackDialog::feedbackAddress()
{
    // 官方反馈邮箱：正式上线换邮箱只改这一行，其它地方不用动
    return QStringLiteral("support@mcblockbox.top");
}

MailFeedbackDialog::MailFeedbackDialog(QWidget *parent)
    : QDialog(parent)
{
    buildUi();
}

void MailFeedbackDialog::buildUi()
{
    setWindowTitle(tr("邮件反馈"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(600);
    setMinimumHeight(560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(12);

    auto *title = new QLabel(tr("给方块盒子写邮件"), this);
    title->setStyleSheet(StyleKit::pageTitle());
    layout->addWidget(title);

    // 一句说明：为什么要有这个入口，正文里会带什么
    auto *intro = new QLabel(
        tr("用你自己的邮箱把问题发到方块盒子官方邮箱，不用登录论坛账号；"
           "邮件里已经带上了版本和系统信息，方便我们定位问题。"),
        this);
    intro->setWordWrap(true);
    intro->setStyleSheet(StyleKit::secondaryLabel());
    layout->addWidget(intro);

    // ── 收件人：只读展示，旁边给个复制按钮，网页邮箱用户直接粘 ──
    auto *addrRow = new QHBoxLayout();
    addrRow->setSpacing(8);
    auto *addrTitle = new QLabel(tr("收件人"), this);
    addrTitle->setFixedWidth(44);
    addrTitle->setStyleSheet(StyleKit::mutedLabel());
    auto *addrValue = new QLabel(feedbackAddress(), this);
    addrValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addrValue->setStyleSheet(StyleKit::bodyLabel());
    auto *copyAddressButton = new QPushButton(tr("复制"), this);
    copyAddressButton->setStyleSheet(StyleKit::ghostButton());
    copyAddressButton->setCursor(Qt::PointingHandCursor);
    connect(copyAddressButton, &QPushButton::clicked, this, &MailFeedbackDialog::copyAddress);
    addrRow->addWidget(addrTitle);
    addrRow->addWidget(addrValue);
    addrRow->addStretch(1);
    addrRow->addWidget(copyAddressButton);
    layout->addLayout(addrRow);

    // ── 主题 ──
    auto *subjectRow = new QHBoxLayout();
    subjectRow->setSpacing(8);
    auto *subjectTitle = new QLabel(tr("主题"), this);
    subjectTitle->setFixedWidth(44);
    subjectTitle->setStyleSheet(StyleKit::mutedLabel());
    m_subjectEdit = new QLineEdit(subjectText(), this);
    m_subjectEdit->setMaxLength(120);
    m_subjectEdit->setStyleSheet(StyleKit::lineEdit());
    subjectRow->addWidget(subjectTitle);
    subjectRow->addWidget(m_subjectEdit, 1);
    layout->addLayout(subjectRow);

    // ── 正文：预填问题模板 + 自动附带的诊断信息 ──
    auto *bodyTitle = new QLabel(tr("正文"), this);
    bodyTitle->setStyleSheet(StyleKit::mutedLabel());
    layout->addWidget(bodyTitle);
    m_bodyEdit = new QTextEdit(this);
    m_bodyEdit->setAcceptRichText(false);
    m_bodyEdit->setPlainText(bodyText());
    m_bodyEdit->setMinimumHeight(240);
    m_bodyEdit->setStyleSheet(StyleKit::textEdit());
    layout->addWidget(m_bodyEdit, 1);

    auto *hint = new QLabel(
        tr("如果邮件客户端没有自动弹出，可以点左下角「复制正文」和右上角「复制」收件人，"
           "到网页邮箱里自己发；需要附日志的话，在邮件客户端里手动添加附件。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(StyleKit::captionLabel());
    layout->addWidget(hint);

    // ── 底部按钮 ──
    auto *footer = new QHBoxLayout();
    footer->setSpacing(10);
    auto *copyBodyButton = new QPushButton(tr("复制正文"), this);
    copyBodyButton->setStyleSheet(StyleKit::ghostButton());
    copyBodyButton->setCursor(Qt::PointingHandCursor);
    connect(copyBodyButton, &QPushButton::clicked, this, &MailFeedbackDialog::copyBody);
    footer->addWidget(copyBodyButton);
    footer->addStretch(1);

    auto *cancelButton = new QPushButton(tr("取消"), this);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    cancelButton->setCursor(Qt::PointingHandCursor);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancelButton);

    auto *sendButton = new QPushButton(tr("打开邮件客户端"), this);
    sendButton->setStyleSheet(StyleKit::primaryButton());
    sendButton->setCursor(Qt::PointingHandCursor);
    sendButton->setDefault(true);
    connect(sendButton, &QPushButton::clicked, this, &MailFeedbackDialog::openMailClient);
    footer->addWidget(sendButton);
    layout->addLayout(footer);
}

QString MailFeedbackDialog::subjectText() const
{
    return tr("方块盒子问题反馈（%1）").arg(AppVersion::current());
}

QString MailFeedbackDialog::bodyText() const
{
    // 系统信息写全一点：不同 Windows 小版本的表现可能不一样
    QString os = QSysInfo::prettyProductName();
    const QString kernel = QSysInfo::kernelVersion();
    if (!kernel.isEmpty())
        os += tr("（内核 %1）").arg(kernel);

    return tr("问题描述：\n"
              "（请写清楚遇到了什么问题，越具体越好）\n\n\n"
              "复现步骤：\n"
              "1. \n"
              "2. \n\n\n"
              "期望的结果：\n\n\n"
              "—— 以下信息由方块盒子自动附带，方便定位问题 ——\n"
              "方块盒子版本：%1\n"
              "系统：%2\n")
        .arg(AppVersion::current(), os);
}

void MailFeedbackDialog::openMailClient()
{
    // 正文被清空时提醒一下，免得发出一封空邮件
    const QString body = m_bodyEdit->toPlainText().trimmed();
    if (body.isEmpty())
    {
        NotificationManager::showError(this, tr("邮件正文是空的，写点内容再发送吧。"));
        return;
    }

    const QString subject = m_subjectEdit->text().trimmed();
    QString mailBody = m_bodyEdit->toPlainText();
    mailBody.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));

    QUrl url(QStringLiteral("mailto:") + feedbackAddress());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("subject"), subject);
    query.addQueryItem(QStringLiteral("body"), mailBody);
    url.setQuery(query);

    if (QDesktopServices::openUrl(url))
    {
        accept();
        return;
    }

    // 系统里没有可用的邮件客户端：把正文放进剪贴板，用户还能走网页邮箱
    QApplication::clipboard()->setText(m_bodyEdit->toPlainText());
    NotificationManager::showError(
        this, tr("没找到可用的邮件客户端，正文已复制，可粘贴到网页邮箱里发送。"));
}

void MailFeedbackDialog::copyAddress()
{
    QApplication::clipboard()->setText(feedbackAddress());
    NotificationManager::showInfo(this, tr("收件邮箱已复制。"));
}

void MailFeedbackDialog::copyBody()
{
    QApplication::clipboard()->setText(m_bodyEdit->toPlainText());
    NotificationManager::showInfo(this, tr("正文已复制，粘贴到邮件正文里即可。"));
}
