/**
 * @file   SkillManager.cpp
 * @brief  AI 技能管理器类实现
 * @author BlockBox Team
 * @date   2026-08-08
 */

#include "SkillManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "utils/plugin/PluginZip.h"

namespace {

const QString kSkillSuffix = QStringLiteral(".skill");
const QString kSkillManifest = QStringLiteral("SKILL.md");

/**
 * @brief 获取行的缩进空格数（制表符按 4 空格计），并返回去缩进后的内容
 */
int lineIndent(const QString &line, QString &content)
{
    int n = 0;
    int i = 0;
    while (i < line.size() && (line[i] == QLatin1Char(' ') || line[i] == QLatin1Char('\t')))
    {
        n += (line[i] == QLatin1Char('\t')) ? 4 : 1;
        ++i;
    }
    content = line.mid(i);
    // 去除行尾注释（仅处理顶层 # 注释，引号内不处理——简化版）
    return n;
}

/**
 * @brief 去除字符串两端的引号
 */
QString unquote(const QString &s)
{
    QString t = s.trimmed();
    if (t.size() >= 2 &&
        ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"'))) ||
         (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
    {
        t = t.mid(1, t.size() - 2);
    }
    return t;
}

/**
 * @brief 解析内联标量为 JSON 值（支持引号字符串、布尔、数字、内联数组 [a, b]）
 */
QJsonValue parseScalar(const QString &s)
{
    QString t = s.trimmed();
    if (t.isEmpty())
        return QJsonValue();

    // 引号字符串
    if (t.size() >= 2 &&
        ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"'))) ||
         (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
        return QJsonValue(unquote(t));

    // 内联数组 [a, b, c]
    if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']')))
    {
        QJsonArray arr;
        QString inner = t.mid(1, t.size() - 2).trimmed();
        if (!inner.isEmpty())
        {
            const QStringList parts = inner.split(QLatin1Char(','));
            for (const QString &p : parts)
            {
                QString v = p.trimmed();
                if (!v.isEmpty())
                    arr.append(parseScalar(v));
            }
        }
        return arr;
    }

    // 内联映射 {a: b, c: d} 或 {}
    if (t.startsWith(QLatin1Char('{')) && t.endsWith(QLatin1Char('}')))
    {
        QJsonObject obj;
        QString inner = t.mid(1, t.size() - 2).trimmed();
        if (!inner.isEmpty())
        {
            const QStringList parts = inner.split(QLatin1Char(','));
            for (const QString &p : parts)
            {
                int c = p.indexOf(QLatin1Char(':'));
                if (c > 0)
                {
                    QString k = p.left(c).trimmed();
                    QString v = p.mid(c + 1).trimmed();
                    obj[unquote(k)] = parseScalar(v);
                }
            }
        }
        return obj;
    }

    // 布尔
    if (t == QLatin1String("true") || t == QLatin1String("True") || t == QLatin1String("TRUE"))
        return QJsonValue(true);
    if (t == QLatin1String("false") || t == QLatin1String("False") || t == QLatin1String("FALSE"))
        return QJsonValue(false);
    if (t == QLatin1String("null") || t == QLatin1String("Null") || t == QLatin1String("~"))
        return QJsonValue();

    // 数字
    bool ok = false;
    int asInt = t.toInt(&ok);
    if (ok)
        return QJsonValue(asInt);
    double asDouble = t.toDouble(&ok);
    if (ok)
        return QJsonValue(asDouble);

    // 普通字符串
    return QJsonValue(t);
}

/**
 * @brief 递归解析 YAML 块映射（key: value 形式）
 *
 * @param lines     全部行
 * @param index     当前行号（引用，会推进）
 * @param mapIndent 本映射期望的缩进级别
 */
QJsonObject parseMapping(const QStringList &lines, int &index, int mapIndent);

/**
 * @brief 递归解析 YAML 块节点（映射或序列）
 */
QJsonValue parseNode(const QStringList &lines, int &index, int minIndent);

QJsonObject parseMapping(const QStringList &lines, int &index, int mapIndent)
{
    QJsonObject obj;
    while (index < lines.size())
    {
        QString content;
        int indent = lineIndent(lines[index], content);
        if (content.isEmpty() || content.startsWith(QLatin1Char('#')))
        {
            ++index;
            continue;
        }
        if (indent < mapIndent)
            break;
        if (indent != mapIndent)
            break; // 缩进不一致，不属于本映射
        if (content.startsWith(QLatin1String("- ")))
            break; // 序列开始

        // 查找 key:value 分隔冒号（冒号后须为空格或行尾，避免误判 file:read 等标量）
        int cp = -1;
        for (int i = 0; i < content.size(); ++i)
        {
            if (content[i] == QLatin1Char(':') &&
                (i + 1 >= content.size() || content[i + 1].isSpace()))
            {
                cp = i;
                break;
            }
        }
        if (cp < 0)
        {
            ++index;
            break;
        }
        QString key = content.left(cp).trimmed();
        QString val = content.mid(cp + 1).trimmed();
        ++index;
        if (val.isEmpty())
            obj[key] = parseNode(lines, index, mapIndent + 1);
        else
            obj[key] = parseScalar(val);
    }
    return obj;
}

QJsonValue parseNode(const QStringList &lines, int &index, int minIndent)
{
    // 跳过空行与注释
    while (index < lines.size())
    {
        QString content;
        int indent = lineIndent(lines[index], content);
        if (content.isEmpty() || content.startsWith(QLatin1Char('#')))
        {
            ++index;
            continue;
        }
        if (indent < minIndent)
            return QJsonValue(); // 回溯

        // 序列
        if (content.startsWith(QLatin1String("- ")))
        {
            QJsonArray arr;
            while (index < lines.size())
            {
                QString c;
                int ind = lineIndent(lines[index], c);
                if (c.isEmpty() || c.startsWith(QLatin1Char('#')))
                {
                    ++index;
                    continue;
                }
                if (ind < minIndent)
                    break;
                if (ind != minIndent)
                    break;
                if (!c.startsWith(QLatin1String("- ")))
                    break;

                QString itemContent = c.mid(2).trimmed();
                if (itemContent.isEmpty())
                {
                    // 纯 "-" 后跟缩进块
                    ++index;
                    arr.append(parseNode(lines, index, minIndent + 1));
                    continue;
                }

                // 判断是否为 "- key: value" 形式（序列项是映射）
                int colonPos = -1;
                bool inQuote = false;
                QChar quoteChar;
                for (int i = 0; i < itemContent.size(); ++i)
                {
                    QChar ch = itemContent[i];
                    if (inQuote)
                    {
                        if (ch == quoteChar)
                            inQuote = false;
                    }
                    else
                    {
                        if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
                        {
                            inQuote = true;
                            quoteChar = ch;
                        }
                        else if (ch == QLatin1Char(':') &&
                                 (i + 1 >= itemContent.size() ||
                                  itemContent[i + 1].isSpace()))
                        {
                            colonPos = i;
                            break;
                        }
                    }
                }

                if (colonPos >= 0)
                {
                    // 序列项是映射：第一行 "- key: value"，后续同级 key 缩进 = minIndent + 2
                    ++index;
                    QJsonObject obj;
                    QString key = itemContent.left(colonPos).trimmed();
                    QString val = itemContent.mid(colonPos + 1).trimmed();
                    int subMapIndent = minIndent + 2;
                    if (val.isEmpty())
                        obj[key] = parseNode(lines, index, subMapIndent + 1);
                    else
                        obj[key] = parseScalar(val);

                    // 继续解析同映射的后续 key-value 行（缩进 == subMapIndent）
                    QJsonObject rest = parseMapping(lines, index, subMapIndent);
                    for (auto it = rest.constBegin(); it != rest.constEnd(); ++it)
                        obj[it.key()] = it.value();

                    arr.append(obj);
                }
                else
                {
                    // 纯标量项
                    arr.append(parseScalar(itemContent));
                    ++index;
                }
            }
            return arr;
        }

        // 映射
        return parseMapping(lines, index, minIndent);
    }
    return QJsonValue();
}

} // namespace

// ============================================================================
// SkillManager 实现
// ============================================================================

SkillManager *SkillManager::instance()
{
    static SkillManager *s_inst = nullptr;
    if (!s_inst)
    {
        s_inst = new SkillManager(QCoreApplication::instance());
        s_inst->refresh();
    }
    return s_inst;
}

SkillManager::SkillManager(QObject *parent)
    : QObject(parent)
{
}

QString SkillManager::skillsDir() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/BlockBox");
}

