/**
 * @file   VoxelViewWidget.h
 * @brief  3D 体素方块视图控件（OpenGL）
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 基于 QOpenGLWidget 渲染投影方块（按方块真实形状 + 材质图集 + 逐顶点环境光遮蔽）。
 * 提供自由飞行相机：WASD 水平移动、空格上升、Shift 下降、左键拖动转向，
 * 滚轮沿视线前后；以及射线拾取（右键破坏、Ctrl+左键放置）。
 *
 * 渲染管线要点（性能与观感）：
 *   - 只生成「看得见」的面：相邻格是不透明白色整方块时，该面直接丢弃；
 *   - 逐顶点 AO（角点遮挡）+ 方向性面光照，无需额外贴图即有明显立体感；
 *   - 同一种方块状态（调色板条目）只解析一次形状/贴图，整份几何按调色板缓存生成；
 *   - 拾取用 DDA 体素步进，只测试射线真正经过的格子；
 *   - 悬停高亮与选中线框走独立的叠加通道，鼠标移动不再重传整份顶点数据。
 *
 * 数据来源于 SchematicDocument（宿主注入，不转移所有权）。
 */
#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QSet>
#include <QTimer>
#include <QVector3D>
#include <climits>

#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/SchematicDocument.h"

/**
 * @brief 3D 体素方块视图控件
 */
class VoxelViewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    /** 图层显示模式 */
    enum class LayerMode {
        All,      ///< 显示全部（忽略图层过滤）
        Single,   ///< 仅显示当前图层（默认）
        Below     ///< 当前图层及以下，下方变暗
    };

    /** 视角预设 */
    enum class ViewPreset {
        Iso,      ///< 斜 45°（默认）
        Front,    ///< 从 -Z 看向 +Z
        Back,     ///< 从 +Z 看向 -Z
        Left,     ///< 从 -X 看向 +X
        Right,    ///< 从 +X 看向 -X
        Top,      ///< 俯视
        Bottom    ///< 仰视
    };

    explicit VoxelViewWidget(QWidget *parent = nullptr);
    ~VoxelViewWidget() override;

    /**
     * @brief 设置要渲染的投影文档（不转移所有权；nullptr 表示清空）
     */
    void setDocument(SchematicDocument *doc);
    SchematicDocument *document() const { return m_doc; }

    /**
     * @brief 设置实例材质加载器（不转移所有权；nullptr 表示关闭贴图，使用颜色回退）
     */
    void setTextureLoader(InstanceTextureLoader *loader);
    InstanceTextureLoader *textureLoader() const { return m_loader; }

    /**
     * @brief 设置方块形状加载器（不转移所有权；nullptr 表示关闭不完整方块形状，使用整体立方体）
     */
    void setShapeLoader(BlockShapeLoader *loader);
    BlockShapeLoader *shapeLoader() const { return m_shapeLoader; }

    void setShowAir(bool show);
    bool showAir() const { return m_showAir; }

    /** 重建几何（纯 CPU，可在控件显示前调用） */
    void rebuild();

    void setHover(int x, int y, int z);
    void resetView();
    /** 仅请求重绘（不重建几何） */
    void setDirty() { update(); }

    /** 平滑飞到整个投影（或指定格子集合）的包围盒 */
    void focusAll();
    void focusOnCells(const QSet<int> &cells);

    /** 应用视角预设（保持当前观察中心与距离） */
    void applyViewPreset(ViewPreset preset);

    /** 「贴面放置」：Ctrl+左键放到鼠标所指方块的外侧相邻格，而不是替换它 */
    void setPlaceAdjacent(bool on) { m_placeAdjacent = on; }
    bool placeAdjacent() const { return m_placeAdjacent; }

    /**
     * @brief 设置被「选中」的方块格（体积序索引，见 SchematicDocument）
     * @param cells 格索引集合；选中格以绿色边框标记
     */
    void setSelectedCells(const QSet<int> &cells);
    const QSet<int> &selectedCells() const { return m_selectedCells; }
    void clearSelection() { setSelectedCells(QSet<int>()); }

    /** 选取工具：开启后，左键单击（不拖动）会发出 selectRequested，拖动仍可转向 */
    void setSelectTool(bool on);
    bool selectTool() const { return m_selectTool; }

    /**
     * @brief 设置图层过滤（Y 层），仅显示该层的方块
     * @param layer -1 表示显示全部；>=0 表示仅显示该 Y 层
     */
    void setLayerFilter(int layer);
    int layerFilter() const { return m_layerFilter; }

    /**
     * @brief 设置图层显示模式（配 setLayerFilter 使用）
     */
    void setLayerMode(LayerMode mode);
    LayerMode layerMode() const { return m_layerMode; }

    /** 当前几何的三角形面数（用于界面上的实时统计） */
    int quadCount() const { return m_quadCount; }

    /** 根据方块名取稳定颜色（供外观预览等复用） */
    static QColor blockColorOf(const QString &blockName);
    QColor blockColor(const SchematicBlockState &state) const;

    /** 命中探测：把屏幕坐标转换为投影内方块坐标，未命中返回 false */
    bool rayPick(const QPoint &pos, int &x, int &y, int &z) const;

