/**
 * @file   PluginManager.h
 * @brief  插件管理器类声明
 * @author BlockBox Team
 * @date   2026-08-05
 *
 * 管理软件同级 BlockBox 文件夹下的 .BlockBox 插件文件。
 * .BlockBox 文件本质是 zip 压缩包（修改后缀而来），
 * 包内 plugin.json 清单文件保存插件信息。
 *
 * 除基础导入/删除外，还提供：
 *   - 启用/禁用与状态持久化（QSettings）
 *   - 导出插件、解包查看源码
 *   - API 兼容与依赖检查、语义化版本比较
 *   - 插件设置读写（QSettings 按插件 ID 分区）
 *   - 在线更新（从 updateUrl 下载 .BlockBox 并替换）
 */
#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "utils/plugin/PluginInfo.h"

class PluginManager : public QObject
{
    Q_OBJECT

public:
    static PluginManager *instance();

    /** 插件目录：软件同级目录下的 BlockBox 文件夹（不存在时自动创建） */
    QString pluginsDir() const;

    /** 当前已解析的插件列表（与子导航列表顺序一致） */
    QList<PluginInfo> plugins() const { return m_plugins; }

    /** 按索引取插件，越界返回空 PluginInfo */
    PluginInfo pluginAt(int index) const;

    /** 按 id 取插件，未找到返回空 PluginInfo */
    PluginInfo pluginById(const QString &id) const;

    /**
     * @brief 指定 id 的插件是否为原生（kind == "native"）插件
     */
    bool isNative(const QString &id) const;

    /**
     * @brief 指定原生插件是否可用（已安装、已解析、已启用且为 native）
     *
     * 供宿主在需要"以插件方式挂载内置功能"（如投影方块编辑器）时判断。
     */
    bool nativeAvailable(const QString &id) const;

    /**
     * @brief 获取插件图标缓存文件路径（无图标时返回空字符串）
     *
     * 将插件包内 icon 指向的图片提取到插件目录 .icons/ 下缓存
     * （文件名带包文件修改时间戳，包更新后自动重新提取），
     * 供侧边栏子导航等场景直接作为普通图片加载。
     */
    QString pluginIconPath(const PluginInfo &info) const;

    /** 重新扫描插件目录并解析清单 */
    void refresh();

    /**
     * @brief 导入插件：将 .BlockBox 文件复制到插件目录
     * @param srcFile 源文件路径
     * @param error   输出参数：失败原因
     * @return true 成功
     */
    bool importPlugin(const QString &srcFile, QString *error = nullptr);

    /**
     * @brief 检查（解析）外部 .BlockBox 文件，不导入
     *
     * 用于导入前的信任确认：读取包内 plugin.json 并解析为 PluginInfo，
     * 不复制文件、不刷新插件列表。loaded == false 时可通过 loadError
     * 查看失败原因。
     *
     * @param srcFile .BlockBox 文件路径
     * @return 解析后的插件信息（可能未加载成功）
     */
    PluginInfo inspectPluginFile(const QString &srcFile) const;

    /**
     * @brief 收集 .BlockBox 包内所有可执行脚本内容（用于危险操作扫描）
     *
     * 遍历包内条目，提取扩展名为 ps1/bat/cmd/vbs/js/py/sh 的文本内容，
     * 按条目顺序拼接。找不到脚本或解析失败时返回空字符串。
     *
     * @param srcFile .BlockBox 文件路径
     * @return 拼接后的脚本内容
     */
    QString collectScriptsContent(const QString &srcFile) const;

    /**
     * @brief 删除插件文件
     * @param id    插件 ID
     * @param error 输出参数：失败原因
     */
    bool removePlugin(const QString &id, QString *error = nullptr);

    // ==================== 启用 / 禁用 ====================

    /** 插件是否处于启用状态（默认启用；状态持久化于 QSettings） */
    bool isEnabled(const QString &id) const;

    /** 设置插件启用状态并持久化；状态变化时发 pluginsChanged */
    void setEnabled(const QString &id, bool enabled);

    // ==================== 导出 / 解包 ====================

    /**
     * @brief 导出插件：把 .BlockBox 复制到目标目录
     * @param id       插件 ID
     * @param destDir  目标目录
     * @param outPath  输出参数：导出的文件路径
     * @param error    输出参数：失败原因
     */
    bool exportPlugin(const QString &id, const QString &destDir,
                      QString *outPath = nullptr, QString *error = nullptr);

    /**
     * @brief 解包插件到目标目录（查看源码/备份）
     * @param id       插件 ID
     * @param destDir  目标目录（不存在时创建）
     * @param error    输出参数：失败原因
     */
    bool extractPlugin(const QString &id, const QString &destDir, QString *error = nullptr);

    // ==================== 校验 / 依赖 ====================

