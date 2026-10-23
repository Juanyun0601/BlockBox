/**
 * @file   translation_service_probe.cpp
 * @brief  翻译服务真实行为探针：本地假服务端，验证请求地址 / 请求头 / 请求体 / 错误提示
 *
 * 覆盖：
 *  1. 端点自带 /v1 时不会被拼成 /v1/v1（这正是「请求失败」的常见根源）；
 *  2. 裸域名会被补成 /v1/chat/completions；
 *  3. 已经带完整路径的端点原样使用；
 *  4. 列表批量翻译请求里带着 [#n#] 标记、模型名与 Authorization；
 *  5. 服务端 401 时错误提示要包含 HTTP 状态与原因（方便用户自查密钥）。
 */

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QSettings>
#include <QUrl>

#include <cstdio>
#include <functional>

#include "utils/AiEndpointUrl.h"
#include "utils/translation/TranslationService.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void expect(bool ok, const QString &what)
{
    if (ok) {
        ++g_pass;
        std::fprintf(stderr, "[pass] %s\n", qPrintable(what));
    } else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s\n", qPrintable(what));
    }
}

/// 极简 HTTP/1.1 假服务端：只解析出第一条请求并回固定内容
class FakeServer
{
public:
    FakeServer()
    {
        QObject::connect(&server, &QTcpServer::newConnection, [this]() { onNewConnection(); });
    }

    bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return server.serverPort(); }

    QByteArray requestLine;
    QByteArray authorization;
    QByteArray body;
    QByteArray replyBody = QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}");
    int replyStatus = 200;
    int requestCount = 0;

private:
    void onNewConnection()
    {
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket)
            return;
        auto *buffer = new QByteArray;
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            const QByteArray head = buffer->left(headerEnd);
            const QByteArray rest = buffer->mid(headerEnd + 4);
            int contentLength = 0;
            requestLine = head.left(head.indexOf("\r\n"));
            const QList<QByteArray> lines = head.split('\n');
            for (const QByteArray &line : lines) {
                const QByteArray trimmed = line.trimmed();
                const QByteArray lower = trimmed.toLower();
                if (lower.startsWith("content-length:"))
                    contentLength = trimmed.mid(15).trimmed().toInt();
                else if (lower.startsWith("authorization:"))
                    authorization = trimmed.mid(14).trimmed();
            }
            if (rest.size() < contentLength)
                return;   // body 还没收全
            body = rest.left(contentLength);
            ++requestCount;

            QByteArray status = "200 OK";
            if (replyStatus == 401)
                status = "401 Unauthorized";
            else if (replyStatus == 500)
                status = "500 Internal Server Error";
            const QByteArray response = "HTTP/1.1 " + status
                + "\r\nContent-Type: application/json"
                + "\r\nContent-Length: " + QByteArray::number(replyBody.size())
                + "\r\nConnection: close\r\n\r\n" + replyBody;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

    QTcpServer server;
};

struct Outcome
{
    QString text;
    QString error;
    bool timedOut = false;
    bool finished = false;
};

Outcome runTranslate(TranslationService &service, const QString &payload,
                     const TranslationConfig &config, bool listMode)
{
    Outcome out;
    QEventLoop loop;
    QMetaObject::Connection finishedConn;
    QMetaObject::Connection errorConn;
    if (listMode) {
        finishedConn = QObject::connect(&service, &TranslationService::listTranslationFinished,
                                        [&out, &loop](const QString &text) {
                                            out.text = text;
                                            out.finished = true;
                                            loop.quit();
                                        });
    } else {
        finishedConn = QObject::connect(&service, &TranslationService::translationFinished,
                                        [&out, &loop](const QString &text) {
                                            out.text = text;
                                            out.finished = true;
                                            loop.quit();
                                        });
    }
    errorConn = QObject::connect(&service, &TranslationService::translationError,
                                 [&out, &loop](const QString &error) {
                                     out.error = error;
                                     loop.quit();
                                 });
    QTimer::singleShot(8000, &loop, [&out, &loop]() {
        out.timedOut = true;
        loop.quit();
    });

    if (listMode)
        service.translateList(payload, config);
    else
        service.translate(payload, config);

    loop.exec();
    QObject::disconnect(finishedConn);
    QObject::disconnect(errorConn);
    return out;
}