void SkillManager::ensureSkillsDir() const
{
    QDir dir;
    if (!dir.exists(skillsDir()))
        dir.mkpath(skillsDir());
}

QString SkillManager::sanitizeId(const QString &name) const
{
    QString id;
    for (const QChar &ch : name)
    {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-'))
            id.append(ch.toLower());
        else
            id.append(QLatin1Char('_'));
    }
    if (id.isEmpty())
        id = QStringLiteral("skill");
    return id;
}

SkillInfo SkillManager::parseSkillMarkdown(const QString &content, const QString &filePath) const
{
    SkillInfo info;
    info.filePath = filePath;
    info.loaded = false;

    // 拆分 YAML front matter 与 Markdown 正文
    // front matter 以首行 --- 开始，到下一个 --- 结束
    QStringList lines = content.split(QLatin1Char('\n'));
    int startLine = -1;
    int endLine = -1;

    for (int i = 0; i < lines.size(); ++i)
    {
        QString trimmed = lines[i].trimmed();
        if (trimmed == QLatin1String("---"))
        {
            if (startLine < 0)
            {
                startLine = i;
            }
            else
            {
                endLine = i;
                break;
            }
        }
    }

    if (startLine < 0 || endLine < 0)
    {
        info.loadError = tr("SKILL.md 缺少 YAML front matter（需以 --- 分隔）");
        return info;
    }

    // 提取 front matter 行
    QStringList fmLines;
    for (int i = startLine + 1; i < endLine; ++i)
        fmLines.append(lines[i]);

    // 提取 Markdown 正文
    QStringList mdLines;
    for (int i = endLine + 1; i < lines.size(); ++i)
        mdLines.append(lines[i]);
    info.markdownContent = mdLines.join(QLatin1Char('\n')).trimmed();

    // 解析 front matter 为 JSON 对象
    int index = 0;
    QJsonObject root = parseMapping(fmLines, index, 0);

    info.name = root.value(QStringLiteral("name")).toString().trimmed();
    info.description = root.value(QStringLiteral("description")).toString().trimmed();
    info.version = root.value(QStringLiteral("version")).toString().trimmed();
    if (info.version.isEmpty())
        info.version = QStringLiteral("1.0.0");
    info.author = root.value(QStringLiteral("author")).toString().trimmed();

    // 权限列表
    const QJsonArray perms = root.value(QStringLiteral("permissions")).toArray();
    for (const QJsonValue &v : perms)
        info.permissions.append(v.toString().trimmed().toLower());

    // 工具定义
    const QJsonArray toolsArr = root.value(QStringLiteral("tools")).toArray();
    for (const QJsonValue &v : toolsArr)
    {
        QJsonObject tobj = v.toObject();
        SkillTool tool;
        tool.name = tobj.value(QStringLiteral("name")).toString().trimmed();
        tool.description = tobj.value(QStringLiteral("description")).toString().trimmed();
        tool.parameters = tobj.value(QStringLiteral("parameters")).toObject();
        tool.script = tobj.value(QStringLiteral("script")).toString().trimmed();
        tool.timeout = tobj.value(QStringLiteral("timeout")).toInt(60);

        // HTTP 定义
        QJsonObject httpObj = tobj.value(QStringLiteral("http")).toObject();
        if (!httpObj.isEmpty())
        {
            tool.http.method = httpObj.value(QStringLiteral("method")).toString().trimmed();
            if (tool.http.method.isEmpty())
                tool.http.method = QStringLiteral("GET");
            tool.http.url = httpObj.value(QStringLiteral("url")).toString().trimmed();
            tool.http.body = httpObj.value(QStringLiteral("body")).toString();
            QJsonObject headersObj = httpObj.value(QStringLiteral("headers")).toObject();
            for (auto it = headersObj.constBegin(); it != headersObj.constEnd(); ++it)
                tool.http.headers.insert(it.key(), it.value().toString());
        }

        if (tool.isValid())
            info.tools.append(tool);
    }

    if (info.name.isEmpty())
    {
        info.loadError = tr("SKILL.md front matter 缺少 name 字段");
        return info;
    }

    info.id = sanitizeId(info.name);
    info.loaded = true;
    return info;
}

