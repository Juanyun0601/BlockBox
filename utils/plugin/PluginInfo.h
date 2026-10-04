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
 */
#ifndef PLUGININFO_H
#define PLUGININFO_H

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
    QStringList permissions;    // 请求的能力权限 token 列表

    // —— 扩展能力定义（可选）——
    QList<PluginSettingItem> settings;  // 设置项定义
    QList<PluginCommand> commands;      // 对外命令/动作
    PluginUi ui;                        // 内嵌界面定义（有则宿主渲染表单+动作，不弹窗）
    PluginStyleContribution style;      // 启动器样式贡献（manifest.style）

    QString filePath;           // .BlockBox 文件绝对路径
    bool    loaded = false;     // 是否已成功解析（清单缺失/损坏为 false）
    QString loadError;          // 解析失败时的错误信息

    bool isValid() const { return !id.isEmpty() && !name.isEmpty() && loaded; }

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
