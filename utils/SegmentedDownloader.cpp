/**
 * @file   SegmentedDownloader.cpp
 * @brief  分段多线程下载引擎实现
 * @author BlockBox Team
 * @date   2026-11-27
 */

#include "SegmentedDownloader.h"

#include "GithubAccelerator.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {

/** 单个分段最多自动重试几次（网络抖一下就重来，不用让用户重新点） */
constexpr int kMaxSegmentAttempts = 3;

/** 小于这个大小的分段不划算：短连接开销比省下的时间还多 */
const qint64 kMinSegmentBytes = 1024 * 1024;

/** 进度信号最快多久发一次，避免每个数据包都刷一遍界面 */
constexpr int kProgressIntervalMs = 150;

/** 速度采样间隔 */
constexpr int kSpeedSampleMs = 500;

/** 默认 UA：不少站点看到非浏览器 UA 会直接拒掉，保留方块盒子自己的标识 */
const char *const kDefaultUserAgent = "BlockBox/1.0";

/** 从 "bytes 0-0/1234567" 里取出总大小；取不到返回 -1 */
qint64 totalFromContentRange(const QString &value)
{
    const int slash = value.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return -1;

    const QString total = value.mid(slash + 1).trimmed();
    if (total.isEmpty() || total == QLatin1String("*"))
        return -1;

    bool ok = false;
    const qint64 size = total.toLongLong(&ok);
    return ok ? size : -1;
}

/** 给请求统一带上 UA / 编码 / 超时设置（Accept-Encoding 必须是 identity，否则 Range 对不上） */
void applyCommonHeaders(QNetworkRequest &request, const QString &userAgent)
{
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Accept-Encoding", "identity");
}

} // namespace

