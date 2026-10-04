#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace modpack {

enum class ModpackType
{
  Unknown,
  CurseForge,
  Modrinth,
  MultiMC,
  MCBBS,
  HMCL,
  Generic
};

struct ModInfo
{
  QString projectId;
  QString fileId;
  QString name;
  QString downloadUrl;
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