/**
 * @file   SeedMapPage.h
 * @brief  种子地图页 —— 输入种子直接看群系地形（不依赖已有存档）
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 「更多 > 种子地图」的落地页。和存档地图（读已生成区块）不同，这里只靠
 * cubiomes 按种子预测：填种子 + 选版本与维度，就能看到群系分布 / 高度 /
 * 地形图，按 region 逐块渲染，画布可拖动、滚轮缩放，也能导出成图片。
 *
 * 界面与「存档地图」保持一致：同一套工具栏分组（标题块 / 渲染组 / 视图组 /
 * 输出按钮）、同一套圆角地图卡片、同一套底部状态条；第二行同样能按类型开关
 * 结构标记，并支持按群系高亮。矿石需要读存档区块，种子地图无法预测，
 * 弹窗里会说明去存档地图里找矿。
 */

#ifndef SEEDMAPPAGE_H
#define SEEDMAPPAGE_H

#include <QFutureWatcher>
#include <QImage>
#include <QList>
#include <QVector>
#include <QWidget>

#include "utils/WorldMap/BiomeCatalog.h"

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QTimer;
class SeedMapCanvas;

/** 一处结构标记（结构类型下标对应 StructureFinder::allStructures()） */
struct SeedStructureMarker
{
    qint64 x = 0;
    qint64 z = 0;
    int type = -1;
};

class SeedMapPage : public QWidget
{
    Q_OBJECT

public:
    explicit SeedMapPage(QWidget *parent = nullptr);
    ~SeedMapPage() override;

    /** 进入页面（「更多>种子地图」）：第一次进入时自动画一张默认地图 */
    Q_INVOKABLE void openPage();

    /** 顶栏刷新 / F5：按当前参数重画 */
    Q_INVOKABLE void refresh();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildUi();

    /**
     * @brief 按当前参数从头开始渲染
     * @param keepCenter true=沿用当前视角中心（改参数后的重画）；
     *                   false=用 m_centerX/m_centerZ（生成 / 跳转）
     */
    void startRender(bool keepCenter = true);
    /** 停掉在途渲染 */
    void stopRender();
    /** 渲染下一块 region（由 m_timer 驱动，一块一块出图） */
    void renderStep();

    /** 把种子输入框解析成世界种子（数字直接转；文字按游戏同款 String.hashCode） */
    quint64 seedValue(bool *ok) const;
    int mcValue() const;
    int dimensionValue() const;
    int shadeValue() const;
    /** 高度下拉的值：-1 = 不限制 */
    int heightValue() const;
    /** cubiomes 维度值：0=主世界 / -1=下界 / +1=末地 */
    int cubiomesDim() const;

    void jumpToCoordinate();
    void exportImage();
    void updateSubtitle();
    void updateViewInfo();
    void updateStatus(const QString &text);
    /** 把当前缩放写回工具栏的百分比标签 */
    void syncZoomLabel();

    // ── 结构 / 群系 / 矿物（对齐存档地图的第二行与内容弹窗） ──
    /** 第二行：结构开关按钮 + 全选 / 清空 + 展开 */
    void buildOverlayRow(QWidget *bar2, QHBoxLayout *bar2Lay);
    /** 内容弹窗（搜索 + 结构 / 群系 / 矿物三列） */
    void buildContentPopup();
    void showContentPopup(bool on);
    void positionContentPopup();
    void rebuildPopupLists(const QString &filter);
    /** 按当前种子 / 版本 / 维度 / 画布范围查结构标记 */
    void requestStructures();
    /** 按画布范围采样群系网格 */
    void refreshBiomeGrid();
    /** 按当前选中的群系重建叠加层 */
    void rebuildBiomeOverlay();
    bool anyStructureEnabled() const;
    bool anyBiomeEnabled() const;
    void setAllStructures(bool on);

    SeedMapCanvas *m_view = nullptr;

    QLabel *m_subtitleLabel = nullptr;
    QLineEdit *m_seedEdit = nullptr;
    QComboBox *m_versionCombo = nullptr;
    QComboBox *m_dimCombo = nullptr;
    QComboBox *m_shadeCombo = nullptr;
    QComboBox *m_heightCombo = nullptr;
    QLineEdit *m_xEdit = nullptr;
    QLineEdit *m_zEdit = nullptr;
    QPushButton *m_gridBtn = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QTimer *m_timer = nullptr;

    // 结构开关（第二行）
    QList<QPushButton *> m_structBtns;
    QVector<bool> m_structEnabled;
    QVector<bool> m_biomeEnabled;
    QFutureWatcher<QVector<SeedStructureMarker>> *m_structWatcher = nullptr;

    // 内容弹窗
    QPushButton *m_expandBtn = nullptr;
    QWidget *m_popup = nullptr;
    QLineEdit *m_popupSearch = nullptr;
    QListWidget *m_structList = nullptr;
    QListWidget *m_biomeList = nullptr;
    QListWidget *m_oreList = nullptr;

    // 群系采样网格与叠加层
    BiomeCatalog::SampleGrid m_biomeGrid;
    QImage m_biomeOverlay;

    /** 待渲染的 region 列表（按离中心由近到远排好） */
    QVector<QPoint> m_pendingRegions;
    int m_originRegionX = 0;   ///< 画布左上角 region X
    int m_originRegionZ = 0;   ///< 画布左上角 region Z
    int m_renderedCount = 0;
    bool m_hasRendered = false; ///< 是否已经画过至少一张（决定首次是否适应窗口）
    /** 画布要摆到正中的方块坐标（生成 / 跳转 / 重画时都会更新） */
    double m_centerX = 0.0;
    double m_centerZ = 0.0;
};

#endif // SEEDMAPPAGE_H
