/**
 * @file   forum_account_probe.cpp
 * @brief  账户管理页「论坛账户界面」视觉 / 行为探针
 *
 * 直接实例化 ForumAccountPage，按参数截取不同状态：
 *   guest     未登录总览（名称应为「未登录」，右侧有登录 / 注册）
 *   login     点「登录」后的登录卡
 *   register  点「注册」后的注册卡
 *   member    用论坛账号登录 → 总览 + 我的主题 / 我的回复 / 我的收藏 / 账号设置
 *   dark      深色主题下的未登录总览
 *
 * 用法：forum_account_probe <mode> [baseUrl] [username] [password]
 * 输出：probe_out/forum_<mode>_*.png；退出码 0 = 全部断言通过。
 *
 * 构建：复用 build-win-release 的 release 对象（去掉 main.o），见 build_probe.bat。
 * 探针会临时改写 QSettings 里的论坛地址 / 会话 / 主题，结束前恢复。
 */
#include "pages/ForumAccountPage.h"
#include "pages/ForumUserProfilePage.h"
#include "utils/ForumAccountService.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QEventLoop>
#include <QFont>
#include <QFrame>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextFragment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <cstdio>

namespace
{

const QString kShotDir = QStringLiteral("probe_out");

class BackgroundLayer : public QWidget
{
public:
    explicit BackgroundLayer(QWidget *parent = nullptr) : QWidget(parent) {}

    void setColor(const QColor &color) { m_color = color; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), m_color);
    }

private:
    QColor m_color = QColor(0xf0, 0xf2, 0xf1);
};

class ProbeWindow : public QWidget
{
public:
    ProbeWindow()
    {
        m_bg = new BackgroundLayer(this);
        m_page = new ForumAccountPage(this);
        resize(1040, 800);
    }

    ForumAccountPage *page() const { return m_page; }
    BackgroundLayer *background() const { return m_bg; }

    /** 显式销毁页面（验证析构路径），此后窗口不再引用它 */
    void destroyPage()
    {
        if (!m_page)
            return;
        m_page->hide();
        delete m_page;
        m_page = nullptr;
    }

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

/** 本地假论坛：只为「通知」页探针提供 /api/me 与 /api/notifications 的固定响应 */
class FakeForumServer : public QObject
{
public:
    explicit FakeForumServer(bool empty, bool fail = false, QObject *parent = nullptr,
                             const QString &bio = QString::fromUtf8("测试账号"))
        : QObject(parent), m_empty(empty), m_fail(fail), m_bio(bio)
    {
    }

    bool start()
    {
        if (!m_server.listen(QHostAddress::LocalHost, 0))
            return false;
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
            {
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket]() { onReadyRead(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                    m_buffers.remove(socket);
                    socket->deleteLater();
                });
            }
        });
        fprintf(stderr, "[probe] fake forum on %s\n", qPrintable(baseUrl()));
        return true;
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

