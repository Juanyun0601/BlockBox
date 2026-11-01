/**
 * @file   forum_account_page_probe.cpp
 * @brief  「论坛账户界面」作为账户管理页唯一界面的接线探针
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），构建真实 MainWindow：
 *   1. 点顶栏账户入口 → 账户管理页就是论坛账户界面，不再有旧经典账户页；
 *   2. 页脚栏（论坛地址 + 打开论坛 / 论坛设置 / 经典账户管理）已整栏去掉；
 *   3. 角色页右上角有「添加角色」「制作皮肤」两个入口；
 *   4. 点「添加角色」→ 弹出添加账户弹窗；点「制作皮肤」→ 打开皮肤制作页；
 *   5. 进 设置 → 高级设置 → 实验性内容，确认账户 / 社区已不再是实验性开关；
 *   6. 侧边栏「社区」直接点就能进入。
 *
 * 输出：probe_out/forum_in_mainwindow_*.png
 * 退出码 0 = 全部断言通过。
 */
#include "mainwindow.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>

#include "components/CollapsibleSectionCard.h"
#include "components/CustomCheckBox.h"
#include "pages/CommunityForumPage.h"
#include "pages/SettingsPage.h"
#include "pages/SkinEditorPage.h"
#include "utils/PageTransitionAnimator.h"

#include <cstdio>
#include <functional>

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

QPushButton *findButton(QWidget *root, const QString &objectName, const QString &text)
{
    if (!root)
        return nullptr;
    for (auto *button : root->findChildren<QPushButton *>())
    {
        if (!objectName.isEmpty() && button->objectName() == objectName)
            return button;
        if (objectName.isEmpty() && !text.isEmpty() && button->text().contains(text))
            return button;
    }
    return nullptr;
}

void clickButton(QPushButton *button)
{
    if (!button)
        return;
    button->click();
    QApplication::processEvents();
    QApplication::processEvents();
}

/** 等页面切换动画停下来（动画未结束时主窗口会忽略新的导航请求） */
void whenIdle(MainWindow *window, const std::function<void()> &then)
{
    auto *timer = new QTimer(window);
    timer->setInterval(80);
    QObject::connect(timer, &QTimer::timeout, window, [timer, window, then]() {
        auto *animator = window->findChild<PageTransitionAnimator *>();
        if (animator && animator->isAnimating())
            return;
        timer->stop();
        timer->deleteLater();
        then();
    });
    timer->start();
}

} // namespace