SegmentedDownloader::SegmentedDownloader(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_userAgent(QString::fromLatin1(kDefaultUserAgent))
{
    // GitHub Releases 之类的直链都是先 302 到 CDN，跟着跳转才能下到东西
    m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    m_speedTimer.setInterval(kSpeedSampleMs);
    connect(&m_speedTimer, &QTimer::timeout, this, [this]() {
        updateSpeed();
        emitProgress();
    });
}

SegmentedDownloader::~SegmentedDownloader()
{
    m_speedTimer.stop();
    abortReplies();
    if (m_file.isOpen())
        m_file.close();
}

void SegmentedDownloader::setSegmentCount(int count)
{
    m_segmentCount = qBound(1, count, 32);
}

void SegmentedDownloader::setUserAgent(const QString &userAgent)
{
    const QString trimmed = userAgent.trimmed();
    m_userAgent = trimmed.isEmpty() ? QString::fromLatin1(kDefaultUserAgent) : trimmed;
}

void SegmentedDownloader::start(const QUrl &url, const QString &savePath)
{
    if (m_state == State::Probing || m_state == State::Downloading)
        return;

    // 上一次的残留（连接 / 临时文件）先清干净，避免两套任务抢同一个文件
    abortReplies();
    if (m_file.isOpen())
        m_file.close();
    m_segments.clear();
    m_fileOpened = false;
    m_singleStream = false;
    m_rangeSupported = false;
    m_rangeRejected = false;
    m_totalBytes = -1;
    m_speed = 0;
    m_lastSpeedBytes = 0;
    m_errorMessage.clear();
    m_savePath = savePath;
    // 先写到 *.part：下完再改名，中途崩了也不会留下一个大小看着对、内容却坏的同名文件
    m_workPath = savePath + QStringLiteral(".part");
    if (QFile::exists(m_workPath))
        QFile::remove(m_workPath);

    const QString originalUrl = url.toString();
    const QString accelerated = GithubAccelerator::instance()->accelerateUrl(originalUrl);
    m_url = QUrl(accelerated.isEmpty() ? originalUrl : accelerated);

    m_progressClock.start();
    setState(State::Probing);
    probeFile();
}

void SegmentedDownloader::probeFile()
{
    QNetworkRequest request(m_url);
    applyCommonHeaders(request, m_userAgent);
    request.setRawHeader("Range", "bytes=0-0");
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    m_probeReply = m_network->get(request);
    connect(m_probeReply, &QNetworkReply::readyRead, this,
            &SegmentedDownloader::handleProbeReadyRead);
    connect(m_probeReply, &QNetworkReply::finished, this,
            &SegmentedDownloader::handleProbeFinished);
    connect(m_probeReply, &QNetworkReply::downloadProgress, this,
            [this](qint64, qint64) { emitProgress(); });
}

void SegmentedDownloader::handleProbeReadyRead()
{
    QNetworkReply *reply = m_probeReply;
    if (!reply)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // 服务器忽略了 Range：说明不支持分段。这里要立刻断开，否则整份文件会先堆进内存
    if (status == 200) {
        m_rangeRejected = true;
        m_totalBytes = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (m_totalBytes <= 0)
            m_totalBytes = -1;
        reply->abort();
        return;
    }

    // 206 只需要开头那一个字节，读掉丢弃就行；其它状态等 finished 统一报错
    reply->readAll();
}

void SegmentedDownloader::handleProbeFinished()
{
    QNetworkReply *reply = m_probeReply;
    if (!reply)
        return;
    m_probeReply = nullptr;

    if (m_state != State::Probing) {
        reply->deleteLater();
        return;
    }

    // 探测阶段就发现不支持分段：退回单连接，直接把文件下完
    if (m_rangeRejected) {
        reply->deleteLater();
        m_rangeSupported = false;
        emit prepared(m_totalBytes, false);
        startSingleStream();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QString errorText = reply->errorString();
        reply->deleteLater();
        fail(tr("连不上服务器：%1").arg(errorText));
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString contentRange =
        QString::fromLatin1(reply->rawHeader("Content-Range"));

    // 200 + 没有 Accept-Ranges：老服务器会无视 Range 直接回整份文件，老老实实单连接下
    if (status == 200) {
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (length > 0)
            m_totalBytes = length;
        reply->deleteLater();
        m_rangeSupported = false;
        emit prepared(m_totalBytes, false);
        startSingleStream();
        return;
    }

    reply->deleteLater();

    if (status == 416) {
        fail(tr("服务器不接受分段请求（HTTP 416），换个直链试试"));
        return;
    }
    if (status != 206) {
        fail(tr("服务器没有按分段要求返回数据（HTTP %1），该链接可能不是直链").arg(status));
        return;
    }

    const qint64 total = totalFromContentRange(contentRange);
    if (total < 0) {
        fail(tr("服务器没有给出文件大小，无法分段下载"));
        return;
    }

    m_totalBytes = total;
    m_rangeSupported = true;

    if (!openOutputFile())
        return;

    emit prepared(m_totalBytes, true);
    if (m_totalBytes == 0) {
        // 空文件：占好位就算下完了
        finishSuccess();
        return;
    }
    startSegments();
}

void SegmentedDownloader::startSegments()
{
    if (m_totalBytes <= 0)
        return;

    // 文件很小时硬拆反而更慢：按 1MB 一段算能拆几段，再和用户设的线程数取小
    const qint64 chunkCount = qMax<qint64>(1, m_totalBytes / kMinSegmentBytes);
    int count = static_cast<int>(qMin<qint64>(m_segmentCount, chunkCount));
    if (count < 1)
        count = 1;

    const qint64 baseSize = m_totalBytes / count;

    m_segments.clear();
    m_segments.reserve(count);
    for (int i = 0; i < count; ++i) {
        Segment seg;
        seg.begin = static_cast<qint64>(i) * baseSize;
        seg.end = (i == count - 1) ? (m_totalBytes - 1) : ((i + 1) * baseSize - 1);
        m_segments.append(seg);
    }

    m_speedClock.start();
    m_progressClock.restart();
    setState(State::Downloading);

    for (int i = 0; i < m_segments.size(); ++i)
        startSegment(i);

    emitProgress();
}

void SegmentedDownloader::startSegment(int index)
{
    if (index < 0 || index >= m_segments.size())
        return;

    Segment &seg = m_segments[index];
    const qint64 expected = seg.end - seg.begin + 1;
    if (seg.received >= expected)
        return;

    const qint64 from = seg.begin + seg.received;
    const qint64 to = seg.end;

    QNetworkRequest request(m_url);
    applyCommonHeaders(request, m_userAgent);
    request.setRawHeader("Range",
                         QStringLiteral("bytes=%1-%2").arg(from).arg(to).toUtf8());
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply *reply = m_network->get(request);
    seg.reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, index, reply]() {
        // 分段可能已经被取消 / 重排：只处理当前这一份连接
        if (index >= m_segments.size() || m_segments.at(index).reply != reply)
            return;
        handleSegmentReadyRead(index);
    });
    connect(reply, &QNetworkReply::finished, this, [this, index, reply]() {
        if (index >= m_segments.size() || m_segments.at(index).reply != reply) {
            reply->deleteLater();
            return;
        }
        handleSegmentFinished(index);
    });
}

