/**
 * @file   CommandInjector.cpp
 * @brief  Minecraft 指令提取与一键注入工具实现
 * @author BlockBox Team
 * @date   2026-09-14
 */

#include "CommandInjector.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include "utils/CommandAssistant/CommandDatabase.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

// ============================================================================
// 指令提取
// ============================================================================

QStringList CommandInjector::extractCommands(const QString &markdown)
{
    QStringList result;
    if (markdown.isEmpty())
    {
        return result;
    }
    // 快速排除：没有任何 '/' 的文本不可能包含指令，避免无谓地加载指令数据库
    if (!markdown.contains(QLatin1Char('/')))
    {
        return result;
    }

    // 懒加载内置指令数据库，用于校验正文 / 通用代码块中的 "/xxx" 是否为真实指令，
    // 避免把文件路径（/usr/bin）、URL 片段等误判为指令。
    static CommandDatabase db;
    static bool dbLoaded = false;
    if (!dbLoaded)
    {
        dbLoaded = true;
        db.load();
    }

    QSet<QString> seen;

    auto addCommand = [&result, &seen](QString cmd) {
        cmd = cmd.trimmed();
        if (cmd.isEmpty())
        {
            return;
        }
        if (!cmd.startsWith(QLatin1Char('/')))
        {
            cmd.prepend(QLatin1Char('/'));
        }
        if (!seen.contains(cmd))
        {
            seen.insert(cmd);
            result.append(cmd);
        }
    };

    // 判断一行是否像指令：以 '/' 开头，且首个单词是已知 Minecraft 指令名
    auto looksLikeCommand = [](const QString &text) -> bool {
        const QString line = text.trimmed();
        if (!line.startsWith(QLatin1Char('/')))
        {
            return false;
        }
        int end = 1;
        while (end < line.size())
        {
            const QChar c = line.at(end);
            if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')
                || c == QLatin1Char(':'))
            {
                ++end;
            }
            else
            {
                break;
            }
        }
        const QString name = line.mid(1, end - 1).toLower();
        if (name.isEmpty())
        {
            return false;
        }
        // 兼容带命名空间的写法（如 /minecraft:give）
        const int colon = name.indexOf(QLatin1Char(':'));
        const QString bare = (colon >= 0) ? name.mid(colon + 1) : name;
        return db.findCommandByEnglish(bare).has_value();
    };

    const QStringList lines = markdown.split(QLatin1Char('\n'));
    bool inFence = false;
    QString fenceLang;

    for (QString line : lines)
    {
        if (line.endsWith(QLatin1Char('\r')))
        {
            line.chop(1);
        }
        const QString trimmed = line.trimmed();

        // 代码块围栏（``` 或 ~~~）
        if (trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~")))
        {
            if (!inFence)
            {
                inFence = true;
                fenceLang = trimmed.mid(3).trimmed().toLower();
            }
            else
            {
                inFence = false;
                fenceLang.clear();
            }
            continue;
        }

        if (inFence)
        {
            const bool taggedMc = fenceLang.startsWith(QStringLiteral("mc"))
                                  || fenceLang.startsWith(QStringLiteral("minecraft"))
                                  || fenceLang == QStringLiteral("commands");
            if (taggedMc)
            {
                // mc 代码块：块内每行都视为指令，跳过注释与空行
                if (trimmed.isEmpty() || trimmed.startsWith(QStringLiteral("//"))
                    || trimmed.startsWith(QLatin1Char('#')))
                {
                    continue;
                }
                addCommand(trimmed);
            }
            else if (looksLikeCommand(trimmed))
            {
                addCommand(trimmed);
            }
            continue;
        }

        // 正文行
        if (looksLikeCommand(trimmed))
        {
            addCommand(trimmed);
        }

        // 正文中的行内代码 `...`
        static const QRegularExpression inlineCode(QStringLiteral("`([^`]+)`"));
        QRegularExpressionMatchIterator it = inlineCode.globalMatch(line);
        while (it.hasNext())
        {
            const QString span = it.next().captured(1).trimmed();
            if (looksLikeCommand(span))
            {
                addCommand(span);
            }
        }
    }

    return result;
}

// ============================================================================
// 游戏窗口注入
// ============================================================================

