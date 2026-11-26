/**
 * @file   ForumPostBrowser.cpp
 * @brief  论坛帖子正文渲染实现（含大图预览对话框）
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumPostBrowser.h"

#include <QAbstractTextDocumentLayout>
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
#include <QTextFragment>
#include <QVBoxLayout>
#include <QVector>

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
        "pre { background: @BG_MUTED@; color: @TEXT_PRIMARY@;"
        "  border: 1px solid @BORDER_LIGHT@; }"
        "pre code { background: transparent; color: @TEXT_PRIMARY@; }"
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

/** 这一行是不是代码块里的行：整行都用等宽字体（Markdown 的 pre 会这么标） */
bool isMonospaceBlock(const QTextBlock &block)
{
    if (!block.isValid())
        return false;
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
    {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid() || fragment.text().trimmed().isEmpty())
            continue;
        const QStringList families = fragment.charFormat().fontFamilies().toStringList();
        for (const QString &family : families)
        {
            const QString lower = family.toLower();
            if (lower.contains(QLatin1String("consolas")) || lower.contains(QLatin1String("courier"))
                || lower.contains(QLatin1String("mono")))
                return true;
        }
    }
    return false;
}

/**
 * @brief 代码块面板：一格表格把整块代码撑满正文宽度
 *
 * Qt 的富文本里 <pre> 的底色只画到每一行的文字宽度，几行长短不一会露出
 * 锯齿状的底色；换成表格后底色、内边距和细描边都能整齐地铺满一行。
 */
QString codePanelHtml(const QStringList &lines)
{
    const QString bg = StyleKit::resolve(QStringLiteral("@BG_MSG@"));
    const QString border = StyleKit::resolve(QStringLiteral("@BORDER_LIGHT@"));
    QString body;
    for (int i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
            body += QStringLiteral("<br />");
        body += lines.at(i);
    }
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\""
                          " style=\"border:1px solid %1; background:%2;\">"
                          "<tr><td style=\"padding:9px 12px;\">%3</td></tr></table>")
        .arg(border, bg, body);
}

/**
 * @brief 引用块面板：左侧一根主色竖条 + 浅底
 *
 * Qt 的 Markdown 解析会把 > 引用变成「左右各缩进 40px 的段落」，
 * HTML 里没有 blockquote 元素，因此拿不到左边那根线；这里把连续的
 * 引用段落拼进一格表格，用单元格的左边框画出那根竖条。
 */
QString quotePanelHtml(const QStringList &paragraphs)
{
    const QString bar = StyleKit::resolve(QStringLiteral("@PRIMARY@"));
    const QString bg = StyleKit::resolve(QStringLiteral("@BG_MSG@"));
    const QString ink = StyleKit::resolve(QStringLiteral("@TEXT_SECONDARY@"));
    QString body;
    for (int i = 0; i < paragraphs.size(); ++i)
    {
        if (i > 0)
            body += QStringLiteral("<br />");
        body += paragraphs.at(i);
    }
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                          "<td style=\"border-left:3px solid %1; background:%2;"
                          " padding:6px 12px; color:%3;\">%4</td></tr></table>")
        .arg(bar, bg, ink, body);
}

/**
 * @brief 把 Markdown 生成的 HTML 里「代码块 / 引用段」换成表格面板
 *
 * 连续的 <pre> 行、连续的引用段落各自合并成一块，中间空行不影响合并。
 */
