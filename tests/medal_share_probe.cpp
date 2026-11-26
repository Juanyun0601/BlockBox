/**
 * @file   medal_share_probe.cpp
 * @brief  勋章分享图渲染探针：独立编译 MedalShareCard，实际出 5 张 PNG 检查版式
 *
 * 只读 utils/ 下的渲染器，不修改它。跑完在 exe 同级的 probe_out/ 下留图，
 * 每张打印一行「[OK] 路径 宽x高」。
 */

#include "MedalShareCard.h"

#include "MedalCatalog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTime>

namespace {

constexpr int kAvatarSize = 256;

/** 现画一张彩色方块头像，模拟用户从图库选的真实头像（走裁剪 + 缩放位图路径） */
QPixmap makeBlockAvatar()
{
    QPixmap avatar(kAvatarSize, kAvatarSize);
    avatar.fill(Qt::transparent);

    QPainter painter(&avatar);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient sky(0, 0, 0, kAvatarSize);
    sky.setColorAt(0.0, QColor(0x3B, 0x82, 0xF6));
    sky.setColorAt(0.5, QColor(0x8B, 0x5C, 0xF6));
    sky.setColorAt(1.0, QColor(0xF9, 0x73, 0x16));
    painter.fillRect(QRect(0, 0, kAvatarSize, kAvatarSize), sky);

    // 像 MC 皮肤一样的色块拼接，避免纯渐变看着像占位图
    const QColor palette[] = {
        QColor(0x2E, 0x7D, 0x32), QColor(0x48, 0xC9, 0x7E), QColor(0xF5, 0xB0, 0x3A),
        QColor(0xE1, 0x1D, 0x48), QColor(0x3B, 0x82, 0xF6), QColor(0xF2, 0xFA, 0xF5),
        QColor(0x65, 0xA3, 0x0D), QColor(0xDB, 0x27, 0x77), QColor(0x10, 0xB9, 0x81),
    };
    const int paletteCount = int(sizeof(palette) / sizeof(palette[0]));
    QRandomGenerator rng(0x0B10C5);
    for (int y = 0; y < kAvatarSize; y += 32)
    {
        for (int x = 0; x < kAvatarSize; x += 32)
        {
            if (rng.bounded(100) < 38)
                continue;
            QColor color = palette[rng.bounded(paletteCount)];
            color.setAlpha(170 + rng.bounded(86));
            painter.fillRect(QRect(x + 2, y + 2, 28, 28), color);
        }
    }

    // 一张方块脸，保证缩到 132px 圆里还能看出是「头像」
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xF2, 0xFA, 0xF5));
    painter.drawRect(QRect(58, 92, 46, 46));
    painter.drawRect(QRect(152, 92, 46, 46));
    painter.setBrush(QColor(0x11, 0x2A, 0x1E));
    painter.drawRect(QRect(70, 104, 22, 22));
    painter.drawRect(QRect(164, 104, 22, 22));
    painter.setBrush(QColor(0x0C, 0x3A, 0x2A));
    painter.drawRect(QRect(82, 178, 92, 14));
    painter.drawRect(QRect(70, 164, 12, 14));
    painter.drawRect(QRect(174, 164, 12, 14));

    painter.end();
    return avatar;
}

MedalShareCard::Item forumItem(const QString &iconId, const QString &name,
                               const QString &rarity)
{
    MedalShareCard::Item item;
    item.iconId = iconId;
    item.name = name;
    item.rarity = rarity;
    item.game = false;
    return item;
}

MedalShareCard::Item gameItem(const QString &advancementId, const QString &name)
{
    MedalShareCard::Item item;
    item.iconId = advancementId;
    item.name = name;
    item.rarity = QStringLiteral("game");
    item.game = true;
    return item;
}

