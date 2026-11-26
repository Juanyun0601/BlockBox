/**
 * @file   McpServer.cpp
 * @brief  方块盒子本地 MCP（Model Context Protocol）服务器实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "utils/mcp/McpServer.h"

#include <QDebug>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

#include "utils/AiService.h"
#include "utils/AppVersion.h"
#include "utils/mcp/McpToolCatalog.h"

namespace {

constexpr int kMaxHeaderBytes = 64 * 1024;
constexpr int kMaxBodyBytes = 8 * 1024 * 1024;
constexpr int kToolTimeoutMs = 120 * 1000;
const char *kProtocolVersion = "2025-06-18";

QByteArray statusText(int status)
{
    switch (status)
    {
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default: return "OK";
    }
}

/** 解 chunked 请求体；返回 false 表示数据还没收全 */
bool decodeChunkedBody(QByteArray &buffer, QByteArray &body, bool &done)
{
    body.clear();
    done = false;
    int pos = 0;
    while (true)
    {
        const int lineEnd = buffer.indexOf("\r\n", pos);
        if (lineEnd < 0)
        {
            return false;
        }
        QByteArray sizeLine = buffer.mid(pos, lineEnd - pos);
        const int semicolon = sizeLine.indexOf(';');
        if (semicolon >= 0)
        {
            sizeLine = sizeLine.left(semicolon);
        }
        bool ok = false;
        const int chunkSize = sizeLine.trimmed().toInt(&ok, 16);
        if (!ok || chunkSize < 0)
        {
            return false;
        }
        pos = lineEnd + 2;

        if (chunkSize == 0)
        {
            // 结束块：允许后面跟着 trailer，再以空行收尾
            if (pos + 2 <= buffer.size() && buffer.mid(pos, 2) == "\r\n")
            {
                done = true;
                buffer.remove(0, pos + 2);
                return true;
            }
            const int trailerEnd = buffer.indexOf("\r\n\r\n", pos);
            if (trailerEnd < 0)
            {
                return false;
            }
            done = true;
            buffer.remove(0, trailerEnd + 4);
            return true;
        }

        if (buffer.size() < pos + chunkSize + 2)
        {
            return false;
        }
        body.append(buffer.mid(pos, chunkSize));
        pos += chunkSize + 2;
    }
}

bool isLocalHostName(const QString &host)
{
    const QString lower = host.toLower();
    return lower == QStringLiteral("localhost")
        || lower == QStringLiteral("127.0.0.1")
        || lower == QStringLiteral("::1")
        || lower == QStringLiteral("[::1]");
}

} // namespace

McpServer::McpServer(QObject *parent)
    : QObject(parent)
{
}

McpServer::~McpServer()
{
    stop();
}

void McpServer::setAiServiceProvider(std::function<AiService *()> provider)
{
    m_aiProvider = std::move(provider);
}

bool McpServer::isRunning() const
{
    return m_server != nullptr && m_server->isListening();
}

bool McpServer::start(quint16 port, const QString &token, bool allowWrite,
                      QString *errorMessage)
{
    stop();

    if (errorMessage)
    {
        errorMessage->clear();
    }

    m_token = token;
    m_allowWrite = allowWrite;

    if (!m_server)
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &McpServer::onNewConnection);
    }

    if (!m_server->listen(QHostAddress::LocalHost, port))
    {
        const QString reason = m_server->errorString();
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        emit statusChanged(false, 0, reason);
        return false;
    }

    m_port = m_server->serverPort();
    emit statusChanged(true, m_port, QString());
    return true;
}

void McpServer::stop()
{
    const bool wasRunning = isRunning();

    // 断开 SSE 会话
    const QStringList sessionIds = m_sseSessions.keys();
    for (const QString &id : sessionIds)
    {
        if (SseSession *session = m_sseSessions.take(id))
        {
            if (session->socket)
            {
                session->socket->disconnectFromHost();
            }
            delete session;
        }
    }

    // 断开普通连接
    const QList<QTcpSocket *> sockets = m_connections.keys();
    for (QTcpSocket *socket : sockets)
    {
        if (Connection *conn = m_connections.take(socket))
        {
            delete conn;
        }
        if (socket)
        {
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    }

    if (m_server)
    {
        m_server->close();
    }
    m_port = 0;

    if (wasRunning)
    {
        emit statusChanged(false, 0, QString());
    }
}

void McpServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections())
    {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
        {
            continue;
        }
        m_connections.insert(socket, new Connection);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            onReadyRead(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            if (Connection *conn = m_connections.take(socket))
            {
                delete conn;
            }
            socket->deleteLater();
        });
    }
}

