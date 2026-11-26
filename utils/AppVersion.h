/**
 * @file   AppVersion.h
 * @brief  启动器自身版本号与发布仓库地址的唯一来源
 * @author BlockBox Team
 * @date   2026-10-05
 *
 * 版本号规则：
 *   正式版：X.Y.Z           （如 1.0.0、2.15.3）
 *   测试版：X.Y.Z-betaN     （如 1.0.0-beta1、1.11.16-beta15）
 *   当前处于初始公测阶段，版本号为 beta10。
 *
 * 每次发布新版本时，只需修改 current() 返回的字符串；
 * 更新检查（UpdateChecker）会拿它与 GitHub Releases 的最新版本做比较，
 * 因此发布 Release 时 tag（或 Release 名称）需与本字符串保持一致。
 *
 * 例外：官方打包脚本（软件发布/一键打包.bat）会通过 qmake 传 APP_VERSION=<版本>
 * 把版本号写死进二进制（宏 BLOCKBOX_BUILD_VERSION），这样打出来的包自报版本
 * 一定和安装包文件名、官网公示清单里的版本号一致，不会出现「装的是 beta9.1、
 * 程序里显示 beta9」这种错位；本地开发不传该参数时仍用下面的默认值。
 */
#ifndef APPVERSION_H
#define APPVERSION_H

#include <QString>

namespace AppVersion {

/** 启动器当前版本号（发布新版本时改这里；官方打包会用 APP_VERSION 覆盖） */
inline QString current()
{
#ifdef BLOCKBOX_BUILD_VERSION
    return QStringLiteral(BLOCKBOX_BUILD_VERSION);
#else
    return QStringLiteral("beta10");
#endif
}

/** GitHub 仓库所有者 */
inline QString repoOwner()
{
    return QStringLiteral("Juanyun0601");
}

/** GitHub 仓库名 */
inline QString repoName()
{
    return QStringLiteral("BlockBox");
}

/**
 * GitHub Releases 列表 API 地址。
 * 注意：不使用 releases/latest —— 它永远不会返回标记为 prerelease 的测试版，
 * 而「抢先升级」通道需要看到测试版，故拉取列表后在本地按升级通道筛选。
 */
inline QString releaseApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/%1/%2/releases?per_page=20")
        .arg(repoOwner(), repoName());
}

/** GitHub Releases 页面地址（用于"前往下载"） */
inline QString releasePageUrl()
{
    return QStringLiteral("https://github.com/%1/%2/releases/latest")
        .arg(repoOwner(), repoName());
}

/**
 * 方块盒子官网更新源（国内优先）。
 * 返回与 GitHub Releases 列表同构的 JSON（tag_name / body / prerelease / assets），
 * 由论坛后台「版本发布」维护，安装包走官网 CDN 直链；
 * 检查更新时先试这个源，超时或暂无数据再回退 GitHub（国际线路）。
 * 若官网论坛域名有变，只需改这里。
 */
inline QString officialReleaseApiUrl()
{
    return QStringLiteral("https://forum.mcblockbox.top/api/app/releases");
}

} // namespace AppVersion

#endif // APPVERSION_H