SkillInfo SkillManager::parseSkillFile(const QString &filePath) const
{
    SkillInfo info;
    info.filePath = filePath;
    info.loaded = false;

    QString manifest;
    if (!PluginZip::extractEntryToString(filePath, kSkillManifest, manifest))
    {
        info.loadError = tr("清单文件 SKILL.md 缺失或无法读取");
        return info;
    }

    return parseSkillMarkdown(manifest, filePath);
}

void SkillManager::refresh()
{
    ensureSkillsDir();

    QList<SkillInfo> parsed;
    QDir dir(skillsDir());
    const QFileInfoList entries = dir.entryInfoList(QStringList() << QStringLiteral("*") + kSkillSuffix,
                                                    QDir::Files | QDir::Readable,
                                                    QDir::Name);
    for (const QFileInfo &fi : entries)
        parsed.append(parseSkillFile(fi.absoluteFilePath()));

    m_skills = parsed;
    emit skillsChanged();
}

SkillInfo SkillManager::skillAt(int index) const
{
    if (index < 0 || index >= m_skills.size())
        return SkillInfo();
    return m_skills.at(index);
}

SkillInfo SkillManager::skillById(const QString &id) const
{
    for (const SkillInfo &s : m_skills)
    {
        if (s.id == id)
            return s;
    }
    return SkillInfo();
}

