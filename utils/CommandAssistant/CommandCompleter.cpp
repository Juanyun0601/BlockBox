/**
 * @file   CommandCompleter.cpp
 * @brief  中文指令补全与下一个单词预测实现
 * @author BlockBox Team
 * @date   2026-07-04
 *
 * 实现细节：
 *   1. complete() 主流程：去前导 '/'、trim、按空格分词；首词匹配指令则进入参数补全，
 *      否则进入命令名补全
 *   2. completeCommandName() 调用数据库中文前缀查询，叠加英文名精确匹配
 *   3. completeParam() 根据 ParamInfo.type 分派：selector 返回固定选择器候选，
 *      item/entity/effect 调用数据库中文前缀查询，enum 返回 candidates，
 *      其他类型（int/float/string/nbt/pos/block）无补全建议
 *   4. checkAvailability() 复制 CommandTranslator 的版本/OP/服务端校验逻辑以保持解耦
 *   5. pos 类型参数占用 3 个位置（x y z），complete() 中对 pos 类型特殊处理
 */

#include "CommandCompleter.h"

#include <QDebug>
#include <QSet>

#include "BlockRegistry.h"

namespace {

/**
 * @brief 固定的目标选择器候选（按 spec 顺序）
 */
struct SelectorEntry
{
    const char *insert;
    const char *display;
    const char *detail;
};

const SelectorEntry kSelectorEntries[] = {
    {"@p", "@最近玩家", "最近玩家"},
    {"@s", "@自己", "自己"},
    {"@a", "@所有玩家", "所有玩家"},
    {"@e", "@所有实体", "所有实体"},
    {"@r", "@随机玩家", "随机玩家"}};

} // namespace

CommandCompleter::CommandCompleter(CommandDatabase *db)
    : m_db(db)
{
    if (m_db == nullptr)
    {
        qWarning() << "CommandCompleter: null CommandDatabase pointer";
    }
}

void CommandCompleter::setBlockRegistry(BlockRegistry *registry)
{
    m_blockRegistry = registry;
}

QList<CompletionCandidate> CommandCompleter::complete(const QString &input, const CommandContext &ctx) const
{
    QList<CompletionCandidate> result;

    if (m_db == nullptr)
    {
        return result;
    }

    // 去除前导 '/' 并 trim
    QString trimmed = input.trimmed();
    if (trimmed.startsWith(QStringLiteral("/")))
    {
        trimmed = trimmed.mid(1).trimmed();
    }

    // 输入为空：返回所有指令候选
    if (trimmed.isEmpty())
    {
        const QList<CommandInfo> &all = m_db->allCommands();
        for (const CommandInfo &cmd : all)
        {
            if (cmd.chinese.isEmpty())
            {
                continue;
            }
            CompletionCandidate candidate;
            candidate.displayText = cmd.chinese.first() + QStringLiteral("（") + cmd.name + QStringLiteral("）");
            candidate.insertText = cmd.chinese.first();
            candidate.detail = cmd.description;
            const QString reason = checkAvailability(cmd, ctx);
            candidate.available = reason.isEmpty();
            candidate.unavailableReason = reason;
            result.append(candidate);
        }
        return result;
    }

    // 按空格分词
    const QStringList parts = trimmed.split(QStringLiteral(" "), Qt::SkipEmptyParts);

    if (parts.size() == 1)
    {
        // 正在输入命令名
        const QString &first = parts.first();

        // 先尝试精确匹配（中文别名或英文名）
        std::optional<CommandInfo> exact = m_db->findCommandByChinese(first);
        if (!exact.has_value())
        {
            exact = m_db->findCommandByEnglish(first);
        }

        if (exact.has_value())
        {
            // 已匹配完整指令，返回下一个参数候选
            return completeParam(exact.value(), 0, QString(), ctx);
        }

        // 未精确匹配，返回前缀匹配的指令候选
        return completeCommandName(first, ctx);
    }

    // parts.size() >= 2：命令名已确认，正在输入参数
    const QString &cmdName = parts.first();
    std::optional<CommandInfo> matched = m_db->findCommandByChinese(cmdName);
    if (!matched.has_value())
    {
        matched = m_db->findCommandByEnglish(cmdName);
    }

    if (!matched.has_value())
    {
        return result;
    }

    const CommandInfo &cmd = matched.value();
    const QString currentWord = parts.last();

    // 计算当前参数索引（命令名后第几个参数）
    // 注意：pos 类型参数占 3 个位置（x y z），需要特殊处理
    // 已输入的参数个数 = parts.size() - 1（去掉命令名）；但 currentWord 是正在输入的，
    // 已确认的参数个数 = parts.size() - 2
    const int confirmedParamCount = parts.size() - 2;
    int paramIndex = -1;
    int consumedSlots = 0;
    for (int i = 0; i < cmd.params.size(); ++i)
    {
        const ParamInfo &param = cmd.params.at(i);
        if (param.type == QStringLiteral("pos"))
        {
            // pos 占 3 个槽位：若已确认的参数尚未跨过此 pos，则当前正在输入该 pos
            if (consumedSlots >= confirmedParamCount)
            {
                paramIndex = i;
                break;
            }
            consumedSlots += 3;
        }
        else
        {
            if (consumedSlots == confirmedParamCount)
            {
                paramIndex = i;
                break;
            }
            consumedSlots += 1;
        }
    }

    // 参数超出定义范围：用最后一个参数兜底
    if (paramIndex < 0 && !cmd.params.isEmpty())
    {
        paramIndex = cmd.params.size() - 1;
    }

    if (paramIndex < 0)
    {
        return result;
    }

    return completeParam(cmd, paramIndex, currentWord, ctx);
}

