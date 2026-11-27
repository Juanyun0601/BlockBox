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
#include <QFileInfo>
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

/** 载荷版本：1=只有实例路径；2=带 kind 与存档/服务器参数 */
constexpr int kPayloadFormat = 2;

/** 将文件名中的非法字符替换为下划线 */
QString sanitizeFileName(const QString &name)
{
    QString result = name;
    const QString illegal = "\\/:*?\"<>|";
    for (const QChar &ch : illegal)
        result.replace(ch, '_');
    result = result.trimmed();
    // Windows 不允许文件名以点/空格结尾
    while (result.endsWith(QLatin1Char('.')) || result.endsWith(QLatin1Char(' ')))
        result.chop(1);
    return result;
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

/** 实例自己设置过的图标：没设置（或文件丢失）时返回空，由调用方决定回退顺序 */
QPixmap customInstanceIconPixmap(const QString &instancePath)
{
    QPixmap pixmap;
    const QString iconPath = SettingsManager::instance()
        ->getProperty("instance/" + instancePath + "/iconPath").toString();
    if (!iconPath.isEmpty() && QFile::exists(iconPath))
        pixmap.load(iconPath);

    // 设置里没记录（例如旧数据、换电脑后设置丢了）时，实例目录里还留着
    // 「选择图标」复制进来的 instanceIcon.*，用它兜底
    if (pixmap.isNull())
    {
        const QDir instanceDir(instancePath);
        const QStringList candidates =
            instanceDir.entryList({QStringLiteral("instanceIcon.*")}, QDir::Files);
        for (const QString &fileName : candidates)
        {
            if (pixmap.load(instanceDir.filePath(fileName)))
                break;
        }
    }
    return pixmap;
}

/** 实例图标：自定义图标，未设置（或文件丢失）则回退到应用图标 */
QPixmap instanceIconPixmap(const QString &instancePath)
{
    QPixmap pixmap = customInstanceIconPixmap(instancePath);
    if (pixmap.isNull())
        pixmap.load(":/Images/icon.ico");
    return pixmap;
}

/** 存档封面：存档目录里的 icon.png（旧版服务端存档可能叫 server-icon.png） */
QPixmap saveIconPixmap(const QString &instancePath, const QString &saveName)
{
    const QDir saveDir(QDir(instancePath).filePath(QStringLiteral("saves/") + saveName));
    const QStringList candidates = {QStringLiteral("icon.png"),
                                    QStringLiteral("server-icon.png")};
    for (const QString &fileName : candidates)
    {
        const QString path = saveDir.filePath(fileName);
        if (!QFile::exists(path))
            continue;
        QPixmap pixmap(path);
        if (!pixmap.isNull())
            return pixmap;
    }
    return QPixmap();
}

/** 快捷方式最终使用的图标：
 *  服务器图标（调用方传入）→ 实例自己设置的图标 → 存档封面 → 应用图标 */
QPixmap iconPixmapForTarget(const QuickLaunchManager::Target &target)
{
    if (!target.icon.isNull())
        return target.icon;
    const QPixmap customIcon = customInstanceIconPixmap(target.instancePath);
    if (!customIcon.isNull())
        return customIcon;
    if (target.kind == QuickLaunchManager::Kind::Save)
    {
        const QPixmap saveIcon = saveIconPixmap(target.instancePath, target.saveName);
        if (!saveIcon.isNull())
            return saveIcon;
    }
    return instanceIconPixmap(target.instancePath);
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

QString kindToString(QuickLaunchManager::Kind kind)
{
    switch (kind)
    {
    case QuickLaunchManager::Kind::Save:   return QStringLiteral("save");
    case QuickLaunchManager::Kind::Server: return QStringLiteral("server");
    case QuickLaunchManager::Kind::Instance: break;
    }
    return QStringLiteral("instance");
}

/** 存档名/文件名必须是单层名称，拒绝 ../ 之类跳到实例目录外的路径 */
bool isSafeLeafName(const QString &name)
{
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        return false;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return false;
    return true;
}

/** 服务器地址的展示形式：默认端口不写出来 */
QString serverAddressText(const QString &address, quint16 port)
{
    if (port == 25565)
        return address;
    return QStringLiteral("%1:%2").arg(address).arg(port);
}

/** 桌面文件主名（不含扩展名）：优先用调用方给的标题，否则按类型自动拼 */
QString shortcutBaseName(const QuickLaunchManager::Target &target)
{
    QString base = target.title.trimmed();
    if (!base.isEmpty())
        return sanitizeFileName(base);

    const QString instanceName = target.instanceName.isEmpty()
        ? instanceDisplayName(target.instancePath)
        : target.instanceName;
    if (instanceName.isEmpty())
        base = QObject::tr("方块盒子");
    else
        base = instanceName;

    switch (target.kind)
    {
    case QuickLaunchManager::Kind::Save:
        if (isSafeLeafName(target.saveName))
            base += QStringLiteral(" - ") + target.saveName;
        break;
    case QuickLaunchManager::Kind::Server:
        if (!target.serverAddress.isEmpty())
            base += QStringLiteral(" - ")
                + serverAddressText(target.serverAddress, target.serverPort);
        break;
    case QuickLaunchManager::Kind::Instance:
        break;
    }
    return sanitizeFileName(base);
}

} // namespace

bool QuickLaunchManager::createDesktopShortcut(const QString &instancePath, QString *errorMessage)
{
    Target target;
    target.kind = Kind::Instance;
    target.instancePath = instancePath;
    target.instanceName = instanceDisplayName(instancePath);
    target.title = target.instanceName;
    return createShortcut(target, errorMessage);
}

bool QuickLaunchManager::createSaveShortcut(const QString &instancePath, const QString &saveName,
                                            QString *errorMessage)
{
    Target target;
    target.kind = Kind::Save;
    target.instancePath = instancePath;
    target.instanceName = instanceDisplayName(instancePath);
    target.saveName = saveName;
    target.title = target.instanceName + QStringLiteral(" - ") + saveName;
    return createShortcut(target, errorMessage);
}

bool QuickLaunchManager::createServerShortcut(const QString &instancePath,
                                              const QString &serverName,
                                              const QString &address, quint16 port,
                                              const QPixmap &icon,
                                              QString *errorMessage)
{
    Target target;
    target.kind = Kind::Server;
    target.instancePath = instancePath;
    target.instanceName = instanceDisplayName(instancePath);
    target.serverAddress = address.trimmed();
    target.serverPort = port == 0 ? 25565 : port;
    target.icon = icon;
    const QString displayName = serverName.trimmed().isEmpty()
        ? serverAddressText(target.serverAddress, target.serverPort)
        : serverName.trimmed();
    target.title = target.instanceName + QStringLiteral(" - ") + displayName;
    return createShortcut(target, errorMessage);
}

bool QuickLaunchManager::createShortcut(const Target &target, QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!Platform::isWindows())
        return fail(QObject::tr("桌面快捷启动暂仅支持 Windows 平台"));
    if (target.instancePath.isEmpty() || !QDir(target.instancePath).exists())
        return fail(QObject::tr("实例不存在，无法创建桌面快捷启动"));

    // 目标参数校验：存档名不能跳出实例目录，服务器地址不能为空
    const QString instancePath = QDir::fromNativeSeparators(target.instancePath);
    if (target.kind == Kind::Save && !isSafeLeafName(target.saveName))
        return fail(QObject::tr("存档名无效，无法创建桌面快捷启动"));
    if (target.kind == Kind::Server && target.serverAddress.trimmed().isEmpty())
        return fail(QObject::tr("服务器地址为空，无法创建桌面快捷启动"));

    // 1. 图标部分：存档封面 / 服务器图标 / 实例图标 → 多尺寸 ICO
    const QByteArray icoData = buildIcoData(iconPixmapForTarget(target));
    if (icoData.isEmpty())
        return fail(QObject::tr("图标编码失败，无法创建桌面快捷启动"));

    // 2. 载荷部分：记录目标类型与实例/存档/服务器信息
    QJsonObject payload;
    payload.insert("format", kPayloadFormat);
    payload.insert("kind", kindToString(target.kind));
    payload.insert("instancePath", instancePath);
    payload.insert("instanceName", target.instanceName.isEmpty()
                                       ? instanceDisplayName(instancePath)
                                       : target.instanceName);
    payload.insert("title", shortcutBaseName(target));
    if (target.kind == Kind::Save)
        payload.insert("saveName", target.saveName);
    else if (target.kind == Kind::Server)
    {
        payload.insert("serverAddress", target.serverAddress.trimmed());
        payload.insert("serverPort", int(target.serverPort));
    }

    const QString baseName = shortcutBaseName(target);
    if (baseName.isEmpty())
        return fail(QObject::tr("无法生成快捷启动文件名"));
    const QString shortcutPath = desktopDirectory() + "/" + baseName + ".blockbox";

    // 3. 实例快捷方式改名后桌面文件不再同名：清掉记录的旧文件，桌面始终只有一个
    if (target.kind == Kind::Instance)
    {
        const QString recorded = SettingsManager::instance()
            ->getProperty("instance/" + instancePath + kRecordedFileSuffix).toString();
        if (!recorded.isEmpty() && recorded != shortcutPath && QFile::exists(recorded))
            QFile::remove(recorded);
    }

    QSaveFile file(shortcutPath);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QObject::tr("无法写入桌面文件：%1").arg(shortcutPath));
    file.write(icoData);
    file.write(kPayloadMarker);
    file.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    file.write("\n");
    if (!file.commit())
        return fail(QObject::tr("无法写入桌面文件：%1").arg(shortcutPath));

    if (target.kind == Kind::Instance)
    {
        SettingsManager::instance()->setProperty(
            "instance/" + instancePath + kRecordedFileSuffix, shortcutPath);
    }

    // 4. 确保双击 .blockbox 文件会用方块盒子打开
    QString assocError;
    if (!registerFileAssociation(&assocError))
        return fail(assocError);
    return true;
}

