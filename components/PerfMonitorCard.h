#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QPaintEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

class PerfMonitorCard : public QWidget
{
    Q_OBJECT

public:
    explicit PerfMonitorCard(const QString &name, QWidget *parent = nullptr);

    void setPercent(int value);
    void setAvailable(bool available);
    void setChartColor(const QColor &color);

    QString cardName() const { return m_nameLabel ? m_nameLabel->text() : QString(); }
    int currentPercent() const;
    const QVector<int>& dataPoints() const { return m_dataPoints; }
    QColor chartColor() const { return m_chartColor; }

signals:
    void zoomRequested(const QString &cardName);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    class ChartWidget : public QWidget
    {
    public:
        explicit ChartWidget(PerfMonitorCard *card, QWidget *parent = nullptr);

    protected:
        void paintEvent(QPaintEvent *event) override;

    private:
        PerfMonitorCard *m_card;
    };

private:
    QLabel *m_nameLabel;
    QLabel *m_percentLabel;
    QProgressBar *m_progressBar;
    ChartWidget *m_chartWidget;
    QLabel *m_unavailableLabel;

    QVector<int> m_dataPoints;
    QColor m_chartColor;
    bool m_available;
};
