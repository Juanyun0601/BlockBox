/**
 * @file   PluginRunnerDialog.h
 * @brief  插件脚本执行进度对话框
 * @author BlockBox Team
 * @date   2026-08-09
 *
 * 用于运行插件 command（run:script.ps1）时显示实时进度：
 *   - 解析 stdout 中的协议行：
 *       BBPROGRESS:<0-100>      -> 设置进度条百分比
 *       BBSTATUS:<text>         -> 设置状态文本
 *       BBRESULT:<text>         -> 追加到结果区（执行完成后默认展开）
 *   - 其他输出原样追加到详细日志
 *   - 支持取消（终止进程），完成后转为"关闭"
 */
#ifndef PLUGINRUNNERDIALOG_H
#define PLUGINRUNNERDIALOG_H

#include <QDialog>
#include <QProcess>

class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;

class PluginRunnerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PluginRunnerDialog(QWidget *parent = nullptr);
    ~PluginRunnerDialog() override;

    /** 启动 PowerShell 脚本（解压已完成，传入绝对路径 + 额外参数） */
    void startPs1(const QString &scriptPath, const QStringList &extraArgs = {});

    /** 设置对话框标题（通常为插件名 + 命令名） */
    void setDialogTitle(const QString &title);

private slots:
    void onStdout();
    void onStderr();
    void onFinished(int code, QProcess::ExitStatus status);
    void onCancel();

private:
    void parseLine(const QString &line);
    void appendDetail(const QString &text);
    void setRunning(bool running);

    QLabel *m_titleLabel;
    QLabel *m_statusLabel;
    QProgressBar *m_progress;
    QPlainTextEdit *m_detail;
    QPushButton *m_cancelBtn;
    QProcess *m_proc;
    bool m_finished;
    int m_lastPercent;
};

#endif // PLUGINRUNNERDIALOG_H
