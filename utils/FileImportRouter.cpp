/**
 * @file   FileImportRouter.cpp
 * @brief  全局文件拖入路由器实现
 * @author BlockBox Team
 * @date   2026-08-24
 */
#include "FileImportRouter.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>

#include "modpack/ModpackDetector.h"

// ============================================================================
// 轻量级 ZIP 文件名列表器：仅读取中央目录获取文件名，无需解压
// 参考 ModpackDetector.cpp 中 ZipReader 的中央目录解析逻辑
// ============================================================================

namespace {

QStringList listZipFileNames(const QString &zipFilePath)
{
    QFile file(zipFilePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    qint64 fileSize = file.size();
    if (fileSize < 22)
        return {};

    // 查找 EOCD 签名 (0x06054b50)
    qint64 searchStart = qMax(qint64(0), fileSize - 65557);
    qint64 searchSize = fileSize - searchStart;
    if (searchSize > 65557)
        searchSize = 65557;

    file.seek(searchStart);
    QByteArray tail = file.read(searchSize);

    qint64 centralDirOffset = 0;
    quint16 totalEntries = 0;
    bool found = false;

    for (int i = tail.size() - 22; i >= 0; --i)
    {
        if (static_cast<quint8>(tail[i]) == 0x50 &&
            static_cast<quint8>(tail[i + 1]) == 0x4b &&
            static_cast<quint8>(tail[i + 2]) == 0x05 &&
            static_cast<quint8>(tail[i + 3]) == 0x06)
        {
            QDataStream ds(tail.mid(i, 22));
            ds.setByteOrder(QDataStream::LittleEndian);
            ds.skipRawData(8);
            quint16 entryCount = 0;
            quint32 cdSize = 0;
            quint32 cdOffset = 0;
            ds >> entryCount >> cdSize >> cdOffset;

            QDataStream ds2(tail.mid(i + 8, 2));
            ds2.setByteOrder(QDataStream::LittleEndian);
            quint16 onDiskEntryCount = 0;
            ds2 >> onDiskEntryCount;

            centralDirOffset = cdOffset;
            totalEntries = qMin(onDiskEntryCount, entryCount);
            found = true;
            break;
        }
    }

    if (!found)
        return {};

    // 读取中央目录，仅提取文件名
    if (!file.seek(centralDirOffset))
        return {};

    QStringList names;
    names.reserve(totalEntries);
    const int cdEntryBaseSize = 46;

    for (quint16 i = 0; i < totalEntries; ++i)
    {
        QByteArray header = file.read(cdEntryBaseSize);
        if (header.size() < cdEntryBaseSize)
            return names;

        QDataStream ds(header);
        ds.setByteOrder(QDataStream::LittleEndian);

        quint32 signature = 0;
        ds >> signature;
        if (signature != 0x02014b50)
            return names;

        // 跳到 fileNameLength 字段 (偏移 28)
        ds.skipRawData(24);
        quint16 fileNameLength = 0;
        quint16 extraFieldLength = 0;
        quint16 fileCommentLength = 0;
        ds >> fileNameLength >> extraFieldLength >> fileCommentLength;

        QByteArray fileNameBytes = file.read(fileNameLength);
        if (fileNameBytes.size() < fileNameLength)
            return names;

        // 跳过 extra field 和 comment
        file.read(extraFieldLength + fileCommentLength);

        names.append(QString::fromUtf8(fileNameBytes));
    }

    return names;
}

} // anonymous namespace

// ============================================================================

FileImportRouter::FileImportRouter(QObject *parent)
    : QObject(parent)
{
}

ImportFileType FileImportRouter::detectFileType(const QString &filePath)
{
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile())
        return ImportFileType::Unknown;

    const QString fileName = fi.fileName();
    const QString lower = fileName.toLower();

    // === 扩展名优先检测 ===

    // Java 模组
    if (lower.endsWith(QStringLiteral(".jar")))
        return ImportFileType::Mod;

    // 插件
    if (lower.endsWith(QStringLiteral(".blockbox")))
        return ImportFileType::Plugin;

    // 基岩版资源
    if (lower.endsWith(QStringLiteral(".mcpack")) ||
        lower.endsWith(QStringLiteral(".mcaddon")) ||
        lower.endsWith(QStringLiteral(".mcworld")))
        return ImportFileType::BedrockResource;

    // .mrpack 直接判定为整合包
    if (lower.endsWith(QStringLiteral(".mrpack")))
        return ImportFileType::Modpack;

