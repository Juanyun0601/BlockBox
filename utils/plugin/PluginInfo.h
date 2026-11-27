/**
 * @file   PluginInfo.h
 * @brief  插件信息数据结构声明
 * @author BlockBox Team
 * @date   2026-08-05
 *
 * BlockBox 插件是一份 zip 压缩包改后缀为 .BlockBox 的文件，
 * 包内必须包含 plugin.json 清单文件保存插件信息。
 *
 * plugin.json 支持字段（大部分可选）：
 *   id/name/version/author/description/icon/entry/apiVersion
 *   homepage/license/tags/category/minApiVersion/updateUrl
 *   dependencies  （数组：依赖的其他插件 id，可带版本要求 "id@>=1.2"）
 *   settings      （数组：插件设置项定义，宿主据此生成设置表单）
 *   commands      （数组：插件对外提供的命令/动作）
 *   style         （字符串或 {qss,file} 对象：启动器界面样式贡献，见 PluginStyleContribution）
 *   aiTools       （数组：插件向 AI 助手提供的工具，见 PluginAiTool；
 *                  需同时请求权限 "ai:tool"，否则不会注入 AI）
 *   vars          （对象：插件私有变量，见 PluginInfo::vars；aiTools[].http 里
 *                  可用 {plugin.<键>} 引用，模型看不到值）
 *   ui.moreEntries（数组：在「更多」页添加的插件页面入口，见 PluginPageEntry；
 *                  需同时请求权限 "ui:page"，否则入口不会出现）
 */
#ifndef PLUGININFO_H
#define PLUGININFO_H

#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QStringList>

/**
 * @brief 插件设置项定义（对应 manifest.settings[] 元素）
 *
 * 宿主根据 items 动态生成设置表单，并将值持久化到
 * QSettings("BlockBox","Plugins") 的 "<pluginId>/<key>" 键下。
 */
struct PluginSettingItem
{
    QString key;                 // 设置键名
    QString label;               // 显示名称
    QString type;                // text / bool / number / select / color
    QString defaultValue;        // 默认值
    QStringList options;         // type == select 时的候选项

    bool isValid() const { return !key.isEmpty(); }

    static PluginSettingItem fromJson(const QJsonObject &obj)
    {
        PluginSettingItem item;
        item.key = obj.value(QStringLiteral("key")).toString().trimmed();
        item.label = obj.value(QStringLiteral("label")).toString().trimmed();
        item.type = obj.value(QStringLiteral("type")).toString().trimmed();
        if (item.type.isEmpty())
            item.type = QStringLiteral("text");
        item.defaultValue = obj.value(QStringLiteral("default")).toString();
        const QJsonArray opts = obj.value(QStringLiteral("options")).toArray();
        for (const QJsonValue &v : opts)
            item.options.append(v.toString());
        return item;
    }
};

/**
 * @brief 插件命令/动作定义（对应 manifest.commands[] 元素）
 *
 * 表示插件对外提供的一个可执行动作，宿主在插件详情页提供执行入口。
 * kind 取值：
 *   "command"  —— 在命令输入框输入命令（command 字段为命令名）
 *   "script"   —— 用系统默认方式打开包内脚本（script 字段为包内路径）
 *   "url"      —— 打开外部链接（url 字段为完整地址）
 *   "label"    —— 仅作分类标题展示（label 字段为标题文本），不执行任何动作，
 *                 宿主渲染为 QLabel 独占一行，用于在命令列表中做分组分隔
 *
 * 安全字段（可选）：
 *   "risk"      —— 风险等级：low / medium / high，未声明时默认 medium
 *   "riskNote"  —— 风险说明，展示在执行前确认弹窗中
 */
struct PluginCommand
{
    QString id;                  // 命令标识
    QString label;               // 显示名称
    QString kind;                // command / script / url
    QString target;              // 命令名 / 包内脚本路径 / 完整 URL
    QString riskLevel;           // 声明风险等级：low / medium / high（默认 medium）
    QString riskNote;            // 风险说明

