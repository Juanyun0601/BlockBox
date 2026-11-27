/**
 * @file   Telemetry.h
 * @brief  匿名使用统计（可关闭）：只上报版本、系统、页面访问和会话时长
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 默认关闭，用户在「设置 → 高级设置 → 诊断与统计」里主动开启后才工作。
 * 采集的是产品改进用的汇总数据：启动次数、会话时长、版本 / 系统 / 平台分布、
 * 各页面访问次数、崩溃数量；不包含账号、实例名、存档、聊天、文件名等用户内容。
 *
 * 事件先写进内存队列并定期落盘（<数据目录>/telemetry/pending.json），
 * 每 5 分钟或退出时批量发到论坛 /api/app/telemetry；发送失败留到下次启动重试。
 */
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QTimer;

class Telemetry : public QObject
{
    Q_OBJECT

public:
    static Telemetry *instance();

    /** 启动时调用：读取设置、恢复上次未发出的队列（不会联网） */
    void install();

    /** 界面就绪、用户已同意协议后调用：记录一次启动并开始定时上报 */
    void start();

    /** 退出前调用：记录会话时长、落盘并尽力发出去 */
    void shutdown();

    /** 是否开启匿名使用统计（设置项 diagnostics/telemetry_enabled，默认关闭） */
    static bool isEnabled();
    static void setEnabled(bool enabled);

    /** 记录一个事件（未开启时什么都不做） */
    static void recordEvent(const QString &name, const QJsonObject &props = QJsonObject());

    /** 记录一次页面访问（只记 page key，不记实例名等用户内容） */
    static void pageView(const QString &page);

    /** 还没发出去的事件数（设置页展示） */
    static int pendingEventCount();

    /** 最近一次上报结果，方便设置页显示「上次发送时间」 */
    static QString lastUploadSummary();

signals:
    /** 开关状态变化（设置页开关与内部状态同步） */
    void enabledChanged(bool enabled);

private:
    explicit Telemetry(QObject *parent = nullptr);
    ~Telemetry() override;

    void enqueue(const QString &name, const QJsonObject &props);
    void schedulePersist();
    void persistQueue();
    void loadQueue();
    void flush(bool waitForFinish);
    QJsonObject basePayload() const;

    QVector<QJsonObject> m_events;
    QNetworkAccessManager *m_network = nullptr;
    QTimer *m_flushTimer = nullptr;
    QTimer *m_persistTimer = nullptr;
    QString m_sessionId;
    qint64 m_sessionStartedAt = 0;
    int m_pageViews = 0;
    bool m_installed = false;
    bool m_started = false;
    bool m_flushing = false;
    bool m_enabled = false;
};

#endif // TELEMETRY_H
