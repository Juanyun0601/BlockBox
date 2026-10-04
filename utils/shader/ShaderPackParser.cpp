/**
 * @file   ShaderPackParser.cpp
 * @brief  光影包选项解析器实现（对齐 Iris 26.1 源码规则）
 * @author BlockBox Team
 * @date   2026-08-01
 */

#include "ShaderPackParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include "utils/JarUtils.h"

namespace {

/// 拆分空格分隔的值列表，过滤空串（与 StringOption.split(" ") 一致）
QStringList splitWhitespace(const QString &text)
{
    QStringList result;
    for (const QString &s : text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts))
        result << s;
    return result;
}

/// 迷你词法游标，镜像 Iris 的 ParsedString 行为
class ParsedString
{
public:
    explicit ParsedString(QString text) : m_text(std::move(text)) {}

    bool takeLiteral(const QString &token)
    {
        if (!m_text.startsWith(token))
            return false;
        m_text = m_text.mid(token.size());
        return true;
    }

    bool takeSomeWhitespace()
    {
        if (m_text.isEmpty() || !m_text.at(0).isSpace())
            return false;
        m_text = m_text.trimmed();
        return true;
    }

    // 仅剥离行首 // 及后续多余的 /（对齐 ParsedString.takeComments）
    bool takeComments()
    {
        if (!m_text.startsWith(QStringLiteral("//")))
            return false;
        m_text = m_text.mid(2);
        while (m_text.startsWith(QLatin1Char('/')))
            m_text = m_text.mid(1);
        return true;
    }

    bool isEnd() const { return m_text.isEmpty(); }

    bool currentlyContains(const QString &token) const { return m_text.contains(token); }

    QString takeRest() const { return m_text; }

    QString takeWord()
    {
        int position = 0;
        for (const QChar &c : m_text)
        {
            if (!c.isLetterOrNumber() && c != QLatin1Char('_'))
                break;
            ++position;
        }
        if (position == 0)
            return QString();
        QString result = m_text.left(position);
        m_text = m_text.mid(position);
        return result;
    }

    QString takeNumber()
    {
        if (isEnd())
            return QString();
        int position = 0;
        while (position < m_text.size())
        {
            if (position + 1 < m_text.size())
            {
                if (!m_text.at(position).isDigit() && !m_text.at(position + 1).isDigit())
                    break;
            }
            else if (!m_text.at(position).isDigit())
            {
                break;
            }
            ++position;
        }
        // 处理 f/F 浮点后缀
        if (position > 0 && position + 1 < m_text.size()
            && (m_text.at(position) == QLatin1Char('f') || m_text.at(position) == QLatin1Char('F')))
        {
            ++position;
        }
        bool ok = false;
        m_text.left(position).toFloat(&ok);
        if (!ok)
            return QString();
        QString result = m_text.left(position);
        m_text = m_text.mid(position);
        return result;
    }

    QString takeWordOrNumber()
    {
        QString number = takeNumber();
        if (number.isEmpty())
            return takeWord();
        return number;
    }

private:
    QString m_text;
};

} // namespace

ShaderPackParser::ShaderPackParser(QObject *parent)
    : QObject(parent)
{
}

