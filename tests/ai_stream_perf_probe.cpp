// AI 助手流式渲染性能探针：
// 复刻 AiChatPage 消息区结构（QScrollArea + 气泡 QLabel + 节流 50ms 全量 setText），
// 测量不同正文长度下每次 flush 的耗时，定位"回答时卡顿"的根因。
//
// 用法：ai_stream_perf_probe.exe [label|textedit] [qss|plain]
//   label     流式用 QLabel 全量 setText（当前实现）
//   textedit  流式用 QTextEdit 增量 insertText（候选修复）
//   qss       加载贴近线上的样式表（默认）
//   plain     不加载样式表（用于隔离 QSS 成本）
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QScrollBar>
#include <QScrollArea>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QDebug>

#include <functional>
#include <vector>

namespace {

QString makeChineseText(int chars)
{
    QString s;
    s.reserve(chars * 3);
    const QString base = QStringLiteral(
        "方块盒子的流式渲染探针文本用于模拟模型输出的中文正文内容。"
        "这里包含一些代码 `int main()` 与链接 [示例](https://example.com) 以贴近真实 Markdown 输入。");
    while (s.size() < chars)
        s += base;
    return s.left(chars);
}

// 复刻 createMessageWidget 的 AI 气泡（QLabel 版）
struct Bubble
{
    QFrame *frame = nullptr;
    QLabel *label = nullptr;
    QTextEdit *edit = nullptr;
};

Bubble makeAiBubble(QWidget *parent)
{
    Bubble b;
    b.frame = new QFrame(parent);
    b.frame->setObjectName(QStringLiteral("aiChatAiBubble"));
    auto *l = new QVBoxLayout(b.frame);
    l->setContentsMargins(14, 10, 14, 10);
    l->setSpacing(0);
    b.label = new QLabel(b.frame);
    b.label->setObjectName(QStringLiteral("msgContent"));
    b.label->setWordWrap(true);
    b.label->setTextFormat(Qt::PlainText);
    b.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    b.frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    b.label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    l->addWidget(b.label);
    return b;
}

} // namespace

