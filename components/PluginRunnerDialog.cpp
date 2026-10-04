/**
 * @file   PluginRunnerDialog.cpp
 * @brief  插件脚本执行进度对话框实现
 * @author BlockBox Team
 * @date   2026-08-09
 */
#include "PluginRunnerDialog.h"

#include <QApplication>
#include <QStyle>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QMessageBox>
#include "utils/ThemeManager.h"

PluginRunnerDialog::PluginRunnerDialog(QWidget *parent)
    : QDialog(parent)
    , m_proc(nullptr)
    , m_finished(false)
    , m_lastPercent(-1)
{
    setWindowTitle(tr("正在执行插件命令"));
    setObjectName(QStringLiteral("pluginRunnerDialog"));
    setModal(true);
    setMinimumSize(520, 280);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(10);

    m_titleLabel = new QLabel(tr("准备执行…"), this);
    m_titleLabel->setObjectName(QStringLiteral("pluginRunnerTitle"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);
    root->addWidget(m_titleLabel);

    m_statusLabel = new QLabel(tr("初始化中…"), this);
    m_statusLabel->setObjectName(QStringLiteral("pluginRunnerStatus"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("pluginRunnerProgress"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setAlignment(Qt::AlignCenter);
    root->addWidget(m_progress);

    m_detail = new QPlainTextEdit(this);
    m_detail->setObjectName(QStringLiteral("pluginRunnerDetail"));
    m_detail->setReadOnly(true);
    m_detail->setMaximumBlockCount(5000);
    m_detail->setPlaceholderText(tr("（详细输出将在此显示）"));
    m_detail->setMinimumHeight(110);
    root->addWidget(m_detail, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_cancelBtn = new QPushButton(tr("取消"), this);
    m_cancelBtn->setObjectName(QStringLiteral("pluginRunnerCancelBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setFixedHeight(32);
    m_cancelBtn->setFixedWidth(96);
    connect(m_cancelBtn, &QPushButton::clicked, this, &PluginRunnerDialog::onCancel);
    btnRow->addWidget(m_cancelBtn);
    root->addLayout(btnRow);

    setRunning(false);

    // 跟随应用主题
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString themeHex = tm->currentThemeColor();
    const QString cardBg    = isLight ? "#ffffff" : "#2e2e32";
    const QString cardBorder= isLight ? "#e6e6e6" : "#45454a";
    const QString textMain  = isLight ? "#202124" : "#ececef";
    const QString textSub   = isLight ? "#6b6b6b" : "#a5a5ad";
    const QString fieldBg   = isLight ? "#f7f7f7" : "#3b3b40";
    const QString hoverBg   = isLight ? "#f6f8ff" : "#38383d";
    const QString dangerBg  = isLight ? "#fef2f2" : "#3a2626";
    const QString dangerFg  = "#e5484d";
    const QString dangerBrd = isLight ? "#e5b0b2" : "#7a3a3c";

    setStyleSheet(QString(
        "QDialog#pluginRunnerDialog { background-color: %1; }"
        "QLabel#pluginRunnerTitle { color: %2; background: transparent; }"
        "QLabel#pluginRunnerStatus { color: %3; background: transparent; }"
        "QProgressBar#pluginRunnerProgress {"
        "  background-color: %4; border: 1px solid %5; border-radius: 6px;"
        "  text-align: center; color: %2; font-size: 12px; min-height: 18px;"
        "}"
        "QProgressBar#pluginRunnerProgress::chunk {"
        "  background-color: %6; border-radius: 5px;"
        "}"
        "QPlainTextEdit#pluginRunnerDetail {"
        "  background-color: %4; border: 1px solid %5; border-radius: 8px;"
        "  color: %3; font-size: 12px; font-family: Consolas, 'Courier New', monospace;"
        "}"
        "QPushButton#pluginRunnerCancelBtn {"
        "  background-color: %1; color: %2; border: 1px solid %5; border-radius: 8px;"
        "  padding: 5px 16px; font-size: 12px;"
        "}"
        "QPushButton#pluginRunnerCancelBtn:hover { background-color: %7; border-color: %6; }"
        "QPushButton#pluginRunnerCancelBtn[finished=\"true\"] {"
        "  background-color: %6; color: #ffffff; border: none;"
        "}"
        "QPushButton#pluginRunnerCancelBtn[finished=\"true\"]:hover { background-color: %6; }"
    ).arg(cardBg, textMain, textSub, fieldBg, cardBorder, themeHex, hoverBg));

    Q_UNUSED(dangerBg); Q_UNUSED(dangerFg); Q_UNUSED(dangerBrd);
}

PluginRunnerDialog::~PluginRunnerDialog()
{
    if (m_proc) {
        if (m_proc->state() != QProcess::NotRunning) {
            disconnect(m_proc, nullptr, this, nullptr);
            m_proc->kill();
            m_proc->waitForFinished(1500);
        }
        m_proc->deleteLater();
    }
}

void PluginRunnerDialog::setDialogTitle(const QString &title)
{
    setWindowTitle(title);
    m_titleLabel->setText(title);
}

void PluginRunnerDialog::startPs1(const QString &scriptPath, const QStringList &extraArgs)
{
    m_detail->clear();
    m_progress->setValue(0);
    m_lastPercent = -1;
    m_finished = false;
    m_statusLabel->setText(tr("正在启动 PowerShell…"));
    appendDetail(tr("脚本：%1").arg(scriptPath));

    if (m_proc) {
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(2000);
        }
        m_proc->deleteLater();
    }
    m_proc = new QProcess(this);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, &PluginRunnerDialog::onStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, &PluginRunnerDialog::onStderr);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PluginRunnerDialog::onFinished);

    setRunning(true);

    QStringList psArgs;
    psArgs << QStringLiteral("-NoProfile") << QStringLiteral("-ExecutionPolicy")
           << QStringLiteral("Bypass") << QStringLiteral("-WindowStyle")
           << QStringLiteral("Hidden") << QStringLiteral("-File")
           << QDir::toNativeSeparators(scriptPath);
    psArgs << extraArgs;

    m_proc->start(QStringLiteral("powershell"), psArgs);
}

void PluginRunnerDialog::onStdout()
{
    if (!m_proc)
        return;
    const QByteArray raw = m_proc->readAllStandardOutput();
    // PowerShell 默认输出可能为 UTF-16 或 UTF-8：优先按 UTF-8 解析
    QString text = QString::fromUtf8(raw);
    // 按行拆分解析协议
    int start = 0;
    while (start <= text.size()) {
        int nl = text.indexOf(QLatin1Char('\n'), start);
        QString line = (nl < 0) ? text.mid(start) : text.mid(start, nl - start);
        // 去除 CR
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        parseLine(line);
        if (nl < 0)
            break;
        start = nl + 1;
    }
}

void PluginRunnerDialog::onStderr()
{
    if (!m_proc)
        return;
    const QByteArray raw = m_proc->readAllStandardError();
    QString text = QString::fromUtf8(raw);
    appendDetail(text);
}

void PluginRunnerDialog::onFinished(int code, QProcess::ExitStatus status)
{
    m_finished = true;
    setRunning(false);

    QString msg;
    if (status != QProcess::NormalExit && code == 0) {
        // 退出状态异常（如被 kill）
        msg = tr("已取消");
        m_progress->setValue(0);
    } else if (code == 0) {
        msg = tr("执行完成");
        m_progress->setValue(100);
    } else {
        msg = tr("执行结束（退出码 %1）").arg(code);
        m_progress->setValue(100);
    }
    m_statusLabel->setText(msg);
    appendDetail(QStringLiteral("—— %1 ——").arg(msg));

    m_cancelBtn->setText(tr("关闭"));
    m_cancelBtn->setProperty("finished", QVariant(true));
    m_cancelBtn->style()->unpolish(m_cancelBtn);
    m_cancelBtn->style()->polish(m_cancelBtn);
}

void PluginRunnerDialog::onCancel()
{
    if (m_finished) {
        accept();
        return;
    }
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        // 先尝试优雅关闭，再强制
        m_proc->terminate();
        if (!m_proc->waitForFinished(2000)) {
            m_proc->kill();
            m_proc->waitForFinished(2000);
        }
    }
    onFinished(1, QProcess::CrashExit);
}

void PluginRunnerDialog::parseLine(const QString &line)
{
    if (line.isEmpty())
        return;
    // 协议行：BBPROGRESS:<0-100>
    if (line.startsWith(QLatin1String("BBPROGRESS:"))) {
        const QString rest = line.mid(11).trimmed();
        int pct = rest.toInt();
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        if (pct != m_lastPercent) {
            m_progress->setValue(pct);
            m_lastPercent = pct;
        }
        return;
    }
    // 协议行：BBSTATUS:<text>
    if (line.startsWith(QLatin1String("BBSTATUS:"))) {
        m_statusLabel->setText(line.mid(9));
        return;
    }
    // 协议行：BBRESULT:<text>
    if (line.startsWith(QLatin1String("BBRESULT:"))) {
        appendDetail(line.mid(9));
        return;
    }
    // 普通输出：追加到详细日志
    appendDetail(line);
}

void PluginRunnerDialog::appendDetail(const QString &text)
{
    if (text.isEmpty())
        return;
    m_detail->appendPlainText(text);
}

void PluginRunnerDialog::setRunning(bool running)
{
    m_cancelBtn->setText(running ? tr("取消") : tr("关闭"));
    m_cancelBtn->setProperty("finished", QVariant(!running));
}
