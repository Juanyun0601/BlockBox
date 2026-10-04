#pragma once

#include "ModpackInfo.h"
#include <QString>

namespace modpack {

class ModpackDetector
{
public:
  static ModpackType detect(const QString& zipFilePath);
  static ModpackInfo parse(const QString& zipFilePath);
};

} // namespace modpack