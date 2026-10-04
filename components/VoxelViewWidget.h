/**
 * @file   VoxelViewWidget.h
 * @brief  3D 体素方块视图控件（OpenGL）
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 基于 QOpenGLWidget 渲染投影方块（non-air 方块以带简单光照的色块显示），
 * 提供轨道相机（左键拖动旋转、滚轮缩放）与射线拾取（右键破坏、Ctrl+左键放置）。
 *
 * 数据来源于 SchematicDocument（宿主注入，不转移所有权）。
 */
#pragma once

#include <QColor>
#include <QHash>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
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
    explicit VoxelViewWidget(QWidget *parent = nullptr);
    ~VoxelViewWidget() override;

    /**
     * @brief 设置要渲染的投影文档（不转移所有权；nullptr 表示清空）
     */
    void setDocument(SchematicDocument *doc);

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
    void rebuild();

    void setHover(int x, int y, int z);
    void resetView();
    void setDirty() { update(); }

    /**
     * @brief 设置图层过滤（Y 层），仅显示该层的方块
     * @param layer -1 表示显示全部；>=0 表示仅显示该 Y 层
     */
    void setLayerFilter(int layer);
    int layerFilter() const { return m_layerFilter; }

    /**
     * @brief 图层显示模式
     * - All：显示全部（忽略图层过滤）
     * - Single：仅显示当前图层（默认）
     * - Below：显示「当前图层及以下」的方块，当前层正常、以下层变暗，便于编辑当前层时查看下方结构
     */
    enum class LayerMode { All, Single, Below };

    /**
     * @brief 设置图层显示模式（配 setLayerFilter 使用）
     */
    void setLayerMode(LayerMode mode);
    LayerMode layerMode() const { return m_layerMode; }

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
    /** 鼠标悬停方块变化（未悬停时坐标为 INT_MIN） */
    void hoverChanged(int x, int y, int z);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    struct CubeVertex {
        float x, y, z;      // corner min
        float sx, sy, sz;   // size
        float r, g, b, a;   // color
        float hoverR, hoverG, hoverB;
        float isHover;
        float dim;          // 0 = 正常；1 = 变暗（此层及以下模式下，当前层以下的方块）
        float uv[6][4];     // 每面对应贴图格在图集中的 UV 矩形 [face index][u0,v0,u1,v1]；未贴图时为 0
        float faceUse[6];   // 每面：1 = 使用贴图，0 = 颜色回退
        bool  rotated = false;   // 是否带元素旋转（模型 element 的 rotation）
        int   rotAxis  = 1;      // 0=x 1=y 2=z
        float rotAngle = 0.0f;   // 度
        float rotOx = 0.5f, rotOy = 0.5f, rotOz = 0.5f; // 绝对世界旋转支点（方块中心）
        int   cellId;       // 所属方块格（平坦索引），用于悬停高亮整格所有元素
    };

    void buildGeometry();
    void destroyGL();
    void orbitBy(float dTheta, float dPhi);
    bool pickRay(const QPoint &pos, int &x, int &y, int &z) const;
    /** 计算顶点数据（仅 CPU，可在控件显示前调用）；随后置 m_dirty */
    void computeVertexData();
    /** 上传顶点数据到 VBO（须在 GL 上下文有效时调用，如 paintGL） */
    void uploadGeometry();
    /** 绑定/更新材质图集纹理（须在 GL 上下文有效时调用） */
    void uploadAtlasTexture();
    static QColor colorForName(const QString &blockName);
    /** 取方块状态（含属性）的形状（带跨重建缓存）；无形状返回 nullptr */
    const BlockShapes *shapesFor(const QString &stateName) const;
    /** 计算一个形状盒的世界轴向包围盒（含元素旋转），用于射线拾取 */
    static void boxWorldBounds(const ShapeBox &box, int x, int y, int z,
                               float &x0, float &y0, float &z0,
                               float &x1, float &y1, float &z1);

    SchematicDocument *m_doc = nullptr;
    InstanceTextureLoader *m_loader = nullptr;
    BlockShapeLoader *m_shapeLoader = nullptr;
    bool m_showAir = false;

    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vao;
    QOpenGLTexture *m_atlasTex = nullptr;
    int m_vertexCount = 0;
    QByteArray m_vertexData;   // 顶点数据缓冲（CPU 构建，paintGL 时上传）
    bool m_glReady = false;    // VBO/VAO/program 是否已构建（initializeGL 后为 true）

    // 相机
    float m_theta = 0.6f;
    float m_phi = 0.5f;
    float m_radius = 12.0f;
    QPoint m_lastPos;
    bool m_dragging = false;
    bool m_dirty = true;

    int m_hoverX = INT_MIN, m_hoverY = INT_MIN, m_hoverZ = INT_MIN;
    int m_layerFilter = -1;
    LayerMode m_layerMode = LayerMode::Single;
    QVector<CubeVertex> m_cubes;
    mutable QHash<QString, BlockShapes> m_shapeCache;   // 形状缓存（按方块状态字符串）
};

