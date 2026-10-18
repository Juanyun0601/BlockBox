/**
 * @file   InstanceBlockNames.h
 * @brief  从游戏实例读取方块中文名
 * @author BlockBox Team
 * @date   2026-10-17
 *
 * 1.19+ 客户端 JAR 不再内置 zh_cn.json，语言文件在实例 assets 索引对应的
 * objects 哈希文件中。本模块按优先级合并：
 *   1) 实例 assets/indexes + objects 的 zh_cn.json
 *   2) 客户端 JAR 内 zh_cn.json（旧版本）
 *   3) mods/*.jar 与 resourcepacks 的 zh_cn（模组/资源包方块）
 * 只收录含中文字符的译名，避免把英文兜底误当成中文。
 */

#pragma once

#include <QHash>
#include <QString>

namespace InstanceBlockNames {

/** 去掉方块状态属性与命名空间：minecraft:oak_log[axis=y] -> oak_log */
QString baseBlockId(const QString &blockId);

/** 按完整方块 ID（大小写不敏感，自动去属性）查中文名；无则返回空 */
QString lookup(const QHash<QString, QString> &cnMap, const QString &blockId);

/**
 * @brief 从实例路径加载方块中文名映射
 * @param instancePath 实例的 versions/<版本> 目录（与材质加载一致）
 * @return 小写 "ns:id" -> 中文名
 */
QHash<QString, QString> loadChineseMap(const QString &instancePath);

} // namespace InstanceBlockNames
