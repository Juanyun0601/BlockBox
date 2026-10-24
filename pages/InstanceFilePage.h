/**
 * @file   InstanceFilePage.h
 * @brief  实例管理 - 通用文件浏览页面（资源包/光影包/存档/截图）
 * @author BlockBox Team
 * @date   2026-06-27
 */
#ifndef INSTANCEFILEPAGE_H
#define INSTANCEFILEPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSet>
#include <QHash>
#include <QMap>

#include "utils/mod/ModData.h"
#include "utils/content/ContentData.h"
#include "../components/ContentViewSwitch.h"

class BlurLoadingOverlay;
class LocalCategoryManager;
class ModrinthAPI;
class MasonryLayout;
class QGraphicsOpacityEffect;
class QMenu;
class QPropertyAnimation;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

class InstanceFilePage : public QWidget
{
    Q_OBJECT

public:
    enum FileType
    {
        ResourcePack,   // 资源包 - resourcepacks/
        ShaderPack,     // 光影包 - shaderpacks/
        Save,           // 存档 - saves/
        Screenshot      // 截图 - screenshots/
    };

    explicit InstanceFilePage(FileType type, QWidget *parent = nullptr);
    ~InstanceFilePage();

    void setInstancePath(const QString &path);
    void refreshFileList();

    /** 子导航搜索框分发：同步到页内搜索框（复用既有过滤管线） */
    Q_INVOKABLE void applyPageFilter(const QString &query);

    /** 获取子目录名称（相对实例路径） */
    QString subDirName() const;
    /** 获取文件类型中文名 */
    QString typeDisplayName() const;

signals:
    /** 搜索无结果时，请求前往下载页搜索 */
    void fileSearchRequested(const QString &keyword);
    /** 请求快捷启动游戏并进入指定存档（仅 Save 类型触发） */
    void quickLaunchSaveRequested(const QString &saveName);
    /** 请求打开指定存档的存档设置页（仅 Save 类型触发） */
    void saveSettingsRequested(const QString &saveName);
    /** 请求打开指定存档的地表地图页（仅 Save 类型触发） */
    void saveMapRequested(const QString &saveName);
    /** 请求打开指定存档的版本转换弹窗（Java ⇄ 基岩，仅 Save 类型触发） */
    void saveConvertRequested(const QString &saveName);
    /** 请求打开指定光影包的光影设置页（仅 ShaderPack 类型触发） */
    void shaderSettingsRequested(const QString &packPath);
    /** 卡片点击「问问AI」：请求跳转 AI 助手并引用该本地资源 */
    void askAiRequested(const ResourceReference &ref);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    // 列表区域直接接受拖放导入（无需先点「拖入此处」按钮）
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onSearchTextChanged(const QString &text);
    void onSelectAllClicked();
    void onOpenFolderClicked();
    void onPasteFileClicked();
    void onCopyFileClicked();
    void onDeleteSelectedClicked();
    void onDownloadSearchClicked();
    void onCheckUpdatesClicked();
    void onHashesMatched(const QMap<QString, ModVersionFile> &versions);
    void onUpdatesChecked(const QMap<QString, ModVersionFile> &versions);
    void onUpdateCheckBatchFailed(const QString &error);
    void onUpdateSelectedClicked();
    void onCategoryFilterChanged(int index);
    void onCategoryManageClicked();
    void onCategoriesChanged();
    void onAssignmentsChanged();
    void onStatusFilterChanged(int index);
    void onTimeFilterChanged(int index);
    void onSizeFilterChanged(int index);
    void onSortChanged(int index);

private:
    /** 文件型资源条目 */
    struct FileEntry
    {
        QString filePath;
        QString fileName;
        qint64 fileSize;
        QDateTime lastModified;
        QString sha1Hash;   // SHA-1 哈希（更新检查用，懒计算）
        bool enabled = true;    // 是否未被 .disabled 后缀禁用
    };

