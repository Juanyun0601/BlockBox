/**
 * @file   stub_platform.cpp
 * @brief  存档备份验证程序的平台层替身：数据目录从环境变量取临时目录
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "platform.h"

#include <QProcessEnvironment>

namespace Platform {

QString getDataDirectory()
{
    return QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("BLOCKBOX_PROBE_DATA_DIR"));
}

} // namespace Platform
