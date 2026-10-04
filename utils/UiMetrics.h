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
/** 实例概览统计卡片的固定宽度，保证各卡片宽度统一；
 *  下限由「大小」卡的极端文本决定（如 1023.9 MB ≈117px + 30px 内边距） */
static constexpr int kStatCardWidth = 150;
/** 自适应卡片（首页最近游玩等）的最小宽度 */
static constexpr int kCardMinWidth = 220;

/* ---- 悬浮气泡 ---- */
/** 悬浮气泡面板统一宽度（子导航气泡与各页内气泡同源） */
static constexpr int kBubbleWidth = 240;
/** 气泡与宿主边缘 / 侧边栏之间的间距 */
static constexpr int kBubbleMargin = 10;

} // namespace UiMetrics

#endif // UIMETRICS_H