QString buildRichBlocks(const QString &html)
{
    static const QRegularExpression blockPattern(
        QStringLiteral("<pre style=\"[^\"]*\">(.*?)</pre>"
                       "|<p style=\"[^\"]*margin-left:40px;[^\"]*margin-right:40px;[^\"]*\">(.*?)</p>"),
        QRegularExpression::DotMatchesEverythingOption);

    QVector<QRegularExpressionMatch> matches;
    auto it = blockPattern.globalMatch(html);
    while (it.hasNext())
        matches.append(it.next());
    if (matches.isEmpty())
        return html;

    const auto kindOf = [](const QRegularExpressionMatch &match) {
        return match.capturedStart(1) >= 0 ? 1 /* 代码块 */ : 2 /* 引用 */;
    };
    const auto textOf = [](const QRegularExpressionMatch &match) {
        return match.capturedStart(1) >= 0 ? match.captured(1) : match.captured(2);
    };

    QString result;
    int lastEnd = 0;
    for (int i = 0; i < matches.size(); ++i)
    {
        const int kind = kindOf(matches.at(i));
        QStringList parts{textOf(matches.at(i))};
        int j = i;
        while (j + 1 < matches.size())
        {
            const QString gap = html.mid(matches.at(j).capturedEnd(0),
                                         matches.at(j + 1).capturedStart(0)
                                             - matches.at(j).capturedEnd(0));
            if (kindOf(matches.at(j + 1)) != kind || !gap.trimmed().isEmpty())
                break;
            ++j;
            parts << textOf(matches.at(j));
        }

        result += html.mid(lastEnd, matches.at(i).capturedStart(0) - lastEnd);
        result += kind == 1 ? codePanelHtml(parts) : quotePanelHtml(parts);
        lastEnd = matches.at(j).capturedEnd(0);
        i = j;
    }
    result += html.mid(lastEnd);
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

// ────────────────────────────────────────────────────────────── 教程增强语法

/** B 站视频链接 → BV 号（不匹配返回空串） */
QString bilibiliBvOf(const QString &url)
{
    static const QRegularExpression pattern(
        QStringLiteral("https?://(?:www\\.)?bilibili\\.com/video/(BV[0-9A-Za-z]+)"));
    const QRegularExpressionMatch match = pattern.match(url);
    return match.hasMatch() ? match.captured(1) : QString();
}

/**
 * @brief 把正文里的裸 B 站视频链接换成占位标记，渲染完再展开成播放卡
 *
 * 直接贴链接是教程里最常见的嵌视频方式。占位标记走 Markdown 时就是一段
 * 普通文字，不会和图片 / 链接语法打架；排完 HTML 再换回播放卡。
 * Markdown 链接里的 URL（]( 前面带右括号）不动，保留作者自己写的文字。
 */
QString replaceBilibiliVideos(const QString &markdown)
{
    static const QRegularExpression pattern(
        QStringLiteral("(?<!\\]\\()(https?://(?:www\\.)?bilibili\\.com/video/(BV[0-9A-Za-z]+))"));
    QString result;
    qsizetype lastEnd = 0;
    auto it = pattern.globalMatch(markdown);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        result += markdown.mid(lastEnd, match.capturedStart() - lastEnd);
        result += QStringLiteral("BBFVIDEO(%1)BBF").arg(match.captured(2));
        lastEnd = match.capturedEnd();
    }
    result += markdown.mid(lastEnd);
    return result;
}

/** 占位标记 → B 站播放卡：视频在正文里放不了，给一张可点击的卡片开浏览器看 */
QString videoPanelHtml(const QString &bv)
{
    const QString bg = StyleKit::resolve(QStringLiteral("@BG_MSG@"));
    const QString border = StyleKit::resolve(QStringLiteral("@BORDER_LIGHT@"));
    const QString ink = StyleKit::resolve(QStringLiteral("@TEXT_SECONDARY@"));
    const QString brand = StyleKit::resolve(QStringLiteral("@LINK@"));
    const QString url = QStringLiteral("https://www.bilibili.com/video/%1").arg(bv);
    return QStringLiteral(
               "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\""
               " style=\"border:1px solid %1; background:%2;\">"
               "<tr><td style=\"padding:9px 12px;\">"
               "<span style=\"color:%3; font-weight:700;\">▶ B 站视频</span>&nbsp;&nbsp;"
               "<a href=\"%4\">%5（在浏览器中打开观看）</a><br />"
               "<span style=\"color:%6; font-size:11px;\">启动器里看不了视频，点上面的链接到 B 站观看</span>"
               "</td></tr></table>")
        .arg(border, bg, brand, url, bv, ink);
}

/** HTML 里的占位标记统一换成播放卡 */
QString expandVideoMarkers(const QString &html)
{
    static const QRegularExpression pattern(QStringLiteral("BBFVIDEO\\((BV[0-9A-Za-z]+)\\)BBF"));
    QString result;
    qsizetype lastEnd = 0;
    auto it = pattern.globalMatch(html);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        result += html.mid(lastEnd, match.capturedStart() - lastEnd);
        result += videoPanelHtml(match.captured(1));
        lastEnd = match.capturedEnd();
    }
    result += html.mid(lastEnd);
    return result;
}

/**
 * @brief 给正文 HTML 里的一 / 二 / 三级标题加锚点，顺带生成目录
 *
 * Qt 的 Markdown 导出不带锚点，这里按出现顺序给每级标题包一层
 * <a name="sec-N">，目录点击跳转就靠这个名字定位；锚点只负责定位，
 * 标题文字颜色仍旧由标题自己决定，不会被链接色染蓝。
 */
