/**
 * @file   ModpackImporter.h
 * @brief  整合包导入器类声明 — MultiMC 模式：导入流程是一个 Task，
 *         进度/状态经 Task 信号对外展示，可随时取消
 * @author BlockBox Team
 * @date   2026-06-10
 */

#pragma once

#include "ModpackInfo.h"
#include "../net/Task.h"
#include "../net/NetJob.h"
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace modpack {

class ModpackImporter : public Task
{
  Q_OBJECT

public:
  explicit ModpackImporter(QObject* parent = nullptr);
  ~ModpackImporter();

  void setModpackInfo(const ModpackInfo& info) { m_info = info; }
  void setZipFilePath(const QString& path) { m_zipPath = path; }
  void setInstanceName(const QString& name) { m_instanceName = name; }
  void setInstancePath(const QString& path) { m_instancePath = path; }

  signals:
  void installProgressChanged(int progress, const QString& message);
  void installFinished(bool success, const QString& instancePath);
  void stageChanged(const QString& stage);

protected:
  void executeTask() override;
  bool abort() override;
  bool canAbort() const override;

private:
  void installVersion();
  void installLoader();
  void installMods();
  void downloadNextMod();
  void extractOverrides();
  void createInstance();
  void failInstall(const QString& error);

  /// Task 进度/状态对外展示（安装页对话框直接绑定）
  void emitStage(const QString& stage);
  void emitProgress(int percent, const QString& message);
  /// 模组下载计数刷新（NetJob 并发回调与串行路径共用）
  void reportModProgress();
  /// 加载器装完后定位其版本目录（新装出的目录优先，其次按 JSON id 匹配已存在目录）
  QString resolveLoaderVersionDir() const;

  QString getCurseForgeDownloadUrl(const QString& projectId, const QString& fileId) const;

  /// Modrinth 包文件候选地址（MultiMC mrpack 语义：downloads 列表顺序 + 镜像展开）
  QStringList buildModCandidates(const ModInfo& mod) const;
  /// 从清单 path / 下载地址推导安全的 jar 文件名
  QString modFileName(const ModInfo& mod, const QStringList& candidates) const;

  /// MultiMC net 层：整合包全部模组文件放进一个 NetJob，并发下载 + 校验
  NetJob::Ptr m_modJob;

  /// CurseForge 清单（只有项目/文件 ID）保留旧下载路径：
  /// 其文件名需要从响应 Content-Disposition 取，引擎路径拿不到
  QNetworkAccessManager* m_networkManager;
  QNetworkReply* m_currentModReply;

  /// 无直接下载地址、走串行下载的 mod 下标队列
  QList<int> m_serialModIndices;
  int m_serialPos = 0;

  ModpackInfo m_info;
  QString m_zipPath;
  QString m_instanceName;
  QString m_instancePath;
  /// 游戏根目录（<root>/versions 是实例列表唯一扫描处）
  QString m_gameRoot;
  /// 加载器安装前 versions/ 目录快照，用于定位本次新装出的加载器版本目录
  QStringList m_versionsBefore;
  /// 本包加载器是否受支持且已给版本号（否则按原版安装）
  bool m_useLoader = false;
  /// 当前正在安装的加载器类型（forge/fabric/neoforge），空表示不在加载器阶段
  QString m_activeLoaderType;

  int m_totalMods;
  int m_modsDownloaded;
  int m_modsFailed;

  /// 用户通过进度对话框请求取消
  bool m_abortRequested = false;
};

} // namespace modpack
