#include "InstanceLogPage.h"
#include "utils/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QScrollBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QTextOption>

InstanceLogPage::InstanceLogPage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
}

InstanceLogPage::~InstanceLogPage()
{
}

void InstanceLogPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 16, 24, 16);
    mainLayout->setSpacing(12);

    // 标签栏 + 操作按钮
    QWidget *headerWidget = new QWidget(this);
    QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);

    m_tabBar = new QTabBar(this);
    m_tabBar->addTab(tr("游戏日志"));
    m_tabBar->addTab(tr("启动日志"));
    m_tabBar->setObjectName("instanceLogTabBar");

    m_clearBtn = new QPushButton(tr("清除"));
    m_clearBtn->setObjectName("logClearBtn");
    m_clearBtn->setCursor(Qt::PointingHandCursor);

    m_exportBtn = new QPushButton(tr("导出"));
    m_exportBtn->setObjectName("logExportBtn");
    m_exportBtn->setCursor(Qt::PointingHandCursor);

    m_lineCountLabel = new QLabel(this);
    m_lineCountLabel->setObjectName("logLineCountLabel");

    headerLayout->addWidget(m_tabBar);
    headerLayout->addStretch();
    headerLayout->addWidget(m_lineCountLabel);
    headerLayout->addWidget(m_clearBtn);
    headerLayout->addWidget(m_exportBtn);

    // 日志内容栈
    m_stack = new QStackedWidget(this);

    // 游戏日志视图
    m_gameLogView = new QTextEdit(this);
    m_gameLogView->setObjectName("instanceLogTextEdit");
    m_gameLogView->setReadOnly(true);
    m_gameLogView->setUndoRedoEnabled(false);
    m_gameLogView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_gameLogView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_gameLogView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QFont logFont("Consolas", 11);
    logFont.setStyleHint(QFont::Monospace);
    m_gameLogView->setFont(logFont);
    m_gameLogView->document()->setDefaultStyleSheet(
        "body { font-family: Consolas, 'Courier New', monospace; font-size: 11pt; }"
        ".error { color: #D32F2F; font-weight: bold; }"
        ".warn { color: #E65100; }"
        ".info { color: #388E3C; }"
        ".debug { color: #78909C; }"
        ".system { color: " + ThemeManager::instance()->getInfoColorHover() + "; }"
        ".highlight { color: #7B1FA2; }"
    );
    m_stack->addWidget(m_gameLogView);

    // 启动日志视图
    m_launcherLogView = new QTextEdit(this);
    m_launcherLogView->setObjectName("instanceLogTextEdit");
    m_launcherLogView->setReadOnly(true);
    m_launcherLogView->setUndoRedoEnabled(false);
    m_launcherLogView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_launcherLogView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_launcherLogView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_launcherLogView->setFont(logFont);
    m_launcherLogView->document()->setDefaultStyleSheet(
        "body { font-family: Consolas, 'Courier New', monospace; font-size: 11pt; }"
        ".error { color: #D32F2F; font-weight: bold; }"
        ".warn { color: #E65100; }"
        ".info { color: #388E3C; }"
        ".debug { color: #78909C; }"
        ".system { color: " + ThemeManager::instance()->getInfoColorHover() + "; }"
        ".highlight { color: #7B1FA2; }"
    );
    m_stack->addWidget(m_launcherLogView);

    m_stack->setCurrentIndex(0);

    mainLayout->addWidget(headerWidget);
    mainLayout->addWidget(m_stack, 1);

    // 连接信号
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        // 更新行数统计
        QTextEdit *view = (index == 0) ? m_gameLogView : m_launcherLogView;
        int lines = view->document()->blockCount();
        m_lineCountLabel->setText(tr("%1 行").arg(lines));
    });

    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        QTextEdit *view = (m_tabBar->currentIndex() == 0) ? m_gameLogView : m_launcherLogView;
        view->clear();
        m_lineCountLabel->setText(tr("0 行"));
        emit logCleared();
    });
    connect(m_exportBtn, &QPushButton::clicked, this, &InstanceLogPage::exportLog);
}

void InstanceLogPage::setInstancePath(const QString &path)
{
    m_instancePath = path;
    loadGameLog();
}

