/**
 * @file   BedrockContentInstaller.h
 * @brief  基岩版附加包安装器
 * @author BlockBox Team
 *
 * 将下载的 .mcpack / .mcaddon / .mcworld / .zip 安装进基岩版实例的 com.mojang 目录。
 *
 * 参考开源项目（BedrockLauncher / BedrockBoot / Add-On 管理器）的安装方式：
 *   - 行为包（AddOn/脚本）→ com.mojang/behavior_packs/ ，并在 development_behavior_packs.json 登记
 *   - 资源包（材质包）   → com.mojang/resource_packs/ ，并在 development_resource_packs.json 登记
 *   - 皮肤包            → com.mojang/skin_packs/
 *   - 地图/世界          → com.mojang/minecraftWorlds/
 *
 * 游戏启动时依据 manifest.json 的 header.uuid + version 从对应 _packs 目录加载，
 * 因此无需覆盖用户的导入历史即可直接生效。
 */

#pragma once

#include <QString>

namespace BedrockContentInstaller {

/** 附加包类型（由 manifest.json modules[].type 判定） */
enum class PackKind
{
    ResourcePack,   // resources → resource_packs
    BehaviorPack,   // data → behavior_packs
    SkinPack,       // skin_pack → skin_packs
    World,          // world_template / .mcworld → minecraftWorlds
    Unknown
};

/**
 * @brief 把下载的附加包安装进指定 com.mojang 目录
 * @param packFile      附加包文件路径（.mcpack / .mcaddon / .mcworld / .zip）
 * @param comMojangDir  基岩版实例的 com.mojang 目录
 * @param errorMessage  失败原因（可选）
 * @return 是否安装成功
 */
bool installPack(const QString &packFile, const QString &comMojangDir, QString *errorMessage = nullptr);

/** 根据文件名后缀粗判类型（.mcworld → World；.mcaddon/.mcpack/.zip → 需解析内部 manifest） */
PackKind classifyFile(const QString &fileName);

} // namespace BedrockContentInstaller
