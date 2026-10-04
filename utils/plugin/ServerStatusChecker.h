/**
 * @file   ServerStatusChecker.h
 * @brief  Minecraft 服务器状态并发检测器
 * @author BlockBox Team
 * @date   2026-08-09
 *
 * 使用 QThreadPool 并发执行 Minecraft 1.7+ Server List Ping 协议检测，
 * 每个服务器一个 QRunnable 任务（QTcpSocket 同步模式，无跨线程 UI 访问），
 * 结果通过信号（跨线程自动 QueuedConnection）回传到 UI 线程。
 */
#ifndef SERVERSTATUSCHECKER_H
#define SERVERSTATUSCHECKER_H

#include <QAtomicInt>
#include <QList>
#include <QObject>
#include <QString>

class QThreadPool;

/**
 * @brief 服务器状态并发检测器
 */
class ServerStatusChecker : public QObject
{
    Q_OBJECT

public:
    /** 检测目标：host + port + 行号（用于回传定位） */
    struct Target
    {
        QString host;
        quint16 port = 25565;
        int row = -1;            // UI 行号，回传用
    };

    explicit ServerStatusChecker(QObject *parent = nullptr);
    ~ServerStatusChecker() override;

    /** 启动一轮检测（取消上一轮未完成的任务） */
    void checkAll(const QList<Target> &targets, int timeoutMs = 3000, int maxConcurrent = 24);
    /** 取消当前检测（未开始的任务直接报失败） */
    void cancel();

signals:
    /** 单台服务器检测完成（跨线程自动排队） */
    void statusReady(int row, bool ok, int pingMs, int online, int max);
    /** 全部检测完成 */
    void allDone();

private:
    friend class ServerPingTask;
    bool isCanceled() const { return m_canceled.loadRelaxed() != 0; }
    int decrementAndGet() { return m_remaining.fetchAndAddRelaxed(-1) - 1; }

    QThreadPool *m_pool = nullptr;
    QList<Target> m_targets;
    int m_timeoutMs = 3000;
    QAtomicInt m_remaining;
    QAtomicInt m_canceled;
};

#endif // SERVERSTATUSCHECKER_H
