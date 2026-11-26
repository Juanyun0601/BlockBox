/**
 * @file   settings_mcp_card_probe.cpp
 * @brief  探针：AI 助手设置页的「本地 MCP 服务器」卡片
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 检查项：
 *   1. AI 助手设置页存在「本地 MCP 服务器」卡片；
 *   2. 有启用开关、端口输入（默认 17890）、写权限开关、非空令牌、复制客户端配置按钮；
 *   3. setMcpServerStatus() 能把运行 / 失败状态写到状态文字上；
 *   4. 顺带截图，便于人工确认排版。
 *
 * 编译/运行：tests\build_mcp_settings_probe.bat
 */
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "components/CollapsibleSectionCard.h"
#include "components/CustomCheckBox.h"
#include "pages/SettingsPage.h"

namespace {

int g_failed = 0;

void expect(const char *name, bool ok, const QString &detail = QString())
{
    std::printf("[%s] %s", ok ? "PASS" : "FAIL", name);
    if (!detail.isEmpty())
    {
        std::printf("  -> %s", qUtf8Printable(detail));
    }
    std::printf("\n");
    if (!ok)
    {
        g_failed += 1;
    }
}

QStringList cardTitles(const QWidget *root)
{
    QStringList titles;
    for (const CollapsibleSectionCard *card : root->findChildren<CollapsibleSectionCard *>())
    {
        titles << card->title().trimmed();
    }
    return titles;
}

bool anyLabelContains(const QWidget *root, const QString &needle, QString *found = nullptr)
{
    for (const QLabel *label : root->findChildren<QLabel *>())
    {
        if (label->text().contains(needle))
        {
            if (found)
            {
                *found = label->text();
            }
            return true;
        }
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
    page.setCurrentSettingsTab(8); // 8 = AI 助手
    QApplication::processEvents();

    QWidget *ai = page.findChild<QWidget *>(QStringLiteral("settingsAiAssistantContent"));
    expect("AI 助手内容容器存在", ai != nullptr);
    if (!ai)
    {
        std::printf("结果：有失败项\n");
        return 1;
    }

    const QStringList titles = cardTitles(ai);
    std::printf("卡片：%s\n", qUtf8Printable(titles.join(QStringLiteral(" | "))));
    expect("包含「本地 MCP 服务器」卡片", titles.contains(QStringLiteral("本地 MCP 服务器")));

    QSpinBox *portSpin = nullptr;
    for (QSpinBox *spin : ai->findChildren<QSpinBox *>())
    {
        if (spin->minimum() == 1024 && spin->maximum() == 65535)
        {
            portSpin = spin;
            break;
        }
    }
    expect("端口输入框存在", portSpin != nullptr);
    if (portSpin)
    {
        expect("端口默认 17890", portSpin->value() == 17890,
               QString::number(portSpin->value()));
    }

    QLineEdit *tokenEdit = nullptr;
    for (QLineEdit *edit : ai->findChildren<QLineEdit *>())
    {
        if (edit->isReadOnly() && edit->text().size() >= 16)
        {
            tokenEdit = edit;
            break;
        }
    }
    expect("访问令牌已生成且只读", tokenEdit != nullptr,
           tokenEdit ? tokenEdit->text() : QString());

    int checkBoxCount = ai->findChildren<CustomCheckBox *>().size();
    expect("至少有三个开关（MCP 启用 / 写权限 / 其他）", checkBoxCount >= 3,
           QString::number(checkBoxCount));

    bool hasCopy = false;
    bool hasRegen = false;
    for (const QPushButton *button : ai->findChildren<QPushButton *>())
    {
        if (button->text().contains(QStringLiteral("复制客户端配置")))
        {
            hasCopy = true;
        }
        if (button->text().contains(QStringLiteral("重新生成")))
        {
            hasRegen = true;
        }
    }
    expect("有「复制客户端配置」按钮", hasCopy);
    expect("有「重新生成」令牌按钮", hasRegen);

    QString statusText;
    expect("有 MCP 状态文字", anyLabelContains(ai, QStringLiteral("状态："), &statusText),
           statusText);

    page.setMcpServerStatus(true, 17890, QString());
    QApplication::processEvents();
    QString runningText;
    expect("运行状态文案正确",
           anyLabelContains(ai, QStringLiteral("正在监听"), &runningText), runningText);

    page.setMcpServerStatus(false, 0, QStringLiteral("端口被占用"));
    QApplication::processEvents();
    QString failedText;
    expect("失败状态文案正确",
           anyLabelContains(ai, QStringLiteral("启动失败"), &failedText), failedText);

    if (ai->layout())
    {
        ai->layout()->activate();
        const int natural = ai->layout()->sizeHint().height();
        if (natural > 0)
        {
            ai->setFixedHeight(natural);
        }
        ai->layout()->activate();
        QApplication::processEvents();
    }
    const QString shot = QStringLiteral("probe_out/settings_mcp_card.png");
    const bool saved = ai->grab().save(shot);
    std::printf("[%s] 截图：%s\n", saved ? "PASS" : "FAIL", qUtf8Printable(shot));
    if (!saved)
    {
        g_failed += 1;
    }

    std::printf("结果：%s（失败 %d 项）\n", g_failed == 0 ? "全部通过" : "有失败项", g_failed);
    return g_failed == 0 ? 0 : 1;
}