void InstanceLogPage::loadGameLog()
{
    m_gameLogView->clear();

    if (m_instancePath.isEmpty())
    {
        m_gameLogView->append(formatLogLine(tr("请先选择一个实例")));
        m_lineCountLabel->setText(tr("0 行"));
        return;
    }

    QString logPath = m_instancePath + "/logs/latest.log";
    QFile file(logPath);
    if (!file.exists())
    {
        m_gameLogView->append(formatLogLine(tr("日志文件不存在: %1").arg(logPath)));
        m_lineCountLabel->setText(tr("0 行"));
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_gameLogView->append(formatLogLine(tr("无法打开日志文件: %1").arg(file.errorString())));
        m_lineCountLabel->setText(tr("0 行"));
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QString content = stream.readAll();
    file.close();

    QStringList lines = content.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines)
    {
        m_gameLogView->append(formatLogLine(line));
    }

    // 自动滚动到底部
    QScrollBar *scrollBar = m_gameLogView->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());

    m_lineCountLabel->setText(tr("%1 行").arg(lines.size()));
}

void InstanceLogPage::loadLauncherLogHistory()
{
    // 启动日志通过 addLauncherLog() 实时添加，此处仅更新行数统计
    int lines = m_launcherLogView->document()->blockCount();
    m_lineCountLabel->setText(tr("%1 行").arg(lines));
}

void InstanceLogPage::addLauncherLog(const QString &detail)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString escapedMsg = detail.toHtmlEscaped().replace("\n", "<br>");

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

    QString formatted = QString(
        "<span class='system'>[%1]</span> "
        "<span class='%2'>%3</span>"
    ).arg(timestamp, cssClass, escapedMsg);

    m_launcherLogView->append(formatted);

    QScrollBar *scrollBar = m_launcherLogView->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());

    // 更新行数（仅在启动日志标签页激活时）
    if (m_tabBar->currentIndex() == 1)
    {
        int lines = m_launcherLogView->document()->blockCount();
        m_lineCountLabel->setText(tr("%1 行").arg(lines));
    }
}

QString InstanceLogPage::formatLogLine(const QString &line)
{
    QString escapedMsg = line.toHtmlEscaped();

    QString cssClass = "system";
    if (escapedMsg.contains("[ERROR]") || escapedMsg.contains("error") ||
        escapedMsg.contains("ERROR") || escapedMsg.contains("Exception") ||
        escapedMsg.contains("FATAL") || escapedMsg.contains("崩溃") ||
        escapedMsg.contains("失败")) {
        cssClass = "error";
    } else if (escapedMsg.contains("[WARN]") || escapedMsg.contains("warn") ||
               escapedMsg.contains("WARNING") || escapedMsg.contains("警告")) {
        cssClass = "warn";
    } else if (escapedMsg.contains("[INFO]") || escapedMsg.contains("info") ||
               escapedMsg.contains("INFO")) {
        cssClass = "info";
    } else if (escapedMsg.contains("[DEBUG]") || escapedMsg.contains("debug") ||
               escapedMsg.contains("DEBUG")) {
        cssClass = "debug";
    } else if (escapedMsg.contains("===")) {
        cssClass = "highlight";
    }

    return QString("<span class='%1'>%2</span>").arg(cssClass, escapedMsg);
}

void InstanceLogPage::reset()
{
    m_gameLogView->clear();
    m_launcherLogView->clear();
    m_lineCountLabel->setText(tr("0 行"));
    m_tabBar->setCurrentIndex(0);
}

void InstanceLogPage::exportLog()
{
    QTextEdit *view = (m_tabBar->currentIndex() == 0) ? m_gameLogView : m_launcherLogView;
    QString plainText = view->toPlainText();
    if (plainText.isEmpty())
    {
        QMessageBox::information(this, tr("导出日志"), tr("日志为空，无法导出"));
        return;
    }

    QString tabName = (m_tabBar->currentIndex() == 0) ? tr("游戏日志") : tr("启动日志");
    QString defaultName = QString("BlockBox_%1_%2.txt")
        .arg(tabName, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("导出%1").arg(tabName),
        defaultName,
        tr("文本文件 (*.txt);;所有文件 (*)")
    );

    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
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
