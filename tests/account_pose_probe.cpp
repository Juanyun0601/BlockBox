/**
 * @file   account_pose_probe.cpp
 * @brief  账户管理页"模型动作"图标按钮网格视觉探针
 *
 * 创建 AccountManagePage，强制显示右侧面板"模型动作"按钮网格：
 *   1. 校验按钮数量 = 5（待机/挥手/走路/跑步/骑马坐姿）且图标均渲染成功；
 *   2. 切换 Dark/Light 主题，验证 checked 态图标随主题色重新着色不崩溃；
 *   3. 依次点击"走路"/"骑马坐姿"/"待机"，校验勾选态互斥迁移；
 *   4. 输出截图 account_pose_probe_dark.png / account_pose_probe.png。
 *
 * 构建：与 account_bg_probe 相同——复用 build-win-release 的 release 目标文件
 * 链接（去掉 main.o，加入本文件编译出的 .o，库参数与主程序链接行一致）。
 * 运行目录：build-win-release（截图输出到当前目录）。退出码 0=PASS，1=FAIL。
 */
#include "pages/AccountManagePage.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QList>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWidget>

#include <cstdio>

namespace {

QList<QPushButton *> poseButtons(const AccountManagePage &page)
{
    QList<QPushButton *> result;
    for (QPushButton *btn : page.findChildren<QPushButton *>())
    {
        if (btn->objectName() == QStringLiteral("skinPoseButton"))
            result.append(btn);
    }
    return result;
}

// 自底向上显示按钮的祖先链（无账户时动作网格被 loadAccounts 隐藏）
void forceShow(QWidget *w)
{
    while (w)
    {
        if (!w->isVisible())
            w->setVisible(true);
        w = w->parentWidget();
    }
}

} // namespace

int main(int argc, char **argv)
{
    // 与主程序一致的默认表面格式（Skin3DWidget 使用 330 core 着色器）
    QSurfaceFormat glFormat;
    glFormat.setVersion(3, 3);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    glFormat.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(glFormat);

    QApplication app(argc, argv);

    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

    AccountManagePage page;
    page.resize(1100, 720);
    page.show();

    QTimer::singleShot(600, [&]() {
        const QList<QPushButton *> btns = poseButtons(page);
        if (btns.size() != 5)
        {
            fprintf(stderr, "[probe] pose buttons = %lld (expect 5) -> FAIL\n",
                    static_cast<long long>(btns.size()));
            app.exit(1);
            return;
        }

        bool pass = true;
        for (QPushButton *btn : btns)
        {
            forceShow(btn);
            if (btn->icon().pixmap(16, 16).isNull())
            {
                fprintf(stderr, "[probe] %s icon renders empty -> FAIL\n",
                        qPrintable(btn->text()));
                pass = false;
            }
        }
        fprintf(stderr, "[probe] icons rendered, pass=%d\n", pass ? 1 : 0);

        // 深色主题：验证 themeChanged 图标重新着色路径不崩溃，并留档截图
        if (ThemeManager::instance())
            ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
        page.grab().save(QStringLiteral("account_pose_probe_dark.png"));
        if (ThemeManager::instance())
            ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

        // 点击"走路"：勾选态应迁移且互斥
        btns[2]->click();
        if (!btns[2]->isChecked())
        {
            fprintf(stderr, "[probe] click walk not checked -> FAIL\n");
            pass = false;
        }
        for (int i = 0; i < btns.size(); ++i)
        {
            if (i != 2 && btns[i]->isChecked())
            {
                fprintf(stderr, "[probe] button %d unexpectedly checked -> FAIL\n", i);
                pass = false;
            }
        }

        // 点击"骑马坐姿"（跨两列的那个）：勾选态应继续迁移
        btns[4]->click();
        if (!btns[4]->isChecked() || btns[2]->isChecked())
        {
            fprintf(stderr, "[probe] click ride state wrong -> FAIL\n");
            pass = false;
        }

        // 回到待机
        btns[0]->click();
        if (!btns[0]->isChecked() || btns[4]->isChecked())
        {
            fprintf(stderr, "[probe] click idle state wrong -> FAIL\n");
            pass = false;
        }

        page.grab().save(QStringLiteral("account_pose_probe.png"));
        fprintf(stderr, "[probe] screenshots saved -> %s\n", pass ? "PASS" : "FAIL");
        std::fflush(stderr);
        app.exit(pass ? 0 : 1);
    });

    return app.exec();
}
