/**
 * @file   PanoramaSkyWidget.cpp
 * @brief  旋转全景背景（3D 天空盒）控件实现，移植自 VersePc2（GPL-3.0）
 * @author BlockBox Team
 */
#include "components/PanoramaSkyWidget.h"

#include "components/BackgroundWidget.h"
#include "components/GlslCompat.h"

#include <QMatrix4x4>
#include <QtMath>
#include <cmath>

namespace {

// 与 Verse 壁纸引擎一致的渲染参数：FOV 75°、立方体边长 10、
// 自转速度 ROTATION_SPEED(0.005) × dt(毫秒) × 0.06，dt 上限 50ms
constexpr qreal kFov = 75.0;
constexpr qreal kNear = 0.1;
constexpr qreal kFar = 1000.0;
constexpr qreal kCubeHalf = 5.0;
constexpr qreal kRotationSpeed = 0.005;
constexpr qint64 kMaxDeltaMs = 50;
constexpr int kFrameIntervalMs = 33;  // ~30fps，与 Verse 全景空闲降帧后一致

struct SkyVertex {
    float x, y, z;
    float u, v;
};

// 面序与 Three.js BoxGeometry 材质序一致：+X, -X, +Y, -Y, +Z, -Z。
// 每面顶点顺序：左上、右上、左下、右下（t=0 为图片顶部）。
const SkyVertex kCubeVerts[24] = {
    // +X
    { 5,  5, -5, 0, 0}, { 5,  5,  5, 1, 0}, { 5, -5, -5, 0, 1}, { 5, -5,  5, 1, 1},
    // -X
    {-5,  5,  5, 0, 0}, {-5,  5, -5, 1, 0}, {-5, -5,  5, 0, 1}, {-5, -5, -5, 1, 1},
    // +Y（图片顶部朝 -Z，即初始视线正前方）
    {-5,  5, -5, 0, 0}, { 5,  5, -5, 1, 0}, {-5,  5,  5, 0, 1}, { 5,  5,  5, 1, 1},
    // -Y
    {-5, -5,  5, 0, 0}, { 5, -5,  5, 1, 0}, {-5, -5, -5, 0, 1}, { 5, -5, -5, 1, 1},
    // +Z
    { 5,  5,  5, 0, 0}, {-5,  5,  5, 1, 0}, { 5, -5,  5, 0, 1}, {-5, -5,  5, 1, 1},
    // -Z（初始视线正前方，对应全景图水平中心）
    {-5,  5, -5, 0, 0}, { 5,  5, -5, 1, 0}, {-5, -5, -5, 0, 1}, { 5, -5, -5, 1, 1},
};

const GLushort kCubeIndices[36] = {
    0,  2,  1,  1,  2,  3,
    4,  6,  5,  5,  6,  7,
    8, 10,  9,  9, 10, 11,
    12, 14, 13, 13, 14, 15,
    16, 18, 17, 17, 18, 19,
    20, 22, 21, 21, 22, 23,
};

// 各面方向基：d = normalize(D + U·u + V·v)，u 向右、v 向上，与顶点 UV 对应
struct FaceBasis {
    float dx, dy, dz;
    float ux, uy, uz;
    float vx, vy, vz;
};

const FaceBasis kFaceBases[6] = {
    { 1, 0, 0,   0, 0, 1,   0, 1, 0},   // +X
    {-1, 0, 0,   0, 0,-1,   0, 1, 0},   // -X
    { 0, 1, 0,   1, 0, 0,   0, 0,-1},   // +Y
    { 0,-1, 0,   1, 0, 0,   0, 0, 1},   // -Y
    { 0, 0, 1,  -1, 0, 0,   0, 1, 0},   // +Z
    { 0, 0,-1,   1, 0, 0,   0, 1, 0},   // -Z
};

inline QRgb bilinearSample(const QImage &img, float fx, float fy)
{
    const int w = img.width();
    const int h = img.height();
    fy = qBound(0.0f, fy, float(h - 1));
    fx = fx - std::floor(fx / w) * w;  // 水平方向环绕，保证 360° 接缝连续
    const int x0 = int(fx);
    const int y0 = int(fy);
    const int x1 = (x0 + 1) % w;
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
                 mix(qAlpha(p00), qAlpha(p10), qAlpha(p01), qAlpha(p11)));
}

