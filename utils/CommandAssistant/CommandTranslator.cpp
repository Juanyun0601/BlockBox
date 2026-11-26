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

#include <QHash>
#include <QRegularExpression>
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

/**
 * @brief 是否是带命名空间的资源 ID（minecraft:diamond、modid:item 等）
 *
 * 这类值本来就是游戏认识的写法，不需要再查中文表，也不该报「未知物品」。
 */
bool looksLikeNamespacedId(const QString &value)
{
    const int colon = value.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon >= value.size() - 1)
    {
        return false;
    }
    for (int i = 0; i < colon; ++i)
    {
        const QChar ch = value.at(i);
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_') && ch != QLatin1Char('-')
            && ch != QLatin1Char('.'))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 常见中文枚举值 → 指令数据库里的英文候选值
 *
 * 数据库的 enum 候选只存英文（survival / clear / destroy…），用户输入的中文值
 * 需要先映射成英文再和候选比对，否则会被当成非法值直接拼进指令。
 */
QString chineseEnumAlias(const QString &value)
{
    static const QHash<QString, QString> aliases = {
        // 游戏模式
        {QStringLiteral("生存"), QStringLiteral("survival")},
        {QStringLiteral("生存模式"), QStringLiteral("survival")},
        {QStringLiteral("创造"), QStringLiteral("creative")},
        {QStringLiteral("创造模式"), QStringLiteral("creative")},
        {QStringLiteral("冒险"), QStringLiteral("adventure")},
        {QStringLiteral("冒险模式"), QStringLiteral("adventure")},
        {QStringLiteral("旁观"), QStringLiteral("spectator")},
        {QStringLiteral("观察"), QStringLiteral("spectator")},
        {QStringLiteral("观察者"), QStringLiteral("spectator")},
        // 天气
        {QStringLiteral("晴"), QStringLiteral("clear")},
        {QStringLiteral("晴天"), QStringLiteral("clear")},
        {QStringLiteral("晴朗"), QStringLiteral("clear")},
        {QStringLiteral("放晴"), QStringLiteral("clear")},
        {QStringLiteral("雨"), QStringLiteral("rain")},
        {QStringLiteral("下雨"), QStringLiteral("rain")},
        {QStringLiteral("雨天"), QStringLiteral("rain")},
        {QStringLiteral("雷"), QStringLiteral("thunder")},
        {QStringLiteral("雷暴"), QStringLiteral("thunder")},
        {QStringLiteral("雷雨"), QStringLiteral("thunder")},
        {QStringLiteral("打雷"), QStringLiteral("thunder")},
        // 难度
        {QStringLiteral("和平"), QStringLiteral("peaceful")},
        {QStringLiteral("简单"), QStringLiteral("easy")},
        {QStringLiteral("普通"), QStringLiteral("normal")},
        {QStringLiteral("正常"), QStringLiteral("normal")},
        {QStringLiteral("困难"), QStringLiteral("hard")},
        // 时间操作
        {QStringLiteral("设置"), QStringLiteral("set")},
        {QStringLiteral("设定"), QStringLiteral("set")},
        {QStringLiteral("设为"), QStringLiteral("set")},
        {QStringLiteral("增加"), QStringLiteral("add")},
        {QStringLiteral("添加"), QStringLiteral("add")},
        {QStringLiteral("查询"), QStringLiteral("query")},
        {QStringLiteral("查看"), QStringLiteral("query")},
        // 放置 / 填充 / 克隆模式
        {QStringLiteral("破坏"), QStringLiteral("destroy")},
        {QStringLiteral("保留"), QStringLiteral("keep")},
        {QStringLiteral("替换"), QStringLiteral("replace")},
        {QStringLiteral("空心"), QStringLiteral("hollow")},
        {QStringLiteral("轮廓"), QStringLiteral("outline")},
        {QStringLiteral("遮罩"), QStringLiteral("masked")},
        // 布尔值
        {QStringLiteral("真"), QStringLiteral("true")},
        {QStringLiteral("假"), QStringLiteral("false")},
        {QStringLiteral("开启"), QStringLiteral("true")},
        {QStringLiteral("关闭"), QStringLiteral("false")},
        // 标题动作
        {QStringLiteral("标题"), QStringLiteral("title")},
        {QStringLiteral("副标题"), QStringLiteral("subtitle")},
        {QStringLiteral("动作栏"), QStringLiteral("actionbar")},
        {QStringLiteral("清空"), QStringLiteral("clear")},
        {QStringLiteral("重置"), QStringLiteral("reset")},
    };
    return aliases.value(value.trimmed());
}

/**
 * @brief 在候选列表里按忽略大小写查值，命中时返回候选原始写法
 */
QString canonicalCandidate(const QStringList &candidates, const QString &value)
{
    for (const QString &candidate : candidates)
    {
        if (candidate.compare(value, Qt::CaseInsensitive) == 0)
        {
            return candidate;
        }
    }
    return QString();
}

/**
 * @brief 时段中文 → /time 的英文值（白天 / 夜晚 / 正午 / 午夜…）
 */
