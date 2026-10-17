#ifndef PROJECTIONMANAGEPAGE_H
#define PROJECTIONMANAGEPAGE_H

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
#include <QFileInfo>
#include <QHash>
#include <QPoint>
#include <QString>

#include "components/ContentViewSwitch.h"

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class LocalCategoryManager;
class QMenu;
class MasonryLayout;

class ProjectionManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectionManagePage(QWidget *parent = nullptr);
    ~ProjectionManagePage();

    void setInstancePath(const QString &path);

signals:
    /** 请求编辑投影文件（点击卡片上的编辑按钮或双击卡片） */
    void projectionEditRequested(const QString &filePath);

private slots:
    void onSearchTextChanged(const QString &text);
    void onAddProjectionClicked();
    void onDeleteProjectionClicked();
    void onBlockEditorClicked();
    void onSelectAllClicked();
    void onCategoryFilterChanged(int index);
    void onCategoryManageClicked();
    void onCategoriesChanged();
    void onAssignmentsChanged();
    void onTimeFilterChanged(int index);
    void onSizeFilterChanged(int index);
    void onSortChanged(int index);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    void loadProjections();
    void clearCards();
    void placeCards();
    /** 列表视图卡片（保持原布局与多选交互） */
    QWidget *createListCard(const QFileInfo &fi, int row);
    /** 瀑布流卡片：顶部 3D 等距封面横幅 + 名称/描述/chips + 操作 */
    QWidget *createMasonryCard(const QFileInfo &fi);
    /** 异步把 3D 渲染封面贴到横幅（QPointer 守护，失败保持渐变底） */
    void requestCoverInto(class QLabel *banner, const QString &filePath, int w, int h);
    /** 视图偏好持久化键 */
    QString viewPrefKey() const;
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    /** 重建双视图容器布局（列表 QGrid / 瀑布流 Masonry） */
    void ensureCardLayout();
    void showEmptyHint(bool show);
    QString projectionsDirPath() const;
    QString dimensionTextFor(const QFileInfo &fi);
    void deleteProjectionFile(const QString &filePath);
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
    /** 重新填充分类筛选下拉框（保留当前选中，含智能筛选项） */
    void reloadCategoryCombo();
    /** 按当前排序模式重排投影列表副本（名称/大小/时间） */
    void sortProjections(QList<QFileInfo> &list) const;
    /** 判断条目是否通过 时间/大小/分类 组合筛选 */
    bool passesFilters(const QFileInfo &fi) const;
    /** 右键「设置分类」子菜单（key 为文件名，含扩展名） */
    void appendCategoryMenu(QMenu *menu, const QString &key);

    QLineEdit *m_searchEdit;
    ContentViewSwitch *m_viewSwitch;
    QLabel *m_statsLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    QGridLayout *m_cardGridLayout;   // 列表视图
    MasonryLayout *m_cardFlowLayout; // 瀑布流视图
    QLabel *m_emptyLabel;

    QWidget *m_bottomBar;
    QPushButton *m_openFolderBtn;
    QPushButton *m_addProjectionBtn;
    QPushButton *m_deleteProjectionBtn;
    QPushButton *m_blockEditorBtn;
    QPushButton *m_selectAllBtn;

    // 分类 + 时间/大小/排序筛选（分类按实例持久化于 blockbox/categories.json，键=文件名）
    LocalCategoryManager *m_categoryManager;
    QComboBox *m_categoryCombo;
    QPushButton *m_categoryManageBtn;
    QComboBox *m_timeCombo;
    QComboBox *m_sizeCombo;
    QComboBox *m_sortCombo;
    QString m_currentCategoryId;      // 空 = 全部；哨兵 = 未分类 / 最近修改 / 大文件
    int m_timeFilter = 0;             // 0=全部 1=7天 2=30天 3=一年
    int m_sizeFilter = 0;             // 0=全部 1=<10MB 2=10~100MB 3=>100MB
    int m_sortMode = 0;               // 0=名称↑ 1=名称↓ 2=大小↓ 3=大小↑ 4=新→旧 5=旧→新
    int m_viewMode = 1;               // 0=列表式, 1=瀑布流

    QList<QFileInfo> m_projectionFiles;
    QString m_currentSearch;
    QString m_instancePath;
    QHash<QString, QString> m_dimensionCache;

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
