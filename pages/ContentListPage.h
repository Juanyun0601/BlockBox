#ifndef CONTENTLISTPAGE_H
#define CONTENTLISTPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include "../utils/content/ContentData.h"
#include "../components/ContentViewSwitch.h"

class BlurLoadingOverlay;
class LocalCategoryManager;
class MasonryLayout;

class ContentListPage : public QWidget
{
    Q_OBJECT

public:
    explicit ContentListPage(ContentType contentType, QWidget *parent = nullptr);
    ~ContentListPage();

    void loadInstances();

signals:
    void contentClicked(const ModInfo &info);

private slots:
    void onInstanceChanged(int index);
    void onRefreshClicked();
    void onFilterChanged(int index);
    void onCategoryFilterChanged(int index);
    void onCategoryManageClicked();
    void onCategoriesChanged();
    void onAssignmentsChanged();
    void onToggleMod(const ModInfo &info);
    void onDeleteMod(const ModInfo &info);
    void onContentContextMenu(const QPoint &pos);

private:
    void initUI();
    void setupConnections();
    void refreshContentList();
    void doScan(const QString &folderPath);
    void onScanFinished(const LocalModList &result);
    void clearCards();
    void placeCards();
    void showLoading(bool show);
    void showEmptyHint(bool show);
    void applyFilter();
    /** 视图偏好持久化键（按内容类型隔离） */
    QString viewPrefKey() const;
    /** 重新填充分类筛选下拉框（保留当前选中） */
    void reloadCategoryCombo();
    /** 刷新卡片上的分类标签（分配变化/重命名后调用） */
    void refreshCardCategoryChips();
    /** 在右键菜单追加"设置分类"子菜单（fileName 为资源目录内文件名/目录名） */
    void appendCategoryMenu(QMenu *menu, const QString &fileName, const QString &filePath);
    /** 列表式卡片（自 onScanFinished 内联构建抽取） */
    QWidget *createListCard(const ModInfo &info);
    /** 瀑布流卡片（复用 MasonryContentCard 组件） */
    QWidget *createMasonryCard(const ModInfo &info);
    void addCard(const ModInfo &info);
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    void rebuildCards();

    bool eventFilter(QObject *obj, QEvent *event) override;

    enum FilterMode { FilterAll = 0, FilterEnabled = 1, FilterDisabled = 2 };

    // Content type
    ContentType m_contentType;
    ContentTypeConfig m_config;

    // UI elements
    QComboBox *m_instanceCombo;
    QComboBox *m_filterCombo;
    // 本地资源分类筛选（按实例、按内容类型独立）
    LocalCategoryManager *m_categoryManager;
    QComboBox *m_categoryCombo;
    QPushButton *m_categoryManageBtn;
    QString m_currentCategoryId;      // 空 = 全部；特殊哨兵值 = 未分类
    QPushButton *m_refreshBtn;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;
    MasonryLayout *m_cardFlowLayout;   // 瀑布流布局（与 m_cardGridLayout 二选一）
    ContentViewSwitch *m_viewSwitch;   // 列表式 / 瀑布流切换
    int m_viewMode;                    // ContentViewSwitch::ViewMode
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_emptyLabel;

    // Data
    LocalModList m_modList;
    QList<QWidget*> m_cardWidgets;
    int m_currentFilter;
    bool m_isLoading;
};

#endif // CONTENTLISTPAGE_H