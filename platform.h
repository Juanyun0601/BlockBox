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
#ifdef Q_OS_WIN
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_NAME "Windows"
#elif defined(Q_OS_MACOS)
    #define PLATFORM_MAC 1
    #define PLATFORM_NAME "macOS"
#elif defined(Q_OS_LINUX)
    #define PLATFORM_LINUX 1
    #define PLATFORM_NAME "Linux"
#elif defined(Q_OS_ANDROID)
    #define PLATFORM_ANDROID 1
    #define PLATFORM_NAME "Android"
#elif defined(HARMONY_OS)
    #define PLATFORM_HARMONY 1
    #define PLATFORM_NAME "HarmonyOS"
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

    // 获取应用数据目录
    QString getDataDirectory();
    
    // 获取配置文件路径
    QString getConfigFilePath();
    
    // 获取.minecraft目录路径（Java 版游戏数据）
    QString getMinecraftDirectory();

    // 获取基岩版游戏数据目录（与 Java 版 .minecraft 分离）
    QString getBedrockDataDirectory();
}