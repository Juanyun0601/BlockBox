/**
 * @file   AndroidBridge.h
 * @brief  Android 运行时桥接 — 通过 JNI 调用系统 Intent 的轻量封装
 *
 * 仅在 Q_OS_ANDROID 下有真实实现（QJniObject，Qt 6.4+）；
 * 桌面平台所有接口安全返回 false，调用方无需关心平台差异。
 *
 * 参考 PojavLauncher / FCL (Fold Craft Launcher) 的安卓端实践：
 *  - 启动基岩版：ACTION_MAIN + setPackage("com.mojang.minecraftpe")
 *  - 安装 APK：ACTION_VIEW + FileProvider content:// URI（若宿主
 *    gradle 未引入 androidx.core 则优雅降级返回 false，由调用方
 *    提示用户到文件管理器手动安装）
 */
#pragma once

#include <QString>

namespace AndroidBridge
{
/// 编译期是否为安卓目标（桌面恒为 false）
bool isAndroidRuntime();

/// 系统栏（顶部状态栏 / 底部导航条）当前状态
enum class SystemBarsState
{
    Unknown, ///< 窗口还没拿到 insets，无法判断
    Hidden,  ///< 已沉浸：两条系统栏都不显示
    Visible  ///< 至少有一条系统栏正露在外面
};

/// 隐藏状态栏等系统栏（沉浸式，顶部时间/电量不再显示）；
/// 必须在 Qt 主线程（即 Android UI 线程）调用。返回是否成功下发。
bool applyImmersiveMode();

/// 查询系统栏现在是否还露着（用来判断需不需要重新下发隐藏）
SystemBarsState systemBarsState();

/**
 * @brief 安装沉浸模式维持器：启动时先隐藏一次，之后在窗口显示/重新获得
 *        焦点、从后台切回、旋转屏幕时自动补回，并低频自检系统是否又把
 *        系统栏放了回来。必须在 QApplication 构造之后、Qt 主线程调用。
 *        桌面端为空实现。
 */
void installImmersiveMode();

/**
 * @brief 安卓 11+：仅检查是否已获得"所有文件访问"权限，不跳转
 * @return 当前是否已授权。API 30 以下恒返回 true（走 requestLegacyExternalStorage）。
 */
bool hasAllFilesAccess();

/**
 * @brief 安卓 11+：检查"所有文件访问"权限（MANAGE_EXTERNAL_STORAGE）
 * @return 当前是否已授权。未授权时会跳转本应用的系统授权页，
 *         用户授权返回后由调用方重新进入目录。API 30 以下恒返回 true
 *         （走 requestLegacyExternalStorage）。
 */
bool ensureAllFilesAccess();

/// 通过包名启动已安装应用（ACTION_MAIN / CATEGORY_LAUNCHER / NEW_TASK）
bool launchApp(const QString &packageName);

/// 启动基岩版 Minecraft（com.mojang.minecraftpe）
bool launchBedrock();

/// 调用系统安装器安装 APK；失败（无 FileProvider 等）返回 false
bool installApk(const QString &apkPath);

/// 通过系统分享面板把文件分享给其他应用（用于把导出包发给安卓启动器）
bool shareFile(const QString &filePath, const QString &mimeType);

/// 检测某包名应用是否已安装
bool isAppInstalled(const QString &packageName);

/**
 * @brief 用指定应用打开本地路径（ACTION_VIEW + file:// URI + setPackage）
 * @param packageName 目标应用包名，如 MT 管理器的 mt.sorter
 * @param path        要打开的文件或目录绝对路径
 * @param mimeType    传给目标应用的 MIME，目录一般用 resource/folder
 * @return 未安装、URI 组装失败或 Intent 被拒绝时返回 false，由调用方回退
 */
bool openPathWith(const QString &packageName, const QString &path, const QString &mimeType);

} // namespace AndroidBridge
