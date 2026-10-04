/**
 * @file   CommandTranslator.cpp
 * @brief  中文指令 → 英文指令转换核心实现
 * @author BlockBox Team
 * @date   2026-07-04
 *
 * 实现细节：
 *   1. translate(): 主流程，去前导 '/' → 分词 → 匹配指令 → 翻译参数 / 兜底
 *   2. matchCommand(): 中文别名精确匹配 → 英文名精确匹配
 *   3. translateParams(): 按 params 定义逐位置翻译，pos 消费 3 个 args；
 *      selector 智能跳过（非 @ 开头且不在映射表中时用 default/@p 填充）
 *   4. translateParam(): 按 type 分发（selector/item/entity/effect/enum/其他）
 *   5. translateSelector(): 中文关键词 → @ 选择器
 *   6. naturalLanguageFallback(): 关键词扫描整句识别意图
 *   7. checkAvailability(): 版本 / OP / 服务端过滤
 *   8. compareVersions(): 按 '.' 分段整数比较
 */

#include "CommandTranslator.h"

#include <QtGlobal>

namespace {

/**
 * @brief 判断字符串是否为已知的目标选择器中文关键词
 * @param value 用户输入值
 * @return 是中文关键词（自己/我/最近玩家/所有玩家/所有实体/随机玩家）返回 true
 */
bool isSelectorKeyword(const QString &value)
{
    static const QStringList keywords = {
        QStringLiteral("自己"),
        QStringLiteral("我"),
        QStringLiteral("最近玩家"),
        QStringLiteral("所有玩家"),
        QStringLiteral("所有实体"),
        QStringLiteral("随机玩家"),
    };
    for (const QString &keyword : keywords)
    {
        if (keyword.compare(value, Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================
// 构造
// ============================================================================

CommandTranslator::CommandTranslator(CommandDatabase *db)
    : m_db(db)
{
}

// ============================================================================
// 主转换接口
// ============================================================================

TranslateResult CommandTranslator::translate(const QString &input, const CommandContext &ctx) const
{
    TranslateResult result;

    QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
    {
        result.warnings.append(QStringLiteral("输入为空"));
        return result;
    }

    // 去除前导 '/'
    if (trimmed.startsWith('/'))
    {
        trimmed = trimmed.mid(1).trimmed();
        if (trimmed.isEmpty())
        {
            result.warnings.append(QStringLiteral("输入为空"));
            return result;
        }
    }

    const QStringList parts = trimmed.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty())
    {
        result.warnings.append(QStringLiteral("输入为空"));
        return result;
    }

    const QString firstWord = parts.first();
    const auto cmd = matchCommand(firstWord);
    if (cmd)
    {
        result.matchedCommandName = cmd->name;
        result.unavailableReason = checkAvailability(*cmd, ctx);

        QStringList warnings;
        const QStringList translatedArgs = translateParams(*cmd, parts.mid(1), warnings);
        result.warnings = warnings;

        QString english = QStringLiteral("/") + cmd->name;
        if (!translatedArgs.isEmpty())
        {
            english += QStringLiteral(" ") + translatedArgs.join(QStringLiteral(" "));
        }
        result.englishCommand = english;
        result.success = true;
        return result;
    }

    return naturalLanguageFallback(input, ctx);
}

// ============================================================================
// 上下文过滤
// ============================================================================

QString CommandTranslator::checkAvailability(const CommandInfo &cmd, const CommandContext &ctx) const
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
    if (ctx.opLevel < cmd.opLevel)
    {
        return QStringLiteral("OP 等级不足，需要 %1").arg(cmd.opLevel);
    }

    // 服务端检查
    if (cmd.serverOnly && !ctx.isServer)
    {
        return QStringLiteral("仅服务端可用");
    }

    return QString();
}

// ============================================================================
// 中文命令名识别
// ============================================================================

std::optional<CommandInfo> CommandTranslator::matchCommand(const QString &firstWord) const
{
    auto cmd = m_db->findCommandByChinese(firstWord);
    if (cmd)
    {
        return cmd;
    }
    return m_db->findCommandByEnglish(firstWord);
}

// ============================================================================
// 参数翻译
// ============================================================================

QStringList CommandTranslator::translateParams(const CommandInfo &cmd, const QStringList &args,
                                               QStringList &warnings) const
{
    QStringList result;
    int argIndex = 0;

    for (const ParamInfo &param : cmd.params)
    {
        // pos 类型：一次消费 3 个 args (x y z)
        if (param.type == QStringLiteral("pos"))
        {
            if (argIndex + 2 < args.size())
            {
                result.append(args.at(argIndex));
                result.append(args.at(argIndex + 1));
                result.append(args.at(argIndex + 2));
                argIndex += 3;
            }
            else if (param.required)
            {
                warnings.append(QStringLiteral("缺少必填参数：%1").arg(param.name));
                break;
            }
            // 可选 pos 缺失则跳过
            continue;
        }

        if (argIndex < args.size())
        {
            const QString value = args.at(argIndex);

            // 智能跳过：当前 param 是 selector，但 value 不像选择器
            // （不以 @ 开头且不在中文关键词映射表中），则用默认值填充当前参数，
            // 将 value 留给下一个参数消费。
            // 例如 "给予 钻石剑 1" 中 "钻石剑" 不是选择器，selector 用 @p 填充，
            // "钻石剑" 留给 item 参数翻译为 diamond_sword。
            if (param.type == QStringLiteral("selector")
                && !value.startsWith('@')
                && !isSelectorKeyword(value))
            {
                QString fillValue = param.defaultValue.value_or(QStringLiteral("@p"));
                result.append(fillValue);
                continue; // 不推进 argIndex
            }

            bool ok = false;
            QString warning;
            const QString translated = translateParam(param, value, ok, warning);
            if (!warning.isEmpty())
            {
                warnings.append(warning);
            }
            result.append(translated);
            argIndex++;
        }
        else
        {
            // 无更多 args
            if (param.required)
            {
                warnings.append(QStringLiteral("缺少必填参数：%1").arg(param.name));
                break;
            }
            else if (param.defaultValue)
            {
                result.append(*param.defaultValue);
            }
            // else 跳过可选参数
        }
    }

    return result;
}

// ============================================================================
// 单个参数翻译
// ============================================================================

QString CommandTranslator::translateParam(const ParamInfo &paramInfo, const QString &value,
                                          bool &ok, QString &warning) const
{
    ok = true;
    warning.clear();

    const QString &type = paramInfo.type;

    if (type == QStringLiteral("selector"))
    {
        const QString translated = translateSelector(value);
        if (translated == value && !value.startsWith('@'))
        {
            // 非已知选择器（理论上 translateParams 已智能跳过，此处兜底）
            warning = QStringLiteral("未知目标选择器：%1").arg(value);
            ok = false;
        }
        return translated;
    }

    if (type == QStringLiteral("item"))
    {
        auto item = m_db->findItemByChinese(value);
        if (item)
        {
            return item->english;
        }
        auto itemEn = m_db->findItemByEnglish(value);
        if (itemEn)
        {
            return itemEn->english;
        }
        warning = QStringLiteral("未知物品：%1").arg(value);
        ok = false;
        return value;
    }

    if (type == QStringLiteral("entity"))
    {
        auto entity = m_db->findEntityByChinese(value);
        if (entity)
        {
            return entity->english;
        }
        auto entityEn = m_db->findEntityByEnglish(value);
        if (entityEn)
        {
            return entityEn->english;
        }
        warning = QStringLiteral("未知实体：%1").arg(value);
        ok = false;
        return value;
    }

    if (type == QStringLiteral("effect"))
    {
        auto effect = m_db->findEffectByChinese(value);
        if (effect)
        {
            return effect->english;
        }
        auto effectEn = m_db->findEffectByEnglish(value);
        if (effectEn)
        {
            return effectEn->english;
        }
        warning = QStringLiteral("未知效果：%1").arg(value);
        ok = false;
        return value;
    }

    if (type == QStringLiteral("enchantment"))
    {
        auto enchant = m_db->findEnchantmentByChinese(value);
        if (enchant)
        {
            return enchant->english;
        }
        auto enchantEn = m_db->findEnchantmentByEnglish(value);
        if (enchantEn)
        {
            return enchantEn->english;
        }
        warning = QStringLiteral("未知附魔：%1").arg(value);
        ok = false;
        return value;
    }

    if (type == QStringLiteral("enum"))
    {
        for (const QString &candidate : paramInfo.candidates)
        {
            if (candidate.compare(value, Qt::CaseInsensitive) == 0)
            {
                return candidate;
            }
        }
        warning = QStringLiteral("非法枚举值：%1（候选：%2）")
                      .arg(value, paramInfo.candidates.join(QStringLiteral(", ")));
        ok = false;
        return value;
    }

    // int / float / string / nbt / block — 原样返回
    return value;
}

// ============================================================================
// 目标选择器翻译
// ============================================================================

QString CommandTranslator::translateSelector(const QString &value) const
{
    // 已是标准 @ 选择器（@p/@s/@a/@e/@r/@n 及复杂选择器如 @a[name=Steve]）原样返回
    if (value.startsWith('@'))
    {
        // 中文显示形式 @自己/@最近玩家 等需转换为英文 @s/@p
        if (value.compare(QStringLiteral("@自己"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("@s");
        }
        if (value.compare(QStringLiteral("@最近玩家"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("@p");
        }
        if (value.compare(QStringLiteral("@所有玩家"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("@a");
        }
        if (value.compare(QStringLiteral("@所有实体"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("@e");
        }
        if (value.compare(QStringLiteral("@随机玩家"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("@r");
        }
        // 标准 @p/@s/@a/@e/@r 或复杂选择器原样返回
        return value;
    }

    if (value.compare(QStringLiteral("自己"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("我"), Qt::CaseInsensitive) == 0)
    {
        return QStringLiteral("@s");
    }
    if (value.compare(QStringLiteral("最近玩家"), Qt::CaseInsensitive) == 0)
    {
        return QStringLiteral("@p");
    }
    if (value.compare(QStringLiteral("所有玩家"), Qt::CaseInsensitive) == 0)
    {
        return QStringLiteral("@a");
    }
    if (value.compare(QStringLiteral("所有实体"), Qt::CaseInsensitive) == 0)
    {
        return QStringLiteral("@e");
    }
    if (value.compare(QStringLiteral("随机玩家"), Qt::CaseInsensitive) == 0)
    {
        return QStringLiteral("@r");
    }

    // 玩家名或未知 → 原样返回
    return value;
}

// ============================================================================
// 自然语言兜底
// ============================================================================

TranslateResult CommandTranslator::naturalLanguageFallback(const QString &input, const CommandContext &ctx) const
{
    TranslateResult result;

    // 内部辅助：根据英文指令名 + 已提取的参数列表构建 TranslateResult
    auto buildResult = [this, &ctx](const QString &cmdName, const QStringList &args) -> TranslateResult {
        TranslateResult r;
        auto cmd = m_db->findCommandByEnglish(cmdName);
        if (!cmd)
        {
            r.warnings.append(QStringLiteral("数据库中未找到指令：%1").arg(cmdName));
            return r;
        }
        r.matchedCommandName = cmd->name;
        r.unavailableReason = checkAvailability(*cmd, ctx);

        QStringList warnings;
        const QStringList translatedArgs = translateParams(*cmd, args, warnings);
        r.warnings = warnings;

        QString english = QStringLiteral("/") + cmd->name;
        if (!translatedArgs.isEmpty())
        {
            english += QStringLiteral(" ") + translatedArgs.join(QStringLiteral(" "));
        }
        r.englishCommand = english;
        r.success = true;
        return r;
    };

    // 内部辅助：从整句中提取最长中文名匹配的英文 ID
    auto extractItem = [this](const QString &text) -> QString {
        QString bestEnglish;
        int bestLen = 0;
        for (const ItemInfo &item : m_db->allItems())
        {
            for (const QString &alias : item.chinese)
            {
                if (text.contains(alias) && alias.length() > bestLen)
                {
                    bestLen = alias.length();
                    bestEnglish = item.english;
                }
            }
        }
        return bestEnglish;
    };

    auto extractEntity = [this](const QString &text) -> QString {
        QString bestEnglish;
        int bestLen = 0;
        for (const EntityInfo &entity : m_db->allEntities())
        {
            for (const QString &alias : entity.chinese)
            {
                if (text.contains(alias) && alias.length() > bestLen)
                {
                    bestLen = alias.length();
                    bestEnglish = entity.english;
                }
            }
        }
        return bestEnglish;
    };

    auto extractEffect = [this](const QString &text) -> QString {
        QString bestEnglish;
        int bestLen = 0;
        for (const EffectInfo &effect : m_db->allEffects())
        {
            for (const QString &alias : effect.chinese)
            {
                if (text.contains(alias) && alias.length() > bestLen)
                {
                    bestLen = alias.length();
                    bestEnglish = effect.english;
                }
            }
        }
        return bestEnglish;
    };

    // 注意：先检查"效果"，避免与"给"冲突（如"给我一个速度效果"应为 effect 而非 give）
    if (input.contains(QStringLiteral("效果")))
    {
        const QString effectEnglish = extractEffect(input);
        QStringList args;
        args << QStringLiteral("give") << QStringLiteral("@p");
        if (!effectEnglish.isEmpty())
        {
            args << effectEnglish;
        }
        return buildResult(QStringLiteral("effect"), args);
    }

    if (input.contains(QStringLiteral("给")))
    {
        const QString itemEnglish = extractItem(input);
        QStringList args;
        args << QStringLiteral("@p");
        if (!itemEnglish.isEmpty())
        {
            args << itemEnglish;
        }
        return buildResult(QStringLiteral("give"), args);
    }

    if (input.contains(QStringLiteral("传送")) || input.contains(QStringLiteral("瞬移")))
    {
        // 默认目标 @p（用户可在转换后手动修改）
        QStringList args;
        args << QStringLiteral("@p");
        return buildResult(QStringLiteral("teleport"), args);
    }

    if (input.contains(QStringLiteral("召唤")))
    {
        const QString entityEnglish = extractEntity(input);
        QStringList args;
        if (!entityEnglish.isEmpty())
        {
            args << entityEnglish;
        }
        return buildResult(QStringLiteral("summon"), args);
    }

    if (input.contains(QStringLiteral("模式")))
    {
        QString mode;
        if (input.contains(QStringLiteral("生存")))
        {
            mode = QStringLiteral("survival");
        }
        else if (input.contains(QStringLiteral("创造")))
        {
            mode = QStringLiteral("creative");
        }
        else if (input.contains(QStringLiteral("冒险")))
        {
            mode = QStringLiteral("adventure");
        }
        else if (input.contains(QStringLiteral("观察")))
        {
            mode = QStringLiteral("spectator");
        }
        QStringList args;
        if (!mode.isEmpty())
        {
            args << mode;
        }
        return buildResult(QStringLiteral("gamemode"), args);
    }

    if (input.contains(QStringLiteral("时间")))
    {
        QString timeValue;
        if (input.contains(QStringLiteral("白天")))
        {
            timeValue = QStringLiteral("day");
        }
        else if (input.contains(QStringLiteral("晚上")) || input.contains(QStringLiteral("夜晚")))
        {
            timeValue = QStringLiteral("night");
        }
        else if (input.contains(QStringLiteral("中午")))
        {
            timeValue = QStringLiteral("noon");
        }
        else if (input.contains(QStringLiteral("午夜")))
        {
            timeValue = QStringLiteral("midnight");
        }
        QStringList args;
        args << QStringLiteral("set");
        if (!timeValue.isEmpty())
        {
            args << timeValue;
        }
        return buildResult(QStringLiteral("time"), args);
    }

    if (input.contains(QStringLiteral("天气")))
    {
        QString weather;
        if (input.contains(QStringLiteral("晴")))
        {
            weather = QStringLiteral("clear");
        }
        else if (input.contains(QStringLiteral("雨")))
        {
            weather = QStringLiteral("rain");
        }
        else if (input.contains(QStringLiteral("雷")))
        {
            weather = QStringLiteral("thunder");
        }
        QStringList args;
        if (!weather.isEmpty())
        {
            args << weather;
        }
        return buildResult(QStringLiteral("weather"), args);
    }

    result.warnings.append(QStringLiteral("无法识别指令意图"));
    return result;
}

// ============================================================================
// 版本比较
// ============================================================================

int CommandTranslator::compareVersions(const QString &a, const QString &b)
{
    const QStringList partsA = a.split('.');
    const QStringList partsB = b.split('.');
    const int maxLen = qMax(partsA.size(), partsB.size());
    for (int i = 0; i < maxLen; ++i)
    {
        const int numA = (i < partsA.size()) ? partsA.at(i).toInt() : 0;
        const int numB = (i < partsB.size()) ? partsB.at(i).toInt() : 0;
        if (numA < numB)
        {
            return -1;
        }
        if (numA > numB)
        {
            return 1;
        }
    }
    return 0;
}
