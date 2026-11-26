/**
 * @file   assistant_ai_sidebar_probe.cpp
 * @brief  实例助手内「AI 助手」页：展开对话选择侧栏的极限时序渲染探针
 *
 * 用户反馈：实例助手 → AI助手 → 点 ☰「对话选择」，展开后一片空白（未渲染）。
 * 常规时序已经证明能正常显示，这里专挑容易出问题的时序：
 *   A. 切到 AI 页后立刻（淡入动画还在跑）点 ☰
 *   B. 快速连点 ☰ 三次
 *   C. 淡入动画中途点 ☰ 后立刻切到别的页再切回
 * 每个状态抓离屏图 + 真实屏幕像素图。
 */
#include <QAbstractAnimation>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsEffect>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#define private public
#include "components/InstanceAssistantWindow.h"
#include "pages/AiChatPage.h"
#undef private

#include "utils/ThemeManager.h"

namespace
{
QTextStream *g_out = nullptr;

void logLine(const QString &line)
{
    if (g_out)
    {
        *g_out << line << QLatin1Char('\n');
        g_out->flush();
    }
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

QString rectStr(const QWidget *w)
{
    if (!w)
        return QStringLiteral("null");
    const QRect g = w->geometry();
    return QStringLiteral("%1,%2 %3x%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
}

QString effectStr(QWidget *w)
{
    if (!w)
        return QStringLiteral("null");
    QGraphicsEffect *e = w->graphicsEffect();
    if (!e)
        return QStringLiteral("none");
    return QString::fromLatin1(e->metaObject()->className());
}

void shotWidget(QWidget *widget, const QString &name)
{
    if (!widget)
        return;
    QDir().mkpath(QStringLiteral(".tmp_ai_sidebar_probe/out"));
    widget->grab().save(QStringLiteral(".tmp_ai_sidebar_probe/out/%1.png").arg(name));
}

void shotScreen(QWidget *widget, const QString &name)
{
    if (!widget || !widget->screen())
        return;
    QDir().mkpath(QStringLiteral(".tmp_ai_sidebar_probe/out"));
    const QRect g = widget->frameGeometry();
    const QPixmap pix = widget->screen()->grabWindow(0, g.x(), g.y(), g.width(), g.height());
    pix.save(QStringLiteral(".tmp_ai_sidebar_probe/out/%1.png").arg(name));
}

void dumpState(InstanceAssistantWindow *window, const QString &tag)
{
    AiChatPage *page = window->m_aiChatPage;
    QFrame *panel = page->m_leftPanel;
    logLine(QStringLiteral("[%1] pageEffect=%2 panelVisible=%3 panel=%4 panelEffect=%5 "
                           "expanded=%6 animRunning=%7 convBtns=%8")
                .arg(tag)
                .arg(effectStr(page))
                .arg(panel ? panel->isVisible() : false)
                .arg(rectStr(panel))
                .arg(effectStr(panel))
                .arg(page->m_convListExpanded)
                .arg(page->m_convToggleAnim
                         ? (page->m_convToggleAnim->state() == QAbstractAnimation::Running)
                         : false)
                .arg(page->m_convButtons.size()));
}
} // namespace

int main(int argc, char *argv[])
{
    // 对话落盘走测试目录，别动用户的真实对话文件
    QStandardPaths::setTestModeEnabled(true);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    for (const QString &path : {QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                                QStringLiteral("C:/Windows/Fonts/msyhbd.ttc")})
        QFontDatabase::addApplicationFont(path);
    QFont probeFont(QStringLiteral("Microsoft YaHei"));
    probeFont.setPixelSize(13);
    app.setFont(probeFont);

    QDir().mkpath(QStringLiteral(".tmp_ai_sidebar_probe/out"));
    QFile report(QStringLiteral(".tmp_ai_sidebar_probe/report.txt"));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 1;
    QTextStream out(&report);
    out.setEncoding(QStringConverter::Utf8);
    g_out = &out;

    logLine(QStringLiteral("=== 实例助手 AI 页对话侧栏极限时序探针 ==="));

    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    settings.setValue(QStringLiteral("ai/convListExpanded"), false);
    settings.sync();

    auto *window = new InstanceAssistantWindow;
    window->setInstanceContext(QStringLiteral("D:/BlockBox/C-BlockBox/.tmp_ai_sidebar_probe/instance"),
                               QStringLiteral("1.20.4"), QStringLiteral("Forge"));
    window->resize(474, 784);
    window->show();
    window->raise();
    window->activateWindow();
    waitMs(1400);

    AiChatPage *page = window->m_aiChatPage;
    for (int i = 0; i < 5; ++i)
    {
        Conversation conv;
        conv.id = QStringLiteral("probe-%1").arg(i);
        conv.title = QStringLiteral("探针对话 %1").arg(i + 1);
        conv.mode = 0;
        page->addConversation(conv);
    }

    // ---- A. 切到 AI 页后立刻点 ☰（淡入动画还在跑） ----
    logLine(QStringLiteral("\n[A] 切页后 0ms 就点 ☰"));
    window->switchToTab(1);
    page->m_toggleConvBtn->click();
    waitMs(400);
    dumpState(window, QStringLiteral("A+400ms"));
    shotWidget(window, QStringLiteral("A1_widget"));
    shotScreen(window, QStringLiteral("A1_screen"));
    waitMs(1200);
    dumpState(window, QStringLiteral("A+1600ms"));
    shotWidget(window, QStringLiteral("A2_widget"));
    shotScreen(window, QStringLiteral("A2_screen"));

    // ---- B. 切走再切回，然后在淡入中途点 ☰ ----
    logLine(QStringLiteral("\n[B] 切页后 90ms 点 ☰"));
    page->setConvListExpanded(false, false);
    window->switchToTab(0);
    waitMs(700);
    window->switchToTab(1);
    waitMs(90);
    page->m_toggleConvBtn->click();
    waitMs(800);
    dumpState(window, QStringLiteral("B+900ms"));
    shotWidget(window, QStringLiteral("B1_widget"));
    shotScreen(window, QStringLiteral("B1_screen"));

    // ---- C. 快速连点 5 次 ----
    logLine(QStringLiteral("\n[C] 快速连点 ☰"));
    for (int i = 0; i < 5; ++i)
    {
        page->m_toggleConvBtn->click();
        waitMs(30);
    }
    waitMs(1200);
    dumpState(window, QStringLiteral("C+900ms"));
    shotWidget(window, QStringLiteral("C1_widget"));
    shotScreen(window, QStringLiteral("C1_screen"));

    // 确保最后是展开态，等动画结束后再抓一张
    if (!page->m_convListExpanded)
    {
        page->m_toggleConvBtn->click();
        waitMs(800);
    }
    dumpState(window, QStringLiteral("C-final"));
    shotWidget(window, QStringLiteral("C2_widget"));
    shotScreen(window, QStringLiteral("C2_screen"));

    // ---- D. 窗口来回缩放 ----
    logLine(QStringLiteral("\n[D] 展开状态下反复缩放窗口"));
    window->resize(500, 700);
    waitMs(260);
    window->resize(424, 760);
    waitMs(260);
    window->resize(560, 640);
    waitMs(500);
    dumpState(window, QStringLiteral("D-resized"));
    shotWidget(window, QStringLiteral("D1_widget"));
    shotScreen(window, QStringLiteral("D1_screen"));

    // ---- E. 拖动窗口位置 ----
    logLine(QStringLiteral("\n[E] 展开状态下拖动窗口"));
    for (int i = 0; i < 12; ++i)
    {
        window->move(window->x() - 6, window->y() + 4);
        waitMs(30);
    }
    waitMs(700);
    dumpState(window, QStringLiteral("E-moved"));
    shotWidget(window, QStringLiteral("E1_widget"));
    shotScreen(window, QStringLiteral("E1_screen"));

    // ---- F. 收起窗口再展开 ----
    logLine(QStringLiteral("\n[F] 收起再展开窗口"));
    window->setCollapsed(true);
    waitMs(600);
    window->setCollapsed(false);
    waitMs(900);
    dumpState(window, QStringLiteral("F-restored"));
    shotWidget(window, QStringLiteral("F1_widget"));
    shotScreen(window, QStringLiteral("F1_screen"));

    // ---- G. 置顶开关（会重建原生窗口） ----
    logLine(QStringLiteral("\n[G] 展开状态下切一次置顶（重建原生窗口）"));
    window->togglePinned();
    waitMs(900);
    dumpState(window, QStringLiteral("G-pinned"));
    shotWidget(window, QStringLiteral("G1_widget"));
    shotScreen(window, QStringLiteral("G1_screen"));
    window->togglePinned();
    waitMs(900);
    dumpState(window, QStringLiteral("G-unpinned"));
    shotWidget(window, QStringLiteral("G2_widget"));
    shotScreen(window, QStringLiteral("G2_screen"));

    // ---- H. 隐藏窗口再显示 ----
    logLine(QStringLiteral("\n[H] 隐藏窗口再显示"));
    window->hide();
    waitMs(400);
    window->show();
    window->raise();
    waitMs(900);
    dumpState(window, QStringLiteral("H-reshown"));
    shotWidget(window, QStringLiteral("H1_widget"));
    shotScreen(window, QStringLiteral("H1_screen"));

    settings.setValue(QStringLiteral("ai/convListExpanded"), false);
    settings.sync();

    logLine(QStringLiteral("done"));
    window->hide();
    window->deleteLater();
    QApplication::processEvents();
    return 0;
}