bool ShaderPackParser::parsePack(const QString &packPath)
{
    m_options.clear();
    m_optionIndex.clear();
    m_sliderList.clear();
    m_screens.clear();
    m_screenIndex.clear();
    m_screenOrderMap.clear();
    m_screenTranslations.clear();
    m_ifdefRefs.clear();
    m_packPath = packPath;

    QFileInfo fi(packPath);
    if (!fi.exists())
        return false;
    m_packFileName = fi.fileName();

    m_shadersDir = locateShadersDir();
    if (m_shadersDir.isEmpty())
        return false;

    // 1. shaders/shaders.properties（ISO-8859-1）
    emit progressChanged(5, tr("读取 shaders.properties"));
    QByteArray propsData;
    if (JarUtils::extractFromJarToMemory(packPath, m_shadersDir + QStringLiteral("shaders.properties"), propsData))
    {
        parseShadersProperties(QString::fromLatin1(propsData));
    }

    // 2. lang 文件（UTF-8）——按语言代码分别存储（对齐 Iris LanguageMap）
    {
        emit progressChanged(12, tr("读取语言文件"));
        m_langMaps.clear();
        QStringList langEntries = JarUtils::listEntriesInJar(packPath, m_shadersDir + QStringLiteral("lang/"));
        if (!langEntries.isEmpty())
        {
            // 批量提取全部 .lang 文件到内存（一次打开 zip）
            QStringList langTargets;
            for (const QString &entry : langEntries)
            {
                if (entry.endsWith(QStringLiteral(".lang"), Qt::CaseInsensitive))
                    langTargets << entry;
            }
            QMap<QString, QByteArray> langResults;
            JarUtils::extractMultipleFromJarToMemory(packPath, langTargets, langResults);
            for (auto it = langResults.constBegin(); it != langResults.constEnd(); ++it)
            {
                QString fileName = it.key().mid(it.key().lastIndexOf('/') + 1);
                fileName.chop(QStringLiteral(".lang").size());
                parseLangFile(fileName, QString::fromUtf8(it.value()));
            }
        }
    }

    // 3. 扫描着色器源文件中的选项（DEFINE / CONST）
    //    对齐 Iris：从 shaders 根与各维度目录的程序文件出发，递归解析 #include，
    //    收集全部 GLSL 源文件（含 .glsl）后再解析选项。
    {
        emit progressChanged(20, tr("扫描着色器源文件"));
        const QStringList sourceFiles = collectSourceFiles();
        if (!sourceFiles.isEmpty())
        {
            // 批量提取全部源文件到内存（一次打开 zip），避免每文件重开 zip 的 I/O 开销
            QStringList targets;
            targets.reserve(sourceFiles.size());
            for (const QString &entry : sourceFiles)
                targets << m_shadersDir + entry;
            QMap<QString, QByteArray> srcResults;
            JarUtils::extractMultipleFromJarToMemory(packPath, targets, srcResults);

            int total = sourceFiles.size();
            int done = 0;
            for (const QString &entry : sourceFiles)
            {
                auto it = srcResults.constFind(m_shadersDir + entry);
                if (it != srcResults.constEnd())
                    parseShaderSource(QString::fromUtf8(it.value()));
                if ((++done % 16) == 0 || done == total)
                    emit progressChanged(20 + 60 * done / total, tr("解析选项 %1/%2").arg(done).arg(total));
            }
        }
    }

    emit progressChanged(80, tr("整理分类"));

    // 4. 确认布尔选项：必须被 #ifdef/#ifndef 引用（对齐 getOptionSet）
    for (int i = m_options.size() - 1; i >= 0; --i)
    {
        Option &opt = m_options[i];
        if (opt.type == QStringLiteral("bool") && !m_ifdefRefs.contains(opt.name))
            m_options.removeAt(i);
    }
    // 重建索引
    m_optionIndex.clear();
    for (int i = 0; i < m_options.size(); ++i)
        m_optionIndex.insert(m_options[i].name, i);

    buildScreens();
    applyLanguage();
    emit progressChanged(100, tr("解析完成"));
    return !m_options.isEmpty();
}

QString ShaderPackParser::locateShadersDir() const
{
    QStringList all = JarUtils::listEntriesInJar(m_packPath);
    QSet<QString> dirs;
    for (const QString &entry : all)
    {
        int slash = entry.lastIndexOf('/');
        QString parent = slash >= 0 ? entry.left(slash) : QString();
        while (!parent.isEmpty())
        {
            dirs.insert(parent + QLatin1Char('/'));
            int next = parent.lastIndexOf('/');
            parent = next >= 0 ? parent.left(next) : QString();
        }
    }

    // 优先：根部 shaders/
    if (dirs.contains(QStringLiteral("shaders/")))
        return QStringLiteral("shaders/");

    // 其次：任意以 "shaders" 结尾的目录（对齐 Iris loadExternalZipShaderpack）
    QStringList candidates;
    for (const QString &dir : dirs)
    {
        QString trimmed = dir;
        while (trimmed.endsWith(QLatin1Char('/')))
            trimmed.chop(1);
        if (trimmed.endsWith(QStringLiteral("shaders")))
            candidates << dir;
    }
    if (candidates.isEmpty())
        return QString();
    std::sort(candidates.begin(), candidates.end(),
              [](const QString &a, const QString &b) {
                  return a.count(QLatin1Char('/')) < b.count(QLatin1Char('/'));
              });
    return candidates.first();
}

QString ShaderPackParser::resolveInclude(const QString &includeLine, const QString &currentDir) const
{
    QString target = includeLine.trimmed();
    if (!target.startsWith(QStringLiteral("#include")))
        return QString();
    target = target.mid(QStringLiteral("#include").size()).trimmed();

    // 移除引号（Iris FileNode.findIncludes 的做法）
    if (target.startsWith(QLatin1Char('"')))
        target = target.mid(1);
    if (target.endsWith(QLatin1Char('"')))
        target.chop(1);
    target = target.trimmed();
    if (target.isEmpty())
        return QString();

    // 前导 / 表示相对 shaders 根目录；否则相对当前文件所在目录
    if (target.startsWith(QLatin1Char('/')))
    {
        target = target.mid(1);
    }
    else
    {
        if (!currentDir.isEmpty())
            target = currentDir + QLatin1Char('/') + target;
    }

    // 归一化 "./" 与 "../"
    QStringList parts;
    const QStringList segs = target.split(QLatin1Char('/'));
    for (const QString &seg : segs)
    {
        if (seg.isEmpty() || seg == QStringLiteral("."))
            continue;
        if (seg == QStringLiteral(".."))
        {
            if (!parts.isEmpty())
                parts.removeLast();
            continue;
        }
        parts << seg;
    }
    return parts.join(QLatin1Char('/'));
}