    // === .json 文件：检测是否为指令包 ===
    if (lower.endsWith(QStringLiteral(".json")))
    {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();
            file.close();

            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject())
            {
                QJsonObject obj = doc.object();
                // 指令包特征：包含 commands 数组
                if (obj.contains(QStringLiteral("commands")) && obj[QStringLiteral("commands")].isArray())
                    return ImportFileType::CommandPack;
            }
        }
        // JSON 但不是指令包，不识别
        return ImportFileType::Unknown;
    }

    // === .zip 文件：需要内容检测 ===
    if (lower.endsWith(QStringLiteral(".zip")))
        return detectZipContentType(filePath);

    return ImportFileType::Unknown;
}

ImportFileType FileImportRouter::detectZipContentType(const QString &filePath)
{
    // 先用 ModpackDetector 检测是否为已知格式整合包
    modpack::ModpackType packType = modpack::ModpackDetector::detect(filePath);
    if (packType != modpack::ModpackType::Unknown &&
        packType != modpack::ModpackType::Generic)
    {
        return ImportFileType::Modpack;
    }

    // 读取 ZIP 中央目录文件名列表
    QStringList fileNames = listZipFileNames(filePath);

    bool hasPackMcmeta = false;
    bool hasLevelDat = false;
    bool hasShadersDir = false;
    bool hasModsDir = false;
    bool hasManifestJson = false;

    for (const QString &name : fileNames)
    {
        if (name == QStringLiteral("pack.mcmeta"))
            hasPackMcmeta = true;
        else if (name == QStringLiteral("level.dat"))
            hasLevelDat = true;
        else if (name.startsWith(QStringLiteral("shaders/")))
            hasShadersDir = true;
        else if (name.startsWith(QStringLiteral("mods/")))
            hasModsDir = true;
        else if (name == QStringLiteral("manifest.json"))
            hasManifestJson = true;
    }

    // 整合包兜底：含 manifest.json + mods/ → 可能是 Generic 整合包
    if (hasManifestJson && hasModsDir)
        return ImportFileType::Modpack;

    // 含 mods/ 目录但无 pack.mcmeta → 可能是整合包
    if (hasModsDir && !hasPackMcmeta)
        return ImportFileType::Modpack;

    // Generic 整合包（ModpackDetector 返回 Generic）
    if (packType == modpack::ModpackType::Generic)
        return ImportFileType::Modpack;

    // 含 level.dat → 世界存档
    if (hasLevelDat)
        return ImportFileType::World;

    // 含 shaders/ 目录 → 光影包
    if (hasShadersDir)
        return ImportFileType::ShaderPack;

    // 含 pack.mcmeta → 资源包
    if (hasPackMcmeta)
        return ImportFileType::ResourcePack;

    return ImportFileType::Unknown;
}

QMap<ImportFileType, QStringList> FileImportRouter::classifyFiles(const QStringList &filePaths)
{
    QMap<ImportFileType, QStringList> groups;
    for (const QString &path : filePaths)
    {
        ImportFileType type = detectFileType(path);
        if (type != ImportFileType::Unknown)
            groups[type].append(path);
    }
    return groups;
}

QString FileImportRouter::fileTypeName(ImportFileType type)
{
    switch (type)
    {
    case ImportFileType::Mod:             return tr("模组");
    case ImportFileType::ShaderPack:      return tr("光影包");
    case ImportFileType::ResourcePack:    return tr("资源包");
    case ImportFileType::DataPack:        return tr("数据包");
    case ImportFileType::World:           return tr("世界存档");
    case ImportFileType::Modpack:         return tr("整合包");
    case ImportFileType::Plugin:          return tr("插件");
    case ImportFileType::CommandPack:     return tr("指令包");
    case ImportFileType::BedrockResource: return tr("基岩版资源");
    case ImportFileType::Unknown:         return tr("未知类型");
    }
    return tr("未知类型");
}

QString FileImportRouter::targetSubFolder(ImportFileType type)
{
    switch (type)
    {
    case ImportFileType::Mod:          return QStringLiteral("mods");
    case ImportFileType::ShaderPack:   return QStringLiteral("shaderpacks");
    case ImportFileType::ResourcePack: return QStringLiteral("resourcepacks");
    case ImportFileType::DataPack:     return QStringLiteral("datapacks");
    case ImportFileType::World:        return QStringLiteral("saves");
    default:                           return QString();
    }
}