    bool isValid() const { return !id.isEmpty(); }

    static PluginCommand fromJson(const QJsonObject &obj)
    {
        PluginCommand cmd;
        cmd.id = obj.value(QStringLiteral("id")).toString().trimmed();
        cmd.label = obj.value(QStringLiteral("label")).toString().trimmed();
        cmd.kind = obj.value(QStringLiteral("kind")).toString().trimmed();
        cmd.target = obj.value(QStringLiteral("target")).toString().trimmed();
        cmd.riskLevel = obj.value(QStringLiteral("risk")).toString().trimmed();
        cmd.riskNote = obj.value(QStringLiteral("riskNote")).toString().trimmed();
        return cmd;
    }
};

/**
 * @brief 插件界面字段定义（对应 manifest.ui.fields[] 元素）
 *
 * 宿主在插件详情卡片下方渲染输入控件，type 取值：
 *   text    —— 单行输入框
 *   path    —— 路径选择（输入框 + 浏览按钮，QFileDialog）
 *   number  —— 数字输入
 *   bool    —— 复选框
 *   select  —— 下拉框（options 为候选项）
 */
struct PluginUiField
{
    QString key;                 // 字段键名（动作参数引用）
    QString label;               // 显示名称
    QString type;                // text / path / number / bool / select
    QString defaultValue;        // 默认值
    QString placeholder;         // 占位提示
    bool required = false;       // 是否必填
    QStringList options;         // type == select 时的候选项

    bool isValid() const { return !key.isEmpty(); }

    static PluginUiField fromJson(const QJsonObject &obj)
    {
        PluginUiField f;
        f.key = obj.value(QStringLiteral("key")).toString().trimmed();
        f.label = obj.value(QStringLiteral("label")).toString().trimmed();
        f.type = obj.value(QStringLiteral("type")).toString().trimmed();
        if (f.type.isEmpty())
            f.type = QStringLiteral("text");
        f.defaultValue = obj.value(QStringLiteral("default")).toString();
        f.placeholder = obj.value(QStringLiteral("placeholder")).toString();
        f.required = obj.value(QStringLiteral("required")).toBool(false);
        const QJsonArray opts = obj.value(QStringLiteral("options")).toArray();
        for (const QJsonValue &v : opts)
            f.options.append(v.toString());
        return f;
    }
};

/**
 * @brief 插件界面动作定义（对应 manifest.ui.actions[] 元素）
 *
 * 宿主渲染为按钮，点击后执行包内脚本（静默模式），并把表单字段值作为参数传入，
 * 脚本的标准输出捕获显示在结果区（不弹独立窗口）。
 *
 * 安全字段（可选）：
 *   "risk"      —— 风险等级：low / medium / high，未声明时默认 medium
 *   "riskNote"  —— 风险说明，展示在执行前确认弹窗中
 */
struct PluginUiAction
{
    QString id;                  // 动作标识
    QString label;               // 按钮文字
    QString script;              // 包内脚本相对路径（如 scripts/xxx.ps1）
    QMap<QString, QString> args; // 脚本参数名 -> 字段 key（或字面值，如 "true"）
    QString riskLevel;           // 声明风险等级：low / medium / high（默认 medium）
    QString riskNote;            // 风险说明

    bool isValid() const { return !id.isEmpty() && !script.isEmpty(); }

    static PluginUiAction fromJson(const QJsonObject &obj)
    {
        PluginUiAction a;
        a.id = obj.value(QStringLiteral("id")).toString().trimmed();
        a.label = obj.value(QStringLiteral("label")).toString().trimmed();
        a.script = obj.value(QStringLiteral("script")).toString().trimmed();
        const QJsonObject argsObj = obj.value(QStringLiteral("args")).toObject();
        for (auto it = argsObj.constBegin(); it != argsObj.constEnd(); ++it)
            a.args.insert(it.key(), it.value().toString());
        a.riskLevel = obj.value(QStringLiteral("risk")).toString().trimmed();
        a.riskNote = obj.value(QStringLiteral("riskNote")).toString().trimmed();
        return a;
    }
};

