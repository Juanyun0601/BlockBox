/**
 * @file   LoaderVersionDir.h
 * @brief  加载器版本目录整理（Forge / NeoForge 共用）
 * @author BlockBox Team
 * @date   2026-10-25
 */

#pragma once

#include <QJsonObject>
#include <QString>

namespace forge {

/// 兼容两种调用方传入的路径：<root>/versions/<versionId> 形式，或游戏根目录
QString resolveMinecraftRoot(const QString &instancePath);

/// 读取版本目录里的版本 JSON（<目录名>/<目录名>.json，回退 version.json）
QJsonObject readVersionJsonFromDir(const QString &versionDir);

/**
 * @brief 把原版基准目录并进加载器版本目录，versions/ 下只留一个文件夹
 *
 * 规则与 Forge 安装完成后一致：原版 JSON 并进加载器 JSON 并去掉 inheritsFrom，
 * 版本隔离时把原版目录里的 libraries/assets 搬进加载器目录，确认没有其他实例
 * 仍在继承、目录里也没有用户数据后才回收原版目录。
 * 任一前置条件不满足时保持原状（两份目录照旧能启动），不影响安装成败。
 *
 * @param gameRoot        游戏根目录
 * @param mcVersion       原版版本号（原版目录名）
 * @param versionId       加载器版本目录名
 * @param copyVanillaJar  需要在实例目录里补一份 <mc>.jar 时传 true
 */
void consolidateLoaderVersionDir(const QString &gameRoot, const QString &mcVersion,
                                 const QString &versionId, bool copyVanillaJar = false);

} // namespace forge
