#ifndef LAUNCHDETAILSPAGE_H
#define LAUNCHDETAILSPAGE_H

#include "components/OutlinedLabel.h"
#include "components/OutlinedTextEdit.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

class QTextDocument;

class LaunchDetailsPage : public QWidget
{
    Q_OBJECT

public:
    explicit LaunchDetailsPage(QWidget *parent = nullptr);
    ~LaunchDetailsPage();

    void setDetails(const QStringList &details);
    void addDetail(const QString &detail);
    void clearDetails();
    void setLaunchStatus(const QString &status, bool isRunning);

    /// 单条启动日志按内容着色为 HTML（启动详情页与启动日志弹窗共用）
    static QString formatLogMessage(const QString &message);
    /// 给日志文本控件套用统一配色（与 formatLogMessage 输出的 CSS 类对应）
    static void applyLogStyleSheet(QTextDocument *document);

signals:
    /** 「取消启动」被点击：由主窗口路由到当前聚焦会话（多实例时可能是并行会话） */
    void cancelLaunchRequested();

private:
    void initUI();
    void exportLog();

    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_headerLayout;

    QWidget *m_statusDot;
    OutlinedLabel *m_statusLabel;
    QPushButton *m_cancelBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_exportBtn;
    OutlinedTextEdit *m_detailsTextEdit;
};

#endif // LAUNCHDETAILSPAGE_H
