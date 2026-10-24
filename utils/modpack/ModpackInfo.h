#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace modpack {

enum class ModpackType
{
  Unknown,
  CurseForge,
  Modrinth,
  MultiMC,
  MCBBS,
  HMCL,
  Generic,
  /// 方块盒子原生格式（zip 内含 blockbox.pack.json 清单）
  BlockBox
};

struct ModInfo
{
  QString projectId;
  QString fileId;
  QString name;
  QString downloadUrl;
  /// Modrinth 清单 downloads 全列表（MultiMC 语义：按列出顺序逐个尝试）
  QStringList downloadUrls;
  /// 清单提供的校验和（下载完成后验证，不符自动换下一个地址）
  QString sha1;
  QString sha512;
  bool required = true;
};

struct ModpackInfo
{
  ModpackType type = ModpackType::Unknown;
  QString name;
  QString version;
  QString author;
  QString gameVersion;
  QString description;
  QString loaderType;
  QString loaderVersion;
  QList<modpack::ModInfo> mods;
  QString overridesPath;

  QString typeName() const;
};

} // namespace modpack