void SegmentedDownloader::handleSegmentReadyRead(int index)
{
    if (m_state != State::Downloading || index < 0 || index >= m_segments.size())
        return;

    Segment &seg = m_segments[index];
    QNetworkReply *reply = seg.reply;
    if (!reply)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 200) {
        // 服务器口头支持分段、实际把整份文件推回来：从头改走单连接，别写出错位的文件
        restartAsSingleStream(tr("服务器忽略了分段请求"));
        return;
    }

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty())
        return;

    if (!writeChunk(seg.begin + seg.received, chunk))
        return;

    seg.received += chunk.size();

    if (!m_progressClock.isValid() || m_progressClock.elapsed() >= kProgressIntervalMs) {
        emitProgress();
        m_progressClock.restart();
    }
}

void SegmentedDownloader::handleSegmentFinished(int index)
{
    if (index < 0 || index >= m_segments.size())
        return;

    Segment &seg = m_segments[index];
    QNetworkReply *reply = seg.reply;
    seg.reply = nullptr;
    if (!reply)
        return;

    // 收尾：把缓冲区里剩下的一点数据也写进去
    QByteArray rest = reply->readAll();
    if (!rest.isEmpty() && m_state == State::Downloading) {
        if (writeChunk(seg.begin + seg.received, rest))
            seg.received += rest.size();
    }

    const QString errorText = reply->errorString();
    reply->deleteLater();

    if (m_state != State::Downloading)
        return;

    const qint64 expected = seg.end - seg.begin + 1;
    if (seg.received >= expected) {
        emitProgress();
        if (allSegmentsDone())
            finishSuccess();
        return;
    }

    // 通道没给够字节数（断流 / 超时）：同一段接着要，最多重试几次
    if (seg.attempts < kMaxSegmentAttempts) {
        ++seg.attempts;
        const int delayMs = 400 * seg.attempts;
        qWarning() << "[SegmentedDownloader] 分段" << index << "未收全，重试：" << errorText;
        QTimer::singleShot(delayMs, this, [this, index]() {
            if (m_state != State::Downloading || index >= m_segments.size())
                return;
            if (m_segments.at(index).reply)
                return;
            startSegment(index);
        });
        return;
    }

    fail(tr("第 %1 段下载失败：%2")
             .arg(index + 1)
             .arg(errorText.isEmpty() ? tr("连接中途断开") : errorText));
}

void SegmentedDownloader::restartAsSingleStream(const QString &reason)
{
    qWarning() << "[SegmentedDownloader] 转为单连接下载：" << reason;

    abortReplies();
    if (m_file.isOpen())
        m_file.close();
    m_fileOpened = false;
    m_segments.clear();
    m_rangeSupported = false;
    m_singleStream = false;

    emit prepared(m_totalBytes, false);
    startSingleStream();
}

