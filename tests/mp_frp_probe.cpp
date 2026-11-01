/**
 * @file   mp_frp_probe.cpp
 * @brief  实例助手联机页 frp 服务商核心（樱花 / OpenFrp）视觉/状态探针
 *
 * 创建 MultiplayerPage（离屏渲染）：
 *   1. 校验核心下拉框为 4 项（陶瓦 / 红石 / 樱花 / OpenFrp）；
 *   2. 切到樱花联机 → 页面栈应停在 frp 首页（index 4），说明文字为樱花内容，
 *      密钥/隧道 ID 输入框与「打开面板 / 手动选择 frpc / 启动隧道」按钮就位，
 *      整页截图 mp_frp_sakura.png；
 *   3. frpc 未安装时点「启动隧道」→ 应弹提示框（校验按钮路径不崩溃）；
 *   4. 切到 OpenFrp 联机 → 说明文字切换为 OpenFrp 内容，截图 mp_frp_openfrp.png；
 *   5. 切回陶瓦再切回樱花，页面栈状态正确、无崩溃。
 *
 * 构建：复用 debug 构建的目标文件链接（去掉 main.o，加入本文件的 .o，
 * 库参数与主程序链接行一致）。
 * 运行目录：debug 构建目录（截图输出到当前目录）。退出码 0=PASS，1=FAIL。
 */
#include "pages/MultiplayerPage.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>

#include <cstdio>

namespace {

QComboBox *findCoreCombo(const MultiplayerPage &page)
{
    return page.findChild<QComboBox *>(QStringLiteral("multiplayerCoreCombo"));
}

QStackedWidget *findStack(const MultiplayerPage &page)
{
    return page.findChild<QStackedWidget *>(QStringLiteral("multiplayerStack"));
}

QList<QDialog *> topDialogs()
{
    QList<QDialog *> result;
    for (QWidget *w : QApplication::topLevelWidgets())
    {
        if (QDialog *dlg = qobject_cast<QDialog *>(w))
            result.append(dlg);
    }
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    MultiplayerPage page;
    page.resize(430, 720);
    page.show();
    QApplication::processEvents();

    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", what);
        if (!ok)
            ++failures;
    };

    // 1) 核心下拉框 4 项
    QComboBox *combo = findCoreCombo(page);
    check(combo != nullptr, "找到核心下拉框");
    check(combo && combo->count() == 4, "核心下拉框为 4 项（陶瓦/红石/樱花/OpenFrp）");
    if (!combo)
        return 1;

    QStackedWidget *stack = findStack(page);

    // 2) 切到樱花联机 → frp 首页（index 4）
    combo->setCurrentIndex(2);
    QApplication::processEvents();
    check(stack && stack->currentIndex() == 4, "切到樱花联机后停在 frp 首页（栈 index 4）");

    QWidget *frpHome = stack ? stack->currentWidget() : nullptr;
    QLabel *desc = frpHome ? frpHome->findChild<QLabel *>(
        QStringLiteral("multiplayerDescLabel")) : nullptr;
    check(desc && desc->text().contains(QStringLiteral("樱花")),
          "樱花核心下说明文字为樱花内容");

    QLineEdit *keyEdit = frpHome
        ? frpHome->findChild<QLineEdit *>(QStringLiteral("frpKeyEdit")) : nullptr;
    QLineEdit *idEdit = frpHome
        ? frpHome->findChild<QLineEdit *>(QStringLiteral("frpIdEdit")) : nullptr;
    check(keyEdit && idEdit, "首页有访问密钥/隧道 ID 输入框");

    QPushButton *startBtn = nullptr;
    QPushButton *panelBtn = nullptr;
    QPushButton *pickBtn = nullptr;
    for (QPushButton *btn : frpHome ? frpHome->findChildren<QPushButton *>()
                                    : QList<QPushButton *>())
    {
        if (btn->objectName() == QStringLiteral("multiplayerCreateBtn"))
            startBtn = btn;
        else if (btn->text() == QStringLiteral("打开面板"))
            panelBtn = btn;
        else if (btn->text() == QStringLiteral("手动选择 frpc"))
            pickBtn = btn;
    }
    check(startBtn && panelBtn && pickBtn, "首页有启动隧道/打开面板/手动选择 frpc 按钮");

    QLabel *status = frpHome ? frpHome->findChild<QLabel *>(
        QStringLiteral("multiplayerStatusLabel")) : nullptr;
    check(status && status->text().contains(QStringLiteral("frpc")),
          "状态标签为 frpc 安装状态文案");

    page.grab().save(QStringLiteral("mp_frp_sakura.png"));

    // 3) frpc 未安装时点「启动隧道」→ 只弹提示框
    if (startBtn && !startBtn->isEnabled())
    {
        startBtn->setEnabled(true); // 槽内部会再次校验
        startBtn->click();
        QApplication::processEvents();
        const QList<QDialog *> dlgs = topDialogs();
        check(!dlgs.isEmpty(), "未安装 frpc 时点启动弹出提示框");
        for (QDialog *dlg : dlgs)
            dlg->deleteLater();
        QApplication::processEvents();
    }

    // 4) 切到 OpenFrp 联机 → 说明切换
    combo->setCurrentIndex(3);
    QApplication::processEvents();
    check(stack && stack->currentIndex() == 4, "切到 OpenFrp 联机后仍停在 frp 首页");
    QLabel *desc2 = stack && stack->currentWidget()
        ? stack->currentWidget()->findChild<QLabel *>(
              QStringLiteral("multiplayerDescLabel")) : nullptr;
    check(desc2 && desc2->text().contains(QStringLiteral("OpenFrp")),
          "OpenFrp 核心下说明文字为 OpenFrp 内容");

    page.grab().save(QStringLiteral("mp_frp_openfrp.png"));

    // 5) 切回陶瓦再切回樱花
    combo->setCurrentIndex(0);
    QApplication::processEvents();
    check(stack && stack->currentIndex() == 0, "切回陶瓦后回到陶瓦首页");
    combo->setCurrentIndex(2);
    QApplication::processEvents();
    check(stack && stack->currentIndex() == 4, "再次切到樱花联机停在 frp 首页");

    std::printf("%s\n", failures == 0 ? "MP_FRP_PROBE: PASS" : "MP_FRP_PROBE: FAIL");
    return failures == 0 ? 0 : 1;
}
