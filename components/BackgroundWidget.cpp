#include "BackgroundWidget.h"

#include "PanoramaSkyRenderer.h"
#include "utils/LowConfigMode.h"

#include <QPainter>
#include <QDebug>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QtMath>
#include <cmath>

BackgroundWidget::BackgroundWidget(QWidget *parent)
    : QWidget(parent)
    , m_mode(Classic)
    , m_solidColor(Qt::white)
    , m_blurRadius(0)
{
    m_panorama = new PanoramaSkyRenderer;
    // 低配设备降低旋转全景的渲染分辨率，优先保证界面流畅
    m_panorama->setRenderLimit(LowConfigMode::instance()->isLowConfigMode() ? 900 : 1200);

    m_flowClock.start();
    m_flowTimer.setInterval(33); // ~30 FPS
    connect(&m_flowTimer, &QTimer::timeout, this, [this]() {
        if (m_flowRunning && isVisible())
            update();
    });

    // 旋转全景：纯 CPU 渲染 + 普通绘制，定时器只推进角度
    m_rotateClock.start();
    m_rotateTimer.setInterval(m_rotateIntervalMs);
    connect(&m_rotateTimer, &QTimer::timeout, this, &BackgroundWidget::tickRotating);
}

BackgroundWidget::~BackgroundWidget()
{
    delete m_panorama;
}

void BackgroundWidget::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    if (mode == FlowLight) {
        startFlowAnimation();
    } else {
        stopFlowAnimation();
    }
    if (mode == Rotating) {
        applyRotatingSource();
        startRotatingAnimation();
    } else {
        stopRotatingAnimation();
    }
    update();
}

void BackgroundWidget::setRotatingSpeed(int percent)
{
    const qreal scale = qBound(0, percent, 400) / 100.0;
    if (qFuzzyCompare(m_panorama->rotationSpeedScale() + 1.0, scale + 1.0))
        return;
    m_panorama->setRotationSpeedScale(scale);
    if (m_mode == Rotating)
        update();
}

void BackgroundWidget::setDarkScrimEnabled(bool enabled)
{
    if (m_darkScrim == enabled)
        return;
    m_darkScrim = enabled;
    update();
}

void BackgroundWidget::setPanoramaScene(const QString &scene)
{
    if (m_panoramaScene == scene)
        return;
    m_panoramaScene = scene;
    if (m_mode == Rotating) {
        applyRotatingSource();
        reloadRotatingFrame();
    }
}

// 旋转全景来源：内置场景优先，未选场景时回退到自定义图片，都没有则用默认场景
void BackgroundWidget::applyRotatingSource()
{
    if (!m_panoramaScene.isEmpty()) {
        m_panorama->setScene(m_panoramaScene);
        return;
    }
    if (!m_imagePath.isEmpty()) {
        const QImage img(m_imagePath);
        if (!img.isNull()) {
            m_panorama->setPanorama(img);
            return;
        }
    }
    // 既没有内置场景，也没有可用的自定义图片：改用默认场景，
    // 否则没有任何素材，只会露出整块黑底盖住界面。
    m_panorama->setScene(PanoramaSkyRenderer::defaultSceneName());
}

// 旋转背景的静态预览图：全景还没生成时用它兜底，避免整屏黑底
QPixmap BackgroundWidget::rotatingPreview() const
{
    // 未选内置场景时优先用自定义图片
    if (m_panoramaScene.isEmpty() && !m_pixmap.isNull())
        return m_pixmap;

    const QString scene = m_panoramaScene.isEmpty()
                              ? PanoramaSkyRenderer::defaultSceneName()
                              : m_panoramaScene;
    if (m_rotatingPreviewScene == scene)
        return m_rotatingPreview;

    m_rotatingPreviewScene = scene;
    m_rotatingPreview = QPixmap(QStringLiteral(":/Images/Panorama/%1/panorama_0.jpg").arg(scene));
    if (m_rotatingPreview.isNull())
        qDebug() << "[BackgroundWidget]" << "No fallback panorama tile for scene:" << scene;
    return m_rotatingPreview;
}

void BackgroundWidget::reloadRotatingFrame()
{
    if (m_mode == Rotating)
        update();
}

