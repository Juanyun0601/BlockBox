/**
 * @file   TouchScroll.h
 * @brief  手机端触屏滚动辅助（手指拖动 + 惯性滑动，滚动时不误触发点击）
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 桌面端靠鼠标滚轮 / 拖动滚动条，手机上则必须能用手指直接拖动滚动。
 * QScrollArea 默认不响应手指拖动；而 QScroller 的两种手势在本应用里都
 * 不可用——顶层窗口不接收触摸事件，系统把触摸合成成鼠标事件分发，
 * TouchGesture 收不到手势，LeftMouseButtonGesture 又会被自己吃按下
 * 事件的卡片/按钮挡住（真机与模拟器实测均如此）。
 *
 * 所以这里采用 Qt 社区经典的"FlickCharm"方案（Ariya Hidayat 的动能
 * 滚动过滤器的思路）自己实现：应用级事件过滤器监视滚动区里的鼠标
 * 事件流（安卓上全部来自触摸合成）——
 *   1. 按下：照常透传给子控件，轻点才能点击；
 *   2. 手指移动超过阈值：进入拖动模式，此后鼠标事件一律吞掉，
 *      并按手指位移直接滚动内容（滚动中绝不产生点击）；
 *   3. 松手：吞掉点击，按最后速度继续惯性滑动、逐渐衰减；
 *   4. 惯性期间手指再按下：立即停止惯性，本次轻点照常点击。
 */
#ifndef TOUCHSCROLL_H
#define TOUCHSCROLL_H

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include "platform.h"

namespace TouchScroll {

namespace detail {

// 起拖阈值（逻辑像素）：超过它才算拖动，以内都按轻点放行
static constexpr qreal kDragThreshold = 14.0;

/* ============================================================
 * FlickScrollEngine — 手机端拖动滚动 + 防误点引擎（应用级事件过滤器）
 *
 * 监视落在已注册滚动区（含全部子控件）里的鼠标事件流：
 *   - 按下：记录起点，照常放行（轻点必须能点）；
 *   - 移动超过起拖阈值：进入拖动模式，之后移动/松手全部吞掉，
 *     按手指位移滚动内容，滚动中不可能误点任何子控件；
 *   - 松手：按收尾速度启动惯性滑动，逐渐衰减到停止；
 *   - 惯性中按下：立即停住，不影响下一次轻点。
 * ============================================================ */
class FlickScrollEngine : public QObject
{
public:
    /// 把滚动区视口纳入拖动滚动范围（首次调用时自动挂上全局过滤器）
    static void watch(QWidget *viewport)
    {
        if (viewport)
            instance()->registerViewport(viewport);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();

        // 自注册：手机端任何 QScrollArea 显示出来（含异步创建的晚到滚动区、
        // 各级弹窗里的滚动区）都自动纳入拖动滚动，不依赖页面切换时机
        maybeAutoRegister(watched, type);

        if (type == QEvent::MouseButtonPress || type == QEvent::MouseMove
            || type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick) {
            QWidget *viewport = viewportFor(watched);
            if (viewport)
                return handleMouse(viewport, watched, static_cast<QMouseEvent *>(event), type);
            // 滚动区之外的事件（滚动条拖动、弹窗等）一律不碰
            return QObject::eventFilter(watched, event);
        }

        return QObject::eventFilter(watched, event);
    }

private:
    /// watched 是某个 QScrollArea（或其视口）时，把它的视口注册进来
    void maybeAutoRegister(QObject *watched, QEvent::Type type)
    {
        if (type != QEvent::Show && type != QEvent::Polish && type != QEvent::MouseButtonPress)
            return;
        if (auto *area = qobject_cast<QScrollArea *>(watched)) {
            registerViewport(area->viewport());
            return;
        }
        if (auto *widget = qobject_cast<QWidget *>(watched)) {
            if (qobject_cast<QScrollArea *>(widget->parentWidget()))
                registerViewport(widget); // 视口自身的事件：父级就是滚动区
        }
    }

