/**
 * @file   forum_thread_detail_probe.cpp
 * @brief  论坛帖子详情页视觉探针：用本地假论坛服务喂数据，截图对比网页版版面
 *
 * 做的事：
 *   1. 把 QSettings 重定向到临时目录，避免污染用户真实配置；
 *   2. 起一个 QTcpServer 冒充论坛接口，按路径返回固定 JSON（主题 / 楼层 / 楼中楼 / 同版块主题）；
 *   3. 把 SettingsManager 的论坛地址指向这个假服务，实例化真实的 ForumThreadDetailPage，
 *      打开 1 号主题，截图到 _out/forum_thread_detail_*.png。
 *
 * 用法（在 build-win-release 内）：
 *   bash build_forum_probe.bat  → 生成 probe_out/forum_thread_detail_probe.exe
 */

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>

#include "pages/ForumThreadDetailPage.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

namespace {

qint64 epochDaysAgo(int days)
{
    return QDateTime::currentSecsSinceEpoch() - qint64(days) * 86400;
}

QJsonObject makeAuthor(const QString &name, const QString &role, int avatarSeed, int postCount,
                       int threadCount, int joinedDaysAgo)
{
    QJsonObject author;
    author.insert(QStringLiteral("id"), avatarSeed);
    author.insert(QStringLiteral("username"), name);
    author.insert(QStringLiteral("display_name"), name);
    author.insert(QStringLiteral("avatar"), QString());   // 走首字母兜底，探针不依赖图片
    author.insert(QStringLiteral("role"), role);
    author.insert(QStringLiteral("status"), QStringLiteral("active"));
    author.insert(QStringLiteral("post_count"), postCount);
    author.insert(QStringLiteral("thread_count"), threadCount);
    author.insert(QStringLiteral("joined_at"), epochDaysAgo(joinedDaysAgo));
    return author;
}

QJsonObject makePost(int id, int floor, int parentId, const QString &content, const QString &name,
                     const QString &role, int likes, bool liked, bool deleted, int editCount,
                     int hoursAgo)
{
    QJsonObject post;
    post.insert(QStringLiteral("id"), id);
    post.insert(QStringLiteral("floor"), floor);
    if (parentId > 0)
        post.insert(QStringLiteral("parent_post_id"), parentId);
    post.insert(QStringLiteral("content"), deleted ? QString() : content);
    post.insert(QStringLiteral("deleted"), deleted);
    post.insert(QStringLiteral("edit_count"), editCount);
    post.insert(QStringLiteral("created_at"), epochDaysAgo(0) - hoursAgo * 3600);
    post.insert(QStringLiteral("likes"), likes);
    post.insert(QStringLiteral("liked"), liked);
    post.insert(QStringLiteral("author"), makeAuthor(name, role, id % 7 + 2, 30 + id, 4 + id,
                                                     hoursAgo / 24 + 90));
    return post;
}

QJsonObject threadFixture()
{
    QJsonObject thread;
    thread.insert(QStringLiteral("id"), 1);
    thread.insert(QStringLiteral("title"), QStringLiteral("整合包启动一直卡在 Forge 加载界面，日志见附件"));
    thread.insert(QStringLiteral("pinned"), true);
    thread.insert(QStringLiteral("locked"), false);
    thread.insert(QStringLiteral("views"), 3421);
    thread.insert(QStringLiteral("reply_count"), 27);
    thread.insert(QStringLiteral("created_at"), epochDaysAgo(3));
    thread.insert(QStringLiteral("last_post_at"), epochDaysAgo(0));
    thread.insert(QStringLiteral("bookmarked"), true);

    QJsonObject board;
    board.insert(QStringLiteral("slug"), QStringLiteral("general"));
    board.insert(QStringLiteral("name"), QStringLiteral("综合讨论"));
    thread.insert(QStringLiteral("board"), board);

    QJsonArray tags;
    tags.append(QStringLiteral("求助"));
    tags.append(QStringLiteral("Forge"));
    tags.append(QStringLiteral("整合包"));
    thread.insert(QStringLiteral("tags"), tags);

    thread.insert(QStringLiteral("author"),
                  makeAuthor(QStringLiteral("小白菜鸟"), QStringLiteral("user"), 12, 64, 9, 420));
    return thread;
}

QJsonObject threadsResponse(int page)
{
    QJsonObject root;
    root.insert(QStringLiteral("thread"), threadFixture());

    QJsonArray posts;

    QJsonObject first = makePost(
        101, 1, 0,
        QStringLiteral("**系统信息**\n\n"
                       "- 内存分配：`8192 MB`\n"
                       "- Java：17.0.8（已由盒子自动安装）\n"
                       "- 整合包：All the Mods 9 v0.2.4\n\n"
                       "启动后进度条停在 `Loading mods` 这一步，大概 40%，然后就一直不动了。\n\n"
                       "> 已经试过关掉光影、删掉 OptiFine，还是卡在这里。\n\n"
                       "日志最后几行：\n\n"
                       "```\n"
                       "[08:12:31] [main/INFO]: Loading 468 mods:\n"
                       "[08:12:33] [main/ERROR]: Missing or unsupported mandatory dependencies\n"
                       "```\n\n"
                       "求助各位大佬，是哪个模组在打架吗？"),
        QStringLiteral("小白菜鸟"), QStringLiteral("user"), 18, true, false, 1, 5);

    QJsonArray children;
    children.append(makePost(151, 2, 101,
                             QStringLiteral("看报错是缺前置，把 `jei` 升到最新试试。"), QStringLiteral("老K"),
                             QStringLiteral("user"), 4, false, false, 0, 4));
    children.append(makePost(152, 3, 101,
                             QStringLiteral("同一楼，先 `bisect` 一半模组跑起来，能定位到具体模组。"),
                             QStringLiteral("摸鱼の人"), QStringLiteral("user"), 2, false, false, 0, 4));
    children.append(makePost(153, 4, 101, QStringLiteral("这个楼层已被作者删除"), QStringLiteral("路人甲"),
                             QStringLiteral("user"), 0, false, true, 0, 3));
    children.append(makePost(154, 5, 101,
                             QStringLiteral("内存给 8G 对 ATM9 太少了，起码 10G，另外记得开 `-XX:+UseG1GC`。"),
                             QStringLiteral("爱坤"), QStringLiteral("admin"), 9, true, false, 0, 3));
    children.append(makePost(155, 6, 101, QStringLiteral("问题解决了吗？同样卡住 +1"), QStringLiteral("萌新"),
                             QStringLiteral("user"), 1, false, false, 0, 2));
    children.append(makePost(156, 7, 101, QStringLiteral("先看 `latest.log` 末尾 200 行，别贴 crash-report。"),
                             QStringLiteral("老K"), QStringLiteral("user"), 6, false, false, 0, 2));
    children.append(makePost(157, 8, 101, QStringLiteral("我也遇到过，换 Java 21 就好了。"),
                             QStringLiteral("萌新"), QStringLiteral("user"), 1, false, false, 0, 1));
    children.append(makePost(158, 9, 101, QStringLiteral("感谢各位，果然是内存分配问题，加到 10G 后正常进游戏了！"),
                             QStringLiteral("小白菜鸟"), QStringLiteral("user"), 22, false, false, 1, 1));
    first.insert(QStringLiteral("children"), children);
    first.insert(QStringLiteral("children_count"), children.size());
    posts.append(first);

    posts.append(makePost(
        102, 10, 0,
        QStringLiteral("补充一下二楼没说完的：\n\n"
                       "1. 先备份 `mods` 文件夹；\n"
                       "2. 每次拿掉一半模组启动；\n"
                       "3. 卡住就换回另一半，两三轮就能锁定问题模组。\n\n"
                       "另外 [官网配置说明](board.html?slug=general) 里也写了推荐参数。"),
        QStringLiteral("老K"), QStringLiteral("user"), 11, false, false, 0, 3));

    posts.append(makePost(
        103, 11, 0,
        QStringLiteral("这里贴一张启动器里的内存设置截图：\n\n"
                       "![内存设置](/uploads/screenshot-memory.png)\n\n"
                       "把最大内存拉到 `10240` 基本就不会卡在加载了。"),
        QStringLiteral("爱坤"), QStringLiteral("admin"), 7, false, false, 2, 2));

    posts.append(makePost(104, 12, 0, QStringLiteral("（这条是测试用的已删除楼层）"),
                          QStringLiteral("路人甲"), QStringLiteral("user"), 0, false, true, 0, 2));

    root.insert(QStringLiteral("posts"), posts);

    QJsonObject pagination;
    pagination.insert(QStringLiteral("page"), page);
    pagination.insert(QStringLiteral("per_page"), 20);
    pagination.insert(QStringLiteral("total"), 27);
    pagination.insert(QStringLiteral("pages"), 3);
    root.insert(QStringLiteral("pagination"), pagination);
    return root;
}

QJsonObject boardResponse(int page)
{
    QJsonObject root;
    QJsonObject board;
    board.insert(QStringLiteral("id"), 1);
    board.insert(QStringLiteral("slug"), QStringLiteral("general"));
    board.insert(QStringLiteral("name"), QStringLiteral("综合讨论"));
    board.insert(QStringLiteral("description"), QStringLiteral("灌水、求助、闲聊都放这里"));
    board.insert(QStringLiteral("icon"), QStringLiteral("💬"));
    board.insert(QStringLiteral("thread_count"), 128);
    board.insert(QStringLiteral("post_count"), 2048);
    root.insert(QStringLiteral("board"), board);

    QJsonArray threads;
    const QStringList titles = {
        QStringLiteral("盒子启动器打不开，双击没反应"),
        QStringLiteral("求推荐 1.20.1 的轻量光影"),
        QStringLiteral("服务器开了加速器还是延迟高"),
        QStringLiteral("分享一个自用的整合包配置"),
        QStringLiteral("为什么我的 Java 路径老是自动变"),
    };
    for (int i = 0; i < titles.size(); ++i)
    {
        QJsonObject thread;
        thread.insert(QStringLiteral("id"), 2 + i);
        thread.insert(QStringLiteral("title"), titles.at(i));
        thread.insert(QStringLiteral("views"), 120 + i * 37);
        thread.insert(QStringLiteral("reply_count"), 3 + i);
        thread.insert(QStringLiteral("created_at"), epochDaysAgo(1 + i));
        thread.insert(QStringLiteral("last_post_at"), epochDaysAgo(i));
        QJsonObject b;
        b.insert(QStringLiteral("slug"), QStringLiteral("general"));
        b.insert(QStringLiteral("name"), QStringLiteral("综合讨论"));
        thread.insert(QStringLiteral("board"), b);
        thread.insert(QStringLiteral("author"),
                      makeAuthor(QStringLiteral("用户%1").arg(i + 1), QStringLiteral("user"), 30 + i,
                                 12, 3, 200));
        threads.append(thread);
    }
    root.insert(QStringLiteral("threads"), threads);

    QJsonObject pagination;
    pagination.insert(QStringLiteral("page"), page);
    pagination.insert(QStringLiteral("per_page"), 20);
    pagination.insert(QStringLiteral("total"), 128);
    pagination.insert(QStringLiteral("pages"), 7);
    root.insert(QStringLiteral("pagination"), pagination);
    return root;
}

/** 极简假论坛服务：读请求行 → 按路径回 JSON */
class FakeForumServer : public QTcpServer
{
public:
    explicit FakeForumServer(QObject *parent = nullptr) : QTcpServer(parent) {}

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray request = socket->readAll();
            const QByteArray firstLine = request.left(request.indexOf("\r\n"));
            QByteArray path = firstLine.split(' ').value(1);
            const int queryAt = path.indexOf('?');
            int page = 1;
            const QByteArray query = queryAt >= 0 ? path.mid(queryAt + 1) : QByteArray();
            path = queryAt >= 0 ? path.left(queryAt) : path;
            for (const QByteArray &pair : query.split('&'))
            {
                if (pair.startsWith("page="))
                    page = pair.mid(5).toInt();
            }