    /** 语义化版本比较：a>b 返回 1，a==b 返回 0，a<b 返回 -1；非法版本按 0 处理 */
    static int compareVersions(const QString &a, const QString &b);

    /** 当前宿主 API 版本（用于 minApiVersion / apiVersion 检查） */
    static QString hostApiVersion() { return QStringLiteral("1.0"); }

    /** 插件是否兼容当前宿主 API；不兼容时 reason 输出原因 */
    bool isApiCompatible(const PluginInfo &info, QString *reason = nullptr) const;

    /** 返回缺失的依赖插件 id 列表（依赖已安装且启用的插件） */
    QStringList missingDependencies(const PluginInfo &info) const;

    // ==================== 设置读写 ====================

    /** 读取插件设置（未设置时返回 defaultValue） */
    QString pluginSetting(const PluginInfo &info, const QString &key,
                          const QString &defaultValue = QString()) const;

    /** 写入插件设置并持久化 */
    void setPluginSetting(const PluginInfo &info, const QString &key, const QString &value);

    // ==================== 启动器样式贡献 ====================

    /**
     * @brief 获取插件贡献的启动器样式 QSS（内联 + 包内 .qss 文件叠加）
     *
     * 未声明 manifest.style 或样式无法读取时返回空字符串。
     *
     * @param info 插件信息
     * @return 完整 QSS 文本（可为空）
     */
    QString pluginStyleQss(const PluginInfo &info) const;

    /**
     * @brief 收集所有「已启用」插件贡献的启动器样式 QSS
     *
     * 供 ThemeManager 在合成全局样式表时追加（追加在后可覆盖内置主题）。
     *
     * @return 每个已启用插件的样式 QSS 列表（跳过无效/空样式）
     */
    QStringList activePluginStyles() const;

    // ==================== 更新 ====================

    /**
     * @brief 从插件清单 updateUrl 下载新版本并替换本地文件
     * @param id    插件 ID
     * @param error 输出参数：失败原因（如无 updateUrl、下载失败、新版本校验失败）
     * @return true 成功
     *
     * @note 同步阻塞版本（下载最长 30 秒），UI 侧请改用 updatePluginAsync
     */
    bool updatePlugin(const QString &id, QString *error = nullptr);

    /**
     * @brief updatePlugin 的异步版本：下载在工作线程执行，不阻塞 UI
     *
     * 结果通过 pluginUpdateFinished 信号（主线程）返回；
     * 同一插件更新进行中时重复调用会被忽略。
     * @return true 表示已启动异步更新（结果看信号），false 表示参数校验失败（也发信号）
     */
    bool updatePluginAsync(const QString &id);

    /**
     * @brief 生成插件项目模板
     * @param name        插件名称
     * @param id          插件 ID（文件/目录名）
     * @param version     版本号
     * @param author      作者
     * @param description 描述
     * @param parentDir   模板父目录（将在其下创建 <id> 文件夹）
     * @param outDir      输出参数：生成的模板目录路径
     * @param error       输出参数：失败原因
     */
    bool createTemplate(const QString &name, const QString &id, const QString &version,
                        const QString &author, const QString &description,
                        const QString &parentDir, QString &outDir, QString *error = nullptr);

    /**
     * @brief 将模板目录打包为 .BlockBox 文件，并复制到插件目录（自动刷新）
     * @param templateDir 模板目录绝对路径
     * @param outPath     输出参数：生成的 .BlockBox 文件绝对路径
     * @param error       输出参数：失败原因
     */
    bool packageToBlockBox(const QString &templateDir, QString &outPath, QString *error = nullptr);

signals:
    /** 插件列表发生变化（导入/打包/删除/刷新后发出） */
    void pluginsChanged();

    /** 插件启用状态发生变化 */
    void pluginEnabledChanged(const QString &id, bool enabled);

    /** 异步插件更新完成（updatePluginAsync 的结果回调，在主线程发出） */
    void pluginUpdateFinished(const QString &id, bool success, const QString &error);

private:
    explicit PluginManager(QObject *parent = nullptr);

    PluginInfo parsePluginFile(const QString &filePath) const;
    void ensurePluginsDir() const;
    QString sanitizeId(const QString &name) const;
    /** 插件设置存储键前缀：<pluginId>/<key> */
    QString settingKey(const QString &id, const QString &key) const;
    /** updatePlugin 的后半段：校验下载结果、版本比较并替换本地文件（主线程执行） */
    bool finalizePluginUpdate(const PluginInfo &info, const QString &tmpPath, QString *error);

    QList<PluginInfo> m_plugins;
    QSet<QString> m_updatingIds; ///< 正在异步更新的插件 ID（防止并发更新同一插件）
};

#endif // PLUGINMANAGER_H
