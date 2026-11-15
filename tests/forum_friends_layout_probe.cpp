/**
 * @file   forum_friends_layout_probe.cpp
 * @brief  「聊天（好友与私信）」页版面探针：左栏子导航化 + 右栏去大卡片
 *
 * 直接实例化 ForumFriendsPage，注入一份假的好友与消息数据（不走网络），
 * 按当前主题渲染并截图，用来肉眼核对：
 *   1. 左栏是圆角玻璃面板（头部标题 + 胶囊页签 + 分组标题 + 条目），不再是一列裸列表；
 *   2. 右栏聊天区不再被一张大卡片包住，会话头 / 消息 / 输入区直接铺在页面上。
 *
 * 输出：probe_out/forum_friends_*.png
 */
#define private public
#include "pages/ForumFriendsPage.h"
#undef private

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>

#include <cstdio>

#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace
{

ForumFriendInfo makeFriend(int id, const QString &name, const QString &last, bool online,
                           int unread, qint64 minutesAgo)
{
    ForumFriendInfo info;
    info.id = id;
    info.username = name;
    info.displayName = name;
    info.online = online;
    info.unread = unread;
    info.lastMessageBody = last;
    info.lastMessageAt = QDateTime::currentSecsSinceEpoch() - minutesAgo * 60;
    return info;
}

ForumChatMessage makeMessage(int id, const QString &body, bool mine, qint64 minutesAgo)
{
    ForumChatMessage message;
    message.id = id;
    message.kind = QStringLiteral("text");
    message.body = body;
    message.mine = mine;
    message.createdAt = QDateTime::currentSecsSinceEpoch() - minutesAgo * 60;
    message.read = mine;
    return message;
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

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    const bool dark = app.arguments().contains(QStringLiteral("--dark"));
    const bool narrow = app.arguments().contains(QStringLiteral("--narrow"));

    // 套用当前主题：程序里 QSS 来自嵌入资源，探针这里直接读工作区里的 style.qss，
    // 保证改过的样式（左栏面板、右栏去卡片）能在截图中生效
    ThemeManager::instance()->applyThemeColor();
    if (dark)
        ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
    QFile qssFile(QStringLiteral("styles/style.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text))
        qApp->setStyleSheet(StyleKit::resolve(QString::fromUtf8(qssFile.readAll())));
    // 深色：真实程序里这几条由 ThemeManager 的 dark override 追加，探针手动补上等价规则
    if (dark)
    {
        qApp->setStyleSheet(qApp->styleSheet() +
                            QStringLiteral("\nQFrame#forumSidePanel {"
                                           " background-color: rgba(30, 30, 30, 0.9);"
                                           " border: 1px solid #4a4a4a; }\n"));
    }
    QApplication::processEvents();

    QDir().mkpath(QStringLiteral("probe_out"));
    QString prefix = dark ? QStringLiteral("probe_out/forum_friends_dark_")
                          : QStringLiteral("probe_out/forum_friends_");
    if (narrow)
        prefix += QStringLiteral("narrow_");

    ForumFriendsPage page;
    page.resize(narrow ? 760 : 1180, 740);
    page.show();
    QApplication::processEvents();

    // 页面内容区本身是透明的：深色截图时补一个深色底，才能看清气泡 / 时间 / 胶囊的真实观感
    if (dark)
    {
        QPalette palette = page.palette();
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#1a1a1a")));
        page.setAutoFillBackground(true);
        page.setPalette(palette);
    }

    // 注入假数据（showEvent 里的未登录分支会清空列表，所以放在 show 之后）
    page.m_snapshot.friends = {
        makeFriend(1, QStringLiteral("阿伟"), QStringLiteral("今晚一起开黑？"), true, 3, 2),
        makeFriend(2, QStringLiteral("像素小猫"), QStringLiteral("我：整合包已经传上去了"), false, 0, 40),
        makeFriend(3, QStringLiteral("Steve_2024"), QStringLiteral("好的，明天下载"), true, 0, 180),
    };
    page.m_peer = page.m_snapshot.friends.first();
    page.renderFriendRows();

    // 右栏：开一个会话并塞几条消息（绕过网络请求）
    page.m_chatEmpty->hide();
    page.m_chatMain->show();
    page.m_chatInput->clear();
    page.renderChatHead();
    page.m_messages = {
        makeMessage(1, QStringLiteral("在吗？想问问那个光影包怎么装"), false, 26),
        makeMessage(2, QStringLiteral("在的，直接把 zip 丢进 shaderpacks 就行"), true, 24),
        makeMessage(3, QStringLiteral("装好了！帧数还挺稳"), false, 3),
    };
    // 引用条 + 撤回占位 + 文件卡片：一起核对右栏的各类消息观感
    ForumChatMessage reply;
    reply.id = 4;
    reply.kind = QStringLiteral("text");
    reply.body = QStringLiteral("收到，我照这个顺序再试一次");
    reply.replyTo = 1;
    reply.replyBody = QStringLiteral("在吗？想问问那个光影包怎么装");
    reply.createdAt = QDateTime::currentSecsSinceEpoch() - 120;
    page.m_messages.append(reply);
    ForumChatMessage withdrawn;
    withdrawn.id = 5;
    withdrawn.kind = QStringLiteral("withdrawn");
    withdrawn.createdAt = QDateTime::currentSecsSinceEpoch() - 90;
    page.m_messages.append(withdrawn);
    ForumChatMessage file;
    file.id = 6;
    file.kind = QStringLiteral("file");
    file.body = QStringLiteral("安装步骤都写在里面了");
    file.fileName = QStringLiteral("光影包安装说明.txt");
    file.fileUrl = QStringLiteral("/uploads/chat/demo.txt");
    file.fileSize = 4096;
    file.mine = true;
    file.read = true;
    file.createdAt = QDateTime::currentSecsSinceEpoch() - 60;
    page.m_messages.append(file);
    page.m_hasMore = true;
    page.renderMessages();

    QApplication::processEvents();
    page.m_msgsScroll->verticalScrollBar()->setValue(page.m_msgsScroll->verticalScrollBar()->maximum());
    // 顺带看一眼「引用条 + 输入坞聚焦描边」的样子
    page.setReplyTarget(1, QStringLiteral("在吗？想问问那个光影包怎么装"), false,
                        QStringLiteral("阿伟"));
    QApplication::processEvents();

    savePng(&page, prefix + QStringLiteral("left_panel.png"));

    // 未选会话时的右栏空状态（不再被卡片包住）
    page.closeConversation();
    QApplication::processEvents();
    savePng(&page, prefix + QStringLiteral("empty.png"));

    // 添加好友页签：搜索框落在子导航面板里
    page.m_addTabButton->click();
    QApplication::processEvents();
    page.renderAddTab();
    QApplication::processEvents();
    savePng(&page, prefix + QStringLiteral("add_tab.png"));

    fprintf(stderr, "[probe] done\n");
    return 0;
}
