/**
 * @file   WorldMap3DView.cpp
 * @brief  存档地图的立体（3D）视图控件实现
 * @author BlockBox Team
 * @date   2026-10-30
 *
 * 做法很朴素但足够好用：把「俯视地表图 + 地表高度图」重采样成一块地形网格，
 * 用透视投影把网格顶点投到屏幕上，再按「由远及近」的顺序逐格填四边形 ——
 * 也就是画家算法。相邻格子共用顶点，表面天然严丝合缝，不会出现漏光的缝。
 *
 * 立体感来自这几件事：① 每格按地形法线做方向光，叠一层凹谷遮蔽（AO），
 * 阴影带一点冷偏；② 海面按视线角度泛天光、岸边一圈泡沫、细碎波光；
 * ③ 越远越往天空色里化（雾色直接取自天空渐变）；④ 地形边缘垂一圈裙边，
 * 连同"未探索"的深色补面，让地图读起来是一块悬浮的实心岛；
 * ⑤ 静止帧按 DPR 超采样 + 逐边抗锯齿，拖动中自动降档保帧率。
 */

#include "WorldMap3DView.h"

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "utils/WorldMap/WorldMapRenderer.h"

namespace {

/** 竖直视场角（度）：地图页的取景习惯，别太广也别太长焦 */
constexpr double kFovY = 42.0;
/** 圆角卡片半径，与 2D 画布保持一致 */
constexpr qreal kCardRadius = 14.0;
/** 俯角上下限（度）：低于 8° 贴地看不出地形，高于 88° 会退化成正俯视 */
constexpr double kMinPitch = 8.0;
constexpr double kMaxPitch = 88.0;
/** 起伏把"高度跨度"拉成地图像素跨度的多少倍（再乘用户给的回转倍数） */
constexpr double kReliefRatio = 0.20;

inline double dot3(double ax, double ay, double az,
                   double bx, double by, double bz)
{
    return ax * bx + ay * by + az * bz;
}

/**
 * 读高度图某格的地表 Y（Format_Grayscale16；kNoHeightEncoded = 无地表）。
 * 直接读位模式，不经过渲染器的接口 —— 立体视图只要读字节，别为此把
 * 整个渲染器（zlib / cubiomes）链进来。
 */
inline qint16 heightValue(const QImage &image, int px, int py)
{
    constexpr qint16 kNone = qint16(WorldMapRenderer::kNoHeightEncoded);
    if (image.isNull() || px < 0 || py < 0
        || px >= image.width() || py >= image.height()
        || image.format() != QImage::Format_Grayscale16)
        return kNone;
    return qint16(reinterpret_cast<const quint16 *>(
                      image.constScanLine(py))[px]);
}

/** 一帧的颜色插值：先乘光照（已预乘），再按雾浓度往天空色里化 */
inline QRgb blendFog(QRgb base, QRgb fog, double t)
{
    if (t <= 0.0)
        return base | 0xff000000u;
    const double k = 1.0 - t;
    return qRgba(int(qRed(base) * k + qRed(fog) * t),
                 int(qGreen(base) * k + qGreen(fog) * t),
                 int(qBlue(base) * k + qBlue(fog) * t), 255);
}

/**
 * 色相启发式认水面：蓝要压倒红绿一截（原版水色 #3F76E4 连同按水深调暗的
 * 变体都命中）。Height 灰度着色模式下自然不命中，水面特效随之整体关闭，
 * 不会误伤其他着色模式的观感。
 */
inline bool isWaterColor(QRgb c)
{
    const int r = qRed(c), g = qGreen(c), b = qBlue(c);
    return b >= 90 && b - r >= 30 && b - g >= 18;
}

/**
 * 一格底色的面积均值采样：小格子全盒平均、大格子 3×3 子采样。
 * 单点采样在平移 / 旋转时细碎颜色会闪烁，均值之后画面稳得多。
 * 同时统计采样点里"像水"的票数：岸线格子颜色一平均就分不出水陆，
 * 按多数票判定要稳得多（否则岸线会一圈锯齿）。
 * 格子里全是透明像素（无数据）时返回 0。
 */
inline QRgb sampleCellColor(const QImage &color, int px, int py, int stride,
                            int w, int h, int *waterVotes, int *voteTotal)
{
    int sr = 0, sg = 0, sb = 0, n = 0, wv = 0;
    auto add = [&](int x, int y) {
        const QRgb c = *(reinterpret_cast<const QRgb *>(color.constScanLine(y)) + x);
        if (qAlpha(c) < 32)
            return;
        sr += qRed(c);
        sg += qGreen(c);
        sb += qBlue(c);
        if (isWaterColor(c))
            ++wv;
        ++n;
    };
    if (stride <= 1)
    {
        add(px, py);
    }
    else if (stride <= 4)
    {
        const int x1 = std::min(px + stride, w);
        const int y1 = std::min(py + stride, h);
        for (int y = py; y < y1; ++y)
            for (int x = px; x < x1; ++x)
                add(x, y);
    }
    else
    {
        for (int t = 0; t < 3; ++t)
            for (int s = 0; s < 3; ++s)
                add(std::min(px + (stride * s) / 2, w - 1),
                    std::min(py + (stride * t) / 2, h - 1));
    }
    if (n == 0)
        return 0;
    *waterVotes = wv;
    *voteTotal = n;
    return qRgb((sr + n / 2) / n, (sg + n / 2) / n, (sb + n / 2) / n);
}

} // namespace

WorldMap3DView::WorldMap3DView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 180);
    setCursor(Qt::OpenHandCursor);

    // 停手片刻后用高精度网格重画一帧（拖动中先给低精度，保证跟手）
    m_refineTimer = new QTimer(this);
    m_refineTimer->setSingleShot(true);
    m_refineTimer->setInterval(120);
    connect(m_refineTimer, &QTimer::timeout, this, [this]() {
        if (!m_fastMode)
            return;
        m_fastMode = false;
        m_meshDirty = true;
        update();
    });
}

WorldMap3DView::~WorldMap3DView() = default;

// ═══════════════════════ 数据绑定 ═══════════════════════

void WorldMap3DView::setSurface(const QImage *color, const QImage *height,
                                int minChunkX, int minChunkZ, int blockPerPixel,
                                bool keepView)
{
    const QImage *prevColor = m_color;
    const bool hadSurface = hasSurface();
    const bool geometryChanged =
        prevColor == nullptr || color == nullptr
        || prevColor->size() != color->size()
        || m_minChunkX != minChunkX || m_minChunkZ != minChunkZ
        || m_bpp != std::max(1, blockPerPixel);

    m_color = (color && color->isNull()) ? nullptr : color;
    m_height = (height && !height->isNull()
                && height->format() == QImage::Format_Grayscale16)
        ? height
        : nullptr;
    m_minChunkX = minChunkX;
    m_minChunkZ = minChunkZ;
    m_bpp = std::max(1, blockPerPixel);

    updateHeightRange();
    m_meshDirty = true;
    if (!keepView || geometryChanged || !hadSurface || !hasSurface())
        fitToWorld(); // 换图 / 第一次拿到画面：重新取景
    else
        update();
}

void WorldMap3DView::clearSurface()
{
    m_color = nullptr;
    m_height = nullptr;
    m_mesh = Mesh();
    m_meshDirty = true;
    m_frame = QImage();
    update();
}

