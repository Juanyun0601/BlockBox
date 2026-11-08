/**
 * @file   settings_advanced_forum_probe.cpp
 * @brief  探针：高级设置页已不再提供「方块盒子论坛 / 论坛地址」设置
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 检查项（全部限定在 settingsAdvancedContent 内）：
 *   1. 卡片列表正好是 JVM 参数 / 运行日志 / 启动前进程检查 / 实验性内容 四张，
 *      没有「方块盒子论坛」卡片；
 *   2. 没有名为 forumUrlEdit 的输入框，也没有「论坛地址」设置行；
 *   3. 顺带把这一页存成 PNG，便于人工确认排版。
 *
 * 编译/运行：build-win-release\build_advanced_forum_probe.bat（内部设置
 * QT_QPA_PLATFORM=offscreen）。
 */

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "components/CollapsibleSectionCard.h"
#include "pages/SettingsPage.h"

namespace {

int g_failed = 0;

void expect(const char *name, bool ok)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        g_failed += 1;
}

QStringList cardTitles(const QWidget *root)
{
    QStringList titles;
    const QList<CollapsibleSectionCard *> cards = root->findChildren<CollapsibleSectionCard *>();
    for (const CollapsibleSectionCard *card : cards)
        titles << card->title().trimmed();
    return titles;
}

bool anyTextContains(const QWidget *root, const QString &needle)
{
    for (const QLabel *label : root->findChildren<QLabel *>()) {
        if (label->text().contains(needle))
            return true;
    }
    for (const QPushButton *button : root->findChildren<QPushButton *>()) {
        if (button->text().contains(needle))
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    SettingsPage page;
    page.resize(1180, 900);
    page.show();
    page.setCurrentSettingsTab(5); // 5 = 高级设置
    QApplication::processEvents();

    QWidget *advanced = page.findChild<QWidget *>(QStringLiteral("settingsAdvancedContent"));
    expect("高级设置内容容器存在", advanced != nullptr);
    if (!advanced) {
        std::printf("结果：有失败项（失败 %d 项）\n", g_failed);
        return 1;
    }

    const QStringList titles = cardTitles(advanced);
    std::printf("卡片顺序：%s\n", qUtf8Printable(titles.join(QStringLiteral(" | "))));

    const QStringList wanted = {QStringLiteral("JVM 参数"), QStringLiteral("运行日志"),
                                QStringLiteral("启动前进程检查"), QStringLiteral("实验性内容")};
    expect("高级设置页卡片与预期一致（四张、顺序正确）", titles == wanted);
    expect("「方块盒子论坛」卡片已删除",
           !titles.contains(QStringLiteral("方块盒子论坛")));
    expect("论坛地址输入框（forumUrlEdit）已删除",
           advanced->findChild<QLineEdit *>(QStringLiteral("forumUrlEdit")) == nullptr);
    expect("「论坛地址」设置行已删除", !anyTextContains(advanced, QStringLiteral("论坛地址")));
    expect("「论坛地址」的说明文字已删除",
           !anyTextContains(advanced, QStringLiteral("左侧导航栏「社区」页会读取这个地址")));

    // 一键求图，方便人工确认这一页的排版：
    // 先按内容自然高度收一下，避免页面被拉成一条细长的空白
    if (advanced->layout()) {
        advanced->layout()->activate();
        const int natural = advanced->layout()->sizeHint().height();
        if (natural > 0)
            advanced->setFixedHeight(natural);
        advanced->layout()->activate();
        QApplication::processEvents();
    }
    const QString shot = QStringLiteral("probe_out/settings_advanced_no_forum.png");
    const bool saved = advanced->grab().save(shot);
    std::printf("[%s] 已保存页面截图：%s\n", saved ? "PASS" : "FAIL", qUtf8Printable(shot));

    std::printf("结果：%s（失败 %d 项）\n", g_failed == 0 ? "全部通过" : "有失败项", g_failed);
    return g_failed == 0 ? 0 : 1;
}