QStringList ShaderPackParser::collectSourceFiles()
{
    const QStringList shaderEntries = JarUtils::listEntriesInJar(m_packPath, m_shadersDir);

    // 全部候选源文件（含 .glsl，Iris 经 include 图加载它们）。
    // listEntriesInJar 返回带 shaders 前缀的完整路径，此处剥离前缀得到相对路径。
    QStringList allSource;
    QSet<QString> allSet;
    for (const QString &fullEntry : shaderEntries)
    {
        const QString entry = fullEntry.mid(m_shadersDir.size());
        if (entry.endsWith(QStringLiteral(".vsh")) || entry.endsWith(QStringLiteral(".fsh"))
            || entry.endsWith(QStringLiteral(".csh")) || entry.endsWith(QStringLiteral(".gsh"))
            || entry.endsWith(QStringLiteral(".tcs")) || entry.endsWith(QStringLiteral(".tes"))
            || entry.endsWith(QStringLiteral(".glsl")))
        {
            allSource << entry;
            allSet.insert(entry);
        }
    }

    // 批量提取全部候选源文件到内存（一次打开 zip），include 遍历不再重开 zip
    QStringList targets;
    targets.reserve(allSource.size());
    for (const QString &entry : allSource)
        targets << m_shadersDir + entry;
    QMap<QString, QByteArray> contents;
    JarUtils::extractMultipleFromJarToMemory(m_packPath, targets, contents);

    // 起点：各维度目录中存在的程序文件 + 根目录标准程序文件。
    // 简化实现：所有候选源文件都作为潜在起点，再递归收集被 #include 的文件。
    QStringList included;
    QSet<QString> seen = allSet;
    QList<QString> queue;
    for (const QString &src : allSource)
        queue << src;

    while (!queue.isEmpty())
    {
        const QString current = queue.takeFirst();
        if (included.contains(current))
            continue;
        included << current;

        QString currentDir = current;
        int slash = currentDir.lastIndexOf(QLatin1Char('/'));
        currentDir = slash >= 0 ? currentDir.left(slash) : QString();

        auto it = contents.constFind(m_shadersDir + current);
        if (it == contents.constEnd())
            continue;
        const QStringList lines = QString::fromUtf8(it.value()).split('\n');
        for (const QString &rawLine : lines)
        {
            const QString trimmed = rawLine.trimmed();
            if (!trimmed.startsWith(QStringLiteral("#include")))
                continue;
            const QString resolved = resolveInclude(trimmed, currentDir);
            if (resolved.isEmpty())
                continue;
            if (!seen.contains(resolved))
            {
                seen.insert(resolved);
                queue << resolved;
            }
        }
    }

    return included;
}

void ShaderPackParser::parseShadersProperties(const QString &content)
{
    // 先收集 sliders 与所有 screen 指令（含子屏），再在 buildScreens 中构建分类
    QStringList lines = content.split('\n');
    QList<ScreenCategory> declaredScreens;
    for (const QString &rawLine : lines)
    {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        int eq = line.indexOf('=');
        if (eq <= 0)
            continue;

        QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();

        if (key == QStringLiteral("sliders"))
        {
            m_sliderList = splitWhitespace(value);
        }
        else if (key == QStringLiteral("screen") || key.startsWith(QStringLiteral("screen.")))
        {
            if (key.endsWith(QStringLiteral(".columns")))
                continue;

            ScreenCategory cat;
            cat.key = (key == QStringLiteral("screen")) ? QString() : key.mid(7);
            const QStringList tokens = splitWhitespace(value);
            for (const QString &tok : tokens)
            {
                if (tok.startsWith('<') || tok == QStringLiteral("_") || tok.isEmpty())
                    continue;
                if (tok == QStringLiteral("*"))
                {
                    // "*" 表示未使用选项回填占位，不作为真实选项名
                    cat.options << QStringLiteral("*");
                    continue;
                }
                if (tok.startsWith('[') && tok.endsWith(']'))
                    cat.subScreens << tok.mid(1, tok.size() - 2);
                else
                    cat.options << tok;
            }
            declaredScreens << cat;
        }
    }

    // 主屏（screen 指令）选项顺序用于全局排序
    for (const ScreenCategory &cat : declaredScreens)
    {
        if (!cat.key.isEmpty())
            continue;
        int order = 0;
        for (const QString &name : cat.options)
        {
            if (!m_screenOrderMap.contains(name))
                m_screenOrderMap.insert(name, order);
            ++order;
        }
        break;
    }

    // 保存声明顺序（主屏排最前）
    m_screens = declaredScreens;
}