bool WorldMap3DView::hasSurface() const
{
    return m_color && !m_color->isNull()
        && m_color->width() >= 2 && m_color->height() >= 2;
}

bool WorldMap3DView::hasHeightData() const
{
    return m_anyHeight;
}

// ═══════════════════════ 网格构建 ═══════════════════════

/**
 * 扫一遍高度图，记下整张图的地表高度范围。
 *
 * 纵向归一化必须按**整张图**来算（只按当前取景窗口算的话，一平移山就忽高忽低），
 * 所以这里按固定步长抽稀采样，几万次读像素，可以忽略不计。
 */
void WorldMap3DView::updateHeightRange()
{
    m_anyHeight = false;
    m_hMin = 0;
    m_hMax = 0;
    if (!hasSurface() || !m_height)
        return;
    const int w = m_height->width();
    const int h = m_height->height();
    const int stride = std::max(1, int(std::ceil(double(std::max(w, h)) / 256.0)));
    int lo = std::numeric_limits<int>::max();
    int hi = std::numeric_limits<int>::min();
    for (int y = 0; y < h; y += stride)
    {
        for (int x = 0; x < w; x += stride)
        {
            const qint16 v = heightValue(*m_height, x, y);
            if (v == std::numeric_limits<qint16>::min())
                continue;
            lo = std::min(lo, int(v));
            hi = std::max(hi, int(v));
        }
    }
    if (lo > hi)
        return;
    m_anyHeight = true;
    m_hMin = lo;
    m_hMax = hi;
}

/**
 * 每 1 格高度对应的世界单位（纵向放大系数）。
 *
 * 先把整张图的高度跨度归一化成图幅（方块）的 kReliefRatio 倍 ——
 * 上万格宽的世界里，几十格的山按真实比例根本看不见，归一化之后"山永远是山"。
 * 最后按网格边长封顶：极平坦的存档（海洋 / 超平坦）不会因为归一化被拉成
 * 一丛尖刺，看上去才像地面而不是针板。
 */
double WorldMap3DView::verticalUnit() const
{
    if (!m_anyHeight || !hasSurface())
        return 0.0;
    const double spanBlocks = double(std::max(m_color->width(),
                                              m_color->height()))
                              * m_bpp;
    const double heightSpan = double(std::max(1, m_hMax - m_hMin));
    const double unit = (spanBlocks * kReliefRatio / heightSpan) * m_relief;
    const double refCell = spanBlocks / double(std::max(24, m_cellsTarget));
    return std::min(unit, refCell * 1.2);
}

/**
 * 视野在地图上覆盖的矩形（图像像素坐标）。
 *
 * 把画面四角沿视线打到地表基准平面上，取包围盒再外扩一圈 ——
 * 网格只铺这一块，所以拉近时自动变细、拉远时自动铺满整图。
 */
void WorldMap3DView::visiblePixelRect(int *px0, int *py0, int *pw, int *ph) const
{
    const int w = m_color->width();
    const int h = m_color->height();
    *px0 = 0;
    *py0 = 0;
    *pw = w;
    *ph = h;

    Camera cam;
    cameraSetup(&cam);
    const QSize fs = framePixelSize();
    const double bw = double(fs.width()) * 0.5;
    const double bh = double(fs.height()) * 0.5;
    const double cxBlocks = double(w) * m_bpp * 0.5;
    const double czBlocks = double(h) * m_bpp * 0.5;
    // 打不到平面（视线接近水平）时按足够远的距离兜底
    const double farT = std::max(m_dist * 8.0, std::max(double(w), double(h))
                                 * m_bpp * 2.0);

    double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    bool first = true;
    for (int k = 0; k < 4; ++k)
    {
        const double sx = (k & 1) ? bw : -bw;
        const double sy = (k & 2) ? bh : -bh;
        // 像素 → 世界射线
        double dx = cam.fwd.x * cam.focal + cam.right.x * sx - cam.up.x * sy;
        double dy = cam.fwd.y * cam.focal + cam.right.y * sx - cam.up.y * sy;
        double dz = cam.fwd.z * cam.focal + cam.right.z * sx - cam.up.z * sy;
        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-9)
            continue;
        dx /= len;
        dy /= len;
        dz /= len;
        double t = farT;
        if (dy < -1e-6)
            t = std::clamp(-cam.eye.y / dy, 1.0, farT);
        const double wx = cam.eye.x + dx * t;
        const double wz = cam.eye.z + dz * t;
        if (first)
        {
            minX = maxX = wx;
            minZ = maxZ = wz;
            first = false;
        }
        else
        {
            minX = std::min(minX, wx);
            maxX = std::max(maxX, wx);
            minZ = std::min(minZ, wz);
            maxZ = std::max(maxZ, wz);
        }
    }
    if (first)
        return;

    // 世界坐标 → 图像像素，再外扩 12%（边缘不至于露白）
    double fx0 = (minX + cxBlocks) / double(m_bpp);
    double fx1 = (maxX + cxBlocks) / double(m_bpp);
    double fz0 = (minZ + czBlocks) / double(m_bpp);
    double fz1 = (maxZ + czBlocks) / double(m_bpp);
    const double growX = (fx1 - fx0) * 0.12 + 1.0;
    const double growZ = (fz1 - fz0) * 0.12 + 1.0;
    fx0 -= growX;
    fx1 += growX;
    fz0 -= growZ;
    fz1 += growZ;

    int px = std::clamp(int(std::floor(fx0)), 0, w - 1);
    int py = std::clamp(int(std::floor(fz0)), 0, h - 1);
    int pxEnd = std::clamp(int(std::ceil(fx1)), 1, w);
    int pyEnd = std::clamp(int(std::ceil(fz1)), 1, h);
    if (pxEnd <= px)
        pxEnd = std::min(w, px + 2);
    if (pyEnd <= py)
        pyEnd = std::min(h, py + 2);
    *px0 = px;
    *py0 = py;
    *pw = std::max(2, pxEnd - px);
    *ph = std::max(2, pyEnd - py);
}

/**
 * 把地图重采样成地形网格。
 *
 * 高度不按真实比例摆（真实比例下 100 格的山放在上万格宽的地图上根本看不见），
 * 而是把整张图的"高度跨度"归一化到地图像素跨度的一个固定比例，再乘「起伏」
 * 倍数 —— 这样缩放到任何尺度，山都是山，不会变成一层薄纸或一排尖刺。
 */
