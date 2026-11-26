/**
 * @file   LayerBuildPlanWidget.h
 * @brief  学习搭建的二维分层平面图（逐格网格 + 四边尺寸刻度）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 只画投影的某一水平层：每个方块画成一格，格与格之间都有网格线；上下左右
 * 四条边各有一圈刻度，标出当前视野里每一格的序号和整层的长度（多少格）。
 * 当前步骤要搭的材料用琥珀色高亮，其余材料压暗，方便对照着在游戏里摆方块。
 * 传入 InstanceTextureLoader 后，格子会铺上实例里的真实方块贴图；取不到
 * 贴图的方块、或没有实例时，继续用代表色填充。
 *
 * 控件只读，不修改投影；支持滚轮缩放、拖动平移、双击/0 键回到适应窗口。
 */

#pragma once

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QSet>
#include <QWidget>

class InstanceTextureLoader;
class SchematicDocument;

/**
 * @brief 单层平面图控件
 */
class LayerBuildPlanWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LayerBuildPlanWidget(QWidget *parent = nullptr);

    /** 设置要展示的投影（不转移所有权；nullptr 表示清空） */
    void setDocument(SchematicDocument *doc);

    /**
     * @brief 设置实例材质加载器（不转移所有权）
     *
     * 传 nullptr 或加载器没有可用图集时，平面图继续用方块代表色绘制。
     */
    void setTextureLoader(InstanceTextureLoader *loader);
    InstanceTextureLoader *textureLoader() const { return m_loader; }

    /**
     * @brief 显示某一层，并高亮这一步要搭的方块格
     * @param layerY      水平层（世界坐标 Y）
     * @param targetCells 这一步要搭的方格（体积序索引，见 SchematicDocument）
     */
    void setStep(int layerY, const QSet<int> &targetCells);

    /** 没有文档时显示的提示（加载中 / 解析失败等） */
    void setPlaceholder(const QString &text);
    /** 文档已就绪、叠在平面图中央的提示（例如全部搭完），空字符串表示不显示 */
    void setOverlayMessage(const QString &text);

    /** 让整层刚好放进可视区域 */
    void fitToView();

    QSize minimumSizeHint() const override;

signals:
    /** 鼠标移到某个格子上：方块 ID 为空表示离开 / 空白格 */
    void cellHovered(int x, int z, const QString &blockId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /** 网格绘图区（四周留出刻度尺的宽度） */
    QRect contentRect() const;
    /** 当前缩放下的刻度间隔（1 / 2 / 5 / 10 … 格） */
    static int tickStepFor(double scale);
    /** 世界坐标格 -> 像素矩形 */
    QRectF cellRect(int x, int z) const;
    /** 屏幕位置 -> 方块格；不在层内返回 false */
    bool cellAt(const QPoint &pos, int &x, int &z) const;
    /** 把平移量限制在合理范围，避免把整层拖出视野 */
    void clampOffset();
    /** 建立调色板下标 -> 方块颜色缓存（换文档时重建） */
    void rebuildPaletteColors();
    QColor colorForPalette(int paletteIndex) const;
    /** 建立调色板下标 -> 方块材质小图缓存（有实例材质时才生成） */
    void rebuildPaletteTextures();
    /** 清掉悬停格并通知外部 */
    void clearHover();

    void drawRulers(QPainter &painter, const QRectF &grid,
                    int x0, int x1, int z0, int z1, int tickStep) const;

    SchematicDocument *m_doc = nullptr;
    InstanceTextureLoader *m_loader = nullptr;
    int m_layerY = -1;
    QSet<int> m_targetCells;
    /** 当前步骤高亮格在层内的矩形范围（缩得很小时用它概括显示，避免逐格重绘） */
    QRect m_targetBoundsCells;
    QHash<int, QColor> m_paletteColors;
    QHash<int, QPixmap> m_paletteTextures;

    double m_scale = 24.0;       ///< 每格像素
    QPointF m_offset;            ///< 网格左上角相对绘图区左上角的偏移
    bool m_fitted = false;

    bool m_panning = false;
    QPoint m_pressPos;
    QPointF m_offsetAtPress;

    int m_hoverX = -1;
    int m_hoverZ = -1;

    QString m_placeholder;
    QString m_overlayMessage;
};
