/**
 * @file   chat_input_probe.cpp
 * @brief  聊天页输入坞视觉探针（对照 AI 助手页的输入卡片）
 *
 * 注入假数据后渲染整页并额外裁出输入区，用来核对输入坞的新版式：
 * 文本域铺满整行、底部工具行（表情/图片/文件 + 字数 + 发送）。
 */
#define private public
#include "pages/ForumFriendsPage.h"
#undef private

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
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

    ThemeManager::instance()->applyThemeColor();
    if (dark)
        ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
    QFile qssFile(QStringLiteral("styles/style.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text))
        qApp->setStyleSheet(StyleKit::resolve(QString::fromUtf8(qssFile.readAll())));
    QApplication::processEvents();

    QDir().mkpath(QStringLiteral("probe_out"));
    QString prefix = dark ? QStringLiteral("probe_out/chat_input_dark_")
                          : QStringLiteral("probe_out/chat_input_");
    if (narrow)
        prefix += QStringLiteral("narrow_");

    ForumFriendsPage page;
    page.resize(narrow ? 760 : 1180, 740);
    page.show();
    QApplication::processEvents();

    if (dark)
    {
        QPalette palette = page.palette();
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#1a1a1a")));
        page.setAutoFillBackground(true);
        page.setPalette(palette);
    }

    page.m_snapshot.friends = {
        makeFriend(1, QStringLiteral("阿伟"), QStringLiteral("今晚一起开黑？"), true, 3, 2),
        makeFriend(2, QStringLiteral("像素小猫"), QStringLiteral("我：整合包已经传上去了"), false, 0, 40),
        makeFriend(3, QStringLiteral("Steve_2024"), QStringLiteral("好的，明天下载"), true, 0, 180),
    };
    page.m_peer = page.m_snapshot.friends.first();
    page.renderFriendRows();

    page.m_chatEmpty->hide();
    page.m_chatMain->show();
    page.m_chatInput->clear();
    page.renderChatHead();
    page.m_messages = {
        makeMessage(1, QStringLiteral("在吗？想问问那个光影包怎么装"), false, 26),
        makeMessage(2, QStringLiteral("在的，直接把 zip 丢进 shaderpacks 就行"), true, 24),
        makeMessage(3, QStringLiteral("装好了！帧数还挺稳"), false, 3),
    };
    page.m_hasMore = true;
    page.renderMessages();

    QApplication::processEvents();
    page.m_msgsScroll->verticalScrollBar()->setValue(page.m_msgsScroll->verticalScrollBar()->maximum());
    page.setReplyTarget(1, QStringLiteral("在吗？想问问那个光影包怎么装"), false,
                        QStringLiteral("阿伟"));
    QApplication::processEvents();

    fprintf(stderr, "[probe] emptyInputH=%d cardH=%d\n", page.m_chatInput->height(), page.m_chatInputBox->height());
    savePng(&page, prefix + QStringLiteral("page.png"));

    // 输入坞特写：单独抓输入卡片 + 引用条
    if (QWidget *composer = page.m_chatInputBox ? page.m_chatInputBox->parentWidget() : nullptr)
    {
        if (!composer->grab().save(prefix + QStringLiteral("composer.png")))
            fprintf(stderr, "[probe] composer shot FAILED\n");
        else
            fprintf(stderr, "[probe] shot %scomposer.png\n", qPrintable(prefix));
    }

    // 多行输入：卡片随内容长高，且工具行仍在底部
    page.m_chatInput->setPlainText(QStringLiteral("在的，直接把 zip 丢进 shaderpacks 就行\n"
                                                  "如果它还闪白，就把光影设置里的抗锯齿关掉\n"
                                                  "再不行把显卡驱动更新一下试试"));
    QApplication::processEvents();
    page.refreshInputSize();
    QApplication::processEvents();
    savePng(&page, prefix + QStringLiteral("multiline.png"));
    if (QWidget *composer = page.m_chatInputBox ? page.m_chatInputBox->parentWidget() : nullptr)
        composer->grab().save(prefix + QStringLiteral("composer_multiline.png"));

    // 关闭会话：空状态仍正常
    page.closeConversation();
    QApplication::processEvents();
    savePng(&page, prefix + QStringLiteral("empty.png"));

    // 重新开一个会话，看字数统计与「发送中」禁用态
    page.m_chatEmpty->hide();
    page.m_chatMain->show();
    page.m_chatInput->setPlainText(QString(900, QChar(0x6D4B)));
    QApplication::processEvents();
    page.refreshInputSize();
    QApplication::processEvents();
    {
        int blocks = 0;
        int lines = 0;
        for (QTextBlock block = page.m_chatInput->document()->begin(); block.isValid();
             block = block.next())
        {
            ++blocks;
            lines += qMax(1, block.lineCount());
        }
        fprintf(stderr,
                "[probe] counter: blocks=%d visualLines=%d docH=%d inputH=%d cardH=%d cardHint=%d\n",
                blocks, lines, qRound(page.m_chatInput->document()->size().height()),
                page.m_chatInput->height(), page.m_chatInputBox->height(),
                page.m_chatInputBox->sizeHint().height());
    }
    if (QWidget *composer = page.m_chatInputBox ? page.m_chatInputBox->parentWidget() : nullptr)
        composer->grab().save(prefix + QStringLiteral("composer_counter.png"));
    page.m_sendButton->setEnabled(false);
    QApplication::processEvents();
    if (QWidget *composer = page.m_chatInputBox ? page.m_chatInputBox->parentWidget() : nullptr)
        composer->grab().save(prefix + QStringLiteral("composer_disabled.png"));
    page.m_sendButton->setEnabled(true);

    // 行高与文档高度实测（用来定输入框最小/最大高度）
    fprintf(stderr, "[probe] lineSpacing=%d\n", page.m_chatInput->fontMetrics().lineSpacing());
    for (int lines = 1; lines <= 6; ++lines)
    {
        QString text;
        for (int i = 0; i < lines; ++i)
            text += QStringLiteral("第 %1 行\n").arg(i + 1);
        page.m_chatInput->setPlainText(text);
        QApplication::processEvents();
        fprintf(stderr, "[probe] lines=%d docH=%d widgetH=%d\n", lines,
                qRound(page.m_chatInput->document()->size().height()),
                page.m_chatInput->height());
    }

    fprintf(stderr, "[probe] done\n");
    return 0;
}
