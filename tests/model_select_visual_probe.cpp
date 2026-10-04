/**
 * @file   model_select_visual_probe.cpp
 * @brief  模型设置窗口（ModelSelectDialog）列表样式与交互探针
 *
 * 直接链接已编译的工程目标文件（build 目录 debug/*.o，排除 main.o），
 * 运行真实的 ModelSelectDialog，验证：
 *   1) 列表项样式截图：model_select_light.png / model_select_dark.png
 *   2) 悬浮展开服务商元数据（由本地 HTTP 桩服务器模拟 /models 返回）
 *   3) 左键点击列表项 → 发出 modelSelected 且对话框以 Accepted 关闭
 *   4) 右键点击非本地模型行 → 弹出含「删除」的菜单（自动关闭，不执行删除）
 *   5) 右键点击本地模型行 → 不弹菜单
 */
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QHostAddress>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <cstdio>

#include "components/ModelSelectDialog.h"
#include "utils/AiService.h"
#include "utils/ThemeManager.h"

namespace {

int g_failures = 0;

void check(bool cond, const char* msg)
{
    std::fprintf(stderr, "[%s] %s\n", cond ? "ok" : "FAIL", msg);
    if (!cond)
        ++g_failures;
}

QWidget* findRow(ModelSelectDialog* dlg, const QString& id)
{
    const auto rows = dlg->findChildren<QWidget*>(QStringLiteral("modelConfiguredItem"));
    for (QWidget* row : rows)
    {
        if (row->property("modelId").toString() == id)
            return row;
    }
    return nullptr;
}

void sendMouseButton(QWidget* target, Qt::MouseButton button)
{
    const QPointF pos(12.0, 12.0);
    QMouseEvent press(QEvent::MouseButtonPress, pos, pos, pos, button, button, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
}

void sendEnterLeave(QWidget* target, QEvent::Type type)
{
    QEvent ev(type);
    QApplication::sendEvent(target, &ev);
}

/**
 * @brief 服务商 /models 桩服务器（OpenAI 兼容 data[] 结构）
 *
 * 返回 deepseek-chat（64K 纯文本）、gpt-4o（128K 文本+图片），
 * qwen-max 不在列表中（无元数据，仅目录推理标记兜底）。
 */
class ModelMetaStub
{
public:
    bool start()
    {
        m_body = R"({"data":[)"
                 R"({"id":"deepseek-chat","context_length":65536,)"
                 R"("architecture":{"modality":"text->text"},"supported_parameters":["chat"]},)"
                 R"({"id":"gpt-4o","context_length":131072,)"
                 R"("architecture":{"modality":"text+image->text"},"supported_parameters":["chat"]})"
                 R"(]})";

        QObject::connect(&m_server, &QTcpServer::newConnection, [this]() {
            while (QTcpSocket* sock = m_server.nextPendingConnection())
            {
                const QByteArray body = m_body;
                QObject::connect(sock, &QTcpSocket::readyRead, sock,
                                 [sock, body, req = QByteArray(), responded = false]() mutable {
                    if (responded)
                        return;
                    req += sock->readAll();
                    if (!req.contains("\r\n\r\n"))
                        return;
                    responded = true;
                    const QByteArray resp =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                        "Connection: close\r\n\r\n" + body;
                    sock->write(resp);
                    sock->disconnectFromHost();
                });
                QObject::connect(sock, &QTcpSocket::disconnected,
                                 sock, &QObject::deleteLater);
            }
        });
        return m_server.listen(QHostAddress::LocalHost);
    }

    quint16 port() const { return m_server.serverPort(); }

private:
    QTcpServer m_server;
    QByteArray m_body;
};

QList<AiModel> makeModels(const QString& cloudBaseUrl)
{
    AiModel m1;
    m1.id = QStringLiteral("deepseek-chat");
    m1.displayName = QStringLiteral("DeepSeek Chat");
    m1.apiUrl = cloudBaseUrl;
    m1.apiKey = QStringLiteral("sk-test-1");

    AiModel m2;
    m2.id = QStringLiteral("gpt-4o");
    m2.displayName = QStringLiteral("GPT-4o");
    m2.apiUrl = cloudBaseUrl;
    m2.apiKey = QStringLiteral("sk-test-2");

    AiModel m3;
    m3.id = QStringLiteral("qwen-max");
    m3.displayName = QStringLiteral("Qwen Max");
    m3.apiUrl = cloudBaseUrl;
    m3.apiKey = QStringLiteral("sk-test-3");
    m3.supportsThinking = true;   // 服务商无返回时的目录标记兜底

    AiModel local;
    local.id = QStringLiteral("qwen3:4b");
    local.displayName = QStringLiteral("qwen3:4b");
    local.apiUrl = QStringLiteral("http://localhost:11434/v1");
    local.apiKey = QStringLiteral("ollama"); // 与 LocalModelDialog 一致：Ollama 不校验 key 但需非空
    local.isLocal = true;

    return {m1, m2, m3, local};
}

/// 等待某行的悬浮信息文本就绪（服务商 /models 异步返回）
bool waitForMetaText(QApplication& app, ModelSelectDialog* dlg, const QString& modelId,
                     const QString& expected, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        app.processEvents();
        QWidget* row = findRow(dlg, modelId);
        if (row)
        {
            if (auto* info = row->findChild<QLabel*>(QStringLiteral("modelMetaInfo")))
            {
                if (info->text() == expected)
                    return true;
            }
        }
        QThread::msleep(10);
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // 本地桩服务器模拟服务商 /models 返回
    ModelMetaStub stub;
    if (!stub.start())
    {
        std::fprintf(stderr, "cannot start meta stub server\n");
        return 2;
    }
    const QString cloudBase =
        QStringLiteral("http://127.0.0.1:%1/v1").arg(stub.port());

    auto* dlg = new ModelSelectDialog;
    dlg->setCurrentModel(QStringLiteral("deepseek-chat"));
    dlg->setModels(makeModels(cloudBase));

    // ---- 截图：浅色 / 深色 ----
    struct Shot { const char* name; ThemeManager::ThemeType theme; };
    const Shot shots[] = {
        {"model_select_light.png", ThemeManager::LightTheme},
        {"model_select_dark.png",  ThemeManager::DarkTheme},
    };
    for (const Shot& s : shots)
    {
        ThemeManager::setTheme(&app, s.theme);
        dlg->show();
        app.processEvents();
        app.processEvents();
        const QString out = QStringLiteral("D:/BlockBox/_out/") + QLatin1String(s.name);
        const bool ok = dlg->grab().save(out);
        std::fprintf(stderr, "[%s] shot %s\n", ok ? "ok" : "FAIL", qUtf8Printable(out));
        if (!ok)
            ++g_failures;
        dlg->hide();
    }

    // ---- 行为 1：左键点击 → 切换并关闭 ----
    ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
    QString selectedId;
    QObject::connect(dlg, &ModelSelectDialog::modelSelected, dlg,
                     [&selectedId](const AiModel& m) { selectedId = m.id; });

    dlg->show();
    app.processEvents();

    // ---- 行为 0：悬浮 → 右侧展开服务商元数据 ----
    check(waitForMetaText(app, dlg, QStringLiteral("deepseek-chat"),
                          QStringLiteral("上下文 64K · 支持：文本")),
          "deepseek meta from provider /models");
    check(waitForMetaText(app, dlg, QStringLiteral("gpt-4o"),
                          QStringLiteral("上下文 128K · 支持：文本、图片")),
          "gpt-4o meta from provider /models");
    check(waitForMetaText(app, dlg, QStringLiteral("qwen-max"),
                          QStringLiteral("支持推理")),
          "qwen-max falls back to catalog thinking flag");

    QWidget* rowHover = findRow(dlg, QStringLiteral("deepseek-chat"));
    check(rowHover != nullptr, "hover target row exists");
    if (rowHover)
    {
        auto* info = rowHover->findChild<QLabel*>(QStringLiteral("modelMetaInfo"));
        check(info != nullptr, "meta label exists");
        if (info)
        {
            check(!info->isVisible(), "meta hidden by default");
            sendEnterLeave(rowHover, QEvent::Enter);
            check(info->isVisible(), "meta visible on hover");
            check(info->text() == QStringLiteral("上下文 64K · 支持：文本"),
                  "deepseek-chat meta text");

            const QString hoverShot = QStringLiteral("D:/BlockBox/_out/model_select_hover.png");
            const bool ok = dlg->grab().save(hoverShot);
            std::fprintf(stderr, "[%s] shot %s\n", ok ? "ok" : "FAIL", qUtf8Printable(hoverShot));
            if (!ok)
                ++g_failures;

            sendEnterLeave(rowHover, QEvent::Leave);
            check(!info->isVisible(), "meta hidden after leave");
        }

        // gpt-4o：图片输入
        sendEnterLeave(rowHover, QEvent::Leave);
    }
    QWidget* rowVision = findRow(dlg, QStringLiteral("gpt-4o"));
    if (rowVision)
    {
        auto* info = rowVision->findChild<QLabel*>(QStringLiteral("modelMetaInfo"));
        if (info)
        {
            sendEnterLeave(rowVision, QEvent::Enter);
            check(info->text() == QStringLiteral("上下文 128K · 支持：文本、图片"),
                  "gpt-4o meta text");
            sendEnterLeave(rowVision, QEvent::Leave);
        }
    }

    QWidget* row2 = findRow(dlg, QStringLiteral("gpt-4o"));
    check(row2 != nullptr, "row gpt-4o exists");
    if (row2)
    {
        sendMouseButton(row2, Qt::LeftButton);
        check(selectedId == QStringLiteral("gpt-4o"), "left click selects gpt-4o");
        check(dlg->result() == QDialog::Accepted, "dialog closed with Accepted");
        check(!dlg->isVisible(), "dialog hidden after left click");
    }

    // ---- 行为 2：右键非本地模型 → 含「删除」的菜单，自动关闭不执行 ----
    selectedId.clear();
    dlg->show();
    app.processEvents();

    QWidget* row1 = findRow(dlg, QStringLiteral("deepseek-chat"));
    check(row1 != nullptr, "row deepseek-chat exists");
    QStringList menuTexts;
    if (row1)
    {
        QTimer::singleShot(80, [&]() {
            if (QWidget* popup = QApplication::activePopupWidget())
            {
                if (auto* menu = qobject_cast<QMenu*>(popup))
                {
                    const auto actions = menu->actions();
                    for (QAction* a : actions)
                        menuTexts << a->text();
                }
                popup->close();
            }
        });
        sendMouseButton(row1, Qt::RightButton);
        check(menuTexts.contains(QStringLiteral("删除")), "context menu has 删除");
        check(dlg->isVisible(), "dialog stays open after right-click menu");
        check(selectedId.isEmpty(), "right click does not switch model");

        // 删除动作未被选择：模型仍应保持已配置
        bool stillConfigured = false;
        for (const AiModel& m : dlg->allModels())
        {
            if (m.id == QStringLiteral("deepseek-chat") && !m.apiKey.isEmpty())
                stillConfigured = true;
        }
        check(stillConfigured, "model still configured (menu dismissed)");
    }

    // ---- 行为 3：右键本地模型 → 不弹菜单 ----
    QWidget* localRow = findRow(dlg, QStringLiteral("qwen3:4b"));
    check(localRow != nullptr, "local model row exists");
    if (localRow)
    {
        bool popupSeen = false;
        QTimer::singleShot(80, [&]() {
            if (QApplication::activePopupWidget())
                popupSeen = true;
            if (QWidget* popup = QApplication::activePopupWidget())
                popup->close();
        });
        sendMouseButton(localRow, Qt::RightButton);
        check(!popupSeen, "no context menu for local model");
    }

    dlg->close();
    QTimer::singleShot(100, &app, &QCoreApplication::quit);
    app.exec();

    std::fprintf(stderr, "== %s (%d failures) ==\n", g_failures == 0 ? "PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
