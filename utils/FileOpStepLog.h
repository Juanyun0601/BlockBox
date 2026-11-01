/**
 * @file   FileOpStepLog.h
 * @brief  从启动器运行日志解析文件操作步骤（只读展示，不重新执行）
 * @author BlockBox Team
 * @date   2026-10-25
 *
 * 运行日志里下载 / 复制 / 解压 / 写入 / 删除等输出格式并不统一，
 * 这里按常见文案做启发式归类，整理成按时间排列的步骤列表，
 * 供设置 → 高级设置 → 运行日志里的「文件操作步骤」弹窗展示。
 */

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

/// 单条文件操作步骤（从日志一行或若干行归并而来）
struct FileOpStep
{
    QDateTime timestamp;   ///< 日志时间戳（解析失败则为空）
    QString   level;       ///< DEBUG / INFO / WARN / ERROR
    QString   module;      ///< 日志分类标签，如 MultiThreadDownloader（可为空）
    QString   kind;        ///< 归类：下载 / 复制 / 解压 / 写入 / 删除 / 安装 / 其他文件操作
    QString   title;       ///< 一行摘要（面向普通用户）
    QString   detail;      ///< 路径 / URL / 原始补充信息
    QString   rawLine;     ///< 原始日志行（调试用）
    int       sessionIndex = -1;  ///< 所属启动会话序号（0 起，按日志里出现顺序）
    bool      success = true;     ///< 是否成功（失败步骤在列表里标红）
};

/// 日志里的一次「启动会话」（从分隔头到下一个分隔头）
struct FileOpLogSession
{
    int          index = -1;
    QString      startedAt;   ///< 分隔头上的时间文本
    QString      version;     ///< 会话头里的启动器版本（可为空）
    FileOpStep   startMarker; ///< 会话头对应的占位步骤（kind 为空，仅用于排序/标注）
};

/**
 * @brief 运行日志 → 文件操作步骤
 *
 * 只读解析，不访问磁盘上的游戏文件，也不会重放任何操作。
 */
namespace FileOpStepLog {

/// 解析一段日志文本（可含多次启动），返回按时间排列的文件操作步骤
QList<FileOpStep> parseLogText(const QString &text);

/// 读取并解析多个日志文件（按传入顺序拼接后再解析；文件不存在则跳过）
QList<FileOpStep> parseLogFiles(const QStringList &paths);

/// 当前日志目录下默认应读取的文件列表（launcher.log 及轮转备份，从旧到新）
QStringList defaultLogFiles();

/// 从已解析步骤中取出会话信息（按 index 升序，去重）
QList<FileOpLogSession> collectSessions(const QList<FileOpStep> &steps);

} // namespace FileOpStepLog
