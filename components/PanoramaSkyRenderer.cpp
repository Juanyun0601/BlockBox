/**
 * @file   PanoramaSkyRenderer.cpp
 * @brief  旋转全景背景的软件渲染器实现
 * @author BlockBox Team
 */
#include "components/PanoramaSkyRenderer.h"

#include "components/BackgroundWidget.h"

#include <QElapsedTimer>
#include <QImage>
#include <QtMath>
#include <cmath>
#include <utility>

namespace {

// 与原先的 GL 实现保持一致：竖直视角 75°，
// 自转角速度 = 0.005 × 速度倍率 × dt(毫秒) × 0.06，dt 上限 50ms
constexpr qreal kFovDegrees = 75.0;
constexpr qreal kRotationSpeed = 0.005;
constexpr qreal kRotationUnit = 0.06;
constexpr qint64 kMaxDeltaMs = 50;

// 画面只会用到竖直方向 ±37.5°，环带多留一点余量即可，不必生成完整 180°
constexpr qreal kBandLatSpanDeg = 100.0;
constexpr int kAsinTableSize = 4096;

// MC 全景贴图（panorama_0..5）到面序 [+X,-X,+Y,-Y,+Z,-Z] 的映射
const int kSceneFaceTiles[6] = { 1, 3, 4, 5, 0, 2 };

// 各面方向基：d = normalize(D + U·s + V·t)，s 向右、t 向上
struct FaceBasis {
    float dx, dy, dz;
    float ux, uy, uz;
    float vx, vy, vz;
};

const FaceBasis kFaceBases[6] = {
    { 1, 0, 0,   0, 0,-1,   0, 1, 0},   // +X
    {-1, 0, 0,   0, 0, 1,   0, 1, 0},   // -X
    { 0, 1, 0,   1, 0, 0,   0, 0,-1},   // +Y
    { 0,-1, 0,   1, 0, 0,   0, 0, 1},   // -Y
    { 0, 0, 1,   1, 0, 0,   0, 1, 0},   // +Z
    { 0, 0,-1,  -1, 0, 0,   0, 1, 0},   // -Z
};

inline QRgb bilinearClamped(const QImage &img, float fx, float fy)
{
    const int w = img.width();
    const int h = img.height();
    if (fx < 0.0f) fx = 0.0f;
    if (fy < 0.0f) fy = 0.0f;
    if (fx > float(w - 1)) fx = float(w - 1);
    if (fy > float(h - 1)) fy = float(h - 1);

    const int x0 = int(fx);
    const int y0 = int(fy);
    const int x1 = qMin(x0 + 1, w - 1);
    const int y1 = qMin(y0 + 1, h - 1);
    const float tx = fx - x0;
    const float ty = fy - y0;

    const QRgb *row0 = reinterpret_cast<const QRgb*>(img.constScanLine(y0));
    const QRgb *row1 = reinterpret_cast<const QRgb*>(img.constScanLine(y1));
    const QRgb p00 = row0[x0], p10 = row0[x1];
    const QRgb p01 = row1[x0], p11 = row1[x1];

    const float w00 = (1 - tx) * (1 - ty);
    const float w10 = tx * (1 - ty);
    const float w01 = (1 - tx) * ty;
    const float w11 = tx * ty;

    auto mix = [w00, w10, w01, w11](int a, int b, int c, int d) -> int {
        return int(w00 * a + w10 * b + w01 * c + w11 * d + 0.5f);
    };
    return qRgba(mix(qRed(p00), qRed(p10), qRed(p01), qRed(p11)),
                 mix(qGreen(p00), qGreen(p10), qGreen(p01), qGreen(p11)),
                 mix(qBlue(p00), qBlue(p10), qBlue(p01), qBlue(p11)),
                 255);
}

// 水平方向环绕采样：等距柱状全景图左右两条边在 360° 接缝处是连续的
inline QRgb bilinearWrappedX(const QImage &img, float fx, float fy)
{
    const int w = img.width();
    const int h = img.height();
    if (h <= 0 || w <= 0)
        return qRgb(0, 0, 0);
    if (fy < 0.0f) fy = 0.0f;
    if (fy > float(h - 1)) fy = float(h - 1);
    fx = fx - std::floor(fx / w) * w;

    const int x0 = int(fx) % w;
    const int y0 = int(fy);
    const int x1 = (x0 + 1) % w;
    const int y1 = qMin(y0 + 1, h - 1);
    const float tx = fx - int(fx);
    const float ty = fy - y0;

    const QRgb *row0 = reinterpret_cast<const QRgb*>(img.constScanLine(y0));
    const QRgb *row1 = reinterpret_cast<const QRgb*>(img.constScanLine(y1));
    const QRgb p00 = row0[x0], p10 = row0[x1];
    const QRgb p01 = row1[x0], p11 = row1[x1];

    const float w00 = (1 - tx) * (1 - ty);
    const float w10 = tx * (1 - ty);
    const float w01 = (1 - tx) * ty;
    const float w11 = tx * ty;

    auto mix = [w00, w10, w01, w11](int a, int b, int c, int d) -> int {
        return int(w00 * a + w10 * b + w01 * c + w11 * d + 0.5f);
    };
    return qRgba(mix(qRed(p00), qRed(p10), qRed(p01), qRed(p11)),
                 mix(qGreen(p00), qGreen(p10), qGreen(p01), qGreen(p11)),
                 mix(qBlue(p00), qBlue(p10), qBlue(p01), qBlue(p11)),
                 255);
}

// asin 查表：每帧要算几十万次，查表比直接调 asin 快一个量级
const float *asinLookup()
{
    static QVector<float> table;
    if (table.isEmpty()) {
        table.resize(kAsinTableSize);
        for (int i = 0; i < kAsinTableSize; ++i) {
            const double u = double(i) / double(kAsinTableSize - 1);
            table[i] = float(std::asin(u));
        }
    }
    return table.constData();
}

inline QRgb sampleCube(const QVector<QImage> &faces, float dx, float dy, float dz)
{
    const float ax = qAbs(dx);
    const float ay = qAbs(dy);
    const float az = qAbs(dz);
    int face;
    if (ax >= ay && ax >= az)
        face = dx > 0.0f ? 0 : 1;
    else if (ay >= az)
        face = dy > 0.0f ? 2 : 3;
    else
        face = dz > 0.0f ? 4 : 5;

    if (face >= faces.size())
        return qRgb(0, 0, 0);
    const QImage &img = faces.at(face);
    const FaceBasis &basis = kFaceBases[face];

    const float denom = dx * basis.dx + dy * basis.dy + dz * basis.dz;
    const float inv = (denom > 1e-6f) ? (1.0f / denom) : 1.0f;
    const float s = (dx * basis.ux + dy * basis.uy + dz * basis.uz) * inv;
    const float t = (dx * basis.vx + dy * basis.vy + dz * basis.vz) * inv;

    const float fx = (s + 1.0f) * 0.5f * float(img.width()) - 0.5f;
    const float fy = (1.0f - t) * 0.5f * float(img.height()) - 0.5f;
    return bilinearClamped(img, fx, fy);
}

inline QRgb sampleEquirect(const QImage &img, float lon, float lat)
{
    const float fx = (lon / float(M_PI) * 0.5f + 0.5f) * float(img.width());
    const float fy = (0.5f - lat / float(M_PI)) * float(img.height());
    return bilinearWrappedX(img, fx, fy);
}

} // namespace

