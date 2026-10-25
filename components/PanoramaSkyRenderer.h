/**
 * @file   PanoramaSkyRenderer.h
 * @brief  旋转全景背景的软件渲染器（纯 CPU 绘制，不依赖 OpenGL）
 * @author BlockBox Team
 *
 * 早期版本用一个 OpenGL 子控件直接渲染 3D 天空盒。GL 画面在 Windows 上要经过
 * 单独的原生表面与合成层：全窗口模态弹窗关闭、窗口最小化后恢复、远程桌面、
 * 显卡驱动重置等时机，系统可能不再把这份画面合成到窗口上，背景就"黑掉"了，
 * 而控件内部其实还在正常绘制（只能靠反复重新映射控件把画面抢回来，不稳定）。
 *
 * 现在改成纯 CPU 实现：把全景素材预先采样成一条 360° 的环带图（只在需要显示的
 * 纬度范围内采样，兼顾质量与开销），每帧再按当前视角从环带里取像素。绘制走的是
 * 普通控件的 paintEvent，和界面其他部分同一条绘制路径，不存在"画了但没合成上去"
 * 的可能，也就不再出现整块黑底。
 */
#ifndef PANORAMASKYRENDERER_H
#define PANORAMASKYRENDERER_H

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

class PanoramaSkyRenderer
{
public:
    PanoramaSkyRenderer();

    // 内置场景：加载 :/Images/Panorama/<name>/panorama_0..5.jpg（MC 全景布局）
    void setScene(const QString &name);
    // 自定义全景图：按等距柱状投影使用
    void setPanorama(const QImage &equirect);
    void setBlurRadius(int radius);
    // 自转速度倍率（1.0 = 默认转速，0 = 静止），设置项以百分比换算后传入
    void setRotationSpeedScale(qreal scale);
    // 渲染画面的最长边上限（像素）：远超素材分辨率的部分没有意义，只会白烧 CPU
    void setRenderLimit(int longSide);

    static QString defaultSceneName();
    QString sceneName() const { return m_scene; }
    qreal rotationSpeedScale() const { return m_speedScale; }

    // 是否已拿到可用的全景素材（未拿到时外层需要自己兜底，避免空白）
    bool hasSource() const { return !m_band.isNull(); }

    // 按毫秒推进自转；速度为 0 时不改变视角，返回 false
    bool advance(qint64 deltaMs);
    qreal rotation() const { return m_rotation; }

    // 渲染当前视角的画面；尺寸与视角都没变时直接复用上一帧
    const QImage &render(const QSize &size);

    // 最近一帧的渲染耗时（毫秒），供外层按机器性能调整刷新间隔
    int lastRenderCostMs() const { return m_lastRenderCostMs; }

private:
    void ensureBand();
    void buildBand();
    void reloadSource();
    void buildSampleTable(const QSize &size);

    QString m_scene;            // 内置场景名，空串表示使用自定义图片
    QImage m_equirectRaw;       // 等距柱状全景源图（未处理，模糊/缩放前的原样）
    QImage m_equirect;          // 已按模糊半径处理过的等距柱状全景图
    QVector<QImage> m_faces;    // 内置场景的 6 个立方体面（已按模糊半径处理）
    int m_blurRadius = 0;
    qreal m_speedScale = 1.0;
    int m_renderLimit = 1200;

    bool m_sourceDirty = true;  // 素材或模糊半径变化，需要重建环带
    QImage m_band;              // 360° 环带图，按下标映射经度/纬度
    quint32 m_bandGeneration = 0; // 环带代次，变化时纵向采样表要重建

    // 纵向采样表：像素的采样行只跟像素位置有关，与自转角无关。
    // 预先把「环带行偏移 + 插值权重」算好，每帧只剩查表与取样，避免每像素开方/反正弦。
    QVector<quint32> m_sampleTable; // (行偏移像素数 << 8) | 权重
    QSize m_sampleSize;             // 表对应的渲染尺寸
    quint32 m_sampleBandGeneration = 0;

    qreal m_rotation = 0.0;     // 累计自转角（弧度，绕竖直轴）
    QImage m_frame;
    QSize m_frameSize;
    qreal m_frameRotation = -1.0;
    bool m_frameValid = false;
    int m_lastRenderCostMs = 0;
};

#endif
