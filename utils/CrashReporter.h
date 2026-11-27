/**
 * @file   CrashReporter.h
 * @brief  崩溃自动上报：接住闪退、留下现场、下次启动询问是否发送
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 启动器自己闪退时（访问冲突、std::terminate、qFatal、信号等）普通用户
 * 拿不到调用栈，只能描述「点了一下就没了」。这里在崩溃当场把原因、调用栈、
 * 出事前最后一段运行日志写进 <数据目录>/crash-reports/，下次启动时弹窗
 * 询问用户是否发送；用户同意后上传到论坛的公开上报接口（不需要登录）。
 *
 * 隐私：上传前用 LogSanitizer 把用户名、用户目录、邮箱、IP、令牌换掉；
 * 报告只包含诊断信息，不包含账号、存档、聊天内容。
 */
#ifndef CRASHREPORTER_H
#define CRASHREPORTER_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QWidget;

/** 一份已经落盘的崩溃报告 */
struct CrashReport
{
    QString id;              ///< 报告标识（进程内生成，服务端按它去重）
    qint64 timestamp = 0;    ///< 崩溃时间（秒级时间戳）
    QString reason;          ///< unhandled_exception / std::terminate / qFatal / SIGSEGV / unexpected_exit ...
    QString signalName;      ///< 信号名或异常代码
    QString exceptionCode;   ///< Windows 异常代码 / 故障地址
    QString faultModule;     ///< 出错模块（module+offset）
    QString thread;          ///< 出错线程
    QString phase;           ///< 启动中 / 运行中 / 退出中
    QString page;            ///< 崩溃时所在页面（只是页面标识，不含用户数据）
    QString appVersion;
    QString platform;
    QString os;
    QString osVersion;
    QString arch;
    QString locale;
    qint64 uptimeSeconds = 0;
    QString stack;           ///< 调用栈（文本）
    QString logTail;         ///< 出事前最后一段运行日志
    QString context;         ///< 额外现场（例如 qFatal 的消息）
    QString filePath;        ///< 本地文件路径
    bool sent = false;       ///< 是否已经成功上传

    bool isValid() const { return !id.isEmpty() && !filePath.isEmpty(); }

    /** 弹窗标题下面那句人话总结 */
    QString summary() const;
    /** 给用户看的完整文本（复制报告 / 详情） */
    QString displayText() const;
};

class CrashReporter : public QObject
{
    Q_OBJECT

public:
    using UploadHandler = std::function<void(bool ok, const QString &message)>;

    static CrashReporter *instance();

    /** 安装崩溃接管（在 QApplication 与 LauncherLogger 之后调用，越早越好） */
    void install();

    /** 正常退出：移除「正在运行」标记，下次启动就不会误判为异常退出 */
    void shutdown();

    /** 记录当前阶段（启动中 / 运行中 / 退出中），崩溃时一并带上 */
    void setAppPhase(const QString &phase);

    /** 记录当前页面标识（只记 page key，不记实例名等用户内容） */
    void setCurrentPage(const QString &page);

    /** 崩溃报告目录（<数据目录>/crash-reports） */
    QString crashDirectory() const;

    /** 本地所有报告（新→旧） */
    QVector<CrashReport> reports(bool includeSent = true) const;

    /** 还没发出去的报告（新→旧） */
    QVector<CrashReport> pendingReports() const;

    int pendingCount() const;

    /**
     * @brief 处理上次留下的崩溃报告
     * @param parent 弹窗父窗口
     * @param done  处理完（发送 / 暂不发送 / 自动发送）后回调，用来接着做检查更新等动作
     *
     * 没开「自动发送」时弹窗询问；开了自动发送且用户之前同意过就直接上传。
     * 没有任何报告时立即回调 done。
     */
    void processPendingReports(QWidget *parent, std::function<void()> done = nullptr);

    /** 上传一份报告（成功后标记为已发送；失败保留，稍后可重试） */
    void uploadReport(const CrashReport &report, UploadHandler done = nullptr);

    /** 依次上传所有待发送报告（设置页「立即发送」） */
    void uploadPendingReports(std::function<void(int okCount, int failCount)> done = nullptr);

    /** 删除一份本地报告 */
    void deleteReport(const CrashReport &report);

    /** 当前平台标识（与服务端版本发布使用同一套字符串） */
    static QString platformKey();

    /** 本机匿名标识（随机 UUID，可在设置里重置；不含任何账号信息） */
    static QString installId();

    /** 换一个新的本机匿名标识（隐私设置里的「重置标识」） */
    static QString resetInstallId();

    static bool autoSendEnabled();
    static void setAutoSendEnabled(bool enabled);

    /** 用户是否明确同意过发送崩溃报告（自动发送必须先有这份同意） */
    static bool sendConsent();
    static void setSendConsent(bool consent);

signals:
    void pendingCountChanged(int count);

private:
    explicit CrashReporter(QObject *parent = nullptr);
    ~CrashReporter() override;

    void writeRunningMarker();
    void collectUnexpectedExit();
    void cleanupOldReports();
    void markSent(const QString &filePath);
    QJsonObject buildPayload(const CrashReport &report) const;
    void notifyPendingChanged();

    QString crashDirectoryCache;
    class QNetworkAccessManager *m_network = nullptr;
    class QTimer *m_heartbeat = nullptr;
};

#endif // CRASHREPORTER_H
