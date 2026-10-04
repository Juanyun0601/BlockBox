#ifndef LAUNCHDETAILSPAGE_H
#define LAUNCHDETAILSPAGE_H

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

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

private:
    void initUI();
    QString formatLogMessage(const QString &message);
    void exportLog();

    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_headerLayout;

    QWidget *m_statusDot;
    QLabel *m_statusLabel;
    QPushButton *m_cancelBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_exportBtn;
    QTextEdit *m_detailsTextEdit;
};

#endif // LAUNCHDETAILSPAGE_H
