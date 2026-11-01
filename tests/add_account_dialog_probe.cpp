/**
 * @file   add_account_dialog_probe.cpp
 * @brief  「添加角色」弹窗探针：三种登录方式的版式与控件是否齐全
 *
 * 直接构造 AddAccountDialog（不 exec，避免阻塞），逐个页签截图：
 *   probe_out/add_account_offline.png / _microsoft.png / _thirdparty.png
 * 退出码 0 = 全部断言通过。
 */
#include "pages/AddAccountDialog.h"

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

    // 真实启动时主题由 StartupProgressWidget::applyTheme 套用；探针里补上，否则弹窗走系统默认样式
    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    QApplication::processEvents();

    AddAccountDialog dialog;
    dialog.show();
    QApplication::processEvents();

    auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("loginMethodTabs"));
    expect("弹窗里有登录方式页签容器", tabs != nullptr);
    expect("三种登录方式都在", tabs && tabs->count() == 3);
    expect("标题是「添加角色」", dialog.windowTitle() == QStringLiteral("添加角色"));
    expect("离线页有玩家名输入框",
           !dialog.findChildren<QLineEdit *>().isEmpty());
    expect("第三方页有认证服务器下拉框",
           !dialog.findChildren<QComboBox *>().isEmpty());
    expect("有「创建离线账户」按钮",
           [&dialog]() {
               for (auto *b : dialog.findChildren<QPushButton *>())
                   if (b->text().contains(QStringLiteral("创建离线账户")))
                       return true;
               return false;
           }());

    savePng(&dialog, shotDir + QStringLiteral("/add_account_offline.png"));

    for (int i = 1; i < 3; ++i)
    {
        if (tabs)
            tabs->setCurrentIndex(i);
        for (int k = 0; k < 10; ++k)
        {
            QThread::msleep(20);
            QApplication::processEvents();
        }
        savePng(&dialog, shotDir
                             + (i == 1 ? QStringLiteral("/add_account_microsoft.png")
                                       : QStringLiteral("/add_account_thirdparty.png")));
        expect(i == 1 ? "切到微软正版页后弹窗有高度" : "切到第三方页后弹窗有高度",
               dialog.height() > 200);
    }

    fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
            g_failures);
    return g_failures == 0 ? 0 : 1;
}