/**
 * @brief 插件页面入口（对应 manifest.ui.moreEntries[] 元素）
 *
 * 插件借此在「更多」页（左侧「更多」分类）挂一个自己的入口卡片：
 * 声明后宿主在更多页「插件入口」区追加一张卡片，左侧「更多」子导航
 * 也会多出一项；插件停用/删除后入口自动消失。
 *
 * 声明该能力需同时请求权限 "ui:page"（未声明时入口不生效，
 * 插件详情页会给出提示），避免插件在用户不知情时往启动器里塞入口。
 *
 * 点击行为由 kind / target 决定（与 commands[] 一致）：
 *   - "url"    打开外部链接（target 为完整 URL）
 *   - "script" 执行包内脚本（target 为包内相对路径，如 scripts/page.ps1）
 */
struct PluginPageEntry
{
    QString id;          // 入口标识（同一插件内唯一）
    QString title;       // 卡片标题
    QString description; // 卡片说明（可空）
    QString icon;        // 包内图标相对路径（可空，为空时用内置图标）
    QString color;       // 主题色（可空，如 "#0EA5E9"；为空时用插件默认配色）
    QString kind;        // url / script
    QString target;      // 完整 URL / 包内脚本相对路径

    // —— 运行时字段（聚合时由宿主填写，清单里不声明）——
    QString pluginId;    // 提供该入口的插件 ID

    bool isValid() const
    {
        if (id.isEmpty() || title.isEmpty() || target.trimmed().isEmpty())
            return false;
        return kind == QLatin1String("url") || kind == QLatin1String("script");
    }

    static PluginPageEntry fromJson(const QJsonObject &obj)
    {
        PluginPageEntry e;
        e.id = obj.value(QStringLiteral("id")).toString().trimmed();
        e.title = obj.value(QStringLiteral("title")).toString().trimmed();
        e.description = obj.value(QStringLiteral("description")).toString().trimmed();
        e.icon = obj.value(QStringLiteral("icon")).toString().trimmed();
        e.color = obj.value(QStringLiteral("color")).toString().trimmed();
        e.kind = obj.value(QStringLiteral("kind")).toString().trimmed().toLower();
        e.target = obj.value(QStringLiteral("target")).toString().trimmed();
        return e;
    }
};

/**
 * @brief 插件界面定义（对应 manifest.ui 对象）
 *
 * 声明后，宿主在插件详情卡片下方渲染 表单 + 动作按钮 + 结果区，
 * 插件功能不再弹独立窗口（如 PowerShell 窗体）。
 */
struct PluginUi
{
    QList<PluginUiField> fields;   // 输入字段
    QList<PluginUiAction> actions; // 动作按钮
    QString style;                 // 界面样式（QSS 片段，宿主应用到内嵌界面区域，可为空）
    QList<PluginPageEntry> moreEntries; // 「更多」页入口（manifest.ui.moreEntries）

    bool isValid() const { return !actions.isEmpty(); }
};

/**
 * @brief 插件启动器样式贡献定义（对应 manifest.style）
 *
 * 插件可借此修改启动器（宿主）的界面样式。插件启用时样式生效，
 * 停用/删除后自动移除。支持两种写法：
 *   - 内联字符串：  "style": "TopBar { background: #202030; }"
 *   - 对象：        "style": { "qss": "…", "file": "styles/theme.qss" }
 *     其中 "file" 指向包内 .qss 文件（与 "qss" 内联文本叠加，均可选）。
 *
 * 样式支持 @TOKEN@ 占位符（如 @PRIMARY@、@BG_CARD@、@TEXT_PRIMARY@），
 * 宿主会按当前主题解析后追加到全局样式表末尾，因此可覆盖内置主题。
 * 声明该能力需同时请求权限 "ui:style"。
 */
