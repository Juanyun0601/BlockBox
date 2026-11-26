/**
 * @file   DshPluginInstaller.h
 * @brief  把「方块盒子桥接」插件装进本机的 DeepSeek Harness（DSH）
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * DSH 的插件是按 profile 装的：每个 profile 目录里有 package.json（依赖 + 启用清单）
 * 和 node_modules。把插件目录放到 node_modules 里、在 package.json 的 dependencies
 * 记一条 link: 依赖、再把包名写进 dsh.profile.bundles，重启 DSH 就会加载。
 *
 * 这里只做文件层面的动作，不依赖 pnpm / node，也不需要 DSH 正在运行：
 *   - 插件本体从 Qt 资源释放到方块盒子数据目录（下次安装可重复覆盖）；
 *   - 安装目标可以是任意 profile 目录，方便自测时用临时目录。
 */
#ifndef BLOCKBOX_DSH_PLUGIN_INSTALLER_H
#define BLOCKBOX_DSH_PLUGIN_INSTALLER_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace DshPluginInstaller {

/** 插件包名，与插件 package.json 的 name、cordis.patch.yml 的行名一致。 */
constexpr const char kPackageName[] = "dsh-blockbox-bridge";

/** 插件中文名，用于界面提示。 */
constexpr const char kDisplayName[] = "方块盒子桥接";

/** 一个 DSH profile 的探测结果。 */
struct Profile
{
    QString name;            ///< profile 名，例如 desktop
    QString dir;             ///< profile 目录绝对路径
    bool installed = false;  ///< 是否已经装过本插件
    QString version;         ///< 已安装插件的版本号
};

/** DSH 数据目录：优先 DSH_HOME，否则 ~/.dsh。 */
QString defaultHome();

/**
 * @brief 扫描 DSH 下的 profile
 * @param home DSH 数据目录；空表示用 defaultHome()
 * @return desktop 排在最前，其余按 package.json 修改时间倒序
 */
QVector<Profile> detectProfiles(const QString &home = QString());

/** 插件需要释放到磁盘的文件（相对路径，与插件包内一致）。 */
QStringList payloadFiles();

/**
 * @brief 把插件文件从 Qt 资源释放到 payloadDir
 * @param payloadDir 目标目录（会被创建，已有文件覆盖）
 * @param errorMessage 失败原因
 * @param log 可选的过程日志
 */
bool writePayload(const QString &payloadDir, QString *errorMessage, QStringList *log = nullptr);

/**
 * @brief 把已经释放好的插件目录装进一个 DSH profile
 * @param profileDir profile 目录（里面要有 package.json）
 * @param payloadDir 插件目录（installInto 会把它复制进 node_modules）
 * @param errorMessage 失败原因
 * @param log 可选的过程日志
 */
bool installInto(const QString &profileDir, const QString &payloadDir,
                 QString *errorMessage, QStringList *log = nullptr);

/** 从 profile 里移除插件（保留 payloadDir，其它依赖和启用项不动）。 */
bool uninstallFrom(const QString &profileDir, QString *errorMessage);

/** 判断 profile 里是否装了本插件；装了就把版本号写进 installedVersion。 */
bool isInstalled(const QString &profileDir, QString *installedVersion = nullptr);

} // namespace DshPluginInstaller

#endif // BLOCKBOX_DSH_PLUGIN_INSTALLER_H
