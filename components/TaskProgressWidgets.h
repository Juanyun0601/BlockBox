/**
 * @file   TaskProgressWidgets.h
 * @brief  任务详情页现代化控件：环形进度、速度曲线、阶段步骤条
 * @author BlockBox Team
 * @date   2026-08-15
 */
#ifndef TASKPROGRESSWIDGETS_H
#define TASKPROGRESSWIDGETS_H

#include <QColor>
#include <QStringList>
#include <QVector>
#include <QWidget>

/* ============================================================
 * RingProgressWidget —— 环形进度（SVG 风格圆弧 + 中心百分比）
 * ============================================================ */
class RingProgressWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RingProgressWidget(QWidget *parent = nullptr);

    void setProgress(double progress);   ///< 0.0 ~ 100.0
    double progress() const;
    void setAccentColor(const QColor &color);
    void setCaption(const QString &caption);
    void setTextColor(const QColor &color);

    QSize sizeHint() const override { return QSize(150, 150); }
    QSize minimumSizeHint() const override { return QSize(110, 110); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double  m_progress;
    QColor  m_accentColor;
    QColor  m_textColor;
    QString m_caption;
};

/* ============================================================
 * SpeedChartWidget —— 实时速度面积曲线
 * ============================================================ */
class SpeedChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpeedChartWidget(QWidget *parent = nullptr);

    void addSample(double mbps);   ///< 每秒采样
    void clearSamples();
    void setAccentColor(const QColor &color);
    void setTextColor(const QColor &color);
    void setPeakHintText(const QString &text);

    QSize sizeHint() const override { return QSize(360, 168); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_samples;
    QColor m_accentColor;
    QColor m_textColor;
    QString m_peakHint;
};

/* ============================================================
 * StageStepperWidget —— 阶段步骤条（圆点 + 连接线 + 名称）
 * ============================================================ */
class StageStepperWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StageStepperWidget(QWidget *parent = nullptr);

    void setStages(const QStringList &stages);
    void setCurrentStage(int index);   ///< -1 表示尚未开始
    void setProgress(double progress); ///< 0.0 ~ 100.0 用于连接线填充比例
    void setAccentColor(const QColor &color);
    void setTextColor(const QColor &color);
    void setMutedColor(const QColor &color);

    QSize sizeHint() const override { return QSize(560, 58); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QStringList m_stages;
    int m_currentIndex;
    double m_progress;
    QColor m_accentColor;
    QColor m_textColor;
    QColor m_mutedColor;
};

#endif // TASKPROGRESSWIDGETS_H
