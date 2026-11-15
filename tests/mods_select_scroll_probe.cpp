/**
 * @file   mods_select_scroll_probe.cpp
 * @brief  实例管理「模组」列表选中卡片后滚动的渲染探针
 *
 * 复现用户反馈：在本地资源列表页选中资源卡片后滚动，选中效果渲染异常。
 * 流程：注入假模组列表 → 真实点击选中一张卡片（走 grabMouse/release 全路径）
 * → 趁高亮动画未结束连续滚动 → 系统级截屏（不是 widget->grab，
 * 后者会整棵重绘、看不到 backing store 上的滚动残影）→ 存 PNG 人工核对。
 *
 * 输出：.tmp_sel_scroll/probe_out/*.png
 * 退出码 0 = 探针跑通（渲染是否异常由人看图判断）。
 */
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QPixmap>
#include <QRect>
#include <QScreen>
#include <QScrollBar>
#include <QScrollArea>
#include <QTimer>

#include <cstdio>

#include "utils/ThemeManager.h"

#define private public
#include "pages/InstanceModsPage.h"
#undef private

#include "components/CardSelectionOverlay.h"

namespace
{
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-52s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void pump(int rounds = 6)
{
    for (int i = 0; i < rounds; ++i)
    {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void waitMs(int ms)
{
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(ms))
        QApplication::processEvents();
}

/** 按窗口句柄截屏：读的是窗口自己的重定向表面（= backing store 刷出来的
 * 内容），被别的窗口挡住也能截到，滚动残影照收 */
QPixmap shotWindow(QWidget *window, const QString &path)
{
    QPixmap shot = window->screen()->grabWindow(window->winId());
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(),
            shot.height(), ok ? "ok" : "FAILED");
    return shot;
}

/** 模拟真实点击卡片：press（eventFilter 会 grabMouse）→ release（切换选中） */
void clickCard(QWidget *card)
{
    const QPoint center = card->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, center, card->mapToGlobal(center),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(card, &press);
    pump(2);
    QMouseEvent release(QEvent::MouseButtonRelease, center, card->mapToGlobal(center),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(card, &release);
    pump(2);
}

void scrollBy(QScrollArea *area, int delta)
{
    QScrollBar *bar = area->verticalScrollBar();
    bar->setValue(bar->value() + delta);
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

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    QDir().mkpath(QStringLiteral(".tmp_sel_scroll/probe_out"));
    const QString out = QStringLiteral(".tmp_sel_scroll/probe_out");

    InstanceModsPage *page = new InstanceModsPage;
    for (QTimer *timer : page->findChildren<QTimer *>())
        timer->stop();

    // 注入假模组列表，走真实扫描完成管线（建索引 → 分批建卡 → 排布 → 过滤）
    LocalModList fake;
    for (int i = 0; i < 60; ++i)
    {
        ModInfo m;
        m.id = QStringLiteral("probe_%1").arg(i);
        m.name = QStringLiteral("Probe Mod %1").arg(i);
        m.fileName = QStringLiteral("probemod%1.jar").arg(i);
        m.filePath = QStringLiteral("C:/tmp/probemods/probemod%1.jar").arg(i);
        m.fileSize = 1024 * (100 + i);
        m.enabled = (i % 3 != 0);
        m.loaderType = QStringLiteral("Fabric");
        m.latestVersion = QStringLiteral("1.0.%1").arg(i);
        m.description = QStringLiteral("probe item");
        fake.mods.append(m);
        if (m.enabled)
            ++fake.enabledCount;
        else
            ++fake.disabledCount;
    }
    fake.totalCount = fake.mods.size();
    page->onScanCompleted(fake);

    auto *host = new QWidget;
    host->setWindowTitle(QStringLiteral("select-scroll-probe"));
    host->resize(900, 720);
    page->setParent(host);
    page->setGeometry(0, 0, 900, 720);
    host->show();
    host->raise();
    host->activateWindow();

    // 等分批建卡全部完成
    for (int i = 0; i < 200 && page->m_cardWidgets.size() < fake.mods.size(); ++i)
    {
        pump(4);
        if (i % 20 == 0)
            waitMs(5);
    }
    expect("60 张卡片全部创建", page->m_cardWidgets.size() == fake.mods.size());
    waitMs(120);
    pump(8);

    // 卡片应已被瀑布流排布出最终尺寸，选中遮罩几何应与卡片一致
    // 选第 3 张（首屏可见），这样滚动前后都能在截图里看到高亮
    QWidget *card = page->m_cardWidgets.value(2);
    expect("目标卡片存在", card != nullptr);
    if (!card)
        return 1;
    const int ovIdx = page->m_cardWidgets.indexOf(card);
    QWidget *overlay = page->m_selectionOverlays.value(ovIdx);
    expect("选中遮罩存在", overlay != nullptr);
    if (overlay)
    {
        fprintf(stderr, "[probe] card rect %dx%d  overlay rect %dx%d\n",
                card->rect().width(), card->rect().height(),
                overlay->rect().width(), overlay->rect().height());
        expect("遮罩几何与卡片一致（滚动前）",
               overlay->geometry().size() == card->rect().size());
    }

    // 真实点击选中（触发 180ms 高亮动画）
    clickCard(card);
    expect("点击后进入选中集合", page->m_selectedModIndices.contains(ovIdx));
    waitMs(40); // 让动画跑起来，此时开始滚动——对齐用户「选完马上滚」的操作

    shotWindow(host, out + QStringLiteral("/01_selected_before_scroll.png"));

    // 小步滚动：先滚 130px（选中卡片仍大部分可见），再滚到远处
    scrollBy(page->m_scrollArea, 130);
    pump(3);
    waitMs(25);
    shotWindow(host, out + QStringLiteral("/02_scroll_one_step.png"));

    // 连续滚动若干步（每步之间给一帧时间，模拟滚轮连续滚动）
    for (int step = 0; step < 5; ++step)
    {
        scrollBy(page->m_scrollArea, 130);
        pump(3);
        waitMs(25);
    }
    shotWindow(host, out + QStringLiteral("/03_during_scroll.png"));
    waitMs(300); // 动画肯定结束了
    pump(8);
    shotWindow(host, out + QStringLiteral("/04_after_scroll.png"));

    if (overlay)
        expect("遮罩几何与卡片一致（滚动后）",
               overlay->geometry().size() == card->rect().size());

    // 滚回去，看旧位置有没有留下高亮残影
    for (int step = 0; step < 6; ++step)
    {
        scrollBy(page->m_scrollArea, -130);
        pump(3);
        waitMs(25);
    }
    pump(8);
    shotWindow(host, out + QStringLiteral("/05_scrolled_back.png"));

    // 诊断：遮罩状态是否还在？（isVisible/opacity 正常 → 是重绘系统丢帧，
    // 不正常 → 是状态被滚动破坏）
    if (overlay)
    {
        auto *sel = qobject_cast<CardSelectionOverlay *>(overlay);
        fprintf(stderr, "[probe] after scroll: overlay visible=%d opacity=%.2f cardVisible=%d\n",
                overlay->isVisible() ? 1 : 0,
                sel ? sel->highlightOpacity() : -1.0,
                card->isVisible() ? 1 : 0);
    }

    // 强制整卡重绘一次，看高亮能不能恢复（能 → 纯粹是滚动后没重绘）
    card->update();
    pump(8);
    waitMs(60);
    shotWindow(host, out + QStringLiteral("/06_after_forced_update.png"));

    // widget->grab() 整棵重渲染，绕过 backing store —— 与屏幕截图对比可以
    // 区分「backing store 残影」和「真被画没了」
    const QPixmap fresh = card->grab();
    fresh.save(out + QStringLiteral("/07_fresh_render_card.png"));
    fprintf(stderr, "[probe] fresh card render %dx%d saved\n",
            fresh.width(), fresh.height());

    // 滚动后取消选中：屏幕上的高亮应该真正淡出（不只是状态里没了）
    clickCard(card);
    expect("再次点击后移出选中集合", !page->m_selectedModIndices.contains(ovIdx));
    waitMs(260); // 等淡出动画走完
    pump(8);
    shotWindow(host, out + QStringLiteral("/08_deselected_after_scroll.png"));

    // 瀑布流视图同样验一遍：选中 → 滚出去再滚回来 → 高亮保持
    page->m_viewMode = ContentViewSwitch::Masonry;
    page->rebuildCards();
    for (int i = 0; i < 20 && page->m_selectionOverlays.size() >= fake.mods.size(); ++i)
        pump(4);
    pump(8);
    QWidget *mCard = page->m_cardWidgets.value(2);
    expect("瀑布流目标卡片存在", mCard != nullptr);
    if (!mCard)
        return 1;
    const int mOvIdx = page->m_cardWidgets.indexOf(mCard);
    auto *mOverlay = qobject_cast<CardSelectionOverlay *>(
        page->m_selectionOverlays.value(mOvIdx));
    expect("瀑布流选中遮罩存在", mOverlay != nullptr);
    clickCard(mCard);
    expect("瀑布流点击后进入选中集合", page->m_selectedModIndices.contains(mOvIdx));
    waitMs(240); // 等淡入完成
    shotWindow(host, out + QStringLiteral("/09_masonry_selected.png"));
    for (int step = 0; step < 5; ++step)
    {
        scrollBy(page->m_scrollArea, 150);
        pump(3);
        waitMs(25);
    }
    for (int step = 0; step < 5; ++step)
    {
        scrollBy(page->m_scrollArea, -150);
        pump(3);
        waitMs(25);
    }
    pump(8);
    expect("瀑布流滚动后高亮状态保持", mOverlay->isVisible()
           && mOverlay->highlightOpacity() > 0.99);
    shotWindow(host, out + QStringLiteral("/10_masonry_after_scroll.png"));

    page->deleteLater();
    pump(4);

    fprintf(stderr, "[probe] done, failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
