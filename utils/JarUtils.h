/**
 * @file   JarUtils.h
 * @brief  JAR文件提取工具函数
 * @author BlockBox Team
 * @date   2026-06-05
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QMap>
#include <QFileInfo>

namespace JarUtils {

// ========== 公开 API ==========

bool extractFromJar(const QString &jarPath, const QString &entryPath, const QString &destPath);

bool extractFromJarToString(const QString &jarPath, const QString &entryPath, QString &content);

/**
 * @brief 直接从JAR中提取条目到内存（跳过临时文件，高效版）
 * @param jarPath JAR 文件路径
 * @param entryPath ZIP 条目路径
 * @param data 输出参数，条目内容
 * @return true 成功，false 失败
 */
bool extractFromJarToMemory(const QString &jarPath, const QString &entryPath, QByteArray &data);

/**
 * @brief 批量从同一个JAR中提取多个条目到内存
 * @param jarPath JAR 文件路径
 * @param entryPaths 条目路径列表
 * @param results 输出参数，key=条目路径，value=内容（不存在的条目不会出现在结果中）
 * @return 成功提取的条目数
 */
int extractMultipleFromJarToMemory(const QString &jarPath,
                                   const QStringList &entryPaths,
                                   QMap<QString, QByteArray> &results);

bool extractJarDirectory(const QString &jarPath, const QString &dirPath, const QString &destDir);

/**
 * @brief 列出 JAR 中所有条目路径（仅路径，不解压内容）
 * @param jarPath   JAR 文件路径
 * @param prefix    路径前缀过滤（如 "assets/minecraft/blockstates/"），空字符串返回全部
 * @return 匹配的条目路径列表
 */
QStringList listEntriesInJar(const QString &jarPath, const QString &prefix = QString());

// ========== 内部实现（公开以保持兼容） ==========

bool tryExtractWithJarTool(const QString &jarPath, const QString &entryPath, const QString &destPath);
bool tryExtractWithPowerShell(const QString &jarPath, const QString &entryPath, const QString &destPath);
bool tryExtractWithPython(const QString &jarPath, const QString &entryPath, const QString &destPath);
bool tryExtractWithUnzip(const QString &jarPath, const QString &entryPath, const QString &destPath);
bool tryExtractManual(const QString &jarPath, const QString &entryPath, const QString &destPath);

bool tryExtractJarDirectoryWithPowerShell(const QString &jarPath, const QString &dirPath, const QString &destDir);
bool tryExtractJarDirectoryWithPython(const QString &jarPath, const QString &dirPath, const QString &destDir);
bool tryExtractJarDirectoryWithUnzip(const QString &jarPath, const QString &dirPath, const QString &destDir);
bool tryExtractJarDirectoryManual(const QString &jarPath, const QString &dirPath, const QString &destDir);

QByteArray decompressRawDeflate(const QByteArray &compressedData, quint32 uncompressedSize);

/**
 * @brief 预加载zlib库，避免每次解压时动态加载
 */
void ensureZlibLoaded();

} // namespace JarUtils