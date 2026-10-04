#ifndef BACKGROUNDWIDGET_H
#define BACKGROUNDWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QString>
#include <QColor>
#include <QTimer>
#include <QElapsedTimer>

class BackgroundWidget : public QWidget
{
    Q_OBJECT

public:
    enum Mode { Classic, SolidColor, Image, FlowLight, Rotating };

    explicit BackgroundWidget(QWidget *parent = nullptr);

    void setMode(Mode mode);
    void setSolidColor(const QColor &color);
    void setImage(const QString &path);
    void setBlurRadius(int radius);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updatePixmap();
    static QImage blurImage(const QImage &source, int radius);
    void paintFlowLight(QPainter &painter);
    void paintRotating(QPainter &painter);
    void startFlowAnimation();
    void stopFlowAnimation();
    qreal flowPhase() const;

    Mode m_mode;
    QColor m_solidColor;
    QString m_imagePath;
    int m_blurRadius;
    QPixmap m_originalPixmap;
    QPixmap m_pixmap;

    QTimer m_flowTimer;
    QElapsedTimer m_flowClock;
    bool m_flowRunning = false;
};

#endif
