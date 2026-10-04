/**
 * @file   ModConfigParser.cpp
 * @brief  模组配置文件解析/表单模型/回写器实现
 * @author BlockBox Team
 * @date   2026-08-29
 */

#include "ModConfigParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

/** 支持编辑的配置文件后缀 */
const QStringList kConfigSuffixes = {
    QStringLiteral("toml"),
    QStringLiteral("json"),
    QStringLiteral("cfg"),
    QStringLiteral("properties"),
    QStringLiteral("ini"),
    QStringLiteral("yaml"),
    QStringLiteral("yml"),
    QStringLiteral("conf"),
};

/** 内容扫描兜底时额外接受的文本后缀 */
const QStringList kContentScanSuffixes = {
    QStringLiteral("txt"),
    QStringLiteral("log"),
    QStringLiteral("csv"),
    QStringLiteral("md"),
};

const QRegularExpression &intRegex()
{
    static const QRegularExpression re(QStringLiteral("^[+-]?[0-9][0-9_]*$"));
    return re;
}

/** 浮点:整数已先行匹配,此处捕获带小数点/指数的数字 */
const QRegularExpression &floatRegex()
{
    static const QRegularExpression re(
        QStringLiteral("^[+-]?[0-9][0-9_]*([.][0-9_]*)?([eE][+-]?[0-9_]+)?$"));
    return re;
}

/** 归一化标识:小写 + 仅保留字母数字与 _.-(用于配置文件名匹配) */
QString normalizeKey(const QString &s)
{
    static const QRegularExpression strip(QStringLiteral("[^a-z0-9_.\\-]"));
    QString out = s.toLower();
    out.remove(strip);
    return out;
}

/** TOML 双精度写回文本:保证带小数点(裸整数会被解析为 int) */
QString formatTomlDouble(double v)
{
    QString s = QString::number(v, 'g', 15);
    if (!s.contains(QLatin1Char('.')) && !s.contains(QLatin1Char('e'))
        && !s.contains(QLatin1Char('E')))
    {
        s += QStringLiteral(".0");
    }
    return s;
}

/** 是否为可直接回写的字面量(token):数字/布尔保持原样,其余按字符串加引号 */
bool isScalarToken(const QString &t)
{
    if (t == QLatin1String("true") || t == QLatin1String("false"))
        return true;
    return intRegex().match(t).hasMatch() || floatRegex().match(t).hasMatch();
}

