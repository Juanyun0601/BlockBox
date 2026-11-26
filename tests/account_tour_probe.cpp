/**
 * @file   account_tour_probe.cpp
 * @brief  账户管理页「首次进入聚光灯引导」视觉 / 行为探针
 *
 * 直接实例化 ForumAccountPage，验证：
 *   step1  角色页（游戏账户列表）被抠亮，气泡贴在它旁边，文案讲「游戏账户 / 启动游戏要用」
 *   step2  资料卡被抠亮，文案讲「方块盒子账户 / 发帖、好友联机」
 *   完成   走完两步后写 account_tour/shown，再进页面不再弹
 *   跳过   点「跳过」同样收掉并记标记
 *   点击   点遮罩空白处也能推进
 *   深色   深色主题下高亮边和气泡可读
 *
 * 用法：account_tour_probe [light|dark] [窗口宽] [窗口高]
 * 输出：probe_out/tour_<主题>_<宽>x<高>_*.png；退出码 0 = 全部断言通过。
 *
 * 构建：复用 build-win-release 的 release 对象（去掉 main.o），见 build_tour_probe.bat。
 */
#include "pages/ForumAccountPage.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QEventLoop>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <cstdio>

namespace
{

const QString kShotDir = QStringLiteral("probe_out");
int g_failures = 0;

class BackgroundLayer : public QWidget
{
public:
    explicit BackgroundLayer(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0xf0, 0xf2, 0xf1));
    }
};

class ProbeWindow : public QWidget
{
public:
    ProbeWindow(int width, int height)
    {
        m_bg = new BackgroundLayer(this);
        m_page = new ForumAccountPage(this);
        resize(width, height);
    }

    ForumAccountPage *page() const { return m_page; }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        m_bg->setGeometry(rect());
        m_bg->lower();
        if (m_page)
        {
            m_page->setGeometry(rect());
            m_page->raise();
        }
    }

