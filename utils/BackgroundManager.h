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

    QString bingImagePath() const;
    void setBingImagePath(const QString &path);

    int blurRadius() const;
    void setBlurRadius(int radius);

    QString backgroundStyleSheet() const;

    void loadFromSettings();
    void saveToSettings();

signals:
    void backgroundChanged();

private:
    explicit BackgroundManager(QObject *parent = nullptr);
    ~BackgroundManager() override;

    static BackgroundManager* m_instance;

    BackgroundMode m_mode;
    QString m_solidColor;
    QString m_imagePath;
    QString m_bingImagePath;
    int m_blurRadius;
};

#endif