void WorldMap3DView::buildMesh(bool fast)
{
    m_mesh = Mesh();
    if (!hasSurface())
        return;

    const QImage *color = m_color;
    const int w = color->width();
    const int h = color->height();
    const double cxBlocks = double(w) * m_bpp * 0.5;
    const double czBlocks = double(h) * m_bpp * 0.5;

    // ① 只看视野覆盖的那一块：拉近时格子自动变小，能看清细节
    int px0 = 0, py0 = 0, pw = w, ph = h;
    visiblePixelRect(&px0, &py0, &pw, &ph);

    const int target = std::max(24, fast ? m_cellsFast : m_cellsTarget);
    const int stride = std::max(1, int(std::ceil(double(std::max(pw, ph))
                                                / double(target))));

    Mesh mesh;
    mesh.stride = stride;
    mesh.vertsX = (pw + stride - 1) / stride + 1;
    mesh.vertsZ = (ph + stride - 1) / stride + 1;
    mesh.cellsX = mesh.vertsX - 1;
    mesh.cellsZ = mesh.vertsZ - 1;
    if (mesh.cellsX < 1 || mesh.cellsZ < 1)
        return;

    const int vertCount = mesh.vertsX * mesh.vertsZ;
    mesh.vx.resize(vertCount);
    mesh.vz.resize(vertCount);
    mesh.vy.resize(vertCount);
    mesh.valid.fill(0, vertCount);

    mesh.cellW = double(stride) * m_bpp;

    // 高度：纵向归一化按整张图的范围算，平移 / 拉近都不会让山忽高忽低
    const bool anyHeight = m_anyHeight && m_height;
    // 尺度要用「方块跨度」而不是像素跨度：bpp>1 时 1 像素 = 多个方块，
    // 拿像素当尺度会让起伏整体缩掉 bpp 倍，地形看着像一张纸
    const double midH = (double(m_hMin) + double(m_hMax)) * 0.5;
    const double unit = verticalUnit();

    auto pixelX = [&](int i) { return std::min(px0 + i * stride, w - 1); };
    auto pixelY = [&](int j) { return std::min(py0 + j * stride, h - 1); };

    for (int j = 0; j < mesh.vertsZ; ++j)
    {
        const int pz = pixelY(j);
        const double wz = double(pz) * m_bpp - czBlocks;
        for (int i = 0; i < mesh.vertsX; ++i)
        {
            const int idx = j * mesh.vertsX + i;
            const int px = pixelX(i);
            mesh.vx[idx] = float(double(px) * m_bpp - cxBlocks);
            mesh.vz[idx] = float(wz);
            if (anyHeight)
            {
                const qint16 v = heightValue(*m_height, px, pz);
                if (v != std::numeric_limits<qint16>::min())
                {
                    mesh.valid[idx] = 1;
                    mesh.vy[idx] = float((double(v) - midH) * unit);
                }
                else
                {
                    mesh.vy[idx] = 0.0f;
                }
            }
            else
            {
                mesh.vy[idx] = 0.0f;
            }
        }
    }

    // ② 空洞扩散：没有地表的顶点从有效邻居借高度（最多 4 轮，只够补边缘毛边）。
    //    补出来的格子按"未探索"深色上色，大块未生成区域的中心仍然留空透天空，
    //    不至于把半张地图糊成一片死黑；边缘也不再像被啃掉一样。
    QVector<quint8> filled = mesh.valid;
    if (anyHeight)
    {
        for (int pass = 0; pass < 4; ++pass)
        {
            bool changed = false;
            for (int j = 0; j < mesh.vertsZ; ++j)
            {
                for (int i = 0; i < mesh.vertsX; ++i)
                {
                    const int k = j * mesh.vertsX + i;
                    if (filled[k])
                        continue;
                    double sum = 0.0;
                    int n = 0;
                    if (i > 0 && filled[k - 1])
                    { sum += mesh.vy[k - 1]; ++n; }
                    if (i + 1 < mesh.vertsX && filled[k + 1])
                    { sum += mesh.vy[k + 1]; ++n; }
                    if (j > 0 && filled[k - mesh.vertsX])
                    { sum += mesh.vy[k - mesh.vertsX]; ++n; }
                    if (j + 1 < mesh.vertsZ && filled[k + mesh.vertsX])
                    { sum += mesh.vy[k + mesh.vertsX]; ++n; }
                    if (n > 0)
                    {
                        mesh.vy[k] = float(sum / double(n));
                        filled[k] = 1;
                        changed = true;
                    }
                }
            }
            if (!changed)
                break;
        }
    }

    // ③ 每格的底色（已乘光照）与旗标
    mesh.cellColor.fill(0, mesh.cellsX * mesh.cellsZ);
    mesh.cellFlags.fill(0, mesh.cellsX * mesh.cellsZ);

    // 光照用的高度先做一次 [1 2 1] 轻度平滑：原始高度逐格抖得厉害，
    // 直接拿来算法线会满屏麻点；平滑只作用于法线，地形本身不变
    QVector<float> smoothH = mesh.vy;
    if (anyHeight && mesh.vertsX > 2 && mesh.vertsZ > 2)
    {
        QVector<float> tmp = mesh.vy;
        for (int j = 0; j < mesh.vertsZ; ++j)
        {
            for (int i = 1; i < mesh.vertsX - 1; ++i)
            {
                const int k = j * mesh.vertsX + i;
                tmp[k] = (mesh.vy[k - 1] + 2.0f * mesh.vy[k] + mesh.vy[k + 1])
                         * 0.25f;
            }
        }
        for (int j = 1; j < mesh.vertsZ - 1; ++j)
        {
            for (int i = 0; i < mesh.vertsX; ++i)
            {
                const int k = j * mesh.vertsX + i;
                smoothH[k] = (tmp[k - mesh.vertsX] + 2.0f * tmp[k]
                              + tmp[k + mesh.vertsX]) * 0.25f;
            }
        }
    }

    // 逐顶点光照：法线、凹谷遮蔽、明暗都在顶点上算一次，格子取四角平均。
    // 相邻格子共享顶点，颜色天生跨格连续 —— 平坡上不会再有网格状的明暗条纹
    // （逐格各自算光会有平直着色的块状条纹，旧版一直有个这毛病）。
    QVector<float> litV(vertCount, 1.0f);   ///< 顶点明暗（环境光 + 漫反射 + 遮蔽）
    QVector<float> shadeV(vertCount, 0.0f); ///< 顶点背光程度（0 受光 ~ 1 最背光，冷偏用）
    if (anyHeight)
    {
        const double occRange = std::max(1e-6, mesh.cellW * 1.6);
        for (int j = 0; j < mesh.vertsZ; ++j)
        {
            for (int i = 0; i < mesh.vertsX; ++i)
            {
                const int k = j * mesh.vertsX + i;
                // 中央差分法线（缺邻居用自身兜底，与 2D 山体阴影同一约定）
                const double hl = double(smoothH[i > 0 ? k - 1 : k]);
                const double hr = double(smoothH[i + 1 < mesh.vertsX ? k + 1 : k]);
                const double hu = double(smoothH[j > 0 ? k - mesh.vertsX : k]);
                const double hd = double(smoothH[j + 1 < mesh.vertsZ
                                                 ? k + mesh.vertsX : k]);
                const double dhx = (hr - hl) * 0.5;
                const double dhz = (hd - hu) * 0.5;
                // n = (-dhx, cellW, -dhz)，光照方向西北偏上（与 2D 地形图同侧）
                const double nlen = std::sqrt(dhx * dhx + mesh.cellW * mesh.cellW
                                              + dhz * dhz);
                const double nl = (0.48 * dhx + 0.78 * mesh.cellW + 0.40 * dhz)
                                  / std::max(1e-9, nlen * 0.9994);
                const double nlPos = std::max(0.0, nl);
                // 凹谷遮蔽：8 邻域普遍比自己高的凹处压暗一层，谷底有"陷进去"的层次
                double sum = 0.0;
                int n = 0;
                for (int dj = -1; dj <= 1; ++dj)
                {
                    const int j2 = j + dj;
                    if (j2 < 0 || j2 >= mesh.vertsZ)
                        continue;
                    for (int di = -1; di <= 1; ++di)
                    {
                        const int i2 = i + di;
                        if (i2 < 0 || i2 >= mesh.vertsX || (di == 0 && dj == 0))
                            continue;
                        sum += smoothH[j2 * mesh.vertsX + i2];
                        ++n;
                    }
                }
                double cav = 0.0;
                if (n > 0)
                    cav = std::clamp((sum / double(n) - double(smoothH[k]))
                                         / occRange, 0.0, 1.0);
                litV[k] = float(std::clamp(
                    (0.52 + 0.62 * nlPos) * (1.0 - 0.20 * cav), 0.40, 1.24));
                shadeV[k] = float(1.0 - nlPos);
            }
        }
    }

    // 空洞补面色：跟随主题的深色中性灰，读起来像"未探索的地底"
    const bool darkTheme = palette().color(QPalette::Window).lightness() < 128;
    const QRgb voidColor = darkTheme ? qRgb(0x2a, 0x30, 0x3a)
                                     : qRgb(0x8d, 0x94, 0x9d);

    float vyLo = std::numeric_limits<float>::max();
    float vyHi = std::numeric_limits<float>::lowest();
    bool anyCell = false;

    for (int j = 0; j < mesh.cellsZ; ++j)
    {
        for (int i = 0; i < mesh.cellsX; ++i)
        {
            const int v00 = j * mesh.vertsX + i;
            const int v10 = v00 + 1;
            const int v01 = v00 + mesh.vertsX;
            const int v11 = v01 + 1;
            if (anyHeight && (!filled[v00] || !filled[v10]
                              || !filled[v01] || !filled[v11]))
                continue; // 扩散也够不到的真空洞：留空透天空

            QRgb base = 0;
            quint8 flags = 0;
            const bool voidCell = anyHeight
                && (!mesh.valid[v00] || !mesh.valid[v10]
                    || !mesh.valid[v01] || !mesh.valid[v11]);
            if (voidCell)
            {
                base = voidColor;
                flags |= FlagVoid;
            }
            else
            {
                int waterVotes = 0, voteTotal = 0;
                base = sampleCellColor(*color, pixelX(i), pixelY(j),
                                       stride, w, h, &waterVotes, &voteTotal);
                if (base == 0)
                    continue; // 整格透明 / 无数据
                if (waterVotes * 2 > voteTotal)
                    flags |= FlagWater;
            }

            const int cell = j * mesh.cellsX + i;

            if (!voidCell && !(flags & FlagWater))
            {
                // 格子取四角顶点明暗的平均（跨格连续，无块状条纹）
                const double lit = (double(litV[v00]) + double(litV[v10])
                                    + double(litV[v01]) + double(litV[v11]))
                                   * 0.25;
                const double shade = (double(shadeV[v00]) + double(shadeV[v10])
                                      + double(shadeV[v01]) + double(shadeV[v11]))
                                     * 0.25;
                // 阴影带一点冷偏：阳光下偏暖、阴影里偏冷，画面的色彩层次更足
                mesh.cellColor[cell] =
                    qRgba(std::clamp(int(qRed(base) * lit * (1.0 - 0.05 * shade)), 0, 255),
                          std::clamp(int(qGreen(base) * lit), 0, 255),
                          std::clamp(int(qBlue(base) * lit * (1.0 + 0.06 * shade)), 0, 255),
                          255);
            }
            else
            {
                // 水面 / 空洞：保持干净平色（水的深浅明暗 2D 已带，
                // 海面不叠地形光照，与 2D Terrain 模式同一约定）
                mesh.cellColor[cell] = base | 0xff000000u;
            }
            mesh.cellFlags[cell] = flags;

            vyLo = std::min(vyLo, std::min(std::min(mesh.vy[v00], mesh.vy[v10]),
                                           std::min(mesh.vy[v01], mesh.vy[v11])));
            vyHi = std::max(vyHi, std::max(std::max(mesh.vy[v00], mesh.vy[v10]),
                                           std::max(mesh.vy[v01], mesh.vy[v11])));
            anyCell = true;
        }
    }

    // 岸线：紧挨陆地（非水、非空洞）的水面格给一圈泡沫
    for (int j = 0; j < mesh.cellsZ; ++j)
    {
        for (int i = 0; i < mesh.cellsX; ++i)
        {
            const int cell = j * mesh.cellsX + i;
            if (!(mesh.cellFlags[cell] & FlagWater))
                continue;
            static constexpr int kDx[4] = { -1, 1, 0, 0 };
            static constexpr int kDz[4] = { 0, 0, -1, 1 };
            for (int d = 0; d < 4; ++d)
            {
                const int ni = i + kDx[d];
                const int nj = j + kDz[d];
                if (ni < 0 || nj < 0 || ni >= mesh.cellsX || nj >= mesh.cellsZ)
                    continue;
                const quint8 nf = mesh.cellFlags[nj * mesh.cellsX + ni];
                if (nf != 0 && !(nf & (FlagWater | FlagVoid)))
                {
                    mesh.cellFlags[cell] |= FlagShore;
                    break;
                }
            }
        }
    }

    mesh.vyMin = anyCell ? vyLo : 0.0f;
    mesh.vyMax = anyCell ? vyHi : 0.0f;

    mesh.empty = false;
    mesh.anyHeight = anyHeight;
    mesh.minHeight = anyHeight ? m_hMin : 0;
    mesh.maxHeight = anyHeight ? m_hMax : 0;
    m_mesh = mesh;
    m_meshDirty = false;
}

