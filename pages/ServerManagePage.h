#ifndef SERVERMANAGEPAGE_H
#define SERVERMANAGEPAGE_H

#include <QWidget>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QList>

struct ServerEntry
{
    QString name;
    QString address;
    quint16 port = 25565;
    QString description;
};

class ServerManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit ServerManagePage(QWidget *parent = nullptr);
    ~ServerManagePage();

    void setInstancePath(const QString &path);

signals:
    void connectToServerRequested(const QString &address);
    /** 请求快捷启动游戏并自动连接到指定服务器 */
    void quickLaunchServerRequested(const QString &address, quint16 port);

private slots:
    void onSearchTextChanged(const QString &text);
    void onAddServerClicked();
    void onDeleteServerClicked();
    void onEditServerClicked(const QString &name);

private:
    void initUI();
    void loadServers();
    void saveServers();
    void clearCards();
    void placeCards();
    void showEmptyHint(bool show);
    QString serversFilePath() const;

    QLineEdit *m_searchEdit;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    QLabel *m_emptyLabel;

    QWidget *m_bottomBar;
    QPushButton *m_addServerBtn;
    QPushButton *m_deleteServerBtn;

    QList<ServerEntry> m_servers;
    QString m_currentSearch;
    QString m_instancePath;
};

#endif