int main(int argc, char *argv[])
{
    const QString shotDir = QStringLiteral("probe_out");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    MainWindow window;
    window.resize(1200, 820);
    window.show();
    QApplication::processEvents();

    // 1.2s：点顶栏账户入口，进入账户管理页
    QTimer::singleShot(1200, &app, [&window]() {
        QMetaObject::invokeMethod(&window, "onAccountManageClicked", Qt::DirectConnection);
        QApplication::processEvents();
    });

    // 2.6s：账户管理页就是论坛账户界面，页脚栏已经去掉
    QTimer::singleShot(2600, &app, [&window, shotDir]() {
        fprintf(stderr, "[probe] ==== phase: initial ====\n");
        auto *uiStack = window.findChild<QStackedWidget *>(QStringLiteral("accountUiStack"));
        QWidget *forumPage = window.findChild<QWidget *>(QStringLiteral("fbRoot"));
        expect("账户管理页显示论坛账户界面", forumPage != nullptr);
        expect("当前显示的就是论坛账户界面", uiStack && uiStack->currentWidget() == forumPage);
        expect("容器里只有一页（旧经典账户页已移除）", uiStack && uiStack->count() == 1);
        expect("页脚栏已删除（没有「打开论坛」按钮）",
               findButton(forumPage, QString(), QStringLiteral("打开论坛")) == nullptr);
        expect("页脚栏已删除（没有「论坛设置」按钮）",
               findButton(forumPage, QString(), QStringLiteral("论坛设置")) == nullptr);
        expect("页脚栏已删除（没有「经典账户管理」按钮）",
               findButton(forumPage, QString(), QStringLiteral("经典账户管理")) == nullptr);
        expect("页脚说明文字已删除",
               !hasLabelWithText(forumPage, QStringLiteral("账户来自方块盒子论坛")));
        expect("角色页有「添加角色」入口",
               findButton(forumPage, QString(), QStringLiteral("添加角色")) != nullptr);
        expect("角色页有「制作皮肤」入口",
               findButton(forumPage, QString(), QStringLiteral("制作皮肤")) != nullptr);
        savePng(&window, shotDir + QStringLiteral("/forum_in_mainwindow_default.png"));
    });

    // 3.4s：点角色页「添加角色」→ 弹出添加账户弹窗（弹窗是模态的，用定时器把它关掉）
    QTimer::singleShot(3400, &app, [&window, shotDir]() {
        fprintf(stderr, "[probe] ==== phase: add-dialog ====\n");
        QWidget *forumPage = window.findChild<QWidget *>(QStringLiteral("fbRoot"));
        auto *addBtn = findButton(forumPage, QString(), QStringLiteral("添加角色"));
        bool dialogSeen = false;

        QTimer::singleShot(600, &window, [&dialogSeen, shotDir]() {
            for (QWidget *top : QApplication::topLevelWidgets())
            {
                auto *dialog = qobject_cast<QDialog *>(top);
                if (!dialog || !dialog->isVisible())
                    continue;
                dialogSeen = true;
                savePng(dialog, shotDir + QStringLiteral("/forum_in_mainwindow_add_dialog.png"));
                dialog->reject();
                return;
            }
        });

        clickButton(addBtn);   // 模态弹窗在嵌套事件循环里等上面的定时器关掉
        expect("点「添加角色」弹出添加账户弹窗", dialogSeen);
    });

    // 4.4s：点角色页「制作皮肤」→ 打开皮肤制作页
    QTimer::singleShot(4400, &app, [&window, shotDir]() {
        fprintf(stderr, "[probe] ==== phase: skin-editor ====\n");
        QWidget *forumPage = window.findChild<QWidget *>(QStringLiteral("fbRoot"));
        clickButton(findButton(forumPage, QString(), QStringLiteral("制作皮肤")));

        auto *skinPage = window.findChild<SkinEditorPage *>();
        expect("打开了皮肤制作页", skinPage != nullptr && skinPage->isVisible());
        savePng(&window, shotDir + QStringLiteral("/forum_in_mainwindow_skin_editor.png"));
    });

    // 5.4s：回到账户管理页，准备看设置
    QTimer::singleShot(5400, &app, [&window]() {
        QMetaObject::invokeMethod(&window, "onAccountManageClicked", Qt::DirectConnection);
        QApplication::processEvents();
    });

    // 6.6s：进 设置 → 高级设置，确认实验性内容里已没有账户 / 社区开关
    QTimer::singleShot(6600, &app, [&window, shotDir]() {
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
        expect("没有「论坛账户管理界面」开关",
               !hasLabelWithText(card ? card : advancedPage, QStringLiteral("论坛账户管理界面")));
        expect("没有「社区」开关",
               !hasLabelWithText(card ? card : advancedPage, QStringLiteral("左侧导航栏会多出")));

        const QList<CustomCheckBox *> toggles =
            card ? card->findChildren<CustomCheckBox *>() : QList<CustomCheckBox *>();
        expect("实验性卡片只剩 2 个开关", toggles.size() == 2);

        for (int i = 0; i < 20; ++i)
        {
            QThread::msleep(30);
            QApplication::processEvents();
        }
        if (card)
            savePng(card, shotDir + QStringLiteral("/forum_in_mainwindow_settings_card.png"));
    });

    // 7.6s：侧边栏「社区」直接可用（等上一段动画停下再点，避免导航被忽略）
    QTimer::singleShot(7600, &app, [&window, shotDir]() {
        fprintf(stderr, "[probe] ==== phase: community ====\n");
        whenIdle(&window, [&window, shotDir]() {
            QMetaObject::invokeMethod(&window, "onParentNavClicked", Qt::DirectConnection,
                                      Q_ARG(int, 6));
            QApplication::processEvents();
            whenIdle(&window, [&window, shotDir]() {
                auto *communityPage = window.findChild<CommunityForumPage *>();
                expect("点侧边栏「社区」能进入社区页",
                       communityPage != nullptr && communityPage->isVisible());
                savePng(&window, shotDir + QStringLiteral("/forum_in_mainwindow_community.png"));
            });
        });
    });

    // 10s：收尾
    QTimer::singleShot(11000, &app, [&app]() {
        fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
                g_failures);
        app.exit(g_failures == 0 ? 0 : 1);
    });

    return app.exec();
}
