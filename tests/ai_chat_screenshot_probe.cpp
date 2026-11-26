/**
 * @file   ai_chat_screenshot_probe.cpp
 * @brief  实例助手 AI 页「只聊天模式 + 截图游戏画面」端到端探针
 *
 * 复现实例助手里的小窗 AI 页（450×760 紧凑模式），逐项确认：
 *   1. 「聊天 / 工作」模式切换条整条隐藏，当前模式固定为聊天；
 *   2. 输入栏里出现「截图」按钮，且位于引用按钮左侧；
 *   3. 真点一次截图按钮：走完整截屏链路（会临时藏窗口），打印提示文字；
 *   4. 注入一张假截图后：缩略图条出现、缩略图数量正确、空输入也能发送；
 *   5. 用户消息气泡里能正确回显截图；
 *   6. 清空截图后缩略图条重新隐藏。
 * 每一步都存一张 PNG 到 probe_out/，便于肉眼核对排版。
 */

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "utils/GameScreenshot.h"

// 探针需要读页面内部状态（模式条 / 待发送截图 / 发送按钮），沿用其他探针的做法
#define private public
#include "utils/ThemeManager.h"
#include "pages/AiChatPage.h"
#undef private

namespace
{
void pump()
{
    for (int i = 0; i < 8; ++i)
    {
        QApplication::processEvents();
    }
}

void waitMs(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

void report(const char *tag, bool ok)
{
    fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", tag);
}

/// 造一张有明暗层次的假截图（纯色图会被判成无效画面）
QImage makeFakeScreenshot()
{
    QImage img(1280, 720, QImage::Format_RGB32);
    QPainter painter(&img);
    QLinearGradient sky(0, 0, 0, img.height());
    sky.setColorAt(0.0, QColor(120, 180, 255));
    sky.setColorAt(1.0, QColor(30, 60, 120));
    painter.fillRect(img.rect(), sky);
    painter.fillRect(0, 520, img.width(), 200, QColor(80, 130, 60));
    painter.fillRect(180, 380, 220, 140, QColor(140, 100, 60));
    painter.fillRect(760, 300, 260, 220, QColor(90, 90, 95));
    painter.setPen(Qt::white);
    painter.drawText(40, 60, QStringLiteral("FAKE GAME FRAME"));
    painter.end();
    return img;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    for (const QString &path : {QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                                QStringLiteral("C:/Windows/Fonts/msyhbd.ttc")})
    {
        QFontDatabase::addApplicationFont(path);
    }
    QFont probeFont(QStringLiteral("Microsoft YaHei"));
    probeFont.setPixelSize(13);
    app.setFont(probeFont);

    QDir().mkpath(QStringLiteral("probe_out"));
    ThemeManager::instance()->applyCurrentThemeStyle();

    // ── 搭一个与实例助手窗口同宽的宿主 ──
    auto *host = new QWidget;
    host->setObjectName(QStringLiteral("probeHost"));
    host->resize(450, 760);
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto *page = new AiChatPage(host);
    hostLayout->addWidget(page, 1);
    page->setCompactMode(true);
    page->setChatOnlyMode(true);
    page->setGameScreenshotEnabled(true);

    host->show();
    pump();
    waitMs(500);
    pump();
    fprintf(stderr, "[info] 初始窗口尺寸 %dx%d\n", host->width(), host->height());
    {
        struct ProbeItem { const char *name; QWidget *widget; };
        const ProbeItem items[] = {
            {"inputPanel", page->m_inputPanel},
            {"inputBox", page->m_inputBox},
            {"toolsArea", page->m_toolsArea},
            {"imageStrip", page->m_imageStrip},
            {"imageRow", page->m_imageRow},
            {"imageHint", page->m_imageStripHint},
            {"messageArea", page->m_messageArea},
            {"welcome", page->m_welcomeWidget},
            {"topBar", page->m_topBar},
            {"inputEdit", page->m_inputEdit},
        };
        for (const ProbeItem &item : items)
        {
            if (!item.widget)
            {
                continue;
            }
            fprintf(stderr, "[size] %-12s min=%4d sizeHint=%4d visible=%d\n",
                    item.name, item.widget->minimumSizeHint().width(),
                    item.widget->sizeHint().width(),
                    item.widget->isVisible() ? 1 : 0);
        }
        fprintf(stderr, "[size] page min=%d hint=%d\n",
                page->minimumSizeHint().width(), page->sizeHint().width());
    }

    // ── 1. 只聊天模式：模式切换条隐藏，模式固定为聊天 ──
    // 模式切换按钮在「对话列表气泡」头部，先把气泡展开再判断，避免误判成「本来就是隐藏的」
    page->setConvListExpanded(true, false);
    waitMs(300);
    pump();
    report("modeBar hidden", page->m_modeBar && !page->m_modeBar->isVisible());
    report("work mode button unreachable",
           page->m_modeWorkBtn && !page->m_modeWorkBtn->isVisible());
    report("current mode == chat (0)", page->m_currentMode == 0);
    host->grab().save(QStringLiteral("probe_out/ai_shot_00_conv_expanded.png"));
    page->setConvListExpanded(false, false);
    waitMs(200);
    pump();
    // 展开气泡期间窗口被撑宽过一次，这里拉回实例助手的真实宽度再继续
    host->resize(450, 760);
    waitMs(150);
    pump();
    fprintf(stderr, "[info] 对话气泡展开/收起后窗口尺寸 %dx%d\n", host->width(), host->height());

    // ── 2. 截图按钮出现在输入栏 ──
    report("screenshot button exists", page->m_screenshotBtn != nullptr);
    report("screenshot button visible",
           page->m_screenshotBtn && page->m_screenshotBtn->isVisible());

    // 截图按钮与引用按钮的先后顺序（截图应在引用左侧）
    const QList<QWidget *> tools = page->m_toolsArea->findChildren<QWidget *>();
    int shotX = -1;
    int refX = -1;
    for (QWidget *widget : tools)
    {
        if (widget == page->m_screenshotBtn)
        {
            shotX = widget->x();
        }
        else if (widget == page->m_referenceBtn)
        {
            refX = widget->x();
        }
    }
    fprintf(stderr, "[info] 截图按钮 x=%d，引用按钮 x=%d\n", shotX, refX);
    report("screenshot button left of reference", shotX >= 0 && refX >= 0 && shotX < refX);

    // 缩略图条初始不占位
    report("image strip hidden before capture",
           page->m_imageStrip && !page->m_imageStrip->isVisible());

    host->grab().save(QStringLiteral("probe_out/ai_shot_01_before.png"));

    // ── 3. 真点一次截图按钮：走一遍真实截屏链路 ──
    if (page->m_screenshotBtn)
    {
        page->m_screenshotBtn->click();
    }
    waitMs(1400);
    pump();
    const QString hintAfterRealClick = page->m_imageStripHint->text();
    fprintf(stderr, "[info] 真实截图结果：hint=\"%s\"，待发送截图 %d 张\n",
            hintAfterRealClick.toUtf8().constData(), int(page->m_pendingImages.size()));
    fprintf(stderr, "[info] 截图后窗口尺寸 %dx%d\n", host->width(), host->height());
    fprintf(stderr, "[info] 窗口在截图后仍然可见：%s\n",
            host->isVisible() ? "是" : "否");
    report("host window restored after capture", host->isVisible());
    host->grab().save(QStringLiteral("probe_out/ai_shot_02_real_click.png"));
    page->clearPendingImages();
    page->showImageHint(QString());

    // ── 4. 注入一张假截图：缩略图条 / 只发图片也能发 ──
    QByteArray jpeg;
    const QString dataUrl = GameScreenshot::toJpegDataUrl(makeFakeScreenshot(), &jpeg);
    fprintf(stderr, "[info] JPEG %d 字节，dataUrl 前缀 %.32s…\n",
            int(jpeg.size()), dataUrl.toUtf8().constData());
    report("jpeg data url built", dataUrl.startsWith(QStringLiteral("data:image/jpeg;base64,")));
    report("jpeg size reasonable (< 900KB)", jpeg.size() <= 900 * 1024);

    ChatImage fake;
    fake.dataUrl = dataUrl;
    fake.width = 1280;
    fake.height = 720;
    // 缓存文件：真实流程由 AiChatPage 自己写，这里直接落一份供缩略图读取
    {
        QFile cacheFile(QStringLiteral("probe_out/ai_shot_fake.jpg"));
        if (cacheFile.open(QIODevice::WriteOnly))
        {
            cacheFile.write(jpeg);
            cacheFile.close();
            fake.cachePath = QDir::current().absoluteFilePath(cacheFile.fileName());
        }
    }

    page->m_pendingImages.append(fake);
    page->rebuildImageStrip();
    waitMs(250);
    pump();

    const int chips = page->m_imageRow->findChildren<QPushButton *>(
                          QStringLiteral("aiChatImageChip")).size();
    int visibleChips = 0;
    for (QPushButton *chip : page->m_imageRow->findChildren<QPushButton *>(
             QStringLiteral("aiChatImageChip")))
    {
        if (chip->isVisible())
        {
            ++visibleChips;
        }
    }
    fprintf(stderr, "[info] 缩略图按钮 %d 个（可见 %d 个，应为 1 个）\n", chips, visibleChips);
    report("image strip visible with pending image",
           page->m_imageStrip && page->m_imageStrip->isVisible());
    report("thumbnail chip created", visibleChips == 1);
    report("send button enabled with image only (empty input)",
           page->m_sendBtn && page->m_sendBtn->isEnabled());
    host->grab().save(QStringLiteral("probe_out/ai_shot_03_pending.png"));

    // ── 5. 用户消息气泡里回显截图 ──
    QWidget *bubble = page->createMessageWidget(QStringLiteral("帮我看看这个刷怪塔为什么刷不出怪"),
                                                true, {}, page->m_pendingImages);
    page->m_messageLayout->insertWidget(page->m_messageLayout->count() - 1, bubble);
    waitMs(250);
    pump();
    const int bubbleChips = bubble->findChildren<QPushButton *>(
                                QStringLiteral("aiChatImageChip")).size();
    fprintf(stderr, "[info] 气泡内图片 %d 个（应为 1 个）\n", bubbleChips);
    report("user bubble renders image", bubbleChips == 1);
    page->grab().save(QStringLiteral("probe_out/ai_shot_04_bubble.png"));

    // ── 6. 清空截图后缩略图条收起 ──
    page->clearPendingImages();
    page->showImageHint(QString());
    waitMs(200);
    pump();
    report("image strip hidden again after clear",
           page->m_imageStrip && !page->m_imageStrip->isVisible());
    report("send button disabled with empty input and no image",
           page->m_sendBtn && !page->m_sendBtn->isEnabled());
    host->grab().save(QStringLiteral("probe_out/ai_shot_05_after_clear.png"));

    // ── 7. 请求体：带图消息要拼成「文本 + image_url」的多模态分段 ──
    {
        AiModel probeModel;
        probeModel.id = QStringLiteral("probe-vision");
        probeModel.displayName = QStringLiteral("Probe Vision");
        probeModel.apiUrl = QStringLiteral("http://127.0.0.1:1");  // 一定连不上，只为拿到已拼好的请求体
        probeModel.apiKey = QStringLiteral("probe-key");

        ChatMessage probeMsg;
        probeMsg.role = QStringLiteral("user");
        probeMsg.content = QStringLiteral("看看这个刷怪塔");
        probeMsg.images.append(fake);

        QList<ChatMessage> probeHistory;
        probeHistory.append(probeMsg);

        auto *service = new AiService;
        service->sendMessage(probeHistory, probeModel, WebSearchMode::Off,
                             AssistantMode::Chat, QString());
        waitMs(500);

        bool textPartOk = false;
        bool imagePartOk = false;
        int partCount = 0;
        for (const QJsonValue &value : service->m_pendingMessages)
        {
            const QJsonObject obj = value.toObject();
            if (obj.value(QStringLiteral("role")).toString() != QStringLiteral("user"))
            {
                continue;
            }
            const QJsonArray parts = obj.value(QStringLiteral("content")).toArray();
            partCount = parts.size();
            for (const QJsonValue &partValue : parts)
            {
                const QJsonObject part = partValue.toObject();
                const QString type = part.value(QStringLiteral("type")).toString();
                if (type == QStringLiteral("text")
                    && part.value(QStringLiteral("text")).toString()
                           == QStringLiteral("看看这个刷怪塔"))
                {
                    textPartOk = true;
                }
                else if (type == QStringLiteral("image_url"))
                {
                    const QString url = part.value(QStringLiteral("image_url")).toObject()
                                            .value(QStringLiteral("url")).toString();
                    imagePartOk = url.startsWith(QStringLiteral("data:image/jpeg;base64,"));
                }
            }
        }
        fprintf(stderr, "[info] 请求体 user.content 分段数 %d\n", partCount);
        report("request carries text part", textPartOk);
        report("request carries image_url part", imagePartOk);
        delete service;
    }

    // ── 8. 主界面（非实例助手）不应出现截图按钮 ──
    auto *plainHost = new QWidget;
    plainHost->resize(900, 700);
    auto *plainLayout = new QVBoxLayout(plainHost);
    plainLayout->setContentsMargins(0, 0, 0, 0);
    auto *plainPage = new AiChatPage(plainHost);
    plainLayout->addWidget(plainPage);
    plainHost->show();
    pump();
    waitMs(300);
    pump();
    // 对话列表气泡默认收起，展开后才能看到「聊天 / 工作」切换
    plainPage->setConvListExpanded(true, false);
    waitMs(300);
    pump();
    report("screenshot button hidden on main AI page",
           plainPage->m_screenshotBtn && !plainPage->m_screenshotBtn->isVisible());
    report("mode bar visible on main AI page (conv list expanded)",
           plainPage->m_modeBar && plainPage->m_modeBar->isVisible());
    plainHost->grab().save(QStringLiteral("probe_out/ai_shot_06_main_page.png"));

    // 探针改过「对话气泡展开状态」这个用户设置，收尾时还原成收起，别影响真实使用
    plainPage->setConvListExpanded(false, false);
    page->setConvListExpanded(false, false);

    fprintf(stderr, "[done] 探针结束\n");
    return 0;
}
