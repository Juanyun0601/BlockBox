/**
 * @file   ForumPostBrowser.cpp
 * @brief  论坛帖子正文渲染实现（含大图预览对话框）
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumPostBrowser.h"

#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/StyleKit.h"

namespace {

// 正文里图片的展示宽度上限，超出则等比缩小
constexpr int kMaxImageWidth = 620;

// 紧凑模式（简介 / 签名）里每个段落的上下间距
constexpr int kCompactParagraphGap = 2;

QString postBodyStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "body { color: @TEXT_PRIMARY@; font-size: 14px; line-height: 165%; }"
        "a { color: @LINK@; }"
        "h1 { font-size: 19px; font-weight: 700; }"
        "h2 { font-size: 17px; font-weight: 700; }"
        "h3 { font-size: 15px; font-weight: 700; }"
        "blockquote { color: @TEXT_SECONDARY@; margin: 6px 0 6px 4px; padding: 1px 0 1px 12px;"
        "  border-left: 3px solid @BORDER@; }"
        "code { background: @CODE_BG@; color: @CODE_TEXT@;"
        "  font-family: Consolas, 'Courier New', monospace; }"
        "pre { background: @CODE_BG@; color: @CODE_TEXT@; border: 1px solid @CODE_BORDER@; }"
        "hr { border: none; border-top: 1px solid @BORDER_LIGHT@; }"
        "td, th { border: 1px solid @BORDER@; padding: 3px 8px; }"));
}

/** 紧凑模式追加的样式：写在正文样式之后，用来压字号和标题层级 */
QString compactBodyStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "body { font-size: 13.5px; line-height: 150%; }"
        "h1 { font-size: 15px; }"
        "h2 { font-size: 14.5px; }"
        "h3 { font-size: 14px; }"
        "h4, h5, h6 { font-size: 13.5px; }"
        "blockquote { margin: 3px 0 3px 2px; padding: 1px 0 1px 9px; }"
        "code, pre { font-size: 12.5px; }"
        "td, th { padding: 2px 6px; }"));
}

/**
 * @brief 把「单个换行」升级成 Markdown 硬换行（行尾补两个空格）
 *
 * Qt 的 Markdown 解析按 CommonMark 把单个换行并成一个空格，多行简介会挤成
 * 一整段。简介是用户在输入框里按回车换的行，这里把回车原样保留；
 * 围栏代码块里的换行本来就是硬换行，不再处理。
 */
QString hardenSingleNewlines(const QString &markdown)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QString result;
    result.reserve(markdown.size() + lines.size() * 2);

    bool inFence = false;
    for (qsizetype i = 0; i < lines.size(); ++i)
    {
        const QString line = lines.at(i);
        const QString trimmed = line.trimmed();
        const bool fenceLine = trimmed.startsWith(QStringLiteral("```"))
                               || trimmed.startsWith(QStringLiteral("~~~"));
        if (fenceLine)
            inFence = !inFence;

        result += line;
        const bool hasNext = (i + 1 < lines.size());
        if (hasNext && !inFence && !fenceLine && !trimmed.isEmpty())
        {
            // 已经是硬换行（行尾两个空格或反斜杠）就不用再补
            const bool alreadyHard = line.endsWith(QStringLiteral("  "))
                                     || line.endsWith(QLatin1Char('\\'));
            if (!alreadyHard)
                result += QStringLiteral("  ");
        }
        if (hasNext)
            result += QLatin1Char('\n');
    }
    return result;
}

/** 把正文 HTML 里的 <img> 包进 bbf-image 链接，点图弹大图 */
QString wrapImagesWithAnchors(const QString &html, QStringList *outPaths)
{
    static const QRegularExpression pattern(
        QStringLiteral("<img src=\"([^\"]+)\"([^>]*)/?>"));
    QString result;
    qsizetype lastEnd = 0;
    auto it = pattern.globalMatch(html);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        result += html.mid(lastEnd, match.capturedStart() - lastEnd);
        const QString path = match.captured(1);
        result += QStringLiteral("<a href=\"bbf-image://%1\"><img src=\"%2\"%3></a>")
                      .arg(path, path, match.captured(2));
        if (outPaths && !outPaths->contains(path))
            outPaths->append(path);
        lastEnd = match.capturedEnd();
    }
    result += html.mid(lastEnd);
    return result;
}

} // namespace

// ────────────────────────────────────────────────────────────── 大图预览

/** 全屏半透明遮罩 + 居中图片，点击任意处关闭 */
class ForumImagePreviewDialog : public QDialog
{
public:
    explicit ForumImagePreviewDialog(const QImage &image, QWidget *parent)
        : QDialog(parent)
    {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setModal(true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 24, 24, 24);

        auto *label = new QLabel(this);
        QPixmap pixmap = QPixmap::fromImage(image);
        QSize available = QGuiApplication::primaryScreen()->availableSize() * 0.86;
        if (pixmap.width() > available.width() || pixmap.height() > available.height())
            pixmap = pixmap.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        label->setPixmap(pixmap);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QDialog::mousePressEvent(event);
        close();
    }
};

