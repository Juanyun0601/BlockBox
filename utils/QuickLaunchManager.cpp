/**
 * @file   QuickLaunchManager.cpp
 * @brief  桌面快捷启动（.blockbox 文件）管理器实现
 * @author BlockBox Team
 * @date   2026-09-06
 */
#include "QuickLaunchManager.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QPixmap>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <utility>

#include "SettingsManager.h"
#include "platform.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#endif

namespace {

/** 载荷标记：ICO 数据之后的正文起始位置（从文件末尾向前查找，避免误匹配图标二进制） */
const char kPayloadMarker[] = "\n;;BLOCKBOX:QUICKLAUNCH:v1;;\n";

/** 快捷启动文件在设置中的记录键后缀（记录桌面文件完整路径，用于改名/换图标后刷新） */
const char kRecordedFileSuffix[] = "/quickLaunchFile";

/** 将文件名中的非法字符替换为下划线 */
QString sanitizeFileName(const QString &name)
{
    QString result = name;
    const QString illegal = "\\/:*?\"<>|";
    for (const QChar &ch : illegal)
        result.replace(ch, '_');
    return result.trimmed();
}

/** 实例显示名：优先设置中的 displayName，为空则用文件夹名 */
QString instanceDisplayName(const QString &instancePath)
{
    QString name = SettingsManager::instance()
        ->getProperty("instance/" + instancePath + "/displayName").toString();
    if (name.isEmpty())
        name = instancePath.split("/").last();
    return name;
}

/** 桌面目录 */
QString desktopDirectory()
{
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktop.isEmpty())
        desktop = QDir::homePath() + "/Desktop";
    return desktop;
}

/** 实例图标：设置中自定义的图标，未设置（或文件丢失）则回退到应用图标 */
QPixmap instanceIconPixmap(const QString &instancePath)
{
    QPixmap pixmap;
    const QString iconPath = SettingsManager::instance()
        ->getProperty("instance/" + instancePath + "/iconPath").toString();
    if (!iconPath.isEmpty() && QFile::exists(iconPath))
        pixmap.load(iconPath);
    if (pixmap.isNull())
        pixmap.load(":/Images/icon.ico");
    return pixmap;
}

/** 把任意图片编码为多尺寸 ICO 数据（PNG 压缩条目，Windows Vista+ 均支持） */
QByteArray buildIcoData(const QPixmap &sourcePixmap)
{
    const QImage source = sourcePixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (source.isNull())
        return QByteArray();

    // 居中裁剪为正方形，避免缩放成图标时变形
    const int side = qMin(source.width(), source.height());
    const QImage square = source.copy((source.width() - side) / 2,
                                      (source.height() - side) / 2, side, side);

    struct IconEntry { int size; QByteArray png; };
    QList<IconEntry> entries;
    const QList<int> sizes = {16, 32, 48, 64, 128, 256};
    for (int size : sizes)
    {
        const QImage scaled = square.scaled(size, size, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation);
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        scaled.save(&buffer, "PNG");
        entries.append({size, buffer.data()});
    }

    QByteArray ico;
    QDataStream stream(&ico, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    // ICONDIR：保留字(0)、类型(1=图标)、条目数
    stream << quint16(0) << quint16(1) << quint16(entries.size());
    // ICONDIRENTRY：PNG 数据紧跟目录之后
    quint32 offset = 6 + quint32(entries.size()) * 16;
    for (const IconEntry &entry : std::as_const(entries))
    {
        // 256px 在目录中记为 0
        const quint8 side8 = entry.size == 256 ? quint8(0) : quint8(entry.size);
        stream << side8 << side8 << quint8(0) << quint8(0)
               << quint16(1) << quint16(32)
               << quint32(entry.png.size()) << offset;
        offset += quint32(entry.png.size());
    }
    for (const IconEntry &entry : std::as_const(entries))
        ico.append(entry.png);
    return ico;
}

} // namespace