struct PluginStyleContribution
{
    QString qss;                 // 内联 QSS 文本（可为空）
    QString styleFile;           // 包内 .qss 相对路径（可为空）

    bool isValid() const
    {
        return !qss.trimmed().isEmpty() || !styleFile.trimmed().isEmpty();
    }

    static PluginStyleContribution fromJson(const QJsonValue &val)
    {
        PluginStyleContribution s;
        if (val.isString()) {
            s.qss = val.toString();
        } else if (val.isObject()) {
            const QJsonObject obj = val.toObject();
            s.qss = obj.value(QStringLiteral("qss")).toString();
            s.styleFile = obj.value(QStringLiteral("file")).toString().trimmed();
        }
        return s;
    }
};

/**
 * @brief 插件 AI 工具的 HTTP 请求定义（对应 manifest.aiTools[].http）
 *
 * 声明后宿主直接发起 HTTP 请求（无需本机脚本环境），支持两类占位符：
 *   - {参数名}       运行时替换为模型传入的参数值
 *   - {plugin.<键>}  运行时替换为插件私有变量（manifest.vars 或保留键
 *                    token/port/data/app，见 PluginInfo::vars）
 *
 * 两类占位符都在宿主侧替换，私有变量的值不会出现在工具定义里，
 * 所以像本地服务的访问令牌这类东西可以安全地放在 vars 里。
 */
struct PluginAiToolHttp
{
    QString method;                  // HTTP 方法：GET / POST / PUT / DELETE（默认 GET）
    QString url;                     // 请求 URL（可含 {参数名} / {plugin.<键>} 占位符）
    QMap<QString, QString> headers;  // 请求头（值支持同样的占位符）
    QString body;                    // 请求体（POST/PUT/PATCH 时使用，可为空；支持占位符）

    bool isValid() const { return !url.trimmed().isEmpty(); }
};

/**
 * @brief 插件 AI 工具定义（对应 manifest.aiTools[] 元素）
 *
 * 插件借此把自身能力开放给 AI 助手：宿主把工具以 OpenAI function calling
 * 形式注入工作模式的工具集，模型可在对话中自主调用，调用结果回填给模型。
 *
 * 执行方式二选一：
 *   - script：解包插件并执行包内脚本（.ps1 / .bat / .cmd），标准输出作为结果；
 *             参数以 -Key Value 命令行参数传入，完整参数另经 PLUGIN_ARGS 环境变量传入
 *   - http：宿主直接发起 HTTP 请求，响应体作为结果
 *
 * 安全约定：插件必须同时声明权限 "ai:tool"（与 manifest.style 需要 "ui:style"
 * 的做法一致），未声明时工具不会注入 AI，插件详情页会给出提示。
 */
struct PluginAiTool
{
    QString name;            // 工具名（英文/数字/下划线，全局唯一；建议 <插件id>_<动作>）
    QString label;           // 显示名称（可空，作描述兜底）
    QString description;     // 工具描述（AI 据此决定是否调用）
    QJsonObject parameters;  // 参数 JSON Schema（OpenAI function calling 格式）
    QString script;          // 包内脚本相对路径（与 http 二选一）
    PluginAiToolHttp http;   // HTTP 请求定义（与 script 二选一）
    int timeout = 60;        // 执行超时（秒），默认 60
    QString riskLevel;       // 声明风险等级：low / medium / high（仅作展示/预留）
    QString riskNote;        // 风险说明（仅作展示/预留）

    /** 执行方式：ps1 / bat / http / 空（无效） */
    QString scriptType() const
    {
        if (http.isValid())
            return QStringLiteral("http");
        if (script.trimmed().isEmpty())
            return QString();
        const QString ext = QFileInfo(script).suffix().toLower();
        if (ext == QStringLiteral("ps1"))
            return QStringLiteral("ps1");
        if (ext == QStringLiteral("bat") || ext == QStringLiteral("cmd"))
            return QStringLiteral("bat");
        return QString();
    }

