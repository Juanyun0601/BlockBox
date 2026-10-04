#pragma once

#include "components/AppDialogBase.h"

#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QWidget>
#include <QColor>

class PerformanceDetailDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit PerformanceDetailDialog(const QString &name,
                                     const QVector<int> &dataPoints,
                                     int currentValue,
                                     const QColor &color,
                                     QWidget *parent = nullptr);

private:
    class BigChartWidget : public QWidget
    {
    public:
        explicit BigChartWidget(const QVector<int> &data,
                                const QColor &color,
                                QWidget *parent = nullptr);

    protected:
        void paintEvent(QPaintEvent *event) override;

    private:
        QVector<int> m_data;
        QColor m_color;
    };

    void setupUI(const QString &name,
                 const QVector<int> &dataPoints,
                 int currentValue,
                 const QColor &color);

    QLabel *m_titleLabel;
    QLabel *m_valueLabel;
    BigChartWidget *m_bigChart;
};