void BackgroundWidget::setSolidColor(const QColor &color)
{
    m_solidColor = color;
    if (m_mode == SolidColor)
        update();
}

void BackgroundWidget::setImage(const QString &path)
{
    if (m_imagePath != path) {
        m_imagePath = path;
        // 自定义图片仅在未选内置场景时生效
        if (m_mode == Rotating) {
            applyRotatingSource();
            reloadRotatingFrame();
        }
        updatePixmap();
        if (m_mode == Image || m_mode == Rotating || m_mode == Bing)
            update();
    }
}

void BackgroundWidget::setBlurRadius(int radius)
{
    radius = qBound(0, radius, 64);
    if (m_blurRadius != radius) {
        m_blurRadius = radius;
        m_panorama->setBlurRadius(radius);
        if (!m_originalPixmap.isNull()) {
            QImage img = m_originalPixmap.toImage();
            m_pixmap = QPixmap::fromImage(blurImage(img, radius));
            if (m_mode == Image || m_mode == Rotating || m_mode == Bing)
                update();
        }
    }
}

void BackgroundWidget::updatePixmap()
{
    if (m_imagePath.isEmpty()) {
        m_originalPixmap = QPixmap();
        m_pixmap = QPixmap();
        return;
    }
    m_originalPixmap = QPixmap(m_imagePath);
    if (m_originalPixmap.isNull()) {
        qDebug() << "[BackgroundWidget]" << "Failed to load image:" << m_imagePath;
        m_pixmap = QPixmap();
        return;
    }
    QImage img = m_originalPixmap.toImage();
    m_pixmap = QPixmap::fromImage(blurImage(img, m_blurRadius));
}

static void boxBlurHorizontal(const QImage &src, QImage &dst, int radius)
{
    int w = src.width(), h = src.height();
    int r = qMin(radius, w / 2);
    if (r <= 0) { dst = src; return; }

    for (int y = 0; y < h; ++y) {
        const QRgb *srcLine = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb *dstLine = reinterpret_cast<QRgb*>(dst.scanLine(y));

        int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
        for (int x = -r; x <= r; ++x) {
            int cx = qBound(0, x, w - 1);
            sumR += qRed(srcLine[cx]);
            sumG += qGreen(srcLine[cx]);
            sumB += qBlue(srcLine[cx]);
            sumA += qAlpha(srcLine[cx]);
        }
        int count = 2 * r + 1;

        for (int x = 0; x < w; ++x) {
            dstLine[x] = qRgba(sumR / count, sumG / count, sumB / count, sumA / count);

            int removeX = qBound(0, x - r, w - 1);
            int addX = qBound(0, x + r + 1, w - 1);
            sumR += qRed(srcLine[addX]) - qRed(srcLine[removeX]);
            sumG += qGreen(srcLine[addX]) - qGreen(srcLine[removeX]);
            sumB += qBlue(srcLine[addX]) - qBlue(srcLine[removeX]);
            sumA += qAlpha(srcLine[addX]) - qAlpha(srcLine[removeX]);
        }
    }
}

static void boxBlurVertical(const QImage &src, QImage &dst, int radius)
{
    int w = src.width(), h = src.height();
    int r = qMin(radius, h / 2);
    if (r <= 0) { dst = src; return; }

    QVector<int> colR(w), colG(w), colB(w), colA(w);

    for (int x = 0; x < w; ++x) {
        int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
        for (int y = -r; y <= r; ++y) {
            int cy = qBound(0, y, h - 1);
            QRgb p = reinterpret_cast<const QRgb*>(src.constScanLine(cy))[x];
            sumR += qRed(p);
            sumG += qGreen(p);
            sumB += qBlue(p);
            sumA += qAlpha(p);
        }
        colR[x] = sumR;
        colG[x] = sumG;
        colB[x] = sumB;
        colA[x] = sumA;
    }
    int count = 2 * r + 1;

    for (int y = 0; y < h; ++y) {
        QRgb *dstLine = reinterpret_cast<QRgb*>(dst.scanLine(y));
        for (int x = 0; x < w; ++x) {
            dstLine[x] = qRgba(colR[x] / count, colG[x] / count, colB[x] / count, colA[x] / count);
        }
        int removeY = qMax(0, y - r);
        int addY = qMin(y + r + 1, h - 1);
        if (addY < h) {
            const QRgb *addLine = reinterpret_cast<const QRgb*>(src.constScanLine(addY));
            const QRgb *removeLine = reinterpret_cast<const QRgb*>(src.constScanLine(removeY));
            for (int x = 0; x < w; ++x) {
                colR[x] += qRed(addLine[x]) - qRed(removeLine[x]);
                colG[x] += qGreen(addLine[x]) - qGreen(removeLine[x]);
                colB[x] += qBlue(addLine[x]) - qBlue(removeLine[x]);
                colA[x] += qAlpha(addLine[x]) - qAlpha(removeLine[x]);
            }
        }
    }
}

