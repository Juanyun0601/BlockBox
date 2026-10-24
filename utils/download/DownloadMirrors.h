/**
 * @file   DownloadMirrors.h
 * @brief  镜像候选列表构建 — 参考 MultiMC 的多源下载策略
 *
 * MultiMC 的下载动作（Net::Download）不绑定单一地址，而是携带一组候选
 * URL：镜像在前、官方在后，某个地址失败（网络错误 / 哈希不符 / 返回了
 * 网页）时自动换下一个。这里把同样的策略提取成通用工具，供统一下载
 * 引擎与各资源下载流程使用。
 *
 * @author BlockBox Team
 */

#ifndef DOWNLOADMIRRORS_H
#define DOWNLOADMIRRORS_H

#include <QString>
#include <QStringList>

#include "../SettingsManager.h"

namespace DownloadMirrors
{

/** MCIM 镜像是否启用（未设置时默认开启，与 Modrinth API 基址的行为一致） */
inline bool mcimEnabled()
{
    const QVariant v = SettingsManager::instance()->property("use_mcim");
    return !v.isValid() || v.toBool();
}

/** 按已知的镜像重写规则改写 URL，返回镜像地址；无匹配规则时返回空串 */
inline QString rewriteForMirror(const QString &url)
{
    // MCIM（MCMod.cn 镜像）：Modrinth 与 CurseForge 的文件 CDN
    if (url.startsWith(QLatin1String("https://cdn.modrinth.com/")))
        return QString(url).replace(QLatin1String("https://cdn.modrinth.com/"),
                                    QLatin1String("https://mod.mcimirror.top/"));
    if (url.startsWith(QLatin1String("https://edge.forgecdn.net/")))
        return QString(url).replace(QLatin1String("https://edge.forgecdn.net/"),
                                    QLatin1String("https://mod.mcimirror.top/"));

    // BMCLAPI：Mojang / Fabric / Forge 官方资源（与 MultiMC 使用的替换规则一致）
    if (url.startsWith(QLatin1String("https://launchermeta.mojang.com/")))
        return QString(url).replace(QLatin1String("https://launchermeta.mojang.com/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/"));
    if (url.startsWith(QLatin1String("https://piston-meta.mojang.com/")))
        return QString(url).replace(QLatin1String("https://piston-meta.mojang.com/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/"));
    if (url.startsWith(QLatin1String("https://piston-data.mojang.com/v1/objects/")))
        return QString(url).replace(QLatin1String("https://piston-data.mojang.com/v1/objects/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/v1/objects/"));
    if (url.startsWith(QLatin1String("https://launcher.mojang.com/v1/objects/")))
        return QString(url).replace(QLatin1String("https://launcher.mojang.com/v1/objects/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/v1/objects/"));
    if (url.startsWith(QLatin1String("https://resources.download.minecraft.net/")))
        return QString(url).replace(QLatin1String("https://resources.download.minecraft.net/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/assets/"));
    if (url.startsWith(QLatin1String("https://libraries.minecraft.net/")))
        return QString(url).replace(QLatin1String("https://libraries.minecraft.net/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/maven/"));
    if (url.startsWith(QLatin1String("https://maven.fabricmc.net/")))
        return QString(url).replace(QLatin1String("https://maven.fabricmc.net/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/maven/"));
    if (url.startsWith(QLatin1String("https://meta.fabricmc.net/")))
        return QString(url).replace(QLatin1String("https://meta.fabricmc.net/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/fabric-meta/"));
    if (url.startsWith(QLatin1String("https://maven.minecraftforge.net/")))
        return QString(url).replace(QLatin1String("https://maven.minecraftforge.net/"),
                                    QLatin1String("https://bmclapi2.bangbang93.com/maven/"));

    return QString();
}

/**
 * 为一个官方下载地址构建候选列表（MultiMC 式多源回退）。
 *
 * - MCIM 覆盖的地址（Modrinth / CurseForge 文件 CDN）：镜像在前做加速、
 *   官方地址兜底；是否加速跟随 use_mcim 设置（默认开）。
 * - BMCLAPI 覆盖的地址（Mojang / Fabric / Forge 官方资源）：官方在前，
 *   官方失败时追加 BMCLAPI 兜底，不影响现有各流程自己的源选择。
 * - 无匹配规则的地址只返回官方地址本身。
 * 引擎会对每个候选做有限次重试，全部失败才判失败。
 */
inline QStringList expandFileCandidates(const QString &url)
{
    QStringList candidates;
    if (url.isEmpty())
        return candidates;

    const QString mirrored = rewriteForMirror(url);
    if (!mirrored.isEmpty() && mirrored != url)
    {
        if (mirrored.startsWith(QLatin1String("https://mod.mcimirror.top/")))
        {
            if (mcimEnabled())
                candidates << mirrored;
            candidates << url;
        }
        else
        {
            candidates << url << mirrored;
        }
        return candidates;
    }

    candidates << url;
    return candidates;
}

} // namespace DownloadMirrors

#endif // DOWNLOADMIRRORS_H