const char *kVertexShader = BLOCKBOX_GLSL_VS_HEADER
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec2 aUV;\n"
    "uniform mat4 uMVP;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "}\n";

const char *kFragmentShader = BLOCKBOX_GLSL_FS_HEADER
    "uniform sampler2D uTex;\n"
    "in vec2 vUV;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(uTex, vUV);\n"
    "}\n";

} // namespace

PanoramaSkyWidget::PanoramaSkyWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    m_vbo = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    m_ibo = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    m_clock.start();

    m_timer.setInterval(kFrameIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (m_animating && isVisible())
            update();
    });
}

PanoramaSkyWidget::~PanoramaSkyWidget()
{
    makeCurrent();
    for (QOpenGLTexture *&tex : m_faceTextures) {
        delete tex;
        tex = nullptr;
    }
    m_vbo.destroy();
    m_ibo.destroy();
    m_vao.destroy();
    doneCurrent();
}

void PanoramaSkyWidget::setPanorama(const QImage &image)
{
    if (image.isNull())
        return;
    m_sourceImage = image;
    m_facesDirty = true;
    m_texturesDirty = true;
    // 不可见时（如启动时尚未切到旋转模式）延迟到 showEvent 再做转换，避免拖慢启动
    if (isVisible())
        ensureFaces();
    update();
}

void PanoramaSkyWidget::setBlurRadius(int radius)
{
    radius = qBound(0, radius, 64);
    if (m_blurRadius == radius)
        return;
    m_blurRadius = radius;
    if (!m_rawFaces.isEmpty()) {
        m_facesDirty = true;
        m_texturesDirty = true;
        if (isVisible())
            ensureFaces();
    }
    update();
}

void PanoramaSkyWidget::ensureFaces()
{
    if (!m_facesDirty)
        return;
    if (m_rawFaces.isEmpty()) {
        m_rawFaces = convertEquirectToFaces(m_sourceImage, 1024);
        if (m_rawFaces.isEmpty()) {
            m_facesDirty = false;
            return;
        }
        // 六个面已生成，原图不再需要，释放内存
        m_sourceImage = QImage();
    }
    m_faces.clear();
    m_faces.reserve(m_rawFaces.size());
    for (const QImage &face : m_rawFaces)
        m_faces.append(BackgroundWidget::blurImage(face, m_blurRadius));
    m_facesDirty = false;
    m_texturesDirty = true;
}