SkillInfo SkillManager::inspectSkillFile(const QString &srcFile) const
{
    return parseSkillFile(srcFile);
}

QString SkillManager::collectScriptsContent(const QString &srcFile) const
{
    bool ok = false;
    const QStringList entries = PluginZip::listEntries(srcFile, &ok);
    if (!ok)
        return QString();

    QStringList scripts;
    for (const QString &entry : entries)
    {
        QString ext = QFileInfo(entry).suffix().toLower();
        if (ext == QLatin1String("ps1") || ext == QLatin1String("bat") ||
            ext == QLatin1String("cmd") || ext == QLatin1String("vbs") ||
            ext == QLatin1String("js") || ext == QLatin1String("py") ||
            ext == QLatin1String("sh"))
        {
            QByteArray data;
            if (PluginZip::extractEntryToMemory(srcFile, entry, data))
                scripts.append(QString::fromUtf8(data));
        }
    }
    return scripts.join(QLatin1String("\n\n"));
}

bool SkillManager::importSkill(const QString &srcFile, QString *error)
{
    ensureSkillsDir();

    QFileInfo srcFi(srcFile);
    if (!srcFi.exists())
    {
        if (error)
            *error = tr("文件不存在");
        return false;
    }

    // 解析源文件获取 id，以此命名目标文件
    SkillInfo info = parseSkillFile(srcFile);
    if (!info.loaded)
    {
        if (error)
            *error = info.loadError.isEmpty() ? tr("解析 SKILL.md 失败") : info.loadError;
        return false;
    }

    QString destPath = skillsDir() + QStringLiteral("/") + info.id + kSkillSuffix;
    if (QFile::exists(destPath))
        QFile::remove(destPath);

    if (!QFile::copy(srcFile, destPath))
    {
        if (error)
            *error = tr("复制文件失败");
        return false;
    }

    refresh();
    return true;
}

bool SkillManager::removeSkill(const QString &id, QString *error)
{
    SkillInfo info = skillById(id);
    if (!info.isValid())
    {
        if (error)
            *error = tr("技能不存在");
        return false;
    }

    if (!QFile::remove(info.filePath))
    {
        if (error)
            *error = tr("删除文件失败");
        return false;
    }

    // 清除信任标记
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("Skills"));
    settings.remove(QStringLiteral("trusted/") + id);
    settings.remove(QStringLiteral("enabled/") + id);

    refresh();
    return true;
}

bool SkillManager::isEnabled(const QString &id) const
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("Skills"));
    return settings.value(QStringLiteral("enabled/") + id, true).toBool();
}

void SkillManager::setEnabled(const QString &id, bool enabled)
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("Skills"));
    settings.setValue(QStringLiteral("enabled/") + id, enabled);
    emit skillEnabledChanged(id, enabled);
}

bool SkillManager::isTrusted(const QString &id) const
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("Skills"));
    return settings.value(QStringLiteral("trusted/") + id, false).toBool();
}

void SkillManager::setTrusted(const QString &id, bool trusted)
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("Skills"));
    settings.setValue(QStringLiteral("trusted/") + id, trusted);
}

// ============================================================================
// 工具执行
// ============================================================================