void SegmentedDownloader::startSingleStream()
{
    if (!m_fileOpened && !openOutputFile())
        return;

    m_singleStream = true;
    m_segments.clear();
    Segment seg;
    seg.begin = 0;
    seg.end = m_totalBytes > 0 ? m_totalBytes - 1 : -1;
    m_segments.append(seg);

    m_speedClock.start();
    m_progressClock.restart();
    setState(State::Downloading);

    QNetworkRequest request(m_url);
    applyCommonHeaders(request, m_userAgent);
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    m_singleReply = m_network->get(request);
    connect(m_singleReply, &QNetworkReply::readyRead, this,
            &SegmentedDownloader::handleSingleReadyRead);
    connect(m_singleReply, &QNetworkReply::finished, this,
            &SegmentedDownloader::handleSingleFinished);

    emitProgress();
}

void SegmentedDownloader::handleSingleReadyRead()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_singleReply || m_segments.isEmpty())
        return;
    if (m_state != State::Downloading) {
        reply->readAll();
        return;
    }

    // 单连接模式下服务器才给总大小也是常事（分块传输时没有 Content-Length）
    if (m_totalBytes <= 0) {
        const qint64 len = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (len > 0) {
            m_totalBytes = len;
            emit prepared(m_totalBytes, false);
        }
    }

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty())
        return;

    Segment &seg = m_segments[0];
    if (!writeChunk(seg.begin + seg.received, chunk))
        return;
    seg.received += chunk.size();

    if (!m_progressClock.isValid() || m_progressClock.elapsed() >= kProgressIntervalMs) {
        emitProgress();
        m_progressClock.restart();
    }
}

void SegmentedDownloader::handleSingleFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_singleReply)
        return;
    m_singleReply = nullptr;

    if (m_state != State::Downloading) {
        reply->deleteLater();
        return;
    }

    QByteArray rest = reply->readAll();
    if (!rest.isEmpty() && !m_segments.isEmpty()) {
        Segment &seg = m_segments[0];
        if (writeChunk(seg.begin + seg.received, rest))
            seg.received += rest.size();
    }

    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    reply->deleteLater();

    if (m_state != State::Downloading)
        return;
    if (error != QNetworkReply::NoError) {
        fail(tr("下载中断：%1").arg(errorText));
        return;
    }

    finishSuccess();
}

bool SegmentedDownloader::openOutputFile()
{
    const QFileInfo info(m_workPath);
    if (!info.absoluteDir().exists())
        QDir().mkpath(info.absolutePath());

    if (m_file.isOpen())
        m_file.close();
    m_file.setFileName(m_workPath);
    if (!m_file.open(QIODevice::ReadWrite)) {
        fail(tr("无法创建文件：%1（%2）").arg(m_savePath, m_file.errorString()));
        return false;
    }

    // 先按总大小占位：各分段按偏移写，最后不用再拼一次文件
    if (m_totalBytes > 0)
        m_file.resize(m_totalBytes);
    else
        m_file.resize(0);

    m_fileOpened = true;
    return true;
}

bool SegmentedDownloader::writeChunk(qint64 offset, const QByteArray &data)
{
    if (!m_file.isOpen()) {
        fail(tr("文件已经关闭，写不进去了"));
        return false;
    }
    if (!m_file.seek(offset) || m_file.write(data) != data.size()) {
        fail(tr("写入文件失败：%1").arg(m_file.errorString()));
        return false;
    }
    return true;
}

bool SegmentedDownloader::allSegmentsDone() const
{
    if (m_segments.isEmpty())
        return false;
    for (const Segment &seg : m_segments) {
        if (seg.received < seg.end - seg.begin + 1)
            return false;
    }
    return true;
}

void SegmentedDownloader::pause()
{
    if (m_state != State::Downloading || !m_rangeSupported || m_singleStream)
        return;

    m_speedTimer.stop();
    abortReplies();
    if (m_file.isOpen())
        m_file.flush();
    setState(State::Paused);
    emitProgress();
}

void SegmentedDownloader::resume()
{
    if (m_state != State::Paused)
        return;

    m_speedClock.start();
    m_progressClock.restart();
    setState(State::Downloading);

    for (int i = 0; i < m_segments.size(); ++i) {
        const Segment &seg = m_segments.at(i);
        if (!seg.reply && seg.received < seg.end - seg.begin + 1)
            startSegment(i);
    }
    emitProgress();
}

