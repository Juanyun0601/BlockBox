/**
 * @file   PanoramaSkyWidget.h
 * @brief  旋转全景背景（3D 天空盒）控件
 * @author BlockBox Team
 *
 * 渲染方案移植自 Verse 启动器 VersePc2（GPL-3.0）的壁纸全景渲染器
 * （frontend/src/features/settings/wallpaper-panorama.js，PanoramaRenderer）：
 * https://github.com/doujie081231/VersePc2
 *
 * 思路一致：把全景图贴在一个大立方体的内壁（材质取 BackSide，从内部观察），
 * 相机位于立方体中心，整体绕竖直轴缓慢自转。差异在于贴图来源：Verse 直接
 * 加载 6 张独立的面贴图，本控件把用户选择的一张全景图在加载时按等距柱状
 * 投影展开为 6 个立方体面。
 */
#ifndef PANORAMASKYWIDGET_H
#define PANORAMASKYWIDGET_H

#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QTimer>
#include <QVector>

class PanoramaSkyWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit PanoramaSkyWidget(QWidget *parent = nullptr);
    ~PanoramaSkyWidget() override;

    void setPanorama(const QImage &image);
    void setBlurRadius(int radius);

protected:
    void initializeGL() override;
    void paintGL() override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void ensureFaces();
    void rebuildTextures();
    static QVector<QImage> convertEquirectToFaces(const QImage &source, int faceSize);

    QImage m_sourceImage;
    QVector<QImage> m_rawFaces;  // 转换后的 6 个面（未模糊），作为模糊的基准
    QVector<QImage> m_faces;     // 应用模糊后的最终面贴图
    int m_blurRadius = 0;
    bool m_facesDirty = false;   // m_faces 需要按当前模糊半径重建
    bool m_texturesDirty = false;
    bool m_pipelineReady = false;

    QOpenGLShaderProgram m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    QOpenGLBuffer m_ibo;
    QOpenGLTexture *m_faceTextures[6] = {};

    QTimer m_timer;
    QElapsedTimer m_clock;
    qreal m_rotation = 0;  // 累计自转角（弧度，绕竖直轴）
    bool m_animating = false;
};

#endif