#ifdef Q_OS_WIN
namespace {

/**
 * @brief 枚举所有顶级窗口，查找属于指定进程 ID 的可见窗口
 * @param pid 目标进程 ID
 * @return 找到的窗口句柄（未找到返回 nullptr）
 */
HWND findWindowByPid(DWORD pid)
{
    struct EnumData
    {
        DWORD targetPid;
        HWND result;
    };
    EnumData data{pid, nullptr};

    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lParam);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(hWnd, &windowPid);
        if (windowPid != d->targetPid)
        {
            return TRUE;
        }
        if (!IsWindowVisible(hWnd))
        {
            return TRUE;
        }
        // 优先选择有标题的窗口
        wchar_t title[256] = {0};
        GetWindowTextW(hWnd, title, 256);
        if (wcslen(title) > 0)
        {
            d->result = hWnd;
            return FALSE;
        }
        if (d->result == nullptr)
        {
            d->result = hWnd;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.result;
}

/**
 * @brief 查找运行中的 javaw.exe 进程 ID
 * @return 第一个 javaw.exe 的 PID（未找到返回 0）
 */
DWORD findJavaProcessId()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"javaw.exe") == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return pid;
}

/**
 * @brief 模拟按键按下与释放
 * @param vk 虚拟键码
 */
void sendKeyPress(WORD vk)
{
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[0].ki.wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

/**
 * @brief 在 UI 线程上延时等待但不完全冻结界面
 * @param ms 毫秒
 *
 * 相比裸 Sleep()，会周期性处理事件，使窗口可重绘、响应部分点击，
 * 同时保持按键注入步骤的顺序时序。
 */
void uiDelayMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    QTimer ticker;
    ticker.setInterval(20);
    QObject::connect(&ticker, &QTimer::timeout, []() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    });
    ticker.start();
    loop.exec();
}

} // namespace
#endif // Q_OS_WIN

bool CommandInjector::isGameRunning()
{
#ifdef Q_OS_WIN
    return findJavaProcessId() != 0;
#else
    return false;
#endif
}

bool CommandInjector::injectToGame(const QString &command, QString *errorMessage)
{
    const QString trimmedCommand = command.trimmed();
    if (trimmedCommand.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("指令为空");
        }
        return false;
    }

#ifdef Q_OS_WIN
    // 1. 查找运行中的 Minecraft 进程
    const DWORD pid = findJavaProcessId();
    if (pid == 0)
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("未找到游戏");
        }
        return false;
    }

    // 2. 查找游戏窗口
    HWND gameWnd = findWindowByPid(pid);
    if (gameWnd == nullptr)
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("未找到窗口");
        }
        return false;
    }

    // 3. 将窗口置前
    if (IsIconic(gameWnd))
    {
        ShowWindow(gameWnd, SW_RESTORE);
    }
    SetForegroundWindow(gameWnd);
    uiDelayMs(150); // 等待窗口切换完成

    // 4. 复制指令到剪贴板（不含前导 '/'，稍后通过按键注入 '/'）
    QString commandBody = trimmedCommand;
    if (commandBody.startsWith(QLatin1Char('/')))
    {
        commandBody = commandBody.mid(1);
    }
    QApplication::clipboard()->setText(commandBody);
    uiDelayMs(50);

    // 5. 发送 '/' 打开聊天框
    sendKeyPress(VK_OEM_2); // '/' 键
    uiDelayMs(100);

    // 6. Ctrl+V 粘贴指令内容
    INPUT ctrlDown = {};
    ctrlDown.type = INPUT_KEYBOARD;
    ctrlDown.ki.wVk = VK_CONTROL;
    SendInput(1, &ctrlDown, sizeof(INPUT));

    sendKeyPress('V');

    INPUT ctrlUp = {};
    ctrlUp.type = INPUT_KEYBOARD;
    ctrlUp.ki.wVk = VK_CONTROL;
    ctrlUp.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &ctrlUp, sizeof(INPUT));

    uiDelayMs(100);

    // 7. 发送 Enter 执行指令
    sendKeyPress(VK_RETURN);

    return true;
#else
    if (errorMessage)
    {
        *errorMessage = QObject::tr("当前平台不支持一键注入");
    }
    return false;
#endif
}
