/**
 * @file   mcp_server_probe.cpp
 * @brief  本地 MCP 服务器端到端探针（无界面）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 直接在进程内启动 McpServer + AiService，用真实 HTTP 请求走一遍：
 *   - /health
 *   - 鉴权失败 / 成功
 *   - initialize / notifications/initialized / tools/list / tools/call
 *   - 只读工具（list_instances / read_game_log）
 *   - 写工具 confirm 闸门与真正执行（manage_mods）
 *   - 关闭写权限后写工具不出现在 tools/list
 *   - 旧版 HTTP+SSE 传输（GET /sse + POST /messages）
 *
 * 通过时输出 MCP-PROBE-OK；失败时输出 MCP-PROBE-FAILED 并以 1 退出。
 */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "utils/AiService.h"
#include "utils/SettingsManager.h"
#include "utils/mcp/McpServer.h"

namespace {

int g_failures = 0;

void check(bool ok, const QString &what, const QString &detail = QString())
{
    QTextStream out(stdout);
    out << (ok ? "[PASS] " : "[FAIL] ") << what;
    if (!ok && !detail.isEmpty())
    {
        out << "  -> " << detail;
    }
    out << "\n";
    out.flush();
    if (!ok)
    {
        ++g_failures;
    }
}

struct HttpReply
{
    int status = 0;
    QByteArray body;
};

HttpReply sendRequest(const QString &method, const QUrl &url, const QByteArray &body,
                      const QString &token)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/event-stream");
    if (!token.isEmpty())
    {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }

    QNetworkReply *reply = method == QStringLiteral("GET") ? manager.get(request)
                                                          : manager.post(request, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();

    HttpReply result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    reply->deleteLater();
    return result;
}

QJsonObject parseJsonObject(const QByteArray &data)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        return QJsonObject();
    }
    return doc.object();
}

QJsonObject rpcCall(quint16 port, const QString &token, const QString &method,
                    const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("method"), method);
    request.insert(QStringLiteral("id"), id);
    if (!params.isEmpty())
    {
        request.insert(QStringLiteral("params"), params);
    }

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    const HttpReply reply = sendRequest(QStringLiteral("POST"), url,
                                        QJsonDocument(request).toJson(QJsonDocument::Compact),
                                        token);
    if (reply.status != 200)
    {
        return QJsonObject{{QStringLiteral("__http_status"), reply.status}};
    }
    return parseJsonObject(reply.body);
}

QString toolText(const QJsonObject &rpcResponse)
{
    const QJsonObject result = rpcResponse.value(QStringLiteral("result")).toObject();
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    if (content.isEmpty())
    {
        return QString();
    }
    return content.first().toObject().value(QStringLiteral("text")).toString();
}

bool toolIsError(const QJsonObject &rpcResponse)
{
    return rpcResponse.value(QStringLiteral("result")).toObject()
        .value(QStringLiteral("isError")).toBool();
}

QJsonObject toolArgs(QJsonObject args)
{
    QJsonObject params;
    params.insert(QStringLiteral("name"), args.take(QStringLiteral("__name")).toString());
    params.insert(QStringLiteral("arguments"), args);
    return params;
}