// ═══════════════════════ 相机 ═══════════════════════

void WorldMap3DView::cameraSetup(Camera *cam) const
{
    const double yaw = qDegreesToRadians(m_yaw);
    const double pitch = qDegreesToRadians(
        std::clamp(m_pitch, kMinPitch, kMaxPitch));

    // 从视角中心指向相机的方向
    const double dx = std::cos(pitch) * std::sin(yaw);
    const double dy = std::sin(pitch);
    const double dz = std::cos(pitch) * std::cos(yaw);

    cam->eye = { m_tx + dx * m_dist, m_ty + dy * m_dist, m_tz + dz * m_dist };
    cam->fwd = { -dx, -dy, -dz };

    // right = normalize(fwd × worldUp)
    double rx = cam->fwd.y * 0.0 - cam->fwd.z * 1.0;
    double ry = cam->fwd.z * 0.0 - cam->fwd.x * 0.0;
    double rz = cam->fwd.x * 1.0 - cam->fwd.y * 0.0;
    const double rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rlen < 1e-6)
    {
        // 正俯视退化：随便给一个水平右向量
        rx = 1.0;
        ry = 0.0;
        rz = 0.0;
    }
    else
    {
        rx /= rlen;
        ry /= rlen;
        rz /= rlen;
    }
    cam->right = { rx, ry, rz };
    // up = right × fwd（已正交归一）
    cam->up = { ry * cam->fwd.z - rz * cam->fwd.y,
                rz * cam->fwd.x - rx * cam->fwd.z,
                rx * cam->fwd.y - ry * cam->fwd.x };

    const double fovy = qDegreesToRadians(kFovY);
    const double fh = double(std::max(1, framePixelSize().height()));
    cam->focal = (fh * 0.5) / std::tan(fovy * 0.5);
    cam->zNear = std::max(0.05, m_dist * 0.002);
}