bool QuickLaunchManager::parseQuickLaunchFile(const QString &filePath, Target *target,
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

    const QJsonObject object = doc.object();
    const QString path = QDir::fromNativeSeparators(
        object.value("instancePath").toString());
    if (path.isEmpty())
        return fail(QObject::tr("快捷启动文件缺少实例路径"));

    Target parsed;
    parsed.instancePath = path;
    parsed.instanceName = object.value("instanceName").toString();
    parsed.title = object.value("title").toString();

    const QString kind = object.value("kind").toString();
    if (kind == QStringLiteral("save"))
    {
        parsed.kind = Kind::Save;
        parsed.saveName = object.value("saveName").toString();
        if (!isSafeLeafName(parsed.saveName))
            return fail(QObject::tr("快捷启动文件里的存档名无效"));
    }
    else if (kind == QStringLiteral("server"))
    {
        parsed.kind = Kind::Server;
        parsed.serverAddress = object.value("serverAddress").toString().trimmed();
        const int port = object.value("serverPort").toInt(25565);
        parsed.serverPort = (port > 0 && port <= 65535) ? quint16(port) : 25565;
        if (parsed.serverAddress.isEmpty())
            return fail(QObject::tr("快捷启动文件缺少服务器地址"));
    }
    else
    {
        // 旧文件（format 1）没有 kind 字段，按实例处理
        parsed.kind = Kind::Instance;
    }

    if (target)
        *target = parsed;
    return true;
}

