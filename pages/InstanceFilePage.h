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
#include <QStackedWidget>

#include "utils/mod/ModData.h"

class BlurLoadingOverlay;
class ModrinthAPI;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

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
    /** 请求打开指定光影包的光影设置页（仅 ShaderPack 类型触发） */
    void shaderSettingsRequested(const QString &packPath);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    void onToggleDragMode();

private:
    /** 文件型资源条目 */
    struct FileEntry
    {
        QString filePath;
        QString fileName;
        qint64 fileSize;
        QDateTime lastModified;
        QString sha1Hash;   // SHA-1 哈希（更新检查用，懒计算）
    };

    void initUI();
    void setupConnections();
    void scanFiles();
    void clearCards();
    void placeCards();
    void initDragModeUI();
    void handleDroppedFiles(const QList<QUrl> &urls);
    void showLoading(bool show);
    void showEmptyHint(bool show);
    void applyFilterAndSearch();
    QWidget *createFileCard(const QString &filePath, const QString &fileName,
                            qint64 fileSize, const QDateTime &lastModified);
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

    FileType m_fileType;

    // UI elements
    QLineEdit *m_searchEdit;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_emptyLabel;
    QPushButton *m_downloadSearchBtn;

    // Drag mode UI
    QStackedWidget *m_stackedWidget;
    QWidget *m_normalPage;
    QWidget *m_dragPage;
    QPushButton *m_dragModeBtn;
    QLabel *m_dragHintLabel;
    QLabel *m_dragStatusLabel;
    bool m_isDragMode;

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