/**
 * @file   FileExplorer.cpp
 * @brief  文件资源管理器统一入口实现
 * @author BlockBox Team
 * @date   2026-09-26
 */
#include "FileExplorer.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>

#include "utils/AndroidBridge.h"
#include "utils/SettingsManager.h"

QString FileExplorer::keyOf(Backend backend)
{
    if (backend == Backend::MtManager)
        return QStringLiteral("mt");
    return QStringLiteral("system");
}

FileExplorer::Backend FileExplorer::fromKey(const QString &key)
{
    if (key == QLatin1String("mt"))
        return Backend::MtManager;
    return Backend::System;
}

FileExplorer::Backend FileExplorer::backend()
{
    // 设置键 fileExplorer/backend：system（默认）/ mt
    return fromKey(SettingsManager::instance()
                       ->getProperty(QStringLiteral("fileExplorer/backend"),
                                     QStringLiteral("system"))
                       .toString());
}

void FileExplorer::setBackend(Backend backend)
{
    SettingsManager::instance()->setProperty(QStringLiteral("fileExplorer/backend"),
                                             keyOf(backend));
}

QString FileExplorer::mtManagerPackage()
{
    return QStringLiteral("mt.sorter");
}

void FileExplorer::open(QWidget *parent, const QString &path)
{
    Q_UNUSED(parent);
    if (path.isEmpty())
        return;

    const QFileInfo fi(path);
    const QString dir = fi.isDir() ? path : fi.absolutePath();
    if (dir.isEmpty())
        return;

    // MT 管理器（仅安卓）：未安装或 Intent 被拒时继续走下面的回退
    if (backend() == Backend::MtManager
        && AndroidBridge::openPathWith(mtManagerPackage(), dir,
                                       QStringLiteral("resource/folder"))) {
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
