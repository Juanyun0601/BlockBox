/**
 * @file   HongshiLobbyClient.cpp
 * @brief  红石联机大厅客户端实现
 * @author BlockBox Team
 * @date   2026-08-30
 */

#include "HongshiLobbyClient.h"

#include <functional>
#include <memory>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
/// 主站 / 镜像站（与 HongshiClient 保持一致）
const QString kPrimaryBase = QStringLiteral("https://hongshi.site");
const QString kMirrorBase  = QStringLiteral("https://shithub.site");

const int kHttpTimeoutMs = 10000;
} // namespace

// ============================
// 单例
// ============================

HongshiLobbyClient *HongshiLobbyClient::instance()
{
    static HongshiLobbyClient *s_inst = nullptr;
    if (!s_inst) {
        s_inst = new HongshiLobbyClient(qApp);
    }
    return s_inst;
}

HongshiLobbyClient::HongshiLobbyClient(QObject *parent)
    : QObject(parent)
{
    m_autoRefreshTimer.setSingleShot(false);
    connect(&m_autoRefreshTimer, &QTimer::timeout,
            this, &HongshiLobbyClient::onAutoRefresh);
}

HongshiLobbyClient::~HongshiLobbyClient() = default;

// ============================
// 公开接口
// ============================

void HongshiLobbyClient::refreshRooms()
{
    if (m_loading) return;
    setLoading(true);
    m_lastError.clear();
    tryFetchRoomList(0);
}

void HongshiLobbyClient::setAutoRefreshInterval(int intervalMs)
{
    if (intervalMs > 0) {
        m_autoRefreshTimer.setInterval(intervalMs);
        if (!m_autoRefreshTimer.isActive())
            m_autoRefreshTimer.start();
    } else {
        m_autoRefreshTimer.stop();
    }
}

// ============================
// 内部：网络请求
// ============================

void HongshiLobbyClient::tryFetchRoomList(int siteIndex)
{
    // 主站 → 镜像，各尝试一次
    if (siteIndex >= 2) {
        setLoading(false);
        if (m_lastError.isEmpty())
            m_lastError = tr("获取房间列表失败");
        emit refreshError(m_lastError);
        return;
    }

    const QString base = (siteIndex == 0) ? kPrimaryBase : kMirrorBase;
    // 参考 hongshionline/hongshi-shell API 路径风格
    QUrl url(base + QStringLiteral("/api/rooms"));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(kHttpTimeoutMs);

    QNetworkReply *reply = m_networkManager.get(req);

    // 超时保护
    QTimer *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(kHttpTimeoutMs);
    connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
    timeoutTimer->start();

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, siteIndex, base]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            m_lastError = QStringLiteral("%1: %2")
                .arg(base, reply->errorString());
            // 尝试下一个站点
            tryFetchRoomList(siteIndex + 1);
            return;
        }

        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError) {
            m_lastError = QStringLiteral("%1: JSON 解析失败").arg(base);
            tryFetchRoomList(siteIndex + 1);
            return;
        }

        // 服务端返回格式可能是：
        //   { "rooms": [ ... ] }        ← 包装格式
        //   [ ... ]                      ← 直接数组
        //   { "code": 0, "data": [...] } ← 通用包装
        QJsonArray arr;
        if (doc.isArray()) {
            arr = doc.array();
        } else if (doc.isObject()) {
            const QJsonObject root = doc.object();
            if (root.contains(QStringLiteral("rooms"))) {
                arr = root.value(QStringLiteral("rooms")).toArray();
            } else if (root.contains(QStringLiteral("data"))) {
                const QJsonValue dataVal = root.value(QStringLiteral("data"));
                if (dataVal.isArray()) {
                    arr = dataVal.toArray();
                } else if (dataVal.isObject()) {
                    // data 可能是 { "rooms": [...] } 嵌套
                    const QJsonObject dataObj = dataVal.toObject();
                    if (dataObj.contains(QStringLiteral("rooms"))) {
                        arr = dataObj.value(QStringLiteral("rooms")).toArray();
                    }
                }
            }
        }

        m_rooms = parseRoomList(arr);
        setLoading(false);
        emit roomsRefreshed(m_rooms);
    });
}

// ============================
// JSON 解析
// ============================

