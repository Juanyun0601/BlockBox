/**
 * @file   forum_account_page_probe.cpp
 * @brief  实验性「论坛账户管理界面」在主窗口里的接线探针
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），构建真实 MainWindow：
 *   1. 打开实验性开关 → 点顶栏账户入口 → 账户管理页应该是新的论坛界面；
 *   2. 关掉开关 → 页面立刻换回经典账户管理页；
 *   3. 再打开 → 又回到论坛界面；
 *   4. 进 设置 → 高级设置 → 实验性内容，确认「论坛账户管理界面」开关存在并截图。
 *
 * 输出：probe_out/forum_in_mainwindow_*.png
 * 退出码 0 = 全部断言通过。
 */
#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPixmap>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>

#include "components/CollapsibleSectionCard.h"
#include "components/CustomCheckBox.h"
#include "pages/SettingsPage.h"
#include "utils/SettingsManager.h"

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

bool savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return false;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(), shot.height(),
            ok ? "ok" : "FAILED");
    return ok;
}

CollapsibleSectionCard *findExperimentalCard(QWidget *page)
{
    if (!page)
        return nullptr;
    CollapsibleSectionCard *found = nullptr;
    for (auto *card : page->findChildren<CollapsibleSectionCard *>())
    {
        if (card->title().contains(QStringLiteral("实验性")))
            found = card;
    }
    return found;
}

