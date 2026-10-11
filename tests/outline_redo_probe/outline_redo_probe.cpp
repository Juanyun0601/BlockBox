// 文字描边重做视觉探针：
// 1. 同屏渲染「旧算法（含字形本体的 8 方向偏移掩码）」与「新算法（盘状膨胀 + 抠出
//    字形本体）」的 AI 回答气泡，放大裁块对比文字边缘质量；
// 2. 渲染流式阶段 OutlinedTextEdit 的描边效果；
// 3. 程序化校验：新描边层不扰动正文像素、复刻排版与 QLabel 真实渲染逐像素对齐、
//    字形本体被完整抠出（描边只落在轮廓之外）。
// 构建：qmake outline_redo_probe.pro && mingw32-make && ./outline_redo_probe.exe
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "MarkdownOutlinedLabel.h"
#include "OutlinedTextEdit.h"
#include "TextOutlineLayer.h"

namespace {

const QColor kOutlineColor("#e8e8e8");
const QColor kTextColor("#333333");
const QColor kBubbleBg("#fbfbfb");

const char *kSampleMd = R"MD(### 方块盒子 AI 助手
这是一段用于检验**文字描边**质量的中文正文，含 *斜体*、`行内代码` 与 [链接](https://example.com)。

- 列表项一：描边应贴合字形轮廓
- 列表项二：正文边缘不发糊、无重影

The quick brown fox jumps over the lazy dog. 0123456789)MD";

QString bubbleQss()
{
    return QStringLiteral(
        "QFrame#aiChatAiBubble { background: %1; border: 1px solid #d0d0d0;"
        " border-radius: 12px; }"
        "QFrame#aiChatAiBubble QLabel#msgContent { color: %2; background: transparent;"
        " font-size: 14px; }").arg(kBubbleBg.name(), kTextColor.name());
}

QFrame* makeBubble(QLabel *label)
{
    auto *frame = new QFrame();
    frame->setObjectName(QStringLiteral("aiChatAiBubble"));
    frame->setStyleSheet(bubbleQss());
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(0);
    layout->addWidget(label);
    return frame;
}

MarkdownOutlinedLabel* makeNewLabel()
{
    auto *label = new MarkdownOutlinedLabel(QString::fromUtf8(kSampleMd));
    label->setObjectName(QStringLiteral("msgContent"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::MarkdownText);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

// ---- 旧算法复刻：8 方向偏移掩码，掩码 = 描边色 × 文字 alpha（含字形本体）----
QPixmap oldOutlineLayer(const QLabel *label)
{
    const QRect r = label->rect();
    if (r.isEmpty())
        return QPixmap();
    const qreal dpr = label->devicePixelRatioF();
    const QImage textImg = TextOutlineLayer::renderLabelText(label, r);
    if (textImg.isNull())
        return QPixmap();

    QImage coloredMask(textImg.size(), QImage::Format_ARGB32_Premultiplied);
    coloredMask.fill(kOutlineColor);
    {
        QPainter pc(&coloredMask);
        pc.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        pc.drawImage(QPoint(0, 0), textImg);
        pc.end();
    }

    QImage outlineImg(textImg.size(), QImage::Format_ARGB32_Premultiplied);
    outlineImg.fill(Qt::transparent);
    {
        QPainter po(&outlineImg);
        po.setCompositionMode(QPainter::CompositionMode_Source);
        const int ow = 1 * int(dpr);
        const QPoint offsets[] = {
            QPoint(-ow, -ow), QPoint(0, -ow), QPoint(ow, -ow),
            QPoint(-ow, 0),                  QPoint(ow, 0),
            QPoint(-ow, ow), QPoint(0, ow), QPoint(ow, ow)
        };
        for (const QPoint &off : offsets)
            po.drawImage(off, coloredMask);
        po.end();
    }
    QPixmap pix = QPixmap::fromImage(outlineImg);
    pix.setDevicePixelRatio(dpr);
    return pix;
}

class OldOutlinedLabel : public QLabel
{
public:
    explicit OldOutlinedLabel(const QString &text) : QLabel(text)
    {
        setObjectName(QStringLiteral("msgContent"));
        setWordWrap(true);
        setTextFormat(Qt::MarkdownText);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    void paintEvent(QPaintEvent *event) override
    {
        m_layer = oldOutlineLayer(this);
        if (!m_layer.isNull())
        {
            QPainter p(this);
            p.drawPixmap(0, 0, m_layer);
            p.end();
        }
        QLabel::paintEvent(event);
    }

    QPixmap m_layer;
};

QImage grabWidget(QWidget *w)
{
    return w->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

QImage zoomCrop(const QImage &src, const QRect &crop, int factor)
{
    return src.copy(crop).scaled(crop.size() * factor,
                                 Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

void savePng(const QImage &img, const QString &path)
{
    if (img.save(path))
        qInfo() << "saved:" << path;
    else
        qWarning() << "FAILED to save:" << path;
}

}  // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    QWidget window;
    window.setWindowTitle(QStringLiteral("文字描边重做探针"));
    window.setStyleSheet(QStringLiteral("background: #f5f5f5;"));
    auto *root = new QVBoxLayout(&window);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    // ---- 第一行：旧算法（左）vs 新算法（右）----
    auto *row = new QHBoxLayout();
    row->setSpacing(16);

    auto *oldLabel = new OldOutlinedLabel(QString::fromUtf8(kSampleMd));
    auto *newLabel = makeNewLabel();
    QFrame *oldBubble = makeBubble(oldLabel);
    QFrame *newBubble = makeBubble(newLabel);
    oldBubble->setFixedWidth(430);
    newBubble->setFixedWidth(430);
    row->addWidget(oldBubble);
    row->addWidget(newBubble);
    root->addLayout(row);

    // ---- 第二行：放大 3 倍的正文首行裁块（人工核对边缘质量）----
    auto *zoomRow = new QHBoxLayout();
    zoomRow->setSpacing(16);
    QLabel *zoomOld = new QLabel(QStringLiteral("旧算法放大 3×"));
    QLabel *zoomNew = new QLabel(QStringLiteral("新算法放大 3×"));
    for (QLabel *z : {zoomOld, zoomNew})
    {
        z->setStyleSheet(QStringLiteral("color:#666; font-size:12px;"));
        zoomRow->addWidget(z);
    }    root->addLayout(zoomRow);

    // ---- 第三行：流式阶段 OutlinedTextEdit（纯文本、描边实时生效）----
    QFrame *streamBubble = new QFrame();
    streamBubble->setObjectName(QStringLiteral("aiChatAiBubble"));
    streamBubble->setStyleSheet(bubbleQss());
    auto *streamLayout = new QVBoxLayout(streamBubble);
    streamLayout->setContentsMargins(14, 10, 14, 10);
    auto *streamEdit = new OutlinedTextEdit(streamBubble);
    streamEdit->setObjectName(QStringLiteral("streamMsgContent"));
    streamEdit->setReadOnly(true);
    streamEdit->setUndoRedoEnabled(false);
    streamEdit->setAcceptRichText(false);
    streamEdit->setFrameStyle(QFrame::NoFrame);
    streamEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    streamEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    streamEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    streamEdit->document()->setDocumentMargin(0);
    streamEdit->viewport()->setAutoFillBackground(false);
    streamEdit->setStyleSheet(QStringLiteral(
        "QTextEdit#streamMsgContent { color: %1; background: transparent;"
        " font-size: 14px; border: none; }").arg(kTextColor.name()));
    streamEdit->setPlainText(
        QStringLiteral("这是一段正在流式输出的回答正文，描边应在输出过程中实时生效，"
                       "与收尾后的 Markdown 标签观感一致。Streamed text 0123456789。"));
    streamEdit->setFixedHeight(qMax(int(streamEdit->document()->size().height()) + 2,
                                    streamEdit->fontMetrics().height() + 4));
    streamLayout->addWidget(streamEdit);
    root->addWidget(streamBubble);
    root->addStretch();

    window.resize(920, 560);
    window.show();

    int exitCode = 0;
    QTimer::singleShot(300, &app, [&]() {
        int fails = 0;

        // ---------- 保存视觉对比图 ----------
        const QImage oldShot = grabWidget(oldBubble);
        const QImage newShot = grabWidget(newBubble);
        savePng(oldShot, QStringLiteral("D:/BlockBox/_out/outline_redo_old.png"));
        savePng(newShot, QStringLiteral("D:/BlockBox/_out/outline_redo_new.png"));
        // 裁正文首行（避开标题与边框）：取气泡内 (15, 45, 220, 40) 区域
        const QRect crop(15, 45, 140, 36);  // 140×36 @3× → 420px 宽，两张并排不撑爆 920px 窗口
        savePng(zoomCrop(oldShot, crop, 3), QStringLiteral("D:/BlockBox/_out/outline_redo_zoom_old.png"));
        savePng(zoomCrop(newShot, crop, 3), QStringLiteral("D:/BlockBox/_out/outline_redo_zoom_new.png"));
        zoomOld->setPixmap(QPixmap::fromImage(zoomCrop(oldShot, crop, 3)));
        zoomNew->setPixmap(QPixmap::fromImage(zoomCrop(newShot, crop, 3)));

        // 流式气泡：QSS/字体 polish 后重新定高（构造期视口过窄会把折行高度算虚）
        streamEdit->setFixedHeight(qMax(int(streamEdit->document()->size().height()) + 2,
                                        streamEdit->fontMetrics().height() + 4));
        app.processEvents();
        qInfo() << "[诊断] streamBubble geo:" << streamBubble->geometry()
                << "streamEdit geo:" << streamEdit->geometry()
                << "viewport:" << streamEdit->viewport()->size()
                << "doc:" << streamEdit->document()->size()
                << "vbar:" << streamEdit->verticalScrollBar()->value();
        savePng(grabWidget(streamBubble), QStringLiteral("D:/BlockBox/_out/outline_redo_stream.png"));

        savePng(grabWidget(&window), QStringLiteral("D:/BlockBox/_out/outline_redo_probe.png"));

        // ---------- 复算与组件内部完全相同的描边层 ----------
        const QSize labelSize = newLabel->size();
        const QImage layerImg = TextOutlineLayer::colorizeRing(
            TextOutlineLayer::buildOutlineRing(
                TextOutlineLayer::renderLabelText(newLabel, QRect(QPoint(0, 0), labelSize)),
                newLabel->outlineWidth()),
            newLabel->outlineColor(), newLabel->devicePixelRatioF())
            .toImage().convertToFormat(QImage::Format_ARGB32);
        const QImage replicaImg = TextOutlineLayer::renderLabelText(newLabel, QRect(QPoint(0, 0), labelSize))
                                      .convertToFormat(QImage::Format_ARGB32);

        // ---------- 无描边参照：同尺寸、同字体的纯 QLabel ----------
        // refOpaque：带气泡同款底色，用于校验1（RGB 扰动比对）
        // refClear ：透明背景，用于校验2/3（纯文字 alpha 比对）
        auto makeRef = [&labelSize](bool opaque) {
            auto *ref = new QLabel(QString::fromUtf8(kSampleMd));
            ref->setObjectName(QStringLiteral("msgContent"));
            ref->setWordWrap(true);
            ref->setTextFormat(Qt::MarkdownText);
            ref->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            ref->setStyleSheet(QStringLiteral(
                "QLabel#msgContent { color: %1; background: %2; font-size: 14px; }")
                .arg(kTextColor.name(),
                     opaque ? kBubbleBg.name() : QStringLiteral("transparent")));
            ref->setFixedSize(labelSize);
            return ref;
        };
        auto *refOpaque = makeRef(true);
        auto *refClear = makeRef(false);
        refOpaque->show();
        refClear->show();
        app.processEvents();

        // 组件截图裁到正文标签所在区域（气泡内含边框与标题边距，不能整框比对）
        const QImage newLabelShot = newShot.copy(newLabel->geometry());
        const QImage refShot = grabWidget(refOpaque);
        const QImage refClearShot = grabWidget(refClear);

        const int w = qMin(refShot.width(), qMin(newLabelShot.width(), layerImg.width()));
        const int h = qMin(refShot.height(), qMin(newLabelShot.height(), layerImg.height()));
        qInfo() << "比对区域:" << w << "x" << h << "标签尺寸:" << labelSize;

        // ---------- 校验 1：新描边层不扰动正文像素 ----------
        // 「无描边参照」与「新组件」在描边层 alpha=0 处必须逐像素一致
        int disturbed = 0;
        for (int y = 0; y < h; ++y)
        {
            const QRgb *pr = reinterpret_cast<const QRgb *>(refShot.constScanLine(y));
            const QRgb *pn = reinterpret_cast<const QRgb *>(newLabelShot.constScanLine(y));
            const QRgb *pl = reinterpret_cast<const QRgb *>(layerImg.constScanLine(y));
            for (int x = 0; x < w; ++x)
            {
                if (qAlpha(pl[x]) != 0)
                    continue;
                if (pr[x] != pn[x])
                    ++disturbed;
            }
        }
        qInfo() << "[校验1] 描边层 alpha=0 处正文像素被扰动数:" << disturbed
                << (disturbed == 0 ? "PASS" : "FAIL");
        if (disturbed != 0) ++fails;

        // ---------- 校验 2：复刻排版与 QLabel 真实渲染 alpha 逐像素对齐 ----------
        // 用透明背景参照，其 alpha 即纯文字覆盖度
        int alphaMismatch = 0, maxAlphaDiff = 0;
        for (int y = 0; y < h; ++y)
        {
            const QRgb *pr = reinterpret_cast<const QRgb *>(refClearShot.constScanLine(y));
            const QRgb *pl = reinterpret_cast<const QRgb *>(replicaImg.constScanLine(y));
            for (int x = 0; x < w; ++x)
            {
                const int d = qAbs(qAlpha(pr[x]) - qAlpha(pl[x]));
                if (d > 64)
                    ++alphaMismatch;
                maxAlphaDiff = qMax(maxAlphaDiff, d);
            }
        }
        qInfo() << "[校验2] 复刻文档 vs QLabel 真实渲染 alpha 差异>64 的像素数:" << alphaMismatch
                << "最大差:" << maxAlphaDiff
                << (alphaMismatch == 0 ? "PASS" : "FAIL");
        if (alphaMismatch != 0) ++fails;

        // 诊断：alpha 非零区域包围盒（判断是偏移还是换行宽度不同）
        auto bbox = [](const QImage &img) {
            int l = img.width(), t = img.height(), r = -1, b = -1;
            for (int y = 0; y < img.height(); ++y)
            {
                const QRgb *pl = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x)
                {
                    if (qAlpha(pl[x]) > 8)
                    {
                        l = qMin(l, x); r = qMax(r, x);
                        t = qMin(t, y); b = qMax(b, y);
                    }
                }
            }
            return QRect(l, t, qMax(0, r - l + 1), qMax(0, b - t + 1));
        };
        qInfo() << "诊断 ref bbox:" << bbox(refClearShot);
        qInfo() << "诊断 replica bbox:" << bbox(replicaImg);
        savePng(refShot, QStringLiteral("D:/BlockBox/_out/outline_diag_ref.png"));
        {
            QImage rep = refShot;
            rep.fill(Qt::white);
            QPainter p(&rep);
            p.drawImage(0, 0, replicaImg);
            p.end();
            savePng(rep, QStringLiteral("D:/BlockBox/_out/outline_diag_replica.png"));
        }

        // ---------- 校验 3：字形本体被完整抠出 ----------
        // 复刻文档中完全不透明（alpha=255）的字形核心像素上，描边层 alpha 必须为 0
        int coreLeak = 0;
        for (int y = 0; y < h; ++y)
        {
            const QRgb *pt = reinterpret_cast<const QRgb *>(replicaImg.constScanLine(y));
            const QRgb *pl = reinterpret_cast<const QRgb *>(layerImg.constScanLine(y));
            for (int x = 0; x < w; ++x)
            {
                if (qAlpha(pt[x]) == 255 && qAlpha(pl[x]) != 0)
                    ++coreLeak;
            }
        }
        qInfo() << "[校验3] 字形核心像素上描边残留数:" << coreLeak
                << (coreLeak == 0 ? "PASS" : "FAIL");
        if (coreLeak != 0) ++fails;

        // ---------- 校验 4：旧算法对照（同样的字形核心，旧掩码必然渗色）----------
        // 旧掩码 = 描边色 × 文字 alpha（含字形本体），8 方向偏移叠加，
        // 在实心字形核心处 8 邻域同为实心 → 描边层 alpha=255 → 渗色
        const QPixmap oldLayerPix = oldOutlineLayer(newLabel);
        const QImage oldLayerImg = oldLayerPix.toImage()
                                       .convertToFormat(QImage::Format_ARGB32);
        int oldCoreLeak = 0;
        const int ow2 = qMin(oldLayerImg.width(), w), oh2 = qMin(oldLayerImg.height(), h);
        for (int y = 0; y < oh2; ++y)
        {
            const QRgb *pt = reinterpret_cast<const QRgb *>(replicaImg.constScanLine(y));
            const QRgb *pl = reinterpret_cast<const QRgb *>(oldLayerImg.constScanLine(y));
            for (int x = 0; x < ow2; ++x)
            {
                if (qAlpha(pt[x]) == 255 && qAlpha(pl[x]) != 0)
                    ++oldCoreLeak;
            }
        }
        qInfo() << "[校验4] 旧算法同区域字形核心渗色数:" << oldCoreLeak
                << (oldCoreLeak > 0 ? "PASS（证明旧算法确实存在渗色）" : "FAIL（旧算法无渗色？对照失效）");
        if (oldCoreLeak <= 0) ++fails;

        qInfo() << (fails == 0 ? QStringLiteral("== 全部校验通过 ==")
                               : QStringLiteral("== %1 项校验失败 ==").arg(fails));
        exitCode = fails == 0 ? 0 : 1;
        app.exit(exitCode);
    });

    return app.exec();
}
