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
#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
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

/** 插件帖：主题上挂了 .BlockBox 插件包，详情页应该换成资源详情页的版面 */
QJsonObject pluginThreadResponse(int page)
{
    QJsonObject root;

    QJsonObject thread;
    thread.insert(QStringLiteral("id"), 2);
    thread.insert(QStringLiteral("title"), QStringLiteral("快捷指令面板：一键跑常用命令"));
    thread.insert(QStringLiteral("pinned"), false);
    thread.insert(QStringLiteral("locked"), false);
    thread.insert(QStringLiteral("views"), 1286);
    thread.insert(QStringLiteral("reply_count"), 9);
    thread.insert(QStringLiteral("created_at"), epochDaysAgo(6));
    thread.insert(QStringLiteral("last_post_at"), epochDaysAgo(0));
    thread.insert(QStringLiteral("bookmarked"), false);

    QJsonObject board;
    board.insert(QStringLiteral("slug"), QStringLiteral("plugins"));
    board.insert(QStringLiteral("name"), QStringLiteral("插件分享"));
    thread.insert(QStringLiteral("board"), board);

    QJsonArray tags;
    tags.append(QStringLiteral("插件"));
    tags.append(QStringLiteral("指令"));
    thread.insert(QStringLiteral("tags"), tags);

    thread.insert(QStringLiteral("author"),
                  makeAuthor(QStringLiteral("方块开发者"), QStringLiteral("user"), 21, 88, 12, 300));

    QJsonObject plugin;
    plugin.insert(QStringLiteral("id"), 7);
    plugin.insert(QStringLiteral("upload_id"), 3);
    plugin.insert(QStringLiteral("plugin_id"), QStringLiteral("blockbox.command-panel"));
    plugin.insert(QStringLiteral("name"), QStringLiteral("快捷指令面板"));
    plugin.insert(QStringLiteral("version"), QStringLiteral("1.2.0"));
    plugin.insert(QStringLiteral("author"), QStringLiteral("方块开发者"));
    plugin.insert(QStringLiteral("description"),
                  QStringLiteral("把常用的传送、调试和设置指令做成一排按钮，进游戏按一个键就能用。"));
    plugin.insert(QStringLiteral("icon"), QString());   // 没有图标，走渐变占位
    plugin.insert(QStringLiteral("file_size"), 84213);
    plugin.insert(QStringLiteral("downloads"), 326);
    thread.insert(QStringLiteral("plugin"), plugin);
    root.insert(QStringLiteral("thread"), thread);

    QJsonArray posts;
    posts.append(makePost(
        201, 1, 0,
        QStringLiteral("**快捷指令面板**\n\n"
                       "把常用的几条指令收进一个面板：\n\n"
                       "- 传送：`/home`、`/spawn`、`/back`\n"
                       "- 调试：`/gamerule`、`/time set day`\n"
                       "- 开关：`/gamemode creative`\n\n"
                       "按 `K` 呼出面板，按钮可以自己改。"),
        QStringLiteral("方块开发者"), QStringLiteral("user"), 12, false, false, 0, 6));
    posts.append(makePost(202, 2, 0, QStringLiteral("装上了，`/home` 和 `/spawn` 两键直连，舒服。"),
                          QStringLiteral("老K"), QStringLiteral("user"), 3, false, false, 0, 2));
    posts.append(makePost(203, 3, 0, QStringLiteral("建议加个「复制当前坐标」的按钮。"),
                          QStringLiteral("萌新"), QStringLiteral("user"), 1, false, false, 0, 1));
    root.insert(QStringLiteral("posts"), posts);

    QJsonObject pagination;
    pagination.insert(QStringLiteral("page"), page);
    pagination.insert(QStringLiteral("per_page"), 20);
    pagination.insert(QStringLiteral("total"), 9);
    pagination.insert(QStringLiteral("pages"), 1);
    root.insert(QStringLiteral("pagination"), pagination);
    return root;
}

