/**
 * @file   InstanceModsPage.h
 * @brief  实例管理 - 模组列表页面，含搜索框、筛选和卡片列表
 * @author BlockBox Team
 * @date   2026-06-19
 */
#ifndef INSTANCEMODSPAGE_H
#define INSTANCEMODSPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QLineEdit>
#include <QQueue>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include "../components/ContentViewSwitch.h"
#include "../components/MasonryContentCard.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include "../utils/mod/ModScanner.h"
#include "../utils/mod/ModData.h"

class BlurLoadingOverlay;
class ModNameFetcher;
class ModrinthAPI;
class LocalCategoryManager;
class QGraphicsOpacityEffect;
class QNetworkAccessManager;
class FlowLayout;
class QPropertyAnimation;

class InstanceModsPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceModsPage(QWidget *parent = nullptr);
    ~InstanceModsPage();

    /** 设置当前实例路径并自动扫描模组 */
    void setInstancePath(const QString &path);

    /** 手动刷新模组列表 */
    void refreshModList();

signals:
    /** 点击模组卡片，请求打开详情页 */
    void modDetailRequested(const ModInfo &info);
    /** 搜索无结果时，请求前往下载页搜索 */
    void modDownloadSearchRequested(const QString &keyword);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSearchTextChanged(const QString &text);
    void onFilterChanged(int index);
    void onScanCompleted(const LocalModList &result);
    void onScanFailed(const QString &error);
    void onToggleMod(const ModInfo &info);
    void onDeleteMod(const ModInfo &info);
    void onModContextMenu(const QPoint &pos);
    void onModNameResolved(const ModInfo &info);
    void onModNameFetchFailed(const QString &modId, const QString &englishName);
    void onCheckUpdatesClicked();
    void onHashesMatched(const QMap<QString, ModVersionFile> &versions);
    void onUpdatesChecked(const QMap<QString, ModVersionFile> &versions);
    void onUpdateCheckBatchFailed(const QString &error);
    void onSelectAllClicked();
    void onOpenFolderClicked();
    void onPasteModClicked();
    void onCopyModClicked();
    void onToggleSelectedClicked();
    void onDownloadSearchClicked();
    void onUpdateSelectedClicked();
    void onCategoryFilterChanged(int index);
    void onCategoryManageClicked();
    void onCategoriesChanged();
    void onAssignmentsChanged();