bool QuickLaunchManager::parseQuickLaunchFile(const QString &filePath, QString *instancePath,
                                              QString *errorMessage)
{
    Target target;
    if (!parseQuickLaunchFile(filePath, &target, errorMessage))
        return false;
    if (instancePath)
        *instancePath = target.instancePath;
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
    // %1 会被替换为被双击文件的路径，即图标取自文件自身内嵌的 ICO 数据。
    // 注意这里必须只写 %1：写成 "%1",0 时资源管理器不会替换 %1（它把这个值
    // 当成普通图标路径），所有 .blockbox 都只显示空白文件图标；%1 本身
    // 就表示取该文件里的第一个图标，等于默认索引 0。
    progIdKey.setValue("DefaultIcon/.", QStringLiteral("%1"));
    progIdKey.setValue("shell/open/.", QObject::tr("用方块盒子打开"));
    progIdKey.setValue("shell/open/command/.", openCommand);

    // 注册到 Applications，让"打开方式"列表中也能找到方块盒子
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    QSettings appKey("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\" + exeName,
                     QSettings::NativeFormat);
    appKey.setValue("shell/open/command/.", openCommand);
    appKey.setValue("FriendlyAppName", QObject::tr("方块盒子"));

    // 通知资源管理器关联已变化，立即生效（无需重启/注销）
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#endif
}
