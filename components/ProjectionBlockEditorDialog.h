/**
 * @file   ProjectionBlockEditorDialog.h
 * @brief  投影方块编辑器对话框（3D 立体视图）
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 基于 VoxelViewWidget 的 3D 体素方块编辑器，数据模型使用 SchematicDocument。
 * 功能：
 *   - 3D 立体查看（WASD 移动、空格升、Shift 降、拖动转向、滚轮前后）
 *   - 交互编辑：顶部两个模式 —— 选取（左键选中方块）、编辑（左键破坏、右键放置当前方块）
 *   - 两点框选：选取范围切换成「框选（两点）」后，左键先后点两个对角方块，
 *     两点围出的整块区域里所有方块一起选中（金色描边标出第一个角点）
 *   - 方块调色板：浏览/选择投影中出现的方块进行放置
 *   - 批量操作：替换选中的方块（一键换成当前方块）、整份投影的整体替换、
 *     全空间填充；「填充方块」可从居中弹窗选形状（实心/空心/框架/倾斜地板/
 *     方形/三角形/圆形/圆柱），再在视图里点两个对角方块生成（也可按坐标填充）
 *   - 图层过滤（按 Y 层突出显示）
 *   - 撤销/重做、保存副本、就地保存
 *
 * 作为启动器内置创作工具使用；数据读写位于启动器核心 SchematicDocument。
 */
#pragma once

#include <QDialog>
#include <QIcon>
#include <QSet>
#include <QVector>
#include <climits>

#include "utils/Schematic/SchematicDocument.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/Schematic/ProjectionFillShape.h"

class VoxelViewWidget;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QToolButton;

/**
 * @brief 投影方块编辑器对话框
 */
class ProjectionBlockEditorDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 打开编辑器
     * @param filePath     要编辑的 .litematic 文件
     * @param instancePath 可选：实例的 versions/<版本> 目录，用于从该实例加载方块材质
     * @param parent       父窗口
     * @return 若用户保存了修改则返回 true（调用方可刷新列表）
     */
    static bool editProjection(QWidget *parent, const QString &filePath,
                               const QString &instancePath = QString());

    explicit ProjectionBlockEditorDialog(QWidget *parent = nullptr);
    ~ProjectionBlockEditorDialog() override;

    /**
     * @brief 加载后返回是否成功
     * @param filePath     .litematic 路径
     * @param instancePath 可选：材质来源实例（与 setTextureSource 等价，但只触发一次视图重建）
     */
    bool loadProjection(const QString &filePath, const QString &instancePath = QString());

    /**
     * @brief 设置实例材质来源（可选）；从对应实例的客户端 jar 加载方块贴图
     * @param instancePath 实例的 versions/<版本> 目录；为空则关闭材质
     */
    void setTextureSource(const QString &instancePath);

    /** 最近一次保存的文件路径（用于把默认投影设置同步为保存结果）；未保存过返回原文件路径 */
    QString lastSavedPath() const { return m_lastSavedPath.isEmpty() ? m_filePath : m_lastSavedPath; }

    /** 是否有未保存（或已保存但发生过）的修改 */
    bool dirty() const { return m_dirty; }

    /** 本次会话是否执行过保存（就地或另存为） */
    bool wasSaved() const { return !m_lastSavedPath.isEmpty(); }