bool WorldMap3DView::projectPoint(const Camera &cam, double x, double y, double z,
                                  QPointF *out, double *depth) const
{
    const double rx = x - cam.eye.x;
    const double ry = y - cam.eye.y;
    const double rz = z - cam.eye.z;
    const double cz = dot3(rx, ry, rz, cam.fwd.x, cam.fwd.y, cam.fwd.z);
    if (cz <= cam.zNear)
        return false;
    const double cx = dot3(rx, ry, rz, cam.right.x, cam.right.y, cam.right.z);
    const double cy = dot3(rx, ry, rz, cam.up.x, cam.up.y, cam.up.z);
    const QSize fs = framePixelSize();
    const double fw = double(std::max(1, fs.width())) * 0.5;
    const double fh = double(std::max(1, fs.height())) * 0.5;
    if (out)
        *out = QPointF(fw + cx * cam.focal / cz, fh - cy * cam.focal / cz);
    if (depth)
        *depth = cz;
    return true;
}

// ═══════════════════════ 视角操作 ═══════════════════════

void WorldMap3DView::fitToWorld()
{
    if (!hasSurface())
        return;

    const double blocksX = double(m_color->width()) * m_bpp;
    const double blocksZ = double(m_color->height()) * m_bpp;
    const double aspect = double(std::max(1, width()))
                          / double(std::max(1, height()));
    const double tanY = std::tan(qDegreesToRadians(kFovY) * 0.5);
    const double tanX = tanY * std::max(0.2, aspect);
    const double needX = blocksX * 0.5 / tanX;
    const double needZ = blocksZ * 0.5 / tanY;
    // 起伏占掉的竖直空间：高度跨度被归一化成"图幅（方块）的 kReliefRatio 倍"
    const double vExtent = double(std::max(m_color->width(),
                                           m_color->height()))
                           * m_bpp * kReliefRatio * m_relief;

    m_tx = 0.0;
    m_tz = 0.0;
    m_ty = 0.0;
    m_dist = std::max(needX, needZ) * 1.18 + vExtent;
    m_yaw = 35.0;
    m_pitch = 42.0;
    m_meshDirty = true;
    update();
    emit viewChanged();
}

void WorldMap3DView::resetView()
{
    fitToWorld();
}

void WorldMap3DView::zoomBy(double factor)
{
    if (factor <= 0.0)
        return;
    const double span = hasSurface()
        ? double(std::max(m_color->width(), m_color->height())) * m_bpp
        : 512.0;
    const double minDist = std::max(12.0, span * 0.015);
    const double maxDist = std::max(minDist * 2.0, span * 14.0);
    m_dist = std::clamp(m_dist * factor, minDist, maxDist);
    m_meshDirty = true;
    update();
    emit viewChanged();
}

void WorldMap3DView::orbitBy(double dyaw, double dpitch)
{
    m_yaw = std::fmod(m_yaw + dyaw, 360.0);
    if (m_yaw < 0.0)
        m_yaw += 360.0;
    m_pitch = std::clamp(m_pitch + dpitch, kMinPitch, kMaxPitch);
    m_meshDirty = true;
    update();
    emit viewChanged();
}

void WorldMap3DView::panByScreen(const QPoint &delta)
{
    if (delta.isNull())
        return;
    Camera cam;
    cameraSetup(&cam);
    // 屏幕像素 → 世界距离（在视角中心所在深度上换算）
    const double s = m_dist / std::max(1.0, cam.focal);
    // 竖直方向沿地面走：俯角越小，同样的屏幕位移对应的地面距离越大
    const double sinP = std::max(0.18, std::sin(qDegreesToRadians(m_pitch)));
    double gx = cam.fwd.x, gz = cam.fwd.z;
    const double glen = std::sqrt(gx * gx + gz * gz);
    if (glen > 1e-6)
    {
        gx /= glen;
        gz /= glen;
    }
    else
    {
        gx = 0.0;
        gz = -1.0;
    }
    const double dx = double(delta.x()) * s;
    const double dy = double(delta.y()) * s / sinP;
    m_tx -= cam.right.x * dx + gx * dy;
    m_tz -= cam.right.z * dx + gz * dy;
    update();
    emit viewChanged();
}

void WorldMap3DView::centerOnBlock(qint64 blockX, qint64 blockZ)
{
    if (!hasSurface())
        return;
    const double cxBlocks = double(m_color->width()) * m_bpp * 0.5;
    const double czBlocks = double(m_color->height()) * m_bpp * 0.5;
    const double bx = double(blockX) - double(m_minChunkX) * 16.0;
    const double bz = double(blockZ) - double(m_minChunkZ) * 16.0;
    m_tx = bx - cxBlocks;
    m_tz = bz - czBlocks;
    m_meshDirty = true;
    update();
    emit viewChanged();
}

QPointF WorldMap3DView::targetBlock() const
{
    if (!hasSurface())
        return QPointF(0, 0);
    const double cxBlocks = double(m_color->width()) * m_bpp * 0.5;
    const double czBlocks = double(m_color->height()) * m_bpp * 0.5;
    return QPointF(double(m_minChunkX) * 16.0 + m_tx + cxBlocks,
                   double(m_minChunkZ) * 16.0 + m_tz + czBlocks);
}

void WorldMap3DView::setReliefScale(double scale)
{
    m_relief = std::clamp(scale, 0.2, 4.0);
    m_meshDirty = true;
    update();
}

bool WorldMap3DView::projectBlock(qint64 blockX, qint64 blockZ, QPoint *out) const
{
    if (!hasSurface() || m_meshDirty || m_mesh.empty)
        return false;
    const double cxBlocks = double(m_color->width()) * m_bpp * 0.5;
    const double czBlocks = double(m_color->height()) * m_bpp * 0.5;
    const double wx = double(blockX) - double(m_minChunkX) * 16.0 - cxBlocks;
    const double wz = double(blockZ) - double(m_minChunkZ) * 16.0 - czBlocks;

    // 纵向位置按高度图的采样值复算（与 buildMesh 同一套归一化）
    double wy = 0.0;
    if (m_anyHeight && m_height)
    {
        const int px = int(std::lround((wx + cxBlocks) / m_bpp));
        const int py = int(std::lround((wz + czBlocks) / m_bpp));
        const qint16 v = heightValue(*m_height, px, py);
        if (v != std::numeric_limits<qint16>::min())
        {
            const double midH = (double(m_hMin) + double(m_hMax)) * 0.5;
            wy = (double(v) - midH) * verticalUnit();
        }
    }

    Camera cam;
    cameraSetup(&cam);
    QPointF sp;
    if (!projectPoint(cam, wx, wy, wz, &sp, nullptr))
        return false;
    if (out)
        *out = sp.toPoint();
    return true;
}

// ═══════════════════════ 渲染 ═══════════════════════

QSize WorldMap3DView::framePixelSize() const
{
    // 拖动中按 0.9 × DPR 渲染（跟手优先，运动中的轻微柔化看不出来）；
    // 静止帧按「DPR × 1.35」的精确尺寸渲染，显示时的平滑缩放正好完成
    // 降采样 —— 这就是抗锯齿本身。
    // （旧实现把 DPR 截到 1.5：高分屏发糊、标准屏有锯齿，两头不讨好）
    const double scale = m_fastMode
        ? std::clamp(devicePixelRatioF() * 0.9, 0.75, 1.2)
        : std::clamp(devicePixelRatioF() * 1.35, 1.0, 2.2);
    int fw = std::max(1, int(std::lround(width() * scale)));
    int fh = std::max(1, int(std::lround(height() * scale)));
    constexpr int kMaxSide = 3200; // 超大窗口兜底，别让帧缓冲失控
    const int longSide = std::max(fw, fh);
    if (longSide > kMaxSide)
    {
        const double k = double(kMaxSide) / double(longSide);
        fw = std::max(1, int(std::lround(fw * k)));
        fh = std::max(1, int(std::lround(fh * k)));
    }
    return QSize(fw, fh);
}

