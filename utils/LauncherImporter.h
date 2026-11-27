/**
 * @file   LauncherImporter.h
 * @brief  从主流 Minecraft 启动器导入数据
 *         （桌面：PCL2 / PCL CE / HMCL / MultiMC / Prism / BakaXL / 官方；
 *          安卓：FCL / Zalith Launcher 2 / PojavLauncher / 本机其他游戏目录）
 * @author BlockBox Team
 *
 * 账户兜底：不管哪个平台，只要找到的游戏目录里带着 accounts.json（PCL 格式）或
 * launcher_accounts.json（官方格式），就把里面的账户也一起导进来（同名只留一个）。
 *
 * 安卓端（共享存储根 = /storage/emulated/0，探针可用 BLOCKBOX_ANDROID_STORAGE 覆盖）：
 *   - FCL：数据目录 <根>/FCL，游戏目录 <根>/FCL/.minecraft（FCL 的 SHARED_COMMON_DIR）。
 *     账户（accounts.json）与 Java 运行时都在应用私有目录里，安卓 11 起系统不允许
 *     其他应用读取，所以只能导入游戏目录 / 版本。
 *   - Zalith Launcher 2（ZL2）：默认游戏目录在自身外部私有目录
 *     <根>/Android/data/com.movtery.zalithlauncher/files/.minecraft，安卓 11 起同样读不到；
 *     用户把游戏目录挪到共享存储时，按目录名里的 zalith 认领。
 *     账户存在应用内数据库，同样无法读取。
 *   - PojavLauncher：游戏目录 <根>/games/PojavLauncher/.minecraft（老版本直接在游戏目录里，
 *     两种布局都认），账户同样在应用私有目录。
 *   - 其他目录：共享存储里别的像 .minecraft 的目录（手动拷进来、旧启动器留下的），
 *     只要不归上面几家，就当作可导入来源（最多认领 8 个，避免卡片塞不下）。
 *   另外：任何游戏目录里如果带着 accounts.json（PCL 格式）或 launcher_accounts.json
 *   （官方启动器格式）——常见于把电脑上的 .minecraft 整份拷到手机——会顺带把账户导进来。
 *
 * 各启动器真实数据位置（Windows，均在 Roaming 即 %APPDATA% 下）：
 *   - PCL2 / PCL CE：%APPDATA%\PCL\config.json（JavaList）、便携版 setup.ini；
 *     账户在游戏目录的 accounts.json（Name / TokenType / IsOffline / AuthServer）。
 *   - HMCL：%APPDATA%\.hmcl 或便携版 <启动器目录>\.hmcl
 *       - 新版：config\user-accounts.json、config\user-game-directories.json、config\user-settings.json
 *       - 旧版：accounts.json、hmcl.json（commonpath / configurations.*.gameDir / javaPath / maxMemory）
 *   - MultiMC / Prism：%APPDATA%\MultiMC 或 %APPDATA%\PrismLauncher，instances\<实例>\.minecraft
 *   - BakaXL：%APPDATA%\BakaXL\CoreDirectory.json（directories 的键就是游戏目录）；
 *     账户（BakaAccount.xml）为设备端加密，无法解析，只能导入版本目录。
 */
#ifndef LAUNCHERIMPORTER_H
#define LAUNCHERIMPORTER_H

#include <QList>
#include <QString>
#include <QStringList>

#include "utils/SettingsManager.h"

/** 第三方登录（authlib-injector）服务器 */
struct LauncherAuthServer {
    QString url;  ///< API 根地址，如 https://littleskin.cn/api/yggdrasil/
    QString name; ///< 显示名（能读到启动器里的服务器名就用它，否则取域名）
};

/** 单个游戏目录的摘要（迁移向导用来让用户按目录挑选） */
struct LauncherGameDirInfo {
    QString path;            ///< 游戏目录（.minecraft）
    QStringList versions;    ///< 识别到的版本名
    int modCount = 0;        ///< mods 目录里的文件数（只看一层）
    int saveCount = 0;       ///< saves 目录里的存档数
};

