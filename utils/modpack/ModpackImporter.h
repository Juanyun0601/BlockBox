/**
 * @file   ModpackImporter.h
 * @brief  整合包导入器类声明
 * @author BlockBox Team
 * @date   2026-06-10
 */

#pragma once

#include "ModpackInfo.h"
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace modpack {

class ModpackImporter : public QObject
{
  Q_OBJECT

public:
  explicit ModpackImporter(QObject* parent = nullptr);
  ~ModpackImporter();

  void setModpackInfo(const ModpackInfo& info) { m_info = info; }
  void setZipFilePath(const QString& path) { m_zipPath = path; }
  void setInstanceName(const QString& name) { m_instanceName = name; }
  void setInstancePath(const QString& path) { m_instancePath = path; }

  void startInstall();

signals:
  void installProgressChanged(int progress, const QString& message);
  void installFinished(bool success, const QString& instancePath);
  void stageChanged(const QString& stage);

private:
  void installVersion();
  void installLoader();
  void installMods();
  void downloadNextMod();
  void extractOverrides();
  void createInstance();
  void failInstall(const QString& error);

  QString getCurseForgeDownloadUrl(const QString& projectId, const QString& fileId) const;

  QNetworkAccessManager* m_networkManager;
  QNetworkReply* m_currentModReply;

  ModpackInfo m_info;
  QString m_zipPath;
  QString m_instanceName;
  QString m_instancePath;

  int m_currentModIndex;
  int m_totalMods;
  int m_modsDownloaded;
  int m_modsFailed;
};

} // namespace modpack