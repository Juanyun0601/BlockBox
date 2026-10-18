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
#include <QSet>
#include <QPoint>
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
class QGraphicsOpacityEffect;
class QPropertyAnimation;

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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSearchTextChanged(const QString &text);
    void onAddServerClicked();
    void onDeleteServerClicked();
    void onEditServerClicked(const QString &name);
    void onSelectAllClicked();
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
    QString serversDatPath() const;
    /** 把一条服务器写入/更新到游戏列表 servers.dat */
    void upsertGameServer(const ServerEntry &entry);
    /** 从游戏列表 servers.dat 删除一条服务器 */
    void removeGameServer(const ServerEntry &entry);
    /** 重新填充分类筛选下拉框（保留当前选中） */
    void reloadCategoryCombo();
    /** 按当前排序模式重排服务器列表副本（名称/地址，忽略大小写） */
    void sortServers(QList<ServerEntry> &list) const;
    /** 右键「设置分类」子菜单（key 为服务器地址） */
    void appendCategoryMenu(QMenu *menu, const QString &key);
    /** 切换单个卡片选中状态（单击） */
    void toggleCardSelection(int cardIdx);
    /** 清除全部选中状态 */
    void deselectAllCards();
    /** 滑动快速多选：查找全局坐标命中的卡片索引 */
    int cardIndexAtGlobal(const QPoint &globalPos) const;
    /** 滑动快速多选：将卡片加入选中集合（只增不减） */
    void selectCardDuringDrag(int cardIdx);
    /** 反向滑动快速取消：将卡片从选中集合移除 */
    void deselectCardDuringDrag(int cardIdx);
    /** 创建卡片的选中高亮遮罩并缓存 */
    QWidget *createSelectionOverlay(QWidget *card);
    /** 播放卡片选中高亮过渡动画 */
    void animateCardHighlight(int idx, bool selected);
    /** 选中高亮遮罩样式（随主题色） */
    QString selectionOverlayStyle() const;
    /** 刷新统计栏文案与底栏按钮（全选/删除选中）状态 */
    void updateSelectionBar();

    QLineEdit *m_searchEdit;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    QLabel *m_emptyLabel;

    QWidget *m_bottomBar;
    QPushButton *m_addServerBtn;
    QPushButton *m_deleteServerBtn;
    QPushButton *m_selectAllBtn;

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

    // 多选状态
    QList<QWidget *> m_cardWidgets;               // 当前可见卡片（与卡片索引一一对应）
    QList<QWidget *> m_selectionOverlays;         // 选中高亮遮罩（与卡片索引对齐）
    QList<QGraphicsOpacityEffect *> m_selectionEffects;
    QList<QPropertyAnimation *> m_selectionAnims;
    QSet<int> m_selectedCardIndices;              // 多选索引集合
    bool m_allSelected = false;
    // 滑动快速多选状态
    bool m_dragSelectActive = false;
    bool m_dragMoved = false;
    QPoint m_dragPressPos;
    int m_dragAnchorIdx = -1;
    int m_dragLastHoverIdx = -1;
};

#endif
