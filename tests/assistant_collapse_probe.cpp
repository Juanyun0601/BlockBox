/**
 * @file   assistant_collapse_probe.cpp
 * @brief  实例助手悬浮窗「收起成小条」时的实时数据探针
 *
 * 复现用户反馈：只有在实例助手的「首页」标签页收起时才显示帧率 / CPU / GPU / 内存，
 * 切到其它标签页再收起就没有数据。本探针在每个标签页上都收起一次，打印：
 *   - 标题栏数据容器与四个标签的可见性
 *   - 四个标签的实际文字
 *   - 窗口缓存下来的数值（m_live*）
 * 同时截图方便肉眼确认。
 */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontDatabase>
#include <QLabel>
#include <QThread>
#include <QWidget>

#include <cstdio>

#include "utils/ThemeManager.h"

#define private public
#include "components/InstanceAssistantWindow.h"
#undef private

namespace
{
void pump()
{
    for (int i = 0; i < 8; ++i)
        QApplication::processEvents();
}

void waitMs(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

const char *textOf(QLabel *label)
{
    static QByteArray buffer;
    buffer = label ? label->text().toUtf8() : QByteArray("(null)");
    return buffer.constData();
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    for (const QString &path : {QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                                QStringLiteral("C:/Windows/Fonts/msyhbd.ttc")})
        QFontDatabase::addApplicationFont(path);
    QFont probeFont(QStringLiteral("Microsoft YaHei"));
    probeFont.setPixelSize(13);
    app.setFont(probeFont);

    QDir().mkpath(QStringLiteral(".tmp_mp_probe/probe_out"));

    auto *window = new InstanceAssistantWindow;
    window->resize(450, 760);
    window->show();
    pump();
    // 等硬件监控采一次（2 秒间隔）
    waitMs(2600);
    pump();

    fprintf(stderr, "[probe] cached before loop: fps=%d cpu=%d(%d) gpu=%d(%d) mem=%d(%d)\n",
            window->m_liveFps, window->m_liveCpuPercent, window->m_liveCpuAvailable,
            window->m_liveGpuPercent, window->m_liveGpuAvailable,
            window->m_liveMemoryPercent, window->m_liveMemoryAvailable);

    const int tabs[] = {0, 1, 2, 3, 4, 5};
    for (int tab : tabs)
    {
        window->switchToTab(tab);
        waitMs(400);
        pump();

        window->setCollapsed(true);
        // 收起后至少过一次 1 秒 tick
        waitMs(1400);
        pump();

        fprintf(stderr,
                "[probe] tab=%d collapsed=%d hostVisible=%d hostVisibleTo=%d "
                "labels(v)=%d,%d,%d,%d texts=%s | %s | %s | %s\n",
                tab, static_cast<int>(window->m_collapsed),
                static_cast<int>(window->m_statsHost->isVisible()),
                static_cast<int>(window->m_statsHost->isVisibleTo(window)),
                static_cast<int>(window->m_statFps->isVisible()),
                static_cast<int>(window->m_statCpu->isVisible()),
                static_cast<int>(window->m_statGpu->isVisible()),
                static_cast<int>(window->m_statMem->isVisible()),
                textOf(window->m_statFps), textOf(window->m_statCpu),
                textOf(window->m_statGpu), textOf(window->m_statMem));

        window->grab().save(QStringLiteral(".tmp_mp_probe/probe_out/collapse_tab%1.png").arg(tab));

        // 同一标签页下，收起态停留久一点看数据是否会继续刷新
        waitMs(1300);
        pump();
        fprintf(stderr, "[probe] tab=%d after+1.3s: %s | %s | %s | %s\n", tab,
                textOf(window->m_statFps), textOf(window->m_statCpu),
                textOf(window->m_statGpu), textOf(window->m_statMem));

        window->setCollapsed(false);
        waitMs(350);
        pump();
    }

    return 0;
}
