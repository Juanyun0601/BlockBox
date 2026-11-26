#include "LaunchLogDialog.h"

#include "components/AppMessageBox.h"
#include "pages/LaunchDetailsPage.h"
#include "utils/ThemeManager.h"

#include <QClipboard>
#include <QFont>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextOption>
#include <QVBoxLayout>

LaunchLogDialog::LaunchLogDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_logView(nullptr)
    , m_copyBtn(nullptr)
{
    setObjectName(QStringLiteral("launchLogDialog"));

    QGridLayout *mainLayout = new QGridLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 居中内容卡片（AppDialogBase 按 objectName 以 "Card" 结尾定位/缩放卡片）
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("launchLogCard"));
    card->setMinimumSize(520, 420);
    card->setMaximumSize(680, 560);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(12);

    QHBoxLayout *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);

    QLabel *titleLabel = new QLabel(tr("启动日志"), card);
    titleLabel->setObjectName(QStringLiteral("launchLogDialogTitle"));

    m_copyBtn = new QPushButton(tr("复制日志"), card);
    m_copyBtn->setObjectName(QStringLiteral("launchLogCopyBtn"));
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_copyBtn, &QPushButton::clicked, this, &LaunchLogDialog::copyLog);

    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    titleRow->addWidget(m_copyBtn);

    m_logView = new QTextEdit(card);
    m_logView->setObjectName(QStringLiteral("launchLogView"));
    m_logView->setReadOnly(true);
    m_logView->setUndoRedoEnabled(false);
    m_logView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_logView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_logView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_logView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_logView->setFrameShape(QTextEdit::NoFrame);
    QFont logFont("Consolas", 11);
    logFont.setStyleHint(QFont::Monospace);
    m_logView->setFont(logFont);
    LaunchDetailsPage::applyLogStyleSheet(m_logView->document());

    cardLayout->addLayout(titleRow);
    cardLayout->addWidget(m_logView, 1);

    mainLayout->addWidget(card, 0, 0, Qt::AlignCenter);

    initStyle();
    hide();
}

LaunchLogDialog::~LaunchLogDialog() = default;

void LaunchLogDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();

    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg      = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder  = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor  = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor   = isLight ? "#4a4a4a" : "#c9c9c9";
    const QString viewBg      = isLight ? "rgba(0, 0, 0, 14)" : "rgba(255, 255, 255, 18)";
    const QString btnBg       = isLight ? "#f5f5f5" : "#3b3b40";
    const QString btnBorder   = isLight ? "#d4d4d4" : "#55555a";
    const QString btnText     = isLight ? "#333333" : "#e8e8e8";

    QString style = QString(
        "QWidget#launchLogCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 16px;"
        "}"
        ""
        "QLabel#launchLogDialogTitle {"
        "    color: %3;"
        "    background-color: transparent;"
        "    font-size: 15px;"
        "    font-weight: bold;"
        "}"
        ""
        "QTextEdit#launchLogView {"
        "    color: %4;"
        "    background-color: %5;"
        "    border: none;"
        "    border-radius: 10px;"
        "    font-family: Consolas, 'Courier New', monospace;"
        "    font-size: 11pt;"
        "    selection-background-color: %6;"
        "}"
        ""
        "QPushButton#launchLogCopyBtn {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 12px;"
        "    color: %9;"
        "    padding: 4px 14px;"
        "    font-size: 12px;"
        "}"
        ""
        "QPushButton#launchLogCopyBtn:hover {"
        "    background-color: %6;"
        "}"
        ""
        "QTextEdit#launchLogView QScrollBar:vertical {"
        "    background: transparent;"
        "    width: 8px;"
        "    margin: 0;"
        "}"
        ""
        "QTextEdit#launchLogView QScrollBar::handle:vertical {"
        "    background: %10;"
        "    border-radius: 4px;"
        "    min-height: 30px;"
        "}"
        ""
        "QTextEdit#launchLogView QScrollBar::add-line:vertical,"
        "QTextEdit#launchLogView QScrollBar::sub-line:vertical {"
        "    height: 0;"
        "}"
    ).arg(cardBg, cardBorder, titleColor, textColor, viewBg,
          tm->getThemeColorHover(), btnBg, btnBorder, btnText)
     .arg(isLight ? "rgba(0, 0, 0, 60)" : "rgba(255, 255, 255, 70)");

    setStyleSheet(style);
}

void LaunchLogDialog::setLogs(const QStringList &lines)
{
    if (!m_logView)
        return;
    m_logView->clear();
    for (const QString &line : lines) {
        m_logView->append(LaunchDetailsPage::formatLogMessage(line));
    }
    QScrollBar *scrollBar = m_logView->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void LaunchLogDialog::appendLog(const QString &line)
{
    if (!m_logView)
        return;
    m_logView->append(LaunchDetailsPage::formatLogMessage(line));
    QScrollBar *scrollBar = m_logView->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void LaunchLogDialog::copyLog()
{
    if (!m_logView || m_logView->toPlainText().isEmpty())
        return;
    QGuiApplication::clipboard()->setText(m_logView->toPlainText());
    AppMessageBox::information(this, tr("复制日志"), tr("启动日志已复制到剪贴板"));
}
