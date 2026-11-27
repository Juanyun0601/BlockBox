#include "utils/AiMarkdownStyle.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>

namespace
{

/// 排版用色：跟随明暗主题取一套柔和的底色，避免浅色代码块出现在深色回答里
struct Palette
{
    QColor codeBg;
    QColor inlineCodeBg;
    QColor inlineCodeText;
    QColor quoteBg;
    QColor quoteText;
    QColor tableBorder;
    QColor tableHeadBg;
    QColor link;
};

Palette makePalette(bool dark)
{
    Palette p;
    if (dark)
    {
        p.codeBg         = QColor(QStringLiteral("#262626"));
        p.inlineCodeBg   = QColor(QStringLiteral("#3a3a3a"));
        p.inlineCodeText = QColor(QStringLiteral("#f0a3b5"));
        p.quoteBg        = QColor(QStringLiteral("#262626"));
        p.quoteText      = QColor(QStringLiteral("#a9b0b8"));
        p.tableBorder    = QColor(QStringLiteral("#3f3f3f"));
        p.tableHeadBg    = QColor(QStringLiteral("#303030"));
        p.link           = QColor(QStringLiteral("#7ab8ff"));
    }
    else
    {
        p.codeBg         = QColor(QStringLiteral("#f1f3f5"));
        p.inlineCodeBg   = QColor(QStringLiteral("#f0f1f3"));
        p.inlineCodeText = QColor(QStringLiteral("#c7254e"));
        p.quoteBg        = QColor(QStringLiteral("#f5f7f9"));
        p.quoteText      = QColor(QStringLiteral("#5b6470"));
        p.tableBorder    = QColor(QStringLiteral("#d6dbe1"));
        p.tableHeadBg    = QColor(QStringLiteral("#eef1f4"));
        p.link           = QColor(QStringLiteral("#1a73e8"));
    }
    return p;
}

/// 等宽字体：优先用字重均匀的编程字体，缺字体时退回系统固定字体
QString monoFamily()
{
    static const QString family = []() {
        const QStringList families = QFontDatabase::families();
        const QStringList preferred{
            QStringLiteral("Consolas"),
            QStringLiteral("Cascadia Mono"),
            QStringLiteral("JetBrains Mono"),
            QStringLiteral("Source Code Pro"),
            QStringLiteral("DejaVu Sans Mono"),
        };
        for (const QString &name : preferred)
        {
            if (families.contains(name, Qt::CaseInsensitive))
                return name;
        }
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }();
    return family;
}

bool looksLikeMonoFamily(const QString &family)
{
    if (family.isEmpty())
        return false;
    static const QStringList hints{
        QStringLiteral("mono"), QStringLiteral("courier"), QStringLiteral("consolas"),
        QStringLiteral("menlo"), QStringLiteral("monaco"), QStringLiteral("code"),
    };
    for (const QString &hint : hints)
    {
        if (family.contains(hint, Qt::CaseInsensitive))
            return true;
    }
    return family.compare(monoFamily(), Qt::CaseInsensitive) == 0;
}

/// 字号：常规气泡 14px、紧凑模式 13px，代码/表格再小一档
struct Metrics
{
    qreal body = 10.5;       // ≈14px @96dpi
    qreal code = 9.0;        // ≈12px
    qreal inlineCode = 9.75; // ≈13px
    qreal table = 9.75;
    qreal h1 = 20.0, h2 = 17.0, h3 = 15.0; // px
};

Metrics makeMetrics(bool compact)
{
    Metrics m;
    if (compact)
    {
        m.body = 9.75;        // ≈13px
        m.code = 8.5;
        m.inlineCode = 9.0;
        m.table = 9.0;
        m.h1 = 18.0;
        m.h2 = 16.0;
        m.h3 = 14.0;
    }
    return m;
}

void styleTable(QTextTable *table, const Palette &p, const Metrics &m)
{
    if (!table)
        return;

    QTextTableFormat tf = table->format();
    tf.setBorder(1);
    tf.setBorderBrush(p.tableBorder);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setBorderCollapse(true);
    tf.setCellSpacing(0);
    tf.setCellPadding(6);
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    tf.setAlignment(Qt::AlignLeft);
    tf.setTopMargin(6);
    tf.setBottomMargin(8);
    table->setFormat(tf);

    for (int row = 0; row < table->rows(); ++row)
    {
        for (int col = 0; col < table->columns(); ++col)
        {
            QTextTableCell cell = table->cellAt(row, col);
            QTextTableCellFormat cf = cell.format().toTableCellFormat();
            cf.setBorder(1);
            cf.setBorderBrush(p.tableBorder);
            cf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            if (row == 0)
                cf.setBackground(p.tableHeadBg);
            cell.setFormat(cf);

            // 单元格文本统一字号；表头行加粗。按块整块合并格式，避免在首格
            // 用「光标选到块尾」时选不到文字导致字号/加粗丢失。
            QTextCharFormat cellFmt;
            cellFmt.setFontPointSize(m.table);
            if (row == 0)
                cellFmt.setFontWeight(QFont::Bold);

            QTextBlock block = cell.firstCursorPosition().block();
            while (block.isValid())
            {
                QTextCursor bc(block);
                QTextTable *owner = bc.currentTable();
                if (owner != table)
                    break;
                const QTextTableCell ownerCell = owner->cellAt(bc);
                if (ownerCell.row() != row || ownerCell.column() != col)
                    break;
                bc.select(QTextCursor::BlockUnderCursor);
                bc.mergeCharFormat(cellFmt);
                block = block.next();
            }
        }
    }
}

} // namespace