void ShaderPackParser::buildScreens()
{
    if (m_screens.isEmpty())
    {
        // 无 screen 指令：全部选项放入单一主屏（对齐 Iris 默认 ["*"]）
        ScreenCategory main;
        main.key = QString();
        for (const Option &opt : m_options)
            main.options << opt.name;
        m_screens << main;
    }
    else
    {
        // 分配归属：选项在 screen 中出现则归属该分类（首个匹配）
        QSet<QString> used;
        for (int s = 0; s < m_screens.size(); ++s)
        {
            ScreenCategory &cat = m_screens[s];
            for (const QString &name : cat.options)
            {
                Option *opt = findOption(name);
                if (opt && !used.contains(name))
                {
                    opt->screen = cat.key;
                    used.insert(name);
                }
            }
        }
        // 未归属任何分类的选项：不显示（对齐 Iris unusedOptions，仅在含 "*" 的
        // 主屏中回填；Photon 等包无 "*"，这些内部宏不进入界面）。这里标记为
        // 空分类，由界面层决定是否展示（默认不展示）。
        bool hasMain = false;
        bool mainHasStar = false;
        for (int s = 0; s < m_screens.size(); ++s)
        {
            if (m_screens[s].key.isEmpty())
            {
                hasMain = true;
                if (m_screens[s].options.contains(QStringLiteral("*")))
                    mainHasStar = true;
                break;
            }
        }
        if (!hasMain)
        {
            ScreenCategory main;
            main.key = QString();
            m_screens.prepend(main);
            mainHasStar = true; // 无 screen 指令等价于全量回填
        }
        for (int i = 0; i < m_options.size(); ++i)
        {
            Option &opt = m_options[i];
            if (!used.contains(opt.name))
            {
                opt.screen = QString();
                if (mainHasStar)
                {
                    for (int s = 0; s < m_screens.size(); ++s)
                    {
                        if (m_screens[s].key.isEmpty())
                        {
                            m_screens[s].options << opt.name;
                            break;
                        }
                    }
                }
            }
        }
    }

    // 滑块标记 / 默认显示名 / 归属排序键
    for (Option &opt : m_options)
    {
        opt.isSlider = m_sliderList.contains(opt.name);
        if (opt.displayName.isEmpty())
            opt.displayName = opt.name;
        opt.order = m_screenOrderMap.value(opt.name, -1);
    }

    // 重建索引 + 设置显示名
    m_screenIndex.clear();
    for (int i = 0; i < m_screens.size(); ++i)
    {
        m_screenIndex.insert(m_screens[i].key, i);
        m_screens[i].displayName = screenDisplayName(m_screens[i].key);
    }

    // 排序：主屏顺序优先，未出现在 screen 中的排最末
    std::stable_sort(m_options.begin(), m_options.end(),
                     [this](const Option &a, const Option &b) {
                         int oa = m_screenOrderMap.value(a.name, -1);
                         int ob = m_screenOrderMap.value(b.name, -1);
                         if (oa >= 0 && ob >= 0)
                             return oa < ob;
                         if (oa >= 0)
                             return true;
                         if (ob >= 0)
                             return false;
                         return a.name < b.name;
                     });
    // 重建索引
    m_optionIndex.clear();
    for (int i = 0; i < m_options.size(); ++i)
        m_optionIndex.insert(m_options[i].name, i);
}

QString ShaderPackParser::screenDisplayName(const QString &key) const
{
    if (key.isEmpty())
        return QStringLiteral("Main");
    return m_screenTranslations.value(key, key);
}