QList<CompletionCandidate> CommandCompleter::completeCommandName(const QString &prefix, const CommandContext &ctx) const
{
    QList<CompletionCandidate> result;

    if (m_db == nullptr || prefix.isEmpty())
    {
        return result;
    }

    // 中文前缀匹配
    const QList<CommandInfo> matched = m_db->findCommandsByChinesePrefix(prefix);

    // 用于去重：英文名精确匹配可能已在中文前缀结果中
    QSet<QString> seenEnglish;

    for (const CommandInfo &cmd : matched)
    {
        if (cmd.chinese.isEmpty())
        {
            continue;
        }
        CompletionCandidate candidate;
        candidate.displayText = cmd.chinese.first() + QStringLiteral("（") + cmd.name + QStringLiteral("）");
        candidate.insertText = cmd.chinese.first();
        candidate.detail = cmd.description;
        const QString reason = checkAvailability(cmd, ctx);
        candidate.available = reason.isEmpty();
        candidate.unavailableReason = reason;
        result.append(candidate);
        seenEnglish.insert(cmd.name.toLower());
    }

    // 英文名精确匹配（前缀等于全名的情况）
    const std::optional<CommandInfo> englishExact = m_db->findCommandByEnglish(prefix);
    if (englishExact.has_value() && !seenEnglish.contains(englishExact.value().name.toLower()))
    {
        const CommandInfo &cmd = englishExact.value();
        CompletionCandidate candidate;
        candidate.displayText = cmd.chinese.first() + QStringLiteral("（") + cmd.name + QStringLiteral("）");
        candidate.insertText = cmd.chinese.first();
        candidate.detail = cmd.description;
        const QString reason = checkAvailability(cmd, ctx);
        candidate.available = reason.isEmpty();
        candidate.unavailableReason = reason;
        result.append(candidate);
    }

    return result;
}

