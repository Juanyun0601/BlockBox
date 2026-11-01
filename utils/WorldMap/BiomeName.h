/**
 * @file   BiomeName.h
 * @brief  从当前实例读取群系 / 物品中文名（lang 文件）
 * @author BlockBox Team
 * @date   2026-10-17
 *
 * 存档地图「当前群系」与结构战利品物品名的数据源：读当前实例客户端 JAR 的
 * assets/minecraft/lang/zh_cn.json 与 en_us.json，把 biome.* / item.* 翻译键
 * 反解为「资源名 ↔ 显示名」映射（中文优先，英文兜底）。
 *
 * 线程模型：loadFromInstance() 为纯 IO 纯函数，应放后台线程执行。
 */

#ifndef BIOMENAME_H
#define BIOMENAME_H

#include <QHash>
#include <QString>

namespace BiomeName {

/** 实例 lang 解析结果：zh_cn.json 与 en_us.json 的群系 / 物品名分表存放 */
struct InstanceNames
{
    /** "minecraft:plains" -> "平原"（zh_cn.json biome.*，可能为空） */
    QHash<QString, QString> zh;
    /** "minecraft:plains" -> "Plains"（en_us.json biome.*，可能为空） */
    QHash<QString, QString> en;
    /** "minecraft:diamond" -> "钻石"（zh_cn.json item.*，可能为空） */
    QHash<QString, QString> itemsZh;
    /** "minecraft:diamond" -> "Diamond"（en_us.json item.*，可能为空） */
    QHash<QString, QString> itemsEn;
};

/**
 * @brief 定位实例的客户端 JAR
 *
 * 支持 BlockBox / HMCL / PCL 常见布局：
 *  - versions/<id>/ 下与目录名同名的 <id>.jar（或 JSON id 命名）
 *  - 目录内最大的 jar（PCL/HMCL 风格）
 *  - 实例路径即游戏根目录时，versions 子目录里的同名 jar
 *  - inheritsFrom 原版父版本目录
 * 找不到返回空串。
 */
QString findClientJar(const QString &instancePath);

/**
 * @brief 解析实例的群系名表
 *
 * 读客户端 JAR 的 assets/minecraft/lang/zh_cn.json 与 en_us.json，
 * 提取 biome.<ns>.<id> 键。找不到 JAR / 没有 biome 键时返回空结构。
 * 后台线程调用。
 */
InstanceNames loadFromInstance(const QString &instancePath);

/**
 * @brief 群系资源名（biome2str 输出，如 "plains"）→ 显示名
 *
 * 优先级：实例 zh_cn → 内置常见群系表（BiomeCatalog，与原版 biome2str
 * 命名一致）→ 实例 en_us → 英文资源名兜底。
 */
QString chineseName(const QString &resourceName,
                    const InstanceNames &instanceNames);

/**
 * @brief 群系 id → 显示名（随存档版本的资源命名）
 *
 * 内部走 biome2str(mc, id) → chineseName。mc 为 versionToInt 的结果。
 * 算不出资源名时返回空串（调用方自行显示占位）。
 */
QString nameForId(int mc, int id, const InstanceNames &instanceNames);

} // namespace BiomeName

#endif // BIOMENAME_H
