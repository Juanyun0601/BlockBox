/**
 * @file   RuntimeArchive.h
 * @brief  运行依赖包解压（zip / tar.gz），不依赖系统外部命令
 * @author BlockBox Team
 * @date   2026-10-23
 *
 * 安卓端无法保证设备上有 unzip / tar 等命令，因此运行依赖包的解压
 * 全部用 zlib 在进程内完成：
 *   - .zip       中央目录 + 原始 deflate 流式解压
 *   - .tar.gz    先解 gzip 到临时文件，再按 tar 格式解包
 */

#ifndef RUNTIMEARCHIVE_H
#define RUNTIMEARCHIVE_H

#include <QString>

#include <functional>

namespace RuntimeArchive {

/** 解压进度回调：percent 0-100，stage 为当前阶段的简短描述 */
using ProgressCallback = std::function<void(int percent, const QString &stage)>;

/** 判断文件是否为受支持的依赖包格式（zip / tar.gz），按文件头识别 */
bool isSupportedArchive(const QString &archivePath);

/**
 * @brief 解压依赖包到 destDir
 * @param archivePath    压缩包路径
 * @param destDir        目标目录（不存在时自动创建）
 * @param errorMessage   失败原因（可选输出）
 * @param onProgress     进度回调（可选）
 * @return 成功返回 true
 */
bool extract(const QString &archivePath,
             const QString &destDir,
             QString *errorMessage = nullptr,
             const ProgressCallback &onProgress = nullptr);

} // namespace RuntimeArchive

#endif // RUNTIMEARCHIVE_H