void ShaderPackParser::parseLangFile(const QString &langCode, const QString &langContent)
{
    // 对齐 Iris LanguageMap：语言代码统一转小写（如 en_US -> en_us），
    // 按语言分别存储完整键值表，供 applyLanguage 切换时使用。
    const QString normCode = langCode.toLower();
    QMap<QString, QString> &table = m_langMaps[normCode];

    QStringList lines = langContent.split('\n');
    for (const QString &rawLine : lines)
    {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        int eq = line.indexOf('=');
        if (eq <= 0)
            continue;

        table.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
}

QStringList ShaderPackParser::languages() const
{
    QStringList codes = m_langMaps.keys();
    // 默认语言排最前（en_us 优先，其次首键）
    std::sort(codes.begin(), codes.end(), [](const QString &a, const QString &b) {
        if (a == QStringLiteral("en_us"))
            return true;
        if (b == QStringLiteral("en_us"))
            return false;
        return a < b;
    });
    return codes;
}

QString ShaderPackParser::language() const
{
    if (!m_activeLang.isEmpty())
        return m_activeLang;
    // 未设置时取默认语言：en_us 优先，否则第一个可用
    if (m_langMaps.contains(QStringLiteral("en_us")))
        return QStringLiteral("en_us");
    if (m_langMaps.isEmpty())
        return QString();
    return m_langMaps.firstKey();
}

bool ShaderPackParser::setLanguage(const QString &code)
{
    const QString normCode = code.toLower();
    if (!m_langMaps.contains(normCode))
        return false;
    m_activeLang = normCode;
    applyLanguage();
    return true;
}

void ShaderPackParser::autoSetLanguage(const QString &appLangCode)
{
    // 软件语言代码 -> 光影包候选语言列表（按优先顺序；en_us/en 由末尾兜底统一处理）
    QStringList candidates;
    if (appLangCode.compare(QStringLiteral("zh"), Qt::CaseInsensitive) == 0)
        candidates << QStringLiteral("zh_cn") << QStringLiteral("zh") << QStringLiteral("zh_tw");
    else if (appLangCode.compare(QStringLiteral("zh_Hant"), Qt::CaseInsensitive) == 0)
        candidates << QStringLiteral("zh_tw") << QStringLiteral("zh_hant") << QStringLiteral("zh_cn")
                   << QStringLiteral("zh");
    else if (appLangCode.compare(QStringLiteral("es"), Qt::CaseInsensitive) == 0)
        candidates << QStringLiteral("es_es") << QStringLiteral("es");
    else
        candidates << QStringLiteral("en_us") << QStringLiteral("en");

    for (const QString &candidate : candidates)
    {
        if (setLanguage(candidate))
            return;
    }

    // 前缀匹配：软件语言代码简化为前两个字符（如 es -> es_mx / es_es），
    // 对齐 Iris 用完整 locale 代码、我们只用简化代码的差异
    if (appLangCode.size() >= 2)
    {
        const QString prefix = appLangCode.left(2).toLower() + QLatin1Char('_');
        const QStringList available = languages();
        for (const QString &avail : available)
        {
            if (avail.startsWith(prefix))
            {
                setLanguage(avail);
                return;
            }
        }
        // 无下划线形式（如 "zh" 直接命名）
        for (const QString &avail : available)
        {
            if (avail.startsWith(appLangCode.left(2).toLower()))
            {
                setLanguage(avail);
                return;
            }
        }
    }

    // 兜底：en_us 优先，其次第一个可用语言
    if (setLanguage(QStringLiteral("en_us")))
        return;
    const QStringList available = languages();
    if (!available.isEmpty())
        setLanguage(available.first());
}

void ShaderPackParser::applyLanguage()
{
    const QString lang = language();
    const QMap<QString, QString> &table = m_langMaps.value(lang);

    // 选项显示名 / 说明 / 值标签 / 前后缀（对齐 OptionMenuElement 读取逻辑）
    for (Option &opt : m_options)
    {
        opt.displayName = table.value(QStringLiteral("option.") + opt.name, opt.name);
        opt.description = table.value(QStringLiteral("option.") + opt.name + QStringLiteral(".comment"),
                                      opt.description);
        opt.valueLabels.clear();
        for (const QString &v : opt.values)
        {
            opt.valueLabels.insert(v,
                                   table.value(QStringLiteral("value.") + opt.name + QLatin1Char('.') + v, v));
        }
        opt.valuePrefix = table.value(QStringLiteral("prefix.") + opt.name, QString());
        opt.valueSuffix = table.value(QStringLiteral("suffix.") + opt.name, QString());
    }

    // 分类显示名（screen.<key>）
    m_screenTranslations.clear();
    for (auto it = table.constBegin(); it != table.constEnd(); ++it)
    {
        if (it.key().startsWith(QStringLiteral("screen."))
            && !it.key().endsWith(QStringLiteral(".comment")))
        {
            QString screenKey = it.key().mid(QStringLiteral("screen.").size());
            if (!screenKey.isEmpty())
                m_screenTranslations.insert(screenKey, it.value());
        }
    }
    for (ScreenCategory &cat : m_screens)
        cat.displayName = screenDisplayName(cat.key);
}

void ShaderPackParser::parseShaderSource(const QString &source)
{
    QStringList lines = source.split('\n');
    static const QRegularExpression ifdefRe(
        R"(^\s*#\s*(ifdef|ifndef)\s+([A-Za-z_]\w*)\s*$)",
        QRegularExpression::CaseInsensitiveOption);

    for (const QString &rawLine : lines)
    {
        QString trimmed = rawLine.trimmed();
        if (trimmed.isEmpty())
            continue;

        // #ifdef / #ifndef 引用跟踪
        QRegularExpressionMatch im = ifdefRe.match(trimmed);
        if (im.hasMatch())
        {
            QString name = im.captured(2);
            if (!m_ifdefRefs.contains(name))
                m_ifdefRefs << name;
            continue;
        }

        // 无相关关键字的行直接忽略（对齐 parseLine 快速路径）
        if (!trimmed.contains(QStringLiteral("#define"))
            && !trimmed.contains(QStringLiteral("const"))
            && !trimmed.contains(QStringLiteral("#ifdef"))
            && !trimmed.contains(QStringLiteral("#ifndef")))
        {
            continue;
        }

        ParsedString line(trimmed);

        if (line.takeLiteral(QStringLiteral("const")))
        {
            // const 已在开头：类型 -> 名称 -> = -> 值 -> ; -> 注释
            if (!line.takeSomeWhitespace())
                continue;
            QString type = line.takeWord().toLower();
            if (type != QStringLiteral("int") && type != QStringLiteral("float")
                && type != QStringLiteral("bool"))
                continue;
            if (!line.takeSomeWhitespace())
                continue;
            QString name = line.takeWord();
            if (name.isEmpty())
                continue;
            line.takeSomeWhitespace();
            if (!line.takeLiteral(QStringLiteral("=")))
                continue;
            line.takeSomeWhitespace();
            QString value = line.takeWordOrNumber();
            if (value.isEmpty())
                continue;
            line.takeSomeWhitespace();
            if (!line.takeLiteral(QStringLiteral(";")))
                continue;
            line.takeSomeWhitespace();

            QString comment;
            if (line.takeComments())
                comment = line.takeRest().trimmed();

            parseConstOption(type, name, value, comment);
            continue;
        }
        else if (line.currentlyContains(QStringLiteral("#define")))
        {
            // #define 选项：先剥离行首 // 注释
            bool hasLeadingComment = line.takeComments();
            line.takeSomeWhitespace();

            if (!line.takeLiteral(QStringLiteral("#define")))
                continue;
            if (!line.takeSomeWhitespace())
                continue;

            QString name = line.takeWord();
            if (name.isEmpty())
                continue;

            // 名称后的空白：可能是布尔（无值）或字符串（有值）
            bool tookWhitespace = line.takeSomeWhitespace();

            if (line.isEnd())
            {
                // 纯布尔：#define NAME
                parseDefineOption(name, QString(), QString(), hasLeadingComment);
                continue;
            }

            if (line.takeComments())
            {
                // 布尔带注释：#define NAME // comment
                QString comment = line.takeRest().trimmed();
                parseDefineOption(name, QString(), comment, hasLeadingComment);
                continue;
            }
            else if (!tookWhitespace)
            {
                continue; // 无效语法
            }

            if (hasLeadingComment)
                continue; // 前导注释仅允许用于布尔

            QString value = line.takeWordOrNumber();
            if (value.isEmpty())
                continue;

            tookWhitespace = line.takeSomeWhitespace();

            if (line.isEnd())
                continue; // 有值但无注释值列表，非布尔 -> 忽略

            if (!tookWhitespace)
            {
                if (!line.takeComments())
                    continue;
            }
            else if (!line.takeComments())
            {
                continue;
            }

            QString comment = line.takeRest().trimmed();
            parseDefineOption(name, value, comment, hasLeadingComment);
        }
    }
}

void ShaderPackParser::parseDefineOption(const QString &name, const QString &value,
                                         const QString &comment, bool hasLeadingComment)
{
    if (m_optionIndex.contains(name))
        return;

    if (value.isEmpty())
    {
        // 布尔选项：默认值 = !hasLeadingComment
        Option opt;
        opt.name = name;
        opt.type = QStringLiteral("bool");
        opt.defaultValue = hasLeadingComment ? QStringLiteral("false")
                                             : QStringLiteral("true");
        opt.currentValue = opt.defaultValue;
        opt.description = comment;
        m_optionIndex.insert(opt.name, m_options.size());
        m_options.append(opt);
        return;
    }

    // 字符串选项：注释必须包含 [v1 v2 ...]，否则忽略（对齐 StringOption.create）
    QString desc;
    QStringList values;
    if (!extractAllowedValues(comment, desc, values))
        return;

    Option opt;
    opt.name = name;
    opt.type = QStringLiteral("string");
    opt.defaultValue = value;
    opt.currentValue = value;
    opt.description = desc;
    opt.values = values;

    // 默认值不在允许列表则追加（对齐 StringOption.create）
    if (!values.contains(value))
        opt.values.append(value);

    m_optionIndex.insert(opt.name, m_options.size());
    m_options.append(opt);
}

void ShaderPackParser::parseConstOption(const QString &type, const QString &name,
                                        const QString &value, const QString &comment)
{
    if (m_optionIndex.contains(name))
        return;
    if (!isConstOptionName(name))
        return;

    if (type == QStringLiteral("bool"))
    {
        QString v = value.toLower();
        if (v != QStringLiteral("true") && v != QStringLiteral("false"))
            return;
        Option opt;
        opt.name = name;
        opt.type = QStringLiteral("bool");
        opt.defaultValue = v;
        opt.currentValue = v;
        opt.description = comment;
        m_optionIndex.insert(opt.name, m_options.size());
        m_options.append(opt);
        return;
    }

    // int / float -> 字符串选项，需值列表
    QString desc;
    QStringList values;
    if (!extractAllowedValues(comment, desc, values))
        return;

    Option opt;
    opt.name = name;
    opt.type = QStringLiteral("string");
    opt.defaultValue = value;
    opt.currentValue = value;
    opt.description = desc;
    opt.values = values;
    if (!values.contains(value))
        opt.values.append(value);

    m_optionIndex.insert(opt.name, m_options.size());
    m_options.append(opt);
}

bool ShaderPackParser::extractAllowedValues(const QString &comment, QString &desc,
                                            QStringList &values)
{
    int open = comment.indexOf('[');
    if (open < 0)
        return false;
    int close = comment.indexOf(']', open);
    if (close < 0)
        return false;

    QStringList parts = comment.mid(open + 1, close - open - 1).split(' ', Qt::SkipEmptyParts);
    desc = (comment.left(open) + comment.mid(close + 1)).trimmed();
    values = parts;
    return true;
}

bool ShaderPackParser::isConstOptionName(const QString &name)
{
    static const QStringList valid = {
        QStringLiteral("shadowMapResolution"), QStringLiteral("shadowDistance"),
        QStringLiteral("voxelDistance"), QStringLiteral("shadowDistanceRenderMul"),
        QStringLiteral("entityShadowDistanceMul"), QStringLiteral("shadowIntervalSize"),
        QStringLiteral("generateShadowMipmap"), QStringLiteral("generateShadowColorMipmap"),
        QStringLiteral("shadowHardwareFiltering"), QStringLiteral("shadowtex0Mipmap"),
        QStringLiteral("shadowtexMipmap"), QStringLiteral("shadowtex1Mipmap"),
        QStringLiteral("shadowtex0Nearest"), QStringLiteral("shadowtexNearest"),
        QStringLiteral("shadow0MinMagNearest"), QStringLiteral("shadowtex1Nearest"),
        QStringLiteral("shadow1MinMagNearest"), QStringLiteral("wetnessHalflife"),
        QStringLiteral("drynessHalflife"), QStringLiteral("eyeBrightnessHalflife"),
        QStringLiteral("centerDepthHalflife"), QStringLiteral("sunPathRotation"),
        QStringLiteral("ambientOcclusionLevel"), QStringLiteral("superSamplingLevel"),
        QStringLiteral("noiseTextureResolution")
    };
    if (valid.contains(name))
        return true;

    // shadowcolor<N>... / shadowHardwareFiltering<N>
    static const QRegularExpression colorRe(
        R"(^shadow(?:color|Color)\d+(?:Mipmap|Nearest|MinMagNearest)$)");
    static const QRegularExpression hwRe(
        R"(^shadowHardwareFiltering\d+$)");
    return colorRe.match(name).hasMatch() || hwRe.match(name).hasMatch();
}

void ShaderPackParser::sortOptions()
{
    // 已由 buildScreens 替代，保留空实现以防外部引用
}

bool ShaderPackParser::loadCurrentValues(const QString &gameDir)
{
    QFileInfo irisFile(gameDir + QStringLiteral("/config/iris.properties"));
    m_hasIrisConfig = irisFile.exists();

    QMap<QString, QString> values;

    if (m_hasIrisConfig)
    {
        // Iris：包专属选项存在 shaderpacks/<pack>.txt
        QString packTxt = gameDir + QStringLiteral("/shaderpacks/") + m_packFileName + QStringLiteral(".txt");
        QFile f(packTxt);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&f);
            while (!in.atEnd())
            {
                QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith('#'))
                    continue;
                int eq = line.indexOf('=');
                if (eq > 0)
                    values[line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
            }
            f.close();
        }
    }
    else
    {
        // OptiFine：optionsshaders.txt 直接位于游戏根目录
        QString optifineFile = gameDir + QStringLiteral("/optionsshaders.txt");
        QFile f(optifineFile);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&f);
            while (!in.atEnd())
            {
                QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith('#'))
                    continue;
                int eq = line.indexOf('=');
                if (eq > 0)
                    values[line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
            }
            f.close();
        }
    }

    for (Option &opt : m_options)
    {
        if (values.contains(opt.name))
            opt.currentValue = values[opt.name];
        else
            opt.currentValue = opt.defaultValue;
    }
    return true;
}