QList<HongshiRoomInfo> HongshiLobbyClient::parseRoomList(const QJsonArray &arr)
{
    QList<HongshiRoomInfo> result;
    result.reserve(arr.size());

    for (const QJsonValue &val : arr) {
        if (!val.isObject()) continue;
        HongshiRoomInfo room = parseRoomObject(val.toObject());
        if (room.roomId.isEmpty()) continue;
        result.append(room);
    }

    return result;
}

HongshiRoomInfo HongshiLobbyClient::parseRoomObject(const QJsonObject &obj) const
{
    HongshiRoomInfo room;

    // 灵活匹配字段名（兼容不同大小写 / 命名风格）
    room.roomId = obj.value(QStringLiteral("roomId")).toString();
    if (room.roomId.isEmpty())
        room.roomId = obj.value(QStringLiteral("id")).toString();
    if (room.roomId.isEmpty())
        room.roomId = obj.value(QStringLiteral("room_id")).toString();

    room.roomName = obj.value(QStringLiteral("roomName")).toString();
    if (room.roomName.isEmpty())
        room.roomName = obj.value(QStringLiteral("name")).toString();

    room.hostName = obj.value(QStringLiteral("hostName")).toString();
    if (room.hostName.isEmpty())
        room.hostName = obj.value(QStringLiteral("host")).toString();
    if (room.hostName.isEmpty())
        room.hostName = obj.value(QStringLiteral("owner")).toString();

    room.gameVersion = obj.value(QStringLiteral("gameVersion")).toString();
    if (room.gameVersion.isEmpty())
        room.gameVersion = obj.value(QStringLiteral("version")).toString();

    room.loaderType = obj.value(QStringLiteral("loaderType")).toString();
    if (room.loaderType.isEmpty())
        room.loaderType = obj.value(QStringLiteral("loader")).toString();

    room.currentPlayers = obj.value(QStringLiteral("currentPlayers")).toInt(0);
    if (room.currentPlayers == 0)
        room.currentPlayers = obj.value(QStringLiteral("players")).toInt(0);
    if (room.currentPlayers == 0)
        room.currentPlayers = obj.value(QStringLiteral("online")).toInt(0);

    room.maxPlayers = obj.value(QStringLiteral("maxPlayers")).toInt(0);
    if (room.maxPlayers == 0)
        room.maxPlayers = obj.value(QStringLiteral("max")).toInt(0);

    room.status = obj.value(QStringLiteral("status")).toString().toLower();
    if (room.status.isEmpty()) {
        // 根据人数推断状态
        if (room.maxPlayers > 0 && room.currentPlayers >= room.maxPlayers)
            room.status = QStringLiteral("full");
        else if (room.currentPlayers > 0)
            room.status = QStringLiteral("playing");
        else
            room.status = QStringLiteral("waiting");
    }

    room.nodeRegion = obj.value(QStringLiteral("nodeRegion")).toString();
    if (room.nodeRegion.isEmpty())
        room.nodeRegion = obj.value(QStringLiteral("region")).toString();
    if (room.nodeRegion.isEmpty())
        room.nodeRegion = obj.value(QStringLiteral("node")).toString();

    // 时间戳：可能是秒或毫秒
    qint64 ts = obj.value(QStringLiteral("createdAt")).toVariant().toLongLong();
    if (ts == 0)
        ts = obj.value(QStringLiteral("created_at")).toVariant().toLongLong();
    if (ts == 0)
        ts = obj.value(QStringLiteral("time")).toVariant().toLongLong();
    // 如果时间戳 > 1e12，认为是毫秒级
    if (ts > 1000000000000LL)
        ts /= 1000;
    room.createdAt = ts;

    room.latencyMs = obj.value(QStringLiteral("latencyMs")).toInt(-1);
    if (room.latencyMs == -1)
        room.latencyMs = obj.value(QStringLiteral("latency")).toInt(-1);

    room.tunnelAddress = obj.value(QStringLiteral("tunnelAddress")).toString();
    if (room.tunnelAddress.isEmpty())
        room.tunnelAddress = obj.value(QStringLiteral("address")).toString();
    if (room.tunnelAddress.isEmpty())
        room.tunnelAddress = obj.value(QStringLiteral("tunnel")).toString();

    return room;
}

// ============================
// 内部工具
// ============================

void HongshiLobbyClient::setLoading(bool loading)
{
    if (m_loading == loading) return;
    m_loading = loading;
    emit loadingChanged(loading);
}

void HongshiLobbyClient::onAutoRefresh()
{
    if (!m_loading) {
        refreshRooms();
    }
}
