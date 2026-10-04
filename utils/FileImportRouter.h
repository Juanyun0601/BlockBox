/**
 * @file   FileImportRouter.h
 * @brief  全局文件拖入路由器：检测文件类型并分发到对应处理器
 * @author BlockBox Team
 * @date   2026-08-24
 */
#ifndef FILEIMPORTROUTER_H
#define FILEIMPORTROUTER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>

/**
 * @brief 拖入文件的分类类型
 */
enum class ImportFileType
{
    Mod,              // .jar → mods/
    ShaderPack,       // .zip → shaderpacks/
    ResourcePack,     // .zip → resourcepacks/
    DataPack,         // .zip → datapacks/
    World,            // .zip → saves/
    Modpack,          // .zip/.mrpack → ModpackImportPage
    Plugin,           // .BlockBox → PluginManager
    CommandPack,      // .json (含 commands) → 指令包管理器
    BedrockResource,  // .mcpack/.mcaddon/.mcworld → BedrockContentInstaller
    Unknown           // 不支持
};

/**
 * @brief 文件导入路由器
 *
 * 负责：
 * 1. 检测单个/多个文件的类型（扩展名 + ZIP 内容检测）
 * 2. 按类型分组
 * 3. 路由到对应的导入处理器
 */
class FileImportRouter : public QObject
{
    Q_OBJECT

public:
    explicit FileImportRouter(QObject *parent = nullptr);

    /**
     * @brief 检测单个文件的导入类型
     * @param filePath 文件绝对路径
     * @return 文件类型枚举
     */
    static ImportFileType detectFileType(const QString &filePath);

    /**
     * @brief 批量分类文件，按类型分组返回
     * @param filePaths 文件路径列表
     * @return 类型 → 文件路径列表的映射
     */
    static QMap<ImportFileType, QStringList> classifyFiles(const QStringList &filePaths);

    /**
     * @brief 获取类型的中文显示名
     */
    static QString fileTypeName(ImportFileType type);

    /**
     * @brief 获取类型对应的目标子文件夹名（Java 版实例）
     */
    static QString targetSubFolder(ImportFileType type);

signals:
    /** 整合包文件被检测到，需要跳转到整合包导入页 */
    void modpackDetected(const QString &filePath);

    /** 导入完成 */
    void importCompleted(ImportFileType type, int successCount, int failCount, const QString &instanceName);

    /** 导入出错 */
    void importFailed(const QString &error);

private:
    /** ZIP 内容检测：尝试判断 .zip 文件属于哪种内容类型 */
    static ImportFileType detectZipContentType(const QString &filePath);
};

#endif // FILEIMPORTROUTER_H
