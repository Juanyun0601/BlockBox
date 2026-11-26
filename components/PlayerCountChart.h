/**
 * @file   PlayerCountChart.h
 * @brief  人数统计折线图：按时间画出在线人数曲线（服务端聚合后的采样点）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 用在社区「服务器导航」帖的详情页（服务器在线人数）和联机大厅房间详情页
 * （房间里正在玩的人数）。数据点由 PlayerStatSeries 传进来，控件自己负责：
 *   - 时间轴按传入的起止时间映射，点稀疏时也不会挤成一团；
 *   - 纵轴按峰值取整（1 / 2 / 5 × 10ⁿ），有上限时额外画一条虚线；
 *   - 鼠标悬停显示该时刻的人数和时间；没有数据时显示一句空态提示。
 *
 * 颜色全部在绘制时从 StyleKit 令牌解析，跟随深浅主题变化。
 */

#pragma once

#include <QColor>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

class PlayerCountChart : public QWidget
{
    Q_OBJECT

public:
    /** 一个采样点：t 为 unix 秒，online 为聚合后的人数（可能是平均值） */
    struct Point
    {
        qint64 t = 0;
        double online = 0.0;
        int maxPlayers = 0;
    };

    explicit PlayerCountChart(QWidget *parent = nullptr);

    /**
     * @brief 传入折线数据
     * @param points 采样点（按时间升序，少于 2 个也能画出一个点）
     * @param fromSec 时间轴起点（unix 秒），0 表示取第一个点
     * @param toSec   时间轴终点（unix 秒），0 表示取最后一个点
     */
    void setPoints(const QVector<Point> &points, qint64 fromSec = 0, qint64 toSec = 0);

    /** 清空数据，回到空态提示 */
    void clear();

    /** 空态文案（默认「暂时还没有人数记录」） */
    void setEmptyText(const QString &text);

    /** 人数上限：大于 0 时画一条虚线并标注「上限 N」 */
    void setCapacity(int capacity);

    /** 是否已经有可绘制的数据 */
    bool hasData() const { return !m_points.isEmpty(); }

    QSize sizeHint() const override { return QSize(420, 168); }
    QSize minimumSizeHint() const override { return QSize(200, 120); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF plotRect() const;
    void updateHover(const QPoint &pos);

    QVector<Point> m_points;
    qint64 m_from = 0;
    qint64 m_to = 0;
    QString m_emptyText;
    int m_capacity = 0;
    int m_hover = -1;
};
