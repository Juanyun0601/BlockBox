/**
 * @file   settings_advanced_forum_probe.cpp
 * @brief  探针：高级设置页已不再提供「方块盒子论坛 / 论坛地址」设置
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 检查项：
 *   1. 「高级设置」里没有名为 forumUrlEdit 的输入框；
 *   2. 没有「方块盒子论坛」卡片标题，也没有「论坛地址」设置行；
 *   3. 同页其它设置卡片（JVM 参数 / 运行日志 / 实验性内容）仍然在，
 *      确认页面本身正常构建，不是空壳导致的「假通过」。
 *
 * 编译方式参考 build-win-release/build_forum_probe.bat，运行前设置：
 *   set QT_QPA_PLATFORM=offscreen
 */

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

#include <cstdio>

#include "pages/SettingsPage.h"

namespace {

int g_failed = 0;

void expect(const char *name, bool ok)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        g_failed += 1;
}

bool hasLabelWithText(const QWidget *root, const QString &text)
{
    const QList<QLabel *> labels = root->findChildren<QLabel *>();
    for (const QLabel *label : labels) {
        if (label->text().trimmed() == text)
            return true;
    }
    return false;
}

bool hasButtonWithText(const QWidget *root, const QString &text)
{
    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (const QPushButton *button : buttons) {
        if (button->text().trimmed() == text)
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    SettingsPage page;
    page.resize(1180, 820);
    page.setCurrentSettingsTab(5); // 5 = 高级设置
    QCoreApplication::processEvents();

    expect("高级设置页构建完成（有「JVM 参数」卡片）",
           hasLabelWithText(&page, QStringLiteral("JVM 参数")));
    expect("高级设置页构建完成（有「运行日志」卡片）",
           hasLabelWithText(&page, QStringLiteral("运行日志")));
    expect("高级设置页构建完成（有「实验性内容」卡片）",
           hasLabelWithText(&page, QStringLiteral("实验性内容")));

    expect("论坛地址输入框（forumUrlEdit）已删除",
           page.findChild<QLineEdit *>(QStringLiteral("forumUrlEdit")) == nullptr);
    expect("「方块盒子论坛」卡片标题已删除",
           !hasLabelWithText(&page, QStringLiteral("方块盒子论坛")));
    expect("「论坛地址」设置行已删除",
           !hasLabelWithText(&page, QStringLiteral("论坛地址")));
    expect("「恢复默认」按钮不再出现在高级设置页",
           !hasButtonWithText(&page, QStringLiteral("恢复默认")));

    std::printf("结果：%s（失败 %d 项）\n", g_failed == 0 ? "全部通过" : "有失败项", g_failed);
    return g_failed == 0 ? 0 : 1;
}