private:
    BackgroundLayer *m_bg = nullptr;
    ForumAccountPage *m_page = nullptr;
};

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void check(bool ok, const QString &what)
{
    fprintf(stderr, "[probe] %s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    if (!ok)
        ++g_failures;
}

QString shot(QWidget &window, const QString &name)
{
    const QString path = QStringLiteral("%1/tour_%2.png").arg(kShotDir, name);
    window.grab().save(path);
    fprintf(stderr, "[probe] shot %s\n", qPrintable(path));
    return path;
}

QWidget *findChildByName(QWidget *root, const char *name)
{
    return root ? root->findChild<QWidget *>(QString::fromLatin1(name)) : nullptr;
}

/** 遮罩上的气泡：挡住气泡中心的点属于气泡，其余属于遮罩 */
QPushButton *findTourButton(QWidget *window, const QString &text)
{
    QWidget *overlay = findChildByName(window, "fbTourOverlay");
    if (!overlay)
        return nullptr;
    const QList<QPushButton *> buttons = overlay->findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
    {
        if (button->text() == text && button->isVisible())
            return button;
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("light");
    const int winWidth = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 1040;
    const int winHeight = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 800;
    const QString sizeTag = QStringLiteral("_%1x%2").arg(winWidth).arg(winHeight);

    QApplication app(argc, argv);

    // 引导只在「没看过」时弹：每次跑探针前清掉标记，退出时还原用户真实值
    QSettings rawSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    const QVariant backup = rawSettings.value(QStringLiteral("account_tour/shown"));
    SettingsManager *settings = SettingsManager::instance();
    if (settings)
        settings->removeProperty(QStringLiteral("account_tour/shown"));

    auto restore = [&]() {
        SettingsManager *s = SettingsManager::instance();
        if (!s)
            return;
        if (backup.isValid())
            s->setProperty(QStringLiteral("account_tour/shown"), backup);
        else
            s->removeProperty(QStringLiteral("account_tour/shown"));
    };

    if (ThemeManager::instance())
    {
        ThemeManager::instance()->loadTheme(mode == QStringLiteral("dark")
                                                ? ThemeManager::DarkTheme
                                                : ThemeManager::LightTheme);
    }

    ProbeWindow window(winWidth, winHeight);
    window.show();
    waitMs(1400);   // 等入场动画 + 650ms 引导延时

    QWidget *overlay = findChildByName(&window, "fbTourOverlay");
    check(overlay != nullptr && overlay->isVisible(), QStringLiteral("首次进入弹出引导遮罩"));

    QLabel *step = qobject_cast<QLabel *>(findChildByName(&window, "fbTourStep"));
    QLabel *title = qobject_cast<QLabel *>(findChildByName(&window, "fbTourTitle"));
    check(step && step->text() == QStringLiteral("1 / 2"), QStringLiteral("第一步标题显示 1 / 2"));
    check(title && title->text().contains(QStringLiteral("游戏账户")),
          QStringLiteral("第一步讲游戏账户"));
    shot(window, mode + sizeTag + QStringLiteral("_step1"));

    QPushButton *next = findTourButton(&window, QStringLiteral("下一步"));
    check(next != nullptr, QStringLiteral("第一步按钮是「下一步」"));
    if (next)
        next->click();
    waitMs(400);

    check(title && title->text().contains(QStringLiteral("方块盒子账户")),
          QStringLiteral("第二步讲方块盒子账户"));
    check(step && step->text() == QStringLiteral("2 / 2"), QStringLiteral("第二步标题显示 2 / 2"));
    shot(window, mode + sizeTag + QStringLiteral("_step2"));

    QPushButton *done = findTourButton(&window, QStringLiteral("我知道了"));
    check(done != nullptr, QStringLiteral("最后一步按钮是「我知道了」"));
    if (done)
        done->click();
    waitMs(300);

    overlay = findChildByName(&window, "fbTourOverlay");
    check(overlay == nullptr || !overlay->isVisible(), QStringLiteral("走完后遮罩收起"));
    check(settings->getProperty(QStringLiteral("account_tour/shown"), false).toBool(),
          QStringLiteral("走完后记下「已看过」"));
    shot(window, mode + sizeTag + QStringLiteral("_done"));

    // 再进一次页面：不该再弹
    window.page()->hide();
    waitMs(120);
    window.page()->show();
    waitMs(1200);
    overlay = findChildByName(&window, "fbTourOverlay");
    check(overlay == nullptr || !overlay->isVisible(), QStringLiteral("看过之后再进页面不再弹"));

    // 重置标记 → 验证点遮罩空白处推进
    if (settings)
        settings->removeProperty(QStringLiteral("account_tour/shown"));
    window.page()->hide();
    waitMs(120);
    window.page()->show();
    waitMs(1200);
    overlay = findChildByName(&window, "fbTourOverlay");
    check(overlay != nullptr && overlay->isVisible(), QStringLiteral("清掉标记后重新弹出"));

    if (overlay)
    {
        // 遮罩左下角远离气泡和亮块，点这里应当推进到第二步
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(30, overlay->height() - 30),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(overlay, &press);
        waitMs(400);
        title = qobject_cast<QLabel *>(findChildByName(&window, "fbTourTitle"));
        check(title && title->text().contains(QStringLiteral("方块盒子账户")),
              QStringLiteral("点遮罩空白处推进到第二步"));
        shot(window, mode + sizeTag + QStringLiteral("_click_advance"));

        QWidget *skip = findChildByName(&window, "fbTourSkip");
        check(skip != nullptr, QStringLiteral("气泡上有「跳过」"));
        if (auto *skipBtn = qobject_cast<QPushButton *>(skip))
            skipBtn->click();
        waitMs(300);
        check(settings->getProperty(QStringLiteral("account_tour/shown"), false).toBool(),
              QStringLiteral("跳过也记下「已看过」"));
    }

    restore();
    fprintf(stderr, "[probe] %s（%d 项失败）\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
