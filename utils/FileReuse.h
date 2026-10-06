/**
 * @file   FileReuse.h
 * @brief  安装新实例时的本地文件复用工具
 *         参考 HMCL/PCL2 的做法：安装新实例前，先在本机其他实例/共享目录中
 *         寻找内容相同的文件（按 manifest 提供的大小校验，必要时按 SHA1 强校验），
 *         通过硬链接（同卷，零拷贝）或复制（跨卷）复用，避免重复下载。
 * @author BlockBox Team
 * @date   2026-10-04
 */
#ifndef FILEREUSE_H
#define FILEREUSE_H

#include <QString>
#include <QStringList>

namespace FileReuse {

/** 判断 path 是否为大小与 expectedSize 一致的文件；expectedSize<=0 时退化为存在性判断 */
bool sizeMatches(const QString &path, qint64 expectedSize);

/**
 * @brief 将 src 以硬链接方式落到 dst，失败（跨卷/文件系统不支持）时退回复制。
 *        dst 已存在时会先删除（仅在被判定为损坏需要替换时才会走到这里）。
 *        src 与 dst 为同一物理文件时直接返回 true，不做任何操作。
 */
bool linkOrCopy(const QString &src, const QString &dst);

/**
 * @brief 用本地已有文件填充 targetPath，避免重新下载。
 *        1) targetPath 已存在且大小一致 → 视为有效，直接返回 true（已在位）
 *        2) 依次检查 candidatePaths：大小一致（可选 SHA1 强校验）→ linkOrCopy 到 targetPath
 *        3) 无可复用候选 → 返回 false，调用方走网络下载
 * @param targetPath     目标文件的绝对路径（可能尚不存在）
 * @param expectedSize   manifest 声明的文件大小；<=0 时只做存在性判断
 * @param candidatePaths 其他实例/共享目录中同内容候选文件的绝对路径
 * @param expectedSha1   可选的 SHA1 强校验（小体积关键文件如客户端 JAR 使用）
 */
bool tryFillFromFilesystem(const QString &targetPath, qint64 expectedSize,
                           const QStringList &candidatePaths,
                           const QString &expectedSha1 = QString());

} // namespace FileReuse

#endif // FILEREUSE_H
