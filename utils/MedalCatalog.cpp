#include "MedalCatalog.h"

#include "IconHelper.h"

#include <QHash>
#include <QIcon>
#include <QPixmap>

namespace {

struct Entry
{
    const char *id;
    const char *icon;
    const char *accent;
};

// 顺序与服务端 lib/medals.js 的展示顺序一致；图标本身带配色，
// accent 只用在进度条和稀有度标签上。
const Entry kEntries[] = {
    {"newcomer", ":/Images/Icons/medals/medal_newcomer.svg", "#10B981"},
    {"quiz", ":/Images/Icons/medals/medal_quiz.svg", "#6366F1"},
    {"first_thread", ":/Images/Icons/medals/medal_first_thread.svg", "#3B82F6"},
    {"helper", ":/Images/Icons/medals/medal_helper.svg", "#E11D48"},
    {"checkin7", ":/Images/Icons/medals/medal_checkin7.svg", "#F97316"},
    {"creator", ":/Images/Icons/medals/medal_creator.svg", "#8B5CF6"},
    {"rich", ":/Images/Icons/medals/medal_rich.svg", "#D97706"},
    {"popular", ":/Images/Icons/medals/medal_popular.svg", "#DB2777"},
    {"bug_hunter", ":/Images/Icons/medals/medal_bug_hunter.svg", "#65A30D"},
    {"veteran", ":/Images/Icons/medals/medal_veteran.svg", "#0F766E"},
    {"quiz100", ":/Images/Icons/medals/medal_quiz100.svg", "#4338CA"},
    {"fixer10", ":/Images/Icons/medals/medal_fixer10.svg", "#0D9488"},
    {"boards8", ":/Images/Icons/medals/medal_boards8.svg", "#0284C7"},
    {"streak30", ":/Images/Icons/medals/medal_streak30.svg", "#DC2626"},
    {"creator5", ":/Images/Icons/medals/medal_creator5.svg", "#9333EA"},
    {"followers100", ":/Images/Icons/medals/medal_followers100.svg", "#BE123C"},
    {"answers10", ":/Images/Icons/medals/medal_answers10.svg", "#0891B2"},
    {"likes500", ":/Images/Icons/medals/medal_likes500.svg", "#B91C1C"},
    {"downloads10000", ":/Images/Icons/medals/medal_downloads10000.svg", "#1D4ED8"},
    {"rich10000", ":/Images/Icons/medals/medal_rich10000.svg", "#B45309"},
};

const Entry &fallbackEntry()
{
    return kEntries[0];
}

} // namespace

namespace MedalCatalog {

Look lookFor(const QString &medalId)
{
    for (const Entry &entry : kEntries)
    {
        if (medalId == QLatin1String(entry.id))
            return {QString::fromLatin1(entry.icon), QColor(QString::fromLatin1(entry.accent))};
    }
    return {QString::fromLatin1(fallbackEntry().icon),
            QColor(QString::fromLatin1(fallbackEntry().accent))};
}

QPixmap pixmapFor(const QString &medalId, int size)
{
    const Look look = lookFor(medalId);
    return IconHelper::loadColoredIcon(look.iconPath, look.accent, size).pixmap(size, size);
}

QString gameIconPath(const QString &advancementId)
{
    // 原版 advancement 形如 minecraft:story/mine_stone，取分类前缀选图标；
    // 模组 / 数据包的分类各式各样，统一用通用奖杯图标兜底。
    static const QHash<QString, QString> kCategories = {
        {QStringLiteral("story"), QStringLiteral(":/Images/Icons/medals/game/game_story.svg")},
        {QStringLiteral("nether"), QStringLiteral(":/Images/Icons/medals/game/game_nether.svg")},
        {QStringLiteral("end"), QStringLiteral(":/Images/Icons/medals/game/game_end.svg")},
        {QStringLiteral("adventure"),
         QStringLiteral(":/Images/Icons/medals/game/game_adventure.svg")},
        {QStringLiteral("husbandry"),
         QStringLiteral(":/Images/Icons/medals/game/game_husbandry.svg")},
    };

    const int colon = advancementId.indexOf(QLatin1Char(':'));
    if (colon > 0 && advancementId.left(colon) == QLatin1String("minecraft"))
    {
        const QString path = advancementId.mid(colon + 1);
        const int slash = path.indexOf(QLatin1Char('/'));
        const QString category = slash > 0 ? path.left(slash) : path;
        const auto it = kCategories.constFind(category);
        if (it != kCategories.constEnd())
            return it.value();
    }
    return QStringLiteral(":/Images/Icons/medals/game/game_generic.svg");
}

QPixmap gamePixmap(const QString &advancementId, int size)
{
    return IconHelper::loadColoredIcon(gameIconPath(advancementId), QColor(0x0F, 0x76, 0x6E), size)
        .pixmap(size, size);
}

QString rarityText(const QString &rarity)
{
    if (rarity == QLatin1String("rare"))
        return QStringLiteral("稀有");
    if (rarity == QLatin1String("epic"))
        return QStringLiteral("史诗");
    if (rarity == QLatin1String("legendary"))
        return QStringLiteral("传说");
    if (rarity == QLatin1String("mythic"))
        return QStringLiteral("神话");
    if (rarity == QLatin1String("game"))
        return QStringLiteral("游戏成就");
    return QStringLiteral("普通");
}

QColor rarityColor(const QString &rarity)
{
    if (rarity == QLatin1String("rare"))
        return QColor(0x25, 0x63, 0xEB);
    if (rarity == QLatin1String("epic"))
        return QColor(0x7C, 0x3A, 0xED);
    if (rarity == QLatin1String("legendary"))
        return QColor(0xD9, 0x77, 0x06);
    if (rarity == QLatin1String("mythic"))
        return QColor(0xDC, 0x26, 0x26);
    if (rarity == QLatin1String("game"))
        return QColor(0x08, 0x91, 0xB2);
    return QColor(0x16, 0xA3, 0x4A);
}

} // namespace MedalCatalog
