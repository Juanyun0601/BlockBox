/**
 * @file   SystemInfo.cpp
 * @brief  系统信息检测实现
 * @author BlockBox Team
 */

#include "SystemInfo.h"

#include <QSettings>
#include <QSysInfo>

#include "platform.h"

QString SystemInfo::readRegistryString(const QString &path, const QString &key)
{
#ifdef PLATFORM_WINDOWS
    QSettings settings(path, QSettings::NativeFormat);
    return settings.value(key).toString().trimmed();
#else
    Q_UNUSED(path);
    Q_UNUSED(key);
    return QString();
#endif
}

QString SystemInfo::windowsEdition()
{
#ifdef PLATFORM_WINDOWS
    const QString editionId = readRegistryString(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
        QStringLiteral("EditionID"));

    // EditionID → 中文显示名（常见版本）
    if (editionId == QLatin1String("Core"))
        return QStringLiteral("家庭版");
    if (editionId == QLatin1String("CoreN"))
        return QStringLiteral("家庭版 N");
    if (editionId == QLatin1String("CoreSingleLanguage"))
        return QStringLiteral("家庭单语言版");
    if (editionId == QLatin1String("Professional"))
        return QStringLiteral("专业版");
    if (editionId == QLatin1String("ProfessionalN"))
        return QStringLiteral("专业版 N");
    if (editionId == QLatin1String("ProfessionalWorkstation"))
        return QStringLiteral("专业工作站版");
    if (editionId == QLatin1String("Enterprise"))
        return QStringLiteral("企业版");
    if (editionId == QLatin1String("Education"))
        return QStringLiteral("教育版");
    if (editionId == QLatin1String("ProfessionalEducation"))
        return QStringLiteral("专业教育版");
    if (editionId == QLatin1String("Home"))
        return QStringLiteral("家庭版");
    if (editionId == QLatin1String("ServerStandard"))
        return QStringLiteral("服务器标准版");
    if (editionId == QLatin1String("ServerDatacenter"))
        return QStringLiteral("服务器数据中心版");
    return editionId; // 未知版本类型时原样返回
#else
    return QString();
#endif
}

SystemInfoData SystemInfo::collect()
{
    SystemInfoData data;
    data.architecture = QSysInfo::currentCpuArchitecture();
    data.kernelVersion = QSysInfo::kernelVersion();

    const QString productType = QSysInfo::productType(); // windows / macos / linux ...
    const QString productName = QSysInfo::prettyProductName();

    if (productType == QLatin1String("windows")) {
        data.osName = QStringLiteral("Windows");
        data.build = readRegistryString(
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
            QStringLiteral("CurrentBuildNumber"));
        data.edition = windowsEdition();

        // 组合完整名：如 "Windows 11 家庭中文版"
        QString full = QStringLiteral("Windows");
        const QString displayVersion = readRegistryString(
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
            QStringLiteral("DisplayVersion"));
        if (!displayVersion.isEmpty())
            full += QLatin1Char(' ') + displayVersion;
        if (!data.edition.isEmpty())
            full += QLatin1Char(' ') + data.edition;
        data.osFullName = full;
    } else if (productType == QLatin1String("macos") || productType == QLatin1String("osx")) {
        data.osName = QStringLiteral("macOS");
        data.osFullName = productName;
    } else if (productType == QLatin1String("linux") || productType == QLatin1String("ubuntu")
               || productType == QLatin1String("debian") || productType == QLatin1String("arch")
               || productType == QLatin1String("fedora") || productType == QLatin1String("manjaro")
               || productType == QLatin1String("opensuse")) {
        data.osName = QStringLiteral("Linux");
        data.osFullName = productName;
    } else if (productType == QLatin1String("android")) {
        data.osName = QStringLiteral("Android");
        data.osFullName = productName;
    } else {
        data.osName = productType.isEmpty() ? QStringLiteral("Unknown") : productType;
        data.osFullName = productName;
    }

    return data;
}

QString SystemInfo::osName()
{
    return collect().osName;
}

QString SystemInfo::osFullName()
{
    return collect().osFullName;
}

QString SystemInfo::architecture()
{
    return collect().architecture;
}

bool SystemInfo::isWindows()
{
#ifdef PLATFORM_WINDOWS
    return true;
#else
    return false;
#endif
}

bool SystemInfo::isLinux()
{
#ifdef PLATFORM_LINUX
    return true;
#else
    return false;
#endif
}

bool SystemInfo::isMac()
{
#ifdef PLATFORM_MAC
    return true;
#else
    return false;
#endif
}

bool SystemInfo::isAndroid()
{
#ifdef PLATFORM_ANDROID
    return true;
#else
    return false;
#endif
}