    /** 工具名是否合法：ASCII 字母/数字/下划线/连字符，1~64 字符（函数名规则） */
    static bool isValidName(const QString &toolName)
    {
        if (toolName.isEmpty() || toolName.size() > 64)
            return false;
        for (const QChar &c : toolName) {
            const bool asciiAlnum = c.isLetterOrNumber() && c.unicode() < 128;
            if (!asciiAlnum && c != QLatin1Char('_') && c != QLatin1Char('-'))
                return false;
        }
        return true;
    }

    bool isValid() const
    {
        return isValidName(name) && (!script.trimmed().isEmpty() || http.isValid());
    }

    static PluginAiTool fromJson(const QJsonObject &obj)
    {
        PluginAiTool t;
        t.name = obj.value(QStringLiteral("name")).toString().trimmed();
        t.label = obj.value(QStringLiteral("label")).toString().trimmed();
        t.description = obj.value(QStringLiteral("description")).toString().trimmed();
        t.parameters = obj.value(QStringLiteral("parameters")).toObject();
        t.script = obj.value(QStringLiteral("script")).toString().trimmed();
        t.timeout = obj.value(QStringLiteral("timeout")).toInt(60);
        t.riskLevel = obj.value(QStringLiteral("risk")).toString().trimmed();
        t.riskNote = obj.value(QStringLiteral("riskNote")).toString().trimmed();

        const QJsonObject httpObj = obj.value(QStringLiteral("http")).toObject();
        if (!httpObj.isEmpty()) {
            t.http.method = httpObj.value(QStringLiteral("method")).toString().trimmed().toUpper();
            if (t.http.method.isEmpty())
                t.http.method = QStringLiteral("GET");
            t.http.url = httpObj.value(QStringLiteral("url")).toString().trimmed();
            t.http.body = httpObj.value(QStringLiteral("body")).toString();
            const QJsonObject headers = httpObj.value(QStringLiteral("headers")).toObject();
            for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
                t.http.headers.insert(it.key(), it.value().toString());
        }
        return t;
    }
};

/**
 * @brief 插件信息结构体
 *
 * 对应 .BlockBox 包内 plugin.json 清单文件。
 */
struct PluginInfo
{
    QString id;                 // 插件唯一标识（英文/数字/下划线）
    QString name;               // 插件显示名称
    QString version;            // 插件版本号
    QString author;             // 作者
    QString description;        // 描述
    QString icon;               // 图标在包内的路径（如 assets/icon.png），可为空
    QString entry;              // 入口文件路径（如 main.js），可为空
    QString apiVersion;         // 适配的插件 API 版本，默认 "1.0"

    // —— 插件类型 ——
    // "script"（默认）：入口为脚本（ps1/js 等），宿主执行。
    // "native"（原生插件）：入口对应宿主内置的注册模块（如投影方块编辑器），
    //   无需脚本即可使用，宿主根据 id 映射到内置功能。
    QString kind;               // script / native，默认 script

    // —— 扩展元数据（可选）——
    QString homepage;           // 插件主页
    QString license;            // 许可证
    QStringList tags;           // 标签
    QString category;           // 分类（工具/美化/功能/其他）
    QString minApiVersion;      // 要求的最低宿主 API 版本
    QString updateUrl;          // 更新检查地址（指向新版本 .BlockBox 文件）
    QStringList dependencies;   // 依赖的其他插件 id

    // —— 能力权限（可选，manifest.permissions）——
    // 插件请求宿主授予的能力，宿主在信任确认弹窗中展示并据此做风控：
    //   file:read   读取方块盒子（启动器）目录文件
    //   file:write  修改方块盒子文件/代码（config.ini、资源等）
    //   registry    读写 Windows 注册表
    //   network     访问网络
    //   admin       请求管理员权限（UAC 提权）
    //   system      执行系统级操作（服务、关机、格式化等）
    //   ui:style    修改启动器界面样式（声明 manifest.style 时必须请求）
    //   ui:page     在「更多」页添加插件自己的页面入口（声明 ui.moreEntries 时必须请求）
    //   ai:tool     让 AI 助手调用插件提供的工具（声明 aiTools 时必须请求）
    QStringList permissions;    // 请求的能力权限 token 列表