void WorldMap3DView::renderFrame()
{
    // 画面尺寸跟随控件（静止帧超采样、拖动帧原速，见 framePixelSize）
    const QSize want = framePixelSize();
    if (m_frame.size() != want)
    {
        m_frame = QImage(want, QImage::Format_ARGB32_Premultiplied);
        m_meshDirty = true; // 画面尺寸变了，取景要跟着重算
    }
    if (m_frame.isNull())
        return;

    if (m_meshDirty)
        buildMesh(m_fastMode);

    QElapsedTimer timer;
    timer.start();

    const int fw = m_frame.width();
    const int fh = m_frame.height();

    // 天空：顶 / 近地平线 / 底三段渐变，天际线更透气（雾色与这里同一组分段，
    // 远地形正好化进它背后的那片天）
    m_frame.fill(skyColorAt(1.0));
    {
        QPainter p(&m_frame);
        QLinearGradient g(0.0, 0.0, 0.0, double(fh));
        g.setColorAt(0.0, skyColorAt(0.0));
        g.setColorAt(0.58, skyColorAt(0.58));
        g.setColorAt(1.0, skyColorAt(1.0));
        p.fillRect(QRect(0, 0, fw, fh), g);
        p.end();
    }

    m_lastQuads = 0;
    if (m_mesh.empty)
    {
        m_lastFrameMs = int(timer.elapsed());
        return;
    }

    Camera cam;
    cameraSetup(&cam);

    QPainter p(&m_frame);
    // 逐边抗锯齿在软件光栅化上太烧（实测静态帧要贵 3 倍），关掉；
    // 静止帧的超采样缩放显示已经把锯齿抹得七七八八
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);

    const Mesh &mesh = m_mesh;

    // 空气透视：俯角越小雾收得越早（贴着地平线看过去就该是朦胧的），
    // 封顶放宽到 0.85，极远的地形能真正融进天际线
    const double sinP = std::clamp(-cam.fwd.y, 0.0, 1.0);
    const double fogStart = m_dist * (0.55 + 0.37 * sinP);
    const double fogSpan = std::max(1.0, m_dist * (3.0 - 1.1 * sinP));
    const double margin = 2.0;

    // 雾色按屏幕行预取成一张表：格子按质心所在行查表，
    // 远地形正好化进它背后那片天，还省掉了逐格的天空取色
    QVector<QRgb> fogLut(fh);
    for (int y = 0; y < fh; ++y)
        fogLut[y] = skyColorAt(double(y) / double(fh));

    // 水面反光混入的天空色（比地平线略偏天顶，海面泛天光不至于发白）
    const QRgb reflColor = blendFog(skyColorAt(0.78), skyColorAt(0.45), 0.35);

    // 裙边：地形边统一垂到一块"底板"高度，悬浮实心岛
    const float vyExtent = std::max(0.0f, mesh.vyMax - mesh.vyMin);
    const float skirtBase =
        mesh.vyMin - float(std::max(mesh.cellW * 2.0, double(vyExtent) * 0.12));

    // 网格铺的图像矩形（与 buildMesh 同一次取景，相机没动过，结果一致）
    int px0 = 0, py0 = 0, pw = 1, ph = 1;
    visiblePixelRect(&px0, &py0, &pw, &ph);
    const int imgW = m_color->width();
    const int imgH = m_color->height();

    const double fw2 = double(fw) * 0.5;
    const double fh2 = double(fh) * 0.5;

    // 由远及近：视线朝向决定了先画哪一端
    const bool xRev = cam.fwd.x > 0.0;
    const bool zRev = cam.fwd.z > 0.0;

    for (int jj = 0; jj < mesh.cellsZ; ++jj)
    {
        const int j = zRev ? (mesh.cellsZ - 1 - jj) : jj;
        for (int ii = 0; ii < mesh.cellsX; ++ii)
        {
            const int i = xRev ? (mesh.cellsX - 1 - ii) : ii;
            const int cell = j * mesh.cellsX + i;
            const QRgb cellColor = mesh.cellColor[cell];
            if (cellColor == 0)
                continue; // 无地表

            const quint8 flags = mesh.cellFlags[cell];
            const int v00 = j * mesh.vertsX + i;
            const int v10 = v00 + 1;
            const int v01 = v00 + mesh.vertsX;
            const int v11 = v01 + 1;

            // 四角 → 相机空间（贴脸的格子做近平面裁剪，不再整格跳过）
            double csX[4], csY[4], csZ[4];
            const int vi[4] = { v00, v10, v11, v01 };
            bool needClip = false;
            for (int k = 0; k < 4; ++k)
            {
                const double rx = double(mesh.vx[vi[k]]) - cam.eye.x;
                const double ry = double(mesh.vy[vi[k]]) - cam.eye.y;
                const double rz = double(mesh.vz[vi[k]]) - cam.eye.z;
                csZ[k] = dot3(rx, ry, rz, cam.fwd.x, cam.fwd.y, cam.fwd.z);
                csX[k] = dot3(rx, ry, rz, cam.right.x, cam.right.y, cam.right.z);
                csY[k] = dot3(rx, ry, rz, cam.up.x, cam.up.y, cam.up.z);
                if (csZ[k] <= cam.zNear)
                    needClip = true;
            }

            QPointF sp[8];
            int np = 0;
            double czAvg = 0.0;
            if (!needClip)
            {
                for (int k = 0; k < 4; ++k)
                {
                    sp[k] = QPointF(fw2 + csX[k] * cam.focal / csZ[k],
                                    fh2 - csY[k] * cam.focal / csZ[k]);
                    czAvg += csZ[k];
                }
                np = 4;
                czAvg *= 0.25;
            }
            else
            {
                // Sutherland–Hodgman：对 z = zNear 裁出可见部分再投影
                for (int k = 0; k < 4; ++k)
                {
                    const int a = k;
                    const int b = (k + 1) & 3;
                    const double da = csZ[a] - cam.zNear;
                    const double db = csZ[b] - cam.zNear;
                    if (da >= 0.0)
                    {
                        sp[np] = QPointF(fw2 + csX[a] * cam.focal / csZ[a],
                                         fh2 - csY[a] * cam.focal / csZ[a]);
                        czAvg += csZ[a];
                        ++np;
                    }
                    if ((da >= 0.0) != (db >= 0.0))
                    {
                        const double t = da / (da - db);
                        const double cxm = csX[a] + (csX[b] - csX[a]) * t;
                        const double cym = csY[a] + (csY[b] - csY[a]) * t;
                        const double czm = csZ[a] + (csZ[b] - csZ[a]) * t;
                        sp[np] = QPointF(fw2 + cxm * cam.focal / czm,
                                         fh2 - cym * cam.focal / czm);
                        czAvg += czm;
                        ++np;
                    }
                }
                if (np < 3)
                    continue;
                czAvg /= double(np);
            }

            // 屏幕裁剪
            double xmin = sp[0].x(), xmax = sp[0].x();
            double ymin = sp[0].y(), ymax = sp[0].y();
            for (int k = 1; k < np; ++k)
            {
                xmin = std::min(xmin, sp[k].x());
                xmax = std::max(xmax, sp[k].x());
                ymin = std::min(ymin, sp[k].y());
                ymax = std::max(ymax, sp[k].y());
            }
            if (xmax < -margin || ymax < -margin
                || xmin > fw + margin || ymin > fh + margin)
                continue;

            // 空气透视：雾色按格子质心落在屏幕的行查表
            const double fogT = std::clamp((czAvg - fogStart) / fogSpan,
                                           0.0, 0.85);
            double syCent = 0.0;
            for (int k = 0; k < np; ++k)
                syCent += sp[k].y();
            const int fogRow = std::clamp(int(syCent / double(np)),
                                          0, fh - 1);
            const QRgb fogColor = fogLut[fogRow];

            // ── 裙边：邻格没画（空洞 / 地图边界）的一侧垂一面墙，只画
            //    朝向相机的墙（背面剔除），地形读起来是一块实心岛 ──
            const QRgb wallBase = qRgb(int(qRed(cellColor) * 0.45),
                                       int(qGreen(cellColor) * 0.45),
                                       int(qBlue(cellColor) * 0.45));
            const QRgb wallColor = blendFog(wallBase, fogColor, fogT);
            auto sideNeedsWall = [&](int ni, int nj, bool mapBorder) {
                if (ni >= 0 && nj >= 0 && ni < mesh.cellsX && nj < mesh.cellsZ)
                    return mesh.cellColor[nj * mesh.cellsX + ni] == 0;
                return mapBorder; // 网格外：只有贴到图像边缘才是真的地图边界
            };
            auto drawWall = [&](const QPointF &sa, const QPointF &sb,
                                int va, int vb, double ox, double oz) {
                // 背面剔除：相机在墙的外侧才看得见这面墙
                const double mx = (double(mesh.vx[va]) + double(mesh.vx[vb])) * 0.5
                                  - cam.eye.x;
                const double mz = (double(mesh.vz[va]) + double(mesh.vz[vb])) * 0.5
                                  - cam.eye.z;
                if (ox * mx + oz * mz <= 0.0)
                    return;
                QPointF ba, bb;
                if (!projectPoint(cam, mesh.vx[va], skirtBase, mesh.vz[va], &ba, nullptr)
                    || !projectPoint(cam, mesh.vx[vb], skirtBase, mesh.vz[vb], &bb, nullptr))
                    return;
                QPolygonF wall;
                wall << sa << sb << bb << ba;
                p.setBrush(QColor::fromRgb(wallColor));
                p.drawPolygon(wall);
            };
            if (!needClip)
            {
                if (sideNeedsWall(i - 1, j, px0 <= 0))
                    drawWall(sp[0], sp[3], v00, v01, -1.0, 0.0);
                if (sideNeedsWall(i + 1, j, px0 + pw >= imgW))
                    drawWall(sp[1], sp[2], v10, v11, 1.0, 0.0);
                if (sideNeedsWall(i, j - 1, py0 <= 0))
                    drawWall(sp[0], sp[1], v00, v10, 0.0, -1.0);
                if (sideNeedsWall(i, j + 1, py0 + ph >= imgH))
                    drawWall(sp[3], sp[2], v01, v11, 0.0, 1.0);
            }

            // ── 水面：天空反光（视线越斜越亮）+ 岸线泡沫 + 细碎波光 ──
            QRgb finalColor = cellColor;
            if (flags & FlagWater)
            {
                const double rcx =
                    (double(mesh.vx[v00]) + double(mesh.vx[v10])
                     + double(mesh.vx[v11]) + double(mesh.vx[v01])) * 0.25 - cam.eye.x;
                const double rcy =
                    (double(mesh.vy[v00]) + double(mesh.vy[v10])
                     + double(mesh.vy[v11]) + double(mesh.vy[v01])) * 0.25 - cam.eye.y;
                const double rcz =
                    (double(mesh.vz[v00]) + double(mesh.vz[v10])
                     + double(mesh.vz[v11]) + double(mesh.vz[v01])) * 0.25 - cam.eye.z;
                const double rlen = std::sqrt(rcx * rcx + rcy * rcy + rcz * rcz);
                // 视线与水面法线（竖直向上）的夹角余弦：俯视≈1，斜望≈0
                const double cosT = rlen > 1e-9
                    ? std::clamp(-rcy / rlen, 0.0, 1.0) : 0.0;
                const double t = std::clamp(
                    0.08 + 0.55 * std::pow(1.0 - cosT, 2.2), 0.0, 0.55);
                finalColor = blendFog(finalColor, reflColor, t);
                if (flags & FlagShore)
                    finalColor = blendFog(finalColor, qRgb(255, 255, 255), 0.18);
                // 静态的细碎波光：按格坐标哈希给一点亮度扰动，海面不再死平
                const quint32 hsh = (quint32(i) * 73856093u)
                                    ^ (quint32(j) * 19349663u);
                const double spark =
                    0.97 + 0.06 * double((hsh >> 7) & 255u) / 255.0;
                finalColor = qRgba(
                    std::clamp(int(qRed(finalColor) * spark), 0, 255),
                    std::clamp(int(qGreen(finalColor) * spark), 0, 255),
                    std::clamp(int(qBlue(finalColor) * spark), 0, 255), 255);
            }

            // 向外扩半个像素：相邻格共用顶点，不留白缝（开 AA 时也靠它防晕边）
            double ccx = 0.0, ccy = 0.0;
            for (int k = 0; k < np; ++k)
            {
                ccx += sp[k].x();
                ccy += sp[k].y();
            }
            ccx /= double(np);
            ccy /= double(np);
            QPolygonF poly;
            poly.resize(np);
            for (int k = 0; k < np; ++k)
            {
                const double vx = sp[k].x() - ccx;
                const double vy = sp[k].y() - ccy;
                const double vlen = std::sqrt(vx * vx + vy * vy);
                poly[k] = vlen < 1e-6
                    ? sp[k]
                    : QPointF(ccx + vx * (1.0 + 0.55 / vlen),
                              ccy + vy * (1.0 + 0.55 / vlen));
            }

            p.setBrush(QColor::fromRgb(blendFog(finalColor, fogColor, fogT)));
            p.drawPolygon(poly);
            ++m_lastQuads;
        }
    }
    p.end();

    m_lastFrameMs = int(timer.elapsed());
}