bool ShaderPackParser::saveConfig(const QString &gameDir)
{
    if (m_options.isEmpty())
        return false;

    // 只保存非默认值（对齐 Iris 的 MutableOptionValues 行为）
    QMap<QString, QString> changed;
    for (const Option &opt : m_options)
    {
        if (opt.currentValue != opt.defaultValue)
            changed[opt.name] = opt.currentValue;
    }

    if (m_hasIrisConfig)
    {
        // 1. 包专属选项 -> shaderpacks/<pack>.txt
        QString packTxt = gameDir + QStringLiteral("/shaderpacks/") + m_packFileName + QStringLiteral(".txt");
        if (changed.isEmpty())
        {
            // 全默认：删除该文件（对齐 tryUpdateConfigPropertiesFile）
            if (QFile::exists(packTxt))
                QFile::remove(packTxt);
        }
        else
        {
            QDir shaderpacksDir(gameDir + QStringLiteral("/shaderpacks"));
            if (!shaderpacksDir.exists())
                shaderpacksDir.mkpath(".");

            QFile f(packTxt);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;
            QTextStream out(&f);
            for (auto it = changed.constBegin(); it != changed.constEnd(); ++it)
                out << it.key() << "=" << it.value() << "\n";
            f.close();
        }

        // 2. 更新 iris.properties 全局设置（启用 + 选中光影包）
        QString irisPath = gameDir + QStringLiteral("/config/iris.properties");
        QMap<QString, QString> global;
        QFile inFile(irisPath);
        if (inFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&inFile);
            while (!in.atEnd())
            {
                QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith('#'))
                    continue;
                int eq = line.indexOf('=');
                if (eq > 0)
                    global[line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
            }
            inFile.close();
        }
        global[QStringLiteral("enableShaders")] = QStringLiteral("true");
        global[QStringLiteral("shaderPack")] = m_packFileName;

        QFile outFile(irisPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream out(&outFile);
        out << QStringLiteral("# This file stores configuration options for Iris, such as the currently active shaderpack\n");
        for (auto it = global.constBegin(); it != global.constEnd(); ++it)
            out << it.key() << "=" << it.value() << "\n";
        outFile.close();
        return true;
    }
    else
    {
        // OptiFine：optionsshaders.txt
        QString optifineFile = gameDir + QStringLiteral("/optionsshaders.txt");
        QFile f(optifineFile);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream out(&f);
        out << QStringLiteral("shaderPack=") << m_packFileName << "\n";
        for (auto it = changed.constBegin(); it != changed.constEnd(); ++it)
            out << it.key() << "=" << it.value() << "\n";
        f.close();
        return true;
    }
}