            QJsonObject body;
            if (path.startsWith("/api/threads/"))
                body = threadsResponse(qMax(1, page));
            else if (path.startsWith("/api/boards/"))
                body = boardResponse(qMax(1, page));
            else
                body = QJsonObject();

            const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
            QByteArray reply = "HTTP/1.1 200 OK\r\n";
            reply += "Content-Type: application/json; charset=utf-8\r\n";
            reply += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n";
            reply += "Connection: close\r\n\r\n";
            reply += payload;
            socket->write(reply);
            socket->flush();
            socket->disconnectFromHost();
            std::fprintf(stderr, "[fake] %s?page=%d -> %d bytes\n", path.constData(), page,
                         payload.size());
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
};

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void grab(QWidget *widget, const QString &path)
{
    const QImage image = widget->grab().toImage();
    const bool ok = image.save(path);
    std::fprintf(stderr, "[%s] %s (%dx%d)\n", ok ? "ok" : "FAIL", qUtf8Printable(path),
                 image.width(), image.height());
}

} // namespace

int main(int argc, char **argv)
{
    const QString outDir = QStringLiteral("D:/BlockBox/_out");
    QDir().mkpath(outDir);

    // 1) 把配置写到临时目录，避免碰到用户真实设置
    const QString settingsDir = outDir + QStringLiteral("/forum_probe_settings");
    QDir().mkpath(settingsDir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);

    QApplication app(argc, argv);
    // 第二个参数给 dark 就出深色截图（默认浅色）
    const bool dark = QCoreApplication::arguments().value(1) == QStringLiteral("dark");
    ThemeManager::setTheme(&app, dark ? ThemeManager::DarkTheme : ThemeManager::LightTheme);

    // 2) 假论坛服务
    FakeForumServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
    {
        std::fprintf(stderr, "cannot listen\n");
        return 2;
    }
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    SettingsManager::instance()->setForumBaseUrl(base);
    std::fprintf(stderr, "forum base url = %s\n", qUtf8Printable(base));

    // 3) 真实页面
    QWidget win;
    win.resize(1480, 2600);
    auto *page = new ForumThreadDetailPage(&win);
    page->resize(win.size());
    win.show();
    QApplication::processEvents();

    page->openThread(1);
    waitMs(1500);
    QApplication::processEvents();

    grab(page, outDir + QStringLiteral("/forum_thread_detail_%1.png").arg(dark ? QStringLiteral("dark")
                                                                              : QStringLiteral("light")));

    QTimer::singleShot(10, &app, &QCoreApplication::quit);
    return app.exec();
}
