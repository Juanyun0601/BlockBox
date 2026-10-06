/**
 * @file   GameDetector.cpp
 * @brief  运行中的 Minecraft 游戏进程检测器实现
 * @author BlockBox Team
 * @date   2026-07-05
 *
 * 实现细节：
 *   1. detectRunningGame() 通过 CreateToolhelp32Snapshot 枚举系统进程，
 *      使用 _wcsicmp 大小写不敏感匹配 javaw.exe，收集所有候选 PID。
 *   2. 对每个 javaw.exe PID，调用 PowerShell 的 Get-CimInstance 获取完整命令行，
 *      超时上限 5000ms，避免 PowerShell 异常时阻塞 UI。
 *   3. parseCommandLine() 解析 --version / --gameDir / -Dminecraft.version= 三种参数，
 *      支持双引号包裹的值，并将 gameDir 中的反斜杠统一为正斜杠。
 */

#include "GameDetector.h"

#include <QDebug>
#include <QList>
#include <QProcess>

// 进程枚举依赖 Win32 API（CreateToolhelp32Snapshot），非 Windows 平台
// 上的实现为“未检测到游戏”的空实现。
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {

/**
 * @brief 提取 --flag value 形式参数的值
 * @param cmdLine 完整命令行
 * @param flag 标记名（如 "--version"）
 * @return 标记后的值（已去除引号），未找到返回空字符串
 *
 * 仅匹配作为独立 token 出现的 flag（前后为空白或字符串边界），
 * 避免误匹配 --versionType 之类的前缀包含 --version 的情况。
 */
QString extractFlagValue(const QString &cmdLine, const QString &flag)
{
  int searchFrom = 0;
  while (true)
  {
    int idx = cmdLine.indexOf(flag, searchFrom, Qt::CaseSensitive);
    if (idx < 0)
    {
      return QString();
    }

    bool atStart = (idx == 0);
    bool prevIsSpace = (idx > 0 && cmdLine[idx - 1].isSpace());
    int afterFlag = idx + flag.length();
    bool followedBySpaceOrQuote = (afterFlag >= cmdLine.length())
      || cmdLine[afterFlag].isSpace()
      || cmdLine[afterFlag] == QLatin1Char('"');

    if ((atStart || prevIsSpace) && followedBySpaceOrQuote)
    {
      int valueStart = afterFlag;
      while (valueStart < cmdLine.length() && cmdLine[valueStart].isSpace())
      {
        ++valueStart;
      }
      if (valueStart >= cmdLine.length())
      {
        return QString();
      }

      if (cmdLine[valueStart] == QLatin1Char('"'))
      {
        ++valueStart;
        int endQuote = cmdLine.indexOf(QLatin1Char('"'), valueStart);
        if (endQuote < 0)
        {
          return cmdLine.mid(valueStart);
        }
        return cmdLine.mid(valueStart, endQuote - valueStart);
      }

      int valueEnd = valueStart;
      while (valueEnd < cmdLine.length() && !cmdLine[valueEnd].isSpace())
      {
        ++valueEnd;
      }
      return cmdLine.mid(valueStart, valueEnd - valueStart);
    }

    searchFrom = idx + 1;
  }
}

/**
 * @brief 提取 -Dkey=value 形式参数的值
 * @param cmdLine 完整命令行
 * @param key 完整键串（如 "-Dminecraft.version="）
 * @return 等号后的值（直至空白），未找到返回空字符串
 */
QString extractDPropertyValue(const QString &cmdLine, const QString &key)
{
  int searchFrom = 0;
  while (true)
  {
    int idx = cmdLine.indexOf(key, searchFrom, Qt::CaseSensitive);
    if (idx < 0)
    {
      return QString();
    }

    bool atStart = (idx == 0);
    bool prevIsSpace = (idx > 0 && cmdLine[idx - 1].isSpace());
    if (!(atStart || prevIsSpace))
    {
      searchFrom = idx + 1;
      continue;
    }

    int valueStart = idx + key.length();
    int valueEnd = valueStart;
    while (valueEnd < cmdLine.length() && !cmdLine[valueEnd].isSpace())
    {
      ++valueEnd;
    }
    return cmdLine.mid(valueStart, valueEnd - valueStart);
  }
}

} // namespace

