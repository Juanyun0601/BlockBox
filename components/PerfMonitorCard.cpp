#include "PerfMonitorCard.h"

#include <QFont>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QResizeEvent>

#include "utils/ThemeManager.h"

static const int MAX_DATA_POINTS = 30;

PerfMonitorCard::ChartWidget::ChartWidget(PerfMonitorCard *card, QWidget *parent)
    : QWidget(parent)
    , m_card(card)
{
    setObjectName("perfChart");
    setMinimumHeight(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PerfMonitorCard::ChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!m_card->m_available || m_card->m_dataPoints.isEmpty())
        return;

    const QVector<int> &points = m_card->m_dataPoints;
    const int count = points.size();
    const QColor &color = m_card->m_chartColor;

    const int w = width();
    const int h = height();
    const int margin = 4;
    const int drawW = w - margin * 2;
    const int drawH = h - margin * 2;

    QVector<QPointF> positions;
    positions.reserve(count);

    if (count == 1)
    {
        qreal x = margin + drawW / 2.0;
        qreal y = margin + drawH - (drawH * points[0] / 100.0);
        positions.append(QPointF(x, y));
    }
    else
    {
        qreal stepX = static_cast<qreal>(drawW) / (count - 1);
        for (int i = 0; i < count; ++i)
        {
            qreal x = margin + i * stepX;
            qreal y = margin + drawH - (drawH * points[i] / 100.0);
            positions.append(QPointF(x, y));
        }
    }

    QPainterPath fillPath;
    fillPath.moveTo(positions.first().x(), margin + drawH);
    for (const QPointF &pt : positions)
        fillPath.lineTo(pt);
    fillPath.lineTo(positions.last().x(), margin + drawH);
    fillPath.closeSubpath();

    QColor fillColor = color;
    fillColor.setAlpha(25);
    painter.setBrush(fillColor);
    painter.setPen(Qt::NoPen);
    painter.drawPath(fillPath);

    QPainterPath linePath;
    linePath.moveTo(positions.first());
    for (int i = 1; i < positions.size(); ++i)
        linePath.lineTo(positions[i]);

    QPen linePen(color, 2);
    linePen.setCapStyle(Qt::RoundCap);
    linePen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(linePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(linePath);

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const QPointF &pt : positions)
        painter.drawEllipse(pt, 2.0, 2.0);
}

PerfMonitorCard::PerfMonitorCard(const QString &name, QWidget *parent)
    : QWidget(parent)
    , m_nameLabel(nullptr)
    , m_percentLabel(nullptr)
    , m_progressBar(nullptr)
    , m_chartWidget(nullptr)
    , m_unavailableLabel(nullptr)
    , m_chartColor(QColor("#4CAF50"))
    , m_available(true)
{
    setObjectName("perfMonitorCard");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("\u70B9\u51FB\u5361\u7247\u653E\u5927\u67E5\u770B"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(6);

    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    m_nameLabel = new QLabel(name, this);
    m_nameLabel->setObjectName("perfName");
    m_nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    headerLayout->addWidget(m_nameLabel, 1);

    layout->addLayout(headerLayout);

    m_percentLabel = new QLabel("0%", this);
    m_percentLabel->setObjectName("perfPercent");
    QFont percentFont = m_percentLabel->font();
    percentFont.setPointSize(28);
    percentFont.setBold(true);
    m_percentLabel->setFont(percentFont);
    m_percentLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_percentLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName("perfProgressBar");
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(4);
    layout->addWidget(m_progressBar);

    m_chartWidget = new ChartWidget(this, this);
    m_chartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_chartWidget, 1);

    m_unavailableLabel = new QLabel(QStringLiteral("\u4E0D\u53EF\u7528"), this);
    m_unavailableLabel->setObjectName("perfUnavailable");
    m_unavailableLabel->setAlignment(Qt::AlignCenter);
    m_unavailableLabel->setVisible(false);
    layout->addWidget(m_unavailableLabel);

    setLayout(layout);
}

void PerfMonitorCard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void PerfMonitorCard::setPercent(int value)
{
    m_percentLabel->setText(QString("%1%").arg(value));
    m_progressBar->setValue(value);

    m_dataPoints.append(value);
    while (m_dataPoints.size() > MAX_DATA_POINTS)
        m_dataPoints.removeFirst();

    m_chartWidget->update();
}

void PerfMonitorCard::setAvailable(bool available)
{
    m_available = available;

    m_nameLabel->setVisible(available);
    m_percentLabel->setVisible(available);
    m_progressBar->setVisible(available);
    m_chartWidget->setVisible(available);
    m_unavailableLabel->setVisible(!available);

    update();
}

void PerfMonitorCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit zoomRequested(m_nameLabel->text());
    QWidget::mouseReleaseEvent(event);
}

void PerfMonitorCard::setChartColor(const QColor &color)
{
    m_chartColor = color;
    const bool isDark = (ThemeManager::instance()->currentTheme()
                         == ThemeManager::DarkTheme);
    m_progressBar->setStyleSheet(
        QString("QProgressBar { background-color: %1; border: none; border-radius: 2px; min-height: 4px; max-height: 4px; }"
                "QProgressBar::chunk { background-color: %2; border-radius: 2px; }")
            .arg(isDark ? "#3d3d3d" : "#e8e8e8", color.name()));
    m_chartWidget->update();
}

int PerfMonitorCard::currentPercent() const
{
    if (m_dataPoints.isEmpty())
        return 0;
    return m_dataPoints.last();
}