QVector<QImage> PanoramaSkyWidget::convertEquirectToFaces(const QImage &source, int faceSize)
{
    QVector<QImage> faces;
    if (source.isNull() || faceSize <= 0)
        return faces;

    QImage src = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // 限制源图尺寸，控制单次转换的耗时与内存占用
    if (src.width() > 4096 || src.height() > 2048)
        src = src.scaled(4096, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (src.isNull())
        return faces;

    const int w = src.width();
    const int h = src.height();

    faces.reserve(6);
    for (int face = 0; face < 6; ++face) {
        const FaceBasis &basis = kFaceBases[face];
        QImage out(faceSize, faceSize, QImage::Format_ARGB32_Premultiplied);
        const float step = 2.0f / faceSize;
        for (int j = 0; j < faceSize; ++j) {
            // v ∈ (-1, 1)，向上为正；行 0 对应图片顶部（v ≈ +1）
            const float v = 1.0f - (j + 0.5f) * step;
            QRgb *dst = reinterpret_cast<QRgb*>(out.scanLine(j));
            for (int i = 0; i < faceSize; ++i) {
                const float u = -1.0f + (i + 0.5f) * step;
                float dx = basis.dx + basis.ux * u + basis.vx * v;
                float dy = basis.dy + basis.uy * u + basis.vy * v;
                float dz = basis.dz + basis.uz * u + basis.vz * v;
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= len; dy /= len; dz /= len;

                // 经度以 -Z（初始视线正前方）为 0，纬度以水平面为 0
                const float lon = std::atan2(dx, -dz);
                const float lat = std::asin(qBound(-1.0f, dy, 1.0f));
                const float fx = (lon / float(M_PI) * 0.5f + 0.5f) * w;
                const float fy = (0.5f - lat / float(M_PI)) * h;
                dst[i] = bilinearSample(src, fx, fy);
            }
        }
        faces.append(out);
    }
    return faces;
}

void PanoramaSkyWidget::initializeGL()
{
    initializeOpenGLFunctions();
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader))
        return;
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader))
        return;
    if (!m_program.link())
        return;

    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(kCubeVerts, int(sizeof(kCubeVerts)));
    m_ibo.create();
    m_ibo.bind();
    m_ibo.allocate(kCubeIndices, int(sizeof(kCubeIndices)));

    constexpr int stride = int(sizeof(SkyVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(12));

    m_vao.release();
    m_ibo.release();
    m_vbo.release();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // 相机位于立方体内部，看到的是内壁（背面），关闭背面剔除
    glDisable(GL_CULL_FACE);
    // 与 Verse 一致的兜底底色 0x0A0A0A，面贴图就绪前不至于黑屏刺眼
    glClearColor(0.0392f, 0.0392f, 0.0392f, 1.0f);

    m_pipelineReady = true;
    if (m_facesDirty)
        ensureFaces();
}

void PanoramaSkyWidget::rebuildTextures()
{
    if (!m_pipelineReady || m_faces.isEmpty())
        return;
    for (QOpenGLTexture *&tex : m_faceTextures) {
        delete tex;
        tex = nullptr;
    }
    for (int i = 0; i < m_faces.size() && i < 6; ++i) {
        auto *tex = new QOpenGLTexture(m_faces.at(i));
        tex->setMinificationFilter(QOpenGLTexture::Linear);
        tex->setMagnificationFilter(QOpenGLTexture::Linear);
        tex->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_faceTextures[i] = tex;
    }
    m_texturesDirty = false;
}

void PanoramaSkyWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 与 Verse 引擎一致的帧间隔限幅，防止切回页面后一次性转过大的角度
    const qint64 dt = qBound<qint64>(0, m_clock.restart(), kMaxDeltaMs);
    m_rotation += kRotationSpeed * dt * 0.06;

    if (!m_pipelineReady)
        return;
    if (m_facesDirty)
        ensureFaces();
    if (m_texturesDirty)
        rebuildTextures();
    if (!m_faceTextures[0])
        return;

    QMatrix4x4 proj;
    proj.perspective(float(kFov), qMax(1.0f, float(width()) / qMax(1, height())), float(kNear), float(kFar));
    QMatrix4x4 model;
    model.rotate(qRadiansToDegrees(m_rotation), 0.0f, 1.0f, 0.0f);

    m_program.bind();
    m_program.setUniformValue("uMVP", proj * model);
    m_vao.bind();
    for (int face = 0; face < 6; ++face) {
        if (!m_faceTextures[face])
            break;
        m_faceTextures[face]->bind();
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT,
                       reinterpret_cast<const void*>(face * 6 * sizeof(GLushort)));
    }
    m_vao.release();
    m_program.release();
}

void PanoramaSkyWidget::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    if (m_facesDirty)
        ensureFaces();
    // 显示时才开始动画，隐藏时自动停表，避免后台空转
    m_animating = true;
    m_clock.restart();
    m_timer.start();
}

void PanoramaSkyWidget::hideEvent(QHideEvent *event)
{
    QOpenGLWidget::hideEvent(event);
    m_animating = false;
    m_timer.stop();
}
