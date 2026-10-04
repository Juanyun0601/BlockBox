/**
 * @file   CommandTranslator.h
 * @brief  中文指令 → 英文指令转换核心
 * @author BlockBox Team
 * @date   2026-07-04
 *
 * 负责将用户输入的中文指令（如 "给予 钻石剑 1"）或自然语言描述（如 "给我一把钻石剑"）
 * 转换为 Minecraft 可识别的英文指令（如 "/give @p diamond_sword 1"）。
 *
 * 转换流程：
 *   1. 去除首尾空格与前导 '/'，按空格分词
 *   2. 首词先按中文别名精确匹配，再按英文名精确匹配
 *   3. 匹配成功 → 调用 translateParams 按位置翻译参数
 *   4. 匹配失败 → 调用 naturalLanguageFallback 进行关键词兜底
 *   5. 通过 checkAvailability 进行版本 / OP / 服务端过滤
 *
 * 验证用例（无需实际运行，仅说明行为）：
 *   - "给予 钻石剑 1"
 *       → firstWord="给予" 命中 give；selector 参数智能跳过（"钻石剑" 不是 @ 开头
 *         也不在 selector 映射表中），用 @p 填充；"钻石剑" 作为 item 翻译为
 *         diamond_sword；"1" 作为 int 原样保留
 *       → "/give @p diamond_sword 1"
 *   - "给我一把钻石剑"
 *       → firstWord="给我" 不命中任何指令；进入自然语言兜底，命中"给"→give，
 *         从句子中匹配物品"钻石剑"→diamond_sword，玩家默认 @p
 *       → "/give @p diamond_sword 1"
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "CommandDatabase.h"

/**
 * @brief 当前游戏上下文，用于指令可用性过滤
 */
struct CommandContext
{
    QString version;       ///< 当前游戏版本（如 "1.20.4"），空字符串表示未知
    int opLevel = 0;       ///< 当前 OP 等级（0-4）
    bool isServer = false; ///< 是否服务端
};

/**
 * @brief 中文 → 英文指令转换结果
 */
struct TranslateResult
{
    QString englishCommand;    ///< 转换后的英文指令（含 /，如 "/give @p diamond_sword 1"）
    bool success = false;      ///< 是否成功转换
    QStringList warnings;      ///< 警告信息（如参数缺失等）
    QString unavailableReason; ///< 若指令在当前上下文不可用，给出原因（空字符串表示可用）
    QString matchedCommandName;///< 匹配到的英文指令名（如 "give"），未匹配为空
};

/**
 * @brief 中文 → 英文指令转换器
 *
 * 依赖 CommandDatabase 提供中英文映射数据，自身不持有数据所有权。
 *
 * 使用示例：
 * @code
 *   CommandDatabase db;
 *   db.load();
 *   CommandTranslator translator(&db);
 *   CommandContext ctx{QStringLiteral("1.20.4"), 4, false};
 *   TranslateResult result = translator.translate(QStringLiteral("给予 钻石剑 1"), ctx);
 *   if (result.success)
 *   {
 *       qDebug() << result.englishCommand << result.warnings << result.unavailableReason;
 *   }
 * @endcode
 */
class CommandTranslator
{
public:
    /**
     * @brief 构造转换器
     * @param db 已加载完成的指令数据库指针（调用方负责生命周期）
     */
    explicit CommandTranslator(CommandDatabase *db);

    /**
     * @brief 主转换接口
     * @param input 用户输入的中文指令或自然语言描述
     * @param ctx   当前游戏上下文
     * @return 转换结果
     */
    TranslateResult translate(const QString &input, const CommandContext &ctx) const;

    /**
     * @brief 上下文过滤：检查指令在当前上下文是否可用
     * @param cmd 指令信息
     * @param ctx 当前上下文
     * @return 不可用原因（空字符串表示可用）
     */
    QString checkAvailability(const CommandInfo &cmd, const CommandContext &ctx) const;

private:
    CommandDatabase *m_db; ///< 指令数据库指针（不持有所有权）

    /**
     * @brief 中文命令名识别
     * @param firstWord 已分词的第一段
     * @return 匹配的指令，未匹配返回 std::nullopt
     */
    std::optional<CommandInfo> matchCommand(const QString &firstWord) const;

    /**
     * @brief 参数翻译：根据指令的 params 定义，将中文参数列表翻译为英文参数列表
     * @param cmd      指令信息
     * @param args     用户输入的参数列表（不含指令名）
     * @param warnings 输出警告信息
     * @return 翻译后的英文参数列表（不含指令名）
     */
    QStringList translateParams(const CommandInfo &cmd, const QStringList &args,
                                QStringList &warnings) const;

    /**
     * @brief 单个参数翻译
     * @param paramInfo 参数定义
     * @param value     用户输入的原始值
     * @param ok        输出是否翻译成功
     * @param warning   输出警告信息
     * @return 翻译后的英文值
     */
    QString translateParam(const ParamInfo &paramInfo, const QString &value,
                           bool &ok, QString &warning) const;

    /**
     * @brief 目标选择器占位符翻译
     *
     * 映射表（大小写不敏感）：
     *   - "自己"/"我" → @s
     *   - "最近玩家" → @p
     *   - "所有玩家" → @a
     *   - "所有实体" → @e
     *   - "随机玩家" → @r
     *
     * 已是 @ 开头的选择器（@p/@s/@a/@e/@r/@n）原样返回；玩家名原样返回。
     *
     * @param value 用户输入值
     * @return 翻译后的选择器或原值
     */
    QString translateSelector(const QString &value) const;

    /**
     * @brief 自然语言兜底解析
     *
     * 当首词不匹配任何中文命令名时启用，按关键词扫描整句识别意图：
     *   - 包含"给" → give
     *   - 包含"传送"/"瞬移" → teleport
     *   - 包含"召唤" → summon
     *   - 包含"模式" → gamemode
     *   - 包含"时间" → time
     *   - 包含"天气" → weather
     *   - 包含"效果" → effect give
     *
     * @param input 用户输入的整句
     * @param ctx   当前上下文
     * @return 转换结果（无法识别时 success=false）
     */
    TranslateResult naturalLanguageFallback(const QString &input, const CommandContext &ctx) const;

    /**
     * @brief 版本比较
     * @param a 版本字符串（如 "1.12.2"）
     * @param b 版本字符串（如 "1.13"）
     * @return -1（a<b）/ 0（a==b）/ 1（a>b）
     *
     * 按 '.' 分段，每段转 int 比较，短的用 0 补齐。
     */
    static int compareVersions(const QString &a, const QString &b);
};
