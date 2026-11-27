/**
 * @file   download_page_probe.cpp
 * @brief  「更多 > 多线程下载」探针：下载引擎功能 + 页面接线 + 截图
 *
 * 覆盖：
 *   1. 分段下载：本地迷你 HTTP 服务（支持 Range）→ 校验分段数、进度与文件内容一致；
 *   2. 不支持分段的服务器（忽略 Range 直接 200）→ 自动退回单连接且文件正确；
 *   3. 暂停 / 继续：中途暂停后接着下，最终文件仍然完整；
 *   4. 页面接线：点「更多」页的「多线程下载」卡片能进页面、贴直链点「开始下载」能下到文件。
 *
 * 只在本机 127.0.0.1 上跑一个临时 HTTP 服务，不访问外网。
 */

#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

#include <cstdio>
#include <functional>

#include "mainwindow.h"
#include "components/SideBar.h"
#include "components/SubNavPanel.h"
#include "pages/MorePage.h"
#include "pages/MultiThreadDownloadPage.h"
#include "utils/SegmentedDownloader.h"
#include "utils/ThemeManager.h"

namespace {

void stderrMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    const char *tag = "[info ]";
    if (type == QtWarningMsg) tag = "[warn ]";
    else if (type == QtCriticalMsg || type == QtFatalMsg) tag = "[crit ]";
    fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    fflush(stderr);
}

void pump(QWidget *w = nullptr, int ms = 100)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (w) w->repaint();
    }
}

bool waitUntil(const std::function<bool()> &pred, int timeoutMs, QWidget *w = nullptr)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        if (pred()) return true;
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (w) w->repaint();
    }
    return pred();
}

bool grab(QWidget *w, const QString &path)
{
    if (!w) return false;
    const bool ok = w->grab().save(path);
    fprintf(stderr, "[%s] %s\n", ok ? "ok" : "FAIL", path.toLocal8Bit().constData());
    fflush(stderr);
    return ok;
}

/** 迷你 HTTP 服务：可切换「支持 / 忽略 Range」，用来验证分段与单连接两条路径 */
class MiniHttpServer
{
public:
    explicit MiniHttpServer(const QByteArray &payload)
        : m_payload(payload)
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this]() {
            while (QTcpSocket *sock = m_server.nextPendingConnection()) {
                QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock]() {
                    QByteArray buf = sock->property("buf").toByteArray();
                    buf.append(sock->readAll());
                    sock->setProperty("buf", buf);

                    const int headerEnd = buf.indexOf("\r\n\r\n");
                    if (headerEnd < 0 || sock->property("done").toBool())
                        return;
                    sock->setProperty("done", true);
                    handleRequest(sock, buf.left(headerEnd));
                });
                QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
    }

    bool start()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return m_server.serverPort(); }
    void setRangeSupported(bool on) { m_range = on; }
    /** 给响应体加一点发送节奏（毫秒 / 64KB），暂停测试要靠它把下载拖慢 */
    void setChunkDelayMs(int ms) { m_chunkDelayMs = ms; }
    QString url() const
    {
        return QStringLiteral("http://127.0.0.1:%1/file.bin").arg(port());
    }
    int requestCount() const { return m_requests; }

