/**
 * @file   server_card_probe.cpp
 * @brief  服务器卡片信息显示探针：造一个假实例 + servers.json，跑真实
 *         ServerManagePage，等状态探测回来后截图并打印每张卡片上的文案。
 *
 * 判定：卡片状态徽标从「检测中」变为「在线/离线」，在线卡片出现人数/版本/延迟
 * 与服务器图标，离线卡片保持默认图标且信息行留空。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "pages/ServerManagePage.h"

#include <cstdio>

namespace
{

void stderrMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    const char *tag = "[info ]";
    if (type == QtWarningMsg)
        tag = "[warn ]";
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        tag = "[crit ]";
    fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    fflush(stderr);
}

QString labelText(QWidget *card, const char *name)
{
    QLabel *l = card->findChild<QLabel *>(QString::fromLatin1(name));
    return l ? l->text() : QStringLiteral("<missing>");
}

void dumpCards(ServerManagePage *page, const char *tag)
{
    qInfo().noquote() << QString("== [%1] 卡片快照 ==").arg(QString::fromLatin1(tag));
    const QList<QWidget *> cards = page->findChildren<QWidget *>();
    int idx = 0;
    for (QWidget *card : cards)
    {
        if (card->objectName() != QLatin1String("resourceCard"))
            continue;
        QLabel *icon = card->findChild<QLabel *>(QStringLiteral("serverCardIcon"));
        qInfo().noquote() << QString("  #%1 key=%2  size=%3x%4")
                                 .arg(idx++)
                                 .arg(card->property("serverKey").toString())
                                 .arg(card->width()).arg(card->height());
        const QPixmap shot = icon ? icon->pixmap() : QPixmap();
        qInfo().noquote() << QString("     status=\"%1\"  info=\"%2\"  motd=\"%3\"  icon=%4x%5")
                                 .arg(labelText(card, "serverCardStatus"))
                                 .arg(labelText(card, "serverCardInfo"))
                                 .arg(labelText(card, "serverCardMotd"))
                                 .arg(shot.isNull() ? -1 : shot.width())
                                 .arg(shot.isNull() ? -1 : shot.height());
    }
}

void shoot(ServerManagePage *page, const QString &name)
{
    const bool ok = page->grab().save(name);
    qInfo().noquote() << "[shot]" << name << (ok ? "ok" : "FAILED");
}

}   // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    // 假实例目录：只放 servers.json（描述字段用来验证离线回退）
    const QString root = QDir::currentPath() + QStringLiteral("/probe_out");
    QDir().mkpath(root);
    const QString instanceDir = root + QStringLiteral("/server_card_probe_instance");
    QDir().mkpath(instanceDir);

    QJsonArray servers;
    auto addServer = [&servers](const QString &name, const QString &addr, int port,
                                const QString &desc) {
        QJsonObject o;
        o[QStringLiteral("name")] = name;
        o[QStringLiteral("address")] = addr;
        o[QStringLiteral("port")] = port;
        o[QStringLiteral("description")] = desc;
        servers.append(o);
    };
    addServer(QStringLiteral("Hypixel"), QStringLiteral("mc.hypixel.net"), 25565,
              QStringLiteral("全服最大的小游戏服，晚上人多"));
    addServer(QStringLiteral("CubeCraft"), QStringLiteral("play.cubecraft.net"), 25565,
              QStringLiteral("小游戏 / 床战"));
    addServer(QStringLiteral("已失效服务器"), QStringLiteral("mc.mineville.org"), 25565,
              QStringLiteral("这个域名已经挂了，用来验证离线样式"));
    addServer(QStringLiteral("本地端口服"), QStringLiteral("127.0.0.1"), 25566, QString());

    QFile file(instanceDir + QStringLiteral("/servers.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qCritical() << "PROBE-FAIL: cannot write servers.json";
        return 1;
    }
    file.write(QJsonDocument(servers).toJson(QJsonDocument::Indented));
    file.close();

    // 借真实 MainWindow 初始化主题与全局样式表
    MainWindow mainWindow;
    mainWindow.resize(1200, 800);
    mainWindow.show();
    QApplication::processEvents();

    ServerManagePage page;
    page.setInstancePath(instanceDir);
    page.resize(1080, 620);
    page.show();
    QApplication::processEvents();

    QTimer::singleShot(1200, &app, [&page]() {
        dumpCards(&page, "刚进页面");
    });

    QTimer::singleShot(7000, &app, [&page]() {
        dumpCards(&page, "探测返回后");
        shoot(&page, QStringLiteral("server_card_probe_online.png"));
    });

    // 失效域名要走 DRV/SRV 兜底，给足超时时间
    QTimer::singleShot(16000, &app, [&app, &page, &mainWindow]() {
        Q_UNUSED(mainWindow)
        dumpCards(&page, "全部超时后");
        shoot(&page, QStringLiteral("server_card_probe_final.png"));
        qInfo() << "PROBE-DONE";
        app.quit();
    });

    return app.exec();
}