/** 等待 SSE 通道里出现指定标记（会先消费已缓冲的数据） */
QByteArray waitForSseData(QTcpSocket &socket, const QByteArray &marker, int timeoutMs,
                          QByteArray pending)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        pending += socket.readAll();
        if (pending.contains(marker))
        {
            return pending;
        }
        if (socket.state() != QAbstractSocket::ConnectedState)
        {
            break;
        }
        QEventLoop loop;
        QObject::connect(&socket, &QTcpSocket::readyRead, &loop, &QEventLoop::quit);
        QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
        QTimer::singleShot(qMin(200, timeoutMs - static_cast<int>(timer.elapsed())), &loop,
                           &QEventLoop::quit);
        loop.exec();
    }
    pending += socket.readAll();
    return pending;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QTextStream out(stdout);

    QTemporaryDir tempDir;
    check(tempDir.isValid(), QStringLiteral("创建临时实例目录"));
    const QString instancePath = tempDir.filePath(QStringLiteral("mcp_instance"));
    QDir().mkpath(instancePath + QStringLiteral("/mods"));
    QDir().mkpath(instancePath + QStringLiteral("/logs"));
    QDir().mkpath(instancePath + QStringLiteral("/crash-reports"));

    {
        QFile mod(instancePath + QStringLiteral("/mods/probe_mod.jar"));
        mod.open(QIODevice::WriteOnly);
        mod.write("probe");
    }
    {
        QFile log(instancePath + QStringLiteral("/logs/latest.log"));
        log.open(QIODevice::WriteOnly);
        log.write("[12:00:00] [main/INFO]: probe-log-line-alpha\n");
        log.write("[12:00:01] [main/INFO]: probe-log-line-omega\n");
    }

    AiService service;
    McpServer server;
    server.setAiServiceProvider([&service]() { return &service; });

    const QString token = QStringLiteral("probe-token-123");
    QString startError;
    check(server.start(0, token, true, &startError), QStringLiteral("启动 MCP 服务器"), startError);
    const quint16 port = server.port();
    check(port > 0, QStringLiteral("拿到监听端口"));
    out << "      port = " << port << "\n";
    out.flush();

    // ── /health ──
    {
        const HttpReply reply = sendRequest(
            QStringLiteral("GET"), QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(port)),
            QByteArray(), token);
        const QJsonObject health = parseJsonObject(reply.body);
        check(reply.status == 200 && health.value(QStringLiteral("ok")).toBool(),
              QStringLiteral("GET /health 返回 ok"));
        check(health.value(QStringLiteral("name")).toString() == QStringLiteral("blockbox-mcp"),
              QStringLiteral("/health 服务名正确"));
    }

    // ── 鉴权 ──
    {
        QJsonObject request;
        request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        request.insert(QStringLiteral("id"), 1);
        request.insert(QStringLiteral("method"), QStringLiteral("initialize"));
        const HttpReply reply = sendRequest(
            QStringLiteral("POST"), QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port)),
            QJsonDocument(request).toJson(QJsonDocument::Compact), QStringLiteral("wrong-token"));
        check(reply.status == 401, QStringLiteral("错误令牌被拒绝（401）"),
              QStringLiteral("status=%1").arg(reply.status));
    }

    // ── initialize ──
    {
        QJsonObject params;
        params.insert(QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18"));
        params.insert(QStringLiteral("capabilities"), QJsonObject());
        params.insert(QStringLiteral("clientInfo"),
                      QJsonObject{{QStringLiteral("name"), QStringLiteral("probe")},
                                  {QStringLiteral("version"), QStringLiteral("1.0")}});
        const QJsonObject response = rpcCall(port, token, QStringLiteral("initialize"), params, 2);
        const QJsonObject result = response.value(QStringLiteral("result")).toObject();
        const QJsonObject serverInfo = result.value(QStringLiteral("serverInfo")).toObject();
        check(serverInfo.value(QStringLiteral("name")).toString() == QStringLiteral("blockbox"),
              QStringLiteral("initialize 返回 blockbox serverInfo"));
        check(result.value(QStringLiteral("protocolVersion")).toString()
                  == QStringLiteral("2025-06-18"),
              QStringLiteral("initialize 协商协议版本"));
        check(result.value(QStringLiteral("capabilities")).toObject()
                  .contains(QStringLiteral("tools")),
              QStringLiteral("initialize 声明 tools 能力"));
    }

    // ── notifications/initialized 返回 202 ──
    {
        QJsonObject request;
        request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        request.insert(QStringLiteral("method"), QStringLiteral("notifications/initialized"));
        const HttpReply reply = sendRequest(
            QStringLiteral("POST"), QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port)),
            QJsonDocument(request).toJson(QJsonDocument::Compact), token);
        check(reply.status == 202, QStringLiteral("initialized 通知返回 202"),
              QStringLiteral("status=%1").arg(reply.status));
    }

    // ── tools/list ──
    {
        const QJsonObject response = rpcCall(port, token, QStringLiteral("tools/list"),
                                             QJsonObject(), 3);
        const QJsonArray tools = response.value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("tools")).toArray();
        QStringList names;
        for (const QJsonValue &value : tools)
        {
            names.append(value.toObject().value(QStringLiteral("name")).toString());
        }
        check(names.size() >= 20, QStringLiteral("tools/list 返回至少 20 个工具"),
              QStringLiteral("count=%1").arg(names.size()));
        check(names.contains(QStringLiteral("list_instances")),
              QStringLiteral("tools/list 包含 list_instances"));
        check(names.contains(QStringLiteral("manage_mods")),
              QStringLiteral("写权限开启时包含 manage_mods"));
        const QJsonObject manageTool = [&tools]() {
            for (const QJsonValue &value : tools)
            {
                const QJsonObject tool = value.toObject();
                if (tool.value(QStringLiteral("name")).toString()
                    == QStringLiteral("manage_mods"))
                {
                    return tool;
                }
            }
            return QJsonObject();
        }();
        check(manageTool.value(QStringLiteral("inputSchema")).toObject()
                  .value(QStringLiteral("properties")).toObject()
                  .contains(QStringLiteral("confirm")),
              QStringLiteral("写工具 schema 里带 confirm 参数"));
    }

    // ── 只读工具：read_game_log ──
    {
        QJsonObject args;
        args.insert(QStringLiteral("__name"), QStringLiteral("read_game_log"));
        args.insert(QStringLiteral("instance_path"), instancePath);
        args.insert(QStringLiteral("tail_lines"), 50);
        const QJsonObject response = rpcCall(port, token, QStringLiteral("tools/call"),
                                             toolArgs(args), 4);
        const QString text = toolText(response);
        check(!toolIsError(response) && text.contains(QStringLiteral("probe-log-line-alpha")),
              QStringLiteral("read_game_log 读到日志内容"), text.left(200));
    }

    // ── 只读工具：list_mods ──
    {
        QJsonObject args;
        args.insert(QStringLiteral("__name"), QStringLiteral("list_mods"));
        args.insert(QStringLiteral("instance_path"), instancePath);
        const QJsonObject response = rpcCall(port, token, QStringLiteral("tools/call"),
                                             toolArgs(args), 5);
        const QString text = toolText(response);
        check(!toolIsError(response) && text.contains(QStringLiteral("probe_mod.jar")),
              QStringLiteral("list_mods 扫到探针模组"), text.left(200));
    }

    // ── 写工具 confirm 闸门 ──
    {
        QJsonObject args;
        args.insert(QStringLiteral("__name"), QStringLiteral("manage_mods"));
        args.insert(QStringLiteral("instance_path"), instancePath);
        args.insert(QStringLiteral("action"), QStringLiteral("disable"));
        args.insert(QStringLiteral("file_names"), QJsonArray{QStringLiteral("probe_mod.jar")});
        const QJsonObject response = rpcCall(port, token, QStringLiteral("tools/call"),
                                             toolArgs(args), 6);
        check(toolIsError(response) && toolText(response).contains(QStringLiteral("confirm")),
              QStringLiteral("没有 confirm 时写工具被拦下"), toolText(response));
        check(QFile::exists(instancePath + QStringLiteral("/mods/probe_mod.jar")),
              QStringLiteral("被拦下时文件没有被改动"));
    }

    // ── 写工具真正执行 ──
    {
        QJsonObject args;
        args.insert(QStringLiteral("__name"), QStringLiteral("manage_mods"));
        args.insert(QStringLiteral("instance_path"), instancePath);
        args.insert(QStringLiteral("action"), QStringLiteral("disable"));
        args.insert(QStringLiteral("file_names"), QJsonArray{QStringLiteral("probe_mod.jar")});
        args.insert(QStringLiteral("confirm"), true);
        const QJsonObject response = rpcCall(port, token, QStringLiteral("tools/call"),
                                             toolArgs(args), 7);
        check(!toolIsError(response), QStringLiteral("confirm=true 时写工具执行成功"),
              toolText(response));
        check(QFile::exists(instancePath + QStringLiteral("/mods/probe_mod.jar.disabled")),
              QStringLiteral("manage_mods 把模组改名为 .disabled"));
    }

    // ── 未知工具 ──
    {
        QJsonObject args;
        args.insert(QStringLiteral("__name"), QStringLiteral("not_a_real_tool"));
        const QJsonObject response = rpcCall(port, token, QStringLiteral("tools/call"),
                                             toolArgs(args), 8);
        check(response.contains(QStringLiteral("error")),
              QStringLiteral("未知工具返回 JSON-RPC error"));
    }

    // ── 关闭写权限：tools/list 不再出现写工具，调用被拒 ──
    {
        server.setAllowWrite(false);
        const QJsonObject response = rpcCall(port, token, QStringLiteral("tools/list"),
                                             QJsonObject(), 9);
        const QJsonArray tools = response.value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("tools")).toArray();
        bool hasWriteTool = false;
        for (const QJsonValue &value : tools)
        {
            if (value.toObject().value(QStringLiteral("name")).toString()
                == QStringLiteral("manage_mods"))
            {
                hasWriteTool = true;
            }
        }
        check(!hasWriteTool, QStringLiteral("关闭写权限后 tools/list 不再列出 manage_mods"));

        QJsonObject args;
        args.insert(QStringLiteral("__name"), QStringLiteral("manage_mods"));
        args.insert(QStringLiteral("instance_path"), instancePath);
        args.insert(QStringLiteral("action"), QStringLiteral("enable"));
        args.insert(QStringLiteral("file_names"),
                    QJsonArray{QStringLiteral("probe_mod.jar.disabled")});
        args.insert(QStringLiteral("confirm"), true);
        const QJsonObject call = rpcCall(port, token, QStringLiteral("tools/call"),
                                         toolArgs(args), 10);
        check(toolIsError(call) && toolText(call).contains(QStringLiteral("写操作未开启")),
              QStringLiteral("写权限关闭时写工具被拒绝"), toolText(call));
        server.setAllowWrite(true);
    }

    // ── 旧版 HTTP+SSE 传输 ──
    {
        QTcpSocket sse;
        sse.connectToHost(QStringLiteral("127.0.0.1"), port);
        const bool connected = sse.waitForConnected(3000);
        check(connected, QStringLiteral("SSE 通道能连接"));
        if (connected)
        {
            QByteArray request;
            request += "GET /sse?token=" + token.toUtf8() + " HTTP/1.1\r\n";
            request += "Host: 127.0.0.1\r\n";
            request += "Accept: text/event-stream\r\n\r\n";
            sse.write(request);
            sse.waitForBytesWritten(1000);

            QByteArray data = waitForSseData(sse, "event: endpoint", 5000, QByteArray());
            check(data.contains("event: endpoint"), QStringLiteral("SSE 返回 endpoint 事件"),
                  data.left(200));

            const int endpointPos = data.indexOf("data: /messages");
            QString endpoint;
            if (endpointPos >= 0)
            {
                const int lineEnd = data.indexOf('\n', endpointPos);
                endpoint = QString::fromUtf8(
                    data.mid(endpointPos + 6, lineEnd - endpointPos - 6)).trimmed();
            }
            check(endpoint.startsWith(QStringLiteral("/messages?sessionId=")),
                  QStringLiteral("SSE endpoint 带 sessionId"), endpoint);

            if (!endpoint.isEmpty())
            {
                QJsonObject params;
                params.insert(QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05"));
                params.insert(QStringLiteral("capabilities"), QJsonObject());
                params.insert(QStringLiteral("clientInfo"),
                              QJsonObject{{QStringLiteral("name"), QStringLiteral("probe-sse")},
                                          {QStringLiteral("version"), QStringLiteral("1.0")}});
                QJsonObject message;
                message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
                message.insert(QStringLiteral("id"), 100);
                message.insert(QStringLiteral("method"), QStringLiteral("initialize"));
                message.insert(QStringLiteral("params"), params);

                const HttpReply ack = sendRequest(
                    QStringLiteral("POST"),
                    QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(endpoint)),
                    QJsonDocument(message).toJson(QJsonDocument::Compact), token);
                check(ack.status == 202, QStringLiteral("SSE 的 POST /messages 返回 202"),
                      QStringLiteral("status=%1").arg(ack.status));

                data = waitForSseData(sse, "event: message", 5000, data);
                check(data.contains("event: message"), QStringLiteral("SSE 通道收到响应"),
                      data.right(300));

                const int msgPos = data.indexOf("event: message");
                const int dataPos = data.indexOf("data: ", msgPos);
                const int dataEnd = data.indexOf('\n', dataPos);
                const QJsonObject sseResponse = parseJsonObject(
                    data.mid(dataPos + 6, dataEnd - dataPos - 6));
                check(sseResponse.value(QStringLiteral("result")).toObject()
                          .value(QStringLiteral("serverInfo")).toObject()
                          .value(QStringLiteral("name")).toString()
                          == QStringLiteral("blockbox"),
                      QStringLiteral("SSE 响应内容正确"));
            }
            sse.disconnectFromHost();
        }
    }

    server.stop();
    check(!server.isRunning(), QStringLiteral("停止服务器"));

    out << (g_failures == 0 ? "MCP-PROBE-OK" : "MCP-PROBE-FAILED") << "\n";
    out.flush();
    return g_failures == 0 ? 0 : 1;
}
