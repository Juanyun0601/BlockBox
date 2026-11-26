/**
 * @file   chat_settings_probe.cpp
 * @brief  「聊天设置」居中弹窗探针：直接实例化 ChatSettingsDialog 并截图
 *
 * 用来肉眼核对：
 *   1. 弹窗是覆盖主窗口的模糊暗遮罩 + 居中圆角卡片，而不是系统小窗；
 *   2. 「当前会话」里的备注 / 置顶 / 免打扰 / 隐藏预览 / 清空草稿都在，没会话时整块隐藏；
 *   3. 通用区是「按 Enter 发送」开关 + 小 / 标准 / 大 三个字号分段按钮；
 *   4. 消息气泡的字号不会被全局气泡样式的 13px 压回去（小 / 大两档对比）；
 *   5. 聊天页左栏：备注名替换昵称、隐藏预览后只显示「消息内容已隐藏」。
 *
 * 输出：probe_out/chat_settings_*.png
 */
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "components/ChatSettingsDialog.h"
#include "utils/ChatPrefs.h"
#include "utils/ChatUiStore.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#define private public
#include "pages/ForumFriendsPage.h"
#undef private

namespace
{

/** 等待若干毫秒，让弹窗完成定位、抓背景与首帧绘制 */
void waitFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
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

/** 假的主窗口：几行聊天文字当背景，模糊遮罩才有内容可糊 */
QWidget *makeHost()
{
    auto *host = new QWidget();
    host->resize(1180, 740);
    host->setAutoFillBackground(true);
    QPalette palette = host->palette();
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#eef1f5")));
    host->setPalette(palette);
    auto *layout = new QVBoxLayout(host);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(10);
    for (const QString &text :
         {QStringLiteral("聊天页背景占位 · 好友与私信"),
          QStringLiteral("在吗？想问问那个光影包怎么装"),
          QStringLiteral("在的，直接把 zip 丢进 shaderpacks 就行"),
          QStringLiteral("装好了！帧数还挺稳")})
    {
        auto *label = new QLabel(text, host);
        label->setStyleSheet(QStringLiteral("color: #334155; font-size: 15px;"));
        layout->addWidget(label);
    }
    layout->addStretch(1);
    host->show();
    return host;
}

void applyQss()
{
    QFile qssFile(QStringLiteral("styles/style.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text))
        qApp->setStyleSheet(StyleKit::resolve(QString::fromUtf8(qssFile.readAll())));
}

void shootDialog(QWidget *host, const QString &prefix, bool withConversation, bool group)
{
    ChatSettingsDialog dialog(host);
    if (withConversation)
    {
        // 造一份「已经设过备注 / 隐藏预览 / 有草稿」的状态：看一眼填满时的样子
        ChatUiStore *store = ChatUiStore::instance();
        const QString key = group ? QStringLiteral("g3") : QStringLiteral("f1");
        store->setRemark(key, group ? QStringLiteral("插件交流群") : QStringLiteral("老王（生存搭子）"));
        store->setHidesPreview(key, true);
        store->setDraft(key, QStringLiteral("晚上八点开新存档吗？"));
        dialog.setConversation(key,
                               group ? QStringLiteral("方块盒子插件交流群") : QStringLiteral("阿伟"),
                               group);
    }
    else
        dialog.setConversation(QString(), QString(), false);
    dialog.show();
    waitFor(700);
    savePng(&dialog, prefix + (withConversation ? QStringLiteral("with_chat.png")
                                                : QStringLiteral("no_chat.png")));
    dialog.hide();
    waitFor(120);
}

/**
 * 消息气泡字号：按聊天页同样的结构（气泡 QFrame + 正文 QLabel）分别用 13 / 15px 画一遍，
 * 确认「聊天设置」调大的字号不会被全局 style.qss 里气泡的 13px 压回去。
 */
void shootBubbleFont()
{
    auto *stage = new QWidget();
    stage->setAutoFillBackground(true);
    QPalette palette = stage->palette();
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#eef1f5")));
    stage->setPalette(palette);
    auto *layout = new QVBoxLayout(stage);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(10);
    for (const int px : {13, 15})
    {
        auto *bubble = new QFrame(stage);
        bubble->setObjectName(QStringLiteral("forumChatBubbleTheirs"));
        auto *bubbleLayout = new QVBoxLayout(bubble);
        bubbleLayout->setContentsMargins(12, 8, 12, 8);
        auto *body = new QLabel(QStringLiteral("在吗？想问问那个光影包怎么装（%1px）").arg(px), bubble);
        body->setObjectName(QStringLiteral("forumChatBodyText"));
        body->setStyleSheet(
            QStringLiteral("QLabel#forumChatBodyText { font-size: %1px; }").arg(px));
        bubbleLayout->addWidget(body);
        layout->addWidget(bubble);
    }
    layout->addStretch(1);
    stage->resize(420, 180);
    stage->show();
    waitFor(300);
    savePng(stage, QStringLiteral("probe_out/chat_settings_bubble_font.png"));
    stage->close();
    delete stage;
}

/**
 * 聊天页左栏：确认备注名替换昵称、隐藏预览后只显示「消息内容已隐藏」。
 * 页面故意用 new 且不删：它自己会发网络请求，回调回来时控件还得在。
 */
void shootPage()
{
    auto *page = new ForumFriendsPage();
    page->resize(1090, 660);
    page->show();
    waitFor(300);

    ForumFriendInfo peer;
    peer.id = 1;
    peer.username = QStringLiteral("awei");
    peer.displayName = QStringLiteral("阿伟");
    peer.online = true;
    peer.unread = 2;
    peer.lastMessageBody = QStringLiteral("在吗？想问问那个光影包怎么装");
    peer.lastMessageKind = QStringLiteral("text");
    peer.lastMessageAt = QDateTime::currentSecsSinceEpoch() - 120;

    ForumGroupInfo groupInfo;
    groupInfo.id = 3;
    groupInfo.name = QStringLiteral("方块盒子插件交流群");
    groupInfo.unread = 12;
    groupInfo.lastMessageBody = QStringLiteral("新版本插件已经传上去了");
    groupInfo.lastMessageKind = QStringLiteral("text");
    groupInfo.lastMessageSender = QStringLiteral("像素小猫");
    groupInfo.lastMessageAt = QDateTime::currentSecsSinceEpoch() - 300;

    ChatUiStore *store = ChatUiStore::instance();
    store->setRemark(QStringLiteral("f1"), QStringLiteral("老王（生存搭子）"));
    store->setHidesPreview(QStringLiteral("f1"), true);
    store->setRemark(QStringLiteral("g3"), QStringLiteral("插件群"));
    store->setHidesPreview(QStringLiteral("g3"), true);
    store->setPinned(QStringLiteral("g3"), true);

    page->m_snapshot.friends = {peer};
    page->m_snapshot.groups = {groupInfo};
    page->m_peer = peer;
    page->m_chatEmpty->hide();
    page->m_chatMain->show();
    page->renderChatHead();
    page->renderFriendRows();
    QApplication::processEvents();
    savePng(page, QStringLiteral("probe_out/chat_settings_page_list.png"));

    page->hide();
    QApplication::processEvents();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    ThemeManager::instance()->applyThemeColor();
    applyQss();
    QApplication::processEvents();

    QDir().mkpath(QStringLiteral("probe_out"));

    QWidget *host = makeHost();
    QApplication::processEvents();

    // 浅色：带当前会话 + 不带会话（纯通用设置）各一张
    shootDialog(host, QStringLiteral("probe_out/chat_settings_light_"), true, false);
    shootDialog(host, QStringLiteral("probe_out/chat_settings_light_"), false, false);

    // 群聊：备注 / 隐藏预览 / 草稿都在的完整样子
    shootDialog(host, QStringLiteral("probe_out/chat_settings_group_"), true, true);

    // 窗口变矮：只有设置内容滚动，底部「完成」始终露在外面
    host->resize(900, 520);
    QApplication::processEvents();
    shootDialog(host, QStringLiteral("probe_out/chat_settings_short_"), true, false);
    host->resize(1180, 740);
    QApplication::processEvents();

    // 字号档位在真实气泡结构里的效果
    shootBubbleFont();

    // 左栏：备注名 + 隐藏预览的实际样子
    shootPage();

    // 深色：再来一张带会话的
    ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
    applyQss();
    QPalette palette = host->palette();
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#1a1a1a")));
    host->setPalette(palette);
    QApplication::processEvents();
    shootDialog(host, QStringLiteral("probe_out/chat_settings_dark_"), true, false);

    host->close();
    fprintf(stderr, "[probe] done\n");
    return 0;
}
