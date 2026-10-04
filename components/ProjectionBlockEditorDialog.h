/**
 * @file   ProjectionBlockEditorDialog.h
 * @brief  投影方块编辑器对话框（3D 立体视图）
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 基于 VoxelViewWidget 的 3D 体素方块编辑器，数据模型使用 SchematicDocument。
 * 功能：
 *   - 3D 立体查看（左键拖动旋转、滚轮缩放）
 *   - 交互编辑：右键破坏、Ctrl+左键放置当前选中方块
 *   - 方块调色板：浏览/选择投影中出现的方块进行放置
 *   - 批量操作：整块替换、全空间填充、区域填充
 *   - 图层过滤（按 Y 层突出显示）
 *   - 撤销/重做、保存副本、就地保存
 *
 * 作为"原生插件"的宿主界面入口使用；数据读写位于启动器核心 SchematicDocument。
 */
#pragma once

#include <QDialog>
#include <climits>

#include "utils/Schematic/SchematicDocument.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/CommandAssistant/GameRegistry.h"

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

    /** 加载后返回是否成功 */
    bool loadProjection(const QString &filePath);

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
    void onToolChanged(int index);
    void onFillLayerChanged(int value);
    void onShowAirToggled(bool checked);
    void onUndo();
    void onRedo();
    void onSaveAs();
    void onSaveInPlace();
    void onReplaceAll();
    void onFillAll();
    void onFillBox();
    void onPickBlock();
    void onRefreshStats();
    void filterPalette(const QString &text);

private:
    void initUI();
    void populatePalette();
    void applyExistingBlockAsCurrent();
    void rebuildView();
    QString currentBlockName() const;
    void pushUndo();
    void updateTitle();
    void updateUndoRedo();
    void applyPluginSettings();
    void loadBlockRegistry();
    QString buildStatsText() const;

    bool            m_dirty = false;
    QString         m_filePath;
    QString         m_lastSavedPath;
    QString         m_instancePath;
    SchematicDocument m_doc;
    InstanceTextureLoader m_texLoader;   // 材质加载器（成员以保持生命周期）
    BlockShapeLoader m_shapeLoader;      // 方块真实形状加载器（成员以保持生命周期）
    GameRegistry m_gameRegistry;         // 游戏注册表（方块中英文名映射）
    QHash<QString, QString> m_blockCnMap; // 方块ID(小写) -> 中文名

    QString displayName() const;

    // 交互工具
    enum Tool : int {
        ToolOrbit = 0,
        ToolPlace,
        ToolBreak,
        ToolFill
    };
    int             m_tool = ToolOrbit;

    // 撤销/重做（存储整份文件快照）
    QStringList     m_undoStack;
    QStringList     m_redoStack;

    // UI
    QStackedWidget  *m_stack = nullptr;
    VoxelViewWidget *m_view = nullptr;
    QComboBox       *m_toolCombo = nullptr;
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

    int m_hoverX = INT_MIN, m_hoverY = INT_MIN, m_hoverZ = INT_MIN;

    QList<SchematicBlockState> m_blocks;   // 用于调色板/收藏（含空气占位）
};