QString injectHeadingAnchors(const QString &html, QVector<ForumTocEntry> *toc, int *headingSeq)
{
    static const QRegularExpression pattern(
        QStringLiteral("<h([1-6])([^>]*)>(.*?)</h\\1>"),
        QRegularExpression::DotMatchesEverythingOption);
    QString result;
    qsizetype lastEnd = 0;
    auto it = pattern.globalMatch(html);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const int level = match.captured(1).toInt();
        result += html.mid(lastEnd, match.capturedStart() - lastEnd);
        if (level <= 3)
        {
            const QString anchor = QStringLiteral("sec-%1").arg(++(*headingSeq));
            ForumTocEntry entry;
            entry.level = level;
            entry.anchor = anchor;
            QString text = match.captured(3);
            text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
            text.replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
                .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
                .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
                .replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
            entry.text = text.simplified();
            if (toc)
                toc->append(entry);
            // 标题文字被锚点包住后，会跟着文档样式里的 a { color: @LINK@ } 变成链接蓝，
            // 正文标题不该是这个颜色。这里在锚点上行内写回正文墨色（行内样式优先级最高，
            // 不受主题/链接色影响），跳转用的 anchor 名字保持不变。
            const QString headingInk = StyleKit::resolve(QStringLiteral("@TEXT_PRIMARY@"));
            result += QStringLiteral("<h%1%2>").arg(match.captured(1), match.captured(2))
                      + QStringLiteral("<a name=\"%1\" style=\"color:%2; text-decoration:none;\">")
                            .arg(anchor, headingInk)
                      + match.captured(3)
                      + QStringLiteral("</a></h%1>").arg(match.captured(1));
        }
        else
        {
            result += match.captured(0);
        }
        lastEnd = match.capturedEnd();
    }
    result += html.mid(lastEnd);
    return result;
}

/** 折叠块面板：标题一行常驻，内容收起 / 展开靠重新渲染 */
QString foldPanelHtml(int index, const QString &title, const QString &innerHtml)
{
    const bool open = !innerHtml.isEmpty();
    const QString bg = StyleKit::resolve(QStringLiteral("@BG_MSG@"));
    const QString border = StyleKit::resolve(QStringLiteral("@BORDER_LIGHT@"));
    const QString brand = StyleKit::resolve(QStringLiteral("@LINK@"));
    const QString ink = StyleKit::resolve(QStringLiteral("@TEXT_TERTIARY@"));
    return QStringLiteral(
               "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\""
               " style=\"border:1px solid %1; background:%2;\">"
               "<tr><td style=\"padding:8px 12px;\">"
               "<a href=\"bbf-fold://%3\" style=\"color:%4; font-weight:700;\">%5 %6</a>"
               "&nbsp;<span style=\"color:%7; font-size:11px;\">%8</span>"
               "</td></tr>%9</table>")
        .arg(border, bg, QString::number(index), brand,
             open ? QStringLiteral("▼") : QStringLiteral("▶"),
             title.toHtmlEscaped(), ink,
             open ? QStringLiteral("（点击收起）") : QStringLiteral("（点击展开）"),
             open ? QStringLiteral("<tr><td style=\"border-top:1px solid %1;"
                                    " padding:2px 12px 9px; background:transparent;\">%2</td></tr>")
                        .arg(border, innerHtml)
                  : QString());
}

/**
 * @brief 思维导图面板：```mindmap 围栏里的缩进列表排成一棵缩进树
 *
 * Qt 富文本画不出连线，这里用逐级缩进 + 圆点分层，根节点用主题色加粗，
 * 和网页版的树状图同一个阅读顺序。
 */
