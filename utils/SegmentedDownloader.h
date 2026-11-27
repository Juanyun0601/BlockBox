/**
 * @file   SegmentedDownloader.h
 * @brief  分段多线程下载引擎：把一条直链按字节区间拆成多段同时下载
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 与 utils/MultiThreadDownloader（各下载页内部用的一次性下载器）的分工：
 * 这里的下载器是给用户全程操控的，除了分段并发，还要回报字节级进度、速度、
 * 剩余时间，并支持暂停 / 继续 / 取消；下载到一半网络抖一下也会自己续上。
 *
 * 工作流程：
 *   1. 先用 Range: bytes=0-0 探一次，从 Content-Range 拿到总大小与能否分段；
 *   2. 能分段就按线程数切区间，各段并发下载并按偏移写进同一个文件；
 *   3. 服务器忽略 Range（回 200）时自动退回单连接顺序下载，不让用户白等。
 *
 * 网络与文件操作都留在调用线程（GUI 线程）里，QNetworkReply 本身是异步的，
 * 不会卡界面；没有另开线程是因为分段的瓶颈在网络带宽，不在 CPU。
 */
#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

class SegmentedDownloader : public QObject
{
    Q_OBJECT

public:
    /** 下载器状态：页面据此决定按钮可不可点、状态文案写什么 */
    enum class State {
        Idle,        ///< 还没开始（空闲 / 上一次已结束）
        Probing,     ///< 正在探测文件大小与是否支持分段
        Downloading, ///< 正在下载（分段或单连接）
        Paused,      ///< 已暂停，分段临时数据仍在，可以继续
        Completed,   ///< 下载完成
        Failed,      ///< 下载失败（错误原因见 errorMessage()）
        Cancelled    ///< 用户取消（半成品文件已删掉）
    };
    Q_ENUM(State)

    explicit SegmentedDownloader(QObject *parent = nullptr);
    ~SegmentedDownloader() override;

    /** 分段数（也就是同时开几条连接），超出 1~32 会被夹到边界内 */
    void setSegmentCount(int count);
    int segmentCount() const { return m_segmentCount; }

    /** 自定义 User-Agent；留空用默认的 BlockBox/1.0 */
    void setUserAgent(const QString &userAgent);
    QString userAgent() const { return m_userAgent; }

    /**
     * @brief 开始一次下载
     *
     * 上一次的下载（无论成功失败）会被直接顶掉，不留半成品。
     * 同目录会按 savePath 里的文件名落盘，目录不存在会自动建。
     */
    void start(const QUrl &url, const QString &savePath);

    /** 暂停：只在分段下载且文件信息已就绪时有效 */
    void pause();
    /** 继续：只对 Paused 状态有效，按各段已收字节续传 */
    void resume();
    /** 取消：断开所有连接并删除半成品文件 */
    void cancel();

    State state() const { return m_state; }
    /** 服务器是否支持按 Range 分段（false 表示走的是单连接顺序下载） */
    bool isRangeSupported() const { return m_rangeSupported; }
    /** 实际开出的分段数（文件太小或服务器不支持时可能小于设定的线程数） */
    int activeSegmentCount() const { return m_segments.size(); }
    /** 文件总大小，未知时为 -1 */
    qint64 totalBytes() const { return m_totalBytes; }
    qint64 receivedBytes() const;
    qint64 bytesPerSecond() const { return m_speed; }
    QString savePath() const { return m_savePath; }
    /** 下载过程中的临时文件（savePath + ".part"），取消 / 完成时会清掉 */
    QString partialPath() const { return m_workPath; }
    QString errorMessage() const { return m_errorMessage; }

signals:
    void stateChanged(SegmentedDownloader::State state);

    /** 文件信息就绪：总大小（未知为 -1）与服务器是否支持分段 */
    void prepared(qint64 totalBytes, bool rangeSupported);

    /**
     * @brief 进度
     * @param receivedBytes 已收字节
     * @param totalBytes    总字节（未知为 -1）
     * @param bytesPerSecond 平滑后的速度
     * @param secondsRemaining 预计剩余秒数（未知为 -1）
     */
    void progress(qint64 receivedBytes, qint64 totalBytes, qint64 bytesPerSecond,
                  int secondsRemaining);

    void finished(const QString &savePath);
    void failed(const QString &message);

private:
    /** 一个下载分段：区间 [begin, end]，received 是目前已落盘的字节数 */
    struct Segment
    {
        qint64 begin = 0;
        qint64 end = 0;
        qint64 received = 0;
        int attempts = 0;
        QNetworkReply *reply = nullptr;
    };

    void probeFile();
    void handleProbeReadyRead();
    void handleProbeFinished();

    void startSegments();
    void startSegment(int index);
    void handleSegmentReadyRead(int index);
    void handleSegmentFinished(int index);

    void startSingleStream();
    void handleSingleReadyRead();
    void handleSingleFinished();
    void restartAsSingleStream(const QString &reason);

    bool openOutputFile();
    bool writeChunk(qint64 offset, const QByteArray &data);
    bool allSegmentsDone() const;

    void abortReplies();
    void finishSuccess();
    void fail(const QString &message);
    void setState(State state);
    void emitProgress();
    void updateSpeed();

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_probeReply = nullptr;
    QNetworkReply *m_singleReply = nullptr;

    QFile m_file;
    QUrl m_url;             // 已过 GitHub 加速后的实际请求地址
    QString m_savePath;     // 用户要的最终文件名
    QString m_workPath;     // 实际写入的临时文件（*.part），下完再改名
    QString m_userAgent;
    QString m_errorMessage;

    QVector<Segment> m_segments;
    QTimer m_speedTimer;
    QElapsedTimer m_speedClock;
    QElapsedTimer m_progressClock;

    State m_state = State::Idle;
    bool m_rangeSupported = false;  // 服务器支持分段（探测到 206）
    bool m_singleStream = false;    // 正在走单连接顺序下载
    bool m_fileOpened = false;
    bool m_rangeRejected = false;   // 探测时服务器忽略了 Range（回了 200）

    qint64 m_totalBytes = -1;
    qint64 m_speed = 0;
    qint64 m_lastSpeedBytes = 0;
    int m_segmentCount = 8;
};
