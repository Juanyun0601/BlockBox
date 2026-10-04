/**
 * @file   CommandCompleter.h
 * @brief  中文指令补全与下一个单词预测
 * @author BlockBox Team
 * @date   2026-07-04
 *
 * 根据用户当前输入与上下文（版本 / OP 等级 / 是否服务端）实时生成补全候选列表：
 *   1. 命令名补全：用户正在输入首词（中文别名前缀），返回匹配的指令候选
 *   2. 参数补全：命令名已确认，根据 params 数组与当前位置返回下一个参数候选
 *      （目标选择器、物品中文名、实体中文名、效果中文名、枚举值等）
 *   3. 上下文过滤：不可用指令在候选中标记 available=false 并附 unavailableReason
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "CommandDatabase.h"
#include "CommandTranslator.h"

class BlockRegistry; // 前置声明

/**
 * @brief 单个补全候选项
 */
struct CompletionCandidate
{
    QString displayText;       ///< 显示文本（如 "传送（teleport）" 或 "@p（最近玩家）" 或 "钻石剑（diamond_sword）"）
    QString insertText;        ///< 插入文本（如 "传送" 或 "@p" 或 "钻石剑"）
    QString detail;            ///< 详情说明（如 "传送实体" 或 "最近玩家" 或 "英文：diamond_sword"）
    bool available = true;     ///< 当前上下文是否可用
    QString unavailableReason; ///< 不可用原因（available=false 时填充）
};

/**
 * @brief 中文指令补全器
 *
 * 依赖 CommandDatabase 提供的查询接口与 CommandTranslator 定义的 CommandContext。
 *
 * 使用示例：
 * @code
 *   CommandDatabase db;
 *   db.load();
 *   CommandCompleter completer(&db);
 *   CommandContext ctx{QStringLiteral("1.20"), 4, false};
 *   QList<CompletionCandidate> candidates = completer.complete(QStringLiteral("传"), ctx);
 * @endcode
 */
class CommandCompleter
{
public:
    /**
     * @brief 构造补全器
     * @param db 指令数据库指针（调用方负责保证生命周期）
     */
    explicit CommandCompleter(CommandDatabase *db);

    /**
     * @brief 主补全接口：根据当前输入与上下文返回候选列表
     * @param input 用户输入（可能带前导 '/'）
     * @param ctx 当前上下文（版本 / OP 等级 / 是否服务端）
     * @return 候选列表
     */
    QList<CompletionCandidate> complete(const QString &input, const CommandContext &ctx) const;

    /**
     * @brief 设置方块注册表（用于 block 类型参数补全）
     * @param registry 方块注册表指针（调用方负责保证生命周期）
     */
    void setBlockRegistry(BlockRegistry *registry);

private:
    CommandDatabase *m_db;       ///< 指令数据库指针
    BlockRegistry *m_blockRegistry = nullptr; ///< 方块注册表指针（可选）

    /**
     * @brief 命令名补全（输入首词未匹配到完整指令时）
     * @param prefix 当前输入的首词
     * @param ctx 当前上下文
     * @return 匹配的指令候选列表
     */
    QList<CompletionCandidate> completeCommandName(const QString &prefix, const CommandContext &ctx) const;

    /**
     * @brief 参数补全（命令名已确认，预测下一个参数）
     * @param cmd 已匹配的指令
     * @param paramIndex 当前参数索引（0 表示命令名后第一个参数）
     * @param currentWord 当前正在输入的参数词（可能为空）
     * @param ctx 当前上下文
     * @return 候选列表
     */
    QList<CompletionCandidate> completeParam(const CommandInfo &cmd, int paramIndex,
                                             const QString &currentWord,
                                             const CommandContext &ctx) const;

    /**
     * @brief 检查指令在上下文是否可用
     * @param cmd 待检查指令
     * @param ctx 当前上下文
     * @return 空字符串表示可用；非空字符串为不可用原因
     */
    QString checkAvailability(const CommandInfo &cmd, const CommandContext &ctx) const;

    /**
     * @brief 比较两个版本号字符串
     * @param a 版本号（如 "1.13"）
     * @param b 版本号（如 "1.20"）
     * @return a<b 返回 -1，a==b 返回 0，a>b 返回 1
     */
    static int compareVersions(const QString &a, const QString &b);
};