/** 投影帖：主题上挂了 .litematic 投影，还带作者传的展示图轮播（图片 + B 站视频），
 *  用来核对「投影详情页的轮播图」和资源详情页的「游戏截图」轮播是不是同一套样式 */
QJsonObject schematicThreadResponse(int page)
{
    QJsonObject root;

    QJsonObject thread;
    thread.insert(QStringLiteral("id"), 3);
    thread.insert(QStringLiteral("title"), QStringLiteral("中世纪风车磨坊 · 完整投影分享"));
    thread.insert(QStringLiteral("pinned"), false);
    thread.insert(QStringLiteral("locked"), false);
    thread.insert(QStringLiteral("views"), 2014);
    thread.insert(QStringLiteral("reply_count"), 12);
    thread.insert(QStringLiteral("created_at"), epochDaysAgo(5));
    thread.insert(QStringLiteral("last_post_at"), epochDaysAgo(0));
    thread.insert(QStringLiteral("bookmarked"), false);

    QJsonObject board;
    board.insert(QStringLiteral("slug"), QStringLiteral("schematic-share"));
    board.insert(QStringLiteral("name"), QStringLiteral("投影分享"));
    thread.insert(QStringLiteral("board"), board);

    QJsonArray tags;
    tags.append(QStringLiteral("建筑"));
    tags.append(QStringLiteral("中世纪"));
    thread.insert(QStringLiteral("tags"), tags);

    thread.insert(QStringLiteral("author"),
                  makeAuthor(QStringLiteral("风车匠"), QStringLiteral("user"), 17, 42, 6, 260));

    QJsonObject schematic;
    schematic.insert(QStringLiteral("id"), 11);
    schematic.insert(QStringLiteral("upload_id"), 5);
    schematic.insert(QStringLiteral("name"), QStringLiteral("风车磨坊"));
    schematic.insert(QStringLiteral("file_name"), QStringLiteral("windmill.litematic"));
    schematic.insert(QStringLiteral("file_size"), 5242880);
    schematic.insert(QStringLiteral("file_count"), 1);
    schematic.insert(QStringLiteral("downloads"), 1893);
    thread.insert(QStringLiteral("schematic"), schematic);

    QJsonArray carousel;
    QJsonObject shot1;
    shot1.insert(QStringLiteral("kind"), QStringLiteral("image"));
    shot1.insert(QStringLiteral("url"), QStringLiteral("/uploads/probe_carousel_1.png"));
    shot1.insert(QStringLiteral("thumb"), QStringLiteral("/uploads/probe_carousel_1.png"));
    carousel.append(shot1);
    QJsonObject shot2;
    shot2.insert(QStringLiteral("kind"), QStringLiteral("image"));
    shot2.insert(QStringLiteral("url"), QStringLiteral("/uploads/probe_carousel_2.png"));
    shot2.insert(QStringLiteral("thumb"), QStringLiteral("/uploads/probe_carousel_2.png"));
    carousel.append(shot2);
    QJsonObject video;
    video.insert(QStringLiteral("kind"), QStringLiteral("bilibili"));
    video.insert(QStringLiteral("url"), QStringLiteral("https://www.bilibili.com/video/BV1xx411c7mD"));
    carousel.append(video);
    thread.insert(QStringLiteral("carousel"), carousel);

    root.insert(QStringLiteral("thread"), thread);

    QJsonArray posts;
    posts.append(makePost(
        301, 1, 0,
        QStringLiteral("**风车磨坊**\n\n"
                       "照着山城小镇搭的风车，叶片会跟着风向转（红石驱动）。\n\n"
                       "- 占地：32×24\n"
                       "- 材料：橡木、石砖、白色羊毛\n"
                       "- 存档版本：1.21.4\n\n"
                       "示意图见上方轮播，B 站有整段旋转演示。"),
        QStringLiteral("风车匠"), QStringLiteral("user"), 26, false, false, 0, 5));
    posts.append(makePost(302, 2, 0, QStringLiteral("叶片轴心那一段能发一下细节吗？"),
                          QStringLiteral("红石党"), QStringLiteral("user"), 2, false, false, 0, 2));
    root.insert(QStringLiteral("posts"), posts);

    QJsonObject pagination;
    pagination.insert(QStringLiteral("page"), page);
    pagination.insert(QStringLiteral("per_page"), 20);
    pagination.insert(QStringLiteral("total"), 12);
    pagination.insert(QStringLiteral("pages"), 1);
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

/** 极简假论坛服务：读请求行 → 按路径回 JSON / PNG */
class FakeForumServer : public QTcpServer
{
public:
    explicit FakeForumServer(QObject *parent = nullptr) : QTcpServer(parent) {}

    /** 预置一张探针图（路径 → PNG 字节），轮播图直接吃这里的图，不连外网 */
    void setProbeImage(const QString &path, const QByteArray &png, const QByteArray &mime)
    {
        m_images.insert(path, png);
        m_mimes.insert(path, mime);
    }

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

            // 轮播图：直接回一张探针 PNG
            if (path.startsWith("/uploads/"))
            {
                const QString key = QString::fromLatin1(path);
                const QByteArray bytes = m_images.value(key);
                if (!bytes.isEmpty())
                {
                    const QByteArray mime = m_mimes.value(key, QByteArrayLiteral("image/png"));
                    QByteArray reply = "HTTP/1.1 200 OK\r\n";
                    reply += "Content-Type: " + mime + "\r\n";
                    reply += "Content-Length: " + QByteArray::number(bytes.size()) + "\r\n";
                    reply += "Connection: close\r\n\r\n";
                    reply += bytes;
                    socket->write(reply);
                    socket->flush();
                    socket->disconnectFromHost();
                    std::fprintf(stderr, "[fake] %s -> %d bytes png\n", path.constData(),
                                 int(bytes.size()));
                    return;
                }
            }

            QJsonObject body;
            if (path.startsWith("/api/threads/"))
            {
                // 1 号是普通求助帖，2 号是挂了插件包的插件帖，3 号是带轮播的投影帖
                const int threadId = path.mid(13).split('/').value(0).toInt();
                if (threadId == 2)
                    body = pluginThreadResponse(qMax(1, page));
                else if (threadId == 3)
                    body = schematicThreadResponse(qMax(1, page));
                else
                    body = threadsResponse(qMax(1, page));
            }
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
                         int(payload.size()));
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    QHash<QString, QByteArray> m_images;
    QHash<QString, QByteArray> m_mimes;
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

    // 探针直接吃源码里的 style.qss：改样式不用重编 36MB 的资源包
    // （排查时可加参数 noqss 跳过，用资源包里编译好的样式）
    if (QCoreApplication::arguments().contains(QStringLiteral("noqss")))
    {
        // 跳过源码样式
    }
    else if (QFile styleFile(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
             styleFile.open(QFile::ReadOnly))
    {
        app.setStyleSheet(ThemeManager::instance()->resolveTokens(
            QString::fromUtf8(styleFile.readAll())));
        std::fprintf(stderr, "[probe] style.qss 从源码加载\n");
    }

    // 2) 假论坛服务
    FakeForumServer server;

    // 卡片轮播要的假「游戏截图」：两张 1280×720 的渐变图，不连外网
    const auto makeProbeShot = [](const QColor &skyTop, const QColor &skyBottom,
                                  const QColor &ground, const QString &caption) {
        QImage image(1280, 720, QImage::Format_RGB32);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QLinearGradient sky(0, 0, 0, image.height());
        sky.setColorAt(0.0, skyTop);
        sky.setColorAt(0.72, skyBottom);
        painter.fillRect(image.rect(), sky);
        painter.fillRect(QRect(0, 520, image.width(), 200), ground);
        // 几块「房子」，让截图看起来像建筑投影
        painter.setBrush(QColor(226, 214, 190));
        painter.setPen(Qt::NoPen);
        painter.drawRect(QRect(120, 380, 260, 180));
        painter.drawRect(QRect(880, 330, 300, 230));
        painter.setBrush(QColor(122, 92, 68));
        painter.drawRect(QRect(960, 220, 30, 140));
        painter.setBrush(QColor(96, 148, 72));
        painter.drawEllipse(QPoint(300, 470), 110, 90);
        QFont font = painter.font();
        font.setPixelSize(42);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 560, image.width(), 80), Qt::AlignCenter, caption);
        painter.end();
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return png;
    };
    server.setProbeImage(QStringLiteral("/uploads/probe_carousel_1.png"),
                         makeProbeShot(QColor(122, 180, 236), QColor(206, 232, 252),
                                       QColor(76, 140, 64), QStringLiteral("风车磨坊 · 正面")),
                         QByteArrayLiteral("image/png"));
    server.setProbeImage(QStringLiteral("/uploads/probe_carousel_2.png"),
                         makeProbeShot(QColor(246, 176, 112), QColor(252, 226, 190),
                                       QColor(88, 132, 68), QStringLiteral("风车磨坊 · 夜景")),
                         QByteArrayLiteral("image/png"));

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

    // 排查用：notread 只建页面不加载主题，plain 只出普通帖截图，noqss 用资源包里的样式
    if (QCoreApplication::arguments().contains(QStringLiteral("notread")))
    {
        QTimer::singleShot(10, &app, &QCoreApplication::quit);
        return app.exec();
    }

    page->openThread(1);
    waitMs(1500);
    QApplication::processEvents();

    grab(page, outDir + QStringLiteral("/forum_thread_detail_%1.png").arg(dark ? QStringLiteral("dark")
                                                                              : QStringLiteral("light")));

    // 第二个参数给 plain 就只出普通帖的截图（排查用）
    if (QCoreApplication::arguments().value(2) == QStringLiteral("plain"))
    {
        QTimer::singleShot(10, &app, &QCoreApplication::quit);
        return app.exec();
    }

    // 4) 插件帖：版面应该换成资源详情页那套（通栏头部 + 左下载 / 右介绍讨论）
    page->openThread(2);
    waitMs(1500);
    QApplication::processEvents();
    grab(page, outDir + QStringLiteral("/forum_thread_detail_plugin_%1.png")
                     .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));

    // 5) 窄窗口：左栏折到正文上方，主题头仍然通栏
    page->resize(900, 2200);
    waitMs(600);
    QApplication::processEvents();
    grab(page, outDir + QStringLiteral("/forum_thread_detail_plugin_narrow_%1.png")
                     .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));

    // 6) 投影帖：轮播卡要和资源详情页的「游戏截图」轮播同一套样式
    page->resize(1480, 2600);
    waitMs(300);
    page->openThread(3);
    waitMs(2200);
    QApplication::processEvents();
    grab(page, outDir + QStringLiteral("/forum_thread_detail_projection_%1.png")
                     .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));

    // 6.1) 点下一张：缩略图高亮和计数要跟着走（顺带验证翻页不花）
    if (auto *next = page->findChild<QPushButton *>(QStringLiteral("modDetailScreenshotNext")))
    {
        QApplication::processEvents();
        next->click();
        waitMs(900);
        QApplication::processEvents();
        grab(page, outDir + QStringLiteral("/forum_thread_detail_projection_next_%1.png")
                         .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
    }

    // 6.15) 再点一张：翻到 B 站视频页（播放卡），缩略图条上的播放格同步高亮
    if (auto *next = page->findChild<QPushButton *>(QStringLiteral("modDetailScreenshotNext")))
    {
        next->click();
        waitMs(900);
        QApplication::processEvents();
        grab(page, outDir + QStringLiteral("/forum_thread_detail_projection_video_%1.png")
                         .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
    }

    // 6.2) 投影帖的窄窗口：舞台按 16:9 收窄，缩略图条仍然可见
    page->resize(900, 2200);
    waitMs(700);
    QApplication::processEvents();
    grab(page, outDir + QStringLiteral("/forum_thread_detail_projection_narrow_%1.png")
                     .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));

    QTimer::singleShot(10, &app, &QCoreApplication::quit);
    return app.exec();
}