PanoramaSkyRenderer::PanoramaSkyRenderer() = default;

QString PanoramaSkyRenderer::defaultSceneName()
{
    return QStringLiteral("overworld");
}

void PanoramaSkyRenderer::setScene(const QString &name)
{
    // 场景没变且已经成功生成过环带，不必重做（重复调用很常见）
    if (m_scene == name && m_equirectRaw.isNull() && !m_band.isNull())
        return;
    m_scene = name;
    m_equirectRaw = QImage();
    reloadSource();
}

void PanoramaSkyRenderer::setPanorama(const QImage &equirect)
{
    if (equirect.isNull())
        return;
    m_scene.clear();
    m_equirectRaw = equirect;
    reloadSource();
}

void PanoramaSkyRenderer::setBlurRadius(int radius)
{
    radius = qBound(0, radius, 64);
    if (m_blurRadius == radius)
        return;
    m_blurRadius = radius;
    // 模糊作用在源素材上（与原先 GL 版本的面贴图模糊等价），源图已有则直接重建
    reloadSource();
}

void PanoramaSkyRenderer::setRotationSpeedScale(qreal scale)
{
    m_speedScale = qBound<qreal>(0.0, scale, 4.0);
}

void PanoramaSkyRenderer::setRenderLimit(int longSide)
{
    m_renderLimit = qMax(320, longSide);
    m_frameValid = false;
}