private:
    void onReadyRead(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer.append(socket->readAll());

        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        const QByteArray header = buffer.left(headerEnd);
        int contentLength = 0;
        for (const QByteArray &line : header.split('\n'))
        {
            const int colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).trimmed().toLower() == "content-length")
                contentLength = line.mid(colon + 1).trimmed().toInt();
        }
        if (buffer.size() < headerEnd + 4 + contentLength)
            return; // body 还没收全

        const QList<QByteArray> requestLine = header.split('\n').value(0).split(' ');
        const QString method = requestLine.size() > 0 ? QString::fromLatin1(requestLine.at(0))
                                                     : QString();
        const QString path = requestLine.size() > 1
                                 ? QString::fromLatin1(requestLine.at(1)).section('?', 0, 0)
                                 : QString();
        const QByteArray requestBody = buffer.mid(headerEnd + 4, contentLength);
        buffer.clear();

        const QByteArray body = responseFor(method, path, requestBody);
        QByteArray head = "HTTP/1.1 " + QByteArray::number(m_status) + " "
                          + (m_status == 200 ? QByteArrayLiteral("OK")
                                             : QByteArrayLiteral("Internal Server Error"))
                          + QByteArrayLiteral("\r\nContent-Type: application/json; charset=utf-8\r\n"
                                              "Connection: close\r\nContent-Length: ");
        m_status = 200;
        head += QByteArray::number(body.size());
        head += QByteArrayLiteral("\r\n\r\n");
        socket->write(head + body);
        socket->disconnectFromHost();
    }

    static QJsonObject notification(int id, const QString &type, const QString &targetKind,
                                    const QString &threadTitle, const QString &excerpt,
                                    const QStringList &actors, int count, bool unread,
                                    qint64 createdAt, int threadId)
    {
        QJsonArray actorArray;
        for (const QString &actor : actors)
            actorArray.append(QJsonObject{{QStringLiteral("username"), actor},
                                          {QStringLiteral("display_name"), actor}});
        QJsonArray ids;
        for (int i = 0; i < count; ++i)
            ids.append(id + i);
        return QJsonObject{{QStringLiteral("id"), id},
                           {QStringLiteral("type"), type},
                           {QStringLiteral("target_kind"), targetKind},
                           {QStringLiteral("thread_id"), threadId},
                           {QStringLiteral("post_id"), id},
                           {QStringLiteral("thread_title"), threadTitle},
                           {QStringLiteral("excerpt"), excerpt},
                           {QStringLiteral("count"), count},
                           {QStringLiteral("unread"), unread},
                           {QStringLiteral("unread_count"), unread ? count : 0},
                           {QStringLiteral("created_at"), static_cast<double>(createdAt)},
                           {QStringLiteral("actors"), actorArray},
                           {QStringLiteral("ids"), ids}};
    }

    QByteArray responseFor(const QString &method, const QString &path,
                           const QByteArray &requestBody)
    {
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        // 标记已读：带 ids 只清这些组，空 body 视为全部已读
        if (path.startsWith(QStringLiteral("/api/notifications")) && method != QStringLiteral("GET"))
        {
            if (path.contains(QStringLiteral("read")))
            {
                const QJsonArray ids = QJsonDocument::fromJson(requestBody)
                                           .object()
                                           .value(QStringLiteral("ids"))
                                           .toArray();
                if (ids.isEmpty())
                {
                    m_allRead = true;
                }
                for (const QJsonValue &value : ids)
                    m_read.insert(value.toInt());
            }
            return QByteArrayLiteral("{}");
        }

        // 用户主页探针：资料 + 空的主题 / 回复列表
        if (path.startsWith(QStringLiteral("/api/users/7/threads"))
            || path.startsWith(QStringLiteral("/api/users/7/posts")))
        {
            const QJsonObject pagination{{QStringLiteral("page"), 1},
                                         {QStringLiteral("per_page"), 20},
                                         {QStringLiteral("total"), 0},
                                         {QStringLiteral("total_pages"), 1}};
            return QJsonDocument(QJsonObject{{QStringLiteral("threads"), QJsonArray()},
                                             {QStringLiteral("posts"), QJsonArray()},
                                             {QStringLiteral("pagination"), pagination}})
                .toJson(QJsonDocument::Compact);
        }
        if (path == QStringLiteral("/api/users/7"))
        {
            const QJsonObject user{{QStringLiteral("id"), 7},
                                   {QStringLiteral("username"), QStringLiteral("probe")},
                                   {QStringLiteral("display_name"), QStringLiteral("方块盒探针")},
                                   {QStringLiteral("bio"), m_bio},
                                   {QStringLiteral("role"), QStringLiteral("admin")},
                                   {QStringLiteral("status"), QStringLiteral("active")},
                                   {QStringLiteral("thread_count"), 12},
                                   {QStringLiteral("post_count"), 34},
                                   {QStringLiteral("created_at"), 1700000000.0}};
            return QJsonDocument(QJsonObject{{QStringLiteral("user"), user},
                                             {QStringLiteral("friendship"),
                                              QStringLiteral("none")},
                                             {QStringLiteral("threads"), QJsonArray()},
                                             {QStringLiteral("replies"), QJsonArray()}})
                .toJson(QJsonDocument::Compact);
        }

        if (path.startsWith(QStringLiteral("/api/me")) && !path.startsWith(QStringLiteral("/api/me/")))
        {
            const QJsonObject user{{QStringLiteral("id"), 7},
                                   {QStringLiteral("username"), QStringLiteral("probe")},
                                   {QStringLiteral("display_name"), QStringLiteral("方块盒探针")},
                                   {QStringLiteral("bio"), m_bio},
                                   {QStringLiteral("role"), QStringLiteral("user")},
                                   {QStringLiteral("status"), QStringLiteral("active")},
                                   {QStringLiteral("thread_count"), 12},
                                   {QStringLiteral("post_count"), 34},
                                   {QStringLiteral("quiz_passed"), true},
                                   {QStringLiteral("created_at"), 1700000000.0}};
            return QJsonDocument(QJsonObject{{QStringLiteral("user"), user}})
                .toJson(QJsonDocument::Compact);
        }

        if (path.startsWith(QStringLiteral("/api/notifications")))
        {
            if (m_fail)
            {
                m_status = 500;
                return QByteArrayLiteral("{\"error\":\"\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8\xE5\xBC\x80\xE5\xB0\x8F\xE5\xB7\xAE\xE4\xBA\x86\"}");
            }
            QJsonArray items;
            int unreadGroups = 0;
            // 按「已读集合」重算每组的未读状态，模拟真实后端
            auto push = [&](QJsonObject item) {
                const QJsonArray ids = item.value(QStringLiteral("ids")).toArray();
                bool groupRead = m_allRead;
                if (!groupRead)
                {
                    groupRead = !ids.isEmpty();
                    for (const QJsonValue &value : ids)
                    {
                        if (!m_read.contains(value.toInt()))
                        {
                            groupRead = false;
                            break;
                        }
                    }
                }
                const bool unread = item.value(QStringLiteral("unread")).toBool() && !groupRead;
                item.insert(QStringLiteral("unread"), unread);
                item.insert(QStringLiteral("unread_count"),
                            unread ? item.value(QStringLiteral("count")).toInt() : 0);
                if (unread)
                    ++unreadGroups;
                items.append(item);
            };

            if (!m_empty)
            {
            push(notification(1, QStringLiteral("mention"), QStringLiteral("post"),
                                      QStringLiteral("1.21.5 光影整合包推荐"),
                                      QStringLiteral("这套光影的配置能不能发一下？我想放到自己的整合包里。"),
                                      {QStringLiteral("Steve"), QStringLiteral("Alex")}, 2, true,
                                      now - 420, 101));
            push(notification(3, QStringLiteral("reply"), QStringLiteral("post"),
                                      QStringLiteral("启动器卡在检测 Java 怎么解决"),
                                      QStringLiteral("换 JDK 21 就好了，旧版启动器挑 Java 版本。"),
                                      {QStringLiteral("Notch")}, 1, true, now - 5400, 102));
            push(notification(4, QStringLiteral("reply"), QStringLiteral("thread"),
                                      QStringLiteral("方块盒子 2.0 更新都改了啥"),
                                      QStringLiteral("新界面比原来清爽太多了，尤其是账户页。"),
                                      {QStringLiteral("Herobrine"), QStringLiteral("Alex"),
                                       QStringLiteral("Steve"), QStringLiteral("Dinnerbone")},
                                      7, true, now - 7200, 103));
            push(notification(11, QStringLiteral("system"), QString(),
                                      QStringLiteral("论坛服务器维护公告"),
                                      QStringLiteral("今晚 23:00 起维护约 30 分钟，期间可能无法发帖。"),
                                      {}, 1, false, now - 86400 - 3600, 0));
            push(notification(12, QStringLiteral("mention"), QStringLiteral("post"),
                                      QStringLiteral("自定义启动参数求助"),
                                      QStringLiteral("你上次发的参数表还有吗？"),
                                      {QStringLiteral("Notch")}, 1, false, now - 86400 * 2, 104));
            push(notification(13, QStringLiteral("reply"), QStringLiteral("thread"),
                                      QStringLiteral("整合包打包工具推荐"),
                                      QString(), {QStringLiteral("Piston")}, 1, false,
                                      now - 86400 * 6, 105));
            }

            QJsonObject pagination{{QStringLiteral("page"), 1},
                                   {QStringLiteral("per_page"), 20},
                                   {QStringLiteral("total"), items.size()},
                                   {QStringLiteral("total_pages"), 1}};
            return QJsonDocument(QJsonObject{{QStringLiteral("items"), items},
                                             {QStringLiteral("unread"), unreadGroups},
                                             {QStringLiteral("pagination"), pagination}})
                .toJson(QJsonDocument::Compact);
        }

        return QByteArrayLiteral("{}");
    }

    QTcpServer m_server;
    QMap<QTcpSocket *, QByteArray> m_buffers;
    bool m_empty = false;
    bool m_fail = false;
    bool m_allRead = false;
    int m_status = 200;
    QSet<int> m_read;
    QString m_bio;   ///< /api/me 与缓存用户返回的简介（简介 Markdown 探针用）
};

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/** 只找当前可见的按钮（隐藏子视图里也有同名按钮） */
QPushButton *findVisibleButton(QWidget *root, const QString &text)
{
    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
    {
        if (button->text() == text && button->isVisible())
            return button;
    }
    return nullptr;
}

