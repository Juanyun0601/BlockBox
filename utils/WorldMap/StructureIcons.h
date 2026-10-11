/**
 * @file   StructureIcons.h
 * @brief  结构图标提供器：内存缓存 → 磁盘缓存 → 远程 API（Wiki）→ 内置兜底
 * @author BlockBox Team
 * @date   2026-10-11
 *
 * 两种图标风格（设置>常规「地图结构图标」可选，默认 Icon）：
 *  - Icon     —— 生成结构表中文字旁的 16px 小图标（`EnvSprite_*.png`，
 *                直接下载 `zh.minecraft.wiki/images/<file>`，图标感强）；
 *  - Overview —— 结构条目页面的主图（MediaWiki pageimages 缩略图，
 *                是一张结构概览照片）。
 * 两种风格各自独立落盘缓存（`<key>.icon.png` / `<key>.png`）；离线或请求
 * 失败时回退**内置图标**（按结构首字 + 分散配色生成的圆形徽章）。
 *
 * 所有接口都在 GUI 线程调用；远程请求是异步的，完成后经 `onUpdated` 回调
 * 通知调用方重绘。
 */

#ifndef STRUCTUREICONS_H
#define STRUCTUREICONS_H

#include <QPixmap>
#include <QString>
#include <functional>

namespace StructureIcons {

/** 图标风格（与 SettingsManager::MapStructureIconStyle 对应） */
enum class Style
{
    Icon,     ///< 文字旁小图标（默认）
    Overview  ///< 结构概览缩略图
};

/** 当前风格（默认 Icon） */
Style style();

/**
 * 切换风格：清内存缓存并触发 onUpdated 让调用方重取图标。
 * 与当前值相同时不做任何事。
 */
void setStyle(Style s);

/**
 * 取结构图标。
 * @param structIdx  allStructures() 下标（用于缓存键与内置图配色）
 * @param key        结构稳定标识（缓存文件名）
 * @param firstChar  内置徽章里的字（取显示名首字）
 * @param wikiTitle  Wiki 条目标题（Overview 风格的页面主图）
 * @param itemTitle  Wiki 物品条目标题（更清晰的小图；可空）
 * @param envSprite  生成结构页小图标文件名（Icon 风格；可空则回退 Overview 流程）
 * @param size       边长像素
 * @param onLoaded   远程图标下载完成后（仅首次）触发，调用方用来重绘
 * @return 图标；缓存未命中且未发起请求时先返回内置图
 */
QPixmap pixmap(int structIdx, const QString &key, const QString &firstChar,
               const QString &wikiTitle, const QString &itemTitle,
               const QString &envSprite, int size,
               const std::function<void()> &onLoaded = {});

/** 清内存缓存（切主题或用户强制刷新时用） */
void clearMemoryCache();

/** 设置全局重绘通知（页面析构前须置空，避免回调踩悬空指针） */
void setUpdatedHandler(std::function<void()> handler);

} // namespace StructureIcons

#endif // STRUCTUREICONS_H