void SegmentedDownloader::cancel()
{
    if (m_state == State::Idle || m_state == State::Completed
        || m_state == State::Cancelled) {
        return;
    }

    m_speedTimer.stop();
    abortReplies();
    if (m_file.isOpen())
        m_file.close();
    m_fileOpened = false;
    m_segments.clear();
    m_singleStream = false;

    // 取消就是不要这个文件了：半成品留着只会让用户下次看到一个坏文件
    if (!m_workPath.isEmpty())
        QFile::remove(m_workPath);

    setState(State::Cancelled);
    emitProgress();
}

qint64 SegmentedDownloader::receivedBytes() const
{
    qint64 total = 0;
    for (const Segment &seg : m_segments)
        total += seg.received;
    return total;
}

void SegmentedDownloader::abortReplies()
{
    if (m_probeReply) {
        QNetworkReply *reply = m_probeReply;
        m_probeReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    if (m_singleReply) {
        QNetworkReply *reply = m_singleReply;
        m_singleReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    for (Segment &seg : m_segments) {
        if (!seg.reply)
            continue;
        QNetworkReply *reply = seg.reply;
        seg.reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
}

void SegmentedDownloader::finishSuccess()
{
    m_speedTimer.stop();

    const qint64 received = receivedBytes();
    if (m_totalBytes > 0 && received != m_totalBytes) {
        fail(tr("下载不完整（%1 / %2 字节）").arg(received).arg(m_totalBytes));
        return;
    }

    if (m_file.isOpen()) {
        if (m_totalBytes <= 0)
            m_file.resize(received);
        m_file.flush();
        m_file.close();
    }
    m_fileOpened = false;

    // 下完才把 *.part 改成用户要的文件名：中途看一眼目录也不会误认成下好了
    if (QFile::exists(m_savePath) && !QFile::remove(m_savePath)) {
        fail(tr("替换旧文件失败：%1").arg(m_savePath));
        return;
    }
    if (!QFile::rename(m_workPath, m_savePath)) {
        fail(tr("保存文件失败：%1").arg(m_savePath));
        return;
    }

    setState(State::Completed);
    emitProgress();
    emit finished(m_savePath);
}

void SegmentedDownloader::fail(const QString &message)
{
    if (m_state == State::Failed || m_state == State::Cancelled)
        return;

    m_speedTimer.stop();
    m_errorMessage = message;
    abortReplies();

    // 已下好的部分不删：分段模式下点「继续」还能从断点接着走
    if (m_file.isOpen())
        m_file.flush();

    setState(State::Failed);
    qWarning() << "[SegmentedDownloader] 下载失败：" << message;
    emit failed(message);
}

void SegmentedDownloader::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;

    if (state == State::Downloading)
        m_speedTimer.start();
    else
        m_speedTimer.stop();

    emit stateChanged(m_state);
}

void SegmentedDownloader::updateSpeed()
{
    const qint64 current = receivedBytes();
    const qint64 delta = qMax<qint64>(0, current - m_lastSpeedBytes);
    m_lastSpeedBytes = current;

    const qint64 elapsedMs = m_speedClock.isValid() ? qMax<qint64>(1, m_speedClock.restart())
                                                   : kSpeedSampleMs;
    const qint64 instant = delta * 1000 / elapsedMs;

    // 轻度平滑一下，免得数字上下乱跳
    m_speed = (m_speed <= 0) ? instant : (m_speed * 3 + instant) / 4;
}

void SegmentedDownloader::emitProgress()
{
    const qint64 received = receivedBytes();

    int secondsRemaining = -1;
    if (m_totalBytes > 0 && m_speed > 0) {
        const qint64 remain = qMax<qint64>(0, m_totalBytes - received);
        secondsRemaining = static_cast<int>(qMin<qint64>(remain / m_speed, 359999));
    }

    emit progress(received, m_totalBytes, m_speed, secondsRemaining);
}