void PanoramaSkyRenderer::reloadSource()
{
    m_faces.clear();
    m_band = QImage();
    m_frameValid = false;
    m_sourceDirty = true;
    // 素材加载本身不贵（读图 + 六面转换），直接同步做完，避免首帧空白
    ensureBand();
}

bool PanoramaSkyRenderer::advance(qint64 deltaMs)
{
    if (m_speedScale <= 0.0)
        return false;
    deltaMs = qBound<qint64>(0, deltaMs, kMaxDeltaMs);
    if (deltaMs <= 0)
        return false;

    m_rotation += kRotationSpeed * m_speedScale * qreal(deltaMs) * kRotationUnit;
    const qreal full = 2.0 * M_PI;
    if (m_rotation >= full || m_rotation <= -full)
        m_rotation = std::fmod(m_rotation, full);
    return true;
}

void PanoramaSkyRenderer::ensureBand()
{
    if (!m_sourceDirty && !m_band.isNull())
        return;

    m_sourceDirty = false;

    // 最多尝试两次：当前场景没有可用素材时回退到默认场景再试一次
    for (int attempt = 0; attempt < 2; ++attempt) {
        QVector<QImage> raw;
        if (!m_scene.isEmpty()) {
            raw.reserve(6);
            for (int face = 0; face < 6; ++face) {
                const QString path = QStringLiteral(":/Images/Panorama/%1/panorama_%2.jpg")
                                         .arg(m_scene, QString::number(kSceneFaceTiles[face]));
                QImage tile(path);
                if (tile.isNull()) {
                    raw.clear();  // 任一面缺失则整组放弃
                    break;
                }
                raw.append(tile);
            }
            if (raw.isEmpty() && m_equirectRaw.isNull()) {
                // 单图场景：目录里只有一张 2:1 等距全景图（如实景素材）
                QImage eq(QStringLiteral(":/Images/Panorama/%1/equirect.jpg").arg(m_scene));
                if (!eq.isNull())
                    m_equirectRaw = eq;
            }
        }

        if (!raw.isEmpty()) {
            m_faces.clear();
            m_faces.reserve(6);
            for (const QImage &face : std::as_const(raw)) {
                QImage img = face.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                if (m_blurRadius > 0)
                    img = BackgroundWidget::blurImage(img, m_blurRadius);
                m_faces.append(img);
            }
            if (m_faces.size() != 6)
                m_faces.clear();
        }

        if (!m_faces.isEmpty() || !m_equirectRaw.isNull())
            break;

        // 场景名失效（旧版本残留或资源缺失）：回退到默认场景，避免没有任何素材
        if (m_scene.isEmpty() || m_scene == defaultSceneName())
            break;
        m_scene = defaultSceneName();
    }

    m_equirect = QImage();
    if (m_faces.isEmpty() && !m_equirectRaw.isNull()) {
        QImage src = m_equirectRaw.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        // 限制源图尺寸，控制转换耗时与内存占用
        if (src.width() > 8192)
            src = src.scaled(8192, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (m_blurRadius > 0)
            src = BackgroundWidget::blurImage(src, m_blurRadius);
        m_equirect = src;
    }
    buildBand();
}

void PanoramaSkyRenderer::buildBand()
{
    m_band = QImage();

    // 环带尺寸：横向按 360° 展开，纵向只覆盖要显示的纬度范围。
    // 4410 像素/360° ≈ 12 px/°，与 1024² 的立方体面贴图分辨率相当。
    int bandWidth = 0;
    if (!m_faces.isEmpty())
        bandWidth = 2560;
    else if (!m_equirect.isNull())
        bandWidth = qBound(1024, m_equirect.width() * 3 / 2, 4096);
    if (bandWidth <= 0)
        return;

    const int bandHeight = qMax(64, int(std::lround(bandWidth * kBandLatSpanDeg / 360.0)));
    QImage band(bandWidth, bandHeight, QImage::Format_RGB32);

    const float latTop = float(qDegreesToRadians(kBandLatSpanDeg * 0.5));
    const float latBottom = -latTop;
    const float invW = 1.0f / float(bandWidth);
    const float invH = 1.0f / float(bandHeight);

    for (int r = 0; r < bandHeight; ++r) {
        // 行 0 对应纬度最高处
        const float lat = latTop - (float(r) + 0.5f) * invH * (latTop - latBottom);
        const float sinLat = std::sin(lat);
        const float cosLat = std::cos(lat);
        QRgb *dst = reinterpret_cast<QRgb*>(band.scanLine(r));
        for (int c = 0; c < bandWidth; ++c) {
            const float lon = -float(M_PI) + (float(c) + 0.5f) * invW * float(2.0 * M_PI);
            const float dx = std::sin(lon) * cosLat;
            const float dz = -std::cos(lon) * cosLat;
            if (!m_faces.isEmpty())
                dst[c] = sampleCube(m_faces, dx, sinLat, dz);
            else
                dst[c] = sampleEquirect(m_equirect, lon, lat);
        }
    }
    m_band = band;
    ++m_bandGeneration;
}

void PanoramaSkyRenderer::buildSampleTable(const QSize &size)
{
    const int W = size.width();
    const int H = size.height();
    const int bandW = m_band.width();
    const int bandH = m_band.height();
    if (W <= 0 || H <= 0 || bandW <= 0 || bandH <= 0)
        return;

    m_sampleTable.resize(W * H);
    m_sampleSize = size;
    m_sampleBandGeneration = m_bandGeneration;

    // 上排最多取到 bandH - 2，这样下排一定存在，取样时不必再判断边界
    const int lastTopRow = qMax(0, bandH - 2);
    const float latTop = float(qDegreesToRadians(kBandLatSpanDeg * 0.5));
    const float latSpan = latTop * 2.0f;
    const float rowScale = float(bandH) / latSpan;
    const float aspect = float(W) / float(H);
    const float tanV = float(std::tan(qDegreesToRadians(kFovDegrees * 0.5)));
    const float *asinTable = asinLookup();

    quint32 *table = m_sampleTable.data();
    for (int y = 0; y < H; ++y) {
        const float ndcY = 1.0f - 2.0f * (float(y) + 0.5f) / float(H);
        const float b = ndcY * tanV;
        const float b2 = b * b;
        quint32 *row = table + y * W;

        for (int x = 0; x < W; ++x) {
            const float ndc = 2.0f * (float(x) + 0.5f) / float(W) - 1.0f;
            const float a = ndc * tanV * aspect;
            const float len = std::sqrt(a * a + b2 + 1.0f);
            const float sinLat = b / len;

            const float uAbs = sinLat < 0.0f ? -sinLat : sinLat;
            int k = int(uAbs * float(kAsinTableSize - 1));
            if (k < 0)
                k = 0;
            if (k >= kAsinTableSize)
                k = kAsinTableSize - 1;
            float lat = asinTable[k];
            if (sinLat < 0.0f)
                lat = -lat;

            // 纬度 → 环带行（行 0 在最上方），rf 允许落在两行之间
            float rf = (latTop - lat) * rowScale - 0.5f;
            if (rf < 0.0f)
                rf = 0.0f;
            else if (rf > float(lastTopRow))
                rf = float(lastTopRow);

            int r0 = int(rf);
            if (r0 < 0)
                r0 = 0;
            else if (r0 > lastTopRow)
                r0 = lastTopRow;

            int wgt = int((rf - float(r0)) * 255.0f + 0.5f);
            if (wgt < 0)
                wgt = 0;
            else if (wgt > 255)
                wgt = 255;

            row[x] = (quint32(r0 * bandW) << 8) | quint32(wgt);
        }
    }
}

namespace {

// 上下两排像素按权重混合（0 = 上排，256 = 下排），走两段 16 位并行运算
inline QRgb blendRows(QRgb top, QRgb bottom, int weight)
{
    const quint32 wt = quint32(weight);
    const quint32 wb = 256u - wt;
    const quint32 rb = ((((top & 0x00FF00FFu) * wb) >> 8)
                        + (((bottom & 0x00FF00FFu) * wt) >> 8)) & 0x00FF00FFu;
    const quint32 g = ((((top & 0x0000FF00u) * wb) >> 8)
                       + (((bottom & 0x0000FF00u) * wt) >> 8)) & 0x0000FF00u;
    return QRgb(rb | g);
}

} // namespace

const QImage &PanoramaSkyRenderer::render(const QSize &size)
{
    ensureBand();

    if (m_band.isNull() || size.width() <= 0 || size.height() <= 0) {
        m_frame = QImage();
        m_frameValid = false;
        return m_frame;
    }

    // 超出素材分辨率再往上渲染没有意义，只会白烧 CPU：按最长边限幅
    QSize target = size;
    const int longSide = qMax(target.width(), target.height());
    if (longSide > m_renderLimit) {
        const qreal ratio = qreal(m_renderLimit) / qreal(longSide);
        target = QSize(qMax(1, qRound(target.width() * ratio)),
                       qMax(1, qRound(target.height() * ratio)));
    }

    if (m_frameValid && m_frameSize == target
        && qFuzzyCompare(m_frameRotation + 2.0, m_rotation + 2.0)) {
        return m_frame;
    }

    QElapsedTimer costTimer;
    costTimer.start();

    const int W = target.width();
    const int H = target.height();

    // 采样行只跟像素位置有关，跟自转角无关：尺寸或素材变了才重建
    if (m_sampleSize != target || m_sampleBandGeneration != m_bandGeneration
        || m_sampleTable.size() != W * H) {
        buildSampleTable(target);
    }

    if (m_frame.size() != target || m_frame.format() != QImage::Format_RGB32)
        m_frame = QImage(W, H, QImage::Format_RGB32);

    const int bandW = m_band.width();
    const int bandStride = m_band.bytesPerLine() / int(sizeof(QRgb));

    const float aspect = float(W) / float(H);
    const float tanV = float(std::tan(qDegreesToRadians(kFovDegrees * 0.5)));
    const float cosT = float(std::cos(m_rotation));
    const float sinT = float(std::sin(m_rotation));

    // 每列：视线方向绕竖直轴转过当前角度后的经度 → 环带列索引
    QVector<int> colIndex(W);
    for (int x = 0; x < W; ++x) {
        const float ndc = 2.0f * (float(x) + 0.5f) / float(W) - 1.0f;
        const float a = ndc * tanV * aspect;
        const float dx = a * cosT + sinT;
        const float dz = a * sinT - cosT;
        const float lon = std::atan2(dx, -dz);
        int c = int((lon + float(M_PI)) * (0.5f / float(M_PI)) * float(bandW));
        if (c < 0)
            c = 0;
        if (c >= bandW)
            c = bandW - 1;
        colIndex[x] = c;
    }

    const QRgb *bandPixels = reinterpret_cast<const QRgb*>(m_band.constBits());
    const quint32 *table = m_sampleTable.constData();
    const quint32 stride = quint32(bandStride);
    const int *cols = colIndex.constData();

    for (int y = 0; y < H; ++y) {
        const quint32 *tableRow = table + y * W;
        QRgb *dst = reinterpret_cast<QRgb*>(m_frame.scanLine(y));

        for (int x = 0; x < W; ++x) {
            const quint32 packed = tableRow[x];
            const quint32 offset = (packed >> 8) + quint32(cols[x]);
            const QRgb top = bandPixels[offset];
            const QRgb bottom = bandPixels[offset + stride];
            dst[x] = blendRows(top, bottom, int(packed & 0xFFu));
        }
    }

    m_frameSize = target;
    m_frameRotation = m_rotation;
    m_frameValid = true;
    m_lastRenderCostMs = int(costTimer.elapsed());
    return m_frame;
}
