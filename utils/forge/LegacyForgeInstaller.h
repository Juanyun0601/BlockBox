/**
 * @file   LegacyForgeInstaller.h
 * @brief  老版本Forge（MC 1.5.2 及更早）的下载/合并逻辑 — 移植自 MultiMC
 *         （LegacyUpdate / VersionFilterData / MMCZip::createModdedJar，Apache-2.0）
 * @author BlockBox Team
 * @date   2026-10-25
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QJsonObject;

namespace legacyforge {

/// FML 运行期依赖库（MultiMC VersionFilterData 的 fmlLibsMapping 条目）
struct FmlLib
{
    QString filename;   ///< 库文件名（如 argo-small-3.2.jar）
    QString sha1;       ///< MultiMC 记录的 SHA1
    bool multimcHosted; ///< true = 仅 MultiMC 官方镜像可靠，Forge 官方源没有
};

/// MC ≤ 1.5.2 走经典安装：Forge 只有 universal/client 压缩包、没有可用的
/// 无头安装器（MultiMC 的 forgeInstallerBlacklist 同样把 1.5.2 拉黑）
bool isLegacyMcVersion(const QString &mcVersion);

/// 该 MC 版本需要的 FML 依赖库清单（1.3.2 起，更早版本返回空）
QList<FmlLib> fmlLibsFor(const QString &mcVersion);

/// Forge universal/client 压缩包候选地址（官方 Maven 与 BMCLAPI 镜像，首选源在前）
QStringList forgeZipCandidateUrls(const QString &mcVersion, const QString &forgeVersion,
                                  bool preferBmcl);

/// FML 依赖库候选地址（files.multimc.org 镜像已验证可用；
/// files.minecraftforge.net 的 /fmllibs 目录已下线，仅作回退）
QStringList fmlLibCandidateUrls(const FmlLib &lib);

/// 合并逻辑移植自 MultiMC MMCZip::createModdedJar（单 jarMod 场景）：
/// Forge 包条目先写入（与原版同名时 Forge 胜出），再并入原版 jar 的其余条目，
/// 并剔除原版带的 META-INF（去除 Mojang 签名，老版 Forge 注入的必要步骤）
bool mergeForgeIntoJar(const QString &forgeZipPath, const QString &vanillaJarPath,
                       const QString &outJarPath, QString *errorOut);

/// 生成经典 Forge 的子版本 JSON：
/// id 为 "<mc>-forge-<forge>"，inheritsFrom 指回原版（主类/启动参数/资源由原版提供），
/// FML 库通过 downloads.artifact.path = fmllibs/<文件名> 挂进 classpath
QJsonObject buildLegacyVersionJson(const QString &mcVersion, const QString &forgeVersion,
                                   const QList<FmlLib> &fmlLibs);

} // namespace legacyforge
