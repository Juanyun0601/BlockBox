// AI 助手流式渲染视觉探针：
// 加载真实 style.qss（token 解析后），渲染"流式正文编辑器 / 完成态 Markdown /
// 流式思考编辑器 / 历史思考标签"四种状态，截图到 _out 供人工核对。
// 构建：qmake ai_stream_visual_probe.pro && mingw32-make && ./ai_stream_visual_probe.exe
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QRegularExpression>

namespace {

QString loadStyle()
{
    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }
    QString css = QString::fromUtf8(f.readAll());
    // token → 颜色：背景类给浅灰（不透明，便于发现"该透明却成了色块"的问题）
    static const QList<QPair<QString, QString>> map{
        {QStringLiteral("BG_CARD"), QStringLiteral("#eceff1")},
        {QStringLiteral("BG_BASE"), QStringLiteral("#f5f5f5")},
        {QStringLiteral("BG_CONTENT"), QStringLiteral("#fafafa")},
        {QStringLiteral("BG_MUTED"), QStringLiteral("#f0f0f0")},
        {QStringLiteral("BG_MSG"), QStringLiteral("#edf0f4")},
        {QStringLiteral("BG_HOVER"), QStringLiteral("#e0e0e0")},
        {QStringLiteral("TEXT_PRIMARY"), QStringLiteral("#222222")},
        {QStringLiteral("TEXT_SECONDARY"), QStringLiteral("#666666")},
        {QStringLiteral("TEXT_TERTIARY"), QStringLiteral("#999999")},
        {QStringLiteral("WARNING_TEXT_DARK"), QStringLiteral("#b26a00")},
        {QStringLiteral("PRIMARY"), QStringLiteral("#1976d2")},
        {QStringLiteral("PRIMARY_RGBA_38"), QStringLiteral("rgba(25,118,210,0.38)")},
        {QStringLiteral("BORDER"), QStringLiteral("#d0d0d0")},
        {QStringLiteral("LINK"), QStringLiteral("#1976d2")},
        {QStringLiteral("CODE_BG"), QStringLiteral("#282c34")},
        {QStringLiteral("CODE_TEXT"), QStringLiteral("#abb2bf")},
        {QStringLiteral("CODE_BORDER"), QStringLiteral("#3a3f4b")},
    };
    for (const auto &kv : map)
    {
        css.replace(QStringLiteral("@%1@").arg(kv.first), kv.second);
    }
    // 其余 token 一律置灰：值不参与本次核对，只要保持"合法颜色"即可
    css.replace(QRegularExpression(QStringLiteral("@[A-Za-z0-9_.]+@")),
                QStringLiteral("#9a9a9a"));
    return css;
}

