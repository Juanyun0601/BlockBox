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
    // 旋转全景来源：场景名 + 已下载素材目录（dir 为空时回退资源内素材，仅供开发探针）
    void setPanoramaSource(const QString &scene, const QString &dir);
    // 旋转全景自转速度（百分比，100 = 默认速度，0 = 静止）
    void setRotatingSpeed(int percent);

    // 深色主题下在壁纸上叠一层暗纱，避免亮图把界面文字冲淡
    void setDarkScrimEnabled(bool enabled);
    bool darkScrimEnabled() const { return m_darkScrim; }

    /**
     * 前台页面重绘密集（存档地图：拖动大图 + 后台瓦片渲染）时打开省电档：
     * 旋转全景降到低帧率、出图分辨率也降一档，把 UI 线程让给前台页面。
     * 关掉后自适应逻辑会自己把帧率拉回去。
     */
    void setEcoMode(bool on);
    bool ecoMode() const { return m_ecoMode; }

    /**
     * 前台正在拖动 / 缩放（最需要跟手）时暂停背景动画：画面定格在当前角度，
     * 只做一次廉价贴图，不再占 UI 线程；停手后调用 false 恢复。
     */
    void setAnimationPaused(bool paused);
    bool animationPaused() const { return m_animationPaused; }

    // 供全景渲染器处理面贴图/全景图模糊复用
    static QImage blurImage(const QImage &source, int radius);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

    /** 全局监听鼠标按下 / 滚轮：用户正在拖动或滚动时先停下背景动画 */
    bool eventFilter(QObject *watched, QEvent *event) override;

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

    // 按「上一帧耗时 + 当前档位」算旋转刷新间隔（毫秒）
    int targetRotateIntervalMs(int costMs) const;
    // 档位切换后重设出图限幅与定时器间隔
    void applyRenderBudget();

    Mode m_mode;
    QColor m_solidColor;
    QString m_imagePath;
    QString m_panoramaScene;
    QString m_panoramaDir;
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

    // 省电档 / 暂停档：前台重绘密集时降低背景开销
    bool m_ecoMode = false;
    bool m_animationPaused = false;
    // 普通档的出图长边（构造时按是否低配定），省电档按它打折
    int m_baseRenderLimit = 1200;
    // 拖手后稍等片刻再恢复转动，避免连续操作时反复起停
    QTimer *m_pauseIdleTimer = nullptr;
    // 按下后先观察一小会儿：确实在拖才停背景，点一下不算
    QTimer *m_pressHoldTimer = nullptr;
};

#endif