    void initUI();
    void setupConnections();
    void scanFiles();
    void clearCards();
    void placeCards();
    void handleDroppedFiles(const QList<QUrl> &urls);
    /** 拖放悬停高亮（true=高亮列表边框，false=恢复） */
    void setDropHighlight(bool on);
    void showLoading(bool show);
    void showEmptyHint(bool show);
    void applyFilterAndSearch();
    QWidget *createFileCard(const QString &filePath, const QString &fileName,
                            qint64 fileSize, const QDateTime &lastModified);
    /** 瀑布流卡片（截图用真实缩略图作封面，其余类型用提取图标作 logo） */
    QWidget *createMasonryCard(const FileEntry &entry);
    /** 视图偏好持久化键（按文件类型隔离） */
    QString viewPrefKey() const;
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    void rebuildCards();
    void updateBottomBarState();
    void toggleCardSelection(int idx);
    void deselectAllCards();
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
    QString formatFileSize(qint64 bytes) const;
    QString computeFileSha1(const QString &filePath);
    void resetUpdateState();
    void markCardHasUpdate(int index, const ModVersionFile &target);
    void processNextModUpdate();
    void removeOldFile(const FileEntry &entry, const QString &newPath);
    /** 重新填充分类筛选下拉框（保留当前选中） */
    void reloadCategoryCombo();
    /** 按当前排序模式重排 m_fileList 并重建 SHA-1 → 索引映射（不重建卡片） */
    void sortFileList();
    /** 重排文件列表并重建卡片（排序下拉变化时调用） */
    void applySortOrder();
    /** 刷新卡片上的分类标签（分配变化/重命名后调用） */
    void refreshCardCategoryChips();
    /** 为指定卡片弹出含"设置分类"的右键子菜单（复用右键菜单构建逻辑） */
    void appendCategoryMenu(QMenu *menu, const QString &fileName, const QString &filePath);

    FileType m_fileType;

    // UI elements
    QLineEdit *m_searchEdit;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    MasonryLayout *m_cardFlowLayout;   // 瀑布流布局（与 m_cardGridLayout 二选一）
    ContentViewSwitch *m_viewSwitch;   // 列表式 / 瀑布流切换
    int m_viewMode;                    // ContentViewSwitch::ViewMode
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_emptyLabel;
    QPushButton *m_downloadSearchBtn;

    // 本地资源分类筛选（按实例、按资源类型独立）
    LocalCategoryManager *m_categoryManager;
    QComboBox *m_categoryCombo;
    QPushButton *m_categoryManageBtn;
    QString m_currentCategoryId;      // 空 = 全部；特殊哨兵值 = 未分类 / 智能筛选

    // 状态 / 时间 / 大小 / 排序筛选（叠加在分类与搜索之上）
    QComboBox *m_statusCombo;         // 筛选：全部 / 已启用 / 已禁用
    QComboBox *m_timeCombo;           // 时间：全部 / 7 天 / 30 天 / 一年
    QComboBox *m_sizeCombo;           // 大小：全部 / <10MB / 10~100MB / >100MB
    QComboBox *m_sortCombo;           // 排序：名称 / 大小 / 修改时间
    int m_statusFilter = 0;           // 0=全部 1=已启用 2=已禁用
    int m_timeFilter = 0;             // 0=全部 1=7 天 2=30 天 3=一年
    int m_sizeFilter = 0;             // 0=全部 1=<10MB 2=10~100MB 3=>100MB
    int m_sortMode = 0;               // 0=名称↑ 1=名称↓ 2=大小↓ 3=大小↑ 4=新→旧 5=旧→新

    // 拖放导入反馈（列表下方，成功/失败提示）
    QLabel *m_dragStatusLabel;

    // Bottom action bar
    QWidget *m_bottomBar;
    QPushButton *m_selectAllBtn;
    QPushButton *m_checkUpdateBtn;
    QPushButton *m_copyBtn;
    QPushButton *m_openFolderBtn;
    QPushButton *m_detailBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_pasteBtn;
    QPushButton *m_updateBtn;
    QPushButton *m_installNewBtn;      // 底部「安装新XX」入口（仅支持在线下载的类型）

    // Data
    QList<FileEntry> m_fileList;
    QList<QWidget *> m_cardWidgets;
    QString m_currentSearch;
    QString m_instancePath;
    bool m_isLoading;
    bool m_allSelected;
    QSet<int> m_selectedIndices;

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

    // 更新检查（资源包/光影包）
    ModrinthAPI *m_updateCheckApi;
    QHash<QString, QString> m_sha1Cache;      // filePath → SHA-1（懒计算缓存）
    QHash<QString, int> m_sha1ToIndex;        // SHA-1 → m_fileList 索引
    QMap<QString, ModVersionFile> m_installedBySha1; // 已安装版本匹配结果
    QMap<QString, ModVersionFile> m_updateCache;     // 有更新的 sha1 → 目标版本
    QList<int> m_updateQueue;                 // 待更新索引队列
    int m_updateCheckFound = 0;
    bool m_updateInProgress = false;

    // 分批创建卡片（避免资源过多时一次性创建导致闪退）
    int m_batchIndex = 0;                     // 当前批次处理到的索引
    int m_batchGeneration = 0;                // 批次代次，用于作废旧的批次回调
    void createNextBatch();                   // 创建下一批卡片
};

#endif // INSTANCEFILEPAGE_H