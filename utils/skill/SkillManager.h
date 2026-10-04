/**
 * @file   SkillManager.h
 * @brief  AI 技能管理器类声明
 * @author BlockBox Team
 * @date   2026-08-08
 *
 * 管理软件同级 BlockBox 文件夹下的 .skill 技能文件。
 * .skill 文件本质是 zip 压缩包（修改后缀而来），
 * 包内 SKILL.md 清单文件保存技能信息（YAML front matter + Markdown 正文）。
 *
 * 职责：
 *   - 扫描技能目录、解析 SKILL.md 清单
 *   - 导入/删除技能（导入时复用 PluginSafetyGuard 做信任确认）
 *   - 启用/禁用与状态持久化（QSettings）
 *   - 执行技能工具（脚本 / HTTP 请求），供 AiService 调用
 *
 * 与 PluginManager 平行，复用 PluginZip 做 zip 读写、
 * 复用 PluginSafetyGuard 做信任确认与危险操作扫描。
 */
#ifndef SKILLMANAGER_H
#define SKILLMANAGER_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "utils/skill/SkillInfo.h"

class SkillManager : public QObject
{
    Q_OBJECT

public:
    static SkillManager *instance();

    /** 技能目录：软件同级目录下的 BlockBox 文件夹（与插件共用，按扩展名 .skill 区分） */
    QString skillsDir() const;

    /** 当前已解析的技能列表 */
    QList<SkillInfo> skills() const { return m_skills; }

    /** 按索引取技能，越界返回空 SkillInfo */
    SkillInfo skillAt(int index) const;

    /** 按 id 取技能，未找到返回空 SkillInfo */
    SkillInfo skillById(const QString &id) const;

    /** 重新扫描技能目录并解析清单 */
    void refresh();

    /**
     * @brief 导入技能：将 .skill 文件复制到技能目录
     *
     * 复制前不做信任确认（信任确认在 UI 层调用 confirmBeforeInstall 完成），
     * 本方法仅负责文件复制与刷新。如目标 id 已存在则覆盖。
     *
     * @param srcFile 源文件路径
     * @param error   输出参数：失败原因
     * @return true 成功
     */
    bool importSkill(const QString &srcFile, QString *error = nullptr);

    /**
     * @brief 解析（检查）外部 .skill 文件，不导入
     *
     * 用于导入前的信任确认：读取包内 SKILL.md 并解析为 SkillInfo，
     * 不复制文件、不刷新列表。loaded == false 时可通过 loadError 查看失败原因。
     *
     * @param srcFile .skill 文件路径
     * @return 解析后的技能信息（可能未加载成功）
     */
    SkillInfo inspectSkillFile(const QString &srcFile) const;

    /**
     * @brief 收集 .skill 包内所有可执行脚本内容（用于危险操作扫描）
     *
     * 遍历包内条目，提取扩展名为 ps1/bat/cmd/vbs/js/py/sh 的文本内容，
     * 按条目顺序拼接。找不到脚本或解析失败时返回空字符串。
     *
     * @param srcFile .skill 文件路径
     * @return 拼接后的脚本内容
     */
    QString collectScriptsContent(const QString &srcFile) const;

    /**
     * @brief 删除技能文件
     * @param id    技能 ID
     * @param error 输出参数：失败原因
     */
    bool removeSkill(const QString &id, QString *error = nullptr);

    // ==================== 启用 / 禁用 ====================

    /** 技能是否处于启用状态（默认启用；状态持久化于 QSettings） */
    bool isEnabled(const QString &id) const;

    /** 设置技能启用状态并持久化；状态变化时发 skillsChanged */
    void setEnabled(const QString &id, bool enabled);

    /** 技能是否已被信任（导入时确认过；未信任的技能不执行工具） */
    bool isTrusted(const QString &id) const;

    /** 标记技能为已信任并持久化 */
    void setTrusted(const QString &id, bool trusted);

    // ==================== 工具执行 ====================

    /**
     * @brief 执行技能工具（同步阻塞，供 AiService 在子线程调用）
     *
     * 根据工具定义选择执行方式：
     *   - script：解包到临时目录，按脚本类型执行（ps1 → powershell，bat/cmd → cmd），
     *             工具参数以 -Key Value 形式传入，标准输出作为结果返回
     *   - http：发起同步 HTTP 请求，响应体作为结果返回
     *
     * 超时由工具 timeout 字段控制（默认 60 秒）。
     * 未信任的技能执行被拒绝，error 置为 "SKILL_NOT_TRUSTED"。
     *
     * @param skillId 技能 ID
     * @param toolName 工具名
     * @param args 工具参数（JSON 对象）
     * @param error 输出参数：失败原因
     * @return 工具执行结果文本
     */
    QString executeTool(const QString &skillId, const QString &toolName,
                        const QJsonObject &args, QString &error);

    /**
     * @brief 收集所有已启用技能的工具定义（供 AiService 注入 Agent 工具集）
     * @return 工具名 → 所属技能 ID 的映射（用于分发时定位技能）
     */
    QHash<QString, QString> enabledToolMap() const;

    /**
     * @brief 收集所有已启用技能的工具定义 JSON（OpenAI function calling 格式）
     * @return QJsonArray，每个元素为 {type:function, function:{name,description,parameters}}
     */
    QJsonArray enabledToolsJson() const;

    /**
     * @brief 汇总所有已启用技能的 Markdown 说明（注入 AI 系统提示词）
     * @return 拼接后的技能说明文本（无启用技能时返回空字符串）
     */
    QString enabledSkillsPrompt() const;

signals:
    /** 技能列表发生变化（导入/删除/刷新后发出） */
    void skillsChanged();

    /** 技能启用状态发生变化 */
    void skillEnabledChanged(const QString &id, bool enabled);

private:
    explicit SkillManager(QObject *parent = nullptr);

    /** 解析 .skill 文件，读取 SKILL.md 并拆分 YAML front matter 与 Markdown 正文 */
    SkillInfo parseSkillFile(const QString &filePath) const;

    /** 确保技能目录存在 */
    void ensureSkillsDir() const;

    /** 从技能名称生成安全 ID（仅保留字母数字下划线，其余替换为下划线） */
    QString sanitizeId(const QString &name) const;

    /** 解析 SKILL.md 文件内容：拆分 front matter 与正文，解析 front matter 为 SkillInfo */
    SkillInfo parseSkillMarkdown(const QString &content, const QString &filePath) const;

    /** 同步执行脚本工具（解包 + QProcess 阻塞等待） */
    QString executeScriptTool(const SkillInfo &info, const SkillTool &tool,
                              const QJsonObject &args, QString &error);

    /** 同步执行 HTTP 工具（QNetworkAccessManager + QEventLoop 阻塞等待） */
    QString executeHttpTool(const SkillTool &tool, const QJsonObject &args,
                            QString &error);

    QList<SkillInfo> m_skills;
};

#endif // SKILLMANAGER_H
