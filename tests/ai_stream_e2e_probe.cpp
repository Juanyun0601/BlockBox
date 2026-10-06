/**
 * @file   ai_stream_e2e_probe.cpp
 * @brief  AI 助手流式渲染端到端探针（链接主程序全部目标文件，仅替换 main.cpp）
 *
 * 流程：
 *   1. 启动真实 MainWindow，切到 AI 助手页；
 *   2. 【新方案】直接驱动 AiChatPage 的流式槽（思考段 + 正文段），全程 50ms 心跳
 *      统计事件循环停顿（心跳只覆盖流式阶段，截图与中途收尾窗口剔除）；
 *   3. 【工作模式时序】在流式中途触发一次 onStreamFinished（等价于第一轮 data:[DONE]，
 *      此后 m_isStreaming 变 false），断言后续轮次仍继续增量显示——不能退化成
 *      "全部输出完才一次性显示"；
 *   4. 流式中/结束后各抓一张截图，并断言流式用 QTextEdit 增量渲染、
 *      收尾后切回 QLabel 的 Markdown；
 *   5. 【旧方案基线】在同一窗口用 QLabel 全量 setText 复现改动前的刷新方式，
 *      测出每次刷新耗时，作为卡顿根因的对照数据；
 *   6. exec 结束后还原对话存档（探针不改动用户数据）。
 *
 * 注：探针在包含 AiChatPage.h 前把 private 置为 public，仅为直接设置
 *     m_isStreaming 等内部状态；生产代码不受影响。
 */
#define private public
#define protected public
#include "pages/AiChatPage.h"
#undef private
#undef protected

#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QString outFile(const QString &name)
{
    const QString dir = QStringLiteral("D:/BlockBox/_out");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + name;
}

struct Stats
{
    double avg = 0, p95 = 0, max = 0;
    long over100 = 0, over150 = 0;
    size_t n = 0;
};