/** 01/02 共用的 12 枚：common / rare / epic / legendary / mythic 全覆盖 + 2 条游戏成就 */
QVector<MedalShareCard::Item> typicalItems()
{
    QVector<MedalShareCard::Item> items;
    items << forumItem(QStringLiteral("newcomer"), QStringLiteral("新手上路"), QStringLiteral("common"));
    items << forumItem(QStringLiteral("quiz"), QStringLiteral("答题小能手"), QStringLiteral("common"));
    items << forumItem(QStringLiteral("first_thread"), QStringLiteral("首次发声"), QStringLiteral("common"));
    items << forumItem(QStringLiteral("helper"), QStringLiteral("热心互助"), QStringLiteral("rare"));
    items << forumItem(QStringLiteral("checkin7"), QStringLiteral("连续签到 7 天"), QStringLiteral("rare"));
    items << forumItem(QStringLiteral("creator"), QStringLiteral("创作之星"), QStringLiteral("epic"));
    items << forumItem(QStringLiteral("rich"), QStringLiteral("社区富豪"), QStringLiteral("epic"));
    items << forumItem(QStringLiteral("popular"), QStringLiteral("人气王"), QStringLiteral("legendary"));
    items << forumItem(QStringLiteral("bug_hunter"), QStringLiteral("猎虫专家"), QStringLiteral("legendary"));
    items << forumItem(QStringLiteral("veteran"), QStringLiteral("资深元老"), QStringLiteral("mythic"));
    items << gameItem(QStringLiteral("minecraft:story/mine_stone"), QStringLiteral("石器时代"));
    items << gameItem(QStringLiteral("minecraft:nether/obtain_blaze_rod"), QStringLiteral("与火共舞"));
    return items;
}

void printLine(const QString &text)
{
    QTextStream out(stdout);
    out << text << Qt::endl;
}

bool saveOne(const QString &fileName, const MedalShareCard::Data &data)
{
    const QImage poster = MedalShareCard::buildPoster(data);
    if (poster.isNull())
    {
        printLine(QStringLiteral("[FAIL] %1 渲染返回空图").arg(fileName));
        return false;
    }

    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/probe_out");
    if (!QDir().mkpath(dir))
    {
        printLine(QStringLiteral("[FAIL] %1 无法创建目录 %2").arg(fileName, dir));
        return false;
    }

    const QString path = QDir(dir).filePath(fileName);
    if (!poster.save(path, "PNG"))
    {
        printLine(QStringLiteral("[FAIL] %1 保存失败 %2").arg(fileName, path));
        return false;
    }

    printLine(QStringLiteral("[OK] %1 %2x%3")
                  .arg(QDir::toNativeSeparators(path))
                  .arg(poster.width())
                  .arg(poster.height()));
    return true;
}

/**
 * 资源自检：确认 20 枚论坛勋章 + 6 个游戏分类图标 + logo 都真的打进了 qrc。
 * pixmapFor 对未知 id 会兜底，所以这里直接查文件是否存在，避免探针漏收图还出「像样」的图。
 */
