/**
 * @file   SkillInfo.h
 * @brief  AI 技能（Skill）数据结构声明
 * @author BlockBox Team
 * @date   2026-08-08
 *
 * BlockBox AI 技能是一份 zip 压缩包改后缀为 .skill 的文件，
 * 包内必须包含根级 SKILL.md 清单文件。SKILL.md 由两部分组成：
 *   1. YAML front matter（--- 分隔）：技能元数据与工具定义
 *   2. Markdown 正文：技能说明（注入 AI 系统提示词，指导 AI 如何使用本技能）
 *
 * SKILL.md 示例：
 * @code
 * ---
 * name: 天气查询
 * description: 查询指定城市的实时天气
 * version: 1.0.0
 * author: BlockBox Team
 * permissions:
 *   - network
 * tools:
 *   - name: get_weather
 *     description: 获取指定城市的天气信息
 *     parameters:
 *       type: object
 *       properties:
 *         city:
 *           type: string
 *           description: 城市名称
 *       required: [city]
 *     script: scripts/weather.ps1
 *     timeout: 30
 * ---
 * # 天气查询技能
 * 当用户询问天气时，调用 get_weather 工具获取实时天气数据。
 * @endcode
 */
#ifndef SKILLINFO_H
#define SKILLINFO_H

#include <QFileInfo>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

/**
 * @brief HTTP 请求定义（对应工具 http 字段，与 script 二选一）
 *
 * 当工具声明 http 字段时，宿主直接发起 HTTP 请求而非执行脚本，
 * 无需依赖本机脚本环境，适合调用外部 API。
 */
struct SkillHttpDef
{
    QString method;                   // HTTP 方法：GET / POST / PUT / DELETE（默认 GET）
    QString url;                      // 请求 URL
    QMap<QString, QString> headers;   // 请求头
    QString body;                     // 请求体（POST/PUT 时使用，可为空）

    bool isValid() const { return !url.isEmpty(); }
};

/**
 * @brief 技能工具定义（对应 SKILL.md front matter 中 tools[] 的一个元素）
 *
 * 每个工具是一个 AI 可调用的函数（OpenAI function calling 格式）。
 * 工具执行方式二选一：
 *   - script：执行包内脚本（.ps1 / .bat / .cmd），脚本标准输出作为工具结果回填给 AI
 *   - http：宿主直接发起 HTTP 请求，响应体作为工具结果回填给 AI
 */
struct SkillTool
{
    QString name;            // 工具名（英文/数字/下划线，全包内唯一）
    QString description;     // 工具描述（AI 据此决定是否调用）
    QJsonObject parameters;  // 参数 JSON Schema（OpenAI function calling 格式）
    QString script;          // 包内脚本相对路径（如 scripts/xxx.ps1），与 http 二选一
    SkillHttpDef http;       // HTTP 请求定义，与 script 二选一
    int timeout = 60;        // 执行超时（秒），默认 60

    /** 脚本类型（根据 script 扩展名推断）：ps1 / bat / http / empty */
    QString scriptType() const
    {
        if (http.isValid())
            return QStringLiteral("http");
        if (script.isEmpty())
            return QString();
        const QString ext = QFileInfo(script).suffix().toLower();
        if (ext == QStringLiteral("ps1"))
            return QStringLiteral("ps1");
        if (ext == QStringLiteral("bat") || ext == QStringLiteral("cmd"))
            return QStringLiteral("bat");
        return QString();
    }

    bool isValid() const { return !name.isEmpty() && (!script.isEmpty() || http.isValid()); }
};

/**
 * @brief 技能包信息结构体
 *
 * 对应 .skill 包内 SKILL.md 清单文件解析结果。
 */
struct SkillInfo
{
    QString id;                 // 技能唯一标识（取自 name 字段的英文化标识，或文件名）
    QString name;               // 技能显示名称（来自 front matter name 字段）
    QString description;        // 技能描述（来自 front matter description 字段）
    QString version;            // 版本号
    QString author;             // 作者

    // —— 能力权限（可选，front matter permissions）——
    // 复用插件系统的权限 token：file:read / file:write / registry / network / admin / system
    QStringList permissions;

    // —— 工具定义 ——
    QList<SkillTool> tools;

    // —— Markdown 正文（front matter 之后的全部内容）——
    // 注入 AI 系统提示词，指导 AI 如何使用本技能
    QString markdownContent;

    QString filePath;           // .skill 文件绝对路径
    bool loaded = false;        // 是否已成功解析（SKILL.md 缺失/损坏为 false）
    QString loadError;          // 解析失败时的错误信息

    bool isValid() const { return !name.isEmpty() && loaded; }

    /** 查找指定名称的工具，未找到返回空项 */
    SkillTool tool(const QString &toolName) const
    {
        for (const SkillTool &t : tools)
        {
            if (t.name == toolName)
                return t;
        }
        return SkillTool();
    }
};

#endif // SKILLINFO_H
