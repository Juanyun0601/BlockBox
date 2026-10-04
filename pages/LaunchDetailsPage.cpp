#include "LaunchDetailsPage.h"
#include "utils/GameLauncher.h"
#include "utils/ThemeManager.h"

#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QStyle>
#include <QTextCursor>
#include <QTextOption>
#include <QTextStream>

LaunchDetailsPage::LaunchDetailsPage(QWidget *parent) : QWidget(parent)
{
    initUI();
}

LaunchDetailsPage::~LaunchDetailsPage()
{}

void LaunchDetailsPage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(24, 20, 24, 20);
    m_mainLayout->setSpacing(16);

    // Header
    m_headerLayout = new QHBoxLayout();
    m_headerLayout->setSpacing(10);

    m_statusDot = new QWidget();
    m_statusDot->setObjectName("statusDot");
    m_statusDot->setFixedSize(12, 12);

    m_statusLabel = new QLabel(tr("启动日志"));
    m_statusLabel->setObjectName("logStatusLabel");

    m_cancelBtn = new QPushButton(tr("取消启动"));
    m_cancelBtn->setObjectName("cancelLaunchBtn");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);

    m_clearBtn = new QPushButton(tr("清除日志"));
    m_clearBtn->setObjectName("clearLogBtn");
    m_clearBtn->setCursor(Qt::PointingHandCursor);

    m_exportBtn = new QPushButton(tr("导出日志"));
    m_exportBtn->setObjectName("exportLogBtn");
    m_exportBtn->setCursor(Qt::PointingHandCursor);

    m_headerLayout->addWidget(m_statusDot);
    m_headerLayout->addWidget(m_statusLabel);
    m_headerLayout->addStretch();
    m_headerLayout->addWidget(m_cancelBtn);
    m_headerLayout->addWidget(m_exportBtn);
    m_headerLayout->addWidget(m_clearBtn);

    // Log area
    m_detailsTextEdit = new QTextEdit();
    m_detailsTextEdit->setObjectName("detailsTextEdit");
    m_detailsTextEdit->setReadOnly(true);
    m_detailsTextEdit->setUndoRedoEnabled(false);
    m_detailsTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_detailsTextEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    m_detailsTextEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QFont logFont("Consolas", 11);
    logFont.setStyleHint(QFont::Monospace);
    m_detailsTextEdit->setFont(logFont);
    m_detailsTextEdit->document()->setDefaultStyleSheet(
        "body { font-family: Consolas, 'Courier New', monospace; font-size: 11pt; }"
        ".error { color: #D32F2F; font-weight: bold; }"
        ".warn { color: #E65100; }"
        ".info { color: #388E3C; }"
        ".debug { color: #78909C; }"
        ".system { color: " + ThemeManager::instance()->getInfoColorHover() + "; }"
        ".timestamp { color: #B0BEC5; }"
        ".highlight { color: #7B1FA2; }"
    );

    m_mainLayout->addLayout(m_headerLayout);
    m_mainLayout->addWidget(m_detailsTextEdit, 1);

    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        GameLauncher::instance()->stopGame();
        addDetail("[INFO] " + tr("用户取消了启动"));
    });
    connect(m_clearBtn, &QPushButton::clicked, this, &LaunchDetailsPage::clearDetails);
    connect(m_exportBtn, &QPushButton::clicked, this, &LaunchDetailsPage::exportLog);
}

void LaunchDetailsPage::setLaunchStatus(const QString &status, bool isRunning)
{
    m_statusLabel->setText(status);
    m_cancelBtn->setVisible(isRunning);
    if (isRunning) {
        m_statusDot->setProperty("status", QStringLiteral("running"));
        m_statusDot->setToolTip(tr("运行中"));
    } else {
        m_statusDot->setProperty("status", QStringLiteral("idle"));
        m_statusDot->setToolTip(tr("空闲"));
    }
    m_statusDot->style()->polish(m_statusDot);
}

QString LaunchDetailsPage::formatLogMessage(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString escapedMsg = message.toHtmlEscaped().replace("\n", "<br>");

    QString cssClass = "system";
    if (escapedMsg.contains("[ERROR]") || escapedMsg.contains("错误") ||
        escapedMsg.contains("失败") || escapedMsg.contains("崩溃") ||
        escapedMsg.contains("Exception") || escapedMsg.contains("Error") ||
        escapedMsg.contains("FATAL")) {
        cssClass = "error";
    } else if (escapedMsg.contains("[WARN]") || escapedMsg.contains("警告") ||
               escapedMsg.contains("warning") || escapedMsg.contains("Warning")) {
        cssClass = "warn";
    } else if (escapedMsg.contains("[INFO]") || escapedMsg.contains("完成") ||
               escapedMsg.contains("成功") || escapedMsg.contains("ready") ||
               escapedMsg.contains("启动")) {
        cssClass = "info";
    } else if (escapedMsg.contains("[DEBUG]") || escapedMsg.contains("debug")) {
        cssClass = "debug";
    } else if (escapedMsg.contains("===")) {
        cssClass = "highlight";
    }

    return QString(
        "<span class='timestamp'>[%1]</span> "
        "<span class='%2'>%3</span>"
    ).arg(timestamp, cssClass, escapedMsg);
}

void LaunchDetailsPage::setDetails(const QStringList &details)
{
    clearDetails();
    for (const QString &detail : details) {
        addDetail(detail);
    }
}

void LaunchDetailsPage::addDetail(const QString &detail)
{
    m_detailsTextEdit->append(formatLogMessage(detail));

    QScrollBar *scrollBar = m_detailsTextEdit->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void LaunchDetailsPage::clearDetails()
{
    m_detailsTextEdit->clear();
}

void LaunchDetailsPage::exportLog()
{
    QString plainText = m_detailsTextEdit->toPlainText();
    if (plainText.isEmpty()) {
        QMessageBox::information(this, tr("导出日志"), tr("日志为空，无法导出"));
        return;
    }

    QString defaultName = QString("BlockBox_launch_log_%1.txt")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    
    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("导出启动日志"),
        defaultName,
        tr("文本文件 (*.txt);;所有文件 (*)")
    );

    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("导出日志"), 
            tr("无法写入文件: %1").arg(file.errorString()));
        return;
    }

    QTextStream stream(&file);
    stream << plainText;
    file.close();

    QMessageBox::information(this, tr("导出日志"), 
        tr("日志已成功导出到:\n%1").arg(filePath));
}
