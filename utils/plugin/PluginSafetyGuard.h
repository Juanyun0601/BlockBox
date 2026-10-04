/**
 * @file   PluginSafetyGuard.h
 * @brief  插件执行安全风控：信任确认弹窗 + 能力权限 + 危险操作扫描
 * @author BlockBox Team
 * @date   2026-08-07
 *
 * 为插件执行提供统一安全风控，核心是「你信任该插件吗？」确认弹窗：
 *   - 展示插件元数据（名称/作者/版本）与请求的能力权限列表
 *     （manifest.permissions：file:read / file:write / registry /
 *      network / admin / system）；
 *   - 扫描脚本危险模式（Remove-Item / shutdown / Format-Volume /
 *     hosts 修改 / 注册表操作等），结果展示在弹窗中；
 *   - 越权检测：脚本执行了敏感操作但插件未声明对应权限时，
 *     自动升级为高风险并给出警告；
 *   - 高风险（或声明 admin/system 等敏感权限）时须勾选
 *     「我已了解风险」才能执行，防止误点。
 */
#ifndef PLUGINSAFETYGUARD_H
#define PLUGINSAFETYGUARD_H

#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace PluginSafetyGuard {

/** 风险等级 */
enum RiskLevel {
    RiskLow = 0,      // 低：常规操作，几乎无副作用
    RiskMedium = 1,   // 中：可能影响系统/启动器设置（默认）
    RiskHigh = 2      // 高：可能删除数据、修改系统、不可逆
};

/** 脚本扫描到的单个风险点 */
struct RiskFinding
{
    QString pattern;   // 命中的危险模式（如 Remove-Item）
    QString detail;    // 说明文字
    bool critical;     // 是否高危（高危会把整体风险提升到 high）
};

/**
 * @brief 解析清单声明的风险等级字符串
 * @param s       "low" / "medium" / "high"（忽略大小写）
 * @param fallback 无法识别时的默认等级
 */
RiskLevel levelFromString(const QString &s, RiskLevel fallback = RiskMedium);

/** 风险等级显示名：低 / 中 / 高 */
QString levelDisplayName(RiskLevel level);

/**
 * @brief 扫描脚本内容中的危险命令模式
 *
 * 仅对每行去除注释（# / REM / :: 开头）后匹配，
 * 避免脚本注释中的关键词造成误报。
 */
QList<RiskFinding> scanScript(const QString &scriptContent);

/**
 * @brief 获取权限 token 的中文描述
 * @param token 如 "file:write"；未知权限原样返回
 */
QString permissionLabel(const QString &token);

/**
 * @brief 获取权限列表的中文描述（按声明顺序）
 */
QStringList permissionLabels(const QStringList &permissions);

/**
 * @brief 综合风险等级
 *
 * 规则：
 *   1. 取清单声明与脚本扫描结果的较高者（扫描到高危 → high）；
 *   2. 声明了敏感权限（admin/system 为 high，file:write/registry 至少 medium）；
 *   3. 存在越权（脚本执行敏感操作但未声明对应权限）→ high。
 */
RiskLevel effectiveLevel(RiskLevel declared,
                         const QList<RiskFinding> &findings,
                         const QStringList &permissions);

/**
 * @brief 检测越权：脚本命中的危险操作要求某权限，但插件未声明
 *
 * @param permissions 插件声明的权限列表
 * @param findings    脚本扫描结果
 * @return 越权权限 token 列表（去重），空表示无越权
 */
QStringList checkPermissionViolations(const QStringList &permissions,
                                      const QList<RiskFinding> &findings);

/**
 * @brief 插件动作执行前信任确认
 *
 * 弹出「你信任该插件吗？」确认弹窗，展示插件元数据、请求的能力权限、
 * 风险等级、风险说明、脚本扫描结果与越权警告；
 * 高风险时需勾选「我已了解风险」才能执行。
 *
 * @param parent        父窗口
 * @param pluginName    插件名称
 * @param pluginAuthor  插件作者
 * @param pluginVersion 插件版本
 * @param permissions   插件声明的能力权限列表
 * @param actionLabel   动作/命令名称
 * @param declared      清单声明风险等级
 * @param riskNote      清单声明风险说明（可为空）
 * @param scriptContent 脚本内容（用于扫描；可为空）
 * @return true 用户信任并确认执行；false 拒绝
 */
bool confirmBeforeExecute(QWidget *parent,
                          const QString &pluginName,
                          const QString &pluginAuthor,
                          const QString &pluginVersion,
                          const QStringList &permissions,
                          const QString &actionLabel,
                          RiskLevel declared,
                          const QString &riskNote,
                          const QString &scriptContent);

/**
 * @brief 添加（导入）插件前信任确认
 *
 * 与 confirmBeforeExecute 弹窗一致，但文案面向「添加插件」场景：
 * 展示插件元数据、请求的能力权限、风险等级、风险说明、
 * 脚本扫描结果与越权警告；高风险时需勾选「我已了解风险」才能添加。
 *
 * @param parent        父窗口
 * @param pluginName    插件名称
 * @param pluginAuthor  插件作者
 * @param pluginVersion 插件版本
 * @param permissions   插件声明的能力权限列表
 * @param declared      清单声明风险等级（无声明时传 RiskMedium）
 * @param riskNote      清单声明风险说明（可为空）
 * @param scriptContent 包内脚本内容汇总（用于危险操作扫描；可为空）
 * @return true 用户信任并允许添加；false 拒绝添加
 */
bool confirmBeforeInstall(QWidget *parent,
                          const QString &pluginName,
                          const QString &pluginAuthor,
                          const QString &pluginVersion,
                          const QStringList &permissions,
                          RiskLevel declared,
                          const QString &riskNote,
                          const QString &scriptContent);

/** 批量导入时单个插件的信任信息 */
struct BatchPluginEntry
{
    QString name;
    QString author;
    QString version;
    QStringList permissions;
    RiskLevel declared;
    QString riskNote;
    QString scriptContent;
};

/**
 * @brief 批量导入插件前信任确认（单次弹窗）
 *
 * 弹出一个汇总对话框，展示所有待导入插件的名称与风险等级。
 * 用户可选择「全部信任」（仅导入低/中风险插件，高风险需单独勾选）
 * 或「逐个确认」（退回到逐个弹窗流程）。
 *
 * @param parent   父窗口
 * @param entries  待导入插件列表
 * @param trustedIndices 输出参数：用户信任的插件索引列表
 * @return true 用户点击了信任（trustedIndices 非空）；false 全部拒绝
 */
bool confirmBatchInstall(QWidget *parent,
                         const QList<BatchPluginEntry> &entries,
                         QList<int> &trustedIndices);

} // namespace PluginSafetyGuard

#endif // PLUGINSAFETYGUARD_H
