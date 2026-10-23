/**
 * @file   InstanceBlockNames.h
 * @brief  方块中文名（实例语言文件 + 内置兜底）与中文输入解析
 * @author BlockBox Team
 * @date   2026-10-17
 *
 * 1.19+ 客户端 JAR 不再内置 zh_cn.json，语言文件在实例 assets 索引对应的
 * objects 哈希文件中。本模块按优先级合并：
 *   1) 实例 assets/indexes + objects 的 zh_cn.json
 *   2) 客户端 JAR 内 zh_cn.json（旧版本）
 *   3) mods 目录下的 jar 与资源包（模组/资源包方块）
 * 只收录含中文字符的译名，避免把英文兜底误当成中文。
 * 实例没有语言文件时（或方块不在实例译名里），回退到内置常见方块中文名。
 */

#pragma once

#include <QHash>
#include <QString>

namespace InstanceBlockNames {

/** 去掉方块状态属性与命名空间：minecraft:oak_log[axis=y] -> oak_log */
QString baseBlockId(const QString &blockId);

/**
 * @brief 按完整方块 ID（大小写不敏感，自动去属性）查中文名
 * 先查实例语言文件，再回退到内置常见方块中文名；都没有则返回空
 */
QString lookup(const QHash<QString, QString> &cnMap, const QString &blockId);

/**
 * @brief 方块状态的中文显示名：中文名 + 状态摘要
 *
 * 如 oak_trapdoor[facing=north,half=bottom,open=false]
 *   -> 橡木活板门（北面·下半·关闭）
 * 未收录的属性按 key=value 原样列出；取不到中文名时回退为英文 ID。
 */
QString displayName(const QHash<QString, QString> &cnMap, const QString &blockId);

/**
 * @brief 把用户输入解析为方块 ID，兼容中文
 *
 * 支持：`minecraft:oak_trapdoor[facing=north]`、`oak_trapdoor`（自动补命名空间）、
 * `中文名 (english_id)`（调色板列表文案）、以及纯中文名 `橡木活板门`。
 * @return 解析出的方块 ID；无法识别时原样返回（去空白后）的输入
 */
QString resolveId(const QHash<QString, QString> &cnMap, const QString &text);

/**
 * @brief 从实例路径加载方块中文名映射
 * @param instancePath 实例的 versions/<版本> 目录（与材质加载一致）
 * @return 小写 "ns:id" -> 中文名
 */
QHash<QString, QString> loadChineseMap(const QString &instancePath);

} // namespace InstanceBlockNames
