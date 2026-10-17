#ifndef SERVERMANAGEPAGE_H
#define SERVERMANAGEPAGE_H

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QList>
#include <QString>

struct ServerEntry
{
    QString name;
    QString address;
    quint16 port = 25565;
    QString description;
};

class LocalCategoryManager;
class QMenu;

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
    void onCategoryFilterChanged(int index);
    void onCategoryManageClicked();
    void onCategoriesChanged();
    void onAssignmentsChanged();
    void onSortChanged(int index);

private:
    void initUI();
    void loadServers();
    void saveServers();
    void clearCards();
    void placeCards();
    void showEmptyHint(bool show);
    QString serversFilePath() const;
    /** 重新填充分类筛选下拉框（保留当前选中） */
    void reloadCategoryCombo();
    /** 按当前排序模式重排服务器列表副本（名称/地址，忽略大小写） */
    void sortServers(QList<ServerEntry> &list) const;
    /** 右键「设置分类」子菜单（key 为服务器地址） */
    void appendCategoryMenu(QMenu *menu, const QString &key);

    QLineEdit *m_searchEdit;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    QLabel *m_emptyLabel;

    QWidget *m_bottomBar;
    QPushButton *m_addServerBtn;
    QPushButton *m_deleteServerBtn;

    // 分类 + 排序筛选（分类按实例持久化于 blockbox/categories.json，键=服务器地址）
    LocalCategoryManager *m_categoryManager;
    QComboBox *m_categoryCombo;
    QPushButton *m_categoryManageBtn;
    QComboBox *m_sortCombo;
    QString m_currentCategoryId;      // 空 = 全部；哨兵 = 未分类
    int m_sortMode = 0;               // 0=名称↑ 1=名称↓ 2=地址↑ 3=地址↓

    QList<ServerEntry> m_servers;
    QString m_currentSearch;
    QString m_instancePath;
};

#endif