    // —— 扩展能力定义（可选）——
    QList<PluginSettingItem> settings;  // 设置项定义
    QList<PluginCommand> commands;      // 对外命令/动作
    PluginUi ui;                        // 内嵌界面定义（有则宿主渲染表单+动作，不弹窗）
    PluginStyleContribution style;      // 启动器样式贡献（manifest.style）
    QList<PluginAiTool> aiTools;        // 提供给 AI 助手的工具（manifest.aiTools）

    // —— 插件私有变量（manifest.vars，可选）——
    // 声明后可在 aiTools[].http 的 url / headers / body 里写 {plugin.<键>}，
    // 宿主在发请求前替换，模型看不到这些值——本地服务的访问令牌不该进模型上下文。
    // 值为 "auto" 时由宿主生成并持久化（见 PluginManager::resolvedPluginVars）。
    //
    // 保留键（不用声明，写 aiTools 的插件一律可用）：
    //   token 32 位十六进制随机串，首次用到时生成、持久化在插件设置里
    //   port  空闲 TCP 端口（首次分配后固定，重启沿用；写具体数字则用它）
    //   data  插件私有数据目录（<数据目录>/plugin-data/<插件id>，自动创建）
    //   app   启动器（软件）所在目录
    QMap<QString, QString> vars;

    QString filePath;           // .BlockBox 文件绝对路径
    bool    loaded = false;     // 是否已成功解析（清单缺失/损坏为 false）
    QString loadError;          // 解析失败时的错误信息

    bool isValid() const { return !id.isEmpty() && !name.isEmpty() && loaded; }

    /** 是否声明了 ai:tool 权限（未声明时 aiTools 不会注入 AI） */
    bool hasAiToolPermission() const
    {
        return permissions.contains(QStringLiteral("ai:tool"));
    }

    /** 是否声明了 network 权限（使用 http 类型 AI 工具时建议声明） */
    bool hasNetworkPermission() const
    {
        return permissions.contains(QStringLiteral("network"));
    }

    /** 是否声明了插件私有变量（manifest.vars） */
    bool hasVars() const { return !vars.isEmpty(); }

    /** 是否声明了 ui:page 权限（未声明时 ui.moreEntries 不会出现在更多页） */
    bool hasPagePermission() const
    {
        return permissions.contains(QStringLiteral("ui:page"));
    }

    /** 有效（可在更多页展示）的入口数量 */
    int validMoreEntryCount() const
    {
        int count = 0;
        for (const PluginPageEntry &e : ui.moreEntries) {
            if (e.isValid())
                ++count;
        }
        return count;
    }

    /** 按 ID 查找更多页入口，未找到返回无效项 */
    PluginPageEntry moreEntry(const QString &entryId) const
    {
        for (const PluginPageEntry &e : ui.moreEntries) {
            if (e.id == entryId)
                return e;
        }
        return PluginPageEntry();
    }

    /** 有效（可注入/可执行）的 AI 工具数量 */
    int validAiToolCount() const
    {
        int count = 0;
        for (const PluginAiTool &t : aiTools) {
            if (t.isValid())
                ++count;
        }
        return count;
    }

    /** 查找指定名称的 AI 工具，未找到返回无效项 */
    PluginAiTool aiTool(const QString &toolName) const
    {
        for (const PluginAiTool &t : aiTools) {
            if (t.name == toolName)
                return t;
        }
        return PluginAiTool();
    }

    /** 查找指定 key 的设置项定义，未找到返回无效项 */
    PluginSettingItem settingItem(const QString &key) const
    {
        for (const PluginSettingItem &item : settings) {
            if (item.key == key)
                return item;
        }
        return PluginSettingItem();
    }
};

#endif // PLUGININFO_H
