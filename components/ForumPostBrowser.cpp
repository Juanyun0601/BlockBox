/**
 * @file   ForumPostBrowser.cpp
 * @brief  论坛帖子正文渲染实现（含大图预览对话框）
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumPostBrowser.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/StyleKit.h"

namespace {

// 正文里图片的展示宽度上限，超出则等比缩小
constexpr int kMaxImageWidth = 620;

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

void ForumPostBrowser::changeEvent(QEvent *event)
{
    AutoHeightTextBrowser::changeEvent(event);
    // 主题切换后正文颜色要跟着换，整体重渲染一次
    if (event->type() == QEvent::StyleChange && !m_markdown.isEmpty())
        renderDocument();
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
    converter.setMarkdown(m_markdown);
    const QString rawHtml = converter.toHtml();

    m_html = wrapImagesWithAnchors(rawHtml, &m_imagePaths);
    setHtml(QStringLiteral("<html><head><style>%1</style></head><body>%2</body></html>")
                .arg(postBodyStyle(), m_html));

    requestPendingImages();
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
            setHtml(QStringLiteral("<html><head><style>%1</style></head><body>%2</body></html>")
                        .arg(postBodyStyle(), m_html));
        });
    }
}