QList<CompletionCandidate> CommandCompleter::completeParam(const CommandInfo &cmd, int paramIndex,
                                                           const QString &currentWord,
                                                           const CommandContext &ctx) const
{
    Q_UNUSED(ctx);

    QList<CompletionCandidate> result;

    if (m_db == nullptr || paramIndex < 0 || paramIndex >= cmd.params.size())
    {
        return result;
    }

    const ParamInfo &param = cmd.params.at(paramIndex);

    if (param.type == QStringLiteral("selector"))
    {
        // 固定选择器候选，按前缀过滤（支持 @ 和中文前缀两种输入方式）
        for (const SelectorEntry &entry : kSelectorEntries)
        {
            const QString insert = QString::fromUtf8(entry.insert);
            const QString display = QString::fromUtf8(entry.display);
            // 用户输入 @p 或 @最近 等都能匹配
            bool match = currentWord.isEmpty();
            if (!match)
            {
                match = insert.startsWith(currentWord, Qt::CaseInsensitive)
                        || display.startsWith(currentWord, Qt::CaseInsensitive);
            }
            if (!match)
            {
                continue;
            }
            // 已完整输入的选择器不再重复显示（避免候选词与输入框重复）
            if (currentWord.compare(insert, Qt::CaseInsensitive) == 0
                || currentWord.compare(display, Qt::CaseInsensitive) == 0)
            {
                continue;
            }
            const QString detail = QString::fromUtf8(entry.detail);
            CompletionCandidate candidate;
            candidate.displayText = display;
            candidate.insertText = display; // 插入中文显示形式，翻译器负责转换为 @s/@p
            candidate.detail = detail;
            candidate.available = true;
            result.append(candidate);
        }
    }
    else if (param.type == QStringLiteral("item"))
    {
        // 物品中文前缀匹配
        const QList<ItemInfo> items = m_db->findItemsByChinesePrefix(currentWord);
        for (const ItemInfo &item : items)
        {
            if (item.chinese.isEmpty())
            {
                continue;
            }
            CompletionCandidate candidate;
            candidate.displayText = item.chinese.first() + QStringLiteral("（") + item.english + QStringLiteral("）");
            candidate.insertText = item.chinese.first();
            candidate.detail = QStringLiteral("英文：") + item.english;
            candidate.available = true;
            result.append(candidate);
        }
    }
    else if (param.type == QStringLiteral("entity"))
    {
        // 实体中文前缀匹配
        const QList<EntityInfo> entities = m_db->findEntitiesByChinesePrefix(currentWord);
        for (const EntityInfo &entity : entities)
        {
            if (entity.chinese.isEmpty())
            {
                continue;
            }
            CompletionCandidate candidate;
            candidate.displayText = entity.chinese.first() + QStringLiteral("（") + entity.english + QStringLiteral("）");
            candidate.insertText = entity.chinese.first();
            candidate.detail = QStringLiteral("英文：") + entity.english;
            candidate.available = true;
            result.append(candidate);
        }
    }
    else if (param.type == QStringLiteral("effect"))
    {
        // 效果中文前缀匹配
        const QList<EffectInfo> effects = m_db->findEffectsByChinesePrefix(currentWord);
        for (const EffectInfo &effect : effects)
        {
            if (effect.chinese.isEmpty())
            {
                continue;
            }
            CompletionCandidate candidate;
            candidate.displayText = effect.chinese.first() + QStringLiteral("（") + effect.english + QStringLiteral("）");
            candidate.insertText = effect.chinese.first();
            candidate.detail = QStringLiteral("英文：") + effect.english;
            candidate.available = true;
            result.append(candidate);
        }
    }
    else if (param.type == QStringLiteral("enchantment"))
    {
        // 附魔中文前缀匹配
        const QList<EnchantmentInfo> enchantments = m_db->findEnchantmentsByChinesePrefix(currentWord);
        for (const EnchantmentInfo &enchant : enchantments)
        {
            if (enchant.chinese.isEmpty())
            {
                continue;
            }
            CompletionCandidate candidate;
            candidate.displayText = enchant.chinese.first() + QStringLiteral("（") + enchant.english + QStringLiteral("）");
            candidate.insertText = enchant.chinese.first();
            candidate.detail = QStringLiteral("英文：") + enchant.english;
            candidate.available = true;
            result.append(candidate);
        }
    }
    else if (param.type == QStringLiteral("enum"))
    {
        // 枚举候选，按前缀过滤
        for (const QString &candidateValue : param.candidates)
        {
            if (!currentWord.isEmpty() && !candidateValue.startsWith(currentWord, Qt::CaseInsensitive))
            {
                continue;
            }
            CompletionCandidate candidate;
            candidate.displayText = candidateValue;
            candidate.insertText = candidateValue;
            candidate.detail = param.name;
            candidate.available = true;
            result.append(candidate);
        }
    }
    else if (param.type == QStringLiteral("block"))
    {
        // 方块 ID 补全（从 BlockRegistry 动态加载）
        if (m_blockRegistry != nullptr)
        {
            const QList<BlockInfo> blocks = m_blockRegistry->findBlocksByPrefix(currentWord);
            for (const BlockInfo &block : blocks)
            {
                CompletionCandidate candidate;
                if (block.chinese.isEmpty())
                {
                    candidate.displayText = block.english;
                }
                else
                {
                    candidate.displayText = block.chinese.first() + QStringLiteral("（") + block.english + QStringLiteral("）");
                }
                candidate.insertText = block.english;
                candidate.detail = QStringLiteral("方块");
                candidate.available = true;
                result.append(candidate);
            }
        }
    }

    // int / float / string / nbt / pos：无补全建议
    return result;
}

QString CommandCompleter::checkAvailability(const CommandInfo &cmd, const CommandContext &ctx) const
{
    // 版本检查
    if (!cmd.minVersion.isEmpty() && !ctx.version.isEmpty())
    {
        if (compareVersions(ctx.version, cmd.minVersion) < 0)
        {
            return QStringLiteral("当前版本不可用，需要 %1+").arg(cmd.minVersion);
        }
    }

    // OP 等级检查
    if (cmd.opLevel > 0 && ctx.opLevel < cmd.opLevel)
    {
        return QStringLiteral("需要 OP 等级 %1").arg(cmd.opLevel);
    }

    // 服务端检查
    if (cmd.serverOnly && !ctx.isServer)
    {
        return QStringLiteral("仅服务端可用");
    }

    return QString();
}

int CommandCompleter::compareVersions(const QString &a, const QString &b)
{
    const QStringList partsA = a.split(QStringLiteral("."));
    const QStringList partsB = b.split(QStringLiteral("."));

    const int maxLen = qMax(partsA.size(), partsB.size());
    for (int i = 0; i < maxLen; ++i)
    {
        bool okA = false;
        bool okB = false;
        const int valA = (i < partsA.size()) ? partsA.at(i).toInt(&okA) : 0;
        const int valB = (i < partsB.size()) ? partsB.at(i).toInt(&okB) : 0;

        const int effectiveA = okA ? valA : 0;
        const int effectiveB = okB ? valB : 0;

        if (effectiveA < effectiveB)
        {
            return -1;
        }
        if (effectiveA > effectiveB)
        {
            return 1;
        }
    }
    return 0;
}
