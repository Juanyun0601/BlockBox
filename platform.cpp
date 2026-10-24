/**
 * @file   platform.cpp
 * @brief  平台适配层实现
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace Platform {

    namespace {

        /**
         * @brief 判断目录是否可写（实际写一个探测文件，避免只看路径存不存在）
         *
         * 安装版默认安装在 C:\Program Files\BlockBox，普通权限运行的程序无法写入该目录。
         * 一旦写不进去，账户、配置、Java 与游戏文件都会静默保存失败（界面上却提示成功）。
         */
        bool directoryWritable(const QString &dir)
        {
            if (dir.isEmpty())
                return false;

            QDir path(dir);
            if (!path.exists() && !path.mkpath(dir))
                return false;

            const QString probePath = dir + QStringLiteral("/.bb_write_test_")
                                      + QString::number(QCoreApplication::applicationPid());
            QFile probe(probePath);
            if (!probe.open(QIODevice::WriteOnly))
                return false;

            const bool ok = (probe.write("bb") == 2);
            probe.close();
            QFile::remove(probePath);
            return ok;
        }

        /// 程序目录是否可写（运行期只算一次）
        bool appDirectoryWritable()
        {
            static const bool writable = directoryWritable(QCoreApplication::applicationDirPath());
            return writable;
        }

        /// 程序目录不可写时使用的数据根目录（用户数据目录下的 BlockBox）
        QString fallbackDataRoot()
        {
            QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
            if (base.isEmpty())
                base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (base.isEmpty())
                base = QDir::homePath();
            return base + QStringLiteral("/BlockBox");
        }

        /**
         * @brief 解析启动器数据目录（<程序目录>/BlockBox，程序目录不可写时退回用户数据目录）
         *
         * 已经存在且本身可写的旧数据目录会被继续沿用，避免老用户"数据失踪"。
         */
        QString resolveDataDir()
        {
            const QString appDir = QCoreApplication::applicationDirPath();
            const QString candidate = appDir + QStringLiteral("/BlockBox");
            if (appDirectoryWritable())
                return candidate;
            if (QDir(candidate).exists() && directoryWritable(candidate))
                return candidate;
            return fallbackDataRoot();
        }

        /// 解析 .minecraft 目录（与数据目录同源，保证可写）
        QString resolveMinecraftDir()
        {
            const QString appDir = QCoreApplication::applicationDirPath();
            const QString candidate = appDir + QStringLiteral("/.minecraft");
            if (appDirectoryWritable())
                return candidate;
            if (QDir(candidate).exists() && directoryWritable(candidate))
                return candidate;
            return resolveDataDir() + QStringLiteral("/.minecraft");
        }

    } // namespace

    // Windows: HKCU 注册表 Run 键；macOS: LaunchAgents plist；Linux: autostart .desktop
    namespace AutoStart {

        static QString windowsRunValue()
        {
            QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                             QSettings::NativeFormat);
            return runKey.value(QStringLiteral("BlockBox")).toString();
        }

        static void windowsSetRunValue(bool enabled)
        {
            QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                             QSettings::NativeFormat);
            if (enabled) {
                const QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
                runKey.setValue(QStringLiteral("BlockBox"), QStringLiteral("\"%1\"").arg(appPath));
            } else {
                runKey.remove(QStringLiteral("BlockBox"));
            }
            runKey.sync();
        }

        static QString macLaunchAgentPath()
        {
            return QDir::home().filePath(QStringLiteral("Library/LaunchAgents/com.BlockBox.BlockBox.plist"));
        }

        static bool macSetLaunchAgent(bool enabled)
        {
            const QString path = macLaunchAgentPath();
            if (enabled) {
                QDir dir(QFileInfo(path).absolutePath());
                if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
                    return false;
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    return false;
                QTextStream stream(&file);
                stream << QStringLiteral(
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                    "<plist version=\"1.0\">\n"
                    "<dict>\n"
                    "    <key>Label</key>\n"
                    "    <string>com.BlockBox.BlockBox</string>\n"
                    "    <key>ProgramArguments</key>\n"
                    "    <array>\n"
                    "        <string>%1</string>\n"
                    "    </array>\n"
                    "    <key>RunAtLoad</key>\n"
                    "    <true/>\n"
                    "</dict>\n"
                    "</plist>\n").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
                file.close();
                return true;
            }
            return QFile::remove(path);
        }

        static QString linuxDesktopFilePath()
        {
            return QDir::home().filePath(QStringLiteral(".config/autostart/blockbox.desktop"));
        }

        static bool linuxSetDesktopFile(bool enabled)
        {
            const QString path = linuxDesktopFilePath();
            if (enabled) {
                QDir dir(QFileInfo(path).absolutePath());
                if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
                    return false;
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    return false;
                QTextStream stream(&file);
                stream << QStringLiteral(
                    "[Desktop Entry]\n"
                    "Type=Application\n"
                    "Name=BlockBox\n"
                    "Exec=%1\n"
                    "Terminal=false\n"
                    "X-GNOME-Autostart-enabled=true\n")
                    .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
                file.close();
                return true;
            }
            return QFile::remove(path);
        }
    }
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
        // 创建BlockBox文件夹用于存放启动器文件
        // 装在 Program Files 等只读位置时退回用户数据目录，否则账户与配置会静默保存失败
        QString blockBoxDir = resolveDataDir();
        QDir blockBoxPath(blockBoxDir);
        if (!blockBoxPath.exists()) {
            blockBoxPath.mkpath(blockBoxDir);
        }

        // 创建.minecraft文件夹用于存放我的世界游戏内容（与数据目录同源，保证可写）
        QString minecraftDir = resolveMinecraftDir();
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
        // 与数据目录保持一致：只读安装目录下改用用户数据目录里的 .minecraft
        return resolveMinecraftDir();
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
        QString bedrockDir = getDataDirectory() + "/BedrockData";
        if (appDirectoryWritable())
            bedrockDir = QCoreApplication::applicationDirPath() + "/BedrockData";
        QDir bedrockPath(bedrockDir);
        if (!bedrockPath.exists()) {
            bedrockPath.mkpath(bedrockDir);
        }
        return bedrockDir;
#endif
    }

    bool isAutoStartEnabled()
    {
#ifdef PLATFORM_WINDOWS
        return !AutoStart::windowsRunValue().isEmpty();
#elif defined(PLATFORM_MAC)
        return QFile::exists(AutoStart::macLaunchAgentPath());
#elif defined(PLATFORM_LINUX)
        return QFile::exists(AutoStart::linuxDesktopFilePath());
#else
        return false;
#endif
    }

    bool setAutoStartEnabled(bool enabled)
    {
#ifdef PLATFORM_WINDOWS
        AutoStart::windowsSetRunValue(enabled);
        return isAutoStartEnabled() == enabled;
#elif defined(PLATFORM_MAC)
        return AutoStart::macSetLaunchAgent(enabled);
#elif defined(PLATFORM_LINUX)
        return AutoStart::linuxSetDesktopFile(enabled);
#else
        Q_UNUSED(enabled);
        return false;
#endif
    }
}