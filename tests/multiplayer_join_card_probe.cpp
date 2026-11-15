/**
 * @file   multiplayer_join_card_probe.cpp
 * @brief  联机页「加入房间」就地输入卡探针：加入 / 创建房间都不再弹窗
 *
 * 只把一个 Java 版联机页和一个基岩版联机页放进空窗口，核对：
 *   1. 点「加入房间」时邀请码输入卡就地展开，焦点落在输入框，没有任何 QDialog；
 *   2. 邀请码填错 / 核心没装时错误提示挂在输入卡里（error 属性置位），仍然没有 QDialog；
 *   3. 点「创建房间」同理不弹窗；
 *   4. 亮 / 暗主题、宽 / 窄屏下输入卡排版正常（截图留档）。
 *
 * 输出：.tmp_mp_join_probe/probe_out/join_*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>

#include <cstdio>

#include "utils/ThemeManager.h"

// 探针要直接点按钮、读私有成员，这里临时放开访问权限（与其它页面探针同一套做法）
#define private public
#include "pages/BedrockMultiplayerPage.h"
#include "pages/MultiplayerPage.h"
#include "utils/GravityCone/GravityConeClient.h"
#include "utils/Terracotta/TerracottaClient.h"
#undef private

namespace
{
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-56s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void pump()
{
    for (int i = 0; i < 6; ++i)
    {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

bool savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return false;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(),
            shot.height(), ok ? "ok" : "FAILED");
    return ok;
}

/// 当前屏幕上可见的弹窗数量：就地交互全程必须一直是 0
int visibleDialogs()
{
    int count = 0;
    for (QWidget *widget : QApplication::topLevelWidgets())
    {
        if (widget->isVisible() && qobject_cast<QDialog *>(widget))
            ++count;
    }
    return count;
}

void stopTimers(QWidget *root)
{
    for (QTimer *timer : root->findChildren<QTimer *>())
        timer->stop();
}

void expectJoinCardOpen(const char *label, QWidget *page, QLineEdit *edit, bool visible)
{
    expect(label, visible);
    Q_UNUSED(page);
    expect("输入卡展开后焦点在邀请码输入框", !visible || (edit && edit->hasFocus()));
}

void expectNoDialog(const char *label)
{
    const int dialogs = visibleDialogs();
    if (dialogs != 0)
        fprintf(stderr, "[probe]   ^ 可见弹窗数量 = %d\n", dialogs);
    expect(label, dialogs == 0);
}

/// 让页面上所有按钮可用（真实环境里核心就绪后就是这个状态）
void enableButtons(QWidget *page)
{
    for (QPushButton *button : page->findChildren<QPushButton *>())
        button->setEnabled(true);
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    // offscreen 平台不会自动找系统字体，手动挂上中文黑体，否则截图全是方框
    for (const QString &path : {QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                                QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"),
                                QStringLiteral("C:/Windows/Fonts/segoeui.ttf")})
    {
        if (QFontDatabase::addApplicationFont(path) >= 0)
            fprintf(stderr, "[probe] font %s ok\n", qPrintable(path));
    }
    QFont probeFont(QStringLiteral("Microsoft YaHei"));
    probeFont.setPixelSize(13);
    app.setFont(probeFont);

    QDir().mkpath(QStringLiteral(".tmp_mp_join_probe/probe_out"));

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

    // ============================================================
    // Java 版联机页
    // ============================================================
    auto *host = new QWidget;
    host->resize(430, 760);

    MultiplayerPage javaPage(host);
    javaPage.setGeometry(0, 0, 430, 760);
    host->show();
    pump();
    stopTimers(&javaPage);
    enableButtons(&javaPage);

    expect("Java 版：加入房间按钮存在", javaPage.m_joinBtn != nullptr);
    expect("Java 版：邀请码输入卡默认收起", javaPage.m_joinCard && !javaPage.m_joinCard->isVisible());

    // ---- 点「加入房间」：就地展开输入卡，不弹窗 ----
    javaPage.m_joinBtn->click();
    pump();
    expectJoinCardOpen("Java 版：点加入房间后输入卡展开", &javaPage, javaPage.m_joinCodeEdit,
                       javaPage.m_joinCard && javaPage.m_joinCard->isVisible());
    expectNoDialog("Java 版：展开输入卡时没有弹窗");

    savePng(host, QStringLiteral(".tmp_mp_join_probe/probe_out/join_java_light_430.png"));

    // ---- 邀请码填错：错误提示在卡片里 ----
    javaPage.m_joinCodeEdit->setText(QStringLiteral("不是邀请码"));
    javaPage.submitJoinCode();
    pump();
    expect("Java 版：邀请码格式错误有就地提示",
           javaPage.m_joinHint
               && javaPage.m_joinHint->property("error").toBool()
               && javaPage.m_joinHint->text().contains(QStringLiteral("格式")));
    expectNoDialog("Java 版：邀请码填错时没有弹窗");
    savePng(host, QStringLiteral(".tmp_mp_join_probe/probe_out/join_java_error_430.png"));

    // ---- 邀请码格式对、但没装核心：仍然只在卡片里说一句 ----
    javaPage.m_joinCodeEdit->setText(QStringLiteral("U/1234-5678-9ABC-DEF0"));
    javaPage.submitJoinCode();
    pump();
    expect("Java 版：没装核心时就地说明", javaPage.m_joinHint
                                               && javaPage.m_joinHint->property("error").toBool()
                                               && !javaPage.m_joinHint->text().isEmpty());
    expectNoDialog("Java 版：没装核心时没有弹窗");

    // ---- 取消：输入卡收起，内容清空 ----
    javaPage.m_joinCancelBtn->click();
    pump();
    expect("Java 版：取消后输入卡收起", javaPage.m_joinCard && !javaPage.m_joinCard->isVisible());
    expect("Java 版：取消后输入框清空", javaPage.m_joinCodeEdit->text().isEmpty());

    // ---- 点「创建房间」：只在页面状态条 / 通知里反馈，不弹窗 ----
    javaPage.m_createBtn->click();
    pump();
    expectNoDialog("Java 版：点创建房间时没有弹窗");

    // ---- 桌面宽度 + 暗色主题 ----
    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    javaPage.applyThemeStyles();
    javaPage.showJoinCard();
    javaPage.m_joinCodeEdit->setText(QStringLiteral("U/8F2K-91QX-7B3D-4M2N"));
    host->resize(900, 760);
    javaPage.setGeometry(0, 0, 900, 760);
    pump();
    savePng(host, QStringLiteral(".tmp_mp_join_probe/probe_out/join_java_dark_900.png"));

    // 样式表里的 @TOKEN@ 必须全部解析完（残留会让 Qt 丢掉整条规则）
    const QString sheet = javaPage.styleSheet();
    expect("Java 版：样式表里没有残留的 @TOKEN@", !sheet.isEmpty() && !sheet.contains(QLatin1Char('@')));

    // ============================================================
    // 基岩版联机页
    // ============================================================
    auto *bedrockHost = new QWidget;
    bedrockHost->resize(430, 760);

    BedrockMultiplayerPage bedrockPage(bedrockHost);
    bedrockPage.setGeometry(0, 0, 430, 760);
    bedrockHost->show();
    pump();
    stopTimers(&bedrockPage);
    enableButtons(&bedrockPage);

    expect("基岩版：加入房间按钮存在", bedrockPage.m_joinBtn != nullptr);
    expect("基岩版：邀请码输入卡默认收起",
           bedrockPage.m_joinCard && !bedrockPage.m_joinCard->isVisible());

    bedrockPage.m_joinBtn->click();
    pump();
    expectJoinCardOpen("基岩版：点加入房间后输入卡展开", &bedrockPage, bedrockPage.m_joinCodeEdit,
                       bedrockPage.m_joinCard && bedrockPage.m_joinCard->isVisible());
    expectNoDialog("基岩版：展开输入卡时没有弹窗");
    savePng(bedrockHost, QStringLiteral(".tmp_mp_join_probe/probe_out/join_bedrock_light_430.png"));

    bedrockPage.m_joinCodeEdit->setText(QStringLiteral("P/1234-5678-9ABC-DEF0"));
    bedrockPage.submitJoinCode();
    pump();
    expect("基岩版：没装核心时就地说明",
           bedrockPage.m_joinHint && bedrockPage.m_joinHint->property("error").toBool()
               && !bedrockPage.m_joinHint->text().isEmpty());
    expectNoDialog("基岩版：提交邀请码时没有弹窗");

    bedrockPage.m_createBtn->click();
    pump();
    expectNoDialog("基岩版：点创建房间时没有弹窗");

    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    bedrockPage.applyThemeStyles();
    bedrockPage.showJoinCard();
    bedrockPage.m_joinCodeEdit->setText(QStringLiteral("P/8F2K-91QX-7B3D-4M2N"));
    bedrockHost->resize(900, 760);
    bedrockPage.setGeometry(0, 0, 900, 760);
    pump();
    savePng(bedrockHost,
            QStringLiteral(".tmp_mp_join_probe/probe_out/join_bedrock_dark_900.png"));

    // 核心推送的进度改成页面里的一行小字
    emit GravityConeClient::instance()->progressUpdated(QStringLiteral("host"),
                                                        QStringLiteral("正在创建房间，等待游戏开服…"));
    pump();
    expect("基岩版：核心进度就地显示",
           bedrockPage.m_progressHint && bedrockPage.m_progressHint->isVisible()
               && !bedrockPage.m_progressHint->text().isEmpty());
    expectNoDialog("基岩版：核心推进度时没有弹窗");

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
            g_failures);
    return g_failures == 0 ? 0 : 1;
}