private:
    void sendResponse(QTcpSocket *sock, const QByteArray &header, const QByteArray &body,
                      bool isHead)
    {
        sock->write(header);
        if (isHead || body.isEmpty()) {
            sock->flush();
            sock->disconnectFromHost();
            return;
        }

        if (m_chunkDelayMs <= 0) {
            sock->write(body);
            sock->flush();
            sock->disconnectFromHost();
            return;
        }

        // 慢速发送：每 m_chunkDelayMs 毫秒推 64KB，方便在传输中间按暂停
        auto *offset = new qint64(0);
        auto *data = new QByteArray(body);
        auto *timer = new QTimer(sock);
        timer->setInterval(m_chunkDelayMs);
        QObject::connect(timer, &QTimer::timeout, sock, [sock, timer, offset, data]() {
            const qint64 remaining = data->size() - *offset;
            if (remaining > 0) {
                const qint64 n = qMin<qint64>(64 * 1024, remaining);
                sock->write(data->constData() + *offset, n);
                *offset += n;
                sock->flush();
            }
            if (*offset >= data->size()) {
                timer->stop();
                sock->disconnectFromHost();
                delete offset;
                delete data;
            }
        });
        timer->start();
    }

    void handleRequest(QTcpSocket *sock, const QByteArray &head)
    {
        ++m_requests;
        const QList<QByteArray> lines = head.split('\n');
        const bool isHead = lines.value(0).trimmed().startsWith("HEAD ");

        qint64 rangeStart = -1;
        qint64 rangeEnd = -1;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray key = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (key != "range" || !value.startsWith("bytes="))
                continue;
            const QByteArray spec = value.mid(6);
            const int dash = spec.indexOf('-');
            if (dash < 0)
                continue;
            rangeStart = spec.left(dash).toLongLong();
            const QByteArray tail = spec.mid(dash + 1);
            rangeEnd = tail.isEmpty() ? -1 : tail.toLongLong();
        }

        const qint64 total = m_payload.size();
        QByteArray header;
        QByteArray body;

        if (m_range && rangeStart >= 0) {
            const qint64 end = (rangeEnd < 0 || rangeEnd >= total) ? total - 1 : rangeEnd;
            const qint64 length = qMax<qint64>(0, end - rangeStart + 1);
            header = "HTTP/1.1 206 Partial Content\r\n"
                     "Content-Type: application/octet-stream\r\n"
                     "Accept-Ranges: bytes\r\n"
                     "Content-Range: bytes " + QByteArray::number(rangeStart) + "-"
                     + QByteArray::number(end) + "/" + QByteArray::number(total) + "\r\n"
                     "Content-Length: " + QByteArray::number(length) + "\r\n"
                     "Connection: close\r\n\r\n";
            body = m_payload.mid(rangeStart, length);
        } else {
            header = "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/octet-stream\r\n"
                     "Content-Length: " + QByteArray::number(total) + "\r\n"
                     "Connection: close\r\n\r\n";
            body = m_payload;
        }

        sendResponse(sock, header, body, isHead);
    }

    QTcpServer m_server;
    QByteArray m_payload;
    bool m_range = true;
    int m_requests = 0;
    int m_chunkDelayMs = 0;
};

QByteArray makePayload(int bytes)
{
    QByteArray payload(bytes, Qt::Uninitialized);
    for (int i = 0; i < bytes; ++i)
        payload[i] = static_cast<char>((i * 37 + (i >> 5)) & 0xFF);
    return payload;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

bool fileEquals(const QString &path, const QByteArray &expect)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray actual = f.readAll();
    if (actual.size() != expect.size()) {
        fprintf(stderr, "[probe] 大小不一致：实际 %lld / 期望 %lld\n",
                static_cast<long long>(actual.size()),
                static_cast<long long>(expect.size()));
        return false;
    }
    const bool same = actual == expect;
    if (!same) {
        const QByteArray a = QCryptographicHash::hash(actual, QCryptographicHash::Sha1).toHex();
        const QByteArray b = QCryptographicHash::hash(expect, QCryptographicHash::Sha1).toHex();
        fprintf(stderr, "[probe] 内容不一致：实际 sha1=%s / 期望 sha1=%s\n",
                a.constData(), b.constData());
    }
    return same;
}

struct DownloadOutcome
{
    bool completed = false;
    bool failed = false;
    QString error;
    bool rangeSupported = false;
    int segments = 0;
    qint64 total = -1;
};

DownloadOutcome runEngineDownload(SegmentedDownloader &engine, const QUrl &url,
                                  const QString &savePath, int threads, int timeoutMs)
{
    DownloadOutcome out;
    QObject::connect(&engine, &SegmentedDownloader::prepared, &engine,
                     [&out, &engine](qint64 total, bool rangeSupported) {
                         out.total = total;
                         out.rangeSupported = rangeSupported;
                         out.segments = engine.activeSegmentCount();
                     });
    QObject::connect(&engine, &SegmentedDownloader::finished, &engine,
                     [&out, &engine](const QString &) {
                         out.completed = true;
                         out.segments = engine.activeSegmentCount();
                     });
    QObject::connect(&engine, &SegmentedDownloader::failed, &engine,
                     [&out](const QString &message) {
                         out.failed = true;
                         out.error = message;
                     });

    engine.setSegmentCount(threads);
    engine.start(url, savePath);
    waitUntil([&out]() { return out.completed || out.failed; }, timeoutMs);
    return out;
}

QPushButton *navButton(SideBar *bar, int parentIndex)
{
    if (!bar) return nullptr;
    const QList<QPushButton *> buttons =
        bar->findChildren<QPushButton *>(QStringLiteral("parentNavButton"));
    for (QPushButton *b : buttons) {
        if (b && b->property("parentIndex").toInt() == parentIndex)
            return b;
    }
    return nullptr;
}