signals:
    /** 用户请求在 (x,y,z) 放置当前选中方块（Ctrl+左键） */
    void placeRequested(int x, int y, int z);
    /** 用户请求破坏 (x,y,z)（右键） */
    void breakRequested(int x, int y, int z);
    /** 用户以选取工具点击了 (x,y,z)（左键单击） */
    void selectRequested(int x, int y, int z);
    /** 鼠标悬停方块变化（未悬停时坐标为 INT_MIN） */
    void hoverChanged(int x, int y, int z);
    /** 几何重建完成：可见面数 + 耗时（毫秒） */
    void geometryRebuilt(int quads, int elapsedMs);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    /** 紧凑顶点（28 字节）：位置 / 颜色 / UV / 光照 / 贴图开关 / 变暗 / 染色 */
    struct Vtx
    {
        float x, y, z;              // 0..11
        uchar r, g, b, a;           // 12..15
        ushort u, v;                // 16..19（归一化图集坐标）
        uchar light;                // 20（面光 × AO）
        uchar tex;                  // 21（1 = 使用贴图）
        uchar dim;                  // 22（1 = 变暗）
        uchar pad;                  // 23
        uchar tr, tg, tb;           // 24..26（贴图乘色：草/树叶/水按种类着色）
        uchar tinted;               // 27（1 = 该顶点使用染色）
    };

    /** 单个形状盒某面的贴图信息 */
    struct RenderFace
    {
        bool useTex = false;
        float uv[4] = {0, 0, 0, 0};   // u0,v0,u1,v1（图集归一化坐标）
        bool cull = true;             // 可被邻居整方块剔除（模型给了 cullface 才为 true）
        bool shadeOff = false;        // 模型 shade:false：不吃方向光
        bool noAo = false;            // 模型 ambientocclusion:false：不做角落阴影
        uchar tint[3] = {255, 255, 255}; // 贴图乘色（tintindex）
    };

    /** 单个形状盒的渲染描述（相对格子原点 0..1） */
    struct RenderBox
    {
        float from[3] = {0, 0, 0};
        float to[3] = {1, 1, 1};
        bool rotated = false;
        int rotAxis = 1;
        float rotAngle = 0.0f;
        float rotOrigin[3] = {0.5f, 0.5f, 0.5f};
        int yaw = 0;                  // blockstate 的 y 旋转（绕格心，渲染期施加）
        RenderFace face[6];
        bool fullCube = true;         // 正好铺满整格（可用于背面剔除）
    };

    /** 一个调色板条目的渲染描述（整份几何只解析一次） */
    struct PaletteRender
    {
        QColor color;
        bool isAir = false;
        bool opaque = true;           // 不透明（可遮挡邻居）
        bool fullCube = false;        // 整格满方块
        bool tinted = false;          // 已带真实材质（不再叠加逐格明度扰动）
        float solidity = 1.0f;        // 实心占比（台阶 0.5 / 平面植物 0）：决定接触阴影强度
        QVector<RenderBox> boxes;
    };

    void buildGeometry();
    void destroyGL();
    void lookBy(float dYaw, float dPitch);
    void applyFlyKeys(float dt);
    void moveEye(const QVector3D &delta);
    QVector3D lookForward() const;
    QVector3D eyePosition() const;
    QVector3D lookTarget() const;
    bool isFlyKey(int qtKey) const;

    /** 射线拾取：DDA 体素步进 + 命中格形状精测 */
    bool pickRay(const QPoint &pos, int &x, int &y, int &z, bool skipAir = false,
                 int *outNormal = nullptr) const;

    // ---- 渲染缓存 ----
    void ensurePaletteCache();
    void buildOccupancy();
    void rebuildSelectionGeometry();
    void rebuildHoverGeometry();

    /** 上传顶点数据到 VBO（须在 GL 上下文有效时调用） */
    void uploadGeometry();
    void uploadSelectionGeometry();
    void uploadHoverGeometry();
    /** 绑定/更新材质图集纹理（须在 GL 上下文有效时调用；仅在材质变化后真正重建） */
    void uploadAtlasTexture();
    void ensureBackgroundResources();

    static QColor colorForName(const QString &blockName);
    /** 取方块状态（含属性）的形状（带跨重建缓存）；无形状返回 nullptr */
    const BlockShapes *shapesFor(const QString &stateName) const;
    /** 计算一个形状盒的世界轴向包围盒（含元素旋转），用于射线拾取 */
    static void boxWorldBounds(const RenderBox &box, int x, int y, int z,
                               float &x0, float &y0, float &z0,
                               float &x1, float &y1, float &z1);
    /** 把某格的形状盒（含旋转）追加为线段顶点（叠加通道用） */
    void appendCellLines(int cellIndex, const QVector<RenderBox> &boxes,
                         uchar r, uchar g, uchar b, QByteArray *out) const;

    SchematicDocument *m_doc = nullptr;
    InstanceTextureLoader *m_loader = nullptr;
    BlockShapeLoader *m_shapeLoader = nullptr;
    bool m_showAir = false;
    bool m_placeAdjacent = false;

    // ---- 主几何通道 ----
    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vao;
    QOpenGLTexture *m_atlasTex = nullptr;
    bool m_atlasFallback = false;      // 当前绑定的是 1×1 白色兜底纹理
    int m_vertexCount = 0;
    int m_quadCount = 0;
    int m_opaqueVertexCount = 0;       // 前段不透明顶点数（其后为半透明，绘制时关深度写入）
    QByteArray m_vertexData;           // CPU 侧顶点缓冲（24B/顶点）
    bool m_glReady = false;
    bool m_uploadDirty = true;         // 需要重传 VBO
    bool m_atlasDirty = true;          // 需要重建图集纹理
    bool m_needsRepaint = true;

    // ---- 叠加通道（选中线框 / 悬停高亮）----
    QOpenGLShaderProgram m_ovProgram;
    QOpenGLBuffer m_selVbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_selVao;
    QByteArray m_selData;              // 16B/顶点：xyz + rgba
    int m_selVertexCount = 0;
    bool m_selUploadDirty = true;
    QOpenGLBuffer m_hoverVbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_hoverVao;
    QByteArray m_hoverData;            // 先若干三角形（半透明面），后若干线段（描边）
    int m_hoverTriVerts = 0;
    int m_hoverLineVerts = 0;
    bool m_hoverUploadDirty = true;

    // ---- 背景 ----
    QOpenGLShaderProgram m_bgProgram;
    QOpenGLVertexArrayObject m_bgVao;
    bool m_bgReady = false;

    // ---- CPU 侧缓存 ----
    mutable QHash<QString, BlockShapes> m_shapeCache;   // 形状缓存（按方块状态字符串）
    QVector<PaletteRender> m_paletteCache;      // 下标 = 调色板下标
    bool m_paletteCacheValid = false;
    QByteArray m_occupancy;                     // 每格 1 字节：bit0=可见非空气，bit1=不透明整方块

    // ---- 相机（环绕观察：中心点 + 距离 + 朝向角）----
    float m_yaw = 0.6f;
    float m_pitch = -0.35f;
    QVector3D m_orbitCenter{0.5f, 0.5f, 0.5f};
    float m_orbitDist = 24.0f;
    bool m_eyeValid = false;
    QPoint m_lastPos;
    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_pressPos;
    bool m_selectPending = false;
    QSet<int> m_heldKeys;
    QTimer m_flyTimer;

    int m_hoverX = INT_MIN, m_hoverY = INT_MIN, m_hoverZ = INT_MIN;
    int m_layerFilter = -1;
    LayerMode m_layerMode = LayerMode::Single;
    bool m_selectTool = false;
    QSet<int> m_selectedCells;
};
