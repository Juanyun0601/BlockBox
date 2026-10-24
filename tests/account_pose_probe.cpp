/**
 * @file   account_pose_probe.cpp
 * @brief  账户管理页"模型动作"图标按钮网格视觉探针
 *
 * 创建 AccountManagePage，强制显示右侧面板"模型动作"按钮网格：
 *   1. 校验按钮数量 = 11（待机/挥手/走路/跑步/潜行/游泳/睡觉/攻击/舞蹈/欢呼/骑马坐姿）
 *      且图标均渲染成功；
 *   2. 依次点击每个动作按钮，每个姿势停留后整页截图 account_pose_<key>.png
 *      （关闭自动旋转，固定 3/4 视角，便于核对肢体角度与朝向）；
 *   3. 校验勾选态互斥迁移（点击后仅目标按钮勾选）；
 *   4. 切换 Dark 主题留档 account_pose_probe_dark.png，最后回到待机。
 *
 * 构建：与 account_bg_probe 相同——复用 build-win-release 的 release 目标文件
 * 链接（去掉 main.o，加入本文件编译出的 .o，库参数与主程序链接行一致）。
 * 运行目录：build-win-release（截图输出到当前目录）。退出码 0=PASS，1=FAIL。
 */
#include "pages/AccountManagePage.h"
#include "components/Skin3DWidget.h"
#include "utils/SkinDownloader.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QList>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWidget>

#include <cstdio>
#include <functional>

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

    // 无账户时页面切到空态并隐藏皮肤预览；探针需要看到 3D 模型核对姿势，
    // 与 account_bg_probe 相同：隐藏空态、强制显示皮肤控件，并铺一张默认皮肤
    if (QWidget *empty = page.findChild<QWidget *>(QStringLiteral("accountEmptyState")))
        empty->hide();
    if (Skin3DWidget *skin = page.findChild<Skin3DWidget *>())
    {
        skin->show();
        skin->setSkin(SkinDownloader::getDefaultSkinForUser(QStringLiteral("Player")));
    }

    QTimer::singleShot(600, [&]() {
        const QList<QPushButton *> btns = poseButtons(page);
        if (btns.size() != 11)
        {
            fprintf(stderr, "[probe] pose buttons = %lld (expect 11) -> FAIL\n",
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
        std::fflush(stderr);

        // 固定视角：关闭自动旋转并复位到默认 3/4 视角，逐姿势截图便于核对
        if (Skin3DWidget *skin = page.findChild<Skin3DWidget *>())
        {
            fprintf(stderr, "[probe] disabling auto-rotate\n");
            std::fflush(stderr);
            skin->setAutoRotate(false);
            skin->resetView(4.6f);   // 拉远相机，保证全身入镜便于核对肢体角度
            skin->rotateView(0.6f, 0.0f); // 水平偏转 ~35° 成真正 3/4 视角（默认 theta=0 几乎正对正面，前后摆动被透视压扁）
            fprintf(stderr, "[probe] view reset\n");
            std::fflush(stderr);
        }

        // 隔离实验：点击前先抓一张（无通知气泡干扰）
        {
            fprintf(stderr, "[probe] pre-click grab\n");
            std::fflush(stderr);
            const QPixmap pm = page.grab();
            fprintf(stderr, "[probe] pre-click grab ok %dx%d\n", pm.width(), pm.height());
            std::fflush(stderr);
        }

        // 依次点击每个动作按钮：勾选态校验 + 停留后整页截图
        // 状态放堆上（ctx），避免递归 lambda 按引用捕获外层局部变量悬空
        struct Ctx
        {
            bool pass = true;
            int index = 0;
            QList<QPushButton *> btns;
            AccountManagePage *page = nullptr;
            std::function<void()> step;
        };
        auto *ctx = new Ctx();
        ctx->btns = btns;
        ctx->page = &page;
        ctx->pass = pass;
        ctx->step = [&app, ctx]() {
            if (ctx->index >= ctx->btns.size())
            {
                // 深色主题留档 + 回到待机收尾
                if (ThemeManager::instance())
                    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
                ctx->page->grab().save(QStringLiteral("account_pose_probe_dark.png"));
                if (ThemeManager::instance())
                    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

                ctx->btns[0]->click();
                if (!ctx->btns[0]->isChecked())
                {
                    fprintf(stderr, "[probe] final idle not checked -> FAIL\n");
                    ctx->pass = false;
                }
                ctx->page->grab().save(QStringLiteral("account_pose_probe.png"));
                fprintf(stderr, "[probe] all pose screenshots saved -> %s\n",
                        ctx->pass ? "PASS" : "FAIL");
                std::fflush(stderr);
                const int rc = ctx->pass ? 0 : 1;
                // ctx 故意不释放：其成员 step 正在执行中，析构自身是未定义行为；探针随即退出
                app.exit(rc);
                return;
            }

            QPushButton *btn = ctx->btns[ctx->index];
            fprintf(stderr, "[probe] clicking %s\n", qPrintable(btn->text()));
            std::fflush(stderr);
            btn->click();
            if (!btn->isChecked())
            {
                fprintf(stderr, "[probe] click %s not checked -> FAIL\n",
                        qPrintable(btn->text()));
                ctx->pass = false;
            }
            for (int j = 0; j < ctx->btns.size(); ++j)
            {
                if (j != ctx->index && ctx->btns[j]->isChecked())
                {
                    fprintf(stderr, "[probe] button %d unexpectedly checked -> FAIL\n", j);
                    ctx->pass = false;
                }
            }

            const QString key = btn->property("poseData").toString();
            const int curIndex = ctx->index;
            QTimer::singleShot(60, [ctx, key, curIndex]() {
                // 每个姿势截图前强制复位相机（防止未知来源的相机扰动影响核对），
                // 450ms 覆盖相机收敛（lerp 0.25/帧 ≈ 15 帧）+ 动画推进到可辨识相位
                if (Skin3DWidget *skin = ctx->page->findChild<Skin3DWidget *>())
                {
                    skin->resetView(4.6f);
                    skin->rotateView(0.6f, 0.0f);
                }
                QTimer::singleShot(450, [ctx, key, curIndex]() {
                    fprintf(stderr, "[probe] grabbing %s\n", qPrintable(key));
                    std::fflush(stderr);
                    const QPixmap pm = ctx->page->grab();
                    fprintf(stderr, "[probe] grabbed %s (%dx%d)\n", qPrintable(key), pm.width(), pm.height());
                    std::fflush(stderr);
                    pm.save(QStringLiteral("account_pose_%1.png").arg(key));
                    fprintf(stderr, "[probe] saved account_pose_%s.png\n", qPrintable(key));
                    std::fflush(stderr);
                    ctx->index = curIndex + 1;
                    ctx->step();
                });
            });
        };
        ctx->step();
    });

    return app.exec();
}