int checkResources()
{
    const char *medalIds[] = {
        "newcomer", "quiz",      "first_thread",     "helper",        "checkin7",
        "creator",  "rich",      "popular",          "bug_hunter",    "veteran",
        "quiz100",  "fixer10",   "boards8",          "streak30",      "creator5",
        "followers100", "answers10", "likes500",     "downloads10000", "rich10000",
    };
    const char *advIds[] = {
        "minecraft:story/mine_stone",   "minecraft:nether/obtain_blaze_rod",
        "minecraft:end/kill_dragon",    "minecraft:adventure/kill_a_mob",
        "minecraft:husbandry/breed_an_animal", "create:mechanical_press",
    };

    QStringList missing;
    for (const char *id : medalIds)
    {
        const QString path = MedalCatalog::lookFor(QString::fromLatin1(id)).iconPath;
        if (!QFile::exists(path))
            missing << path;
    }
    for (const char *id : advIds)
    {
        const QString path = MedalCatalog::gameIconPath(QString::fromUtf8(id));
        if (!QFile::exists(path))
            missing << path;
    }
    if (!QFile::exists(QStringLiteral(":/Images/logo.png")))
        missing << QStringLiteral(":/Images/logo.png");
    if (!QFile::exists(QStringLiteral(":/Images/blockbox_icon_dark.png")))
        missing << QStringLiteral(":/Images/blockbox_icon_dark.png");

    if (missing.isEmpty())
    {
        printLine(QStringLiteral("[OK] 资源自检 34/34 全部命中（20 论坛勋章 + 6 游戏分类 + 2 品牌图）"));
        return 0;
    }
    printLine(QStringLiteral("[WARN] 资源缺失 %1 项: %2")
                  .arg(missing.size())
                  .arg(missing.join(QStringLiteral(", "))));
    return missing.size();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("medal_share_probe"));

    const QPixmap avatar = makeBlockAvatar();
    const qint64 joined2023 =
        QDateTime(QDate(2023, 5, 14), QTime(9, 30, 0)).toSecsSinceEpoch();

    const int missingResources = checkResources();
    int failures = 0;

    // ── 01 典型浅色主题：真实头像 + 12 枚 + 全稀有度 + 2 条游戏成就 ──
    {
        MedalShareCard::Data data;
        data.brand = QColor(0x2e, 0x7d, 0x32);
        data.avatar = avatar;
        data.displayName = QStringLiteral("矿工小明");
        data.username = QStringLiteral("miner_xm");
        data.joinedAt = joined2023;
        data.earned = 12;
        data.total = 25; // 论坛 20 + 已获得成就 5
        data.forumEarned = 7;
        data.forumTotal = 20;
        data.gameEarned = 5;
        data.items = typicalItems();
        data.next = forumItem(QStringLiteral("quiz100"), QStringLiteral("百题斩"),
                              QStringLiteral("epic"));
        data.nextProgress = QStringLiteral("答题满分 · 40 / 100 题");
        data.siteUrl = QStringLiteral("https://mcblockbox.top");
        failures += saveOne(QStringLiteral("01_typical_light.png"), data) ? 0 : 1;
    }

    // ── 02 典型深色主题：同样内容，brand 换成亮一档的绿（走 accentFor 另一条分支）──
    {
        MedalShareCard::Data data;
        data.brand = QColor(0x66, 0xbb, 0x6a);
        data.avatar = avatar;
        data.displayName = QStringLiteral("矿工小明");
        data.username = QStringLiteral("miner_xm");
        data.joinedAt = joined2023;
        data.earned = 12;
        data.total = 25;
        data.forumEarned = 7;
        data.forumTotal = 20;
        data.gameEarned = 5;
        data.items = typicalItems();
        data.siteUrl = QStringLiteral("https://mcblockbox.top");
        failures += saveOne(QStringLiteral("02_typical_dark.png"), data) ? 0 : 1;
    }

    // ── 03 空态：0 枚勋章、无头像、无昵称（走默认昵称 + 字母头像兜底）──
    {
        MedalShareCard::Data data;
        data.brand = QColor(0x2e, 0x7d, 0x32);
        data.earned = 0;
        data.total = 20;
        data.forumEarned = 0;
        data.forumTotal = 20;
        data.gameEarned = 0;
        data.siteUrl = QStringLiteral("https://mcblockbox.top");
        failures += saveOne(QStringLiteral("03_empty.png"), data) ? 0 : 1;
    }

    // ── 04 满墙：20 枚论坛勋章全齐 + 10 条游戏成就，验证最后一格「+N」截断 ──
    {
        MedalShareCard::Data data;
        data.brand = QColor(0x2e, 0x7d, 0x32);
        data.avatar = avatar;
        data.displayName = QStringLiteral("全勋章收集者");
        data.username = QStringLiteral("all_medals");
        data.joinedAt = joined2023;
        data.earned = 30;
        data.total = 30;
        data.forumEarned = 20;
        data.forumTotal = 20;
        data.gameEarned = 10;
        // 20 枚论坛勋章全齐，顺序与服务端展示顺序一致
        const char *allForum[][3] = {
            {"newcomer", "新手上路", "common"},
            {"quiz", "答题小能手", "common"},
            {"first_thread", "首次发声", "common"},
            {"helper", "热心互助", "rare"},
            {"checkin7", "连续签到 7 天", "rare"},
            {"creator", "创作之星", "epic"},
            {"rich", "社区富豪", "epic"},
            {"popular", "人气王", "legendary"},
            {"bug_hunter", "猎虫专家", "legendary"},
            {"veteran", "资深元老", "mythic"},
            {"quiz100", "百题斩", "epic"},
            {"fixer10", "修复达人", "rare"},
            {"boards8", "板块常客", "rare"},
            {"streak30", "签到 30 天", "epic"},
            {"creator5", "创作五连", "legendary"},
            {"followers100", "百人关注", "rare"},
            {"answers10", "答疑十次", "epic"},
            {"likes500", "五百赞", "legendary"},
            {"downloads10000", "万次下载", "mythic"},
            {"rich10000", "万元家底", "mythic"},
        };
        for (int i = 0; i < 20; ++i)
            data.items << forumItem(QString::fromLatin1(allForum[i][0]),
                                     QString::fromUtf8(allForum[i][1]),
                                     QString::fromLatin1(allForum[i][2]));
        data.items << gameItem(QStringLiteral("minecraft:story/mine_stone"), QStringLiteral("石器时代"));
        data.items << gameItem(QStringLiteral("minecraft:story/enchant_item"), QStringLiteral("附魔师"));
        data.items << gameItem(QStringLiteral("minecraft:nether/obtain_blaze_rod"), QStringLiteral("与火共舞"));
        data.items << gameItem(QStringLiteral("minecraft:nether/fast_travel"), QStringLiteral("下界快旅"));
        data.items << gameItem(QStringLiteral("minecraft:end/enter_end"), QStringLiteral("进入末地"));
        data.items << gameItem(QStringLiteral("minecraft:end/kill_dragon"), QStringLiteral("屠龙者"));
        data.items << gameItem(QStringLiteral("minecraft:adventure/kill_a_mob"), QStringLiteral("怪物猎人"));
        data.items << gameItem(QStringLiteral("minecraft:adventure/trade"), QStringLiteral("以物易物"));
        data.items << gameItem(QStringLiteral("minecraft:husbandry/breed_an_animal"), QStringLiteral("繁育师"));
        data.items << gameItem(QStringLiteral("create:mechanical_press"), QStringLiteral("机械师"));
        data.siteUrl = QStringLiteral("https://mcblockbox.top");
        failures += saveOne(QStringLiteral("04_many.png"), data) ? 0 : 1;
    }

    // ── 05 超长昵称：60 个汉字、无账号名、注册时间 0、2 枚勋章 ──
    {
        const QString base = QStringLiteral("超级无敌方块世界挖矿打工仔社区元老级人物"); // 20 字
        MedalShareCard::Data data;
        data.brand = QColor(0x2e, 0x7d, 0x32);
        data.displayName = (base + base + base).left(60);
        data.joinedAt = 0;
        data.earned = 2;
        data.total = 20;
        data.forumEarned = 1;
        data.forumTotal = 20;
        data.gameEarned = 1;
        data.items << forumItem(QStringLiteral("newcomer"), QStringLiteral("新手上路"), QStringLiteral("common"));
        data.items << gameItem(QStringLiteral("minecraft:story/mine_stone"), QStringLiteral("石器时代"));
        data.next = forumItem(QStringLiteral("quiz"), QStringLiteral("答题小能手"),
                              QStringLiteral("common"));
        data.nextProgress = QStringLiteral("通过入站答题 · 0 / 1");
        data.siteUrl = QStringLiteral("https://mcblockbox.top");
        failures += saveOne(QStringLiteral("05_longname.png"), data) ? 0 : 1;
    }

    printLine(QStringLiteral("[DONE] %1 组，失败 %2 组，输出目录 %3")
                  .arg(5)
                  .arg(failures)
                  .arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()
                                               + QStringLiteral("/probe_out"))));
    return (failures == 0 && missingResources == 0) ? 0 : 1;
}
