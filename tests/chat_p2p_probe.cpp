/**
 * @file   chat_p2p_probe.cpp
 * @brief  聊天 P2P 直传协议探针：本机一端起 share，另一端 requestFile 拉取
 *
 * 覆盖：
 *   1. 邀约 JSON 来回序列化字段一致
 *   2. 正常直传：字节一致、进度信号齐全、发送侧收到 shareFinished(true)
 *   3. 口令错误：对方收到 BBX_ERR，接收失败且不留半成品文件
 *   4. 邀约过期：requestFile 直接拒绝
 *   5. 停止共享后：连不上对方，接收按失败收尾
 *
 * 构建：见 chat_p2p_probe.pro；运行：chat_p2p_probe
 * 输出：逐条 PASS/FAIL；退出码 0 = 全部通过。
 */
#include "utils/ChatP2P.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace {

int g_failed = 0;

void report(bool ok, const char *name, const QString &detail = QString())
{
    std::printf("%s %s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : QString("  — %1").arg(detail).toUtf8().constData());
    std::fflush(stdout);
    if (!ok)
        ++g_failed;
}

/** 写一个 3MB 的伪随机测试文件（LCG 生成，可复现） */
QString makeSourceFile(const QDir &dir, const QString &name, qint64 size)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    quint32 seed = 0x20261122u;
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    qint64 left = size;
    while (left > 0)
    {
        const int take = static_cast<int>(qMin<qint64>(left, chunk.size()));
        for (int i = 0; i < take; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            chunk[i] = static_cast<char>(seed >> 16);
        }
        file.write(chunk.constData(), take);
        left -= take;
    }
    file.close();
    return path;
}

bool filesEqual(const QString &a, const QString &b)
{
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    if (fa.size() != fb.size())
        return false;
    while (!fa.atEnd())
    {
        const QByteArray ca = fa.read(64 * 1024);
        const QByteArray cb = fb.read(ca.size());
        if (ca != cb)
            return false;
    }
    return true;
}

