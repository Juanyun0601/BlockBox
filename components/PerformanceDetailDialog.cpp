#include "PerformanceDetailDialog.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>

PerformanceDetailDialog::BigChartWidget::BigChartWidget(const QVector<int> &data,
                                                         const QColor &color,
                                                         QWidget *parent)
    : QWidget(parent)
    , m_data(data)
    , m_color(color)
{
    setMinimumSize(400, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PerformanceDetailDialog::BigChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_data.isEmpty())
        return;

    const int count = m_data.size();
    const int w = width();
    const int h = height();
    const int marginL = 40;
    const int marginR = 16;
    const int marginT = 16;
    const int marginB = 32;

    const int drawW = w - marginL - marginR;
    const int drawH = h - marginT - marginB;

    painter.setPen(QPen(QColor(0, 0, 0, 30), 1));
    int gridLines = 4;
    for (int i = 0; i <= gridLines; ++i)
    {
        qreal y = marginT + drawH * i / gridLines;
        painter.drawLine(QPointF(marginL, y), QPointF(w - marginR, y));

        int val = 100 - (100 * i / gridLines);
        painter.setPen(QColor(150, 150, 150));
        QFont font = painter.font();
        font.setPointSize(8);
        painter.setFont(font);
        painter.drawText(QRectF(0, y - 8, marginL - 6, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1%").arg(val));
        painter.setPen(QPen(QColor(0, 0, 0, 30), 1));
    }

    QVector<QPointF> positions;
    positions.reserve(count);

    qreal stepX = static_cast<qreal>(drawW) / (count - 1);
    for (int i = 0; i < count; ++i)
    {
        qreal x = marginL + i * stepX;
        qreal y = marginT + drawH - (drawH * m_data[i] / 100.0);
        positions.append(QPointF(x, y));
    }

    QPainterPath fillPath;
    fillPath.moveTo(positions.first().x(), marginT + drawH);
    for (const QPointF &pt : positions)
        fillPath.lineTo(pt);
    fillPath.lineTo(positions.last().x(), marginT + drawH);
    fillPath.closeSubpath();

    QColor fillColor = m_color;
    fillColor.setAlpha(20);
    painter.setBrush(fillColor);
    painter.setPen(Qt::NoPen);
    painter.drawPath(fillPath);

    QPainterPath linePath;
    linePath.moveTo(positions.first());
    for (int i = 1; i < positions.size(); ++i)
        linePath.lineTo(positions[i]);

    QPen linePen(m_color, 2.5);
    linePen.setCapStyle(Qt::RoundCap);
    linePen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(linePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(linePath);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    for (const QPointF &pt : positions)
        painter.drawEllipse(pt, 3.0, 3.0);

    if (!positions.isEmpty())
    {
        QPen hlPen(Qt::white, 2);
        painter.setPen(hlPen);
        painter.setBrush(m_color);
        painter.drawEllipse(positions.last(), 5.0, 5.0);
    }
}

PerformanceDetailDialog::PerformanceDetailDialog(const QString &name,
                                                  const QVector<int> &dataPoints,
                                                  int currentValue,
                                                  const QColor &color,
                                                  QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName("perfDetailDialog");
    setWindowTitle(name + QStringLiteral(" - \u6027\u80FD\u8BE6\u60C5"));

    setupUI(name, dataPoints, currentValue, color);
}

void PerformanceDetailDialog::setupUI(const QString &name,
                                       const QVector<int> &dataPoints,
                                       int currentValue,
                                       const QColor &color)
{
    // 外层布局：卡片居中（AppDialogBase 提供铺满主窗口的模糊遮罩背景）
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("perfDetailCard"));
    card->setMinimumSize(520, 420);
    card->setMaximumSize(760, 700);
    outer->addWidget(card, 0, Qt::AlignCenter);

    QVBoxLayout *mainLayout = new QVBoxLayout(card);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_titleLabel = new QLabel(name, this);
    m_titleLabel->setObjectName("perfDetailTitle");
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    headerLayout->addWidget(m_titleLabel, 1);

    mainLayout->addLayout(headerLayout);

    m_valueLabel = new QLabel(this);
    m_valueLabel->setObjectName("perfDetailValue");
    QString valueStyle = QString("color: %1;").arg(color.name());
    m_valueLabel->setStyleSheet(valueStyle);
    QFont valFont = m_valueLabel->font();
    valFont.setPointSize(48);
    valFont.setBold(true);
    m_valueLabel->setFont(valFont);
    m_valueLabel->setAlignment(Qt::AlignCenter);
    m_valueLabel->setText(QString("%1%").arg(currentValue));
    mainLayout->addWidget(m_valueLabel);

    if (!dataPoints.isEmpty())
    {
        int minVal = *std::min_element(dataPoints.begin(), dataPoints.end());
        int maxVal = *std::max_element(dataPoints.begin(), dataPoints.end());
        double avgVal = 0.0;
        for (int v : dataPoints) avgVal += v;
        avgVal /= dataPoints.size();

        QHBoxLayout *statsLayout = new QHBoxLayout();
        statsLayout->setContentsMargins(0, 0, 0, 0);
        statsLayout->setSpacing(0);

        auto createStatBox = [this](const QString &label, const QString &value) -> QWidget*
        {
            QWidget *box = new QWidget(this);
            box->setObjectName("perfDetailStatBox");
            QVBoxLayout *boxLayout = new QVBoxLayout(box);
            boxLayout->setContentsMargins(8, 6, 8, 6);
            boxLayout->setSpacing(2);
            boxLayout->setAlignment(Qt::AlignCenter);

            QLabel *valLabel = new QLabel(value, box);
            valLabel->setObjectName("perfDetailStatValue");
            valLabel->setAlignment(Qt::AlignCenter);
            boxLayout->addWidget(valLabel);

            QLabel *nameLabel = new QLabel(label, box);
            nameLabel->setObjectName("perfDetailStatName");
            nameLabel->setAlignment(Qt::AlignCenter);
            boxLayout->addWidget(nameLabel);

            return box;
        };

        statsLayout->addWidget(createStatBox(QStringLiteral("\u6700\u5C0F\u503C"), QString("%1%").arg(minVal)));
        statsLayout->addWidget(createStatBox(QStringLiteral("\u5E73\u5747\u503C"), QString("%1%").arg(qRound(avgVal))));
        statsLayout->addWidget(createStatBox(QStringLiteral("\u6700\u5927\u503C"), QString("%1%").arg(maxVal)));

        mainLayout->addLayout(statsLayout);
    }

    m_bigChart = new BigChartWidget(dataPoints, color, this);
    mainLayout->addWidget(m_bigChart, 1);
}
