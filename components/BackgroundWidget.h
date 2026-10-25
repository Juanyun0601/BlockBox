#ifndef BACKGROUNDWIDGET_H
#define BACKGROUNDWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QString>
#include <QColor>
#include <QTimer>
#include <QElapsedTimer>

class PanoramaSkyRenderer;

class BackgroundWidget : public QWidget
{
    Q_OBJECT

public:
    enum Mode { Classic, SolidColor, Image, FlowLight, Rotating, Bing };

    explicit BackgroundWidget(QWidget *parent = nullptr);
    ~BackgroundWidget() override;

    void setMode(Mode mode);
    void setSolidColor(const QColor &color);
    void setImage(const QString &path);
    void setBlurRadius(int radius);
    // 旋转全景内置场景（空串=使用自定义图片）
    void setPanoramaScene(const QString &scene);
    // 旋转全景自转速度（百分比，100 = 默认速度，0 = 静止）
    void setRotatingSpeed(int percent);

    // 深色主题下在壁纸上叠一层暗纱，避免亮图把界面文字冲淡
    void setDarkScrimEnabled(bool enabled);
    bool darkScrimEnabled() const { return m_darkScrim; }

    // 供全景渲染器处理面贴图/全景图模糊复用
    static QImage blurImage(const QImage &source, int radius);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updatePixmap();
    void paintFlowLight(QPainter &painter);
    void startFlowAnimation();
    void stopFlowAnimation();
    qreal flowPhase() const;

    // 旋转全景的来源与动画
    void applyRotatingSource();
    void startRotatingAnimation();
    void stopRotatingAnimation();
    void tickRotating();
    void reloadRotatingFrame();
    // 素材缺失时的静态兜底，避免整块纯黑盖住界面
    QPixmap rotatingPreview() const;

    Mode m_mode;
    QColor m_solidColor;
    QString m_imagePath;
    QString m_panoramaScene;
    int m_blurRadius;
    bool m_darkScrim = false;
    QPixmap m_originalPixmap;
    QPixmap m_pixmap;

    mutable QPixmap m_rotatingPreview;
    mutable QString m_rotatingPreviewScene;

    // 旋转全景由纯 CPU 渲染器绘制（走普通绘制路径，不依赖显卡与额外原生表面）
    PanoramaSkyRenderer *m_panorama = nullptr;

    QTimer m_flowTimer;
    QElapsedTimer m_flowClock;
    bool m_flowRunning = false;

    QTimer m_rotateTimer;
    QElapsedTimer m_rotateClock;
    int m_rotateIntervalMs = 33;
    bool m_rotateDrewOnce = false;
};

#endif