/** 找到行内第一个引号外的 '#'(TOML 行尾注释),找不到返回 -1 */
int indexOfInlineComment(const QString &text)
{
    bool inString = false;
    for (int i = 0; i < text.size(); ++i)
    {
        const QChar c = text.at(i);
        if (inString)
        {
            if (c == QLatin1Char('\\'))
            {
                ++i; // 跳过转义字符
                continue;
            }
            if (c == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (c == QLatin1Char('"'))
            inString = true;
        else if (c == QLatin1Char('#'))
            return i;
    }
    return -1;
}

/** 按引号外的逗号切分数组元素(保留元素内转义) */
QStringList splitArrayElements(const QString &inner)
{
    QStringList out;
    QString current;
    bool inString = false;
    for (int i = 0; i < inner.size(); ++i)
    {
        const QChar c = inner.at(i);
        if (inString)
        {
            current += c;
            if (c == QLatin1Char('\\') && i + 1 < inner.size())
            {
                current += inner.at(++i);
                continue;
            }
            if (c == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (c == QLatin1Char('"'))
        {
            inString = true;
            current += c;
        }
        else if (c == QLatin1Char(','))
        {
            out.append(current.trimmed());
            current.clear();
        }
        else
        {
            current += c;
        }
    }
    const QString tail = current.trimmed();
    if (!tail.isEmpty())
        out.append(tail);
    return out;
}

/** 截断过长的只读原文展示 */
QString elideRawText(const QString &text, int limit = 120)
{
    if (text.size() <= limit)
        return text;
    return text.left(limit) + QStringLiteral("…");
}

} // namespace

// ────────────────────────── Document ──────────────────────────

// 使用默认实现(QList/QJsonDocument 均可隐式共享)

// ────────────────────────── 格式识别与解析 ──────────────────────────

ModConfigParser::Format ModConfigParser::detectFormat(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QLatin1String("toml"))
        return Format::Toml;
    if (suffix == QLatin1String("cfg"))
        return Format::ForgeCfg;
    if (suffix == QLatin1String("properties"))
        return Format::Properties;
    if (suffix == QLatin1String("json"))
        return Format::Json;

    // 非常规后缀按内容嗅探
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return Format::Unknown;
    const QString head = QString::fromUtf8(file.read(4096));
    if (head.trimmed().startsWith(QLatin1Char('{')))
        return Format::Json;
    static const QRegularExpression cfgPrefixRe(QStringLiteral("(^|\\n)\\s*[BIDS]:"));
    if (head.contains(cfgPrefixRe))
        return Format::ForgeCfg;
    if (head.contains(QLatin1Char('=')))
        return Format::Toml;
    return Format::Unknown;
}

bool ModConfigParser::parse(const QString &filePath, Document &out)
{
    out = Document();
    out.filePath = filePath;
    out.format = detectFormat(filePath);
    if (out.format == Format::Unknown)
    {
        out.error = QStringLiteral("无法识别的配置文件格式");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        out.error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();

    if (out.format == Format::Json)
        return parseJson(data, out);

    const QString content = QString::fromUtf8(data);
    QStringList lines = content.split(QLatin1Char('\n'));
    // 统一去除行尾 \r,写回时以文本模式重新生成平台换行
    for (QString &l : lines)
    {
        while (l.endsWith(QLatin1Char('\r')))
            l.chop(1);
    }
    out.rawLines = lines;
    return parseLineBased(lines, out.format, out);
}

bool ModConfigParser::parseLineBased(const QStringList &lines, Format format, Document &out)
{
    out.sections.append(Section()); // 根节
    Section *current = &out.sections.last();
    QStringList pendingComments;

    for (int i = 0; i < lines.size(); ++i)
    {
        const QString &raw = lines.at(i);
        const QString t = raw.trimmed();

        if (t.isEmpty())
        {
            pendingComments.clear();
            continue;
        }
        if (t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char('!')))
        {
            pendingComments.append(t.mid(1).trimmed());
            continue;
        }

        // ── 分节头 ──
        if (format == Format::ForgeCfg && t.endsWith(QLatin1Char('{')))
        {
            const QString name = t.left(t.size() - 1).trimmed();
            Section sec;
            sec.path = current->path.isEmpty()
                ? name
                : current->path + QLatin1Char('.') + name;
            out.sections.append(sec);
            current = &out.sections.last();
            pendingComments.clear();
            continue;
        }
        if (format == Format::ForgeCfg && t == QLatin1String("}"))
        {
            // 弹回父节
            const int dot = current->path.lastIndexOf(QLatin1Char('.'));
            const QString parentPath = dot < 0 ? QString() : current->path.left(dot);
            current = &out.sections.first();
            if (!parentPath.isEmpty())
            {
                for (Section &s : out.sections)
                {
                    if (s.path == parentPath)
                    {
                        current = &s;
                        break;
                    }
                }
            }
            pendingComments.clear();
            continue;
        }
        if (format == Format::Toml && t.startsWith(QLatin1Char('[')))
        {
            // 表头可能带行尾注释(如 "[general] # 客户端配置"),取最后一个 ']' 之前
            const bool isArrayTable = t.startsWith(QLatin1String("[["));
            const int close = t.lastIndexOf(QLatin1Char(']'));
            QString name;
            if (isArrayTable && close >= 3)
                name = t.mid(2, close - 3).trimmed();
            else if (!isArrayTable && close >= 1)
                name = t.mid(1, close - 1).trimmed();
            if (!name.isEmpty())
            {
                // [[数组表]] 同样按分节分组:表头行不会被改动,节内键照常行级回写
                Section sec;
                sec.path = name;
                out.sections.append(sec);
                current = &out.sections.last();
                pendingComments.clear();
                continue;
            }
        }

        // ── 键值行 ──
        int eq = t.indexOf(QLatin1Char('='));
        if (format == Format::Properties)
        {
            const int colon = t.indexOf(QLatin1Char(':'));
            if (colon >= 0 && (eq < 0 || colon < eq))
                eq = colon;
        }
        if (eq <= 0)
        {
            pendingComments.clear();
            continue;
        }

        Option opt;
        opt.line = i;
        opt.keyRaw = t.left(eq).trimmed();
        if (format == Format::Properties)
            opt.separator = t.at(eq);

        if (format == Format::ForgeCfg)
        {
            // 类型前缀 B:/I:/D:/S:
            if (opt.keyRaw.size() >= 2 && opt.keyRaw.at(1) == QLatin1Char(':'))
            {
                const QChar letter = opt.keyRaw.at(0).toUpper();
                opt.key = opt.keyRaw.mid(2);
                switch (letter.unicode())
                {
                case 'B': opt.type = ValueType::Boolean; break;
                case 'I': opt.type = ValueType::Integer; break;
                case 'D': opt.type = ValueType::Floating; break;
                case 'S': opt.type = ValueType::String; break;
                default: opt.type = ValueType::Unsupported; break;
                }
            }
            else
            {
                opt.key = opt.keyRaw;
                opt.type = ValueType::String;
            }
        }
        else
        {
            opt.key = opt.keyRaw;
            if (opt.key.startsWith(QLatin1Char('"')) && opt.key.endsWith(QLatin1Char('"'))
                && opt.key.size() >= 2)
            {
                opt.key = unescapeTomlString(opt.key.mid(1, opt.key.size() - 2));
            }
        }

        QString valueText = t.mid(eq + 1);
        if (format == Format::Toml)
        {
            const int hash = indexOfInlineComment(valueText);
            if (hash >= 0)
            {
                opt.inlineComment = valueText.mid(hash + 1).trimmed();
                valueText = valueText.left(hash).trimmed();
            }
            else
            {
                valueText = valueText.trimmed();
            }
            parseTomlValue(valueText, opt);
        }
        else
        {
            valueText = valueText.trimmed();
            if (format == Format::ForgeCfg && opt.type == ValueType::Unsupported)
            {
                opt.unsupported = true;
                opt.rawText = elideRawText(t);
            }
            else if (valueText == QLatin1String("true")
                     || valueText == QLatin1String("false"))
            {
                opt.type = ValueType::Boolean;
                opt.value = (valueText == QLatin1String("true"));
            }
            else if (intRegex().match(valueText).hasMatch())
            {
                opt.type = ValueType::Integer;
                opt.value = valueText.remove(QLatin1Char('_')).toLongLong();
            }
            else if (valueText.contains(QLatin1Char('.'))
                     && floatRegex().match(valueText).hasMatch())
            {
                opt.type = ValueType::Floating;
                opt.value = valueText.toDouble();
            }
            else
            {
                opt.type = ValueType::String;
                opt.value = valueText;
            }
        }

        applyCommentMetadata(pendingComments, opt);
        if (!pendingComments.isEmpty())
            opt.comment = pendingComments.join(QLatin1Char('\n'));
        pendingComments.clear();
        current->options.append(opt);
    }
    return true;
}

void ModConfigParser::parseTomlValue(const QString &text, Option &opt)
{
    opt.rawText = text;
    if (text.isEmpty())
    {
        // 值在后续行:多行数组 / 多行字符串,只读保留
        opt.unsupported = true;
        opt.rawText = elideRawText(opt.keyRaw + QStringLiteral(" = …"));
        return;
    }
    if (text == QLatin1String("true") || text == QLatin1String("false"))
    {
        opt.type = ValueType::Boolean;
        opt.value = (text == QLatin1String("true"));
        return;
    }
    if (text.startsWith(QLatin1Char('"')))
    {
        if (text.startsWith(QLatin1String("\"\"\"")))
        {
            opt.unsupported = true;
            return;
        }
        if (text.size() >= 2 && text.endsWith(QLatin1Char('"'))
            && !text.endsWith(QLatin1String("\\\"")))
        {
            opt.type = ValueType::String;
            opt.value = unescapeTomlString(text.mid(1, text.size() - 2));
            return;
        }
        opt.unsupported = true;
        return;
    }
    if (text.startsWith(QLatin1Char('\'')))
    {
        if (text.startsWith(QLatin1String("'''")))
        {
            opt.unsupported = true;
            return;
        }
        if (text.size() >= 2 && text.endsWith(QLatin1Char('\'')))
        {
            opt.type = ValueType::String;
            opt.value = text.mid(1, text.size() - 2); // 字面字符串不转义
            return;
        }
        opt.unsupported = true;
        return;
    }
    if (text.startsWith(QLatin1Char('{')))
    {
        opt.unsupported = true; // 内联表
        return;
    }
    if (text.startsWith(QLatin1Char('[')))
    {
        if (!text.endsWith(QLatin1Char(']')))
        {
            opt.unsupported = true; // 跨行数组
            opt.rawText = elideRawText(opt.keyRaw + QStringLiteral(" = …"));
            return;
        }
        const QString inner = text.mid(1, text.size() - 2).trimmed();
        if (inner.isEmpty())
        {
            opt.type = ValueType::StringList;
            opt.value = QStringList();
            return;
        }
        const QStringList elems = splitArrayElements(inner);
        QStringList tokens;
        bool allScalar = true;
        for (const QString &e : elems)
        {
            if (e.isEmpty() || !isScalarToken(e))
            {
                allScalar = false;
                break;
            }
            tokens.append(e);
        }
        if (allScalar)
        {
            // 元素存原始字面(含引号),回写时逐字保留类型
            opt.type = ValueType::StringList;
            opt.value = tokens;
        }
        else
        {
            opt.unsupported = true; // 含嵌套结构
        }
        return;
    }
    if (intRegex().match(text).hasMatch())
    {
        opt.type = ValueType::Integer;
        opt.value = QString(text).remove(QLatin1Char('_')).toLongLong();
        return;
    }
    if (floatRegex().match(text).hasMatch())
    {
        opt.type = ValueType::Floating;
        opt.value = QString(text).remove(QLatin1Char('_')).toDouble();
        return;
    }
    opt.unsupported = true; // 日期时间等
}

void ModConfigParser::applyCommentMetadata(const QStringList &comments, Option &opt)
{
    static const QRegularExpression rangeRe(
        QStringLiteral("^Range:\\s*(\\S+)\\s*~\\s*(\\S+)$"),
        QRegularExpression::CaseInsensitiveOption);

    for (const QString &c : comments)
    {
        if (c.startsWith(QLatin1String("Allowed Values:"), Qt::CaseInsensitive))
        {
            const QString rest = c.mid(15).trimmed();
            const QStringList vals = rest.split(QRegularExpression(QStringLiteral("[,/|]")));
            for (QString v : vals)
            {
                v = v.trimmed();
                if (!v.isEmpty() && !opt.allowedValues.contains(v))
                    opt.allowedValues.append(v);
            }
        }
        else if (c.startsWith(QLatin1String("Valid Values:"), Qt::CaseInsensitive))
        {
            const QString rest = c.mid(13).trimmed();
            const QStringList vals = rest.split(QRegularExpression(QStringLiteral("[,/|]")));
            for (QString v : vals)
            {
                v = v.trimmed();
                if (!v.isEmpty() && !opt.allowedValues.contains(v))
                    opt.allowedValues.append(v);
            }
        }
        else if (c.startsWith(QLatin1String("Default:"), Qt::CaseInsensitive))
        {
            opt.defaultValue = c.mid(8).trimmed();
        }
        else
        {
            const auto m = rangeRe.match(c);
            if (m.hasMatch())
            {
                const QString loText = m.captured(1);
                const QString hiText = m.captured(2);
                bool okLo = false, okHi = false;
                const double lo = loText.toDouble(&okLo);
                const double hi = hiText.toDouble(&okHi);
                if (okLo && okHi && hi >= lo)
                {
                    opt.hasRange = true;
                    opt.rangeMin = lo;
                    opt.rangeMax = hi;
                    opt.rangeIsInt = !loText.contains(QLatin1Char('.'))
                        && !loText.contains(QLatin1Char('e'))
                        && !loText.contains(QLatin1Char('E'))
                        && !hiText.contains(QLatin1Char('.'))
                        && !hiText.contains(QLatin1Char('e'))
                        && !hiText.contains(QLatin1Char('E'));
                }
            }
        }
    }
}

bool ModConfigParser::parseJson(const QByteArray &data, Document &out)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull() || !doc.isObject())
    {
        out.error = parseError.errorString();
        return false;
    }
    out.jsonDoc = doc;
    walkJsonObject(doc.object(), QString(), out);
    return true;
}