QImage BackgroundWidget::blurImage(const QImage &source, int radius)
{
    if (radius <= 0 || source.isNull())
        return source;

    QImage img = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int r = qMin(radius, qMin(img.width(), img.height()) / 2);
    if (r <= 0)
        return source;

    QImage temp(img.size(), QImage::Format_ARGB32_Premultiplied);
    boxBlurHorizontal(img, temp, r);
    QImage result(img.size(), QImage::Format_ARGB32_Premultiplied);
    boxBlurVertical(temp, result, r);

    return result;
}

void BackgroundWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    switch (m_mode) {
    case SolidColor:
        painter.fillRect(rect(), m_solidColor);
        break;

    case Image:
    case Bing:
        if (!m_pixmap.isNull()) {
            QPixmap scaled = m_pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int x = (width() - scaled.width()) / 2;
            int y = (height() - scaled.height()) / 2;
            painter.drawPixmap(x, y, scaled);
        }
        break;

    case FlowLight:
        paintFlowLight(painter);
        break;

    case Rotating:
        {
            const QImage &frame = m_panorama->render(size());
            if (!frame.isNull()) {
                painter.drawImage(QRect(0, 0, width(), height()), frame);
                // 按上一帧耗时自适应刷新间隔：慢机器上宁可帧率低一点，也不要拖慢整个界面
                const int cost = m_panorama->lastRenderCostMs();
                const int wanted = cost >= 24 ? 66 : (cost >= 12 ? 50 : 33);
                if (wanted != m_rotateIntervalMs) {
                    m_rotateIntervalMs = wanted;
                    m_rotateTimer.setInterval(wanted);
                }
                m_rotateDrewOnce = true;
            } else {
                // 还没有可用素材（或素材损坏）：用静态全景兜底，绝不露出黑底
                m_rotateDrewOnce = false;
                const QPixmap backdrop = rotatingPreview();
                if (!backdrop.isNull()) {
                    const QPixmap scaled = backdrop.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                                           Qt::SmoothTransformation);
                    painter.drawPixmap((width() - scaled.width()) / 2,
                                       (height() - scaled.height()) / 2, scaled);
                } else {
                    painter.fillRect(rect(), QColor(15, 23, 42));
                }
            }
        }
        break;

    case Classic:
    default:
        break;
    }

    // 深色主题：亮色壁纸/流光/全景上叠暗纱，保证浅色文字与玻璃卡片可读
    // 纯色/经典底已随主题变深，不再叠纱
    if (m_darkScrim && (m_mode == Image || m_mode == Bing || m_mode == Rotating || m_mode == FlowLight)) {
        if (m_mode == FlowLight)
            painter.fillRect(rect(), QColor(8, 8, 10, 150));
        else
            painter.fillRect(rect(), QColor(0, 0, 0, 110));
    }
}