protected:
    void closeEvent(QCloseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onPlace(int x, int y, int z);
    void onBreak(int x, int y, int z);
    void onSelect(int x, int y, int z);
    void onClearSelection();
    void onToolChanged(int index);
    void onFillLayerChanged(int value);
    void onShowAirToggled(bool checked);
    void onUndo();
    void onRedo();
    void onSaveAs();
    void onSaveInPlace();
    void onReplaceAll();
    void onFillAll();
    /** 打开居中的形状选择弹窗；选完形状后进入两点拾取或按坐标填充 */
    void onFillShapeClicked();
    void onPickBlock();
    void onRefreshStats();
    void filterPalette(const QString &text);
    void onViewPreset(int presetValue);
    void onFocusView();
    void onExportScreenshot();
    void onStepLayer(int delta);
    void onPlaceAdjacentToggled(bool on);
    void onGeometryRebuilt(int quads, int elapsedMs);
    void updateHoverLabel(int x, int y, int z);

private:
    struct UndoStep;
    void initUI();
    void applyEditorChrome();
    void applyEditorChromeExtras();
    void refreshToolButtonIcons();
    void setupShortcuts();
    void populatePalette();
    void applyTextureAndShapes();
    void rebuildView();
    /** 把当前工具同步成视图的左键动作（右键始终放置） */
    void applyToolToView();
    // 「当前方块」下拉框：显示中文名，内部保存完整方块 ID
    QString currentBlockId() const;
    void refreshCurrentBlockOptions();
    void addCurrentBlockItem(const QString &blockId);
    void setCurrentBlockId(const QString &blockId);
    /** 调色板/下拉框图标：优先用材质图，缺失时退回方块色块，保证总能看见 */
    QIcon paletteItemIcon(const SchematicBlockState &state) const;
    void updateTitle();
    void updateUndoRedo();
    void applyEditorSettings();
    void loadBlockRegistry();
    QString buildStatsText() const;
    /** 只刷新顶部尺寸/面数等徽章，不做整表统计 */
    void refreshStatsChips();
    /** 世界坐标 -> 体积序索引；越界返回 -1 */
    int cellIndexAt(int x, int y, int z) const;
    /** 记录一次单格改动（只存旧/新调色板下标，开销极小） */
    void recordCellEdit(int cell, int beforeIndex, int afterIndex, const QString &label);
    /** 记录一次批量改动（存整表快照） */
    void recordBulkEdit(const QList<int> &beforeIndices, const QString &label);
    /** 把某条记录应用到文档（useBefore = 撤销，false = 重做） */
    bool applyUndoStep(const UndoStep &step, bool useBefore);
    void trimUndoStack();
    /** 当前选取范围：0 = 只选这一个方块；1 = 选中全部同类型方块；2 = 两点框选 */
    int selectScope() const;
    /** 当前是否是「两点框选」范围 */
    bool isBoxSelectScope() const { return selectScope() == 2; }
    /** 按当前选取范围收集要选中的方块格（体积序索引）；点击空气返回空集 */
    QSet<int> collectSelection(int x, int y, int z) const;
    /** 两点框选：收集两个对角点围出的立方体里全部非空气方块（框超出投影的部分忽略） */
    QSet<int> collectBoxSelection(int x1, int y1, int z1, int x2, int y2, int z2) const;
    /** 两点框选是否已点下第一个角点、正等第二个角点 */
    bool boxSelectPending() const { return m_boxPivotX != INT_MIN; }
    /** 上一次框选是否已完整点过两个角点（用于显示范围尺寸） */
    bool hasDoneBox() const { return m_boxAX != INT_MIN && m_boxBX != INT_MIN; }
    /** 收起待定角点（金色描边），不动已有选中 */
    void clearBoxPivot();
    /** 把待定角点同步成视图里的金色描边 */
    void syncBoxPivotMarker();
    void updateSelectionLabel();
    /** 取消全部选中（同时忘掉上次点击的锚点与框选角点） */
    void clearSelection();

    // ── 填充方块（选形状 → 两点生成） ──
    /** 进入两点填充：按所选形状，在视图里先后点两个对角方块生成 */
    void startFillPick(ProjectionFillShape shape, bool airOnly);
    /** 退出两点填充（已经生成的方块保持不动） */
    void cancelFillPick();
    /** 是否正在等待填充的第一个/第二个角点 */
    bool fillPickPending() const { return m_fillActive; }
    /** 把待定的第一个角点同步成视图里的金色描边 */
    void syncFillMarker();
    /** 弹窗里的「按坐标填充…」：按输入的坐标范围生成形状 */
    void fillShapeByCoordinates(ProjectionFillShape shape, bool airOnly);
    /**
     * @brief 把形状写进文档（世界坐标范围，含边界；超出投影的部分忽略）
     * @return 实际写入的格子（体积序索引）；没有产生改动时返回空
     */
    QVector<int> applyFillShape(ProjectionFillShape shape, bool airOnly,
                                int x0, int y0, int z0, int x1, int y1, int z1);

    bool            m_dirty = false;
    QString         m_filePath;
    QString         m_lastSavedPath;
    QString         m_instancePath;
    SchematicDocument m_doc;
    InstanceTextureLoader *m_texLoader = nullptr;   // 进程内共享图集（不拥有）
    BlockShapeLoader *m_shapeLoader = nullptr;      // 进程内共享形状加载器（不拥有）
    QHash<QString, QString> m_blockCnMap; // 方块ID(小写) -> 中文名

    QString displayName() const;

    // 交互模式（顶部只有两个模式按钮）
    enum Tool : int {
        ToolSelect = 0, ///< 选取：左键单击选中方块（范围可选单格 / 同类全部）
        ToolEdit        ///< 编辑：左键破坏、右键放置当前方块
    };
    int             m_tool = ToolEdit;

    // 撤销/重做（内存中的差异快照，不再落磁盘临时文件）
    struct UndoStep
    {
        bool        bulk = false;
        QString     label;
        QVector<int> cells;         // 单格：体积序索引
        QVector<int> beforeIndices; // 单格：旧调色板下标
        QVector<int> afterIndices;  // 单格：新调色板下标
        QList<int>  beforeAll;      // 批量：整表快照
        QList<int>  afterAll;
    };
    QVector<UndoStep> m_undoStack;
    QVector<UndoStep> m_redoStack;

    // UI
    QStackedWidget  *m_stack = nullptr;
    VoxelViewWidget *m_view = nullptr;
    QComboBox       *m_toolCombo = nullptr;
    QComboBox       *m_selectScopeCombo = nullptr;
    QLabel          *m_selectLabel = nullptr;
    QPushButton     *m_clearSelBtn = nullptr;
    QComboBox       *m_currentBlockCombo = nullptr;
    QPushButton     *m_pickBtn = nullptr;
    QSlider         *m_layerSlider = nullptr;
    QLabel          *m_layerLabel = nullptr;
    QToolButton     *m_belowToggle = nullptr;
    QListWidget     *m_paletteList = nullptr;
    QLineEdit       *m_paletteFilter = nullptr;
    QLabel          *m_statsLabel = nullptr;
    QLabel          *m_posLabel = nullptr;
    QPushButton     *m_undoBtn = nullptr;
    QPushButton     *m_redoBtn = nullptr;
    QPushButton     *m_saveAsBtn = nullptr;
    QPushButton     *m_saveBtn = nullptr;
    QToolButton     *m_airBtn = nullptr;

    // 新增 UI
    QLabel          *m_fileLabel = nullptr;
    QLabel          *m_chipSize = nullptr;
    QLabel          *m_chipFaces = nullptr;
    QLabel          *m_chipPerf = nullptr;
    QLabel          *m_paletteCountLabel = nullptr;
    QToolButton     *m_placeAdjacentToggle = nullptr;
    QPushButton     *m_focusBtn = nullptr;
    QPushButton     *m_shotBtn = nullptr;
    QPushButton     *m_layerPrevBtn = nullptr;
    QPushButton     *m_layerNextBtn = nullptr;
    QVector<QPushButton *> m_toolButtons;
    QVector<QPushButton *> m_presetButtons;

    int m_lastQuadCount = 0;
    int m_lastRebuildMs = -1;

    int m_hoverX = INT_MIN, m_hoverY = INT_MIN, m_hoverZ = INT_MIN;
    // 最近一次选取点击的方块（切换选取范围时据此重新选中）
    int m_selAnchorX = INT_MIN, m_selAnchorY = INT_MIN, m_selAnchorZ = INT_MIN;
    // 两点框选：第一个角点（INT_MIN = 还没点），以及上一次框选完成的两个对角（显示尺寸用）
    int m_boxPivotX = INT_MIN, m_boxPivotY = INT_MIN, m_boxPivotZ = INT_MIN;
    int m_boxAX = INT_MIN, m_boxAY = INT_MIN, m_boxAZ = INT_MIN;
    int m_boxBX = INT_MIN, m_boxBY = INT_MIN, m_boxBZ = INT_MIN;

    // 填充方块：当前形状、是否只填空格、是否在等两点、已点的第一个角点
    ProjectionFillShape m_fillShape = ProjectionFillShape::Solid;
    bool            m_fillAirOnly = false;
    bool            m_fillActive = false;
    int m_fillAX = INT_MIN, m_fillAY = INT_MIN, m_fillAZ = INT_MIN;

    QList<SchematicBlockState> m_blocks;   // 用于调色板/收藏（含空气占位）
};