bool QuickLaunchManager::createDesktopShortcut(const QString &instancePath, QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!Platform::isWindows())
        return fail(QObject::tr("桌面快捷启动暂仅支持 Windows 平台"));
    if (instancePath.isEmpty() || !QDir(instancePath).exists())
        return fail(QObject::tr("实例不存在，无法创建桌面快捷启动"));

    // 1. 图标部分：实例图标 → 多尺寸 ICO
    const QByteArray icoData = buildIcoData(instanceIconPixmap(instancePath));
    if (icoData.isEmpty())
        return fail(QObject::tr("实例图标编码失败，无法创建桌面快捷启动"));

    // 2. 载荷部分：记录实例路径等信息
    QJsonObject payload;
    payload.insert("format", 1);
    payload.insert("instancePath", QDir::fromNativeSeparators(instancePath));
    payload.insert("instanceName", instanceDisplayName(instancePath));
    const QString target = desktopDirectory() + "/"
        + sanitizeFileName(instanceDisplayName(instancePath)) + ".blockbox";

    // 3. 实例改名后桌面文件不再同名：清掉记录的旧文件，桌面始终只有一个
    const QString recorded = SettingsManager::instance()
        ->getProperty("instance/" + instancePath + kRecordedFileSuffix).toString();
    if (!recorded.isEmpty() && recorded != target && QFile::exists(recorded))
        QFile::remove(recorded);

    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QObject::tr("无法写入桌面文件：%1").arg(target));
    file.write(icoData);
    file.write(kPayloadMarker);
    file.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    file.write("\n");
    if (!file.commit())
        return fail(QObject::tr("无法写入桌面文件：%1").arg(target));

    SettingsManager::instance()->setProperty("instance/" + instancePath + kRecordedFileSuffix,
                                             target);

    // 4. 确保双击 .blockbox 文件会用方块盒子打开
    QString assocError;
    if (!registerFileAssociation(&assocError))
        return fail(assocError);
    return true;
}

bool QuickLaunchManager::parseQuickLaunchFile(const QString &filePath, QString *instancePath,
                                              QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QObject::tr("无法读取快捷启动文件：%1").arg(filePath));
    const QByteArray data = file.readAll();

    const QByteArray marker(kPayloadMarker);
    const int markerIndex = data.lastIndexOf(marker);
    if (markerIndex < 0)
        return fail(QObject::tr("不是有效的方块盒子快捷启动文件"));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        data.mid(markerIndex + marker.size()), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QObject::tr("快捷启动文件载荷解析失败"));

    const QString path = QDir::fromNativeSeparators(
        doc.object().value("instancePath").toString());
    if (path.isEmpty())
        return fail(QObject::tr("快捷启动文件缺少实例路径"));
    if (instancePath)
        *instancePath = path;
    return true;
}

void QuickLaunchManager::refreshDesktopShortcut(const QString &instancePath)
{
    const QString recorded = SettingsManager::instance()
        ->getProperty("instance/" + instancePath + kRecordedFileSuffix).toString();
    // 文件已被用户删除/移动时不再重建，尊重用户的清理
    if (recorded.isEmpty() || !QFile::exists(recorded))
        return;
    QString error;
    createDesktopShortcut(instancePath, &error);
}

bool QuickLaunchManager::registerFileAssociation(QString *errorMessage)
{
    [[maybe_unused]] auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

#ifndef Q_OS_WIN
    return true;
#else
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    // 双击 .blockbox 文件时系统会以 "<方块盒子>" "<文件路径>" 的形式调用
    const QString openCommand = QStringLiteral("\"") + exePath + QStringLiteral("\" \"%1\"");

    // 写入 HKCU\Software\Classes，无需管理员权限
    QSettings extensionKey("HKEY_CURRENT_USER\\Software\\Classes\\.blockbox",
                           QSettings::NativeFormat);
    extensionKey.setValue(".", "BlockBox.InstanceFile");

    // DefaultIcon 指向 %1：图标内嵌在文件本身，每个 .blockbox 文件显示各自的实例图标
    QSettings progIdKey("HKEY_CURRENT_USER\\Software\\Classes\\BlockBox.InstanceFile",
                        QSettings::NativeFormat);
    progIdKey.setValue(".", QObject::tr("方块盒子文件"));
    progIdKey.setValue("FriendlyTypeName", QObject::tr("方块盒子文件"));
    // %1 会被替换为被双击文件的路径，即图标取自文件自身内嵌的 ICO 数据
    progIdKey.setValue("DefaultIcon/.", QStringLiteral("\"%1\",0"));
    progIdKey.setValue("shell/open/.", QObject::tr("用方块盒子打开"));
    progIdKey.setValue("shell/open/command/.", openCommand);

    // 注册到 Applications，让"打开方式"列表中也能找到方块盒子
    QSettings appKey("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\BlockBox.exe",
                     QSettings::NativeFormat);
    appKey.setValue("shell/open/command/.", openCommand);

    // 通知资源管理器关联已变化，立即生效（无需重启/注销）
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#endif
}
