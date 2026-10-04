/**
 * @file   ModConfigParser.h
 * @brief  模组配置文件解析/表单模型/回写器
 * @author BlockBox Team
 * @date   2026-08-29
 *
 * 将实例 config/ 目录中常见格式的模组配置文件解析为「分节 + 设置项」
 * 的表单模型,供本地模组设置页(LocalModSettingsPage)渲染与编辑:
 *   - TOML(Forge/NeoForge 主流格式):解析 Forge 自动生成的注释元数据,
 *     #Range: min ~ max → 数值范围,#Allowed Values: → 下拉枚举,
 *     #Default: → 恢复默认值;
 *   - 旧版 Forge cfg(类型前缀 B:/I:/D:/S: + 花括号分节);
 *   - properties 键值对;
 *   - JSON(Fabric 模组常见):整体重序列化回写。
 *
 * 回写策略:
 *   - 行式格式(TOML/cfg/properties)仅替换被编辑键所在行,注释、
 *     空行与未编辑内容逐字保留;多行数组/内联表/数组表等复杂结构
 *     标记为 unsupported,只读展示并原样保留,不会破坏配置;
 *   - JSON 解析后整体重序列化(JSON 本无注释,格式变化可接受),
 *     复杂嵌套结构同样只读保留;
 *   - 写入使用 QSaveFile 原子提交,失败不影响原文件。
 */

#ifndef MODCONFIGPARSER_H
#define MODCONFIGPARSER_H

#include <QChar>
#include <QJsonDocument>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "ModData.h"

class ModConfigParser
{
public:
    /** 配置文件格式 */
    enum class Format
    {
        Unknown,
        Toml,       ///< Forge/NeoForge TOML 配置
        ForgeCfg,   ///< 旧版 Forge cfg(类型前缀 + 花括号分节)
        Properties, ///< properties 键值对
        Json        ///< Fabric 常见 JSON 配置
    };

    /** 设置项值类型 */
    enum class ValueType
    {
        Unsupported, ///< 多行数组/内联表/数组表等复杂结构(只读)
        Boolean,
        Integer,
        Floating,
        String,
        StringList   ///< 标量数组(TOML/JSON);旧版 cfg 不做列表推断
    };

    /** 单个设置项 */
    struct Option
    {
        QString key;               ///< 展示用键名(不含类型前缀与引号)
        QString keyRaw;            ///< 回写用原始键文本(TOML 可能带引号;ForgeCfg 含类型前缀)
        ValueType type = ValueType::Unsupported;
        QVariant value;            ///< bool / qlonglong / double / QString / QStringList
        QVariant defaultValue;     ///< #Default: 注释解析结果;无效表示未知
        QString comment;           ///< 键上方注释(不含 # 前缀,多行以 \n 连接)
        QString inlineComment;     ///< 行尾注释(TOML;不含 # 前缀)
        QStringList allowedValues; ///< #Allowed Values: 枚举候选
        bool hasRange = false;     ///< #Range: min ~ max
        double rangeMin = 0.0;
        double rangeMax = 0.0;
        bool rangeIsInt = false;   ///< 范围两端均为整数
        int line = -1;             ///< 原文件行号(0 基,行式格式回写定位;JSON 恒为 -1)
        bool unsupported = false;  ///< true 时只读展示 rawText
        QString rawText;           ///< unsupported 时的展示文本
        bool edited = false;       ///< 表单中已被修改
        QString jsonPath;          ///< JSON 专用:点分路径(根级键不含点)
        QChar separator = QLatin1Char('='); ///< properties 分隔符(= 或 :)
    };

    /** 分节(根节 path 为空) */
    struct Section
    {
        QString path;            ///< 完整路径("a" 或 "a.b",根节为空)
        QList<Option> options;
    };

    /** 解析结果文档 */
    struct Document
    {
        Format format = Format::Unknown;
        QString filePath;
        QString error;           ///< 解析失败原因
        QList<Section> sections; ///< 首个为根节(可能无选项)
        QStringList rawLines;    ///< 行式格式原始行(回写基底;JSON 为空)
        QJsonDocument jsonDoc;   ///< JSON 格式的工作副本
    };

    /** 按扩展名推断格式 */
    static Format detectFormat(const QString &filePath);

    /** 解析配置文件;失败时 out.error 给出原因 */
    static bool parse(const QString &filePath, Document &out);

    /** 将 Document 中 edited 的设置项写回原文件(原子写入) */
    static bool save(Document &doc, QString *error = nullptr);

    /**
     * 收集某个模组在 configDir 下的配置文件。
     * 匹配顺序:modid/jar 文件名精确匹配 → 前缀(id-* 等) →
     * 归一化包含匹配(模糊兜底);含 config/<id>/ 目录内文件。
     */
    static QStringList collectConfigFiles(const ModInfo &info, const QString &configDir);

    /** TOML 基本字符串转义(供表单层还原数组元素展示) */
    static QString escapeTomlString(const QString &s);
    static QString unescapeTomlString(const QString &s);

private:
    static bool parseLineBased(const QStringList &lines, Format format, Document &out);
    static void parseTomlValue(const QString &text, Option &opt);
    static void applyCommentMetadata(const QStringList &comments, Option &opt);
    static bool parseJson(const QByteArray &data, Document &out);
    static void walkJsonObject(const class QJsonObject &obj, const QString &path, Document &out);

    static bool saveLineBased(Document &doc, QString *error);
    static bool saveJson(Document &doc, QString *error);
    static QString tomlValueText(const Option &opt);
    static QString plainValueText(const Option &opt);
};

#endif // MODCONFIGPARSER_H