QString pathOf(const QByteArray &requestLine)
{
    const QList<QByteArray> parts = requestLine.split(' ');
    return parts.size() >= 2 ? QString::fromUtf8(parts.at(1)) : QString();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    FakeServer server;
    if (!server.listen()) {
        std::fprintf(stderr, "[FAIL] 无法监听本地端口\n");
        return 1;
    }
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.port());

    server.replyBody = QByteArrayLiteral(
        "{\"choices\":[{\"message\":{\"content\":\"[#0#] \\u4e92\\u8865\\u5149\\u5f71 ||| "
        "\\u9ad8\\u8d28\\u91cf\\u5149\\u7167\\u7684\\u5149\\u5f71\\u5305\"}}]}");

    // ── 1. 自带 /v1 的自定义端点：不能拼成 /v1/v1 ──
    {
        TranslationService service;
        TranslationConfig config;
        config.customApiUrl = base + QStringLiteral("/v1");
        config.customApiKey = QStringLiteral("test-key");
        config.customModel = QStringLiteral("test-model");

        const Outcome out = runTranslate(service, QStringLiteral("[#0#] Shadow ||| A pack"),
                                         config, true);
        const QString path = pathOf(server.requestLine);
        expect(!out.timedOut && out.error.isEmpty(),
               QStringLiteral("1 自带 /v1 的端点能拿到响应（错误=%1）").arg(out.error));
        expect(path == QStringLiteral("/v1/chat/completions"),
               QStringLiteral("1 请求路径 = %1（不能出现 /v1/v1）").arg(path));
        expect(server.authorization == QByteArrayLiteral("Bearer test-key"),
               QStringLiteral("1 带上了 Authorization"));
        const QJsonObject body = QJsonDocument::fromJson(server.body).object();
        expect(body.value(QStringLiteral("model")).toString() == QStringLiteral("test-model"),
               QStringLiteral("1 请求体用配置里的模型名"));
        const QString userContent = body.value(QStringLiteral("messages")).toArray()
                                        .at(1).toObject()
                                        .value(QStringLiteral("content")).toString();
        expect(userContent.contains(QStringLiteral("[#0#]")),
               QStringLiteral("1 请求体里保留了 [#n#] 标记"));
        expect(out.text.contains(QStringLiteral("互补光影")),
               QStringLiteral("1 解析出译文（%1）").arg(out.text.left(40)));
    }

    // ── 2. 裸域名：补成 /v1/chat/completions ──
    {
        TranslationService service;
        TranslationConfig config;
        config.customApiUrl = base;
        config.customApiKey = QStringLiteral("k");
        config.customModel = QStringLiteral("m");

        const Outcome out = runTranslate(service, QStringLiteral("[#0#] A ||| b"), config, true);
        const QString path = pathOf(server.requestLine);
        expect(!out.timedOut && out.error.isEmpty() && path == QStringLiteral("/v1/chat/completions"),
               QStringLiteral("2 裸域名补全路径 = %1").arg(path));
    }

    // ── 3. 已带完整路径：原样使用 ──
    {
        TranslationService service;
        TranslationConfig config;
        config.customApiUrl = base + QStringLiteral("/v1/chat/completions");
        config.customApiKey = QStringLiteral("k");
        config.customModel = QStringLiteral("m");

        const Outcome out = runTranslate(service, QStringLiteral("[#0#] A ||| b"), config, true);
        const QString path = pathOf(server.requestLine);
        expect(!out.timedOut && out.error.isEmpty() && path == QStringLiteral("/v1/chat/completions"),
               QStringLiteral("3 完整路径不被重复拼接 = %1").arg(path));
    }

    // ── 4. 结尾带斜杠的端点 ──
    {
        TranslationService service;
        TranslationConfig config;
        config.customApiUrl = base + QStringLiteral("/v1/");
        config.customApiKey = QStringLiteral("k");
        config.customModel = QStringLiteral("m");

        const Outcome out = runTranslate(service, QStringLiteral("[#0#] A ||| b"), config, true);
        const QString path = pathOf(server.requestLine);
        expect(!out.timedOut && out.error.isEmpty() && path == QStringLiteral("/v1/chat/completions"),
               QStringLiteral("4 末尾斜杠被吃掉 = %1").arg(path));
    }

    // ── 5. 服务端 401：错误提示要带状态码与原因 ──
    {
        server.replyStatus = 401;
        server.replyBody = QByteArrayLiteral(
            "{\"error\":{\"message\":\"Authentication Fails, Your api key is invalid\","
            "\"type\":\"authentication_error\"}}");
        TranslationService service;
        TranslationConfig config;
        config.customApiUrl = base + QStringLiteral("/v1");
        config.customApiKey = QStringLiteral("bad");
        config.customModel = QStringLiteral("m");

        const Outcome out = runTranslate(service, QStringLiteral("[#0#] A ||| b"), config, true);
        expect(out.error.contains(QStringLiteral("401"))
                   && out.error.contains(QStringLiteral("Authentication Fails")),
               QStringLiteral("5 401 提示可读：%1").arg(out.error));
        server.replyStatus = 200;
        server.replyBody = QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}");
    }

    // ── 6. 自定义 API 来源（translate()）同样走归一化 ──
    {
        TranslationService service;
        TranslationConfig config;
        config.source = TranslationSource::CustomAPI;
        config.customApiUrl = base + QStringLiteral("/v1");
        config.customApiKey = QStringLiteral("k");
        config.customModel = QStringLiteral("m");

        const Outcome out = runTranslate(service, QStringLiteral("hello"), config, false);
        const QString path = pathOf(server.requestLine);
        expect(!out.timedOut && out.error.isEmpty() && path == QStringLiteral("/v1/chat/completions"),
               QStringLiteral("6 自定义 API 来源路径 = %1").arg(path));
    }

    // ── 7. 端点归一化规则（纯函数，不依赖本机配置）──
    {
        const auto norm = [](const char *url) {
            return AiEndpointUrl::buildChatCompletionsUrl(QString::fromUtf8(url));
        };
        expect(norm("https://api.deepseek.com/v1")
                   == QStringLiteral("https://api.deepseek.com/v1/chat/completions"),
               QStringLiteral("7a deepseek /v1 补 chat/completions"));
        expect(norm("https://openrouter.ai/api/v1")
                   == QStringLiteral("https://openrouter.ai/api/v1/chat/completions"),
               QStringLiteral("7b openrouter /api/v1 只补一段"));
        expect(norm("https://api.openai.com/v1/")
                   == QStringLiteral("https://api.openai.com/v1/chat/completions"),
               QStringLiteral("7c 末尾斜杠先去掉"));
        expect(norm("https://x.dev/v1/chat/completions")
                   == QStringLiteral("https://x.dev/v1/chat/completions"),
               QStringLiteral("7d 已带完整路径原样返回"));
        expect(norm("https://x.dev")
                   == QStringLiteral("https://x.dev/v1/chat/completions"),
               QStringLiteral("7e 裸域名补 /v1/chat/completions"));
        expect(norm("https://open.bigmodel.cn/api/paas/v4")
                   == QStringLiteral("https://open.bigmodel.cn/api/paas/v4/chat/completions"),
               QStringLiteral("7f 非 v1 的版本段也能识别"));
    }

    // ── 8. 模型没按格式翻译（例如选了安全审核模型）：要报错而不是写回乱码 ──
    {
        server.replyStatus = 200;
        server.replyBody = QByteArrayLiteral(
            "{\"choices\":[{\"message\":{\"content\":\"User Safety: safe\"}}]}");
        TranslationService service;
        TranslationConfig config;
        config.customApiUrl = base;
        config.customApiKey = QStringLiteral("k");
        config.customModel = QStringLiteral("content-safety");

        const Outcome out = runTranslate(service, QStringLiteral("[#0#] A ||| b"), config, true);
        expect(out.error.contains(QStringLiteral("对话模型")),
               QStringLiteral("8 非翻译内容被拦下：%1").arg(out.error.left(50)));
        server.replyBody = QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}");
    }

    // ── 9. 详情页（单段）拿到「User Safety: safe」这类非译文：报错而不是把它当译文写上页面 ──
    {
        server.replyStatus = 200;
        server.replyBody = QByteArrayLiteral(
            "{\"choices\":[{\"message\":{\"content\":\"User Safety: safe\"}}]}");
        TranslationService service;
        TranslationConfig config;
        config.customApiUrl = base + QStringLiteral("/v1");
        config.customApiKey = QStringLiteral("k");
        config.customModel = QStringLiteral("content-safety");

        const Outcome out = runTranslate(service, QStringLiteral("Create is a mod about machines."),
                                         config, false);
        expect(out.text.isEmpty() && out.error.contains(QStringLiteral("对话模型")),
               QStringLiteral("9 单段非译文被拦下：%1").arg(out.error.left(50)));
    }

    // ── 10. 详情页正常译成中文：放行 ──
    {
        server.replyBody = QByteArrayLiteral(
            "{\"choices\":[{\"message\":{\"content\":\"Create 是一个关于机器的模组。\"}}]}");
        TranslationService service;
        TranslationConfig config;
        config.customApiUrl = base + QStringLiteral("/v1");
        config.customApiKey = QStringLiteral("k");
        config.customModel = QStringLiteral("chat-model");

        const Outcome out = runTranslate(service, QStringLiteral("Create is a mod about machines."),
                                         config, false);
        expect(!out.timedOut && out.error.isEmpty()
                   && out.text.contains(QStringLiteral("机器")),
               QStringLiteral("10 中文译文正常返回：%1").arg(out.text.left(30)));
    }
    std::fprintf(stderr, "\n[summary] pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
