/**
 * @file   FileReuse.cpp
 * @brief  安装新实例时的本地文件复用工具实现
 * @author BlockBox Team
 * @date   2026-10-04
 */
#include "FileReuse.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

/** 计算文件 SHA1，返回小写十六进制字符串；无法读取时返回空串 */
QString calculateSha1(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha1);
    const qint64 chunkSize = 1024 * 1024; // 1MB chunks
    while (!file.atEnd())
    {
        hash.addData(file.read(chunkSize));
    }
    return QString(hash.result().toHex());
}

} // namespace

namespace FileReuse {

bool sizeMatches(const QString &path, qint64 expectedSize)
{
    QFileInfo fi(path);
    if (!fi.isFile())
        return false;
    if (expectedSize <= 0)
        return true; // manifest 未提供大小时退化为存在性判断
    return fi.size() == expectedSize;
}

bool linkOrCopy(const QString &src, const QString &dst)
{
    QFileInfo srcInfo(src);
    if (!srcInfo.isFile())
        return false;

    // 防自链接：候选与目标是同一物理文件时无需任何操作
    const QString srcCanonical = srcInfo.canonicalFilePath();
    if (!srcCanonical.isEmpty() && srcCanonical == QFileInfo(dst).canonicalFilePath())
        return true;

    QDir().mkpath(QFileInfo(dst).absolutePath());

    QFile dstFile(dst);
    if (dstFile.exists() && !dstFile.remove())
        return false;

#ifdef Q_OS_WIN
    const QString dstNative = QDir::toNativeSeparators(dst);
    const QString srcNative = QDir::toNativeSeparators(src);
    if (CreateHardLinkW(reinterpret_cast<LPCWSTR>(dstNative.utf16()),
                        reinterpret_cast<LPCWSTR>(srcNative.utf16()),
                        nullptr))
    {
        return true;
    }
#else
    if (::link(QFile::encodeName(src).constData(),
               QFile::encodeName(dst).constData()) == 0)
    {
        return true;
    }
#endif

    // 跨卷/文件系统不支持硬链接 → 退化为复制（仍远快于重新下载）
    return QFile::copy(src, dst);
}

bool tryFillFromFilesystem(const QString &targetPath, qint64 expectedSize,
                           const QStringList &candidatePaths,
                           const QString &expectedSha1)
{
    if (sizeMatches(targetPath, expectedSize))
        return true; // 已在位

    for (const QString &candidate : candidatePaths)
    {
        if (candidate == targetPath)
            continue;
        if (!sizeMatches(candidate, expectedSize))
            continue;

        if (!expectedSha1.isEmpty())
        {
            // 调用方提供了哈希（关键小文件）时做强校验，避免复用到损坏文件
            const QString actual = calculateSha1(candidate);
            if (actual.isEmpty() || actual != expectedSha1)
                continue;
        }

        if (linkOrCopy(candidate, targetPath))
            return true;
    }
    return false;
}

} // namespace FileReuse
