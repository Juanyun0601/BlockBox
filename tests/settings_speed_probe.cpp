/**
 * @file   settings_speed_probe.cpp
 * @brief  旋转背景「旋转速度」设置回归探针
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），构建真实 MainWindow 与设置页：
 *  1. 打开「设置 → 界面设置」，确认旋转模式下出现「旋转速度」滑条；
 *  2. 校验滑条范围 0~400、当前值与 BackgroundManager 一致；
 *  3. 拖动滑条后 BackgroundManager 与 QSettings 同步更新，数值标签同步刷新；
 *  4. 抓取界面设置页整页图像，便于人工确认排版。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QWidget>

#include "pages/SettingsPage.h"
#include "utils/BackgroundManager.h"

#include <cstdio>

namespace {

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int failures = 0;

void check(bool ok, const QString &what, const QString &detail = QString())
{
    if (!ok)
        ++failures;
    QString line = QStringLiteral("[%1] %2").arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), what);
    if (!detail.isEmpty())
        line += QStringLiteral(" | ") + detail;
    QTextStream(stdout) << line << "\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString outDir = QStringLiteral("D:/BlockBox/build_shots/speed_probe");
    QDir().mkpath(outDir);

    // 确保处于旋转背景模式，旋转速度区才会显示
    const QString savedMode = [] {
        QSettings s(QStringLiteral("BlockBox"), QStringLiteral("Settings"));
        return s.value(QStringLiteral("background/mode")).toString();
    }();
    const int savedSpeed = BackgroundManager::instance()->rotatingSpeed();
    {
        QSettings s(QStringLiteral("BlockBox"), QStringLiteral("Settings"));
        s.setValue(QStringLiteral("background/mode"), QStringLiteral("4"));
        s.sync();
    }
    BackgroundManager::instance()->loadFromSettings();
    BackgroundManager::instance()->setMode(BackgroundManager::Rotating);

    MainWindow w;
    w.resize(1200, 800);
    w.show();
    pump(2600);

    // 走真实导航路径：侧栏第 2 项（设置）→ 设置页左侧第 1 项（界面设置）
    QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Q_ARG(int, 2));
    pump(1400);
    QMetaObject::invokeMethod(&w, "onSettingsNavItemClicked", Q_ARG(int, 1));
    pump(1600);
    pump(1600);

    SettingsPage *page = w.findChild<SettingsPage *>();
    if (!page) {
        qCritical() << "PROBE-FAIL: SettingsPage 未找到";
        return 1;
    }
    page->setCurrentSettingsTab(1);  // 界面设置
    pump(1500);

    QWidget *iface = page->findChild<QWidget *>(QStringLiteral("settingsInterfaceContent"));
    check(iface != nullptr, QStringLiteral("找到界面设置页内容"));

    QSlider *slider = iface ? iface->findChild<QSlider *>(QStringLiteral("bgSpeedSlider")) : nullptr;
    check(slider != nullptr, QStringLiteral("旋转模式显示「旋转速度」滑条"));
    if (!slider) {
        QTextStream(stdout) << QStringLiteral("%1 failures\n").arg(failures);
        return 1;
    }

    check(slider->minimum() == 0 && slider->maximum() == 400,
          QStringLiteral("滑条范围为 0~400"), QStringLiteral("min=%1 max=%2")
              .arg(slider->minimum())
              .arg(slider->maximum()));
    check(slider->value() == savedSpeed,
          QStringLiteral("滑条初值与已保存转速一致"),
          QStringLiteral("value=%1 saved=%2").arg(slider->value()).arg(savedSpeed));

    // 数值标签：要么「静止」，要么「N%」
    QLabel *valueLabel = nullptr;
    const QRegularExpression valueExpr(QStringLiteral("^(静止|\\d+%)$"));
    const QList<QLabel *> labels = iface->findChildren<QLabel *>();
    for (QLabel *lb : labels) {
        if (!lb->isVisible())
            continue;
        if (valueExpr.match(lb->text().trimmed()).hasMatch()) {
            valueLabel = lb;
            break;
        }
    }
    check(valueLabel != nullptr, QStringLiteral("存在转速数值标签"),
          valueLabel ? valueLabel->text() : QStringLiteral("未找到"));

    // 拖动到 250% 应同步到 BackgroundManager 与 QSettings
    slider->setValue(250);
    pump(400);
    check(BackgroundManager::instance()->rotatingSpeed() == 250,
          QStringLiteral("滑条改动同步到 BackgroundManager"),
          QStringLiteral("current=%1").arg(BackgroundManager::instance()->rotatingSpeed()));
    {
        QSettings s(QStringLiteral("BlockBox"), QStringLiteral("Settings"));
        const int stored = s.value(QStringLiteral("background/rotatingSpeed"), -1).toInt();
        check(stored == 250, QStringLiteral("转速写入设置"),
              QStringLiteral("stored=%1").arg(stored));
    }
    check(valueLabel && valueLabel->text().trimmed() == QStringLiteral("250%"),
          QStringLiteral("数值标签随滑条刷新"),
          valueLabel ? valueLabel->text() : QStringLiteral("null"));

    // 拖到最左侧应显示「静止」
    slider->setValue(0);
    pump(400);
    check(valueLabel && valueLabel->text().trimmed() == QStringLiteral("静止"),
          QStringLiteral("0% 显示为「静止」"),
          valueLabel ? valueLabel->text() : QStringLiteral("null"));

    // 抓图：整页 + 背景设置卡片区域
    if (iface) {
        const QPixmap shot = iface->grab();
        shot.save(QStringLiteral("%1/interface_page.png").arg(outDir));
        // 以滑条为中心，向上取一屏，覆盖「全景场景 / 旋转速度」两行
        if (slider) {
            const QPoint pos = slider->mapTo(iface, QPoint(0, 0));
            const int top = qMax(0, pos.y() - 260);
            const int h = qMin(iface->height() - top, 520);
            const QPixmap crop = shot.copy(0, top, shot.width(), h);
            crop.save(QStringLiteral("%1/rotating_section.png").arg(outDir));
        }
    }

    // 还原用户设置
    slider->setValue(savedSpeed);
    pump(300);
    {
        QSettings s(QStringLiteral("BlockBox"), QStringLiteral("Settings"));
        if (savedMode.isEmpty())
            s.remove(QStringLiteral("background/mode"));
        else
            s.setValue(QStringLiteral("background/mode"), savedMode);
        s.sync();
    }

    QTextStream(stdout) << QStringLiteral("%1 failures\n").arg(failures);
    QTextStream(stdout).flush();
    std::fflush(stdout);
    // 探针退出时跳过 MainWindow 析构（内部后台任务在无事件循环时会崩），直接结束进程
    std::_Exit(failures == 0 ? 0 : 1);
}