// 流光动态背景：4 片慢速漂移的柔光光斑 + 斜向扫过的光束
void BackgroundWidget::paintFlowLight(QPainter &painter)
{
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0)
        return;

    const qreal t = flowPhase();
    const qreal minDim = qMin(w, h);

    // 基础底色（浅色主题适配）
    QLinearGradient base(0, 0, 0, h);
    base.setColorAt(0.0, QColor(236, 253, 245));
    base.setColorAt(0.45, QColor(248, 250, 252));
    base.setColorAt(1.0, QColor(255, 255, 255));
    painter.fillRect(rect(), base);

    painter.setPen(Qt::NoPen);
    painter.setRenderHint(QPainter::Antialiasing);

    auto drawBlob = [&](qreal cx, qreal cy, qreal radius, QColor color) {
        QRadialGradient g(cx, cy, radius);
        color.setAlphaF(0.30);
        g.setColorAt(0.0, color);
        QColor mid = color;
        mid.setAlphaF(0.10);
        g.setColorAt(0.55, mid);
        QColor end = color;
        end.setAlphaF(0.0);
        g.setColorAt(1.0, end);
        painter.setBrush(g);
        painter.drawEllipse(QPointF(cx, cy), radius, radius);
    };

    // 光斑沿椭圆路径缓慢漂移
    const qreal size = 0.9;
    drawBlob(w * (0.18 + 0.10 * qSin(t * 0.21)),
             h * (0.15 + 0.08 * qCos(t * 0.17)),
             minDim * 0.55 * size,
             QColor(16, 185, 129));

    drawBlob(w * (0.82 + 0.09 * qCos(t * 0.19 + 1.3)),
             h * (0.20 + 0.10 * qSin(t * 0.22 + 0.7)),
             minDim * 0.50 * size,
             QColor(99, 102, 241));

    drawBlob(w * (0.25 + 0.10 * qSin(t * 0.23 + 2.1)),
             h * (0.82 + 0.09 * qCos(t * 0.18 + 3.0)),
             minDim * 0.48 * size,
             QColor(236, 72, 153));

    drawBlob(w * (0.80 + 0.09 * qSin(t * 0.20 + 4.2)),
             h * (0.80 + 0.10 * qCos(t * 0.24 + 1.9)),
             minDim * 0.45 * size,
             QColor(6, 182, 212));

    // 斜向光束扫过
    const qreal streakLen = w * 0.5;
    for (int i = 0; i < 3; ++i) {
        qreal period = 9.0 + i * 1.5;
        qreal phase = t / period + i * 0.33;
        qreal frac = phase - std::floor(phase);
        qreal yPos = h * (0.18 + 0.32 * i);
        qreal x = -streakLen + frac * (w + 2 * streakLen);

        QLinearGradient streak(x - streakLen / 2, yPos,
                              x + streakLen / 2, yPos);
        streak.setColorAt(0.0, QColor(255, 255, 255, 0));
        streak.setColorAt(0.4, QColor(255, 255, 255, 200));
        streak.setColorAt(0.6, QColor(255, 255, 255, 200));
        streak.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.setBrush(streak);

        painter.save();
        painter.translate(x, yPos);
        painter.rotate(-12);
        QRectF band(-streakLen / 2, -2, streakLen, 4);
        painter.drawRoundedRect(band, 2, 2);
        painter.restore();
    }
}

void BackgroundWidget::startFlowAnimation()
{
    m_flowRunning = true;
    m_flowClock.restart();
    m_flowTimer.start();
    update();
}

void BackgroundWidget::stopFlowAnimation()
{
    m_flowRunning = false;
    m_flowTimer.stop();
}

qreal BackgroundWidget::flowPhase() const
{
    return m_flowClock.elapsed() / 1000.0;
}

void BackgroundWidget::startRotatingAnimation()
{
    m_rotateDrewOnce = false;
    m_rotateClock.restart();
    if (!m_rotateTimer.isActive())
        m_rotateTimer.start(m_rotateIntervalMs);
}

void BackgroundWidget::stopRotatingAnimation()
{
    m_rotateTimer.stop();
}

void BackgroundWidget::tickRotating()
{
    if (m_mode != Rotating || !isVisible())
        return;
    const qint64 dt = m_rotateClock.restart();
    const bool moved = m_panorama->advance(dt);
    if (!moved && m_rotateDrewOnce)
        return; // 转速为 0 或画面没变：不重绘，省电
    update();
}

void BackgroundWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_mode == FlowLight || m_mode == Rotating)
        update();
}

void BackgroundWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // 隐藏时停止定时器，避免后台持续触发浪费 CPU
    m_flowTimer.stop();
    m_rotateTimer.stop();
}

void BackgroundWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 重新显示时恢复定时器
    if (m_flowRunning && m_mode == FlowLight)
        m_flowTimer.start();
    if (m_mode == Rotating) {
        m_rotateClock.restart();
        m_rotateTimer.start(m_rotateIntervalMs);
        update();
    }
}
