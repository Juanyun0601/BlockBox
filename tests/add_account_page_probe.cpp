/**
 * @file   add_account_page_probe.cpp
 * @brief  「添加角色」页探针：三种登录方式的版式与控件是否齐全
 *
 * 直接构造 AddAccountPage 并当作独立窗口显示，逐个页签截图：
 *   probe_out/add_account_offline.png / _microsoft.png / _thirdparty.png
 * 退出码 0 = 全部断言通过。
 */
#include "pages/AddAccountPage.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QThread>
#include <QTimer>

#include "utils/ThemeManager.h"

#include <cstdio>

namespace
{

int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-46s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(), shot.height(),
            ok ? "ok" : "FAILED");
}

} // namespace

int main(int argc, char *argv[])
{
    const QString shotDir = QStringLiteral("probe_out");
    QDir().mkpath(shotDir);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    // 真实启动时主题由 StartupProgressWidget::applyTheme 套用；探针里补上，否则页面走系统默认样式
    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    QApplication::processEvents();

    AddAccountPage page;
    page.resize(900, 700);
    page.show();
    QApplication::processEvents();

    auto *tabs = page.findChild<QTabWidget *>(QStringLiteral("loginMethodTabs"));
    expect("页面里有登录方式页签容器", tabs != nullptr);
    expect("三种登录方式都在", tabs && tabs->count() == 3);

    auto *title = page.findChild<QLabel *>(QStringLiteral("loginTitle"));
    expect("标题是「添加角色」", title && title->text() == QStringLiteral("添加角色"));
    expect("离线页有玩家名输入框", !page.findChildren<QLineEdit *>().isEmpty());
    expect("第三方页有认证服务器下拉框", !page.findChildren<QComboBox *>().isEmpty());
    expect("有「创建离线账户」按钮",
           [&page]() {
               for (auto *b : page.findChildren<QPushButton *>())
                   if (b->text().contains(QStringLiteral("创建离线账户")))
                       return true;
               return false;
           }());
    expect("卡片比页面窄（页内居中的单列卡片）",
           [&page]() {
               QWidget *column = page.findChild<QWidget *>(QStringLiteral("loginColumn"));
               return column && column->width() < page.width();
           }());

    savePng(&page, shotDir + QStringLiteral("/add_account_offline.png"));

    for (int i = 1; i < 3; ++i)
    {
        if (tabs)
            tabs->setCurrentIndex(i);
        for (int k = 0; k < 10; ++k)
        {
            QThread::msleep(20);
            QApplication::processEvents();
        }
        savePng(&page, shotDir
                             + (i == 1 ? QStringLiteral("/add_account_microsoft.png")
                                       : QStringLiteral("/add_account_thirdparty.png")));
        expect(i == 1 ? "切到微软正版页后卡片有高度" : "切到第三方页后卡片有高度",
               [&page]() {
                   QWidget *column = page.findChild<QWidget *>(QStringLiteral("loginColumn"));
                   return column && column->height() > 200;
               }());
    }

    // 窄屏（手机竖屏）：卡片跟着缩到可用宽度，不应把内容横向裁掉
    if (tabs)
        tabs->setCurrentIndex(0);
    page.resize(380, 620);
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    savePng(&page, shotDir + QStringLiteral("/add_account_narrow.png"));
    expect("窄屏下卡片缩到页面宽度以内",
           [&page]() {
               QWidget *column = page.findChild<QWidget *>(QStringLiteral("loginColumn"));
               return column && column->width() <= page.width();
           }());

    fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
            g_failures);
    return g_failures == 0 ? 0 : 1;
}
