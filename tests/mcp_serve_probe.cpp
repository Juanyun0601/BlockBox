/**
 * @file   mcp_serve_probe.cpp
 * @brief  只启动 MCP 服务器的探针（给官方 SDK 兼容性测试用）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 用法：mcp_serve_probe.exe <port> <token> <allowWrite 0|1>
 * 启动后输出 READY <port>，随后进入事件循环，Ctrl+C / 关闭进程退出。
 */
#include <QApplication>
#include <QTextStream>

#include "utils/AiService.h"
#include "utils/mcp/McpServer.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const quint16 port = argc > 1 ? static_cast<quint16>(QString::fromLatin1(argv[1]).toUInt())
                                  : 17891;
    const QString token = argc > 2 ? QString::fromLatin1(argv[2])
                                   : QStringLiteral("serve-probe-token");
    const bool allowWrite = argc > 3 && QString::fromLatin1(argv[3]) == QStringLiteral("1");

    AiService service;
    McpServer server;
    server.setAiServiceProvider([&service]() { return &service; });

    QString error;
    if (!server.start(port, token, allowWrite, &error))
    {
        QTextStream(stderr) << "START-FAILED " << error << "\n";
        return 1;
    }

    QTextStream(stdout) << "READY " << server.port() << "\n";
    QTextStream(stdout).flush();
    return app.exec();
}