QString AiMarkdownStyle::toRichText(const QString &markdown, bool dark, bool compact)
{
    if (markdown.trimmed().isEmpty())
        return QString();

    const Palette p = makePalette(dark);
    const Metrics m = makeMetrics(compact);

    QTextDocument doc;
    doc.setDocumentMargin(0);
    QFont base = QApplication::font();
    base.setPointSizeF(m.body);
    base.setWeight(QFont::Normal);
    doc.setDefaultFont(base);
    doc.setMarkdown(markdown);

    bool inCodeRun = false;
    for (QTextBlock b = doc.firstBlock(); b.isValid(); b = b.next())
    {
        QTextBlockFormat bf = b.blockFormat();
        const int heading = bf.headingLevel();
        const bool code = bf.hasProperty(QTextFormat::BlockCodeFence);
        const int quote = bf.property(QTextFormat::BlockQuoteLevel).toInt();
        const bool listItem = (b.textList() != nullptr);
        const bool tableCell = (QTextCursor(b).currentTable() != nullptr);
        const qreal ruler = bf.property(QTextFormat::BlockTrailingHorizontalRulerWidth).toReal();

        bf.setTopMargin(0);
        bf.setBottomMargin(8);
        bf.setLineHeight(158, QTextBlockFormat::ProportionalHeight);

        if (ruler > 0)
        {
            // Qt 的分隔线固定用黑色绘制、样式表管不到；这里改成留白，避免大白/大黑横杠
            bf.clearProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
            bf.setTopMargin(8);
            bf.setBottomMargin(8);
            bf.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
        }
        else if (code)
        {
            if (!inCodeRun)
                bf.setTopMargin(8);
            const QTextBlock next = b.next();
            const bool lastOfRun = !next.isValid()
                                   || !next.blockFormat().hasProperty(QTextFormat::BlockCodeFence);
            bf.setBottomMargin(lastOfRun ? 8 : 0);
            bf.setLeftMargin(12);
            bf.setRightMargin(10);
            bf.setBackground(p.codeBg);
            bf.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
        }
        else if (heading > 0)
        {
            // 标题缩小到和聊天正文相称的层级，且首行贴顶不留空
            bf.setTopMargin(b.previous().isValid() ? (heading == 1 ? 6 : 14) : 0);
            bf.setBottomMargin(heading == 1 ? 8 : 6);
            bf.setLineHeight(130, QTextBlockFormat::ProportionalHeight);
        }
        else if (quote > 0)
        {
            bf.setTopMargin(6);
            bf.setBottomMargin(8);
            bf.setLeftMargin(12);
            bf.setRightMargin(8);
            bf.setBackground(p.quoteBg);
            bf.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
        }
        else if (tableCell)
        {
            bf.setBottomMargin(1);
            bf.setLineHeight(126, QTextBlockFormat::ProportionalHeight);
        }
        else if (listItem)
        {
            bf.setTopMargin(1);
            bf.setBottomMargin(1);
            bf.setLineHeight(148, QTextBlockFormat::ProportionalHeight);
        }
        else if (b.text().isEmpty())
        {
            bf.setBottomMargin(0);
            bf.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
        }

        QTextCursor cur(b);
        cur.setBlockFormat(bf);

        QTextCharFormat blockFmt;
        if (heading > 0)
        {
            const qreal px = (heading == 1) ? m.h1 : (heading == 2 ? m.h2 : m.h3);
            blockFmt.setFontPointSize(px * 0.75);
            blockFmt.setFontWeight(QFont::Bold);
        }
        else if (code)
        {
            blockFmt.setFontFamilies({monoFamily()});
            blockFmt.setFontPointSize(m.code);
        }
        else if (quote > 0)
        {
            blockFmt.setForeground(p.quoteText);
        }
        if (!blockFmt.isEmpty())
        {
            cur.select(QTextCursor::BlockUnderCursor);
            cur.mergeCharFormat(blockFmt);
        }

        // 行内格式：代码片段与链接
        const bool inTable = tableCell;
        for (auto it = b.begin(); !it.atEnd(); ++it)
        {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;

            const QTextCharFormat fmt = fragment.charFormat();
            const QStringList families = fmt.fontFamilies().toStringList();
            const bool inlineCode = !code && !families.isEmpty()
                                    && looksLikeMonoFamily(families.first());

            int end = fragment.position() + fragment.length();
            QTextCursor fc(b);
            fc.setPosition(fragment.position());
            fc.setPosition(end, QTextCursor::KeepAnchor);

            if (inlineCode)
            {
                QTextCharFormat codeFmt;
                codeFmt.setFontFamilies({monoFamily()});
                codeFmt.setFontPointSize(inTable ? m.table : m.inlineCode);
                codeFmt.setForeground(p.inlineCodeText);
                codeFmt.setBackground(p.inlineCodeBg);
                fc.mergeCharFormat(codeFmt);
            }
            else if (fmt.isAnchor())
            {
                QTextCharFormat linkFmt;
                linkFmt.setForeground(p.link);
                linkFmt.setFontUnderline(false);
                fc.mergeCharFormat(linkFmt);
            }
        }

        inCodeRun = code;
    }

    // 表格：补边框、表头底色、单元格留白
    QList<QTextTable *> tables;
    for (QTextBlock b = doc.firstBlock(); b.isValid(); b = b.next())
    {
        if (QTextTable *t = QTextCursor(b).currentTable())
        {
            if (!tables.contains(t))
                tables.append(t);
        }
    }
    for (QTextTable *t : tables)
        styleTable(t, p, m);

    return doc.toHtml();
}