private:
    void initUI();
    void setupConnections();
    void clearCards();
    void placeCards();
    void showLoading(bool show);
    void showEmptyHint(bool show);
    void applyFilterAndSearch();
    void detectInstanceMeta();
    void resetUpdateState();
    void markCardHasUpdate(int index, const ModVersionFile &target);
    void applyCachedUpdates();
    void addCard(const ModInfo &info);
    void addListCard(const ModInfo &info);
    void addMasonryCard(const ModInfo &info);
    QWidget *createMasonryCardWidget(const ModInfo &info, QWidget *parent);
    void rebuildCards();
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    void updateCardNames(int index, const ModInfo &info);
    void refreshCardNameLabels();
    void updateBottomBarState();
    void toggleCardSelection(int idx);
    void deselectAllCards();
    /** 重新填充分类筛选下拉框（保留当前选中） */
    void reloadCategoryCombo();
    /** 刷新卡片上的分类标签（分配变化/重命名后调用） */
    void refreshCardCategoryChips();
    /** 滑动快速多选：查找全局坐标命中的卡片索引 */
    int cardIndexAtGlobal(const QPoint &globalPos) const;
    /** 滑动快速多选：将卡片加入选中集合（只增不减） */
    void selectCardDuringDrag(int idx);
    /** 反向滑动快速取消：将卡片从选中集合移除 */
    void deselectCardDuringDrag(int idx);
    /** 创建卡片的选中高亮遮罩并缓存 */
    QWidget *createSelectionOverlay(QWidget *card);
    /** 播放卡片选中高亮过渡动画 */
    void animateCardHighlight(int idx, bool selected);
    /** 选中高亮遮罩样式（随主题色） */
    QString selectionOverlayStyle() const;
    void processNextModUpdate();
    void markCachedUpdateChips();
    void removeOldModFiles(const ModInfo &mod, const QString &newPath);

    enum FilterMode { FilterAll = 0, FilterEnabled = 1, FilterDisabled = 2 };

    // UI elements
    QLineEdit *m_searchEdit;
    QComboBox *m_filterCombo;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    FlowLayout *m_cardFlowLayout;
    ContentViewSwitch *m_viewSwitch;
    int m_viewMode;              // 0=列表式, 1=瀑布流
    QList<ModInfo> m_cardInfos;  // 当前列表卡片数据缓存
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_emptyLabel;
    QPushButton *m_downloadSearchBtn;  // 搜索无结果时前往下载页

    // Bottom action bar
    QWidget *m_bottomBar;
    QPushButton *m_bottomCheckUpdateBtn;
    QPushButton *m_selectAllBtn;
    QPushButton *m_copyModBtn;
    QPushButton *m_toggleSelectedBtn;
    QPushButton *m_openFolderBtn;
    QPushButton *m_detailBtn;
    QPushButton *m_pasteModBtn;
    QPushButton *m_updateModBtn;

    // Data
    ModScanner *m_scanner;
    ModNameFetcher *m_nameFetcher;
    ModrinthAPI *m_updateCheckApi;
    LocalModList m_modList;
    QList<QWidget*> m_cardWidgets;
    QMap<QString, int> m_modIndexMap; // modId/fileName → index in modList
    QMap<QString, int> m_sha1ToIndex;  // SHA-1 → index in modList（更新检查用）
    QMap<QString, ModVersionFile> m_installedBySha1; // 本次检查已安装版本的 SHA-1 匹配结果
    QMap<QString, ModVersionFile> m_updateCache;     // 更新结果缓存：有更新的 sha1 → 目标版本
    QStringList m_updateCacheKeys;   // 缓存对应的哈希集合（排序后）
    QDateTime m_updateCacheTime;     // 缓存时间（6 小时 TTL）
    QString m_gameVersion;           // 当前实例的 Minecraft 版本
    QString m_loaderType;            // 当前实例的加载器
    int m_updateCheckTotal;          // 本次检查总数
    int m_updateCheckFound;          // 发现更新的数量
    int m_currentFilter;
    QString m_currentSearch;
    QString m_instancePath;
    bool m_isLoading;
    bool m_allSelected;
    QSet<int> m_selectedModIndices;   // 多选索引集合
    QList<int> m_updateQueue;         // 待更新的模组索引队列
    bool m_updateInProgress = false;  // 正在更新模组

    // 本地资源分类
    LocalCategoryManager *m_categoryManager;
    QComboBox *m_categoryCombo;
    QPushButton *m_categoryManageBtn;
    QString m_currentCategoryId;      // 空 = 全部；特殊哨兵值 = 未分类

    // 选中高亮遮罩（与 m_cardWidgets 对齐）
    QList<QWidget *> m_selectionOverlays;
    QList<QGraphicsOpacityEffect *> m_selectionEffects;
    QList<QPropertyAnimation *> m_selectionAnims;

    // 滑动快速多选状态
    bool m_dragSelectActive = false;  // 是否处于滑动多选状态
    bool m_dragMoved = false;         // 是否发生了实际拖拽
    QPoint m_dragPressPos;            // 按下时的全局坐标
    int m_dragAnchorIdx = -1;         // 按下时的卡片索引
    int m_dragLastHoverIdx = -1;      // 最近一次滑过的卡片索引

    // 分批创建卡片（避免资源过多时一次性创建导致闪退）
    int m_batchIndex = 0;                     // 当前批次处理到的索引
    int m_batchGeneration = 0;                // 批次代次，用于作废旧的批次回调
    void createNextBatch();                   // 创建下一批卡片
};

#endif // INSTANCEMODSPAGE_H