namespace {
// JSON 键名可能本身包含 '.',直接按点分割会把 "a.b" 拆成两级路径,
// 回写时会新增垃圾嵌套对象且用户编辑静默丢失。构建路径时对键转义,写回时再解析。
QString escapeJsonPathKey(const QString &key)
{
    QString out;
    out.reserve(key.size() + 8);
    for (const QChar c : key) {
        if (c == QLatin1Char('\\'))
            out += QLatin1String("\\\\");
        else if (c == QLatin1Char('.'))
            out += QLatin1String("\\.");
        else
            out += c;
    }
    return out;
}

QStringList splitJsonPath(const QString &path)
{
    QStringList parts;
    QString cur;
    bool escaped = false;
    for (const QChar c : path) {
        if (escaped) {
            cur += c;
            escaped = false;
        } else if (c == QLatin1Char('\\')) {
            escaped = true;
        } else if (c == QLatin1Char('.')) {
            parts.append(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.append(cur);
    return parts;
}
} // namespace

void ModConfigParser::walkJsonObject(const QJsonObject &obj, const QString &path, Document &out)
{
    // 先创建本节(根节因此排在最前),按索引填充选项(指针会因追加失效)
    Section sec;
    sec.path = path;
    const int sectionIndex = out.sections.size();
    out.sections.append(sec);

    for (auto it = obj.begin(); it != obj.end(); ++it)
    {
        const QString key = it.key();
        const QJsonValue v = it.value();
        Option opt;
        opt.key = key;
        opt.jsonPath = path.isEmpty() ? escapeJsonPathKey(key)
                                      : path + QLatin1Char('.') + escapeJsonPathKey(key);

        if (v.isBool())
        {
            opt.type = ValueType::Boolean;
            opt.value = v.toBool();
        }
        else if (v.isDouble())
        {
            const double d = v.toDouble();
            if (std::fabs(d - std::round(d)) < 1e-9 && std::fabs(d) < 1e15)
            {
                opt.type = ValueType::Integer;
                opt.value = static_cast<qlonglong>(std::round(d));
            }
            else
            {
                opt.type = ValueType::Floating;
                opt.value = d;
            }
        }
        else if (v.isString())
        {
            opt.type = ValueType::String;
            opt.value = v.toString();
        }
        else if (v.isArray())
        {
            const QJsonArray arr = v.toArray();
            QStringList tokens;
            bool allScalar = !arr.isEmpty();
            for (const QJsonValue &e : arr)
            {
                if (e.isString())
                    tokens.append(e.toString());
                else if (e.isBool())
                    tokens.append(e.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
                else if (e.isDouble())
                    tokens.append(QString::number(e.toDouble()));
                else
                {
                    allScalar = false;
                    break;
                }
            }
            if (allScalar)
            {
                opt.type = ValueType::StringList;
                opt.value = tokens;
            }
            else
            {
                opt.unsupported = true;
                opt.rawText = elideRawText(
                    QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            }
        }
        else if (v.isObject())
        {
            // 嵌套对象 → 子分节,不作为选项展示
            walkJsonObject(v.toObject(), opt.jsonPath, out);
            continue;
        }
        else
        {
            opt.unsupported = true; // null
            opt.rawText = QStringLiteral("null");
        }

        out.sections[sectionIndex].options.append(opt);
    }
}

// ────────────────────────── 回写 ──────────────────────────

QString ModConfigParser::escapeTomlString(const QString &s)
{
    QString out = QStringLiteral("\"");
    for (const QChar c : s)
    {
        switch (c.unicode())
        {
        case '"': out += QStringLiteral("\\\""); break;
        case '\\': out += QStringLiteral("\\\\"); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\t': out += QStringLiteral("\\t"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        default: out += c; break;
        }
    }
    out += QStringLiteral("\"");
    return out;
}

QString ModConfigParser::unescapeTomlString(const QString &s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i)
    {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\') && i + 1 < s.size())
        {
            const QChar n = s.at(++i);
            switch (n.unicode())
            {
            case 'n': out += QLatin1Char('\n'); break;
            case 't': out += QLatin1Char('\t'); break;
            case 'r': out += QLatin1Char('\r'); break;
            case '"': out += QLatin1Char('"'); break;
            case '\\': out += QLatin1Char('\\'); break;
            default: out += n; break;
            }
        }
        else
        {
            out += c;
        }
    }
    return out;
}

QString ModConfigParser::tomlValueText(const Option &opt)
{
    switch (opt.type)
    {
    case ValueType::Boolean:
        return opt.value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case ValueType::Integer:
        return QString::number(opt.value.toLongLong());
    case ValueType::Floating:
        return formatTomlDouble(opt.value.toDouble());
    case ValueType::String:
        return escapeTomlString(opt.value.toString());
    case ValueType::StringList:
    {
        const QStringList tokens = opt.value.toStringList();
        QStringList out;
        for (const QString &tok : tokens)
        {
            // 字面量 token(数字/布尔)原样回写,其余按字符串加引号
            if (isScalarToken(tok) && !tok.startsWith(QLatin1Char('"')))
                out.append(tok);
            else if (tok.startsWith(QLatin1Char('"')) && tok.endsWith(QLatin1Char('"'))
                     && tok.size() >= 2)
                out.append(tok); // 未被编辑过的原始字面
            else
                out.append(escapeTomlString(tok));
        }
        return QStringLiteral("[") + out.join(QStringLiteral(", ")) + QStringLiteral("]");
    }
    default:
        return opt.rawText;
    }
}

QString ModConfigParser::plainValueText(const Option &opt)
{
    switch (opt.type)
    {
    case ValueType::Boolean:
        return opt.value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case ValueType::Integer:
        return QString::number(opt.value.toLongLong());
    case ValueType::Floating:
        return QString::number(opt.value.toDouble(), 'g', 15);
    case ValueType::String:
    case ValueType::StringList:
        return opt.value.toString();
    default:
        return opt.rawText;
    }
}

bool ModConfigParser::saveLineBased(Document &doc, QString *error)
{
    QStringList out = doc.rawLines;
    for (const Section &sec : doc.sections)
    {
        for (const Option &opt : sec.options)
        {
            if (!opt.edited || opt.unsupported)
                continue;
            if (opt.line < 0 || opt.line >= out.size())
                continue;

            const QString valueText = (doc.format == Format::Toml)
                ? tomlValueText(opt)
                : plainValueText(opt);
            if (doc.format == Format::Toml)
            {
                out[opt.line] = opt.keyRaw + QStringLiteral(" = ") + valueText
                    + (opt.inlineComment.isEmpty()
                           ? QString()
                           : QStringLiteral(" # ") + opt.inlineComment);
            }
            else
            {
                // ForgeCfg 的 keyRaw 已含类型前缀;properties 保留原分隔符
                out[opt.line] = opt.keyRaw + opt.separator + valueText;
            }
        }
    }

    QSaveFile file(doc.filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(out.join(QLatin1Char('\n')).toUtf8());
    if (!file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }

    doc.rawLines = out;
    for (Section &sec : doc.sections)
    {
        for (Option &opt : sec.options)
            opt.edited = false;
    }
    return true;
}

bool ModConfigParser::saveJson(Document &doc, QString *error)
{
    if (doc.jsonDoc.isNull() || !doc.jsonDoc.isObject())
    {
        if (error)
            *error = QStringLiteral("JSON 文档无效");
        return false;
    }
    QJsonObject root = doc.jsonDoc.object();

    // 深度写入:按点分路径定位键
    std::function<void(QJsonObject &, const QStringList &, const Option &)> writePath =
        [&](QJsonObject &parent, const QStringList &keys, const Option &opt)
    {
        if (keys.isEmpty())
            return;
        const QString key = keys.first();
        if (keys.size() == 1)
        {
            switch (opt.type)
            {
            case ValueType::Boolean:
                parent.insert(key, opt.value.toBool());
                break;
            case ValueType::Integer:
                parent.insert(key, static_cast<double>(opt.value.toLongLong()));
                break;
            case ValueType::Floating:
                parent.insert(key, opt.value.toDouble());
                break;
            case ValueType::String:
                parent.insert(key, opt.value.toString());
                break;
            case ValueType::StringList:
            {
                QJsonArray arr;
                for (const QString &tok : opt.value.toStringList())
                {
                    if (tok == QLatin1String("true") || tok == QLatin1String("false"))
                        arr.append(tok == QLatin1String("true"));
                    else if (intRegex().match(tok).hasMatch()
                             || floatRegex().match(tok).hasMatch())
                        arr.append(tok.toDouble());
                    else
                        arr.append(tok);
                }
                parent.insert(key, arr);
                break;
            }
            default:
                break;
            }
            return;
        }
        QJsonObject child = parent.value(key).toObject();
        writePath(child, keys.mid(1), opt);
        parent.insert(key, child);
    };

    for (const Section &sec : doc.sections)
    {
        for (const Option &opt : sec.options)
        {
            if (!opt.edited || opt.unsupported)
                continue;
            writePath(root, splitJsonPath(opt.jsonPath), opt);
        }
    }

    doc.jsonDoc = QJsonDocument(root);
    QSaveFile file(doc.filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(doc.jsonDoc.toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }

    for (Section &sec : doc.sections)
    {
        for (Option &opt : sec.options)
            opt.edited = false;
    }
    return true;
}

bool ModConfigParser::save(Document &doc, QString *error)
{
    if (doc.format == Format::Json)
        return saveJson(doc, error);
    if (doc.format == Format::Toml || doc.format == Format::ForgeCfg
        || doc.format == Format::Properties)
    {
        return saveLineBased(doc, error);
    }
    if (error)
        *error = QStringLiteral("无法识别的配置文件格式");
    return false;
}

// ────────────────────────── 配置文件收集 ──────────────────────────

namespace {

/** 去分隔符归一化:ferrite-core → ferritecore(匹配命名风格差异) */
QString collapsedKey(const QString &normalized)
{
    static const QRegularExpression separators(QStringLiteral("[._\\-]"));
    QString out = normalized;
    out.remove(separators);
    return out;
}

/**
 * 内容扫描兜底:在 config 目录(顶层 + 一级子目录)的文本文件中
 * 查找模组标识,按出现次数降序返回命中文件(最多 maxHits 个)。
 * 仅在名称匹配全部落空时调用,单文件与总量均有上限控制耗时。
 */
QStringList collectByContentScan(const QDir &dir, const QStringList &searchKeys, int maxHits)
{
    struct Hit
    {
        QString path;
        int count = 0;
    };

    const qint64 kMaxFileSize = 512 * 1024; // 512KB
    const int kMaxFiles = 300;

    QStringList candidatePaths;
    const QFileInfoList topFiles = dir.entryInfoList(QDir::Files);
    for (const QFileInfo &fi : topFiles)
        candidatePaths.append(fi.absoluteFilePath());
    const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &d : subDirs)
    {
        const QFileInfoList subFiles = QDir(d.absoluteFilePath()).entryInfoList(QDir::Files);
        for (const QFileInfo &fi : subFiles)
            candidatePaths.append(fi.absoluteFilePath());
    }

    QList<Hit> hits;
    int scanned = 0;
    for (const QString &path : candidatePaths)
    {
        if (scanned >= kMaxFiles)
            break;
        const QFileInfo fi(path);
        // 只读文本类后缀,限制单文件大小
        const QString suffix = fi.suffix().toLower();
        if (!kConfigSuffixes.contains(suffix) && !kContentScanSuffixes.contains(suffix))
            continue;
        if (fi.size() <= 0 || fi.size() > kMaxFileSize)
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString text = QString::fromUtf8(file.readAll());
        ++scanned;

        int total = 0;
        for (const QString &key : searchKeys)
            total += text.count(key, Qt::CaseInsensitive);
        if (total > 0)
            hits.append({path, total});
    }

    std::sort(hits.begin(), hits.end(),
              [](const Hit &a, const Hit &b) { return a.count > b.count; });

    QStringList result;
    for (const Hit &h : hits)
    {
        if (result.size() >= maxHits)
            break;
        result.append(h.path);
    }
    return result;
}

} // namespace

QStringList ModConfigParser::collectConfigFiles(const ModInfo &info, const QString &configDir)
{
    QStringList result;
    if (configDir.isEmpty())
        return result;
    QDir dir(configDir);
    if (!dir.exists())
        return result;

    QString jarStem = info.fileName;
    if (jarStem.endsWith(QLatin1String(".disabled"), Qt::CaseInsensitive))
        jarStem.chop(9);
    if (jarStem.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive))
        jarStem.chop(4);

    QStringList ids;
    if (!info.id.isEmpty())
        ids.append(info.id);
    if (!jarStem.isEmpty())
        ids.append(jarStem);
    if (!info.englishName.isEmpty())
        ids.append(info.englishName);
    if (!info.name.isEmpty())
        ids.append(info.name);

    QStringList normIds;     // 归一化(小写 + 仅保留字母数字与 _.-)
    QStringList collapsedIds; // 归一化再去分隔符(ferrite-core → ferritecore)
    for (const QString &id : ids)
    {
        const QString n = normalizeKey(id);
        if (n.isEmpty() || normIds.contains(n))
            continue;
        normIds.append(n);
        const QString c = collapsedKey(n);
        if (!c.isEmpty() && !collapsedIds.contains(c))
            collapsedIds.append(c);
    }
    if (normIds.isEmpty())
        return result;

    QSet<QString> seen;
    auto tryAdd = [&result, &seen](const QFileInfo &fi) {
        const QString key = fi.absoluteFilePath().toLower();
        if (seen.contains(key))
            return;
        seen.insert(key);
        result.append(fi.absoluteFilePath());
    };
    auto isConfigFile = [](const QFileInfo &fi) {
        return fi.isFile() && kConfigSuffixes.contains(fi.suffix().toLower());
    };

    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    // 1) 精确匹配:<id>.<后缀>(含去分隔符等价名)与 config/<id>/ 目录内全部文件
    //    (目录内不限后缀,覆盖 jei/jei.ini 等非常规扩展名)
    for (const QFileInfo &fi : entries)
    {
        const QString n = normalizeKey(fi.completeBaseName());
        const bool nameHit = normIds.contains(n)
            || collapsedIds.contains(collapsedKey(n));
        if (!nameHit)
            continue;
        if (fi.isFile())
        {
            if (isConfigFile(fi))
                tryAdd(fi);
        }
        else if (fi.isDir())
        {
            const QFileInfoList subFiles =
                QDir(fi.absoluteFilePath()).entryInfoList(QDir::Files);
            for (const QFileInfo &sf : subFiles)
                tryAdd(sf);
        }
    }

    // 2) 前缀匹配:<id>-* / <id>_*
    for (const QFileInfo &fi : entries)
    {
        if (!isConfigFile(fi))
            continue;
        const QString n = normalizeKey(fi.completeBaseName());
        for (const QString &id : normIds)
        {
            if (n.startsWith(id + QLatin1Char('-')) || n.startsWith(id + QLatin1Char('_')))
            {
                tryAdd(fi);
                break;
            }
        }
    }

    // 3) 模糊匹配:文件名包含 id(归一化或去分隔符,长度 >= 3)
    if (result.isEmpty())
    {
        for (const QFileInfo &fi : entries)
        {
            if (!isConfigFile(fi))
                continue;
            const QString n = normalizeKey(fi.fileName());
            const QString c = collapsedKey(n);
            bool hit = false;
            for (int i = 0; i < normIds.size() && !hit; ++i)
            {
                const QString &id = normIds.at(i);
                if (id.size() < 3)
                    continue;
                if (n.contains(id))
                    hit = true;
                else if (i < collapsedIds.size() && collapsedIds.at(i).size() >= 3
                         && c.contains(collapsedIds.at(i)))
                    hit = true;
            }
            if (hit)
                tryAdd(fi);
        }
    }

    // 4) 内容扫描兜底(全部落空时):在文件内容中查找模组标识
    if (result.isEmpty())
    {
        QStringList searchKeys;
        for (const QString &id : ids)
        {
            // 过短的标识会产生大量误报,不参与内容匹配
            if (id.size() >= 4 && !searchKeys.contains(id))
                searchKeys.append(id);
        }
        if (!searchKeys.isEmpty())
        {
            const QStringList scanned = collectByContentScan(dir, searchKeys, 10);
            for (const QString &path : scanned)
                tryAdd(QFileInfo(path));
        }
    }

    return result;
}