QString timeOfDayAlias(const QString &value)
{
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("白天"), QStringLiteral("day")},
        {QStringLiteral("日间"), QStringLiteral("day")},
        {QStringLiteral("早上"), QStringLiteral("day")},
        {QStringLiteral("日出"), QStringLiteral("day")},
        {QStringLiteral("夜晚"), QStringLiteral("night")},
        {QStringLiteral("晚上"), QStringLiteral("night")},
        {QStringLiteral("夜间"), QStringLiteral("night")},
        {QStringLiteral("日落"), QStringLiteral("night")},
        {QStringLiteral("正午"), QStringLiteral("noon")},
        {QStringLiteral("中午"), QStringLiteral("noon")},
        {QStringLiteral("午夜"), QStringLiteral("midnight")},
        {QStringLiteral("半夜"), QStringLiteral("midnight")},
    };
    return aliases.value(value.trimmed());
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

        // 「时间 正午」「时间 设置 夜晚」：补全省略的 set，并把时段换成英文值
        QStringList args = parts.mid(1);
        if (cmd->name == QLatin1String("time") && !args.isEmpty())
        {
            const QString firstTimeValue = timeOfDayAlias(args.first());
            if (!firstTimeValue.isEmpty())
            {
                args = QStringList{QStringLiteral("set"), firstTimeValue} + args.mid(1);
            }
            else if (args.size() >= 2)
            {
                const QString secondTimeValue = timeOfDayAlias(args.at(1));
                if (!secondTimeValue.isEmpty())
                {
                    args[1] = secondTimeValue;
                }
            }
        }

        // 「经验 10」：省略操作、目标时按「增加给自己」处理
        if ((cmd->name == QLatin1String("xp") || cmd->name == QLatin1String("experience"))
            && args.size() == 1)
        {
            bool isNumber = false;
            args.first().toInt(&isNumber);
            if (isNumber)
            {
                args = QStringList{QStringLiteral("add"), QStringLiteral("@s"), args.first()};
            }
        }

        const QStringList translatedArgs = translateParams(*cmd, args, warnings);
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
    bool aborted = false;

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
                aborted = true;
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
                aborted = true;
                break;
            }
            else if (param.defaultValue)
            {
                result.append(*param.defaultValue);
            }
            // else 跳过可选参数
        }
    }

    // 参数表已全部走完但还有剩余词：最后一个参数是字符串类时整句拼回去
    // （say / execute / title 这类消息体本来就是多词的，不能只留第一个词）
    if (!aborted && argIndex < args.size() && !result.isEmpty() && !cmd.params.isEmpty()
        && cmd.params.last().type == QStringLiteral("string"))
    {
        result.last() += QLatin1Char(' ') + args.mid(argIndex).join(QLatin1Char(' '));
        argIndex = args.size();
    }

    if (argIndex < args.size())
    {
        warnings.append(QStringLiteral("多余的参数没有用到：%1")
                            .arg(args.mid(argIndex).join(QLatin1Char(' '))));
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
        if (looksLikeNamespacedId(value))
        {
            return value;
        }
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
        if (looksLikeNamespacedId(value))
        {
            return value;
        }
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
        if (looksLikeNamespacedId(value))
        {
            return value;
        }
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
        if (looksLikeNamespacedId(value))
        {
            return value;
        }
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
        // 1. 用户直接写了英文候选值
        const QString direct = canonicalCandidate(paramInfo.candidates, value);
        if (!direct.isEmpty())
        {
            return direct;
        }
        // 2. 中文说法（创造 / 晴 / 破坏…）映射成英文后再和候选比对
        const QString aliased = canonicalCandidate(paramInfo.candidates, chineseEnumAlias(value));
        if (!aliased.isEmpty())
        {
            return aliased;
        }
        warning = QStringLiteral("非法枚举值：%1（候选：%2）")
                      .arg(value, paramInfo.candidates.join(QStringLiteral(", ")));
        ok = false;
        return value;
    }

    if (type == QStringLiteral("block"))
    {
        // 方块和物品共用同一套 ID：石头 / 石头砖 等先在物品表里找，
        // 让 /setblock、/fill 这类指令也能吃中文方块名。
        if (looksLikeNamespacedId(value))
        {
            return value;
        }
        if (auto block = m_db->findItemByChinese(value))
        {
            return block->english;
        }
        if (auto block = m_db->findItemByEnglish(value))
        {
            return block->english;
        }
        warning = QStringLiteral("未知方块：%1").arg(value);
        ok = false;
        return value;
    }

    // int / float / string / nbt — 原样返回
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

    // 整句里显式写出的目标选择器（@a / @p[name=…] 等）
    auto extractSelector = [](const QString &text) -> QString {
        static const QRegularExpression selectorRe(QStringLiteral("@[praens](?:\\[[^\\]]*\\])?"));
        const QRegularExpressionMatch match = selectorRe.match(text);
        return match.hasMatch() ? match.captured(0) : QString();
    };

    // 整句里显式写出的坐标（普通数字或 ~ / ^ 相对坐标）
    auto extractCoordinates = [](const QString &text) -> QStringList {
        static const QRegularExpression coordRe(
            QStringLiteral("(?:^|\\s)([~^]?(?:-?\\d+(?:\\.\\d+)?|~))(?=\\s|$)"));
        QStringList coords;
        auto it = coordRe.globalMatch(text);
        while (it.hasNext())
        {
            coords.append(it.next().captured(1));
        }
        return coords;
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
        // 保留用户写出来的目标和坐标，没写才用 @p 兜底
        QStringList args;
        const QString target = extractSelector(input);
        args << (target.isEmpty() ? QStringLiteral("@p") : target);
        const QStringList coords = extractCoordinates(input);
        const int coordCount = qMin(3, static_cast<int>(coords.size()));
        for (int i = 0; i < coordCount; ++i)
        {
            args << coords.at(i);
        }
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
            const QString target = extractSelector(input);
            if (!target.isEmpty())
            {
                args << target;
            }
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