void McpServer::onReadyRead(QTcpSocket *socket)
{
    auto it = m_connections.constFind(socket);
    if (it == m_connections.constEnd())
    {
        return;
    }
    Connection *conn = it.value();
    if (!conn || conn->complete)
    {
        return;
    }

    conn->buffer += socket->readAll();
    if (conn->buffer.size() > kMaxHeaderBytes + kMaxBodyBytes)
    {
        writeHttp(socket, 413, "application/json",
                  QByteArray("{\"error\":\"request too large\"}"));
        return;
    }

    QString err;
    if (!parseRequest(*conn, err))
    {
        if (!err.isEmpty())
        {
            QJsonObject error{{QStringLiteral("error"), err}};
            writeHttp(socket, err.contains(QStringLiteral("过大")) ? 413 : 400,
                      "application/json", QJsonDocument(error).toJson(QJsonDocument::Compact));
        }
        return;
    }

    HttpRequest req;
    req.method = conn->method;
    req.path = conn->path;
    req.headers = conn->headers;
    req.query = conn->query;
    req.body = conn->body;
    conn->complete = true;
    conn->buffer.clear();

    dispatchHttp(socket, req);
}

bool McpServer::parseRequest(Connection &conn, QString &err)
{
    err.clear();

    if (!conn.headerParsed)
    {
        const int headerEnd = conn.buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
        {
            if (conn.buffer.size() > kMaxHeaderBytes)
            {
                err = QStringLiteral("请求头过大");
            }
            return false;
        }

        const QByteArray head = conn.buffer.left(headerEnd);
        conn.buffer.remove(0, headerEnd + 4);

        const QList<QByteArray> lines = head.split('\n');
        if (lines.isEmpty())
        {
            err = QStringLiteral("空请求");
            return false;
        }

        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        if (requestLine.size() < 2)
        {
            err = QStringLiteral("请求行无效");
            return false;
        }
        conn.method = QString::fromLatin1(requestLine.at(0)).trimmed().toUpper();
        const QString target = QString::fromLatin1(requestLine.at(1)).trimmed();

        const QUrl url(QStringLiteral("http://127.0.0.1") + target);
        conn.path = url.path();
        const QUrlQuery query(url);
        const auto items = query.queryItems(QUrl::FullyDecoded);
        for (const auto &item : items)
        {
            conn.query.insert(item.first, item.second);
        }

        for (int i = 1; i < lines.size(); ++i)
        {
            const QByteArray line = lines.at(i).trimmed();
            if (line.isEmpty())
            {
                continue;
            }
            const int colon = line.indexOf(':');
            if (colon <= 0)
            {
                continue;
            }
            const QString key = QString::fromLatin1(line.left(colon)).trimmed().toLower();
            const QString value = QString::fromLatin1(line.mid(colon + 1)).trimmed();
            conn.headers.insert(key, value);
        }

        conn.headerParsed = true;
        conn.contentLength = conn.headers.value(QStringLiteral("content-length")).toLongLong();
        conn.chunked = conn.headers.value(QStringLiteral("transfer-encoding"))
                           .contains(QStringLiteral("chunked"), Qt::CaseInsensitive);
    }

    if (conn.chunked)
    {
        QByteArray decoded;
        bool done = false;
        if (!decodeChunkedBody(conn.buffer, decoded, done))
        {
            if (conn.buffer.size() > kMaxBodyBytes)
            {
                err = QStringLiteral("请求体过大");
            }
            return false;
        }
        if (!done)
        {
            return false;
        }
        conn.body = decoded;
        return true;
    }

    if (conn.contentLength > 0)
    {
        if (conn.contentLength > kMaxBodyBytes)
        {
            err = QStringLiteral("请求体过大");
            return false;
        }
        if (conn.buffer.size() < conn.contentLength)
        {
            return false;
        }
        conn.body = conn.buffer.left(static_cast<int>(conn.contentLength));
    }

    return true;
}

