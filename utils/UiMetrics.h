/**
 * @file   UiMetrics.h
 * @brief  全局 UI 布局度量常量（宽度 / 留白 / 间距统一来源）
 * @author BlockBox Team
 * @date   2026-08-29
 *
 * 页面边距、卡片与气泡宽度等同类度量必须从此处取值，
 * 禁止在页面内重新硬编码同名数值，调整全局布局只改本文件。
 */
#ifndef UIMETRICS_H
#define UIMETRICS_H

#include <QScreen>
#include <QSize>
#include <QWidget>
#include <QGuiApplication>

namespace UiMetrics {

/* ---- 布局骨架 ---- */
/** 侧边栏固定宽度 */
static constexpr int kSidebarWidth = 76;
/** 主内容区最大宽度，窗口更宽时内容区水平居中，两侧留白露出背景 */
static constexpr int kMaxContentWidth = 1440;

/* ---- 页面留白 ---- */
/** 页面主布局统一水平内边距（所有页面左右对齐到同一线） */
static constexpr int kPagePadH = 24;
/** 卡片网格容器（滚动区内）叠加的水平内边距 */
static constexpr int kGridPadH = 14;

/* ---- 卡片 ---- */
/** 瀑布流内容卡片 / 任务栏任务卡的固定宽度 */
static constexpr int kCardWidth = 260;
/** 实例概览统计卡片固定为正方形，边长保证各卡片统一；
 *  下限由「数量 + 类型同行」中最宽的文本组合决定 */
static constexpr int kStatCardSize = 124;
/** 自适应卡片（首页最近游玩等）的最小宽度 */
static constexpr int kCardMinWidth = 220;

/* ---- 悬浮气泡 ---- */
/** 悬浮气泡面板统一宽度（子导航气泡与各页内气泡同源） */
static constexpr int kBubbleWidth = 240;
/** 气泡与宿主边缘 / 侧边栏之间的间距 */
static constexpr int kBubbleMargin = 10;

/* ---- 屏幕适配 ---- */
/**
 * @brief 把期望尺寸限制在所在屏幕可用范围内（不含任务栏等系统栏）
 *
 * 界面尺寸按桌面宽屏设计；移动端屏幕小，虽然全局已按 0.6 缩放，
 * 部分宽高很大的弹窗仍可能超出屏幕，这里统一兜底压回屏内，
 * 同时四周预留 8% 边距，保证弹窗与系统栏之间留出呼吸空间。
 */
inline QSize fitToScreen(QWidget *widget, const QSize &desired)
{
    const QScreen *screen = widget ? widget->screen() : nullptr;
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return desired;
    const QSize avail = screen->availableGeometry().size() * 0.92;
    return desired.boundedTo(avail);
}

} // namespace UiMetrics

#endif // UIMETRICS_H