bool clickCardByTitle(QWidget *page, const QString &title)
{
    if (!page) return false;
    const QList<QLabel *> labels =
        page->findChildren<QLabel *>(QStringLiteral("resourcesCardTitle"));
    for (QLabel *l : labels) {
        if (!l || l->text() != title)
            continue;
        QWidget *card = l->parentWidget() ? l->parentWidget()->parentWidget() : nullptr;
        if (!card)
            continue;
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10),
                          card->mapToGlobal(QPoint(10, 10)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(card, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(10, 10),
                            card->mapToGlobal(QPoint(10, 10)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(card, &release);
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("BlockBox"));

    const QString outDir = QStringLiteral("D:/BlockBox/_out/download_page_probe");
    QDir().mkpath(outDir);

    const QByteArray payload = makePayload(3 * 1024 * 1024);
    MiniHttpServer server(payload);
    if (!server.start()) {
        qCritical() << "PROBE-FAIL: 本地 HTTP 服务起不来";
        return 2;
    }
    fprintf(stderr, "[probe] 本地服务：%s\n", server.url().toLocal8Bit().constData());

    const QString workDir = QDir::tempPath() + QStringLiteral("/bb_download_probe");
    QDir(workDir).removeRecursively();
    QDir().mkpath(workDir);

    int failures = 0;

    // ---- 1. 支持 Range：应当分段并发，文件一致 ----
    {
        const QString path = workDir + QStringLiteral("/segmented.bin");
        SegmentedDownloader engine;
        const DownloadOutcome out = runEngineDownload(
            engine, QUrl(server.url()), path, 4, 30000);
        fprintf(stderr,
                "[probe] 分段：完成=%d 失败=%d 支持分段=%d 分段数=%d 总大小=%lld 请求数=%d\n",
                out.completed ? 1 : 0, out.failed ? 1 : 0, out.rangeSupported ? 1 : 0,
                out.segments, static_cast<long long>(out.total), server.requestCount());
        if (!out.completed || out.rangeSupported != true || out.segments < 2
            || out.total != payload.size() || !fileEquals(path, payload)) {
            qWarning() << "PROBE-FAIL: 分段下载结果不对" << out.error;
            ++failures;
        }
    }

    // ---- 2. 服务器忽略 Range：应自动退回单连接，文件仍然一致 ----
    {
        server.setRangeSupported(false);
        const QString path = workDir + QStringLiteral("/single.bin");
        SegmentedDownloader engine;
        const DownloadOutcome out =
            runEngineDownload(engine, QUrl(server.url()), path, 8, 30000);
        fprintf(stderr, "[probe] 单连接：完成=%d 失败=%d 支持分段=%d 总大小=%lld\n",
                out.completed ? 1 : 0, out.failed ? 1 : 0, out.rangeSupported ? 1 : 0,
                static_cast<long long>(out.total));
        if (!out.completed || out.rangeSupported || !fileEquals(path, payload)) {
            qWarning() << "PROBE-FAIL: 单连接回退结果不对" << out.error;
            ++failures;
        }
        server.setRangeSupported(true);
    }

    // ---- 3. 暂停 / 继续：中途暂停后接着下，文件仍然一致 ----
    {
        const QString path = workDir + QStringLiteral("/paused.bin");
        SegmentedDownloader engine;
        bool completed = false;
        bool failed = false;
        QObject::connect(&engine, &SegmentedDownloader::finished,
                         [&completed](const QString &) { completed = true; });
        QObject::connect(&engine, &SegmentedDownloader::failed,
                         [&failed](const QString &) { failed = true; });

        server.setChunkDelayMs(20);
        engine.setSegmentCount(4);
        engine.start(QUrl(server.url()), path);
        const bool gotRun = waitUntil(
            [&engine, &completed, &failed, &payload]() {
                return completed || failed
                       || engine.receivedBytes() > payload.size() / 3;
            },
            15000);
        const bool paused = engine.state() == SegmentedDownloader::State::Downloading;
        if (paused)
            engine.pause();
        const qint64 receivedAtPause = engine.receivedBytes();
        pump(nullptr, 400);
        const bool reallyPaused = engine.state() == SegmentedDownloader::State::Paused;
        if (paused)
            engine.resume();
        waitUntil([&completed, &failed]() { return completed || failed; }, 30000);

        fprintf(stderr,
                "[probe] 暂停：等到数据=%d 暂停成功=%d 暂停时已收=%lld 完成=%d 失败=%d\n",
                gotRun ? 1 : 0, reallyPaused ? 1 : 0,
                static_cast<long long>(receivedAtPause), completed ? 1 : 0, failed ? 1 : 0);
        if (!completed || !reallyPaused || !fileEquals(path, payload)) {
            qWarning() << "PROBE-FAIL: 暂停 / 继续结果不对";
            ++failures;
        }
        server.setChunkDelayMs(0);
    }

    // ---- 4. 页面接线：更多页卡片 → 页面 → 贴直链点开始 → 文件落盘 ----
    ThemeManager *tm = ThemeManager::instance();
    tm->setTheme(&app, ThemeManager::LightTheme);
    pump(nullptr, 100);

    MainWindow w;
    w.resize(1360, 880);
    w.show();
    pump(&w, 2400);

    SideBar *bar = w.findChild<SideBar *>();
    if (!bar) {
        qCritical() << "PROBE-FAIL: 没找到侧边栏";
        return 3;
    }

    QPushButton *moreBtn = navButton(bar, 7);
    if (!moreBtn) {
        qCritical() << "PROBE-FAIL: 侧边栏 index 7 不是「更多」";
        return 4;
    }
    moreBtn->click();
    pump(&w, 2000);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    pump(&w, 300);
    grab(&w, outDir + QStringLiteral("/10_more_page.png"));

    // 更多页是懒加载的：点完侧边栏才会建出来
    MorePage *morePage = w.findChild<MorePage *>();
    if (!morePage) {
        qCritical() << "PROBE-FAIL: 点了「更多」也没建出更多页";
        return 4;
    }

    if (!clickCardByTitle(morePage, QString::fromUtf8("多线程下载"))) {
        qCritical() << "PROBE-FAIL: 更多页没有「多线程下载」卡片";
        ++failures;
    }
    pump(&w, 1800);

    auto *page = w.findChild<MultiThreadDownloadPage *>();
    if (!page) {
        qCritical() << "PROBE-FAIL: 没进入多线程下载页";
        return 5;
    }
    grab(&w, outDir + QStringLiteral("/20_download_page.png"));

    const QList<QLineEdit *> edits = page->findChildren<QLineEdit *>();
    QPushButton *startBtn = nullptr;
    for (QPushButton *b : page->findChildren<QPushButton *>()) {
        if (b->text() == QString::fromUtf8("开始下载")) {
            startBtn = b;
            break;
        }
    }
    if (edits.size() < 2 || !startBtn) {
        qCritical() << "PROBE-FAIL: 下载页控件没找全" << edits.size() << startBtn;
        return 6;
    }

    const QString uiPath = workDir + QStringLiteral("/from_page.bin");
    QFile::remove(uiPath);
    edits.at(0)->setText(server.url());
    edits.at(1)->setText(uiPath);
    pump(nullptr, 200);
    startBtn->click();

    const bool done = waitUntil([&uiPath, &payload]() {
        const QFileInfo info(uiPath);
        return info.exists() && info.size() == payload.size();
    }, 30000, &w);
    pump(&w, 300);
    grab(&w, outDir + QStringLiteral("/30_download_done.png"));

    // 下完之后 *.part 应该已经改名成成品，不能在目录里留一份临时文件
    const bool partCleaned = !QFileInfo::exists(uiPath + QStringLiteral(".part"));
    fprintf(stderr, "[probe] 临时文件已清理=%d\n", partCleaned ? 1 : 0);

    QString stateText;
    for (QLabel *l : page->findChildren<QLabel *>()) {
        if (l->text().contains(QString::fromUtf8("下载完成")))
            stateText = l->text();
    }
    fprintf(stderr, "[probe] 页面下载：落盘=%d 状态文案=%s\n", done ? 1 : 0,
            stateText.toLocal8Bit().constData());
    if (!done || !partCleaned || !fileEquals(uiPath, payload) || stateText.isEmpty()) {
        qWarning() << "PROBE-FAIL: 页面下载没成功（状态文案：" << stateText << "）";
        ++failures;
    }

    QFile::remove(uiPath);
    QDir(workDir).removeRecursively();

    fprintf(stderr, failures == 0 ? "PROBE-OK\n" : "PROBE-FAILURES=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