QString SkillManager::executeScriptTool(const SkillInfo &info, const SkillTool &tool,
                                        const QJsonObject &args, QString &error)
{
    // 解包到临时目录
    const QString tmpDir = QDir::tempPath() + QStringLiteral("/BlockBox/skill_") + info.id;
    // 清理旧解包
    QDir(tmpDir).removeRecursively();
    QDir().mkpath(tmpDir);

    bool ok = false;
    PluginZip::extractAllToDir(info.filePath, tmpDir, &ok);
    if (!ok)
    {
        error = tr("解包技能失败");
        return QString();
    }

    const QString scriptPath = QDir::cleanPath(tmpDir + QStringLiteral("/") + tool.script);
    if (!QFileInfo::exists(scriptPath))
    {
        error = tr("包内未找到脚本：%1").arg(tool.script);
        return QString();
    }

    // 构建命令行参数：-Key Value 形式
    QStringList procArgs;
    QString program;
    QString type = tool.scriptType();

    if (type == QLatin1String("ps1"))
    {
        program = QStringLiteral("powershell");
        procArgs << QStringLiteral("-NoProfile") << QStringLiteral("-ExecutionPolicy")
                 << QStringLiteral("Bypass") << QStringLiteral("-WindowStyle")
                 << QStringLiteral("Hidden") << QStringLiteral("-File")
                 << QDir::toNativeSeparators(scriptPath);
    }
    else if (type == QLatin1String("bat"))
    {
        program = QStringLiteral("cmd");
        procArgs << QStringLiteral("/c") << QStringLiteral("call")
                 << QDir::toNativeSeparators(scriptPath);
    }
    else
    {
        error = tr("不支持的脚本类型：%1").arg(tool.script);
        return QString();
    }

    // 工具参数作为命令行参数传入（-Key Value）
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
    {
        procArgs << QStringLiteral("-%1").arg(it.key());
        QString val = it.value().isString() ? it.value().toString()
                                            : QString::fromUtf8(
                                                  QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
        if (!val.isEmpty())
            procArgs << val;
    }

    QProcess proc;
    // 同时通过环境变量传入完整 JSON，便于复杂脚本解析
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SKILL_ARGS"),
               QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
    env.insert(QStringLiteral("SKILL_ID"), info.id);
    env.insert(QStringLiteral("SKILL_TOOL"), tool.name);
    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(tmpDir);

    proc.start(program, procArgs);
    if (!proc.waitForStarted(5000))
    {
        error = tr("启动脚本失败：%1").arg(proc.errorString());
        return QString();
    }

    int timeoutMs = tool.timeout * 1000;
    if (timeoutMs <= 0)
        timeoutMs = 60000;

    if (!proc.waitForFinished(timeoutMs))
    {
        proc.kill();
        proc.waitForFinished(2000);
        error = tr("脚本执行超时（%1 秒）").arg(tool.timeout);
        return QString();
    }

    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    QString errOutput = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();

    if (proc.exitCode() != 0)
    {
        error = tr("脚本退出码 %1%2")
                    .arg(proc.exitCode())
                    .arg(errOutput.isEmpty() ? QString() : QStringLiteral("：") + errOutput);
        return output; // 仍返回标准输出（可能含部分结果）
    }

    if (output.isEmpty() && !errOutput.isEmpty())
        output = errOutput;

    return output;
}

QString SkillManager::executeHttpTool(const SkillTool &tool, const QJsonObject &args,
                                      QString &error)
{
    QString urlStr = tool.http.url;
    if (urlStr.isEmpty())
    {
        error = tr("HTTP 工具未定义 url");
        return QString();
    }

    // URL 模板替换：{key} → args[key]
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
    {
        urlStr.replace(QStringLiteral("{%1}").arg(it.key()),
                       it.value().toString());
    }

    QUrl url(urlStr);
    if (!url.isValid())
    {
        error = tr("无效的 URL：%1").arg(urlStr);
        return QString();
    }

    QNetworkAccessManager nam;
    QNetworkRequest request(url);

    // 默认 Content-Type
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    // 自定义请求头
    for (auto it = tool.http.headers.constBegin(); it != tool.http.headers.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    QString method = tool.http.method.toUpper();
    if (method.isEmpty())
        method = QStringLiteral("GET");

    // 请求体：优先用 manifest 定义的 body，否则 POST/PUT 用 args JSON
    QByteArray bodyData;
    if (method == QLatin1String("POST") || method == QLatin1String("PUT") ||
        method == QLatin1String("PATCH"))
    {
        if (!tool.http.body.isEmpty())
            bodyData = tool.http.body.toUtf8();
        else
            bodyData = QJsonDocument(args).toJson(QJsonDocument::Compact);
    }
    else
    {
        // GET/DELETE：剩余未用于模板替换的参数追加为 query
        QUrlQuery query(url);
        for (auto it = args.constBegin(); it != args.constEnd(); ++it)
        {
            if (!tool.http.url.contains(QStringLiteral("{%1}").arg(it.key())))
                query.addQueryItem(it.key(), it.value().toString());
        }
        url.setQuery(query);
        request.setUrl(url);
    }

    QNetworkReply *reply = nullptr;
    if (method == QLatin1String("GET"))
        reply = nam.get(request);
    else if (method == QLatin1String("POST"))
        reply = nam.post(request, bodyData);
    else if (method == QLatin1String("PUT"))
        reply = nam.put(request, bodyData);
    else if (method == QLatin1String("DELETE"))
        reply = nam.deleteResource(request);
    else
        reply = nam.sendCustomRequest(request, method.toUtf8(), bodyData);

    if (!reply)
    {
        error = tr("创建 HTTP 请求失败");
        return QString();
    }

    int timeoutMs = tool.timeout * 1000;
    if (timeoutMs <= 0)
        timeoutMs = 60000;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    QString result;
    if (timer.isActive())
    {
        // 正常完成
        timer.stop();
        if (reply->error() != QNetworkReply::NoError)
        {
            error = tr("HTTP 请求失败：%1").arg(reply->errorString());
            result = QString::fromUtf8(reply->readAll());
        }
        else
        {
            result = QString::fromUtf8(reply->readAll());
        }
    }
    else
    {
        // 超时
        reply->abort();
        error = tr("HTTP 请求超时（%1 秒）").arg(tool.timeout);
    }

    reply->deleteLater();
    return result;
}

QString SkillManager::executeTool(const QString &skillId, const QString &toolName,
                                  const QJsonObject &args, QString &error)
{
    SkillInfo info = skillById(skillId);
    if (!info.isValid())
    {
        error = QStringLiteral("SKILL_NOT_FOUND");
        return QString();
    }

    if (!isTrusted(skillId))
    {
        error = QStringLiteral("SKILL_NOT_TRUSTED");
        return QString();
    }

    SkillTool tool = info.tool(toolName);
    if (!tool.isValid())
    {
        error = QStringLiteral("SKILL_TOOL_NOT_FOUND: ") + toolName;
        return QString();
    }

    if (tool.http.isValid())
        return executeHttpTool(tool, args, error);
    else
        return executeScriptTool(info, tool, args, error);
}

QHash<QString, QString> SkillManager::enabledToolMap() const
{
    QHash<QString, QString> map;
    for (const SkillInfo &s : m_skills)
    {
        if (!s.isValid() || !isEnabled(s.id))
            continue;
        for (const SkillTool &t : s.tools)
        {
            if (t.isValid())
                map.insert(t.name, s.id);
        }
    }
    return map;
}

QJsonArray SkillManager::enabledToolsJson() const
{
    QJsonArray tools;
    for (const SkillInfo &s : m_skills)
    {
        if (!s.isValid() || !isEnabled(s.id))
            continue;
        for (const SkillTool &t : s.tools)
        {
            if (!t.isValid())
                continue;
            QJsonObject tool;
            tool["type"] = QStringLiteral("function");
            QJsonObject function;
            function["name"] = t.name;
            function["description"] = t.description;
            function["parameters"] = t.parameters.isEmpty()
                                         ? QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                       {QStringLiteral("properties"), QJsonObject{}},
                                                       {QStringLiteral("required"), QJsonArray{}}}
                                         : t.parameters;
            tool["function"] = function;
            tools.append(tool);
        }
    }
    return tools;
}

QString SkillManager::enabledSkillsPrompt() const
{
    QStringList parts;
    for (const SkillInfo &s : m_skills)
    {
        if (!s.isValid() || !isEnabled(s.id))
            continue;
        QStringList toolNames;
        for (const SkillTool &t : s.tools)
        {
            if (t.isValid())
                toolNames.append(t.name);
        }
        QString section = QStringLiteral("## 技能：%1\n工具：%2\n\n%3")
                              .arg(s.name, toolNames.join(QStringLiteral(", ")),
                                   s.markdownContent);
        parts.append(section);
    }

    if (parts.isEmpty())
        return QString();

    return QStringLiteral("以下是当前已启用的技能说明，你可以根据用户需求调用对应工具：\n\n") +
           parts.join(QStringLiteral("\n\n---\n\n"));
}