bool ShaderPackParser::isIrisMode() const
{
    return m_hasIrisConfig;
}

QString ShaderPackParser::configTargetName() const
{
    return m_hasIrisConfig ? QStringLiteral("Iris") : QStringLiteral("OptiFine");
}

QString ShaderPackParser::packFileName() const
{
    return m_packFileName;
}

QString ShaderPackParser::packDisplayName() const
{
    QString name = m_packFileName;
    int dot = name.lastIndexOf('.');
    if (dot > 0)
        name = name.left(dot);
    return name;
}

const QList<ShaderPackParser::Option> &ShaderPackParser::options() const
{
    return m_options;
}

const QList<ShaderPackParser::ScreenCategory> &ShaderPackParser::screens() const
{
    return m_screens;
}

const ShaderPackParser::ScreenCategory &ShaderPackParser::mainScreen() const
{
    for (const ScreenCategory &cat : m_screens)
    {
        if (cat.key.isEmpty())
            return cat;
    }
    static const ScreenCategory empty;
    return empty;
}

ShaderPackParser::Option *ShaderPackParser::findOption(const QString &name)
{
    auto it = m_optionIndex.find(name);
    if (it == m_optionIndex.end())
        return nullptr;
    if (it.value() < 0 || it.value() >= m_options.size())
        return nullptr;
    return &m_options[it.value()];
}

void ShaderPackParser::setValue(const QString &name, const QString &value)
{
    Option *opt = findOption(name);
    if (opt)
        opt->currentValue = value;
}

void ShaderPackParser::resetToDefaults()
{
    for (Option &opt : m_options)
        opt.currentValue = opt.defaultValue;
}