void AiMarkdownStyle::applyToLabel(QLabel *label, const QString &markdown, bool dark, bool compact)
{
    if (!label)
        return;

    label->setProperty("bbAiMarkdown", markdown);
    if (markdown.trimmed().isEmpty())
    {
        label->setProperty("bbAiPlain", QString());
        label->setTextFormat(Qt::PlainText);
        label->setText(QString());
        return;
    }

    const QString html = toRichText(markdown, dark, compact);
    if (html.isEmpty())
    {
        label->setProperty("bbAiPlain", markdown);
        label->setTextFormat(Qt::PlainText);
        label->setText(markdown);
        return;
    }

    label->setTextFormat(Qt::RichText);
    label->setText(html);

    // 纯文本取自同一份 HTML 的解析结果，长度与标签内文档一致（全选定位要用）
    QTextDocument plainDoc;
    plainDoc.setHtml(html);
    label->setProperty("bbAiPlain", plainDoc.toPlainText());
}

QString AiMarkdownStyle::markdownOf(const QLabel *label)
{
    if (!label)
        return QString();
    return label->property("bbAiMarkdown").toString();
}

QString AiMarkdownStyle::plainTextOf(const QLabel *label)
{
    if (!label)
        return QString();
    const QVariant plain = label->property("bbAiPlain");
    if (plain.isValid())
        return plain.toString();
    return label->text();
}