bool hasLabelWithText(QWidget *root, const QString &text)
{
    if (!root)
        return false;
    for (auto *label : root->findChildren<QLabel *>())
    {
        if (label->text().contains(text))
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    const QString shotDir = QStringLiteral("probe_out");

    // 备份并预置实验性开关
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    const QVariant oldFlag = settings.value(QStringLiteral("experimental/forumAccount"));
    settings.setValue(QStringLiteral("experimental/forumAccount"), true);
    settings.sync();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    SettingsManager *manager = SettingsManager::instance();
    manager->setExperimentalForumAccountEnabled(true);

    MainWindow window;
    window.resize(1200, 820);
    window.show();
    QApplication::processEvents();

    auto restore = [&settings, oldFlag]() {
        if (oldFlag.isValid())
            settings.setValue(QStringLiteral("experimental/forumAccount"), oldFlag);
        else
            settings.remove(QStringLiteral("experimental/forumAccount"));
        settings.sync();
    };

    // 1.2s：点顶栏账户入口，进入账户管理页
    QTimer::singleShot(1200, &app, [&window]() {
        QMetaObject::invokeMethod(&window, "onAccountManageClicked", Qt::DirectConnection);
        QApplication::processEvents();
    });

    // 2.6s：确认是新界面
    QTimer::singleShot(2600, &app, [&window, shotDir]() {
        fprintf(stderr, "[probe] ==== phase: initial ====\n");
        auto *uiStack = window.findChild<QStackedWidget *>(QStringLiteral("accountUiStack"));
        QWidget *forumPage = window.findChild<QWidget *>(QStringLiteral("fbRoot"));
        QWidget *classicSidebar = window.findChild<QWidget *>(QStringLiteral("accountLeftSidebar"));
        expect("账户管理页是新的论坛界面", forumPage != nullptr);
        expect("当前显示的就是论坛界面", uiStack && uiStack->currentWidget() == forumPage);
        expect("经典账户列表此时未创建", classicSidebar == nullptr);
        savePng(&window, shotDir + QStringLiteral("/forum_in_mainwindow_experimental.png"));
    });

    // 3.4s：关掉开关 → 应切回经典界面
    QTimer::singleShot(3400, &app, [&window, shotDir]() {
        fprintf(stderr, "[probe] ==== phase: switch-off ====\n");
        SettingsManager::instance()->setExperimentalForumAccountEnabled(false);
        QApplication::processEvents();
        QApplication::processEvents();

        auto *uiStack = window.findChild<QStackedWidget *>(QStringLiteral("accountUiStack"));
        QWidget *current = uiStack ? uiStack->currentWidget() : nullptr;
        QWidget *forumPage = window.findChild<QWidget *>(QStringLiteral("fbRoot"));
        fprintf(stderr, "[probe]   uiStack=%p current=%p '%s' forum=%p count=%d index=%d\n",
                static_cast<void *>(uiStack), static_cast<void *>(current),
                current ? qPrintable(current->objectName()) : "-", static_cast<void *>(forumPage),
                uiStack ? uiStack->count() : -1, uiStack ? uiStack->currentIndex() : -1);
        expect("关闭开关后切到经典账户页", current != nullptr && current != forumPage);
        expect("经典账户列表已创建", window.findChild<QWidget *>(QStringLiteral("accountLeftSidebar")) != nullptr);
        expect("经典页仍在容器中（未销毁）", uiStack && uiStack->indexOf(forumPage) >= 0);
        savePng(&window, shotDir + QStringLiteral("/forum_in_mainwindow_classic.png"));
    });

    // 4.4s：再打开 → 又切回论坛界面
    QTimer::singleShot(4400, &app, [&window]() {
        fprintf(stderr, "[probe] ==== phase: switch-on ====\n");
        SettingsManager::instance()->setExperimentalForumAccountEnabled(true);
        QApplication::processEvents();
        QApplication::processEvents();

        auto *uiStack = window.findChild<QStackedWidget *>(QStringLiteral("accountUiStack"));
        QWidget *forumPage = window.findChild<QWidget *>(QStringLiteral("fbRoot"));
        expect("重新打开后回到论坛界面", forumPage != nullptr && uiStack
                                              && uiStack->currentWidget() == forumPage);
    });

    // 5.2s：进 设置 → 高级设置，确认新增的开关
    QTimer::singleShot(5200, &app, [&window, shotDir]() {
        fprintf(stderr, "[probe] ==== phase: settings ====\n");
        QMetaObject::invokeMethod(&window, "onParentNavClicked", Qt::DirectConnection,
                                  Q_ARG(int, 2));
        QApplication::processEvents();

        auto *settingsPage = window.findChild<SettingsPage *>();
        auto *stack = settingsPage
                          ? settingsPage->findChild<QStackedWidget *>(QStringLiteral("settingsContentStack"))
                          : nullptr;
        if (!stack)
        {
            expect("找到设置页内容栈", false);
            return;
        }
        stack->setCurrentIndex(5);   // 高级设置
        QApplication::processEvents();

        QWidget *advancedPage = stack->currentWidget();
        CollapsibleSectionCard *card = findExperimentalCard(advancedPage);
        expect("找到「实验性内容」卡片", card != nullptr);
        expect("卡片里有「论坛账户管理界面」开关项",
               hasLabelWithText(card ? card : advancedPage, QStringLiteral("论坛账户管理界面")));

        // 开关状态应与实验性设置一致（此时为开启）
        const QList<CustomCheckBox *> toggles =
            card ? card->findChildren<CustomCheckBox *>() : QList<CustomCheckBox *>();
        expect("实验性卡片有 4 个开关", toggles.size() >= 3);
        if (toggles.size() >= 3)
            expect("「论坛账户管理界面」开关处于开启状态", toggles.at(2)->isChecked());

        // 让开关滑块的动画跑完再截图
        for (int i = 0; i < 20; ++i)
        {
            QThread::msleep(30);
            QApplication::processEvents();
        }
        if (card)
            savePng(card, shotDir + QStringLiteral("/forum_in_mainwindow_settings_card.png"));
    });

    // 6.6s：收尾
    QTimer::singleShot(6600, &app, [&app, &restore]() {
        restore();
        fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
                g_failures);
        app.exit(g_failures == 0 ? 0 : 1);
    });

    const int rc = app.exec();
    restore();
    return rc;
}
