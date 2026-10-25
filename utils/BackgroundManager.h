#ifndef BACKGROUNDMANAGER_H
#define BACKGROUNDMANAGER_H

#include <QObject>
#include <QString>

class BackgroundManager : public QObject
{
    Q_OBJECT

public:
    enum BackgroundMode {
        Classic,
        SolidColor,
        Image,
        FlowLight,
        Rotating,
        Bing
    };

    static BackgroundManager* instance();

    BackgroundMode currentMode() const;
    void setMode(BackgroundMode mode);

    QString solidColor() const;
    void setSolidColor(const QString &color);

    QString imagePath() const;
    void setImagePath(const QString &path);

    // 旋转全景模式的内置场景名（如 "overworld"），空串表示使用自定义图片
    QString panoramaScene() const;
    void setPanoramaScene(const QString &scene);

    QString bingImagePath() const;
    void setBingImagePath(const QString &path);

    int blurRadius() const;
    void setBlurRadius(int radius);

    // 旋转全景的自转速度，按百分比表示（100 = 默认速度，0 = 静止）
    int rotatingSpeed() const;
    void setRotatingSpeed(int percent);

    QString backgroundStyleSheet() const;

    void loadFromSettings();
    void saveToSettings();

signals:
    void backgroundChanged();
    // 只与旋转速度有关，订阅方据此直接调整天空盒而无需重建整套样式表
    void rotatingSpeedChanged(int percent);

private:
    explicit BackgroundManager(QObject *parent = nullptr);
    ~BackgroundManager() override;

    static BackgroundManager* m_instance;

    BackgroundMode m_mode;
    QString m_solidColor;
    QString m_imagePath;
    QString m_panoramaScene;
    QString m_bingImagePath;
    int m_blurRadius;
    int m_rotatingSpeed;
};

#endif
