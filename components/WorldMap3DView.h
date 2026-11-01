/**
 * @file   WorldMap3DView.h
 * @brief  存档地图的立体（3D）视图控件
 * @author BlockBox Team
 * @date   2026-10-30
 *
 * 把俯视地表图 + 地表高度图当成一块真正的地形网格来画：可绕视角中心转圈、
 * 抬高 / 压低视线、拉近拉远、右键平移，画面是带光照与雾效的立体地形，
 * 山脊、河谷、海岸线一眼能看出来。
 *
 * 渲染走纯软件光栅化（QPainter 多边形填充 + 画家算法），不依赖 OpenGL：
 * 桌面、Android、鸿蒙同一套代码，掉不了驱动、也不占额外的显存。
 *
 * 线程模型：所有接口都在 UI 线程调用；重绘时现场采样网格，不后台渲染。
 */
#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QResizeEvent;
class QTimer;

/**
 * @brief 存档地表地图的立体视图
 *
 * 用法：页面把当前画布（颜色图 + 高度图）用 setSurface() 绑进来，
 * 视口消息（滚轮 / 拖拽）在控件内部消化；工具条的 +/− / 适应 调
 * zoomBy() / fitToWorld()。
 */
class WorldMap3DView : public QWidget
{
    Q_OBJECT

public:
    explicit WorldMap3DView(QWidget *parent = nullptr);
    ~WorldMap3DView() override;

    /**
     * @brief 绑定地图数据（不拷贝，指针须在下次绑定前一直有效）
     *
     * @param color      俯视地表图（与 2D 视图同一张）
     * @param height     地表高度图（Format_Grayscale16；可空 = 按海平面平地画）
     * @param minChunkX  图像左上角区块坐标 X
     * @param minChunkZ  图像左上角区块坐标 Z
     * @param blockPerPixel 每像素方块数
     * @param keepView   是否保持当前视角（换着色 / 换维度时保持观感）
     */
    void setSurface(const QImage *color, const QImage *height,
                    int minChunkX, int minChunkZ, int blockPerPixel,
                    bool keepView = true);

    /** 解除绑定（无存档 / 还没有画面时） */
    void clearSurface();
    bool hasSurface() const;

    /** 摄像机取景到整张地图（「适应」按钮 / 双击 / 快捷键 0） */
    void fitToWorld();
    /** 回到默认视角（斜上方环视整张图） */
    void resetView();
    /** 以屏幕中心为锚缩放：factor < 1 拉近，> 1 拉远 */
    void zoomBy(double factor);
    /** 转视角（度）：dyaw 水平、dpitch 垂直 */
    void orbitBy(double dyaw, double dpitch);
    /** 平移视角中心（屏幕像素增量） */
    void panByScreen(const QPoint &delta);
    /** 让视角中心落到某个方块坐标上 */
    void centerOnBlock(qint64 blockX, qint64 blockZ);

    /** 视角中心（方块坐标） */
    QPointF targetBlock() const;

    /** 起伏放大倍数（1 = 与真实比例一致；越大山越高） */
    void setReliefScale(double scale);
    double reliefScale() const { return m_relief; }

    // ── 探针 / 状态栏用 ──
    double yawDegrees() const { return m_yaw; }
    double pitchDegrees() const { return m_pitch; }
    /** 相机到视角中心的距离（方块） */
    double cameraDistance() const { return m_dist; }
    /** 最近一帧画的四边形数 */
    int lastQuadCount() const { return m_lastQuads; }
    /** 最近一帧的渲染耗时（毫秒） */
    int lastFrameMs() const { return m_lastFrameMs; }
    /** 最近一帧的画面（探针截图用） */
    QImage lastFrame() const { return m_frame; }
    /** 高度图里是否有可用数据（false = 只能按平地画） */
    bool hasHeightData() const;
    /** 方块坐标投影到屏幕；不可见 / 无效返回 false */
    bool projectBlock(qint64 blockX, qint64 blockZ, QPoint *out) const;

signals:
    /** 视角变化（页面据此刷新状态栏） */
    void viewChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /** 三维点（世界方块坐标；网格按视角中心做了平移，避免大坐标掉精度） */
    struct Vec3
    {
        double x = 0, y = 0, z = 0;
    };

    /** 相机：位置 + 右手坐标系（right / up / forward） */
    struct Camera
    {
        Vec3 eye;
        Vec3 right;
        Vec3 up;
        Vec3 fwd;
        double focal = 1.0;  ///< 焦距（像素）
        double zNear = 1.0;
    };