QString mindmapPanelHtml(const QStringList &lines)
{
    struct Row
    {
        int level = 0;
        QString text;
    };
    QVector<Row> rows;
    int previousLevel = -1;
    for (const QString &raw : lines)
    {
        if (raw.trimmed().isEmpty())
            continue;
        QString text = raw.trimmed();
        text.remove(QRegularExpression(QStringLiteral("^(?:[-*+]|\\d+[.)])\\s+")));
        int indent = 0;
        for (const QChar ch : raw)
        {
            if (ch == QLatin1Char(' '))
                indent += 1;
            else if (ch == QLatin1Char('\t'))
                indent += 2;
            else
                break;
        }
        int level = qMax(0, indent / 2);
        if (previousLevel >= 0 && level > previousLevel + 1)
            level = previousLevel + 1;   // 缩进跳档时贴回上一层，树不会断枝
        rows.append({ level, text });
        previousLevel = level;
    }
    if (rows.isEmpty())
        return QString();

    const QString bg = StyleKit::resolve(QStringLiteral("@BG_MSG@"));
    const QString border = StyleKit::resolve(QStringLiteral("@BORDER_LIGHT@"));
    const QString brand = StyleKit::resolve(QStringLiteral("@PRIMARY@"));
    const QString ink = StyleKit::resolve(QStringLiteral("@TEXT_PRIMARY@"));
    QString body;
    for (const Row &row : rows)
    {
        const bool root = row.level == 0;
        body += QStringLiteral("<tr><td style=\"padding:1px 12px 1px %1px;\">"
                               "<span style=\"color:%2; font-size:%3px;\">%4</span> "
                               "<span style=\"color:%5;%6\">%7</span></td></tr>")
                    .arg(QString::number(10 + row.level * 20), brand,
                         root ? QStringLiteral("12") : QStringLiteral("9"),
                         root ? QStringLiteral("■") : QStringLiteral("●"), ink,
                         root ? QStringLiteral(" font-weight:700;") : QString(),
                         row.text.toHtmlEscaped());
    }
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\""
                          " style=\"border:1px solid %1; background:%2;\">%3</table>")
        .arg(border, bg, body);
}

/** 正文分段：普通 Markdown / 折叠块（::: fold） / 思维导图（```mindmap） */
struct MdSegment
{
    enum Kind
    {
        Markdown,
        Fold,
        Mindmap
    };
    Kind kind = Markdown;
    QString text;
    QString title;   ///< 折叠块的标题
};

QVector<MdSegment> splitMdSegments(const QString &markdown)
{
    QVector<MdSegment> segments;
    const QStringList lines = markdown.split(QLatin1Char('\n'));

    static const QRegularExpression foldOpen(
        QStringLiteral("^:::\\s+fold\\s+(\\S.*)$"));
    static const QRegularExpression fenceOpen(QStringLiteral("^\\s*```\\s*([A-Za-z0-9]*)"));

    MdSegment current;
    bool inFence = false;
    bool inMindmap = false;
    bool inFold = false;
    QStringList buffer;
    auto flush = [&]() {
        if (!buffer.isEmpty() || current.kind == MdSegment::Markdown)
        {
            current.text = buffer.join(QLatin1Char('\n'));
            if (!current.text.trimmed().isEmpty() || current.kind != MdSegment::Markdown)
                segments.append(current);
        }
        buffer.clear();
        current = MdSegment{};
    };

    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();

        if (inMindmap)
        {
            if (trimmed.startsWith(QStringLiteral("```")))
            {
                current.text = buffer.join(QLatin1Char('\n'));
                segments.append(current);
                buffer.clear();
                inMindmap = false;
                current = MdSegment{};
            }
            else
            {
                buffer.append(line);
            }
            continue;
        }

        if (!inFence && !inFold)
        {
            const QRegularExpressionMatch fenceMatch = fenceOpen.match(line);
            if (fenceMatch.hasMatch())
            {
                if (fenceMatch.captured(1).compare(QLatin1String("mindmap"), Qt::CaseInsensitive) == 0)
                {
                    flush();
                    current.kind = MdSegment::Mindmap;
                    inMindmap = true;
                    continue;
                }
                inFence = true;
            }
            else if (trimmed.startsWith(QStringLiteral(":::")))
            {
                const QRegularExpressionMatch openMatch = foldOpen.match(trimmed);
                if (openMatch.hasMatch())
                {
                    flush();
                    current.kind = MdSegment::Fold;
                    current.title = openMatch.captured(1).trimmed();
                    inFold = true;
                    continue;
                }
            }
        }
        else if (inFence && trimmed.startsWith(QStringLiteral("```")))
        {
            inFence = false;
        }
        else if (inFold && trimmed == QLatin1String(":::"))
        {
            current.text = buffer.join(QLatin1Char('\n'));
            segments.append(current);
            buffer.clear();
            inFold = false;
            current = MdSegment{};
            continue;
        }

        buffer.append(line);
    }
    flush();
    return segments;
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
    // 正文浏览框只是排版容器：底色、边框和内边距都由外层卡片负责
    setObjectName(QStringLiteral("forumPostBrowser"));
    document()->setDocumentMargin(0);
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
        if (url.scheme() == QLatin1String("bbf-fold"))
        {
            // 折叠块标题：切换这一块的展开状态，重新渲染正文
            const int index = url.toString().remove(QStringLiteral("bbf-fold://")).toInt();
            if (index >= 0 && index < m_foldOpen.size())
            {
                m_foldOpen[index] = !m_foldOpen.at(index);
                renderDocument();
            }
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
    m_toc.clear();

    if (m_markdown.simplified().isEmpty())
    {
        setHtml(QStringLiteral("<body><span style=\"color:%1\">%2</span></body>")
                    .arg(StyleKit::resolve(QStringLiteral("@TEXT_TERTIARY@")), tr("（无正文）")));
        return;
    }

    // 分段渲染：普通 Markdown 交给 Qt，折叠块 / 思维导图 / B 站链接自己排
    const QVector<MdSegment> segments = splitMdSegments(m_markdown);
    int foldCount = 0;
    for (const MdSegment &segment : segments)
    {
        if (segment.kind == MdSegment::Fold)
            ++foldCount;
    }
    // 折叠状态按下标记住：内容没变时重渲染（换主题 / 展开收起）不丢状态
    QVector<bool> oldFoldStates = m_foldOpen;
    m_foldOpen = QVector<bool>(foldCount, false);
    for (int i = 0; i < qMin(oldFoldStates.size(), foldCount); ++i)
        m_foldOpen[i] = oldFoldStates.at(i);

    int headingSeq = 0;
    int foldIndex = 0;
    QString html;
    for (const MdSegment &segment : segments)
    {
        switch (segment.kind)
        {
        case MdSegment::Fold:
        {
            const bool open = foldIndex < m_foldOpen.size() && m_foldOpen.at(foldIndex);
            html += foldPanelHtml(foldIndex, segment.title,
                                  open ? renderMarkdownHtml(segment.text, headingSeq) : QString());
            ++foldIndex;
            break;
        }
        case MdSegment::Mindmap:
            html += mindmapPanelHtml(segment.text.split(QLatin1Char('\n')));
            break;
        case MdSegment::Markdown:
        default:
            html += renderMarkdownHtml(segment.text, headingSeq);
            break;
        }
    }

    m_html = wrapImagesWithAnchors(html, &m_imagePaths);
    applyDocument();

    requestPendingImages();
}

