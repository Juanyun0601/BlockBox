/**
 * @file   Telemetry.cpp
 * @brief  匿名使用统计实现
 * @author BlockBox Team
 * @date   2026-11-27
 */
#include "utils/Telemetry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScreen>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include "platform.h"
#include "utils/AppVersion.h"
#include "utils/CrashReporter.h"
#include "utils/SettingsManager.h"
#include "utils/SystemInfo.h"

namespace {

/// 单次请求最多带多少条事件（服务端也有限制）
constexpr int kMaxEventsPerBatch = 80;
/// 本地最多攒多少条，超了丢最老的
constexpr int kMaxQueuedEvents = 400;
/// 定时上报间隔：5 分钟
constexpr int kFlushIntervalMs = 5 * 60 * 1000;
/// 退出时等待网络的最长时间
constexpr int kShutdownFlushWaitMs = 1800;

QString queueFilePath()
{
    return Platform::getDataDirectory() + QStringLiteral("/telemetry/pending.json");
}

QString formFactorName()
{
    return Platform::isMobile() ? QStringLiteral("mobile") : QStringLiteral("desktop");
}

QString screenSummary()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return QString();
    const QSize size = screen->availableGeometry().size();
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

} // namespace

Telemetry *Telemetry::instance()
{
    static Telemetry telemetry;
    return &telemetry;
}

Telemetry::Telemetry(QObject *parent)
    : QObject(parent)
{
}

Telemetry::~Telemetry() = default;

void Telemetry::install()
{
    if (m_installed)
        return;
    m_installed = true;

    m_enabled = QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
                    .value(QStringLiteral("diagnostics/telemetry_enabled"), false)
                    .toBool();

    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(false);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, [this]() { flush(false); });

    m_persistTimer = new QTimer(this);
    m_persistTimer->setSingleShot(true);
    m_persistTimer->setInterval(4000);
    connect(m_persistTimer, &QTimer::timeout, this, [this]() { persistQueue(); });

    loadQueue();
    // 用户关掉统计后本地不应该继续留着待发数据：清空队列，避免下次误发
    if (!m_enabled) {
        m_events.clear();
        QFile::remove(queueFilePath());
    }
}

void Telemetry::start()
{
    if (m_started)
        return;
    m_started = true;
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sessionStartedAt = QDateTime::currentSecsSinceEpoch();
    m_pageViews = 0;

    if (!m_enabled)
        return;

    QJsonObject props;
    props.insert(QStringLiteral("form_factor"), formFactorName());
    const QString screen = screenSummary();
    if (!screen.isEmpty())
        props.insert(QStringLiteral("screen"), screen);
    enqueue(QStringLiteral("app_start"), props);

    if (m_flushTimer)
        m_flushTimer->start();
    // 启动后等一会儿再发，不跟启动更新检查抢网络
    QTimer::singleShot(10000, this, [this]() { flush(false); });
}

void Telemetry::shutdown()
{
    if (!m_started) {
        persistQueue();
        return;
    }
    if (m_flushTimer)
        m_flushTimer->stop();

    if (m_enabled) {
        QJsonObject props;
        const qint64 duration =
            qMax<qint64>(0, QDateTime::currentSecsSinceEpoch() - m_sessionStartedAt);
        props.insert(QStringLiteral("duration_seconds"), duration);
        props.insert(QStringLiteral("page_views"), m_pageViews);
        enqueue(QStringLiteral("app_exit"), props);
        persistQueue();
        flush(true);
    } else {
        persistQueue();
    }
    m_started = false;
}

bool Telemetry::isEnabled()
{
    if (Telemetry *telemetry = instance())
        return telemetry->m_enabled;
    return false;
}

void Telemetry::setEnabled(bool enabled)
{
    Telemetry *telemetry = instance();
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    settings.setValue(QStringLiteral("diagnostics/telemetry_enabled"), enabled);
    settings.sync();

    if (telemetry->m_enabled == enabled)
        return;
    telemetry->m_enabled = enabled;

    if (enabled) {
        if (!telemetry->m_started)
            telemetry->start();
        else {
            telemetry->m_sessionStartedAt = QDateTime::currentSecsSinceEpoch();
            QJsonObject props;
            props.insert(QStringLiteral("form_factor"), formFactorName());
            telemetry->enqueue(QStringLiteral("app_start"), props);
            if (telemetry->m_flushTimer)
                telemetry->m_flushTimer->start();
            telemetry->flush(false);
        }
    } else {
        if (telemetry->m_flushTimer)
            telemetry->m_flushTimer->stop();
        telemetry->m_events.clear();
        telemetry->persistQueue();
    }

    emit telemetry->enabledChanged(enabled);
}

void Telemetry::recordEvent(const QString &name, const QJsonObject &props)
{
    instance()->enqueue(name, props);
}

void Telemetry::pageView(const QString &page)
{
    Telemetry *telemetry = instance();
    if (!telemetry->m_started || !telemetry->m_enabled || page.isEmpty())
        return;
    telemetry->m_pageViews += 1;
    QJsonObject props;
    props.insert(QStringLiteral("page"), page);
    telemetry->enqueue(QStringLiteral("page_view"), props);
}