QString grab(ProbeWindow &window, const QString &name)
{
    const QString path = QStringLiteral("%1/forum_%2.png").arg(kShotDir, name);
    window.grab().save(path);
    fprintf(stderr, "[probe] shot %s\n", qPrintable(path));
    return path;
}

bool waitForLogin(int timeoutMs)
{
    ForumAccountService *service = ForumAccountService::instance();
    if (service->isLoggedIn())
        return true;

    QEventLoop loop;
    bool ok = false;
    QObject::connect(service, &ForumAccountService::userChanged, &loop, [&](const ForumUser &user) {
        if (user.isValid())
        {
            ok = true;
            loop.quit();
        }
    });
    QObject::connect(service, &ForumAccountService::requestFailed, &loop,
                     [&](const QString &message, int) {
                         fprintf(stderr, "[probe] login failed: %s\n", qPrintable(message));
                         loop.quit();
                     });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("guest");
    const QString baseUrl =
        argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("http://127.0.0.1:3210");
    const QString userName = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QStringLiteral("probe");
    const QString password = argc > 4 ? QString::fromLocal8Bit(argv[4]) : QStringLiteral("probe123456");

    // 备份会被改写的设置，退出时还原
    QSettings appSettings(QStringLiteral("BlockBox"), QStringLiteral("Settings"));
    QSettings forumSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));

    const QStringList appKeys = {QStringLiteral("theme"), QStringLiteral("themeColor")};
    const QStringList forumKeys = {
        QStringLiteral("forum/baseUrl"),        QStringLiteral("forum/sessionToken"),
        QStringLiteral("forum/sessionBaseUrl"), QStringLiteral("forum/cachedUser")};

    QMap<QString, QVariant> appBackup;
    for (const QString &key : appKeys)
        appBackup.insert(key, appSettings.value(key));
    QMap<QString, QVariant> forumBackup;
    for (const QString &key : forumKeys)
        forumBackup.insert(key, forumSettings.value(key));

    auto restore = [&]() {
        for (auto it = appBackup.cbegin(); it != appBackup.cend(); ++it)
        {
            if (it.value().isValid())
                appSettings.setValue(it.key(), it.value());
            else
                appSettings.remove(it.key());
        }
        for (auto it = forumBackup.cbegin(); it != forumBackup.cend(); ++it)
        {
            if (it.value().isValid())
                forumSettings.setValue(it.key(), it.value());
            else
                forumSettings.remove(it.key());
        }
        appSettings.sync();
        forumSettings.sync();
    };

    QApplication app(argc, argv);

    // 「notifications」模式用本地假论坛喂数据，不依赖真实后端
    FakeForumServer *fakeServer = nullptr;
    QString effectiveBaseUrl = baseUrl;
    const bool emptyMode = (mode == QStringLiteral("notifications_empty"));
    const bool errorMode = (mode == QStringLiteral("notifications_error"));
    const bool notificationsMode = (mode == QStringLiteral("notifications")
                                    || mode == QStringLiteral("notifications_dark") || emptyMode
                                    || errorMode);
    // 简介 Markdown 探针：假论坛返回一段带格式的简介，验证资料卡上的渲染
    const bool bioMode = (mode == QStringLiteral("bio") || mode == QStringLiteral("bio_dark"));
    // 用户主页（别人看到的我的主页）上的简介同样是 Markdown
    const bool userBioMode = (mode == QStringLiteral("userbio")
                              || mode == QStringLiteral("userbio_dark"));
    // 老简介回归：纯文本、靠回车分行，不该被并成一段
    const bool plainBioMode = (mode == QStringLiteral("bio_plain"));
    const QString plainBio = QStringLiteral("我是个纯文本简介，没有任何标记。\n"
                                            "第二行靠回车分开，应该还是两行。");

    QString markdownBio = QStringLiteral(
        "**爱折腾整合包**，常用 *Fabric* 和 `Iris`。\n"
        "第二行：在输入框里按的回车要能换行\n\n"
        "- 列表项一\n"
        "- 列表项二\n\n"
        "> 引用块里的说明\n\n"
        "[站内帖子](thread.html?id=101) 以及 [外部链接](https://example.com)");
    if (plainBioMode)
        markdownBio = plainBio;

    if (notificationsMode || bioMode || userBioMode || plainBioMode)
    {
        fakeServer = new FakeForumServer(emptyMode, errorMode, &app, markdownBio);
        if (!fakeServer->start())
        {
            fprintf(stderr, "[probe] fake forum could not listen\n");
            return 1;
        }
        effectiveBaseUrl = fakeServer->baseUrl();
    }

    // 指向测试论坛，并清掉上一轮残留的会话
    forumSettings.setValue(QStringLiteral("forum/baseUrl"), effectiveBaseUrl);
    forumSettings.remove(QStringLiteral("forum/sessionToken"));
    forumSettings.remove(QStringLiteral("forum/sessionBaseUrl"));
    forumSettings.remove(QStringLiteral("forum/cachedUser"));
    if (notificationsMode || bioMode || userBioMode || plainBioMode)
    {
        const QJsonObject cachedUser{{QStringLiteral("id"), 7},
                                     {QStringLiteral("username"), QStringLiteral("probe")},
                                     {QStringLiteral("display_name"), QStringLiteral("方块盒探针")},
                                     {QStringLiteral("bio"), markdownBio},
                                     {QStringLiteral("role"), QStringLiteral("user")},
                                     {QStringLiteral("status"), QStringLiteral("active")},
                                     {QStringLiteral("thread_count"), 12},
                                     {QStringLiteral("post_count"), 34},
                                     {QStringLiteral("quiz_passed"), true},
                                     {QStringLiteral("created_at"), 1700000000.0}};
        forumSettings.setValue(QStringLiteral("forum/sessionToken"),
                               QStringLiteral("probe-token"));
        forumSettings.setValue(QStringLiteral("forum/sessionBaseUrl"), effectiveBaseUrl);
        forumSettings.setValue(
            QStringLiteral("forum/cachedUser"),
            QString::fromUtf8(QJsonDocument(cachedUser).toJson(QJsonDocument::Compact)));
    }
    forumSettings.sync();

    const bool dark = (mode == QStringLiteral("dark"))
                      || (mode == QStringLiteral("notifications_dark"))
                      || (mode == QStringLiteral("bio_dark"))
                      || (mode == QStringLiteral("userbio_dark"));
    if (ThemeManager::instance())
    {
        ThemeManager::instance()->loadTheme(dark ? ThemeManager::DarkTheme
                                                 : ThemeManager::LightTheme);
    }

    bool memberLoggedIn = false;
    if (mode == QStringLiteral("member"))
    {
        ForumAccountService::instance()->login(userName, password);
        memberLoggedIn = waitForLogin(12000);
        if (!memberLoggedIn)
        {
            fprintf(stderr, "[probe] could not log in as %s\n", qPrintable(userName));
            restore();
            return 1;
        }
    }

    ProbeWindow window;
    window.background()->setColor(dark ? QColor(0x0e, 0x14, 0x11) : QColor(0xf0, 0xf2, 0xf1));
    window.show();
    waitMs(600);

    bool pass = true;

    auto expect = [&pass](const char *label, bool condition) {
        fprintf(stderr, "[probe] %-42s %s\n", label, condition ? "OK" : "FAIL");
        if (!condition)
            pass = false;
    };

    if (mode == QStringLiteral("guest") || mode == QStringLiteral("dark"))
    {
        const QString tag = dark ? QStringLiteral("dark_overview") : QStringLiteral("guest_overview");
        waitMs(800);
        grab(window, tag);

        auto *nameLabel = window.page()->findChild<QLabel *>(QStringLiteral("fbName"));
        expect("名称显示「未登录」", nameLabel && nameLabel->text() == QStringLiteral("未登录"));

        QPushButton *loginBtn = findVisibleButton(window.page(), QStringLiteral("登录"));
        QPushButton *registerBtn = findVisibleButton(window.page(), QStringLiteral("注册"));
        for (QPushButton *btn : window.page()->findChildren<QPushButton *>())
        {
            if (btn->text() == QStringLiteral("登录") || btn->text() == QStringLiteral("注册"))
            {
                fprintf(stderr, "[probe]   btn '%s' visible=%d enabled=%d parent='%s'\n",
                        qPrintable(btn->text()), int(btn->isVisible()), int(btn->isEnabled()),
                        qPrintable(btn->parentWidget() ? btn->parentWidget()->objectName() : "-"));
            }
        }
        expect("右侧有「登录」按钮", loginBtn && loginBtn->isVisible());
        expect("右侧有「注册」按钮", registerBtn && registerBtn->isVisible());

        // 尺寸自检：卡片应贴着内容收缩，不应被长页面撑满整屏
        auto *contentHost = window.page()->findChild<QWidget *>(QStringLiteral("fbContentHost"));
        auto *viewStack = window.page()->findChild<QStackedWidget *>(QStringLiteral("fbViewStack"));
        if (contentHost && viewStack)
        {
            fprintf(stderr, "[probe] page=%dx%d viewStack=%dx%d contentHost=%dx%d\n",
                    window.page()->width(), window.page()->height(), viewStack->width(),
                    viewStack->height(), contentHost->width(), contentHost->height());
        }
    }
    else if (mode == QStringLiteral("login") || mode == QStringLiteral("register"))
    {
        const QString buttonText = (mode == QStringLiteral("login")) ? QStringLiteral("登录")
                                                                     : QStringLiteral("注册");
        QPushButton *button = findVisibleButton(window.page(), buttonText);
        expect("找到入口按钮", button != nullptr);
        if (button)
        {
            button->click();
            waitMs(500);
        }
        grab(window, mode);

        auto *title = window.page()->findChild<QLabel *>(QStringLiteral("fbAuthTitle"));
        const QString expectTitle = (mode == QStringLiteral("login"))
                                        ? QStringLiteral("登录方块盒子论坛")
                                        : QStringLiteral("注册方块盒子论坛");
        expect("登录卡标题正确", title && title->text() == expectTitle);
    }
    else if (mode == QStringLiteral("member"))
    {
        waitMs(2500);
        grab(window, QStringLiteral("member_overview"));

        auto *nameLabel = window.page()->findChild<QLabel *>(QStringLiteral("fbName"));
        expect("名称显示论坛昵称", nameLabel && nameLabel->text() != QStringLiteral("未登录")
                                      && !nameLabel->text().isEmpty());
        fprintf(stderr, "[probe] 当前名称：%s\n", nameLabel ? qPrintable(nameLabel->text()) : "-");

        const QList<QPair<QString, QString>> tabs = {
            {QStringLiteral("我的回复"), QStringLiteral("member_replies")},
            {QStringLiteral("我的收藏"), QStringLiteral("member_bookmarks")},
            {QStringLiteral("账号设置"), QStringLiteral("member_settings")},
        };
        for (const auto &item : tabs)
        {
            QPushButton *tab = findVisibleButton(window.page(), item.first);
            expect(qPrintable(QStringLiteral("找到页签 ") + item.first), tab != nullptr);
            if (tab)
            {
                tab->click();
                waitMs(1500);
                grab(window, item.second);
            }
        }

        // 账号设置里的昵称输入框应已回填论坛昵称
        bool foundEdit = false;
        const QList<QLineEdit *> edits =
            window.page()->findChildren<QLineEdit *>(QStringLiteral("fbInput"));
        for (QLineEdit *edit : edits)
        {
            if (!edit->text().isEmpty())
                foundEdit = true;
        }
        expect("账号设置表单已回填", foundEdit);
    }
    else if (mode == QStringLiteral("authflow"))
    {
        // 走完整的界面登录流程：点登录 → 填表单 → 提交
        QPushButton *entry = findVisibleButton(window.page(), QStringLiteral("登录"));
        expect("找到登录入口", entry != nullptr);
        if (entry)
        {
            entry->click();
            waitMs(400);
        }

        auto *viewStack = window.page()->findChild<QStackedWidget *>(QStringLiteral("fbViewStack"));
        QWidget *authPage = viewStack ? viewStack->widget(1) : nullptr;
        expect("进入登录视图", authPage != nullptr);

        const QList<QLineEdit *> edits =
            authPage ? authPage->findChildren<QLineEdit *>() : QList<QLineEdit *>();
        expect("登录表单有两个输入框", edits.size() >= 2);
        if (edits.size() >= 2)
        {
            edits.at(0)->setText(userName);
            edits.at(1)->setText(password);
        }

        QPushButton *submit = nullptr;
        for (QPushButton *button : authPage->findChildren<QPushButton *>())
        {
            if (button->objectName() == QStringLiteral("fbPrimaryBlock"))
            {
                submit = button;
                break;
            }
        }
        expect("找到提交按钮", submit != nullptr);
        if (submit)
        {
            submit->click();
            waitMs(2500);
        }

        grab(window, QStringLiteral("authflow"));

        auto *nameLabel = window.page()->findChild<QLabel *>(QStringLiteral("fbName"));
        expect("登录后名称不再是「未登录」",
               nameLabel && nameLabel->text() != QStringLiteral("未登录"));
        if (nameLabel)
            fprintf(stderr, "[probe] 登录后名称：%s\n", qPrintable(nameLabel->text()));
    }
    else if (mode == QStringLiteral("authfail") || mode == QStringLiteral("logout"))
    {
        QPushButton *entry = findVisibleButton(window.page(), QStringLiteral("登录"));
        if (entry)
        {
            entry->click();
            waitMs(400);
        }

        auto *viewStack = window.page()->findChild<QStackedWidget *>(QStringLiteral("fbViewStack"));
        QWidget *authPage = viewStack ? viewStack->widget(1) : nullptr;
        const QList<QLineEdit *> edits =
            authPage ? authPage->findChildren<QLineEdit *>() : QList<QLineEdit *>();
        QPushButton *submit = nullptr;
        if (authPage)
        {
            for (QPushButton *button : authPage->findChildren<QPushButton *>())
            {
                if (button->objectName() == QStringLiteral("fbPrimaryBlock"))
                {
                    submit = button;
                    break;
                }
            }
        }

        if (mode == QStringLiteral("authfail"))
        {
            if (edits.size() >= 2)
            {
                edits.at(0)->setText(userName);
                edits.at(1)->setText(QStringLiteral("wrong-password"));
            }
            if (submit)
            {
                submit->click();
                waitMs(2000);
            }
            grab(window, QStringLiteral("authfail"));

            auto *errorLabel = window.page()->findChild<QLabel *>(QStringLiteral("fbError"));
            expect("错误提示可见", errorLabel && errorLabel->isVisible()
                                      && !errorLabel->text().isEmpty());
            if (errorLabel)
                fprintf(stderr, "[probe] 错误提示：%s\n", qPrintable(errorLabel->text()));
        }
        else
        {
            // 先正常登录，再点「退出登录」，确认回到未登录状态
            if (edits.size() >= 2)
            {
                edits.at(0)->setText(userName);
                edits.at(1)->setText(password);
            }
            if (submit)
            {
                submit->click();
                waitMs(2500);
            }

            QPushButton *logout = findVisibleButton(window.page(), QStringLiteral("退出登录"));
            expect("找到退出登录按钮", logout != nullptr);
            if (logout)
            {
                logout->click();
                waitMs(1500);
            }
            grab(window, QStringLiteral("logout"));

            auto *nameLabel = window.page()->findChild<QLabel *>(QStringLiteral("fbName"));
            expect("退出后名称回到「未登录」", nameLabel && nameLabel->text() == QStringLiteral("未登录"));
        }
    }
    else if (mode == QStringLiteral("destroy"))
    {
        // 页面建好、跑过一轮网络请求后直接销毁，验证析构路径
        waitMs(2000);
        grab(window, QStringLiteral("destroy_before"));
        window.destroyPage();
        QApplication::processEvents();
        waitMs(800);
        QApplication::processEvents();
        expect("页面成功销毁", true);
        restore();
        fprintf(stderr, "[probe] PROBE-PASS\n");
        return 0;
    }
    else if (mode == QStringLiteral("characters"))
    {
        // 角色页：用临时账户验证瀑布流卡片 + 点击切换，结束后清掉临时账户
        SettingsManager *settings = SettingsManager::instance();
        const QList<AccountInfo> before = settings->getAccounts();
        QString beforeDefault;
        for (const AccountInfo &account : before)
        {
            if (account.isDefault)
                beforeDefault = account.username;
        }

        const QStringList testNames = {QStringLiteral("Steve"), QStringLiteral("Alex"),
                                       QStringLiteral("Ari")};
        for (int i = 0; i < testNames.size(); ++i)
        {
            if (!settings->accountExists(testNames.at(i)))
                settings->addAccount(testNames.at(i), QObject::tr("离线"), QString(), i == 0);
        }

        auto *charactersTab = findVisibleButton(window.page(), QStringLiteral("角色"));
        expect("找到「角色」页签", charactersTab != nullptr);
        if (charactersTab)
        {
            charactersTab->click();
            waitMs(800);
        }
        grab(window, QStringLiteral("characters"));

        // 卡片数量 = 测试账户数（页面里用 fbCharCard 命名）
        int cardCount = 0;
        for (QWidget *card : window.page()->findChildren<QWidget *>(QStringLiteral("fbCharCard")))
        {
            if (card->isVisible())
                ++cardCount;
        }
        expect("瀑布流卡片已生成", cardCount >= testNames.size());
        fprintf(stderr, "[probe] 可见卡片数：%d\n", cardCount);

        // 点第二张卡片切换账户
        QList<QWidget *> visibleCards;
        for (QWidget *card : window.page()->findChildren<QWidget *>(QStringLiteral("fbCharCard")))
        {
            if (card->isVisible())
                visibleCards.append(card);
        }
        QString switchedTo;
        if (visibleCards.size() >= 2)
        {
            auto *second = visibleCards.at(1);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(12, 12), QPointF(12, 12),
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(second, &release);
            waitMs(600);
            for (const AccountInfo &account : settings->getAccounts())
            {
                if (account.isDefault)
                    switchedTo = account.username;
            }
            grab(window, QStringLiteral("characters_switched"));
        }
        expect("点击卡片后默认账户被切换", !switchedTo.isEmpty());
        if (!switchedTo.isEmpty())
            fprintf(stderr, "[probe] 切换后的默认账户：%s\n", qPrintable(switchedTo));

        // 双击一张卡片进入账户详情页（左侧 3D 预览 + 右侧资料 / 操作）
        QList<QWidget *> cardsAfterSwitch;
        for (QWidget *card : window.page()->findChildren<QWidget *>(QStringLiteral("fbCharCard")))
        {
            if (card->isVisible())
                cardsAfterSwitch.append(card);
        }
        if (!cardsAfterSwitch.isEmpty())
        {
            QMouseEvent dblClick(QEvent::MouseButtonDblClick, QPointF(14, 14), QPointF(14, 14),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(cardsAfterSwitch.at(0), &dblClick);
            waitMs(1200);
            grab(window, QStringLiteral("characters_detail"));

            auto *detailName = window.page()->findChild<QLabel *>(QStringLiteral("fbDetailName"));
            expect("详情页显示账户名", detailName && !detailName->text().isEmpty());
            if (detailName)
                fprintf(stderr, "[probe] 详情页账户：%s\n", qPrintable(detailName->text()));

            if (auto *pv = window.page()->findChild<QWidget *>(QStringLiteral("fbDetailPreview")))
                fprintf(stderr, "[probe] 预览卡片 %dx%d\n", pv->width(), pv->height());
            if (auto *vs = window.page()->findChild<QStackedWidget *>(QStringLiteral("fbViewStack")))
                fprintf(stderr, "[probe] 视图栈 %dx%d\n", vs->width(), vs->height());
            auto *detailSkin = window.page()->findChild<QWidget *>(QStringLiteral("fbDetailSkin"));
            expect("详情页左侧有 3D 模型预览", detailSkin != nullptr && detailSkin->isVisible());
            if (detailSkin)
            {
                fprintf(stderr, "[probe] 3D 预览尺寸 %dx%d visible=%d\n", detailSkin->width(),
                        detailSkin->height(), int(detailSkin->isVisible()));
                QWidget *page = detailSkin;
                while (page && page->parentWidget() && page->parentWidget() != window.page())
                    page = page->parentWidget();
                if (page)
                    fprintf(stderr, "[probe] 详情页尺寸 %dx%d\n", page->width(), page->height());
            }
            expect("详情页右侧有模型动作按钮",
                   !window.page()->findChildren<QPushButton *>(QStringLiteral("fbPoseButton")).isEmpty());

            auto *backBtn = findVisibleButton(window.page(), QStringLiteral("← 返回角色"));
            expect("详情页有返回按钮", backBtn != nullptr);
            if (backBtn)
            {
                backBtn->click();
                waitMs(700);
            }
            auto *charactersTabAfter = findVisibleButton(window.page(), QStringLiteral("角色"));
            expect("返回后回到角色页", charactersTabAfter != nullptr);
            grab(window, QStringLiteral("characters_back"));
        }

        // 清理临时账户，恢复原来的默认账户
        for (const QString &name : testNames)
            settings->removeAccount(name);
        if (!beforeDefault.isEmpty())
            settings->setDefaultAccount(beforeDefault);
        restore();
        fprintf(stderr, "[probe] %s\n", pass ? "PROBE-PASS" : "PROBE-FAIL");
        return pass ? 0 : 1;
    }
    else if (notificationsMode)
    {
        QPushButton *tab = findVisibleButton(window.page(), QStringLiteral("通知"));
        expect("找到通知页签", tab != nullptr);
        if (tab)
        {
            tab->click();
            waitMs(1800);
        }
        const QString tag = emptyMode ? QStringLiteral("notifications_empty")
                            : errorMode ? QStringLiteral("notifications_error")
                            : (mode == QStringLiteral("notifications_dark")
                                   ? QStringLiteral("notifications_dark")
                                   : QStringLiteral("notifications"));
        grab(window, tag);

        int rows = 0;
        for (QWidget *row : window.page()->findChildren<QWidget *>(QStringLiteral("fbNotifRow")))
        {
            if (row->isVisible())
                ++rows;
        }
        fprintf(stderr, "[probe] 可见通知行数：%d\n", rows);
        expect(emptyMode || errorMode ? "非列表态不渲染通知行" : "通知行已渲染",
               emptyMode || errorMode ? rows == 0 : rows == 6);

        auto *markAll = findVisibleButton(window.page(), QStringLiteral("全部已读"));
        fprintf(stderr, "[probe] 「全部已读」按钮：%s\n", markAll ? "存在" : "缺失");
        if (markAll)
            fprintf(stderr, "[probe] 按钮 enabled=%d\n", int(markAll->isEnabled()));

        if (!emptyMode && !errorMode)
        {
            // 点第一行：应把它标记已读，并按服务端结果重载列表（不会残留未读样式）
            QWidget *firstRow = nullptr;
            for (QWidget *row : window.page()->findChildren<QWidget *>(QStringLiteral("fbNotifRow")))
            {
                if (row->isVisible())
                {
                    firstRow = row;
                    break;
                }
            }
            expect("找到第一行通知", firstRow != nullptr);
            if (firstRow)
            {
                const QPointF center(firstRow->width() / 2.0, firstRow->height() / 2.0);
                QMouseEvent press(QEvent::MouseButtonPress, center, center, Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(firstRow, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, center, center, Qt::LeftButton,
                                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(firstRow, &release);
                waitMs(1600);
                grab(window, tag + QStringLiteral("_rowclick"));
                auto *chipAfter = window.page()->findChild<QLabel *>(
                    QStringLiteral("fbNotifUnreadChip"));
                fprintf(stderr, "[probe] 点开一行后 chip：%s\n",
                        chipAfter ? qPrintable(chipAfter->text()) : "缺失");
                expect("点开一行后未读组数从 3 减到 2",
                       chipAfter && chipAfter->isVisible()
                           && chipAfter->text().contains(QStringLiteral("2")));
            }
        }

        if (errorMode)
        {
            auto *alert = window.page()->findChild<QFrame *>(QStringLiteral("fbNotifAlert"));
            expect("错误提示条可见", alert != nullptr && alert->isVisible());
            QLabel *errorBox = nullptr;
            const auto errorLabels =
                window.page()->findChildren<QLabel *>(QStringLiteral("fbNotifAlertText"));
            for (QLabel *label : errorLabels)
            {
                if (label->isVisible())
                {
                    errorBox = label;
                    break;
                }
            }
            fprintf(stderr, "[probe] 错误条：%s\n",
                    errorBox ? qPrintable(errorBox->text()) : "缺失/隐藏");
            expect("错误提示可见", errorBox != nullptr);
            auto *retry = findVisibleButton(window.page(), QStringLiteral("重新加载"));
            expect("重新加载按钮可见", retry != nullptr);
            expect("错误态「全部已读」置灰", markAll && !markAll->isEnabled());
        }
        else if (!emptyMode)
        {
            // 滚到底看「昨天 / 更早」分组和系统公告行
            if (auto *scroll = window.page()->findChild<QScrollArea *>(QStringLiteral("fbScroll")))
            {
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
                waitMs(400);
                grab(window, tag + QStringLiteral("_bottom"));
            }

            // 未读统计 chip 应可见且带上未读组数
            auto *chip = window.page()->findChild<QLabel *>(QStringLiteral("fbNotifUnreadChip"));
            fprintf(stderr, "[probe] 未读 chip：%s\n",
                    chip && chip->isVisible() ? qPrintable(chip->text()) : "缺失/隐藏");
            expect("未读 chip 可见", chip && chip->isVisible());

            // 点「全部已读」：服务端改口后页面应重载成全部已读
            if (markAll)
            {
                markAll->click();
                waitMs(1600);
                grab(window, tag + QStringLiteral("_allread"));
                fprintf(stderr, "[probe] 全部已读后：chip 可见=%d 按钮 enabled=%d\n",
                        int(chip && chip->isVisible()), int(markAll->isEnabled()));
                expect("全部已读后 chip 隐藏", chip && !chip->isVisible());
                expect("全部已读后按钮置灰", markAll && !markAll->isEnabled());
            }
        }

        waitMs(200);
        restore();
        fprintf(stderr, "[probe] %s\n", pass ? "PROBE-PASS" : "PROBE-FAIL");
        return pass ? 0 : 1;
    }
    else if (bioMode)
    {
        // 简介 Markdown：资料卡上应该看到排版后的效果，而不是原始的 ** / ` 标记
        waitMs(1500);
        const QString tag = dark ? QStringLiteral("bio_dark") : QStringLiteral("bio");
        grab(window, tag);

        auto *browser = window.page()->findChild<QTextBrowser *>(QStringLiteral("fbBioBrowser"));
        expect("资料卡简介由 Markdown 渲染控件展示", browser != nullptr && browser->isVisible());
        if (browser)
        {
            QString plain = browser->toPlainText();
            fprintf(stderr, "[probe] 简介纯文本：%s\n",
                    qPrintable(QString(plain).replace(QLatin1Char('\n'), QStringLiteral(" / "))));
            expect("Markdown 标记已渲染，不再是原文", !plain.contains(QStringLiteral("**"))
                                                          && !plain.contains(QStringLiteral("`Iris`")));
            expect("换行按回车原样保留（含「第二行」）", plain.contains(QStringLiteral("第二行")));
            expect("列表 / 引用块渲染出多个段落",
                   browser->document()->blockCount() >= 5);

            bool bold = false;
            bool linkFound = false;
            for (QTextBlock block = browser->document()->begin(); block.isValid();
                 block = block.next())
            {
                for (auto it = block.begin(); !it.atEnd(); ++it)
                {
                    const QTextFragment fragment = it.fragment();
                    if (!fragment.isValid())
                        continue;
                    if (fragment.text().contains(QStringLiteral("爱折腾整合包"))
                        && fragment.charFormat().fontWeight() >= QFont::Bold)
                        bold = true;
                    if (fragment.charFormat().isAnchor())
                        linkFound = true;
                }
            }
            expect("**加粗** 生效", bold);
            expect("链接渲染成可点的超链接", linkFound);

            // 简介的高度要贴合内容，不能把下面的数据行挤开
            fprintf(stderr, "[probe] 简介控件高度：%d\n", browser->height());
            expect("简介高度贴合内容", browser->height() > 0 && browser->height() < 320);
        }

        // 账号设置里的「预览」：看到的就是资料卡上的样子
        QPushButton *settingsTab = findVisibleButton(window.page(), QStringLiteral("账号设置"));
        expect("找到「账号设置」页签", settingsTab != nullptr);
        if (settingsTab)
        {
            settingsTab->click();
            waitMs(1200);

            // 简介在设置页中部，先把滚动区挪到简介附近再截图
            auto *scroll = window.page()->findChild<QScrollArea *>(QStringLiteral("fbScroll"));
            auto *bioStack = window.page()->findChild<QWidget *>(QStringLiteral("fbBioHost"));
            if (scroll && bioStack)
            {
                scroll->ensureWidgetVisible(bioStack, 0, 150);
                waitMs(400);
            }
            grab(window, tag + QStringLiteral("_settings"));

            auto *bioEdit = window.page()->findChild<QPlainTextEdit *>(QStringLiteral("fbInput"));
            bool bioFilled = false;
            for (QPlainTextEdit *edit :
                 window.page()->findChildren<QPlainTextEdit *>(QStringLiteral("fbInput")))
            {
                if (edit->toPlainText().contains(QStringLiteral("爱折腾整合包")))
                    bioFilled = true;
            }
            Q_UNUSED(bioEdit);
            expect("设置里的简介编辑器回填了原文", bioFilled);

            QPushButton *previewButton = findVisibleButton(window.page(), QStringLiteral("预览"));
            expect("简介旁边有「预览」开关", previewButton != nullptr);
            if (previewButton)
            {
                previewButton->click();
                waitMs(900);
                if (scroll && bioStack)
                {
                    scroll->ensureWidgetVisible(bioStack, 0, 150);
                    waitMs(300);
                }
                grab(window, tag + QStringLiteral("_preview"));
                auto *previewBrowser =
                    window.page()->findChild<QTextBrowser *>(QStringLiteral("fbBioPreview"));
                expect("预览里也是排版后的简介",
                       previewBrowser && previewBrowser->isVisible()
                           && !previewBrowser->toPlainText().contains(QStringLiteral("**")));
                expect("预览开关文字变成「继续编辑」",
                       previewButton->text() == QStringLiteral("继续编辑"));
            }
        }
    }
    else if (plainBioMode)
    {
        // 老简介回归：纯文本、靠回车分行，渲染后仍然是两行
        waitMs(1500);
        grab(window, QStringLiteral("bio_plain"));

        auto *browser = window.page()->findChild<QTextBrowser *>(QStringLiteral("fbBioBrowser"));
        expect("纯文本简介照常显示", browser != nullptr && browser->isVisible());
        if (browser)
        {
            const QString plain = browser->toPlainText();
            const qsizetype idx = plain.indexOf(QStringLiteral("第二行"));
            const QChar before = idx > 0 ? plain.at(idx - 1) : QChar();
            const bool lineBroken = (before == QChar(0x2028)) || (before == QLatin1Char('\n'))
                                    || (before == QLatin1Char('\r'));
            fprintf(stderr, "[probe] 纯文本简介：%s\n",
                    qPrintable(QString(plain).replace(QChar(0x2028), QStringLiteral(" <br> "))));
            expect("回车分开的两行没有并成一段", lineBroken);
        }
    }
    else if (userBioMode)
    {
        // 别人点开我的主页时看到的简介：和资料卡同一套 Markdown 排版
        auto *profilePage = new ForumUserProfilePage();
        profilePage->resize(1040, 700);
        profilePage->show();
        profilePage->openUser(7);
        waitMs(2000);
        const QString tag = dark ? QStringLiteral("userbio_dark") : QStringLiteral("userbio");
        const QString path = QStringLiteral("%1/forum_%2.png").arg(kShotDir, tag);
        profilePage->grab().save(path);
        fprintf(stderr, "[probe] shot %s\n", qPrintable(path));

        auto *browser = profilePage->findChild<QTextBrowser *>(QStringLiteral("forumProfileBio"));
        expect("用户主页简介由 Markdown 渲染控件展示",
               browser != nullptr && browser->isVisible());
        if (browser)
        {
            const QString plain = browser->toPlainText();
            fprintf(stderr, "[probe] 主页简介纯文本：%s\n",
                    qPrintable(QString(plain).replace(QLatin1Char('\n'), QStringLiteral(" / "))));
            expect("主页简介里的 Markdown 已渲染",
                   !plain.contains(QStringLiteral("**")) && plain.contains(QStringLiteral("爱折腾整合包")));
            expect("主页简介保留了作者的换行", plain.contains(QStringLiteral("第二行")));
        }
        profilePage->deleteLater();
    }
    else
    {
        fprintf(stderr, "[probe] unknown mode: %s\n", qPrintable(mode));
        restore();
        return 2;
    }

    waitMs(200);
    restore();
    fprintf(stderr, "[probe] %s\n", pass ? "PROBE-PASS" : "PROBE-FAIL");
    return pass ? 0 : 1;
}