QString ForumPostBrowser::renderMarkdownHtml(const QString &markdown, int &headingSeq)
{
    QTextDocument converter;
    // 简介这类短文本保留作者敲下的换行，帖子正文仍按 CommonMark 排版
    converter.setMarkdown(m_compact ? hardenSingleNewlines(markdown) : markdown);
    QString raw = converter.toHtml();

    raw = injectHeadingAnchors(raw, &m_toc, &headingSeq);
    raw = buildRichBlocks(raw);
    raw = expandVideoMarkers(raw);
    return raw;
}

int ForumPostBrowser::anchorY(const QString &anchor) const
{
    QTextDocument *doc = document();
    if (!doc || anchor.isEmpty())
        return -1;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
    {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.length() <= 0)
                continue;
            if (fragment.charFormat().anchorNames().contains(anchor))
            {
                // documentMargin 为 0，块的 y 就是控件里的 y；
                // 高度贴合内容的浏览器没有内部滚动条，页面滚动区直接用这个值
                return qRound(doc->documentLayout()->blockBoundingRect(block).y());
            }
        }
    }
    return -1;
}

void ForumPostBrowser::applyDocument()
{
    setHtml(QStringLiteral("<html><head><style>%1</style></head><body>%2</body></html>")
                .arg(documentStyle(), m_html));
    if (m_compact)
        tightenParagraphs(kCompactParagraphGap, kCompactParagraphGap);
    else
        tuneBodyRhythm();
}

void ForumPostBrowser::tuneBodyRhythm()
{
    QTextDocument *doc = document();
    if (!doc)
        return;

    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
    {
        const QTextBlockFormat current = block.blockFormat();
        // 代码 / 引用面板（表格单元）里的块本来就没有段距，不要把它们再撑开
        if (current.topMargin() == 0 && current.bottomMargin() == 0)
            continue;

        // 代码块一行一段：行与行挨着，保留等宽排版的样子
        // 列表项之间只留一点点缝；普通段落按网页版的 8px 段距收尾
        int top = 2;
        int bottom = 8;
        if (isMonospaceBlock(block))
        {
            top = 0;
            bottom = 0;
        }
        else if (block.textList())
        {
            top = 1;
            bottom = 1;
        }

        QTextBlockFormat format = current;
        if (format.topMargin() == top && format.bottomMargin() == bottom)
            continue;
        format.setTopMargin(top);
        format.setBottomMargin(bottom);
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
    }
    cursor.endEditBlock();
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