void WorldMap3DView::scheduleRefine()
{
    if (!m_fastMode)
    {
        m_fastMode = true;
        m_meshDirty = true;
    }
    if (m_refineTimer)
        m_refineTimer->start();
}

// ═══════════════════════ 配色 ═══════════════════════

QColor WorldMap3DView::cardColor() const
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    return dark ? QColor(0x1a, 0x1a, 0x1a) : QColor(0xf5, 0xf5, 0xf5);
}

QColor WorldMap3DView::borderColor() const
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    return dark ? QColor(0x3a, 0x3a, 0x3a) : QColor(0xe8, 0xea, 0xed);
}

/** 天空渐变在屏幕纵向 t（0 = 顶，1 = 底）处的颜色；雾色按它取，远地形正好化进天空 */
QRgb WorldMap3DView::skyColorAt(double t) const
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor top = dark ? QColor(0x0d, 0x12, 0x1c) : QColor(0xb7, 0xd3, 0xf2);
    const QColor bottom = dark ? QColor(0x1b, 0x22, 0x30) : QColor(0xe6, 0xef, 0xfa);
    // 近地平线一档亮色：亮主题是薄雾般的淡青白，暗主题是略亮一档的蓝灰，
    // 天际线因此有一层"地平线辉光"，不再是一条生硬的直线
    const QColor horizon = dark ? QColor(0x27, 0x33, 0x47) : QColor(0xdc, 0xe8, 0xf7);
    t = std::clamp(t, 0.0, 1.0);
    if (t < 0.58)
    {
        const double k = t / 0.58;
        return qRgb(int(top.red() + (horizon.red() - top.red()) * k),
                    int(top.green() + (horizon.green() - top.green()) * k),
                    int(top.blue() + (horizon.blue() - top.blue()) * k));
    }
    const double k = (t - 0.58) / 0.42;
    return qRgb(int(horizon.red() + (bottom.red() - horizon.red()) * k),
                int(horizon.green() + (bottom.green() - horizon.green()) * k),
                int(horizon.blue() + (bottom.blue() - horizon.blue()) * k));
}

