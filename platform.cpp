/**
 * @file   platform.cpp
 * @brief  平台适配层实现
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "platform.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#if defined(HARMONY_OS) || defined(Q_OS_HARMONY)
// Qt for OpenHarmony 的 JS 线程通道与 NAPI 封装（QtCore 私有接口）。
// 与 utils/HarmonyBridge.cpp 相同的次序：先引入 <napi.h> 让 NAPI_VERSION 取
// SDK 兼容值，再引私有头，避免重复展开时踩到本 SDK 未提供的实验性符号。
#include <napi.h>
#include <napi/native_api.h>
#include <QtCore/private/qcore_ohos_p.h>
#endif

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
            if (!path.exists() && !path.mkpath(dir)) {
                qWarning() << "[Platform] 目录不存在且无法创建：" << dir;
                return false;
            }

            const QString probePath = dir + QStringLiteral("/.bb_write_test_")
                                      + QString::number(QCoreApplication::applicationPid());
            QFile probe(probePath);
            if (!probe.open(QIODevice::WriteOnly)) {
                // 安卓上这里最常见的两种原因：公共目录没给「所有文件访问」权限，
                // 或外部存储被卸载 / 挂载为只读
                qWarning() << "[Platform] 目录不可写：" << dir << "原因：" << probe.errorString();
                return false;
            }

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

#ifdef PLATFORM_ANDROID
        /**
         * @brief 安卓端启动器数据根目录：优先沿用外部应用目录，不可写时退回内部目录
         *
         * 外部存储（/storage/emulated/0/Android/data/<包名>/files）不是每台设备、
         * 每个时刻都可用（未挂载、被系统清理、权限异常）。旧实现只拼外部路径，
         * 一旦不可写，新实例从建目录起就全部失败；内部目录
         * （/data/user/0/<包名>/files）始终可写，作为兜底。
         * 已经存在且可写的外部目录优先沿用，避免老用户数据“失踪”。
         */
        QString androidWritableDataRoot()
        {
            static const QString cached = []() {
                QStringList candidates;
                const QString external =
                    QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
                if (!external.isEmpty())
                    candidates << external + QStringLiteral("/BlockBox");
                const QString internal =
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                if (!internal.isEmpty())
                    candidates << internal + QStringLiteral("/BlockBox");

                for (const QString &candidate : candidates) {
                    if (directoryWritable(candidate))
                        return candidate;
                }
                if (!candidates.isEmpty())
                    return candidates.first();
                return QDir::homePath() + QStringLiteral("/BlockBox");
            }();
            return cached;
        }
#endif

    } // namespace

    bool isDirectoryWritable(const QString &dir)
    {
        const QString trimmed = dir.trimmed();
        if (trimmed.isEmpty())
            return false;
        return directoryWritable(QDir::cleanPath(trimmed));
    }

    FormFactor getFormFactor()
    {
#if defined(Q_OS_ANDROID) || defined(ANDROID_OS)
        return FormFactor::Mobile;
#elif defined(HARMONY_OS) || defined(Q_OS_HARMONY)
        static const FormFactor cached = []() {
            // 测试 / 排障：允许强制指定形态
            const QByteArray forced = qgetenv("BLOCKBOX_FORM_FACTOR");
            if (forced.compare("mobile", Qt::CaseInsensitive) == 0)
                return FormFactor::Mobile;
            if (forced.compare("desktop", Qt::CaseInsensitive) == 0)
                return FormFactor::Desktop;

            // 查询系统设备形态（等价于 ArkTS 侧 import deviceInfo from '@ohos.deviceInfo'）
            // phone / tablet → 移动端；2in1 / pc → 电脑端。
            // 必须在 JS 线程上调用 NAPI，借 QOhosJsThreadGateway 阻塞等待结果；
            // 网关在 Qt 主线程跑起来之前就可用（QStandardPaths 鸿蒙实现同样依赖它）。
            QString deviceType;
            try {
                deviceType = QOhosJsThreadGateway::eval(
                    [](QOhosJsState &js) -> QString {
                        try {
                            Napi::Env env(js.env());
                            Napi::HandleScope scope(env);

                            napi_value moduleValue = nullptr;
                            if (napi_load_module(env, "@ohos.deviceInfo", &moduleValue) != napi_ok
                                || moduleValue == nullptr)
                                return {};
                            Napi::Value value = Napi::Object(env, moduleValue).Get("deviceType");
                            if (!value.IsString())
                                return {};
                            std::string type = value.As<Napi::String>().Utf8Value();
                            return QString::fromStdString(type);
                        } catch (...) {
                            // NAPI 抛出的异常必须清掉，否则会污染后续 JS 调用
                            Napi::Env(js.env()).GetAndClearPendingException();
                            return {};
                        }
                    },
                    "Platform::getFormFactor");
            } catch (...) {
                deviceType.clear();
            }

            if (deviceType == QStringLiteral("phone") || deviceType == QStringLiteral("tablet"))
                return FormFactor::Mobile;
            if (deviceType == QStringLiteral("2in1") || deviceType == QStringLiteral("pc"))
                return FormFactor::Desktop;
            // 查询失败时按手机处理：鸿蒙用户以手机为主，维持既有触屏表现
            return FormFactor::Mobile;
        }();
        return cached;
#else
        return FormFactor::Desktop;
#endif
    }

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
        // Android: 优先外部应用目录，外部存储不可用时退回内部应用目录（保证可写）
        QString dataDir = androidWritableDataRoot();
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