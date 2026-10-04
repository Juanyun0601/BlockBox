#ifndef SCREENSHOTVIEWER_H
#define SCREENSHOTVIEWER_H

#include <QDialog>
#include <QVector>
#include <QPixmap>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QEvent>

class ScreenshotViewer : public QDialog
{
    Q_OBJECT

public:
    explicit ScreenshotViewer(const QVector<QPixmap> &pixmaps, int initialIndex,
                              QWidget *parent = nullptr);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUI();
    void showImage(int index);
    void fitToWindow();
    void updateNavButtons();
    void updateThumbnailHighlight();

    QVector<QPixmap> m_pixmaps;
    int m_currentIndex;

    QGraphicsView *m_view;
    QGraphicsScene *m_scene;
    QPushButton *m_prevBtn;
    QPushButton *m_nextBtn;
    QWidget *m_thumbnailStrip;
    QHBoxLayout *m_thumbnailLayout;
    double m_zoomFactor;
};

#endif