// ============================================================================
// 公开接口实现
// ============================================================================

bool GameDetector::isJavaGameRunning()
{
#ifdef Q_OS_WIN
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
  {
    return false;
  }

  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  bool found = false;

  if (Process32FirstW(snapshot, &pe))
  {
    do
    {
      if (_wcsicmp(pe.szExeFile, L"javaw.exe") == 0)
      {
        found = true;
        break;
      }
    } while (Process32NextW(snapshot, &pe));
  }

  CloseHandle(snapshot);
  return found;
#else
  return false;
#endif
}

std::optional<GameProcessInfo> GameDetector::detectRunningGame()
{
#ifdef Q_OS_WIN
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
  {
    qWarning() << "[GameDetector]"
               << "CreateToolhelp32Snapshot failed, error =" << GetLastError();
    return std::nullopt;
  }

  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);

  if (!Process32FirstW(snapshot, &pe))
  {
    CloseHandle(snapshot);
    return std::nullopt;
  }

  // 先收集所有 javaw.exe 的 PID，再关闭快照，避免 PowerShell 阻塞期间占用快照句柄
  QList<qint64> javaPids;
  do
  {
    if (_wcsicmp(pe.szExeFile, L"javaw.exe") == 0)
    {
      javaPids.append(static_cast<qint64>(pe.th32ProcessID));
    }
  } while (Process32NextW(snapshot, &pe));

  CloseHandle(snapshot);

  if (javaPids.isEmpty())
  {
    return std::nullopt;
  }

  for (qint64 pid : javaPids)
  {
    // 调用 PowerShell 获取完整命令行
    QString script = QString::fromUtf8(
      "Get-CimInstance Win32_Process -Filter \"ProcessId=%1\" | "
      "Select-Object -ExpandProperty CommandLine").arg(pid);

    QProcess process;
    process.setProgram(QStringLiteral("powershell"));
    process.setArguments({QStringLiteral("-NoProfile"),
                          QStringLiteral("-NonInteractive"),
                          QStringLiteral("-Command"),
                          script});
    process.start();

    if (!process.waitForFinished(5000))
    {
      qWarning() << "[GameDetector]" << "PowerShell timeout for PID" << pid;
      process.kill();
      process.waitForFinished(1000);
      continue;
    }

    if (process.exitCode() != 0)
    {
      qWarning() << "[GameDetector]"
                 << "PowerShell exit code" << process.exitCode()
                 << "for PID" << pid;
      continue;
    }

    QString commandLine = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (commandLine.isEmpty())
    {
      continue;
    }

    GameProcessInfo info = parseCommandLine(commandLine);
    info.pid = pid;

    // 仅当解析到有效版本或游戏目录时视为命中
    if (!info.version.isEmpty() || !info.gameDir.isEmpty())
    {
      return info;
    }
  }

  return std::nullopt;
#else
  return std::nullopt;
#endif
}

// ============================================================================
// 私有实现
// ============================================================================

GameProcessInfo GameDetector::parseCommandLine(const QString &commandLine)
{
  GameProcessInfo info;

  // 1. 解析 --version <X>
  info.version = extractFlagValue(commandLine, QStringLiteral("--version"));

  // 2. 解析 --gameDir <Y>，规范化路径分隔符为正斜杠
  info.gameDir = extractFlagValue(commandLine, QStringLiteral("--gameDir"));
  info.gameDir.replace(QLatin1Char('\\'), QLatin1Char('/'));

  // 3. 若 version 为空，回退到 -Dminecraft.version=<X>
  if (info.version.isEmpty())
  {
    info.version = extractDPropertyValue(commandLine,
                                         QStringLiteral("-Dminecraft.version="));
  }

  // 4. 若 gameDir 与 version 均存在，构造版本 JAR 路径
  if (!info.gameDir.isEmpty() && !info.version.isEmpty())
  {
    info.jarPath = info.gameDir
                 + QStringLiteral("/versions/")
                 + info.version
                 + QStringLiteral("/")
                 + info.version
                 + QStringLiteral(".jar");
  }

  return info;
}