void McpServer::dispatchHttp(QTcpSocket *socket, const HttpRequest &req)
{
    if (!checkOrigin(req))
    {
        writeHttp(socket, 403, "application/json",
                  QByteArray("{\"error\":\"origin not allowed\"}"));
        return;
    }

    if (req.method == QStringLiteral("OPTIONS"))
    {
        writeHttp(socket, 204, "text/plain", QByteArray());
        return;
    }

    if (req.path == QStringLiteral("/health"))
    {
        handleHealth(socket, req);
        return;
    }

    if (req.path == QStringLiteral("/mcp"))
    {
        handleStreamable(socket, req);
        return;
    }

    if (req.path == QStringLiteral("/sse"))
    {
        if (req.method == QStringLiteral("GET"))
        {
            handleSseConnect(socket, req);
        }
        else
        {
            writeHttp(socket, 405, "application/json",
                      QByteArray("{\"error\":\"use GET /sse\"}"));
        }
        return;
    }

    if (req.path == QStringLiteral("/messages"))
    {
        if (req.method == QStringLiteral("POST"))
        {
            handleSseMessage(socket, req);
        }
        else
        {
            writeHttp(socket, 405, "application/json",
                      QByteArray("{\"error\":\"use POST /messages\"}"));
        }
        return;
    }

    writeHttp(socket, 404, "application/json", QByteArray("{\"error\":\"not found\"}"));
}