    void registerViewport(QWidget *viewport)
    {
        if (!viewport || m_viewports.contains(viewport))
            return;
        m_viewports.insert(viewport);
        // 视口销毁时移出集合；进行中的拖动手势不中断（异步加载内容的页面
        // 会在拖动途中重建滚动区，松手仍要吞掉，否则会误点新列表的卡片）
        QObject::connect(viewport, &QObject::destroyed, this, [this](QObject *obj) {
            m_viewports.remove(static_cast<QWidget *>(obj));
            if (m_activeViewport == obj)
                m_activeViewport.clear();
        });
    }
    static FlickScrollEngine *instance()
    {
        static FlickScrollEngine *s_engine = nullptr;
        if (!s_engine) {
            s_engine = new FlickScrollEngine(QCoreApplication::instance());
            if (QCoreApplication::instance())
                QCoreApplication::instance()->installEventFilter(s_engine);
        }
        return s_engine;
    }

    explicit FlickScrollEngine(QObject *parent)
        : QObject(parent)
        , m_kineticTimer(new QTimer(this))
    {
        // 惯性滑动节拍：约 60fps，每拍按当前速度滚一格并衰减
        m_kineticTimer->setInterval(16);
        connect(m_kineticTimer, &QTimer::timeout, this, [this] {
            if (m_activeViewport.isNull())
                return;
            QScrollBar *bar = verticalBar(m_activeViewport);
            if (!bar) {
                stopKinetic();
                return;
            }
            const int step = qRound(m_velocity * m_kineticTimer->interval());
            bar->setValue(bar->value() + step);
            m_velocity *= 0.93; // 每拍衰减
            if (qAbs(m_velocity) < 0.02 || step == 0
                || bar->value() == bar->minimum() || bar->value() == bar->maximum())
                stopKinetic();
        });
    }

    /// watched（或其祖先链）是否落在被保护的滚动区里；是则返回该视口
    QWidget *viewportFor(QObject *watched) const
    {
        QObject *node = watched;
        for (int depth = 0; node && depth < 64; ++depth, node = node->parent()) {
            if (node->isWidgetType() && m_viewports.contains(static_cast<QWidget *>(node)))
                return static_cast<QWidget *>(node);
        }
        return nullptr;
    }

    static QScrollBar *verticalBar(QWidget *viewport)
    {
        auto *area = qobject_cast<QAbstractScrollArea *>(viewport->parentWidget());
        return area ? area->verticalScrollBar() : nullptr;
    }

    bool handleMouse(QWidget *viewport, QObject *watched, QMouseEvent *event, QEvent::Type type)
    {
        const QPointF global = event->globalPosition();

        if (type == QEvent::MouseButtonPress) {
            stopKinetic();
            // 滑块/滚动条等需要按住拖动的控件不劫持，保持原生交互
            if (qobject_cast<QAbstractSlider *>(watched)) {
                m_pressActive = false;
                m_dragging = false;
                return QObject::eventFilter(watched, event);
            }
            // 控件声明了 flickScrollExempt（按住拖动是它自己的交互，
            // 例如人机验证的自绘滑块）：同样不劫持，按下/移动/松手全放行
            for (QObject *node = watched; node; node = node->parent()) {
                if (node->isWidgetType()
                    && static_cast<QWidget *>(node)->property("flickScrollExempt").toBool()) {
                    m_pressActive = false;
                    m_dragging = false;
                    return QObject::eventFilter(watched, event);
                }
            }
            m_pressActive = true;
            m_dragging = false;
            m_activeViewport = viewport;
            m_pressGlobal = global;
            m_lastGlobal = global;
            m_velocity = 0.0;
            m_moveClock.restart();
            return QObject::eventFilter(watched, event); // 按下照常透传
        }

        // 松手后指针移出滚动区的移动、其他手势的杂散事件：一律不碰
        if (!m_pressActive)
            return QObject::eventFilter(watched, event);
        // 事件落在其他滚动区里（视口在拖动途中被异步内容重建时会发生）：
        // 换用新视口继续这次拖动
        if (viewport)
            m_activeViewport = viewport;

        if (type == QEvent::MouseMove) {
            if (!m_dragging) {
                const QPointF delta = global - m_pressGlobal;
                if (QPointF::dotProduct(delta, delta) < kDragThreshold * kDragThreshold)
                    return QObject::eventFilter(watched, event); // 还在轻点范围内
                m_dragging = true; // 超过起拖阈值：开始劫持，本次手势不再有点击
                m_lastGlobal = global;
                m_moveClock.restart();
            }
            const qreal dy = m_lastGlobal.y() - global.y(); // 上滑为正 → 内容上移
            const int elapsed = qMax(1, int(m_moveClock.elapsed()));
            m_velocity = 0.65 * m_velocity + 0.35 * (dy / elapsed);
            m_lastGlobal = global;
            m_moveClock.restart();
            if (!m_activeViewport.isNull())
                if (QScrollBar *bar = verticalBar(m_activeViewport))
                    bar->setValue(bar->value() + qRound(dy));
            return true; // 吞掉移动：子控件不该感受到拖动
        }

        if (type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick) {
            const bool wasDragging = m_dragging;
            const qreal releaseVelocity = m_velocity;
            m_pressActive = false;
            m_dragging = false;
            if (wasDragging) {
                // 拖动后的松手不当成点击；带速度就顺势进入惯性滑动
                if (auto *button = qobject_cast<QAbstractButton *>(watched)) {
                    if (button->isDown())
                        button->setDown(false);
                }
                if (type == QEvent::MouseButtonRelease && !m_activeViewport.isNull()
                    && qAbs(releaseVelocity) >= 0.02) {
                    m_velocity = releaseVelocity;
                    m_kineticTimer->start();
                }
                return true;
            }
            return QObject::eventFilter(watched, event); // 轻点：照常产生点击
        }

        return QObject::eventFilter(watched, event);
    }

