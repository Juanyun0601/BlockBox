/**
 * @file   InstanceResourcesPage.h
 * @brief  实例助手 - 资源管理页面，管理当前实例的模组/存档/截图/资源包/光影包
 * @author BlockBox Team
 * @date   2026-07-21
 *
 * 用于 InstanceAssistantWindow 的「资源管理」标签页。窗口宽度较窄（450px），
 * 因此采用单列垂直卡片列表，顶部通过 Tab 切换资源类型：
 *  - 模组：扫描 mods/ 目录，支持启用/禁用切换
 *  - 存档：扫描 saves/ 目录下的子目录，支持快捷启动
 *  - 截图：扫描 screenshots/ 目录下的图片，支持查看
 *  - 资源包：扫描 resourcepacks/ 目录下的 .zip
 *  - 光影包：扫描 shaderpacks/ 目录下的 .zip
 *
 * 复用 ModScanner 异步扫描模组；其他类型通过 QDir 同步扫描。
 * 兼容版本隔离（versions/{ver}/{subDir}）与非隔离（.minecraft/{subDir}）两种布局。
 */
#ifndef INSTANCERESOURCESPAGE_H
#define INSTANCERESOURCESPAGE_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

#include "utils/mod/ModData.h"

class BlurLoadingOverlay;
class LocalCategoryManager;
class ModScanner;
class ModNameFetcher;
class QComboBox;
class QEvent;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class InstanceResourcesPage : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int highlightPos READ highlightPos WRITE setHighlightPos)

public:
    explicit InstanceResourcesPage(QWidget *parent = nullptr);
    ~InstanceResourcesPage();

    /** 设置当前实例路径并刷新当前类型的资源列表 */
    void setInstancePath(const QString &path);

    /** 手动刷新当前类型的资源列表 */
    void refreshList();

    /** 指示条当前位置（用于动画） */
    int highlightPos() const { return m_highlightPos; }
    /** 设置指示条位置（由动画驱动） */
    void setHighlightPos(int pos);

signals:
    /** 双击模组卡片，请求打开本地模组详情页 */
    void modDetailRequested(const ModInfo &info);
    /** 请求快捷启动游戏并进入指定存档（仅存档类型触发） */
    void quickLaunchSaveRequested(const QString &saveName);

private slots:
    void onTabChanged(int index);
    void onSearchTextChanged(const QString &text);
    void onScanCompleted(const LocalModList &result);
    void onScanFailed(const QString &error);
    void onModNameResolved(const ModInfo &info);
    void onModNameFetchFailed(const QString &modId, const QString &englishName);
    void onSelectAllClicked();
    void onOpenFolderClicked();
    void onPasteFileClicked();
    void onCategoryFilterChanged(int index);
    void onCategoryManageClicked();
    void onCategoriesChanged();
    void onAssignmentsChanged();

private:
    /** 资源类型 */
    enum ResourceType
    {
        TypeMods = 0,         // 模组
        TypeSave = 1,         // 存档
        TypeScreenshot = 2,   // 截图
        TypeResourcePack = 3, // 资源包
        TypeShaderPack = 4    // 光影包
    };

    /** 文件型资源条目（存档/截图/资源包/光影包） */
    struct FileEntry
    {
        QString filePath;
        QString fileName;
        qint64 fileSize = 0;
        QDateTime lastModified;
        bool isDir = false; // 存档为 true
    };
private:
    void initUI();
    void setupConnections();
    QString resolveResourceDir(const QString &subDir) const;
    QString typeDisplayName() const;
    QString subDirName() const;

    /** 创建 Tab 按钮，统一外观与光标行为 */
    QPushButton *createTabButton(const QString &text, int index);
    /** 应用主题色到 Tab 栏与指示条 */
    void applyTabThemeStyles();
    /** 切换 Tab 选中态，并触发指示条位移动画 */
    void switchTab(int index);
    /** 重新计算指示条尺寸与位置（resize/首次显示时调用） */
    void updateHighlightGeometry();

    void clearCards();
    void placeCards();
    void showLoading(bool show);
    void showEmptyHint(bool show);
    void applyFilterAndSearch();
    QString formatFileSize(qint64 bytes) const;
    QString selectedCardStyle() const;
    void toggleCardSelection(int idx);
    void deselectAllCards();
    void updateBottomBarState();
    void updateStatsLabel();
    /** 重新填充分类筛选下拉框（保留当前选中） */
    void reloadCategoryCombo();
    /** 刷新卡片上的分类标签（分配变化/重命名后调用） */
    void refreshCardCategoryChips();
    /** 为指定文件弹出含“设置分类”的右键菜单 */
    void showFileContextMenu(const QPoint &pos, int idx);

    /** 扫描文件型资源（存档/截图/资源包/光影包） */
    void scanFiles();
    /** 创建模组卡片 */
    QWidget *createModCard(const ModInfo &info);
    /** 创建文件型资源卡片 */
    QWidget *createFileCard(const FileEntry &entry);
    /** 弹出模组右键菜单 */
    void onModContextMenu(const QPoint &pos, int idx);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    // ---- UI ----
    QPushButton *m_tabModsBtn;
    QPushButton *m_tabSavesBtn;
    QPushButton *m_tabScreenshotsBtn;
    QPushButton *m_tabResourcePacksBtn;
    QPushButton *m_tabShaderPacksBtn;
    QWidget *m_tabBar;             ///< Tab 栏容器
    QWidget *m_tabHighlight;       ///< Tab 选中指示条（位于按钮下方）
    QPropertyAnimation *m_tabAnim; ///< 指示条位移动画
    QList<QPushButton *> m_tabButtons; ///< 所有 Tab 按钮，按枚举顺序
    int m_highlightPos;            ///< 指示条当前 X 坐标（动画属性）
    QWidget *m_tabIndicator;
    QLineEdit *m_searchEdit;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QVBoxLayout *m_cardLayout;
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_emptyLabel;

    // 本地资源分类（按资源类型独立）
    LocalCategoryManager *m_categoryManager;
    QComboBox *m_categoryCombo;
    QPushButton *m_categoryManageBtn;
    QString m_currentCategoryId;      // 空 = 全部；特殊哨兵值 = 未分类

    // 底部操作栏
    QWidget *m_bottomBar;
    QPushButton *m_selectAllBtn;
    QPushButton *m_openFolderBtn;
    QPushButton *m_pasteBtn;
    QPushButton *m_detailBtn;

    // ---- 数据 ----
    ResourceType m_currentType;
    QString m_instancePath;
    QString m_currentSearch;
    bool m_isLoading;
    bool m_allSelected;

    // 模组数据
    QList<ModInfo> m_modList;
    QHash<QString, int> m_modIndexMap; // modId/fileName → index

    // 文件型资源数据
    QList<FileEntry> m_fileList;

    // 卡片控件与选中索引
    QList<QWidget *> m_cardWidgets;
    QSet<int> m_selectedIndices;

    // 扫描器
    ModScanner *m_scanner;
    ModNameFetcher *m_nameFetcher;
};

#endif // INSTANCERESOURCESPAGE_H
