/**
 * @file   platform.h
 * @brief  平台适配层头文件
 * @author BlockBox Team
 * @date   2026-05-09
 */

#pragma once

#include <QtGlobal>
#include <QString>

// 平台检测宏
// 注意判断顺序：Qt 在安卓 / 鸿蒙上会同时定义 Q_OS_LINUX（见 qsystemdetection.h），
// 若把 Q_OS_LINUX 放在前面，安卓和鸿蒙都会被识别成 Linux，PLATFORM_ANDROID /
// PLATFORM_HARMONY 永远不会生效（安卓端「安装新实例无法写入文件夹」的根因之一：
// 安卓专用目录逻辑全是死代码，默认实例目录落到公共存储）。必须先去重再按特例判断。
#ifdef Q_OS_WIN
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_NAME "Windows"
#elif defined(Q_OS_MACOS)
    #define PLATFORM_MAC 1
    #define PLATFORM_NAME "macOS"
#elif defined(Q_OS_ANDROID) || defined(ANDROID_OS)
    #define PLATFORM_ANDROID 1
    #define PLATFORM_NAME "Android"
#elif defined(HARMONY_OS) || defined(Q_OS_HARMONY)
    #define PLATFORM_HARMONY 1
    #define PLATFORM_NAME "HarmonyOS"
#elif defined(Q_OS_LINUX)
    #define PLATFORM_LINUX 1
    #define PLATFORM_NAME "Linux"
#else
    #define PLATFORM_UNKNOWN 1
    #define PLATFORM_NAME "Unknown"
#endif

// 平台适配函数
namespace Platform {
    // 获取平台名称
    inline QString getPlatformName() {
        return QString(PLATFORM_NAME);
    }
    
    // 检查是否为Windows平台
    inline bool isWindows() {
#ifdef PLATFORM_WINDOWS
        return true;
#else
        return false;
#endif
    }
    
    // 检查是否为macOS平台
    inline bool isMac() {
#ifdef PLATFORM_MAC
        return true;
#else
        return false;
#endif
    }
    
    // 检查是否为Linux平台
    inline bool isLinux() {
#ifdef PLATFORM_LINUX
        return true;
#else
        return false;
#endif
    }
    
    // 检查是否为HarmonyOS平台
    inline bool isHarmony() {
#ifdef PLATFORM_HARMONY
        return true;
#else
        return false;
#endif
    }

    // 检查是否为Android平台
    inline bool isAndroid() {
#ifdef PLATFORM_ANDROID
        return true;
#else
        return false;
#endif
    }

    // 设备形态：桌面（键鼠、可自由调整大小的窗口）或移动（手机 / 平板触屏）
    enum class FormFactor { Desktop, Mobile };

    // 运行期设备形态判定（结果缓存）。
    // 鸿蒙端同时覆盖电脑（2in1 设备）与手机 / 平板：通过系统 deviceInfo.deviceType
    // 区分——phone/tablet 为移动端，2in1/pc 为桌面端；查询失败时按手机处理
    //（保持既有触屏表现）。其余平台编译期即可确定。
    // 可用环境变量 BLOCKBOX_FORM_FACTOR=mobile/desktop 强制指定（测试排障用）。
    FormFactor getFormFactor();

    // 检查是否为移动端形态（手机 / 平板）
    // 注意：不能依赖上面的 PLATFORM_xxx 宏。Android 构建同时定义 Q_OS_LINUX，
    // 会在上方被判定为 Linux；这里直接按 Qt / 工程宏判断。
    // 鸿蒙不在此列：它同时包含电脑与手机两种形态，须运行期查询（见 getFormFactor）。
    inline bool isMobile() {
#if defined(Q_OS_ANDROID) || defined(ANDROID_OS)
        return true;
#elif defined(HARMONY_OS) || defined(Q_OS_HARMONY)
        return getFormFactor() == FormFactor::Mobile;
#else
        return false;
#endif
    }

    // 获取应用数据目录
    QString getDataDirectory();

    // 目录是否可写：会实际创建目录并写入一个探针文件。
    // 用于「安装新实例」前的检查，以及纠正指向只读位置的旧默认实例目录；
    // 只看路径是否存在无法判断 Program Files、安卓程序目录这类只读位置。
    bool isDirectoryWritable(const QString &dir);
    
    // 获取配置文件路径
    QString getConfigFilePath();
    
    // 获取.minecraft目录路径（Java 版游戏数据）
    QString getMinecraftDirectory();

    // 获取基岩版游戏数据目录（与 Java 版 .minecraft 分离）
    QString getBedrockDataDirectory();

    // 开机自启动：查询当前是否已开启（Windows 查注册表 Run 键，
    // macOS 查 LaunchAgents，Linux 查 ~/.config/autostart）
    bool isAutoStartEnabled();

    // 开启/关闭开机自启动，返回是否成功
    bool setAutoStartEnabled(bool enabled);
}