void McpServer::handleHealth(QTcpSocket *socket, const HttpRequest &)
{
    QJsonObject result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("name"), QStringLiteral("blockbox-mcp"));
    result.insert(QStringLiteral("version"), AppVersion::current());
    result.insert(QStringLiteral("port"), static_cast<int>(m_port));
    result.insert(QStringLiteral("write"), m_allowWrite);
    result.insert(QStringLiteral("tools"), McpToolCatalog::tools(m_allowWrite).size());
    writeHttp(socket, 200, "application/json",
              QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void McpServer::handleStreamable(QTcpSocket *socket, const HttpRequest &req)
{
    if (req.method == QStringLiteral("GET") || req.method == QStringLiteral("DELETE"))
    {
        // 本服务器没有服务端主动推送的消息，按规范返回 405 即可
        writeHttp(socket, 405, "application/json",
                  QByteArray("{\"error\":\"server-initiated stream is not supported\"}"));
        return;
    }

    if (req.method != QStringLiteral("POST"))
    {
        writeHttp(socket, 405, "application/json",
                  QByteArray("{\"error\":\"use POST /mcp\"}"));
        return;
    }

    if (!checkAuth(req))
    {
        writeHttp(socket, 401, "application/json",
                  QByteArray("{\"error\":\"invalid or missing token\"}"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(req.body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        writeHttp(socket, 400, "application/json",
                  QByteArray("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":"
                             "{\"code\":-32700,\"message\":\"parse error\"}}"));
        return;
    }
    if (!doc.isObject())
    {
        // MCP 2025-06-18 起不再支持 JSON-RPC 批量请求
        writeHttp(socket, 400, "application/json",
                  QByteArray("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":"
                             "{\"code\":-32600,\"message\":\"batch requests are not supported\"}}"));
        return;
    }

    const QJsonObject message = doc.object();
    if (!message.contains(QStringLiteral("id")))
    {
        // 通知类消息（如 notifications/initialized）不需要响应体
        writeHttp(socket, 202, "text/plain", QByteArray());
        return;
    }

    QPointer<QTcpSocket> guard(socket);
    processRpc(message, [this, guard](const QJsonObject &response) {
        if (!guard)
        {
            return;
        }
        writeHttp(guard.data(), 200, "application/json",
                  QJsonDocument(response).toJson(QJsonDocument::Compact));
    });
}

void McpServer::handleSseConnect(QTcpSocket *socket, const HttpRequest &req)
{
    if (!checkAuth(req))
    {
        writeHttp(socket, 401, "application/json",
                  QByteArray("{\"error\":\"invalid or missing token\"}"));
        return;
    }

    if (Connection *conn = m_connections.take(socket))
    {
        delete conn;
    }

    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *session = new SseSession;
    session->id = sessionId;
    session->socket = socket;
    m_sseSessions.insert(sessionId, session);

    QByteArray headers;
    headers += "HTTP/1.1 200 OK\r\n";
    headers += "Content-Type: text/event-stream\r\n";
    headers += "Cache-Control: no-cache\r\n";
    headers += "Connection: keep-alive\r\n";
    headers += "Access-Control-Allow-Origin: *\r\n";
    headers += "\r\n";
    socket->write(headers);

    writeSse(socket, QByteArray("endpoint"),
             QStringLiteral("/messages?sessionId=%1").arg(sessionId).toUtf8());

    auto *heartbeat = new QTimer(socket);
    connect(heartbeat, &QTimer::timeout, this, [socket]() {
        if (socket && socket->state() == QAbstractSocket::ConnectedState)
        {
            socket->write(": ping\n\n");
            socket->flush();
        }
    });
    heartbeat->start(20000);

    connect(socket, &QTcpSocket::disconnected, this, [this, sessionId]() {
        if (SseSession *s = m_sseSessions.take(sessionId))
        {
            delete s;
        }
    });
}

void McpServer::handleSseMessage(QTcpSocket *socket, const HttpRequest &req)
{
    if (!checkAuth(req))
    {
        writeHttp(socket, 401, "application/json",
                  QByteArray("{\"error\":\"invalid or missing token\"}"));
        return;
    }

    const QString sessionId = req.query.value(QStringLiteral("sessionId"));
    SseSession *session = m_sseSessions.value(sessionId);
    if (!session || !session->socket)
    {
        writeHttp(socket, 404, "application/json",
                  QByteArray("{\"error\":\"unknown session\"}"));
        return;
    }

    // 旧版 HTTP+SSE 传输：POST 先回 202，真正结果从 SSE 通道下发
    writeHttp(socket, 202, "text/plain", QByteArray());

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(req.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        return;
    }
    const QJsonObject message = doc.object();
    if (!message.contains(QStringLiteral("id")))
    {
        return;
    }

    processRpc(message, [this, sessionId](const QJsonObject &response) {
        SseSession *s = m_sseSessions.value(sessionId);
        if (!s || !s->socket)
        {
            return;
        }
        writeSse(s->socket, QByteArray("message"),
                 QJsonDocument(response).toJson(QJsonDocument::Compact));
    });
}

void McpServer::processRpc(const QJsonObject &request,
                           const std::function<void(const QJsonObject &)> &respond)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();

    if (method == QStringLiteral("initialize"))
    {
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        const QString requested = params.value(QStringLiteral("protocolVersion")).toString();
        static const QStringList kSupported = {
            QStringLiteral("2024-11-05"),
            QStringLiteral("2025-03-26"),
            QStringLiteral("2025-06-18"),
        };
        const QString version = kSupported.contains(requested)
                                    ? requested
                                    : QString::fromLatin1(kProtocolVersion);

        QJsonObject toolsCapability;
        toolsCapability.insert(QStringLiteral("listChanged"), false);

        QJsonObject capabilities;
        capabilities.insert(QStringLiteral("tools"), toolsCapability);

        QJsonObject serverInfo;
        serverInfo.insert(QStringLiteral("name"), QStringLiteral("blockbox"));
        serverInfo.insert(QStringLiteral("title"), QStringLiteral("方块盒子"));
        serverInfo.insert(QStringLiteral("version"), AppVersion::current());

        QJsonObject result;
        result.insert(QStringLiteral("protocolVersion"), version);
        result.insert(QStringLiteral("capabilities"), capabilities);
        result.insert(QStringLiteral("serverInfo"), serverInfo);
        result.insert(QStringLiteral("instructions"),
                      QStringLiteral("方块盒子本地 MCP 服务器：可以查看本机实例、模组、日志、"
                                     "崩溃报告、存档、服务器列表，按种子计算结构坐标。"
                                     "默认只读；写操作需要在方块盒子「设置 > AI 助手 > 本地 MCP 服务器」里"
                                     "开启，并对每次写操作传 confirm=true。"));
        respond(makeRpcResult(id, result));
        return;
    }

    if (method == QStringLiteral("ping") || method == QStringLiteral("logging/setLevel"))
    {
        respond(makeRpcResult(id, QJsonObject()));
        return;
    }

    if (method == QStringLiteral("tools/list"))
    {
        QJsonObject result;
        result.insert(QStringLiteral("tools"), McpToolCatalog::tools(m_allowWrite));
        respond(makeRpcResult(id, result));
        return;
    }

    if (method == QStringLiteral("tools/call"))
    {
        callTool(id, request.value(QStringLiteral("params")).toObject(), respond);
        return;
    }

    if (method == QStringLiteral("resources/list")
        || method == QStringLiteral("resources/templates/list")
        || method == QStringLiteral("prompts/list"))
    {
        // 暂不提供资源与提示词，返回空列表让客户端正常走完握手
        QJsonObject result;
        if (method == QStringLiteral("resources/list"))
        {
            result.insert(QStringLiteral("resources"), QJsonArray());
        }
        else if (method == QStringLiteral("resources/templates/list"))
        {
            result.insert(QStringLiteral("resourceTemplates"), QJsonArray());
        }
        else
        {
            result.insert(QStringLiteral("prompts"), QJsonArray());
        }
        respond(makeRpcResult(id, result));
        return;
    }

    if (method.startsWith(QStringLiteral("notifications/")))
    {
        return;
    }

    respond(makeRpcError(id, -32601,
                         QStringLiteral("未知方法：%1").arg(method.isEmpty()
                                                               ? QStringLiteral("(空)")
                                                               : method)));
}

void McpServer::callTool(const QJsonValue &requestId, const QJsonObject &params,
                         const std::function<void(const QJsonObject &)> &respond)
{
    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    const QJsonObject args = params.value(QStringLiteral("arguments")).toObject();

    if (!McpToolCatalog::isKnown(name))
    {
        respond(makeRpcError(requestId, -32602,
                             QStringLiteral("未知工具：%1").arg(name.isEmpty()
                                                                   ? QStringLiteral("(空)")
                                                                   : name)));
        return;
    }

    const bool writing = McpToolCatalog::isWrite(name);
    if (writing && !m_allowWrite)
    {
        respond(makeRpcResult(requestId,
                              makeToolResult(QStringLiteral("写操作未开启：请先在方块盒子"
                                                            "「设置 > AI 助手 > 本地 MCP 服务器」"
                                                            "里打开「允许写操作」，再重试。"),
                                             true)));
        return;
    }
    if (writing && !args.value(QStringLiteral("confirm")).toBool(false))
    {
        respond(makeRpcResult(requestId,
                              makeToolResult(QStringLiteral("这是写操作（%1），还没有执行。"
                                                            "请先向用户确认影响，然后在 arguments 里"
                                                            "加 \"confirm\": true 重新调用。")
                                                 .arg(name),
                                             true)));
        return;
    }

    AiService *service = m_aiProvider ? m_aiProvider() : nullptr;
    if (!service)
    {
        respond(makeRpcResult(requestId,
                              makeToolResult(QStringLiteral("AI 服务尚未就绪，请先打开方块盒子"
                                                            "的 AI 助手页再重试。"),
                                             true)));
        return;
    }

    QJsonValue safeId = requestId;
    const QString toolName = name;

    // 与 Agent 工具一致：耗时工具丢进线程池，避免阻塞界面
    QFuture<QPair<QString, QString>> future =
        QtConcurrent::run([this, service, name, args, writing]() {
            QString error;
            QString text;
            if (writing)
            {
                QMutexLocker locker(&m_writeMutex);
                text = service->executeExternalTool(name, args, &error);
            }
            else
            {
                text = service->executeExternalTool(name, args, &error);
            }
            return qMakePair(text, error);
        });

    auto finished = std::make_shared<bool>(false);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, timer, respond, finished, safeId, toolName]() {
        if (*finished)
        {
            return;
        }
        *finished = true;
        timer->stop();
        timer->deleteLater();
        qWarning().noquote() << QStringLiteral("[MCP] 工具执行超时：%1").arg(toolName);
        respond(makeRpcResult(safeId,
                              makeToolResult(QStringLiteral("工具 %1 执行超时（%2 秒），"
                                                            "已放弃等待。")
                                                 .arg(toolName)
                                                 .arg(kToolTimeoutMs / 1000),
                                             true)));
    });

    auto *watcher = new QFutureWatcher<QPair<QString, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<QString, QString>>::finished, this,
            [this, watcher, timer, finished, respond, safeId, toolName]() {
                watcher->deleteLater();
                if (*finished)
                {
                    return;
                }
                *finished = true;
                timer->stop();
                timer->deleteLater();

                const QPair<QString, QString> outcome = watcher->result();
                QString text = outcome.first;
                const QString error = outcome.second;

                bool isError = !error.isEmpty();
                if (isError)
                {
                    text = error;
                }
                else if (text.trimmed().isEmpty())
                {
                    text = QStringLiteral("（%1 执行完成，但没有返回内容）").arg(toolName);
                }
                else if (text.startsWith(QStringLiteral("错误："))
                         || text.startsWith(QStringLiteral("启动失败"))
                         || text.startsWith(QStringLiteral("下载失败")))
                {
                    isError = true;
                }

                qInfo().noquote() << QStringLiteral("[MCP] 工具完成：%1%2")
                                         .arg(toolName, isError ? QStringLiteral("（失败）")
                                                                : QString());
                respond(makeRpcResult(safeId, makeToolResult(text, isError)));
            });
    watcher->setFuture(future);
    timer->start(kToolTimeoutMs);

    qInfo().noquote() << QStringLiteral("[MCP] 调用工具：%1").arg(toolName);
}