/** 单个启动器的检测结果 */
struct LauncherDetect {
    QString key;              ///< "pcl" / "hmcl" / "multimc" / "baka" / "official"
    QString displayName;      ///< 显示名
    bool detected = false;    ///< 是否检测到该启动器（数据目录或游戏目录存在）
    QString dataDir;          ///< 数据目录（%APPDATA% 下）
    QString minecraftDir;     ///< 关联的 .minecraft 目录（第一个，可能为空）
    QStringList gameDirs;     ///< 关联的全部有效游戏目录（含 versions）
    int accountCount = 0;     ///< 检测到的可导入账户数（-1 表示不可读/已加密）
    int versionCount = 0;     ///< 关联版本数
    QStringList versionNames; ///< 版本名列表（尽量短，用于提示）
    QString javaPath;         ///< 启动器配置的 Java 路径（可能为空）
    QStringList javaPaths;    ///< 启动器里登记的全部 Java（存在且去重）
    QList<LauncherAuthServer> authServers; ///< 第三方登录服务器
    int downloadThreads = -1; ///< 下载线程数（-1 未配置）
    int downloadSource = -1;  ///< 下载源：0 官方 / 1 BMCL 镜像 / -1 未配置
    bool autoMemory = false;      ///< 启动器是否使用自动分配内存
    bool autoMemoryKnown = false; ///< 是否读到了内存分配方式
    int javaMaxMemoryMb = -1; ///< 启动器配置的最大内存（MB，-1 未配置）
    int javaMinMemoryMb = -1; ///< 启动器配置的最小内存（MB，-1 未配置）
    QString detail;           ///< 状态详情文案（中文，直接显示在卡片上）
    QStringList accountNames; ///< 账户预览（名字 + 类型，不用于导入）
    QList<LauncherGameDirInfo> gameDirInfos; ///< 每个游戏目录的版本 / mods / 存档摘要
};

/** 一次导入的结果 */
struct LauncherImportResult {
    bool ok = false;
    int accountCount = 0;      ///< 可导入账户数
    int versionCount = 0;      ///< 关联版本数
    QStringList versionNames;  ///< 版本名列表
    QString minecraftDir;      ///< 关联的 .minecraft 目录（第一个）
    QString javaPath;          ///< 启动器配置的 Java 路径（可能为空）
    QStringList javaPaths;     ///< 启动器里登记的全部 Java
    QList<LauncherAuthServer> authServers; ///< 第三方登录服务器
    int downloadThreads = -1;  ///< 下载线程数（-1 未配置）
    int downloadSource = -1;   ///< 下载源：0 官方 / 1 BMCL 镜像 / -1 未配置
    bool autoMemory = false;      ///< 启动器是否使用自动分配内存
    bool autoMemoryKnown = false; ///< 是否读到了内存分配方式
    int javaMaxMemoryMb = -1;  ///< 启动器配置的最大内存（MB，-1 未配置）
    int javaMinMemoryMb = -1;  ///< 启动器配置的最小内存（MB，-1 未配置）
    QStringList instanceDirs;  ///< 需要注册成实例文件夹的游戏目录
    QString detail;            ///< 结果详情（中文）
    QList<AccountInfo> accounts; ///< 解析到的可导入账户（已做令牌降级）
};

class LauncherImporter
{
public:
    /** 当前平台支持的启动器 key
     *  （安卓：fcl / zl2 / pojav / other；桌面：pcl / hmcl / multimc / baka） */
    static QStringList supportedKeys();

    /** 安卓共享存储根（如 /storage/emulated/0）；非安卓返回空 */
    static QString androidStorageRoot();

    /** 编译目标是否为安卓（桌面版设了 BLOCKBOX_ANDROID_STORAGE 也算，便于探针测试） */
    static bool isAndroidTarget();

    /** 检测全部受支持启动器 */
    static QList<LauncherDetect> detectAll();

    /** 检测单个启动器（供"重新检测"按钮使用） */
    static LauncherDetect detectOne(const QString &key);

    /** 从指定启动器导入（账户 + 游戏目录 + Java + 第三方登录服 + 下载设置 + 内存） */
    static LauncherImportResult importFrom(const QString &key);

    /** 解析指定启动器的账户列表（不写入任何设置，供调用方使用） */
    static QList<AccountInfo> importAccounts(const QString &key);

    /** %APPDATA% 路径（Windows Roaming；可用 BLOCKBOX_APPDATA 覆盖以便调试） */
    static QString appDataDir();

    /** 定位启动器关联的 .minecraft 目录（存在 versions 子目录才返回） */
    static QString findMinecraftDir(const QString &key);

    /** 定位启动器关联的全部游戏目录（存在 versions 子目录才返回） */
    static QStringList findMinecraftDirs(const QString &key);
};

#endif // LAUNCHERIMPORTER_H