QFrame* makeBubble(QWidget *parent, QLabel **outLabel)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("aiChatAiBubble"));
    auto *lay = new QVBoxLayout(frame);
    lay->setContentsMargins(14, 10, 14, 10);
    lay->setSpacing(0);

    auto *label = new QLabel(frame);
    label->setObjectName(QStringLiteral("msgContent"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    lay->addWidget(label);
    *outLabel = label;
    return frame;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyleSheet(loadStyle());

    QWidget win;
    win.setObjectName(QStringLiteral("root"));
    win.setStyleSheet(QStringLiteral("#root { background: #f7f7f7; }"));
    win.resize(780, 900);

    auto *root = new QVBoxLayout(&win);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    auto *area = new QScrollArea(&win);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setFrameShape(QFrame::NoFrame);
    area->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    auto *container = new QWidget();
    container->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *msgLayout = new QVBoxLayout(container);
    msgLayout->setContentsMargins(0, 0, 0, 0);
    msgLayout->setSpacing(10);
    area->setWidget(container);
    root->addWidget(area, 1);

    auto addCaption = [&](const QString &text) {
        auto *cap = new QLabel(text);
        cap->setStyleSheet(QStringLiteral("color:#888; font-size:12px;"));
        msgLayout->addWidget(cap);
    };

    // 1. 流式正文（QTextEdit 增量追加）
    QTextEdit *streamEdit = nullptr;
    QLabel *streamBubble = nullptr;
    addCaption(QStringLiteral("① 流式正文 QTextEdit#streamMsgContent（应透明、无边框、14px）"));
    {
        QLabel *label = nullptr;
        auto *bubble = makeBubble(container, &label);
        streamBubble = label;
        label->hide();
        auto *edit = new QTextEdit(bubble);
        edit->setObjectName(QStringLiteral("streamMsgContent"));
        edit->setReadOnly(true);
        edit->setUndoRedoEnabled(false);
        edit->setAcceptRichText(false);
        edit->setFrameStyle(QFrame::NoFrame);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setLineWrapMode(QTextEdit::WidgetWidth);
        edit->setCursor(Qt::IBeamCursor);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        edit->document()->setDocumentMargin(0);
        edit->viewport()->setAutoFillBackground(false);
        bubble->layout()->addWidget(edit);
        streamEdit = edit;
        msgLayout->addWidget(bubble);
    }

    // 2. 完成态 Markdown（保持既有 QLabel 渲染）
    addCaption(QStringLiteral("② 完成态 QLabel#msgContent Markdown（对照，应与改动前一致）"));
    {
        QLabel *label = nullptr;
        auto *bubble = makeBubble(container, &label);
        label->setTextFormat(Qt::MarkdownText);
        label->setText(QStringLiteral(
            "### 渲染结果\n\n支持 **加粗**、[链接](https://example.com) 与行内 `code`：\n\n"
            "- 列表项 A\n- 列表项 B\n\n```cpp\nint main() { return 0; }\n```"));
        msgLayout->addWidget(bubble);
    }

    // 3. 流式思考（展开态，QTextEdit）
    QTextEdit *thinkEdit = nullptr;
    addCaption(QStringLiteral("③ 流式思考 QTextEdit#thinkingStreamContent（展开态，13px 琥珀色）"));
    {
        auto *box = new QWidget();
        box->setObjectName(QStringLiteral("thinkingContentWidget"));
        box->setStyleSheet(QStringLiteral("background-color: transparent; padding-left: 16px; "
                                          "border-left: 2px solid #1976d2;"));
        auto *lay = new QVBoxLayout(box);
        lay->setContentsMargins(0, 4, 0, 0);

        auto *head = new QLabel(QStringLiteral("思考  已完成  最新思考片段预览……"));
        head->setStyleSheet(QStringLiteral("color:#888; font-size:12px;"));
        lay->addWidget(head);

        auto *edit = new QTextEdit(box);
        edit->setObjectName(QStringLiteral("thinkingStreamContent"));
        edit->setReadOnly(true);
        edit->setFrameStyle(QFrame::NoFrame);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        edit->document()->setDocumentMargin(0);
        edit->viewport()->setAutoFillBackground(false);
        lay->addWidget(edit);
        thinkEdit = edit;
        msgLayout->addWidget(box);
    }

    // 4. 历史思考（QLabel，对照）
    addCaption(QStringLiteral("④ 历史思考 QLabel#thinkingContent（对照）"));
    {
        auto *box = new QWidget();
        box->setObjectName(QStringLiteral("thinkingContentWidget"));
        box->setStyleSheet(QStringLiteral("background-color: transparent; padding-left: 16px; "
                                          "border-left: 2px solid #1976d2;"));
        auto *lay = new QVBoxLayout(box);
        lay->setContentsMargins(0, 4, 0, 0);
        auto *label = new QLabel(QStringLiteral(
            "首先需要确认用户想安装的是 Java 版还是基岩版；接着检查当前实例的版本与加载器，"
            "然后从资源列表里筛选出兼容的模组，最后给出安装步骤与注意事项。"));
        label->setObjectName(QStringLiteral("thinkingContent"));
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
        lay->addWidget(label);
        msgLayout->addWidget(box);
    }

    auto *debug = new QLabel(&win);
    debug->setStyleSheet(QStringLiteral("color:#006600; font-size:12px; background:#ffffff; "
                                        "padding:4px 6px; border:1px solid #d0d0d0;"));
    root->addWidget(debug);

    win.show();

    // 真实时序：气泡先完成布局，流式内容随后到达（对齐 AiChatPage 的 50ms 节流）
    QTimer::singleShot(300, [&]() {
        streamEdit->setPlainText(QStringLiteral(
            "你好！这里是流式正文的实时追加效果。方块盒子的 AI 助手会在模型生成的同时把内容"
            "增量写入下方控件，避免每 50ms 对整篇正文重新排版。"
            "当回答结束时，这段纯文本会切换为 QLabel 的 Markdown 渲染。"));
        thinkEdit->setPlainText(QStringLiteral(
            "首先需要确认用户想安装的是 Java 版还是基岩版；接着检查当前实例的版本与加载器，"
            "然后从资源列表里筛选出兼容的模组，最后给出安装步骤与注意事项。"));
        for (QTextEdit *e : {streamEdit, thinkEdit})
        {
            const int h = int(e->document()->size().height()) + 2;
            e->setFixedHeight(qMax(h, e->fontMetrics().height() + 4));
        }
    });

    QTimer::singleShot(700, [&]() {
        debug->setText(QStringLiteral(
                           "streamEdit: w=%1 h=%2 doc=%3x%4 textW=%5 | "
                           "thinkEdit: w=%6 h=%7 doc=%8x%9 | labelHidden=%10")
                           .arg(streamEdit->width()).arg(streamEdit->height())
                           .arg(int(streamEdit->document()->size().width()))
                           .arg(int(streamEdit->document()->size().height()))
                           .arg(int(streamEdit->document()->textWidth()))
                           .arg(thinkEdit->width()).arg(thinkEdit->height())
                           .arg(int(thinkEdit->document()->size().width()))
                           .arg(int(thinkEdit->document()->size().height()))
                           .arg(!streamBubble->isVisible()));
        QTimer::singleShot(200, [&]() {
            const QString out = QStringLiteral("D:/BlockBox/_out/ai_stream_visual.png");
            QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));
            const bool ok = win.grab().save(out);
            qInfo("saved=%d path=%s", ok ? 1 : 0, out.toUtf8().constData());
            app.quit();
        });
    });
    return app.exec();
}
