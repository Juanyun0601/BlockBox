/**
 * @file   McpServer.h
 * @brief  方块盒子本地 MCP（Model Context Protocol）服务器
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 把启动器已有的 AI 工作模式工具通过本地 HTTP 暴露给外部 AI 客户端
 * （Claude Code / Cursor / Cherry Studio 等），客户端只需填 http://127.0.0.1:<端口>/mcp。
 *
 * 传输：
 *   - Streamable HTTP（2025-03-26 / 2025-06-18）：POST /mcp，响应 application/json
 *   - 旧版 HTTP+SSE（2024-11-05）：GET /sse + POST /messages?sessionId=...
 *   - GET /health：供排查连接是否正常
 *
 * 安全：
 *   - 只监听 127.0.0.1
 *   - 除「无 Origin 的本地客户端」外，只放行 localhost / 127.0.0.1 的 Origin（防 DNS rebinding）
 *   - 需要访问令牌（Authorization: Bearer <token> 或 ?token=）
 *   - 写操作默认关闭，开启后仍需每次传 confirm=true
 */
#ifndef BLOCKBOX_MCP_SERVER_H
#define BLOCKBOX_MCP_SERVER_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class AiService;
class QTcpServer;
class QTcpSocket;
class QTimer;

class McpServer : public QObject
{
    Q_OBJECT

public:
    explicit McpServer(QObject *parent = nullptr);
    ~McpServer() override;

    /** 设置 AI 服务提供者：MCP 工具复用启动器里正在运行的 AiService 实例 */
    void setAiServiceProvider(std::function<AiService *()> provider);

    /**
     * @brief 启动（或重启）HTTP 服务
     * @param port         监听端口（1~65535；0 表示由系统分配，仅探针使用）
     * @param token        访问令牌（空表示不校验，仅建议在探针里用）
     * @param allowWrite   是否允许写操作
     * @param errorMessage 失败原因
     */
    bool start(quint16 port, const QString &token, bool allowWrite,
               QString *errorMessage = nullptr);

    /** 停止服务并断开所有连接 */
    void stop();

    bool isRunning() const;
    quint16 port() const { return m_port; }

    /** 运行时更新令牌 / 写开关（无需重启监听） */
    void setToken(const QString &token) { m_token = token; }
    void setAllowWrite(bool allowWrite) { m_allowWrite = allowWrite; }
    QString token() const { return m_token; }
    bool allowWrite() const { return m_allowWrite; }

signals:
    /**
     * @brief 服务状态变化
     * @param running 是否正在监听
     * @param port    实际端口（未运行时为 0）
     * @param detail  失败原因或附加说明（成功时为空）
     */
    void statusChanged(bool running, quint16 port, const QString &detail);

private slots:
    void onNewConnection();

private:
    struct Connection
    {
        QByteArray buffer;
        bool headerParsed = false;
        bool complete = false;
        bool chunked = false;
        qint64 contentLength = -1;
        QString method;
        QString path;
        QHash<QString, QString> headers;
        QHash<QString, QString> query;
        QByteArray body;
    };

    struct HttpRequest
    {
        QString method;
        QString path;
        QHash<QString, QString> headers;
        QHash<QString, QString> query;
        QByteArray body;
    };

    struct SseSession
    {
        QPointer<QTcpSocket> socket;
        QString id;
    };

    void onReadyRead(QTcpSocket *socket);

    /** 解析缓冲区中的请求；返回 true 表示已完整；err 非空表示格式错误 */
    bool parseRequest(Connection &conn, QString &err);

    void dispatchHttp(QTcpSocket *socket, const HttpRequest &req);
    void handleStreamable(QTcpSocket *socket, const HttpRequest &req);
    void handleSseConnect(QTcpSocket *socket, const HttpRequest &req);
    void handleSseMessage(QTcpSocket *socket, const HttpRequest &req);
    void handleHealth(QTcpSocket *socket, const HttpRequest &req);

    void processRpc(const QJsonObject &request,
                    const std::function<void(const QJsonObject &)> &respond);
    void callTool(const QJsonValue &requestId, const QJsonObject &params,
                  const std::function<void(const QJsonObject &)> &respond);

    QJsonObject makeToolResult(const QString &text, bool isError) const;
    QJsonObject makeRpcResult(const QJsonValue &id, const QJsonObject &result) const;
    QJsonObject makeRpcError(const QJsonValue &id, int code, const QString &message) const;

    void writeHttp(QTcpSocket *socket, int status, const QByteArray &contentType,
                   const QByteArray &body);
    void writeSse(QTcpSocket *socket, const QByteArray &event, const QByteArray &data);
    void closeConnection(QTcpSocket *socket);

    bool checkAuth(const HttpRequest &req) const;
    bool checkOrigin(const HttpRequest &req) const;

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QString m_token;
    bool m_allowWrite = false;
    std::function<AiService *()> m_aiProvider;
    QMutex m_writeMutex;

    QHash<QTcpSocket *, Connection *> m_connections;
    QHash<QString, SseSession *> m_sseSessions;
};

#endif // BLOCKBOX_MCP_SERVER_H
