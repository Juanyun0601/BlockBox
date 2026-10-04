#include "ScreenshotViewer.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include <QWheelEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QApplication>
#include <QStyle>

ScreenshotViewer::ScreenshotViewer(const QVector<QPixmap> &pixmaps, int initialIndex,
                                   QWidget *parent)
    : QDialog(parent)
    , m_pixmaps(pixmaps)
    , m_currentIndex(initialIndex)
    , m_view(nullptr)
    , m_scene(nullptr)
    , m_prevBtn(nullptr)
    , m_nextBtn(nullptr)
    , m_thumbnailStrip(nullptr)
    , m_thumbnailLayout(nullptr)
    , m_zoomFactor(1.0)
{
    setWindowTitle(tr("截图查看"));
    setMinimumSize(600, 400);
    resize(900, 650);
    setupUI();
    showImage(m_currentIndex);
}

void ScreenshotViewer::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Image display area
    QWidget *displayArea = new QWidget();
    QGridLayout *displayLayout = new QGridLayout(displayArea);
    displayLayout->setContentsMargins(4, 0, 4, 0);
    displayLayout->setSpacing(0);

    QString btnStyle =
        "QPushButton { background: rgba(0,0,0,40); border: none; border-radius: 16px; }"
        "QPushButton:hover { background: rgba(0,0,0,70); border-radius: 16px; }";

    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setStyleSheet("background: #1a1a1a;");
    displayLayout->addWidget(m_view, 0, 0, 1, 3);

    m_prevBtn = new QPushButton();
    m_prevBtn->setFixedSize(32, 32);
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    m_prevBtn->setFlat(true);
    m_prevBtn->setStyleSheet(btnStyle);
    displayLayout->addWidget(m_prevBtn, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_nextBtn = new QPushButton();
    m_nextBtn->setFixedSize(32, 32);
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    m_nextBtn->setFlat(true);
    m_nextBtn->setStyleSheet(btnStyle);
    displayLayout->addWidget(m_nextBtn, 0, 2, Qt::AlignRight | Qt::AlignVCenter);

    mainLayout->addWidget(displayArea, 1);

    // Thumbnail strip
    m_thumbnailStrip = new QWidget();
    m_thumbnailStrip->setFixedHeight(90);
    m_thumbnailStrip->setStyleSheet("background: #2a2a2a;");
    m_thumbnailLayout = new QHBoxLayout(m_thumbnailStrip);
    m_thumbnailLayout->setContentsMargins(8, 5, 8, 5);
    m_thumbnailLayout->setSpacing(6);

    for (int i = 0; i < m_pixmaps.size(); ++i) {
        QLabel *thumb = new QLabel();
        thumb->setObjectName("thumbnail");
        thumb->setFixedSize(120, 75);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setCursor(Qt::PointingHandCursor);
        thumb->installEventFilter(this);
        if (!m_pixmaps[i].isNull())
            thumb->setPixmap(m_pixmaps[i].scaled(120, 75, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_thumbnailLayout->addWidget(thumb);
    }
    m_thumbnailLayout->addStretch();

    mainLayout->addWidget(m_thumbnailStrip);

    // Style icons
    updateNavButtons();

    // Connect navigation
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentIndex > 0)
            showImage(m_currentIndex - 1);
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentIndex < m_pixmaps.size() - 1)
            showImage(m_currentIndex + 1);
    });

}

void ScreenshotViewer::showImage(int index)
{
    if (index < 0 || index >= m_pixmaps.size())
        return;
    if (m_pixmaps[index].isNull())
        return;

    m_currentIndex = index;
    m_zoomFactor = 1.0;

    m_scene->clear();
    QGraphicsPixmapItem *item = m_scene->addPixmap(m_pixmaps[index]);
    m_scene->setSceneRect(m_pixmaps[index].rect());

    fitToWindow();
    updateNavButtons();
    updateThumbnailHighlight();
    setWindowTitle(QString(tr("截图查看 (%1/%2)")).arg(m_currentIndex + 1).arg(m_pixmaps.size()));
}

void ScreenshotViewer::fitToWindow()
{
    if (m_pixmaps.isEmpty() || m_currentIndex < 0)
        return;

    m_view->resetTransform();
    QPixmap px = m_pixmaps[m_currentIndex];
    double scaleX = double(m_view->viewport()->width()) / px.width();
    double scaleY = double(m_view->viewport()->height()) / px.height();
    double scale = qMin(scaleX, scaleY);
    if (scale < 1.0)
        scale = 1.0;
    m_view->scale(scale, scale);
    m_zoomFactor = scale;
}

void ScreenshotViewer::updateNavButtons()
{
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    QIcon leftIcon = IconHelper::loadColoredIcon(":/Images/Icons/chevron_left.svg", themeColor, 20);
    QIcon rightIcon = IconHelper::loadColoredIcon(":/Images/Icons/chevron_right.svg", themeColor, 20);

    m_prevBtn->setIcon(leftIcon);
    m_prevBtn->setIconSize(QSize(20, 20));
    m_prevBtn->setVisible(m_currentIndex > 0);

    m_nextBtn->setIcon(rightIcon);
    m_nextBtn->setIconSize(QSize(20, 20));
    m_nextBtn->setVisible(m_currentIndex < m_pixmaps.size() - 1);
}

void ScreenshotViewer::updateThumbnailHighlight()
{
    for (int i = 0; i < m_thumbnailLayout->count(); ++i) {
        QLayoutItem *item = m_thumbnailLayout->itemAt(i);
        if (item && item->widget()) {
            QLabel *thumb = qobject_cast<QLabel*>(item->widget());
            if (thumb) {
                bool selected = (i == m_currentIndex);
                thumb->setProperty("selected", selected);
                thumb->style()->polish(thumb);
            }
        }
    }
}

bool ScreenshotViewer::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLabel *thumb = qobject_cast<QLabel*>(watched);
        if (thumb) {
            for (int i = 0; i < m_thumbnailLayout->count(); ++i) {
                QLayoutItem *item = m_thumbnailLayout->itemAt(i);
                if (item && item->widget() == thumb) {
                    showImage(i);
                    return true;
                }
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ScreenshotViewer::wheelEvent(QWheelEvent *event)
{
    double delta = event->angleDelta().y();
    double factor = (delta > 0) ? 1.15 : 1.0 / 1.15;
    m_zoomFactor *= factor;

    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->scale(factor, factor);
    event->accept();
}

void ScreenshotViewer::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    if (!m_pixmaps.isEmpty() && m_currentIndex >= 0) {
        fitToWindow();
    }
}

void ScreenshotViewer::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_Left:
        if (m_currentIndex > 0)
            showImage(m_currentIndex - 1);
        break;
    case Qt::Key_Right:
        if (m_currentIndex < m_pixmaps.size() - 1)
            showImage(m_currentIndex + 1);
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_view->scale(1.15, 1.15);
        m_zoomFactor *= 1.15;
        break;
    case Qt::Key_Minus:
        m_view->scale(1.0 / 1.15, 1.0 / 1.15);
        m_zoomFactor /= 1.15;
        break;
    case Qt::Key_R:
        fitToWindow();
        break;
    default:
        QDialog::keyPressEvent(event);
    }
}
