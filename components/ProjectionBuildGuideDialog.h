/**
 * @file   ProjectionBuildGuideDialog.h
 * @brief  投影「学习搭建」窗口：从最下面一层开始，按材料一步步教怎么搭
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 从本地投影卡片的「学习搭建」按钮进入（主窗口 / 实例助手都能进）。
 * 只读，不修改投影文件：
 *   - 先只显示投影最下面一层，并自动找出这一层数量最多的材料作为第一步；
 *   - 平面图逐格铺网格，四边都有刻度，标出每个格子的序号和整层长度；
 *   - 有实例材质时平面图铺真实方块贴图，搭建顺序列表也带材质小图标；
 *   - 当前步骤要搭的方块用琥珀色高亮，立体图同步只看这一层并高亮同一批方块；
 *   - 点「这一步搭完了，下一步」进入本层的下一种材料；本层全部材料搭完后
 *     自动进入上一层，如此循环，直到整份投影搭完。
 *
 * 窗口为独立可拖动/缩放的普通窗口，方便一边看教程一边切回游戏。
 */

#pragma once

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <memory>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
class InstanceTextureLoader;
class LayerBuildPlanWidget;
class SchematicDocument;
class VoxelViewWidget;

/**
 * @brief 一个搭建步骤：某一层里的一种材料
 */
struct ProjectionBuildStep
{
    int layerY = 0;              ///< 水平层（世界坐标 Y）
    int layerOrdinal = 0;        ///< 非空层序号（0 起，自下往上）
    QString blockName;           ///< 方块 ID（整份投影里同一种方块归为一种材料）
    QVector<int> cells;          ///< 该材料在这一层的方格（体积序索引）
    int count = 0;               ///< 本层该材料的数量
    int indexInLayer = 0;        ///< 本层第几种材料（0 起，数量从多到少）
    int stepCountInLayer = 0;    ///< 本层一共有几种材料
};

/**
 * @brief 一层投影的搭建信息
 */
struct ProjectionBuildLayer
{
    int y = 0;                   ///< 世界坐标 Y
    int ordinal = 0;             ///< 非空层序号（0 起）
    int blockCount = 0;          ///< 本层方块总数
    QVector<int> stepIndices;    ///< 本层各步骤在总步骤表里的下标
};

/**
 * @brief 学习搭建窗口
 */
class ProjectionBuildGuideDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 打开学习搭建窗口（独立窗口，不阻塞调用方）
     * @param parent       父窗口（窗口会跟随父窗口生命周期；不阻塞父窗口）
     * @param filePath     投影文件绝对路径（.litematic / .schematic / .schem）
     * @param instancePath 可选：材质与方块中文名的来源实例
     * @param displayName  可选：标题里显示的名称，空则取文件名
     */
    static void open(QWidget *parent, const QString &filePath,
                     const QString &instancePath = QString(),
                     const QString &displayName = QString());

    explicit ProjectionBuildGuideDialog(QWidget *parent, const QString &filePath,
                                        const QString &instancePath = QString(),
                                        const QString &displayName = QString());
    ~ProjectionBuildGuideDialog() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    /** 后台解析结果：doc 为空时 error 说明原因 */
    struct LoadResult
    {
        std::shared_ptr<SchematicDocument> doc;
        QVector<ProjectionBuildStep> steps;
        QVector<ProjectionBuildLayer> layers;
        int totalBlocks = 0;
        QString error;
    };

    /** 解析投影并排好「层 -> 材料」的搭建顺序（工作线程执行） */
    static LoadResult loadProjection(const QString &filePath);
    /** 把解析好的文档与步骤接到界面上 */
    void applyLoad(const LoadResult &result);

    void initUI();
    void applyChrome();

    /** 开始后台解析 */
    void startLoad();
    /** 后台补方块中文名（不卡首屏） */
    void startBlockNamesLoad();
    /** 中文名回来之后刷新界面上的材料名 */
    void refreshMaterialNames();

    /** 显示第 index 步；index 等于步骤总数表示全部完成 */
    void goToStep(int index);
    /** 按当前步骤刷新平面图、立体图、文字和按钮状态 */
    void refreshStepUi();
    /** 完成 / 跳过当前步骤，进入下一步 */
    void advanceStep();
    /** 回到第一步 */
    void restart();
    /** 标记全部完成 */
    void showCompleted();

    /** 重建左侧搭建顺序树 */
    void rebuildStepTree();
    /** 同步树里的当前步骤、已完成状态与当前层展开状态 */
    void updateStepTreeState();

    QSet<int> targetCellSet() const;
    QSet<int> layerCells(int layerY);

    QString materialPrimary(const QString &blockName) const;
    QString materialSecondary(const QString &blockName) const;
    QString subjectName() const;

    void setControlsEnabled(bool enabled);
    void updateHoverHint(int x, int z, const QString &blockId);

    QString m_filePath;
    QString m_instancePath;
    QString m_displayName;

    std::shared_ptr<SchematicDocument> m_doc;
    InstanceTextureLoader *m_textureLoader = nullptr;
    QVector<ProjectionBuildStep> m_steps;
    QVector<ProjectionBuildLayer> m_layers;
    int m_totalBlocks = 0;

    int m_currentStep = 0;
    int m_lastShownLayer = -1;
    bool m_completed = false;

    bool m_loadStarted = false;
    bool m_loading = false;
    bool m_docReady = false;

    QHash<QString, QString> m_cnMap;
    bool m_cnMapLoading = false;

    /** 每层的全部方块格（立体图按层取景用；按需构建） */
    QHash<int, QSet<int>> m_layerCellCache;

    // ---- 顶部 ----
    QLabel *m_fileLabel = nullptr;
    QLabel *m_sizeChip = nullptr;
    QLabel *m_layerChip = nullptr;
    QLabel *m_blockChip = nullptr;

    // ---- 当前步骤 ----
    QWidget *m_stepCard = nullptr;
    QLabel *m_stepLabel = nullptr;
    QLabel *m_materialSwatch = nullptr;
    QLabel *m_materialLabel = nullptr;
    QLabel *m_materialIdLabel = nullptr;
    QLabel *m_stepHint = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;

    // ---- 平面图 / 立体图 / 步骤树 ----
    LayerBuildPlanWidget *m_plan = nullptr;
    VoxelViewWidget *m_view = nullptr;
    QComboBox *m_viewCombo = nullptr;
    QTreeWidget *m_tree = nullptr;

    // ---- 底部 ----
    QLabel *m_hintLabel = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_skipBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_restartBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
