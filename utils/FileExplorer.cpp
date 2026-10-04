/**
 * @file   FileExplorer.cpp
 * @brief  文件资源管理器统一入口实现
 * @author BlockBox Team
 * @date   2026-09-26
 */
#include "FileExplorer.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include "components/AppFileDialog.h"
#include "utils/AndroidBridge.h"
#include "utils/SettingsManager.h"

QString FileExplorer::keyOf(Backend backend)
{
    switch (backend) {
    case Backend::Builtin:
        return QStringLiteral("builtin");
    case Backend::MtManager:
        return QStringLiteral("mt");
    case Backend::System:
        break;
    }
    return QStringLiteral("system");
}

FileExplorer::Backend FileExplorer::fromKey(const QString &key)
{
    if (key == QLatin1String("builtin"))
        return Backend::Builtin;
    if (key == QLatin1String("mt"))
        return Backend::MtManager;
    return Backend::System;
}

FileExplorer::Backend FileExplorer::backend()
{
    // 设置键 fileExplorer/backend：system（默认）/ builtin / mt
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
    if (path.isEmpty())
        return;

    const QFileInfo fi(path);
    const QString dir = fi.isDir() ? path : fi.absolutePath();
    if (dir.isEmpty())
        return;

    const Backend b = [parent]() {
        Backend bb = backend();
#ifdef Q_OS_ANDROID
        // 安卓：系统资源管理器无法打开应用内目录，System 回退到内置浏览器
        if (bb == Backend::System && parent)
            bb = Backend::Builtin;
#endif
        return bb;
    }();

    // MT 管理器（仅安卓）：未安装或 Intent 被拒时继续走下面的回退
    if (b == Backend::MtManager
        && AndroidBridge::openPathWith(mtManagerPackage(), dir,
                                       QStringLiteral("resource/folder"))) {
        return;
    }

    if ((b == Backend::Builtin || b == Backend::MtManager) && parent) {
        AppFileDialog dlg(parent, AppFileDialog::ExistingDirectory);
        dlg.setWindowTitle(QCoreApplication::translate("FileExplorer", "文件资源管理器"));
        dlg.setInitialDirectory(dir);
        dlg.exec();
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