// ────────────────────────────────────────────────────────────── 正文渲染

void ForumPostBrowser::showImagePreview(const QString &path, QWidget *parent)
{
    const QString absolute = ForumAccountService::instance()->absoluteUrl(path);
    QPointer<QWidget> parentGuard(parent);
    ForumImageStore::instance()->requestImage(absolute, [parentGuard](const QImage &image) {
        if (parentGuard.isNull())
            return;
        if (image.isNull())
        {
            NotificationManager::showInfo(parentGuard, ForumPostBrowser::tr("图片加载失败，稍后再试"));
            return;
        }
        ForumImagePreviewDialog preview(image, parentGuard);
        preview.exec();
    });
}

ForumPostBrowser::ForumPostBrowser(QWidget *parent)
    : AutoHeightTextBrowser(parent)
{
    setOpenLinks(false);
    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QLatin1String("bbf-image"))
        {
            // 自定义 scheme：bhf-image:// + 站内路径
            QString raw = url.toString();
            raw.remove(QStringLiteral("bbf-image://"));
            emit imageClicked(QUrl::fromPercentEncoding(raw.toUtf8()));
            return;
        }
        emit linkClicked(url);
    });
}

void ForumPostBrowser::setPostMarkdown(const QString &markdown)
{
    if (m_markdown == markdown && !m_html.isEmpty())
        return;
    m_markdown = markdown;
    renderDocument();
}

void ForumPostBrowser::setCompactMode(bool on)
{
    if (m_compact == on)
        return;
    m_compact = on;
    // 已经渲染过内容时按新样式重排一遍（标题字号、段落间距都不一样）
    if (!m_markdown.isEmpty())
        renderDocument();
}

void ForumPostBrowser::changeEvent(QEvent *event)
{
    AutoHeightTextBrowser::changeEvent(event);
    // 主题切换后正文颜色要跟着换，整体重渲染一次
    if (event->type() == QEvent::StyleChange && !m_markdown.isEmpty())
        renderDocument();
}

QString ForumPostBrowser::documentStyle() const
{
    return m_compact ? postBodyStyle() + compactBodyStyle() : postBodyStyle();
}

void ForumPostBrowser::renderDocument()
{
    m_html.clear();
    m_imagePaths.clear();

    if (m_markdown.simplified().isEmpty())
    {
        setHtml(QStringLiteral("<body><span style=\"color:%1\">%2</span></body>")
                    .arg(StyleKit::resolve(QStringLiteral("@TEXT_TERTIARY@")), tr("（无正文）")));
        return;
    }

    QTextDocument converter;
    // 简介这类短文本保留作者敲下的换行，帖子正文仍按 CommonMark 排版
    converter.setMarkdown(m_compact ? hardenSingleNewlines(m_markdown) : m_markdown);
    const QString rawHtml = converter.toHtml();

    m_html = wrapImagesWithAnchors(rawHtml, &m_imagePaths);
    applyDocument();

    requestPendingImages();
}

void ForumPostBrowser::applyDocument()
{
    setHtml(QStringLiteral("<html><head><style>%1</style></head><body>%2</body></html>")
                .arg(documentStyle(), m_html));
    if (m_compact)
        tightenParagraphs(kCompactParagraphGap, kCompactParagraphGap);
}

void ForumPostBrowser::tightenParagraphs(int top, int bottom)
{
    QTextDocument *doc = document();
    if (!doc)
        return;

    // Markdown 解析会给每段加较为宽松的外边距，短文本里显得很空，
    // 这里统一收紧（只动上下间距，引用块的左缩进保留）。
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
    {
        QTextBlockFormat format = block.blockFormat();
        if (format.topMargin() == top && format.bottomMargin() == bottom)
            continue;
        format.setTopMargin(top);
        format.setBottomMargin(bottom);
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
    }
    cursor.endEditBlock();
}

void ForumPostBrowser::requestPendingImages()
{
    auto *service = ForumAccountService::instance();
    for (const QString &path : m_imagePaths)
    {
        if (m_resolvedImages.contains(path))
            continue;
        m_resolvedImages.insert(path);

        QPointer<ForumPostBrowser> guard(this);
        const QString absolute = service->absoluteUrl(path);
        ForumImageStore::instance()->requestImage(absolute,
                                                  [this, guard, path, absolute](const QImage &image) {
            if (guard.isNull() || image.isNull())
                return;

            QImage scaled = image;
            if (scaled.width() > kMaxImageWidth)
                scaled = scaled.scaledToWidth(kMaxImageWidth, Qt::SmoothTransformation);
            document()->addResource(QTextDocument::ImageResource, QUrl(path), scaled);
            document()->addResource(QTextDocument::ImageResource, QUrl(absolute), scaled);

            // 重新设置一遍 HTML 触发重排（AutoHeightTextBrowser 会跟着贴合新高度）
            applyDocument();
        });
    }
}
