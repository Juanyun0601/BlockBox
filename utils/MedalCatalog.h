/**
 * @file   MedalCatalog.h
 * @brief  账号勋章的本地外观：勋章 id → SVG 图标 / 稀有度配色
 * @date   2026-11-22
 *
 * 勋章的解锁条件、进度和是否已获得都由方块盒子论坛账号决定（服务端统一下发），
 * 本地只负责把服务端返回的 id 换成对应的图标和颜色。这样换账号、换设备看到的
 * 都是同一套勋章，不会出现「本机自己算一套」的情况。
 */
#pragma once

#include <QColor>
#include <QString>

class QPixmap;

namespace MedalCatalog {

/** 一枚勋章在本地需要的外观信息 */
struct Look
{
    QString iconPath;  ///< 资源路径（:/Images/Icons/medals/xxx.svg）
    QColor accent;     ///< 稀有度主色，进度条 / 标签用它
};

/** 取勋章外观；不认识的 id 用「方块新芽」兜底，不会出现空图标 */
Look lookFor(const QString &medalId);

/** 按逻辑尺寸渲染图标（已处理高 DPI 与 2 倍超采样） */
QPixmap pixmapFor(const QString &medalId, int size);

/** 游戏成就的分类图标：原版五大类各有图标，模组成就统一用通用图标 */
QString gameIconPath(const QString &advancementId);

/** 按逻辑尺寸渲染游戏成就分类图标 */
QPixmap gamePixmap(const QString &advancementId, int size);

/** 稀有度文案：common / rare / epic / legendary */
QString rarityText(const QString &rarity);

/** 稀有度主色 */
QColor rarityColor(const QString &rarity);

} // namespace MedalCatalog
