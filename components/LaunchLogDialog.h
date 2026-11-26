#ifndef LAUNCHLOGDIALOG_H
#define LAUNCHLOGDIALOG_H

#include "AppDialogBase.h"

#include <QStringList>

class QPushButton;
class QTextDocument;
class QTextEdit;

/**
 * @brief 启动日志弹窗：居中卡片内查看启动过程日志
 *
 * 日志按级别着色（与启动详情页同一套配色），支持打开后实时追加新行；
 * 点击卡片以外的遮罩区域即可关闭，安卓返回键也能关闭。
 */
class LaunchLogDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit LaunchLogDialog(QWidget *parent = nullptr);
    ~LaunchLogDialog() override;

    /// 批量填充已有日志（替换当前内容）
    void setLogs(const QStringList &lines);
    /// 追加一条日志（弹窗可见时实时刷新）
    void appendLog(const QString &line);

private slots:
    void copyLog();

private:
    void initStyle();

    QTextEdit *m_logView;
    QPushButton *m_copyBtn;
};

#endif // LAUNCHLOGDIALOG_H