Stats summarize(const std::vector<double> &gaps)
{
    Stats s;
    if (gaps.empty()) return s;
    std::vector<double> g = gaps;
    std::sort(g.begin(), g.end());
    s.n = g.size();
    s.max = g.back();
    s.p95 = g[size_t(g.size() * 0.95)];
    double sum = 0;
    for (double v : g)
    {
        sum += v;
        if (v > 100) ++s.over100;
        if (v > 150) ++s.over150;
    }
    s.avg = sum / double(g.size());
    return s;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    int failures = 0;
    auto expect = [&](const char *what, bool ok, const QString &detail) {
        fprintf(stderr, "  [%s] %s %s\n", ok ? "PASS" : "FAIL", what,
                detail.toUtf8().constData());
        if (!ok) ++failures;
    };

    MainWindow w;
    w.resize(1280, 800);
    w.show();
    settle(2000);

    QMetaObject::invokeMethod(&w, "onParentNavClicked", Q_ARG(int, 3));
    settle(2500);
    auto *page = w.findChild<AiChatPage *>();
    expect("ai chat page opened", page != nullptr, QString());
    if (!page)
    {
        return 1;
    }

    // 对话存档备份（onStreamFinished 会触发落盘防抖，exec 结束后再还原）
    const QString convFile = page->conversationsFilePath();
    QByteArray convBackup;
    const bool convExisted = QFile::exists(convFile);
    if (convExisted)
    {
        QFile f(convFile);
        if (f.open(QIODevice::ReadOnly)) convBackup = f.readAll();
    }

    // ── 准备流式状态：新对话草稿态，直接驱动渲染路径 ──
    page->clearMessageArea();
    page->showWelcome(false);
    page->m_currentConvIndex = -1;
    page->m_currentContent.clear();
    page->m_currentReasoning.clear();
    page->m_currentAiBubble = nullptr;
    page->m_currentThinkingBubble = nullptr;
    page->m_currentThinkingContent = nullptr;
    page->m_currentContentLabel = nullptr;
    page->m_currentThinkingLabel = nullptr;
    page->m_currentContentEdit = nullptr;
    page->m_currentThinkingEdit = nullptr;
    page->m_streamDirty = false;
    page->m_streamThinkingDirty = false;
    page->m_streamFlushedLen = 0;
    page->m_streamThinkingFlushedLen = 0;
    page->m_isStreaming = true;
    settle(300);

    // 事件循环心跳：预期 50ms 一跳，记录 (时间, 间隔)
    std::vector<std::pair<double, double>> samples;
    QElapsedTimer beat;
    beat.start();
    qint64 last = 0;
    double shotBegin = -1, shotEnd = -1;
    QTimer heartbeat;
    heartbeat.setInterval(50);
    QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
        const double now = double(beat.elapsed());
        if (last > 0) samples.emplace_back(now, now - double(last));
        last = qint64(now);
    });
    heartbeat.start();

    const QString reasoningBase = QStringLiteral(
        "先确认用户想安装的是 Java 版还是基岩版，再检查实例的版本与加载器是否兼容，"
        "最后从资源列表里筛选出匹配的模组并整理成安装步骤。");
    const QString contentBase = QStringLiteral(
        "方块盒子的 AI 助手正在把模型输出增量写入正文控件，"
        "这样每 50ms 只追加新到的文本，不再对整篇内容重新排版。");

    QString reasoning;
    QString content;
    QTimer feeder;
    feeder.setInterval(15);
    int tick = 0;
    const int reasoningTicks = 60;
    const int contentTicks = 400;
    // 工作模式时序：每轮 data:[DONE] 都会 emit streamFinished，第一轮结束在第 80 拍
    const int midFinishTick = reasoningTicks + 20;
    double midFinishMs = 0;
    double lenAfterMid = 0;
    double midFinBegin = -1, midFinEnd = -1;
    double finalizeMs = 0;

    QObject::connect(&feeder, &QTimer::timeout, [&]() {
        ++tick;
        if (tick <= reasoningTicks)
        {
            reasoning += reasoningBase;
            page->onStreamReasoning(reasoningBase);
            return;
        }
        if (tick <= reasoningTicks + contentTicks)
        {
            content += contentBase;
            page->onStreamContent(contentBase);

            // 复现工作模式：第一轮 [DONE] 触发 onStreamFinished（m_isStreaming 变 false），
            // 此后仍要继续流式显示后续轮次的内容
            if (tick == midFinishTick)
            {
                TokenUsage u;
                u.valid = false;
                midFinBegin = double(beat.elapsed());
                QElapsedTimer t;
                t.start();
                page->onStreamFinished(u);
                midFinishMs = double(t.nsecsElapsed() / 1e6);
                midFinEnd = double(beat.elapsed());
                settle(80);
                fprintf(stderr, "  info: round-1 [DONE] -> onStreamFinished (%.0f ms), m_isStreaming=%d\n",
                        midFinishMs, int(page->m_isStreaming));
                expect("mid-round: m_isStreaming is false after [DONE]", !page->m_isStreaming,
                       QString());
            }

            if (tick == reasoningTicks + 150)
            {
                shotBegin = double(beat.elapsed());
                w.grab().save(outFile(QStringLiteral("ai_stream_mid.png")));
                shotEnd = double(beat.elapsed());

                const auto edits = page->findChildren<QTextEdit *>(QStringLiteral("streamMsgContent"));
                QTextEdit *edit = edits.isEmpty() ? nullptr : edits.last();
                QLabel *curLbl = (edit && edit->parentWidget())
                                     ? edit->parentWidget()->findChild<QLabel *>(QStringLiteral("msgContent"))
                                     : nullptr;
                expect("mid: round-2 streaming edit exists", edit != nullptr, QString());
                expect("mid: current bubble label hidden", curLbl != nullptr && !curLbl->isVisible(),
                       QString());
                if (edit)
                {
                    lenAfterMid = double(edit->document()->characterCount());
                    fprintf(stderr, "  info: mid edit %dx%d docH=%d textLen=%.0f\n",
                            edit->width(), edit->height(),
                            int(edit->document()->size().height()), lenAfterMid);
                }
                expect("mid: round-2 content rendered incrementally", lenAfterMid > 100,
                       QStringLiteral("textLen=%1").arg(lenAfterMid, 0, 'f', 0));
            }
            else if (tick == reasoningTicks + 300)
            {
                // 中途 [DONE] 之后内容仍在增量增长（否则就是"全部输出完才显示"）
                const auto edits = page->findChildren<QTextEdit *>(QStringLiteral("streamMsgContent"));
                const double now = edits.isEmpty() ? 0
                                                   : double(edits.last()->document()->characterCount());
                expect("keeps streaming after round [DONE]", now > lenAfterMid,
                       QStringLiteral("mid=%1 later=%2").arg(lenAfterMid).arg(now));
            }
            return;
        }

        // ── 流式结束 ──
        feeder.stop();
        heartbeat.stop();   // 心跳只覆盖流式阶段，收尾渲染单独计时
        fprintf(stderr, "  info: driven content=%d chars, reasoning=%d chars\n",
                int(content.size()), int(reasoning.size()));

        QElapsedTimer fin;
        fin.start();
        TokenUsage usage;
        usage.valid = false;
        page->onStreamFinished(usage);
        finalizeMs = double(fin.nsecsElapsed() / 1e6);

        // 收尾时编辑器是 deleteLater，需让延时删除事件跑完
        settle(300);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        settle(200);
        w.grab().save(outFile(QStringLiteral("ai_stream_after.png")));

        // 中途 [DONE] 会产生两个气泡，取最后一个（当前回合）做断言
        const auto labels = page->findChildren<QLabel *>(QStringLiteral("msgContent"));
        QLabel *lbl = labels.isEmpty() ? nullptr : labels.last();
        auto *edit = page->findChild<QTextEdit *>(QStringLiteral("streamMsgContent"));
        expect("after: edit destroyed", edit == nullptr, QString());
        expect("after: label visible", lbl != nullptr && lbl->isVisible(), QString());
        expect("after: label holds full content",
               lbl != nullptr && lbl->text().size() >= content.size(),
               QStringLiteral("labelLen=%1 contentLen=%2 bubbles=%3")
                   .arg(lbl ? lbl->text().size() : 0)
                   .arg(content.size())
                   .arg(labels.size()));
        fprintf(stderr, "  info: finalize (markdown render) = %.0f ms for %d chars\n",
                finalizeMs, int(content.size()));

        // 思考区展开：验证流式思考编辑器在展开后按文档高度同步（不被裁切）
        if (auto *headerBtn = page->findChild<QPushButton *>(QStringLiteral("thinkingHeaderRow")))
        {
            headerBtn->click();
            settle(300);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            auto *te = page->findChild<QTextEdit *>(QStringLiteral("thinkingStreamContent"));
            expect("thinking: expand edit exists", te != nullptr, QString());
            if (te)
            {
                const int docH = int(te->document()->size().height());
                expect("thinking: height fits document", te->height() >= docH,
                       QStringLiteral("h=%1 docH=%2").arg(te->height()).arg(docH));
                fprintf(stderr, "  info: thinking edit %dx%d doc=%dx%d\n",
                        te->width(), te->height(),
                        int(te->document()->size().width()), docH);
            }
            w.grab().save(outFile(QStringLiteral("ai_stream_thinking.png")));
        }
        else
        {
            expect("thinking: header found", false, QString());
        }

        // 流式阶段心跳统计（剔除截图与中途 [DONE] 收尾渲染窗口）
        std::vector<double> gaps;
        for (const auto &s : samples)
        {
            if (shotBegin >= 0 && s.first >= shotBegin - 100 && s.first <= shotEnd + 100)
                continue;
            if (midFinBegin >= 0 && s.first >= midFinBegin - 100 && s.first <= midFinEnd + 100)
                continue;
            gaps.push_back(s.second);
        }
        const Stats st = summarize(gaps);
        fprintf(stderr,
                "  info: [new path] heartbeat n=%zu avg=%.1fms p95=%.1fms max=%.1fms over100ms=%ld over150ms=%ld\n",
                st.n, st.avg, st.p95, st.max, st.over100, st.over150);
        // 100ms 上下偶发一次属系统抖动；>150ms 且 p95 超标才算回答期间真卡顿
        expect("new path: no stall > 150ms while answering", st.over150 == 0,
               QStringLiteral("maxGap=%1ms over100=%2").arg(st.max, 0, 'f', 1).arg(st.over100));
        expect("new path: p95 heartbeat < 90ms", st.p95 < 90.0,
               QStringLiteral("p95=%1ms").arg(st.p95, 0, 'f', 1));

        // ── 旧方案基线：QLabel 全量 setText 每次刷新的耗时 ──
        if (lbl)
        {
            lbl->setTextFormat(Qt::PlainText);
            const std::vector<int> lens{1000, 2000, 4000, 6000, 8000};
            fprintf(stderr, "  info: [old path] QLabel full setText per flush:\n");
            for (int n : lens)
            {
                const QString s = content.left(n);
                QElapsedTimer t;
                t.start();
                lbl->setText(s);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
                QCoreApplication::processEvents(QEventLoop::AllEvents);
                const double ms = double(t.nsecsElapsed() / 1e6);
                fprintf(stderr, "        len=%4d -> %.1f ms%s\n", n, ms,
                        ms > 50 ? "  (> 50ms 节流周期，必然卡顿)" : "");
            }
            // 还原最终 markdown 内容
            lbl->setTextFormat(Qt::MarkdownText);
            lbl->setText(content);
        }

        app.exit(failures);
    });
    feeder.start();

    const int code = app.exec();

    // 还原对话存档（落盘防抖已在 exec 期间触发）
    if (convExisted)
    {
        QFile f(convFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(convBackup);
    }
    else if (QFile::exists(convFile))
    {
        QFile::remove(convFile);
    }

    fprintf(stderr, code == 0 ? "ALL PASS\n" : "FAILURES=%d\n", code);
    return code != 0 ? 1 : 0;
}