    /** 每格旗标（cellFlags 的位组合） */
    enum CellFlag : quint8
    {
        FlagWater = 1 << 0, ///< 水面格：按色相启发式识别，不叠地形光照
        FlagShore = 1 << 1, ///< 岸线格：紧挨陆地的水，带一圈泡沫
        FlagVoid  = 1 << 2, ///< 空洞填出来的格：没有真实地表数据，深色补面
    };

    /** 采样网格：顶点高度 + 每格底色与光照（一次重建、多帧复用） */
    struct Mesh
    {
        int cellsX = 0;      ///< 横向格数
        int cellsZ = 0;      ///< 纵向格数
        int stride = 1;      ///< 每格代表的图像像素数
        int vertsX = 0;      ///< 顶点数 = cellsX + 1
        int vertsZ = 0;
        double cellW = 1.0;  ///< 一格在世界里的宽 / 深（方块）
        QVector<float> vx;   ///< 顶点世界坐标（已相对网格中心）
        QVector<float> vz;
        QVector<float> vy;   ///< 顶点高度（已乘起伏倍数）
        QVector<quint8> valid; ///< 顶点是否有真实地表（空洞扩散不置位）
        QVector<QRgb> cellColor; ///< 每格底色（已乘光照）
        QVector<quint8> cellFlags; ///< 每格旗标（CellFlag 组合，0 = 无地表不画）
        float vyMin = 0.0f;  ///< 已画格子的顶点高度范围（裙边深度定标用）
        float vyMax = 0.0f;
        bool empty = true;
        int minHeight = 0;   ///< 地图最低 / 最高地表（定标用）
        int maxHeight = 0;
        bool anyHeight = false;
    };

    void buildMesh(bool fast);
    void renderFrame();
    /** 画面缓冲应有的像素尺寸（按设备像素比放大，最多 1.5 倍） */
    QSize framePixelSize() const;
    void cameraSetup(Camera *cam) const;
    /**
     * 当前视野在地图上覆盖的矩形（图像像素）。
     * 网格只铺这一块：拉近时格子变小、能看清细节，拉远时自动铺满全图。
     */
    void visiblePixelRect(int *px0, int *py0, int *pw, int *ph) const;
    /** 扫一遍高度图，记下整张图的地表高度范围（纵向归一化与全图一致） */
    void updateHeightRange();
    /** 每 1 格高度换成多少个世界单位（起伏倍数 + 平坦度上限都算在这里） */
    double verticalUnit() const;
    /** 把世界坐标投影成屏幕坐标；返回 false 表示在相机后面 */
    bool projectPoint(const Camera &cam, double x, double y, double z,
                      QPointF *out, double *depth) const;
    void scheduleRefine();

    QColor cardColor() const;
    QColor borderColor() const;
    /** 天空渐变在屏幕纵向 t（0 = 顶，1 = 底）处的颜色；雾色按它取，远地形正好化进天空 */
    QRgb skyColorAt(double t) const;

    // ── 绑定的数据 ──
    const QImage *m_color = nullptr;
    const QImage *m_height = nullptr;
    int m_minChunkX = 0;
    int m_minChunkZ = 0;
    int m_bpp = 1;

    // ── 相机 ──
    double m_yaw = 35.0;    ///< 水平角（度）
    double m_pitch = 42.0;  ///< 俯角（度，0 = 平视，≤ 88）
    double m_dist = 600.0;  ///< 相机到视角中心的距离（方块）
    double m_tx = 0.0;      ///< 视角中心的世界坐标（相对地图中心）
    double m_ty = 0.0;
    double m_tz = 0.0;
    double m_relief = 1.4;  ///< 起伏放大倍数（1 = 真实比例，默认略夸张一点）

    Mesh m_mesh;
    bool m_meshDirty = true;
    int m_cellsTarget = 224; ///< 静止时的网格边长（格）
    int m_cellsFast = 128;   ///< 拖动中的网格边长（格）
    /** 整张图的地表高度范围 + 是否有高度数据（纵向归一化用，与取景窗口无关） */
    int m_hMin = 0;
    int m_hMax = 0;
    bool m_anyHeight = false;

    QImage m_frame;
    QSize m_frameSize;
    int m_lastQuads = 0;
    int m_lastFrameMs = 0;
    bool m_fastMode = false;
    QTimer *m_refineTimer = nullptr;

    // ── 鼠标交互 ──
    bool m_dragging = false;
    bool m_panning = false;
    QPoint m_pressPos;
    QPoint m_lastPos;
};