int main(int argc, char **argv)
{
    // 进度落盘：控制台输出在 Windows 下可能被缓冲，探针用文件保证可观测
    QFile log(QStringLiteral("ai_stream_perf_probe.log"));
    log.open(QIODevice::WriteOnly | QIODevice::Truncate);
    auto say = [&](const QString &s) {
        log.write((s + QStringLiteral("\n")).toUtf8());
        log.flush();
    };

    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("label");
    const QString style = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("qss");
    say(QStringLiteral("mode=%1 style=%2").arg(mode, style));

    QApplication app(argc, argv);

    if (style == QLatin1String("qss"))
    {
        // 与线上一致的基础样式（字号/边框近似）
        app.setStyleSheet(QStringLiteral(
            "QFrame#aiChatAiBubble { background: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 12px; }"
            "QFrame#aiChatAiBubble QLabel#msgContent { color: #0F172A; font-size: 14px; }"
            "QScrollArea { background: #F8FAFC; border: none; }"));
    }

    QWidget win;
    win.resize(860, 720);
    auto *root = new QVBoxLayout(&win);
    root->setContentsMargins(0, 0, 0, 0);

    auto *area = new QScrollArea(&win);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *container = new QWidget();
    auto *msgLayout = new QVBoxLayout(container);
    msgLayout->setContentsMargins(24, 16, 24, 16);
    msgLayout->setSpacing(8);
    area->setWidget(container);
    root->addWidget(area, 1);

    // 历史消息（模拟已有 10 条对话：Markdown 长回复 + 用户消息）
    for (int i = 0; i < 10; ++i)
    {
        auto hist = makeAiBubble(container);
        hist.label->setTextFormat(Qt::MarkdownText);
        hist.label->setText(QStringLiteral(
            "### 历史消息 %1\n\n这是之前的 **Markdown** 回复，包含列表：\n\n"
            "- 第一项说明\n- 第二项说明\n\n```cpp\nint answer = %1;\nreturn answer;\n```\n\n"
            "结尾还有一段较长的总结文字，用于撑起真实布局高度。").arg(i));
        msgLayout->addWidget(hist.frame);
    }

    // 当前流式气泡
    auto cur = makeAiBubble(container);
    msgLayout->addWidget(cur.frame);

    const bool useTextEdit = (mode == QLatin1String("textedit"));
    if (useTextEdit)
    {
        cur.label->hide();
        cur.edit = new QTextEdit(cur.frame);
        cur.edit->setObjectName(QStringLiteral("msgContent"));
        cur.edit->setReadOnly(true);
        cur.edit->setFrameStyle(QFrame::NoFrame);
        cur.edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        cur.edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        cur.edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        cur.edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        cur.edit->document()->setDocumentMargin(0);
        cur.frame->layout()->addWidget(cur.edit);
    }

    win.show();

    const int targetChars = 8000;    // 正文目标长度
    const int stepChars = 40;        // 每 50ms 追加字数（≈800 字/秒，偏快的模型）
    QString content;
    content.reserve(targetChars);

    struct Sample { int len; double setTextMs; double scrollMs; double restMs; };
    std::vector<Sample> samples;

    QElapsedTimer flushTimer;
    QTimer tick;
    tick.setInterval(50);

    // md 模式：只测量"回答完成时"一次性 Markdown 渲染在不同长度下的耗时
    if (mode == QLatin1String("md"))
    {
        const std::vector<int> sizes{1000, 2000, 4000, 8000};
        size_t idx = 0;
        std::function<void()> step = [&]() {
            if (idx >= sizes.size())
            {
                app.quit();
                return;
            }
            const int n = sizes[idx++];
            const QString txt = QStringLiteral("# 小标题\n\n**加粗** 与 `代码`：\n\n")
                                + makeChineseText(n).left(n)
                                + QStringLiteral("\n\n- 列表项一\n- 列表项二\n");
            QElapsedTimer t;
            t.start();
            cur.label->setTextFormat(Qt::MarkdownText);
            cur.label->setText(txt);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            double layoutMs = t.nsecsElapsed() / 1e6;
            area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
            QCoreApplication::processEvents(QEventLoop::AllEvents);
            double totalMs = t.nsecsElapsed() / 1e6;
            say(QStringLiteral("markdown len=%1 layout=%2ms 总计=%3ms")
                    .arg(n).arg(layoutMs, 0, 'f', 1).arg(totalMs, 0, 'f', 1));
            QTimer::singleShot(200, step);
        };
        QTimer::singleShot(300, step);
        return app.exec();
    }


    QObject::connect(&tick, &QTimer::timeout, [&]() {
        if (content.size() >= targetChars)
        {
            tick.stop();

            // 结束：一次性 Markdown 渲染耗时（对应 onStreamFinished）
            QElapsedTimer md;
            md.start();
            if (useTextEdit)
            {
                cur.edit->setMarkdown(content);
            }
            else
            {
                cur.label->setTextFormat(Qt::MarkdownText);
                cur.label->setText(content);
            }
            // 排版与绘制是延迟发生的，一并计入
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QCoreApplication::processEvents(QEventLoop::AllEvents);
            double mdMs = md.nsecsElapsed() / 1e6;

            say(QStringLiteral("---- 结果 ----"));
            say(QStringLiteral("正文总长: %1 字符").arg(content.size()));
            for (const auto &s : samples)
            {
                say(QStringLiteral("%1 setText=%2 scroll=%3 layout+paint=%4")
                        .arg(s.len, 8)
                        .arg(s.setTextMs, 8, 'f', 2)
                        .arg(s.scrollMs, 8, 'f', 2)
                        .arg(s.restMs, 10, 'f', 2));
            }
            say(QStringLiteral("最终 Markdown 渲染一次性耗时: %1 ms").arg(mdMs, 0, 'f', 1));
            app.quit();
            return;
        }

        content += makeChineseText(stepChars);

        flushTimer.start();
        if (useTextEdit)
        {
            cur.edit->moveCursor(QTextCursor::End);
            cur.edit->insertPlainText(content.right(stepChars));
            // 真实实现需让气泡随文档高度增长
            const int docH = int(cur.edit->document()->size().height()) + 8;
            cur.edit->setMinimumHeight(docH);
            cur.edit->setMaximumHeight(docH);
        }
        else
        {
            cur.label->setText(content);
        }
        double setTextMs = flushTimer.nsecsElapsed() / 1e6;

        // 布局阶段：QLabel::setText 只是标记脏，真正的全文排版在 LayoutRequest 中发生
        double t1 = flushTimer.nsecsElapsed() / 1e6;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        double layoutMs = flushTimer.nsecsElapsed() / 1e6 - t1;

        // 滚动到底
        QScrollBar *sb = area->verticalScrollBar();
        double t2 = flushTimer.nsecsElapsed() / 1e6;
        sb->setValue(sb->maximum());
        double scrollMs = flushTimer.nsecsElapsed() / 1e6 - t2;

        // 绘制阶段：update() 投递的 paint 事件
        double t3 = flushTimer.nsecsElapsed() / 1e6;
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        double paintMs = flushTimer.nsecsElapsed() / 1e6 - t3;

        if (content.size() % 2000 < stepChars || content.size() <= stepChars)
            samples.push_back({int(content.size()), setTextMs, scrollMs, layoutMs + paintMs});
        else if (!samples.empty() && content.size() - samples.back().len >= 2000)
            samples.push_back({int(content.size()), setTextMs, scrollMs, layoutMs + paintMs});
    });

    QTimer::singleShot(300, [&]() { tick.start(); });
    return app.exec();
}