// ═══════════════════════ 绘制 ═══════════════════════

void WorldMap3DView::paintEvent(QPaintEvent *)
{
    // 视角、数据、控件尺寸任一变化都置了 m_meshDirty，这里统一按需重渲染
    if (m_meshDirty || m_frame.isNull() || m_frame.size() != framePixelSize())
        renderFrame();

    const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath clip;
    clip.addRoundedRect(outer, kCardRadius, kCardRadius);
    p.setClipPath(clip);
    p.fillPath(clip, cardColor());

    if (!hasSurface())
    {
        p.setPen(palette().color(QPalette::Mid));
        p.drawText(rect(), Qt::AlignCenter, tr("还没有可显示的立体地图"));
    }
    else
    {
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(rect(), m_frame);

        // ── 左下角操作提示：只在静止时显示，拖动中不晃眼 ──
        const QString hint = tr("拖拽旋转 · 滚轮缩放 · 右键拖拽平移 · Ctrl+滚轮调起伏");
        QFont f = font();
        f.setPointSizeF(std::max(7.5, f.pointSizeF() - 1.0));
        p.setFont(f);
        const QFontMetrics fm(f);
        const QRect tr1 = fm.boundingRect(hint);
        const QRectF box(12, height() - 12 - tr1.height() - 8,
                         tr1.width() + 18, tr1.height() + 8);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 86));
        p.drawRoundedRect(box, 7, 7);
        p.setPen(QColor(255, 255, 255, 214));
        p.drawText(box, Qt::AlignCenter, hint);

        // ── 右上角方位标：四个方向跟着视角转，红点是北（世界 -Z） ──
        Camera cam;
        cameraSetup(&cam);
        const QPointF cc(width() - 36.0, 36.0);
        const double rad = 23.0;
        p.setPen(QPen(QColor(255, 255, 255, 110), 1.0));
        p.setBrush(QColor(0, 0, 0, 62));
        p.drawEllipse(cc, rad, rad);

        QFont nf = f;
        nf.setBold(true);
        nf.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
        p.setFont(nf);
        // 世界四向（北 -Z / 东 +X / 南 +Z / 西 -X）投到屏幕上的方向
        const double dirs[4][2] = { { 0.0, -1.0 }, { 1.0, 0.0 },
                                    { 0.0, 1.0 }, { -1.0, 0.0 } };
        const char *names[4] = { "N", "E", "S", "W" };
        for (int k = 0; k < 4; ++k)
        {
            const double sx = dirs[k][0] * cam.right.x + dirs[k][1] * cam.right.z;
            const double sy = -(dirs[k][0] * cam.up.x + dirs[k][1] * cam.up.z);
            const double len = std::sqrt(sx * sx + sy * sy);
            if (len < 1e-4)
                continue; // 正俯视时东西方向会退化成一点
            const QPointF dir(sx / len, sy / len);
            const QPointF at = cc + dir * (rad - 7.5);
            const bool north = (k == 0);
            p.setPen(north ? QColor(0xff, 0x6d, 0x5a) : QColor(255, 255, 255, 205));
            p.drawText(QRectF(at.x() - 5.0, at.y() - 5.0, 10.0, 10.0),
                       Qt::AlignCenter,
                       QString::fromLatin1(names[k]));
            if (north)
            {
                // 北方再补一个小箭头，方向一眼就看得出来
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0xff, 0x6d, 0x5a, 220));
                QPolygonF arrow;
                arrow << (cc + dir * (rad - 13.0))
                      << (cc + dir * (rad - 18.0)
                          + QPointF(-dir.y(), dir.x()) * 2.8)
                      << (cc + dir * (rad - 18.0)
                          - QPointF(-dir.y(), dir.x()) * 2.8);
                p.drawPolygon(arrow);
            }
        }
        p.setFont(f);
    }

    // 卡片描边
    p.setClipping(false);
    p.setPen(QPen(borderColor(), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(outer, kCardRadius, kCardRadius);
}

void WorldMap3DView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_meshDirty = true;
    update();
}

// ═══════════════════════ 交互 ═══════════════════════

void WorldMap3DView::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    m_pressPos = event->pos();
    m_lastPos = event->pos();
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton
        || event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WorldMap3DView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging && !m_panning)
    {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint delta = event->pos() - m_lastPos;
    m_lastPos = event->pos();

    if (m_dragging)
    {
        scheduleRefine();
        // 水平 0.35°/px、垂直 0.3°/px：转一圈大约 1000px，跟手不飘
        orbitBy(-double(delta.x()) * 0.35, double(delta.y()) * 0.30);
    }
    else if (m_panning)
    {
        scheduleRefine();
        panByScreen(delta);
    }
    event->accept();
}

void WorldMap3DView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    if (m_panning && (event->button() == Qt::RightButton
                      || event->button() == Qt::MiddleButton))
    {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void WorldMap3DView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        fitToWorld();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void WorldMap3DView::wheelEvent(QWheelEvent *event)
{
    const int dy = event->angleDelta().y();
    if (dy == 0)
    {
        event->ignore();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier)
    {
        // Ctrl + 滚轮：调整起伏（山更高 / 更平）
        setReliefScale(m_relief * (dy > 0 ? 1.12 : 0.89));
    }
    else
    {
        scheduleRefine();
        zoomBy(dy > 0 ? 0.87 : 1.0 / 0.87);
    }
    event->accept();
}

void WorldMap3DView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomBy(0.85);
        event->accept();
        return;
    case Qt::Key_Minus:
        zoomBy(1.0 / 0.85);
        event->accept();
        return;
    case Qt::Key_0:
        fitToWorld();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_A:
        panByScreen(QPoint(-40, 0));
        event->accept();
        return;
    case Qt::Key_Right:
    case Qt::Key_D:
        panByScreen(QPoint(40, 0));
        event->accept();
        return;
    case Qt::Key_Up:
    case Qt::Key_W:
        panByScreen(QPoint(0, -40));
        event->accept();
        return;
    case Qt::Key_Down:
    case Qt::Key_S:
        panByScreen(QPoint(0, 40));
        event->accept();
        return;
    case Qt::Key_BracketLeft:
        setReliefScale(m_relief * 0.88);
        event->accept();
        return;
    case Qt::Key_BracketRight:
        setReliefScale(m_relief * 1.14);
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}
