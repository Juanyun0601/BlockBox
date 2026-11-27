/**
 * @file   ZcodePluginInstaller.h
 * @brief  把「方块盒子桥接」插件装进本机的 ZCode
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * ZCode 的插件按用户级目录装：<home>/cli/plugins/ 下有三处：
 *   - marketplaces/<marketId>/marketplace.json  市场登记；
 *   - cache/<marketId>/<pluginName>/<version>/  插件文件本体；
 *   - installed_plugins.json                    安装记录清单。
 * 三处写齐后重启 ZCode 就会加载本插件。
 *
 * 这里只做文件层面的动作，不需要 ZCode 正在运行：
 *   - 插件本体从 Qt 资源释放到方块盒子数据目录（下次安装可重复覆盖）；
 *   - 安装目标是 <home>/cli/plugins 目录，方便自测时用临时目录。
 */
#ifndef BLOCKBOX_ZCODE_PLUGIN_INSTALLER_H
#define BLOCKBOX_ZCODE_PLUGIN_INSTALLER_H

#include <QString>
#include <QStringList>

namespace ZcodePluginInstaller {

/** 插件包名，与插件 plugin.json 的 name 一致。 */
constexpr const char kPackageName[] = "zcode-blockbox-plugin";

/** 插件中文名，用于界面提示。 */
constexpr const char kDisplayName[] = "方块盒子桥接";

/** 装进 ZCode 时使用的市场 id。 */
constexpr const char kMarketId[] = "blockbox-bridge";

/** 插件版本号，与插件 plugin.json 的 version 一致。 */
constexpr const char kVersion[] = "0.1.0";

/** 一个 ZCode 安装位置的探测结果。 */
struct Installation
{
    QString home;            ///< ZCode 数据根目录，例如 ~/.zcode
    QString pluginsDir;      ///< <home>/cli/plugins 绝对路径
    bool installed = false;  ///< 是否已经装过本插件
    QString version;         ///< 已安装插件的版本号
};

/** ZCode 数据目录：优先 ZCODE_CLI_HOME，否则 ~/.zcode。 */
QString defaultHome();

/**
 * @brief 探测本机 ZCode 的插件目录
 * @param home ZCode 数据目录；空表示用 defaultHome()
 * @return 目录存在才有结果；installed/version 指本插件的状态
 */
Installation detectInstallation(const QString &home = QString());

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
 * @brief 把已经释放好的插件目录装进一个 ZCode 插件目录
 * @param pluginsDir <home>/cli/plugins 目录（里面应有 installed_plugins.json，可缺）
 * @param payloadDir 插件目录（会复制进 cache/<marketId>/<pkg>/<version>）
 * @param errorMessage 失败原因
 * @param log 可选的过程日志
 */
bool installInto(const QString &pluginsDir, const QString &payloadDir,
                 QString *errorMessage, QStringList *log = nullptr);

/** 从 ZCode 里移除插件（插件文件、安装记录、市场条目一起清掉）。 */
bool uninstallFrom(const QString &pluginsDir, QString *errorMessage);

/** 判断 ZCode 里是否装了本插件；装了就把版本号写进 installedVersion。 */
bool isInstalled(const QString &pluginsDir, QString *installedVersion = nullptr);

} // namespace ZcodePluginInstaller

#endif // BLOCKBOX_ZCODE_PLUGIN_INSTALLER_H
