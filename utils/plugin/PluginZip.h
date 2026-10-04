/**
 * @file   PluginZip.h
 * @brief  插件 ZIP 读写工具声明（基于 zlib，无第三方依赖）
 * @author BlockBox Team
 * @date   2026-08-05
 *
 * .BlockBox 插件文件本质是 zip 压缩包，仅扩展名不同。
 * 本工具提供：列出条目、提取条目到内存、将目录打包为 zip。
 */
#ifndef PLUGINZIP_H
#define PLUGINZIP_H

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace PluginZip {

/**
 * @brief 列出 zip 文件中的所有条目路径
 * @param zipPath zip 文件路径
 * @param ok      输出参数：解析成功与否，可为 nullptr
 */
QStringList listEntries(const QString &zipPath, bool *ok = nullptr);

/**
 * @brief 提取 zip 中指定条目到内存
 * @param zipPath   zip 文件路径
 * @param entryPath 条目路径（区分大小写，目录条目无内容）
 * @param data      输出参数：条目内容
 * @return true 成功
 */
bool extractEntryToMemory(const QString &zipPath, const QString &entryPath, QByteArray &data);

/**
 * @brief 从 zip 中提取条目为字符串（UTF-8）
 */
bool extractEntryToString(const QString &zipPath, const QString &entryPath, QString &content);

/**
 * @brief 将目录打包为 zip 文件（STORE 或 DEFLATE 压缩）
 * @param srcDir  源目录绝对路径
 * @param zipPath 输出 zip 文件路径
 * @param ok      输出参数：成功与否，可为 nullptr
 * @return 打包的文件条目数，失败返回 -1
 */
int zipDirectory(const QString &srcDir, const QString &zipPath, bool *ok = nullptr);

/**
 * @brief 将 zip 包内所有条目解压到指定目录
 * @param zipPath zip 文件路径
 * @param destDir 目标目录绝对路径（不存在时自动创建）
 * @param ok      输出参数：成功与否，可为 nullptr
 * @return 解压的文件条目数，失败返回 -1
 */
int extractAllToDir(const QString &zipPath, const QString &destDir, bool *ok = nullptr);

} // namespace PluginZip

#endif // PLUGINZIP_H