int Telemetry::pendingEventCount()
{
    return instance()->m_events.size();
}

QString Telemetry::lastUploadSummary()
{
    return QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .value(QStringLiteral("diagnostics/telemetry_last_upload"))
        .toString();
}

void Telemetry::enqueue(const QString &name, const QJsonObject &props)
{
    if (!m_enabled || !m_started || name.isEmpty())
        return;

    QJsonObject event;
    event.insert(QStringLiteral("name"), name);
    event.insert(QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch());
    if (!props.isEmpty())
        event.insert(QStringLiteral("props"), props);
    m_events.append(event);

    if (m_events.size() > kMaxQueuedEvents)
        m_events = m_events.mid(m_events.size() - kMaxQueuedEvents);

    schedulePersist();
}

void Telemetry::schedulePersist()
{
    if (m_persistTimer)
        m_persistTimer->start();
}

void Telemetry::persistQueue()
{
    const QString path = queueFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    if (m_events.isEmpty()) {
        QFile::remove(path);
        return;
    }

    QJsonArray array;
    for (const QJsonObject &event : m_events)
        array.append(event);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    file.flush();
    file.close();
}

void Telemetry::loadQueue()
{
    QFile file(queueFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray())
        return;

    const QJsonArray array = doc.array();
    m_events.clear();
    m_events.reserve(qMin(array.size(), kMaxQueuedEvents));
    const int start = qMax(0, array.size() - kMaxQueuedEvents);
    for (int i = start; i < array.size(); ++i) {
        if (array.at(i).isObject())
            m_events.append(array.at(i).toObject());
    }
}

QJsonObject Telemetry::basePayload() const
{
    const SystemInfoData info = SystemInfo::collect();
    QJsonObject payload;
    payload.insert(QStringLiteral("install_id"), CrashReporter::installId());
    payload.insert(QStringLiteral("session_id"), m_sessionId);
    payload.insert(QStringLiteral("app_version"), AppVersion::current());
    payload.insert(QStringLiteral("platform"), CrashReporter::platformKey());
    payload.insert(QStringLiteral("os"), info.osName);
    payload.insert(QStringLiteral("os_version"), info.kernelVersion);
    payload.insert(QStringLiteral("arch"), info.architecture);
    payload.insert(QStringLiteral("locale"), QLocale::system().name());
    payload.insert(QStringLiteral("form_factor"), formFactorName());
    payload.insert(QStringLiteral("client_time"), QDateTime::currentSecsSinceEpoch());
    return payload;
}

void Telemetry::flush(bool waitForFinish)
{
    if (!m_enabled || m_events.isEmpty() || m_flushing) {
        if (waitForFinish && m_enabled)
            persistQueue();
        return;
    }

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    // 先把要发的这一批从队列里摘出来：发送期间新来的事件排在后面
    const int count = qMin(m_events.size(), kMaxEventsPerBatch);
    const QVector<QJsonObject> batch = m_events.mid(0, count);
    m_events = m_events.mid(count);

    QJsonObject payload = basePayload();
    QJsonArray events;
    for (const QJsonObject &event : batch)
        events.append(event);
    payload.insert(QStringLiteral("events"), events);

    QNetworkRequest request(
        QUrl(SettingsManager::instance()->forumBaseUrl()
             + QStringLiteral("/api/app/telemetry")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/json; charset=utf-8"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BlockBoxLauncher/%1").arg(AppVersion::current()));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-Install-Id", CrashReporter::installId().toUtf8());
    request.setTransferTimeout(waitForFinish ? 1500 : 15000);

    m_flushing = true;
    QNetworkReply *reply = m_network->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QEventLoop *loop = waitForFinish ? new QEventLoop(this) : nullptr;
    QTimer *timeout = waitForFinish ? new QTimer(this) : nullptr;
    if (timeout) {
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, loop, [loop, reply]() {
            reply->abort();
            loop->quit();
        });
        timeout->start(kShutdownFlushWaitMs);
    }

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, batch, loop, timeout, waitForFinish]() {
                reply->deleteLater();
                if (timeout)
                    timeout->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    // 发送失败：把这批放回队首，下次启动再试
                    for (int i = batch.size() - 1; i >= 0; --i)
                        m_events.prepend(batch.at(i));
                    QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
                        .setValue(QStringLiteral("diagnostics/telemetry_last_upload"),
                                  QDateTime::currentDateTime().toString(
                                      QStringLiteral("yyyy-MM-dd HH:mm"))
                                      + QCoreApplication::translate(
                                          "Telemetry", " 发送失败，稍后重试"));
                } else {
                    QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
                        .setValue(QStringLiteral("diagnostics/telemetry_last_upload"),
                                  QDateTime::currentDateTime().toString(
                                      QStringLiteral("yyyy-MM-dd HH:mm"))
                                      + QCoreApplication::translate(
                                          "Telemetry", " 已发送 %1 条").arg(batch.size()));
                }
                persistQueue();
                m_flushing = false;
                if (loop) {
                    loop->quit();
                    loop->deleteLater();
                }
            });

    if (waitForFinish && loop)
        loop->exec();
}
