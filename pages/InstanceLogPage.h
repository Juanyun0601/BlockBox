#ifndef INSTANCELOGPAGE_H
#define INSTANCELOGPAGE_H

#include <QWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

class InstanceLogPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceLogPage(QWidget *parent = nullptr);
    ~InstanceLogPage();

    void setInstancePath(const QString &path);
    void addLauncherLog(const QString &detail);
    void reset();
    /** 重新加载日志（由顶栏刷新按钮 / F5 触发） */
    void loadGameLog();

signals:
    void logCleared();

private:
    void initUI();
    void loadLauncherLogHistory();
    QString formatLogLine(const QString &line);
    void exportLog();

    QString m_instancePath;
    QTabBar *m_tabBar;
    QStackedWidget *m_stack;

    // 游戏日志
    QTextEdit *m_gameLogView;

    // 启动日志
    QTextEdit *m_launcherLogView;

    // 底部操作栏
    QPushButton *m_clearBtn;
    QPushButton *m_exportBtn;
    QLabel *m_lineCountLabel;

    QTimer *m_autoScrollTimer;
};

#endif // INSTANCELOGPAGE_H
