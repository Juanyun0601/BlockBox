/**
 * @file   CrashReportDialog.cpp
 * @brief  崩溃报告确认弹窗实现
 * @author BlockBox Team
 * @date   2026-11-27
 */
#include "components/CrashReportDialog.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "components/UserAgreementDialog.h"
#include "utils/ThemeManager.h"

CrashReportDialog::CrashReportDialog(const CrashReport &report, QWidget *parent)
    : AppDialogBase(parent)
    , m_report(report)
{
    setObjectName(QStringLiteral("crashReportDialog"));
    setWindowTitle(tr("上次运行没有正常结束"));
    initUI();
}

bool CrashReportDialog::consentChecked() const
{
    return m_consentBox && m_consentBox->isChecked();
}

bool CrashReportDialog::autoSendChecked() const
{
    return m_autoSendBox && m_autoSendBox->isChecked();
}

void CrashReportDialog::applyButtonState()
{
    if (m_sendButton)
        m_sendButton->setEnabled(consentChecked());
}

void CrashReportDialog::initUI()
{
    ThemeManager *theme = ThemeManager::instance();
    const QString themeColor = theme->currentThemeColor();
    const bool isLight = (theme->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString textColor = isLight ? "#3f3f46" : "#d4d4d8";
    const QString subColor = isLight ? "#7a7a82" : "#9a9aa2";

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("crashReportCard"));
    card->setMinimumWidth(600);
    card->setMaximumWidth(800);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 18, 24, 16);
    cardLayout->setSpacing(8);

    // ── 标题 ──
    QHBoxLayout *titleRow = new QHBoxLayout();
    titleRow->setSpacing(12);

    QLabel *icon = new QLabel(card);
    icon->setFixedSize(38, 38);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; color: #ffffff; border-radius: 19px;"
        " font-size: 20px; font-weight: 800; }").arg(themeColor));
    icon->setText(QStringLiteral("!"));
    titleRow->addWidget(icon, 0, Qt::AlignTop);

    QVBoxLayout *titleCol = new QVBoxLayout();
    titleCol->setSpacing(3);
    QLabel *title = new QLabel(tr("方块盒子上次没有正常结束"), card);
    title->setStyleSheet(QStringLiteral("color: %1; font-size: 16px; font-weight: 800;")
                             .arg(textColor));
    titleCol->addWidget(title);

    QLabel *summary = new QLabel(m_report.summary(), card);
    summary->setWordWrap(true);
    summary->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px;").arg(subColor));
    titleCol->addWidget(summary);
    titleRow->addLayout(titleCol, 1);
    cardLayout->addLayout(titleRow);

    // ── 诊断信息 ──
    QLabel *intro = new QLabel(
        tr("这份诊断信息能帮开发者更快定位闪退原因。报告只包含版本、系统、调用栈和"
           "出事前的最后一段运行日志，上传前会自动隐藏用户名、文件路径、IP 和登录令牌。"),
        card);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; line-height: 1.5;")
                             .arg(textColor));
    cardLayout->addWidget(intro);

    QTextBrowser *details = new QTextBrowser(card);
    details->setObjectName(QStringLiteral("crashReportDetails"));
    details->setReadOnly(true);
    details->setOpenExternalLinks(false);
    details->setFixedHeight(150);
    details->setFrameShape(QFrame::NoFrame);
    details->setPlainText(m_report.displayText());
    details->setStyleSheet(QStringLiteral(
        "QTextBrowser#crashReportDetails { background: rgba(0, 0, 0, 24);"
        " border: 1px solid %1; border-radius: 10px; padding: 8px;"
        " color: %2; font-family: Consolas, 'Cascadia Mono', monospace;"
        " font-size: 11.5px; }").arg(cardBorder, textColor));
    cardLayout->addWidget(details);

    // ── 隐私说明 / 隐私政策入口 ──
    QLabel *privacy = new QLabel(
        tr("想先了解我们怎么处理这些信息？<a href=\"privacy\" style=\"color:%1\">查看《隐私政策》</a>")
            .arg(themeColor),
        card);
    privacy->setTextFormat(Qt::RichText);
    privacy->setWordWrap(true);
    privacy->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;").arg(subColor));
    connect(privacy, &QLabel::linkActivated, this, [this](const QString &) {
        UserAgreementDialog::showDocument(UserAgreementDialog::PrivacyPolicy, this);
    });
    cardLayout->addWidget(privacy);

    // ── 复选项 ──
    const QString checkStyle = QStringLiteral(
        "QCheckBox { color: %1; font-size: 12.5px; spacing: 7px; }"
        "QCheckBox::indicator { width: 17px; height: 17px; border-radius: 4px;"
        " border: 1px solid %2; background: transparent; }"
        "QCheckBox::indicator:checked { background: %3; border-color: %3; }")
        .arg(textColor, cardBorder, themeColor);

    m_consentBox = new QCheckBox(
        tr("同意发送这份崩溃报告和出问题前的运行日志（已自动脱敏）"), card);
    m_consentBox->setChecked(true);
    m_consentBox->setStyleSheet(checkStyle);
    cardLayout->addWidget(m_consentBox);

    m_autoSendBox = new QCheckBox(
        tr("以后自动发送崩溃报告，不再每次询问"), card);
    m_autoSendBox->setChecked(CrashReporter::autoSendEnabled());
    m_autoSendBox->setStyleSheet(checkStyle);
    cardLayout->addWidget(m_autoSendBox);

    connect(m_consentBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked && m_autoSendBox)
            m_autoSendBox->setChecked(false);
        applyButtonState();
    });

    // ── 按钮 ──
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    buttons->setContentsMargins(0, 4, 0, 0);

    QPushButton *deleteButton = new QPushButton(tr("删除本地报告"), card);
    deleteButton->setCursor(Qt::PointingHandCursor);
    deleteButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none;"
        " padding: 6px 2px; font-size: 12px; text-decoration: underline; }")
        .arg(subColor));
    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        m_choice = Delete;
        reject();
    });
    buttons->addWidget(deleteButton, 0, Qt::AlignVCenter);

    QPushButton *copyButton = new QPushButton(tr("复制报告"), card);
    copyButton->setCursor(Qt::PointingHandCursor);
    copyButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 10px; padding: 6px 14px; font-size: 12.5px; }"
        "QPushButton:hover { background-color: rgba(0, 0, 0, 20); }")
        .arg(cardBg, textColor, cardBorder));
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_report.displayText());
    });
    buttons->addWidget(copyButton, 0, Qt::AlignVCenter);

    buttons->addStretch();

    QPushButton *laterButton = new QPushButton(tr("暂不发送"), card);
    laterButton->setCursor(Qt::PointingHandCursor);
    laterButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 10px; padding: 7px 18px; font-size: 13px; }"
        "QPushButton:hover { background-color: rgba(0, 0, 0, 26); }")
        .arg(cardBg, textColor, cardBorder));
    connect(laterButton, &QPushButton::clicked, this, [this]() {
        m_choice = Later;
        reject();
    });
    buttons->addWidget(laterButton, 0, Qt::AlignVCenter);

    m_sendButton = new QPushButton(tr("发送报告"), card);
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: #ffffff; border: none;"
        " border-radius: 10px; padding: 7px 20px; font-size: 13px; font-weight: 700; }"
        "QPushButton:disabled { background-color: #b9b9b9; color: #f2f2f2; }")
        .arg(themeColor));
    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        m_choice = Send;
        accept();
    });
    buttons->addWidget(m_sendButton, 0, Qt::AlignVCenter);

    cardLayout->addLayout(buttons);

    card->setStyleSheet(QStringLiteral(
        "QWidget#crashReportCard { background-color: %1; border: 1px solid %2;"
        " border-radius: 16px; }").arg(cardBg, cardBorder));

    rootLayout->addWidget(card, 0, Qt::AlignCenter);
    applyButtonState();
}