    void stopKinetic()
    {
        m_kineticTimer->stop();
        m_velocity = 0.0;
    }

    QSet<QWidget *> m_viewports;
    QPointer<QWidget> m_activeViewport; // 当前拖动/惯性作用的目标视口
    bool m_pressActive = false;         // 有一次按下还在滚动区里进行中
    bool m_dragging = false;            // 已越过起拖阈值：本次手势吞掉全部点击
    QPointF m_pressGlobal;
    QPointF m_lastGlobal;
    qreal m_velocity = 0.0;             // 纵向速度，像素/毫秒
    QElapsedTimer m_moveClock;          // 相邻两次移动的时间差
    QTimer *m_kineticTimer = nullptr;
};

/**
 * @brief 给滚动区开启手指拖动滚动（带惯性），滚动时不误触发点击
 *
 * 仅手机端生效，桌面端保持滚轮 / 滚动条的原行为；轻点子控件仍然正常点击。
 */
inline void applyDragScroll(QAbstractScrollArea *area)
{
    if (!area)
        return;

    QWidget *viewport = area->viewport();
    if (!viewport)
        return;

    FlickScrollEngine::watch(viewport);
}

} // namespace detail

/**
 * @brief 让一个页面 / 容器里的所有内容滚动区都支持手指拖动滚动（手机端）
 *
 * 只处理页面正文用的 QScrollArea；文本框、列表、表格等自带滚动的控件
 * 保持原生行为不动。已处理过的不重复处理。返回本次新开启的数量。
 */
inline int enablePageDragScroll(QWidget *root)
{
    if (!root || !Platform::isMobile())
        return 0;

    int enabled = 0;
    const QList<QScrollArea *> areas = root->findChildren<QScrollArea *>();
    for (QScrollArea *area : areas) {
        if (!area || area->property("touchDragScrollEnabled").toBool())
            continue;
        area->setProperty("touchDragScrollEnabled", true);
        detail::applyDragScroll(area);
        ++enabled;
    }
    return enabled;
}

/**
 * @brief 让滚动区支持手指上下拖动滚动（松手后带惯性滑动），滚动时不误触发点击
 *
 * 仅手机端生效，桌面端保持滚轮 / 滚动条的原行为；轻点子控件仍然正常点击。
 */
inline void enableDragScroll(QAbstractScrollArea *area)
{
    if (!area || !Platform::isMobile())
        return;
    detail::applyDragScroll(area);
}

} // namespace TouchScroll

#endif // TOUCHSCROLL_H
