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
#include <QStringList>
#include <QHash>

#include "utils/McServerPinger.h"

struct ServerEntry
{
    QString name;
    QString address;
    quint16 port = 25565;
    QString description;
};

class LocalCategoryManager;
class CardSelectionOverlay;
class QMenu;
class QShowEvent;

class ServerManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit ServerManagePage(QWidget *parent = nullptr);
    ~ServerManagePage();

    void setInstancePath(const QString &path);

    /** 子导航搜索框分发：同步到页内搜索框（复用现有过滤管线） */
    Q_INVOKABLE void applyPageFilter(const QString &query);

    /** 重新扫描磁盘上的服务器列表（游戏内新加的服务器会在这里出现） */
    Q_INVOKABLE void reloadServers();

    /** 顶栏刷新 / F5：重新扫描服务器列表，并重新探测在线状态 */
    Q_INVOKABLE void refresh();

signals:
    void connectToServerRequested(const QString &address);
    /** 请求快捷启动游戏并自动连接到指定服务器 */
    void quickLaunchServerRequested(const QString &address, quint16 port);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

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
    /**
     * 游戏实际写入 servers.dat 的路径（用于写回）：
     * 优先取已存在的那个文件（多个时取最近改动的），都不存在时按版本隔离设置推断
     */
    QString serversDatPath() const;
    /** 游戏可能写出 servers.dat 的全部位置：版本目录 + 共享 .minecraft 目录 */
    QStringList serversDatCandidates() const;
    /** 磁盘上服务器数据的指纹（大小 + 修改时间），用于判断是否需要重新扫描 */
    qint64 diskStamp() const;
    /** 记录「磁盘内容已同步到界面」，避免重复扫描 */
    void markDiskSynced();
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
    /** 刷新统计栏文案与底栏按钮（全选/删除选中）状态 */
    void updateSelectionBar();

    /** 对当前可见的服务器卡片批量发起状态探测（force=true 时忽略新鲜度缓存） */
    void pingServers(bool force = false);
    /** 单条探测；结果通过 McServerPinger::pingFinished 回填 */
    void pingOne(const QString &key, const QString &host, quint16 port);
    /** 把缓存里的探测结果画到卡片上（状态/人数/版本/延迟/MOTD/图标） */
    void applyPingResultToCard(QWidget *card);
    /** 按卡片当前宽度重新省略名称与 MOTD 文案 */
    void elideCardTexts(QWidget *card);

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
    /** 上次扫描磁盘时的指纹；-1 表示尚未扫描 */
    qint64 m_lastDiskStamp = -1;

    // 服务器状态探测（Minecraft Server List Ping）
    McServerPinger *m_pinger = nullptr;
    QHash<QString, McServerPinger::Result> m_pingResults;   // key = ServersDatIO::addressKey
    QHash<QString, qint64> m_pingAtMs;                       // 上次探测时刻，用于节流
    qint64 m_lastPingSweepMs = 0;

    // 多选状态
    QList<QWidget *> m_cardWidgets;               // 当前可见卡片（与卡片索引一一对应）
    QList<CardSelectionOverlay *> m_selectionOverlays; // 选中高亮遮罩（与卡片索引对齐）
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
