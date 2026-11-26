/**
 * @file   GameScreenshot.cpp
 * @brief  抓取游戏画面实现
 * @author BlockBox Team
 * @date   2026-11-21
 *
 * 实现要点：
 *   1. 抓屏前先把调用方窗口藏起来，并留出约 200ms 让系统重绘被露出的游戏画面；
 *   2. Windows 上枚举 javaw.exe / java.exe 的顶层窗口，取面积最大的那个当作游戏窗口，
 *      按 DWM 实测边框（不含窗口阴影）换算成 Qt 逻辑坐标后只抓这一块；
 *   3. 找不到游戏窗口或抓取失败时退回整屏截图，保证「截图」按钮不会白按一次。
 */

#include "GameScreenshot.h"

#include <QBuffer>
#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#endif

namespace {

/// 等待 ms 毫秒，期间继续处理事件：让被露出的游戏画面先由系统重绘出来
void waitWithEvents(int ms)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(ms);
    loop.exec();
}

/// 抓整块主屏（各平台的保底路径）
QImage grabPrimaryScreen()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
    {
        return QImage();
    }
    const QPixmap pm = screen->grabWindow(0);
    return pm.isNull() ? QImage() : pm.toImage();
}

/// 画面是不是「一片纯色」：抓黑图 / 系统不给画面时用来判失败，免得把全黑图发给 AI
bool looksBlank(const QImage &image)
{
    if (image.isNull() || image.width() < 8 || image.height() < 8)
    {
        return true;
    }

    const QImage img = image.convertToFormat(QImage::Format_RGB32);
    int minLuma = 255;
    int maxLuma = 0;
    // 隔点采样就够了，几百万像素逐点算太慢
    for (int y = 0; y < img.height(); y += 7)
    {
        for (int x = 0; x < img.width(); x += 7)
        {
            const int luma = qGray(img.pixel(x, y));
            minLuma = qMin(minLuma, luma);
            maxLuma = qMax(maxLuma, luma);
        }
    }
    return (maxLuma - minLuma) <= 4;
}

#ifdef Q_OS_WIN
/// 枚举 javaw.exe / java.exe 进程（Minecraft 由 Java 启动）
QList<DWORD> collectJavaPids()
{
    QList<DWORD> pids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return pids;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, L"javaw.exe") == 0
                || _wcsicmp(entry.szExeFile, L"java.exe") == 0)
            {
                pids.append(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pids;
}

/// 窗口在桌面上的实际外框（优先 DWM 实测边框，避免把窗口阴影也算进来）
QRect windowFrameRect(HWND hwnd)
{
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &rect, sizeof(rect)))
        && !GetWindowRect(hwnd, &rect))
    {
        return QRect();
    }
    return QRect(QPoint(rect.left, rect.top),
                 QSize(rect.right - rect.left, rect.bottom - rect.top));
}

/// 找游戏窗口时的遍历上下文
struct GameWindowSearch
{
    const QList<DWORD> *pids = nullptr;
    HWND best = nullptr;
    QRect bestRect;
    QString bestTitle;
    qint64 bestArea = 0;
};

BOOL CALLBACK enumGameWindowProc(HWND hwnd, LPARAM lParam)
{
    auto *search = reinterpret_cast<GameWindowSearch *>(lParam);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!search->pids->contains(pid))
    {
        return TRUE;
    }
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
    {
        return TRUE;
    }
    // 只认顶层窗口：日志窗、提示框之类的子窗/附属窗不参与比较
    if (GetWindow(hwnd, GW_OWNER) != nullptr)
    {
        return TRUE;
    }

    const QRect rect = windowFrameRect(hwnd);
    // 游戏画面不可能比这更小；顺便滤掉启动过程中一闪而过的小窗
    if (rect.width() < 320 || rect.height() < 240)
    {
        return TRUE;
    }

    const qint64 area = qint64(rect.width()) * qint64(rect.height());
    if (area <= search->bestArea)
    {
        return TRUE;
    }

    wchar_t title[512] = {0};
    GetWindowTextW(hwnd, title, 512);

    search->best = hwnd;
    search->bestRect = rect;
    search->bestTitle = QString::fromWCharArray(title);
    search->bestArea = area;
    return TRUE;
}

/// 取面积最大的 Java 游戏窗口（同一进程可能还有小工具窗）
HWND findGameWindow(QRect *rectOut, QString *titleOut)
{
    const QList<DWORD> pids = collectJavaPids();
    if (pids.isEmpty())
    {
        return nullptr;
    }

    GameWindowSearch search;
    search.pids = &pids;
    EnumWindows(enumGameWindowProc, reinterpret_cast<LPARAM>(&search));

    if (!search.best)
    {
        return nullptr;
    }
    if (rectOut)
    {
        *rectOut = search.bestRect;
    }
    if (titleOut)
    {
        *titleOut = search.bestTitle;
    }
    return search.best;
}

