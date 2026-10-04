/**
 * @file   ModpackExporter.h
 * @brief  整合包导出器类声明
 * @author BlockBox Team
 * @date   2026-06-19
 */

#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

namespace modpack {

/**
 * @brief 导出格式枚举
 */
enum class ExportFormat
{
  CurseForge,
  MultiMC,
  Server,
  Modrinth,
  Mcbbs,
  BlockBox,
  Packwiz
};

/**
 * @brief 导出信息结构体
 */
struct ModpackExportInfo
{
  QString name;
  QString author;
  QString version;
  QString description;
  QString url;
  QString fileApi;
  bool forceUpdate = false;
  QString authlibInjectorServer;
  QList<int> supportedJavaVersions;
  int minMemory = 0;
  QString javaArgs;
  QString launchArgs;
  QStringList selectedModules;
};

/**
 * @brief 整合包导出器
 *
 * 将实例导出为 CurseForge / MultiMC / Server / Modrinth / Mcbbs / BlockBox 格式的整合包 zip 文件。
 * 异步运行并通过信号报告进度。
 */
class ModpackExporter : public QObject
{
  Q_OBJECT

public:
  /**
   * @brief 构造函数
   * @param instancePath  实例目录路径
   * @param exportFormat  导出格式
   * @param exportInfo    导出信息（名称、作者、版本、描述等）
   * @param selectedFiles 选中的文件列表（相对于实例目录的路径）
   * @param outputPath    输出文件路径
   * @param parent        父对象
   */
  explicit ModpackExporter(const QString& instancePath,
                           ExportFormat exportFormat,
                           const ModpackExportInfo& exportInfo,
                           const QStringList& selectedFiles,
                           const QString& outputPath,
                           QObject* parent = nullptr);

  ~ModpackExporter();

public slots:
  /**
   * @brief 开始导出
   */
  void startExport();

signals:
  /**
   * @brief 导出进度变化信号
   * @param percent 进度百分比 (0-100)
   * @param stage   当前阶段描述
   */
  void exportProgressChanged(int percent, const QString& stage);

  /**
   * @brief 导出完成信号
   * @param success    是否成功
   * @param outputPath 输出文件路径（成功时有效）
   * @param error      错误信息（失败时有效）
   */
  void exportFinished(bool success, const QString& outputPath, const QString& error);

private:
  void exportAsCurseForge();
  void exportAsMultiMC();
  void exportAsServer();
  void exportAsModrinth();
  void exportAsMcbbs();
  void exportAsBlockBox();
  void exportAsPackwiz();

  QString detectGameVersion() const;
  QString detectLoaderType() const;
  QString detectLoaderVersion() const;
  QMap<QString, QString> detectAllLoaders() const;
  QByteArray calculateSha1(const QString& filePath) const;
  QByteArray calculateSha256(const QString& filePath) const;
  QByteArray calculateSha512(const QString& filePath) const;
  bool isAlreadyCompressed(const QString& ext) const;

  QString m_instancePath;
  ExportFormat m_exportFormat;
  ModpackExportInfo m_exportInfo;
  QStringList m_selectedFiles;
  QString m_outputPath;
};

} // namespace modpack