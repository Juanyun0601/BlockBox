/**
 * @file   PublishKit.h
 * @brief  发布中心共用的部件工厂与资源类型定义
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 发布页、发布资源页、平台跳转页共用：
 * 资源类型枚举（模组 / 整合包 / 资源包 / 光影包 / 数据包 / 世界 / 插件 / 投影）、
 * 各类型对应的 CurseForge / Modrinth 发布地址，以及带渐变图标的横向选择卡片。
 * 卡片样式沿用资源页的 resourcesCard 系列对象名，不额外写 QSS。
 */
#pragma once

#include <QColor>
#include <QMetaType>
#include <QPixmap>
#include <QString>

class QWidget;

namespace ForumKit
{
class ClickableCard;
}

namespace PublishKit
{
/** 可发布的资源类型（顺序即「发布资源」页卡片顺序） */
enum class ResourceKind
{
    Mod = 0,      ///< 模组
    Modpack,      ///< 整合包
    ResourcePack, ///< 资源包
    ShaderPack,   ///< 光影包
    DataPack,     ///< 数据包
    World,        ///< 世界
    Plugin,       ///< 插件（走启动器内的插件发帖页）
    Schematic     ///< 投影（走启动器内的投影发帖页）
};

/** 类型显示名：模组 / 整合包 / 资源包 / 光影包 / 数据包 / 世界 / 插件 / 投影 */
QString kindName(ResourceKind kind);

/** 「发布资源」页卡片上的一句话说明 */
QString kindDescription(ResourceKind kind);

/** 类型卡片图标（内置 SVG 路径） */
QString kindIconPath(ResourceKind kind);

/** 类型卡片渐变起始色 / 结束色 */
QColor kindGradientStart(ResourceKind kind);
QColor kindGradientEnd(ResourceKind kind);

/** 该类型在 CurseForge 的发布分类地址 */
QString curseForgeUrl(ResourceKind kind);

/** 该类型在 Modrinth 的发布分类地址；Modrinth 不支持时返回空字符串 */
QString modrinthUrl(ResourceKind kind);

/** 渐变圆角图标块（白色图标 + 柔和阴影） */
QPixmap makeGradientIconPixmap(const QColor &c1, const QColor &c2,
                               const QString &iconPath, int size);

/**
 * @brief 横向选择卡片：渐变图标 + 标题 + 描述 + 右侧箭头
 *
 * 返回 ForumKit::ClickableCard，点击动作挂到 card->onClick。
 * 使用资源页同款对象名，深色/浅色主题与 hover 状态自动跟随。
 */
ForumKit::ClickableCard *makeChoiceCard(QWidget *parent, const QString &title,
                                        const QString &desc, const QColor &grad1,
                                        const QColor &grad2, const QString &iconPath,
                                        int width, int height);

/** 描边区块标题（资源页同款 resourcesSectionTitle） */
QWidget *makeSectionTitle(QWidget *parent, const QString &text);
} // namespace PublishKit

Q_DECLARE_METATYPE(PublishKit::ResourceKind)