QJsonObject McpServer::makeToolResult(const QString &text, bool isError) const
{
    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), text);

    QJsonObject result;
    result.insert(QStringLiteral("content"), QJsonArray{content});
    result.insert(QStringLiteral("isError"), isError);
    return result;
}

QJsonObject McpServer::makeRpcResult(const QJsonValue &id, const QJsonObject &result) const
{
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("result"), result);
    return response;
}

QJsonObject McpServer::makeRpcError(const QJsonValue &id, int code,
                                    const QString &message) const
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);

    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id.isUndefined() ? QJsonValue() : id);
    response.insert(QStringLiteral("error"), error);
    return response;
}

void McpServer::writeHttp(QTcpSocket *socket, int status, const QByteArray &contentType,
                          const QByteArray &body)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
    {
        closeConnection(socket);
        return;
    }

    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(status) + " " + statusText(status) + "\r\n";
    response += "Content-Type: " + (contentType.isEmpty() ? QByteArray("application/json")
                                                          : contentType) + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type, Authorization, Mcp-Session-Id, "
                "MCP-Protocol-Version\r\n";
    response += "Access-Control-Expose-Headers: Mcp-Session-Id\r\n";
    response += "\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    closeConnection(socket);
}

void McpServer::writeSse(QTcpSocket *socket, const QByteArray &event, const QByteArray &data)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
    {
        return;
    }
    QByteArray payload;
    if (!event.isEmpty())
    {
        payload += "event: " + event + "\n";
    }
    payload += "data: " + data + "\n\n";
    socket->write(payload);
    socket->flush();
}

void McpServer::closeConnection(QTcpSocket *socket)
{
    if (!socket)
    {
        return;
    }
    if (Connection *conn = m_connections.take(socket))
    {
        delete conn;
    }
    socket->disconnectFromHost();
}

bool McpServer::checkAuth(const HttpRequest &req) const
{
    if (m_token.isEmpty())
    {
        return true;
    }

    QString provided;
    const QString authorization = req.headers.value(QStringLiteral("authorization")).trimmed();
    if (authorization.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive))
    {
        provided = authorization.mid(7).trimmed();
    }
    if (provided.isEmpty())
    {
        provided = req.query.value(QStringLiteral("token")).trimmed();
    }
    if (provided.isEmpty())
    {
        provided = req.headers.value(QStringLiteral("x-blockbox-token")).trimmed();
    }
    return provided == m_token;
}

bool McpServer::checkOrigin(const HttpRequest &req) const
{
    const QString origin = req.headers.value(QStringLiteral("origin")).trimmed();
    if (origin.isEmpty())
    {
        // 桌面 MCP 客户端通常不发 Origin
        return true;
    }
    if (origin == QStringLiteral("null"))
    {
        return false;
    }
    const QUrl url(origin);
    return url.isValid() && isLocalHostName(url.host());
}