/// 把 Win32 的物理像素矩形换算成 Qt 逻辑像素矩形（抓屏接口吃逻辑坐标）
QRect physicalToLogicalRect(const QRect &physical)
{
    if (physical.isEmpty())
    {
        return QRect();
    }

    const QPoint center = physical.center();
    for (QScreen *screen : QGuiApplication::screens())
    {
        if (!screen)
        {
            continue;
        }
        const qreal dpr = screen->devicePixelRatio();
        if (dpr <= 0)
        {
            continue;
        }
        // 每块屏幕的物理范围 = 逻辑范围 × 缩放比，用窗口中心落在哪块屏判断
        const QRect screenPhysical(screen->geometry().topLeft() * dpr,
                                   screen->geometry().size() * dpr);
        if (!screenPhysical.contains(center))
        {
            continue;
        }
        return QRect(QPoint(qRound(physical.x() / dpr), qRound(physical.y() / dpr)),
                     QSize(qRound(physical.width() / dpr),
                           qRound(physical.height() / dpr)));
    }

    // 找不到匹配屏幕（多屏排布异常）：按原样返回，至少不会比整屏更糟
    return physical;
}
#endif // Q_OS_WIN

} // namespace

GameScreenshot::Result GameScreenshot::capture(const QList<QWidget *> &windowsToHide)
{
    Result result;

    // 1. 先藏起会挡在游戏前面的窗口（实例助手悬浮窗、手机端输入弹窗…）
    QList<QPointer<QWidget>> hiddenWindows;
    for (QWidget *widget : windowsToHide)
    {
        if (!widget)
        {
            continue;
        }
        QWidget *top = widget->window();
        if (!top || !top->isVisible() || hiddenWindows.contains(top))
        {
            continue;
        }
        hiddenWindows.append(top);
        top->hide();
    }
    if (!hiddenWindows.isEmpty())
    {
        // 窗口刚藏起来时屏幕上还是旧画面，等系统把游戏画面重绘出来
        waitWithEvents(200);
    }

    // 2. Windows：优先只截游戏窗口那一块
#ifdef Q_OS_WIN
    QRect gameRect;
    QString gameTitle;
    HWND gameWindow = findGameWindow(&gameRect, &gameTitle);
    if (gameWindow)
    {
        // 万一游戏还被别的窗口压着，抓到的是别的界面，先把它提到最前
        SetForegroundWindow(gameWindow);
        waitWithEvents(80);

        const QRect logicalRect = physicalToLogicalRect(gameRect);
        QScreen *screen = QGuiApplication::screenAt(logicalRect.center());
        if (!screen)
        {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen && !logicalRect.isEmpty())
        {
            const QRect screenGeo = screen->geometry();
            const QRect inter = logicalRect.intersected(screenGeo);
            if (inter.width() >= 8 && inter.height() >= 8)
            {
                const QPixmap pm = screen->grabWindow(0, inter.x() - screenGeo.x(),
                                                      inter.y() - screenGeo.y(),
                                                      inter.width(), inter.height());
                if (!pm.isNull())
                {
                    result.image = pm.toImage();
                    result.windowTitle = gameTitle;
                }
            }
        }
    }
#endif

    // 3. 没找到游戏窗口 / 抓取失败：退回整屏截图
    if (result.image.isNull())
    {
        result.image = grabPrimaryScreen();
        result.windowTitle.clear();
    }

    // 4. 恢复窗口（无论成败都要恢复，不能把用户窗口弄丢）
    for (const QPointer<QWidget> &widget : hiddenWindows)
    {
        if (!widget)
        {
            continue;
        }
        widget->show();
        widget->raise();
        widget->activateWindow();
    }

    if (result.image.isNull())
    {
        result.error = QStringLiteral("没能截到画面，请确认游戏窗口没有被最小化。");
    }
    else if (result.image.width() < 8 || result.image.height() < 8 || looksBlank(result.image))
    {
        // 抓成纯色基本都是系统没给画面（黑图），当失败处理，别把黑图发给 AI
        result.image = QImage();
        result.error = QStringLiteral("没截到有效画面（可能是游戏还没渲染出画面，或屏幕被别的窗口挡住），请稍后再试。");
    }

    return result;
}

QString GameScreenshot::toJpegDataUrl(const QImage &image, QByteArray *outBytes)
{
    if (image.isNull())
    {
        return QString();
    }

    // 统一转成不含 alpha 的 RGB，JPEG 编码更稳
    QImage img = image.convertToFormat(QImage::Format_RGB32);
    const int maxSide = 1280;
    if (img.width() > maxSide || img.height() > maxSide)
    {
        img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // 逐级降质：先把单张控制在 900KB 以内（base64 后约 1.2MB）
    QByteArray bytes;
    const int qualities[] = {82, 70, 55};
    for (int quality : qualities)
    {
        QByteArray buffer;
        QBuffer out(&buffer);
        if (!out.open(QIODevice::WriteOnly))
        {
            return QString();
        }
        if (!img.save(&out, "JPEG", quality))
        {
            return QString();
        }
        out.close();
        bytes = buffer;
        if (bytes.size() <= 900 * 1024)
        {
            break;
        }
    }

    if (bytes.isEmpty())
    {
        return QString();
    }
    if (outBytes)
    {
        *outBytes = bytes;
    }
    return QStringLiteral("data:image/jpeg;base64,")
           + QString::fromLatin1(bytes.toBase64());
}
