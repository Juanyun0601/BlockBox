// Licensed under the Apache-2.0 license. See README.md for details.
//
// Ported from MultiMC launcher/FileSystem.{h,cpp} (Apache-2.0).
// Only the helpers used by the net/ layer are carried over; FS::write
// reports failure by return value instead of throwing (BlockBox does not
// use exceptions).

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QDebug>

namespace FS
{

/**
 * write data to a file safely
 */
inline bool write(const QString &filename, const QByteArray &data)
{
    QFileInfo info(filename);
    if (!QDir().mkpath(info.absolutePath()))
    {
        qWarning() << "Couldn't create folder for" << filename;
        return false;
    }
    QSaveFile file(filename);
    if (!file.open(QSaveFile::WriteOnly))
    {
        qWarning() << "Couldn't open" << filename << "for writing:" << file.errorString();
        return false;
    }
    if (data.size() != file.write(data))
    {
        qWarning() << "Error writing data to" << filename << ":" << file.errorString();
        return false;
    }
    if (!file.commit())
    {
        qWarning() << "Error while committing data to" << filename << ":" << file.errorString();
        return false;
    }
    return true;
}

/**
 * Creates all the folders in a path for the specified path
 * last segment of the path is treated as a file name and is ignored!
 */
inline bool ensureFilePathExists(QString filenamepath)
{
    QFileInfo a(filenamepath);
    QDir dir;
    QString ensuredPath = a.path();
    bool success = dir.mkpath(ensuredPath);
    return success;
}

/**
 * Creates all the folders in a path for the specified path
 * last segment of the path is treated as a folder name and is created!
 */
inline bool ensureFolderPathExists(QString foldernamepath)
{
    QFileInfo a(foldernamepath);
    QDir dir;
    QString ensuredPath = a.filePath();
    bool success = dir.mkpath(ensuredPath);
    return success;
}

inline QString PathCombine(const QString &path1, const QString &path2)
{
    if(!path1.size())
        return path2;
    if(!path2.size())
        return path1;
    return QDir::cleanPath(path1 + QDir::separator() + path2);
}

inline QString PathCombine(const QString &path1, const QString &path2, const QString &path3)
{
    return PathCombine(PathCombine(path1, path2), path3);
}

inline QString PathCombine(const QString &path1, const QString &path2, const QString &path3, const QString &path4)
{
    return PathCombine(PathCombine(path1, path2, path3), path4);
}

}
