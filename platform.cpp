/**
 * @file   platform.cpp
 * @brief  平台适配层实现
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace Platform {
    QString getDataDirectory() {
#ifdef PLATFORM_ANDROID
        // Android: 使用外部存储的Documents目录（应用私有）
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/BlockBox";
        QDir dataPath(dataDir);
        if (!dataPath.exists()) {
            dataPath.mkpath(dataDir);
        }

        // 创建.minecraft文件夹用于存放我的世界游戏内容
        QString minecraftDir = dataDir + "/.minecraft";
        QDir minecraftPath(minecraftDir);
        if (!minecraftPath.exists()) {
            minecraftPath.mkpath(minecraftDir);
        }

        return dataDir;
#else
        // 获取应用程序所在目录
        QString appDir = QCoreApplication::applicationDirPath();
        
        // 创建BlockBox文件夹用于存放启动器文件
        QString blockBoxDir = appDir + "/BlockBox";
        QDir blockBoxPath(blockBoxDir);
        if (!blockBoxPath.exists()) {
            blockBoxPath.mkpath(blockBoxDir);
        }
        
        // 创建.minecraft文件夹用于存放我的世界游戏内容
        QString minecraftDir = appDir + "/.minecraft";
        QDir minecraftPath(minecraftDir);
        if (!minecraftPath.exists()) {
            minecraftPath.mkpath(minecraftDir);
        }
        
        return blockBoxDir;
#endif
    }
    
    QString getConfigFilePath() {
        QString dataDir = getDataDirectory();
        return dataDir + "/config.ini";
    }
    
    // 获取.minecraft目录路径
    QString getMinecraftDirectory() {
#ifdef PLATFORM_ANDROID
        return getDataDirectory() + "/.minecraft";
#else
        QString appDir = QCoreApplication::applicationDirPath();
        return appDir + "/.minecraft";
#endif
    }
    
    // 获取基岩版游戏数据目录（与 Java 版 .minecraft 分离）
    QString getBedrockDataDirectory() {
#ifdef PLATFORM_ANDROID
        QString dataDir = getDataDirectory();
        QString bedrockDir = dataDir + "/BedrockData";
        QDir bedrockPath(bedrockDir);
        if (!bedrockPath.exists()) {
            bedrockPath.mkpath(bedrockDir);
        }
        return bedrockDir;
#else
        QString appDir = QCoreApplication::applicationDirPath();
        QString bedrockDir = appDir + "/BedrockData";
        QDir bedrockPath(bedrockDir);
        if (!bedrockPath.exists()) {
            bedrockPath.mkpath(bedrockDir);
        }
        return bedrockDir;
#endif
    }
}