/** 带超时地转事件循环，等谓词成立 */
bool waitSignal(int timeoutMs, const std::function<bool()> &predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        if (predicate())
            return true;
        QEventLoop loop;
        QTimer::singleShot(50, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QDir workDir(QCoreApplication::applicationDirPath());
    const QString source = makeSourceFile(workDir, QStringLiteral("bbx_probe_src.bin"),
                                          3 * 1024 * 1024);
    if (source.isEmpty())
    {
        report(false, "make_source_file");
        return 1;
    }
    const qint64 sourceSize = QFileInfo(source).size();

    // ── 1. 邀约 JSON 来回序列化 ──
    {
        ChatP2POffer offer;
        offer.tid = QStringLiteral("tid-1");
        offer.token = QStringLiteral("tok-1");
        offer.name = QStringLiteral("demo.zip");
        offer.size = 12345;
        offer.port = 47323;
        offer.ips = {QStringLiteral("192.168.1.5"), QStringLiteral("10.0.0.2")};
        offer.exp = 111;
        const ChatP2POffer round = ChatP2POffer::fromJson(
            QJsonDocument(offer.toJson()).object());
        report(round.isOffer() && round.tid == offer.tid && round.token == offer.token
                   && round.name == offer.name && round.size == offer.size
                   && round.port == offer.port && round.ips == offer.ips && round.exp == offer.exp,
               "offer_json_roundtrip",
               QStringLiteral("isOffer=%1 tid=%2 token=%3 name=%4 size=%5 port=%6 ips=%7 exp=%8")
                   .arg(round.isOffer() ? 1 : 0)
                   .arg(round.tid, round.token, round.name)
                   .arg(round.size).arg(round.port).arg(round.ips.join(QLatin1Char(',')))
                   .arg(round.exp));

        ChatP2POffer control;
        control.tid = QStringLiteral("tid-2");
        control.act = QStringLiteral("retry-normal");
        const ChatP2POffer controlRound = ChatP2POffer::fromJson(
            QJsonDocument(control.toJson()).object());
        report(controlRound.isControl() && !controlRound.isOffer(), "control_json_roundtrip");

        const ChatP2POffer garbage = ChatP2POffer::fromJson(QJsonObject{
            {QStringLiteral("v"), 1}});
        report(!garbage.isValid(), "garbage_body_invalid");
    }

    // ── 2. 正常直传 ──
    {
        // 每个用例一个局部作用域对象：块结束自动断开本块所有信号连接，
        // 避免后一个用例触发前一个用例悬垂捕获
        QObject scope;
        ChatP2P::instance()->stopShare(); // 上一用例的共享先停掉

        const ChatP2POffer offer = ChatP2P::instance()->startShare(source);
        if (!offer.isOffer())
        {
            report(false, "start_share", QStringLiteral("tid=%1").arg(offer.tid));
        }
        else
        {
            report(!offer.ips.isEmpty() && offer.port > 0 && offer.size == sourceSize,
                   "start_share", QStringLiteral("port=%1 ips=%2")
                       .arg(offer.port).arg(offer.ips.join(QLatin1Char(','))));

            bool shareDone = false;
            bool shareOk = false;
            qint64 shareFinalBytes = -1;
            int shareProgressCount = 0;
            int recvProgressCount = 0;

            QObject::connect(ChatP2P::instance(), &ChatP2P::shareProgress, &scope,
                             [&](const QString &, double, qint64 sent, qint64, qint64) {
                                 ++shareProgressCount;
                                 shareFinalBytes = sent;
                             });
            QObject::connect(ChatP2P::instance(), &ChatP2P::shareFinished, &scope,
                             [&](const QString &, bool ok, const QString &) {
                                 shareDone = true;
                                 shareOk = ok;
                             });

            ChatP2POffer localOffer = offer;
            localOffer.ips = {QStringLiteral("127.0.0.1")}; // 探针里直连回环
            const QString savePath = workDir.filePath(QStringLiteral("bbx_probe_dst.bin"));
            QFile::remove(savePath);

            bool recvDone = false;
            bool recvOk = false;
            QString recvMessage;
            QString recvPath;
            QObject::connect(ChatP2P::instance(), &ChatP2P::receiveProgress, &scope,
                             [&](const QString &, double, qint64, qint64, qint64) {
                                 ++recvProgressCount;
                             });
            QObject::connect(ChatP2P::instance(), &ChatP2P::receiveFinished, &scope,
                             [&](const QString &, bool ok, const QString &path, const QString &msg) {
                                 recvDone = true;
                                 recvOk = ok;
                                 recvPath = path;
                                 recvMessage = msg;
                             });

            const QString requestId = ChatP2P::instance()->requestFile(localOffer, savePath);
            report(requestId == offer.tid, "request_accepted");
            waitSignal(20000, [&] { return recvDone && shareDone; });
            report(recvDone && recvOk && shareDone && shareOk, "transfer_finished",
                   QStringLiteral("recv=%1 share=%2 msg=%3")
                       .arg(recvOk ? "ok" : "fail", shareOk ? "ok" : "fail", recvMessage));
            report(recvPath == savePath, "receive_path");
            report(recvProgressCount > 0 && shareProgressCount > 0
                       && shareFinalBytes == sourceSize,
                   "progress_signals",
                   QStringLiteral("recvProgress=%1 shareProgress=%2 lastSent=%3")
                       .arg(recvProgressCount).arg(shareProgressCount).arg(shareFinalBytes));
            report(filesEqual(source, savePath), "bytes_identical");
            QFile::remove(savePath);
        }
        ChatP2P::instance()->stopShare(); // 已传完：静默清理，不会发 shareFinished
    }

    // ── 3. 口令错误 ──
    {
        QObject scope;
        ChatP2P::instance()->stopShare();

        const ChatP2POffer offer = ChatP2P::instance()->startShare(source);
        if (!offer.isOffer())
        {
            report(false, "start_share_for_bad_token");
        }
        else
        {
            ChatP2POffer forged = offer;
            forged.token = QStringLiteral("bad").repeated(12);
            forged.ips = {QStringLiteral("127.0.0.1")};
            const QString savePath = workDir.filePath(QStringLiteral("bbx_probe_badtok.bin"));
            bool recvDone = false;
            bool recvOk = false;
            QString recvMessage;
            QObject::connect(ChatP2P::instance(), &ChatP2P::receiveFinished, &scope,
                             [&](const QString &, bool ok, const QString &, const QString &msg) {
                                 recvDone = true;
                                 recvOk = ok;
                                 recvMessage = msg;
                             });
            ChatP2P::instance()->requestFile(forged, savePath);
            waitSignal(15000, [&] { return recvDone; });
            const bool noLeftover = !QFileInfo::exists(savePath);
            report(recvDone && !recvOk && noLeftover, "bad_token_rejected", recvMessage);
        }
        ChatP2P::instance()->stopShare();
    }

    // ── 4. 邀约过期 ──
    {
        QObject scope;
        ChatP2P::instance()->stopShare();

        const ChatP2POffer offer = ChatP2P::instance()->startShare(source);
        if (!offer.isOffer())
        {
            report(false, "start_share_for_expired");
        }
        else
        {
            ChatP2POffer stale = offer;
            stale.exp = QDateTime::currentMSecsSinceEpoch() - 1000;
            const QString requestId = ChatP2P::instance()->requestFile(
                stale, workDir.filePath(QStringLiteral("bbx_probe_stale.bin")));
            report(requestId.isEmpty() && !ChatP2P::instance()->isReceiving(stale.tid),
                   "expired_offer_rejected");
        }
        ChatP2P::instance()->stopShare();
    }

    // ── 5. 停止共享后：连不上对方 ──
    {
        QObject scope;
        ChatP2P::instance()->stopShare();

        const ChatP2POffer offer = ChatP2P::instance()->startShare(source);
        if (!offer.isOffer())
        {
            report(false, "start_share_for_stop");
        }
        else
        {
            ChatP2POffer localOffer = offer;
            localOffer.ips = {QStringLiteral("127.0.0.1")};
            bool recvDone = false;
            bool recvOk = false;
            QString recvMessage;
            QObject::connect(ChatP2P::instance(), &ChatP2P::receiveFinished, &scope,
                             [&](const QString &, bool ok, const QString &, const QString &msg) {
                                 recvDone = true;
                                 recvOk = ok;
                                 recvMessage = msg;
                             });
            ChatP2P::instance()->stopShare(); // 发送方取消共享：监听已关
            recvDone = false;
            recvOk = false;
            const QString requestId = ChatP2P::instance()->requestFile(
                localOffer, workDir.filePath(QStringLiteral("bbx_probe_stopped.bin")));
            report(!requestId.isEmpty(), "request_after_stop_still_starts");
            waitSignal(20000, [&] { return recvDone; });
            const bool noLeftover =
                !QFileInfo::exists(workDir.filePath(QStringLiteral("bbx_probe_stopped.bin")));
            report(recvDone && !recvOk && noLeftover, "connect_after_stop_fails", recvMessage);
        }
        ChatP2P::instance()->stopShare();
    }

    ChatP2P::instance()->stopShare();
    QFile::remove(source);
    std::printf("%s\n", g_failed == 0 ? "ALL PASS" : "HAS FAILURES");
    return g_failed == 0 ? 0 : 1;
}
