/**
 * @file   CrashReportDialog.h
 * @brief  下次启动时的崩溃报告确认弹窗
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 展示上次异常退出的时间、原因、调用栈和最后一段日志，
 * 让用户明确同意后再上传；也可以选择暂不发送或删除这份本地记录。
 */
#ifndef CRASHREPORTDIALOG_H
#define CRASHREPORTDIALOG_H

#include "components/AppDialogBase.h"
#include "utils/CrashReporter.h"

class QCheckBox;
class QLabel;
class QPushButton;

class CrashReportDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /// 用户离开弹窗时选择的动作
    enum Choice {
        Later = 0, ///< 暂不发送（保留本地报告，下次启动再问）
        Send,      ///< 同意并发送
        Delete     ///< 删除本地报告
    };

    explicit CrashReportDialog(const CrashReport &report, QWidget *parent = nullptr);

    Choice choice() const { return m_choice; }
    bool consentChecked() const;
    bool autoSendChecked() const;

private:
    void initUI();
    void applyButtonState();

    CrashReport m_report;
    Choice m_choice = Later;

    QCheckBox *m_consentBox = nullptr;
    QCheckBox *m_autoSendBox = nullptr;
    QPushButton *m_sendButton = nullptr;
};

#endif // CRASHREPORTDIALOG_H
