/**
 * @file   SaveSettingsPage.cpp
 * @brief  存档设置页面实现
 * @author BlockBox Team
 * @date   2026-08-01
 */

#include "SaveSettingsPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include "components/AppMessageBox.h"
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

namespace {

QString gameModeName(int gameType)
{
    switch (gameType)
    {
    case 0: return QObject::tr("生存模式");
    case 1: return QObject::tr("创造模式");
    case 2: return QObject::tr("冒险模式");
    case 3: return QObject::tr("观察者模式");
    default: return QObject::tr("未知");
    }
}

QString formatTicksAsDuration(qint64 ticks)
{
    // 20 游戏刻 = 1 秒
    qint64 totalSeconds = ticks / 20;
    qint64 days = totalSeconds / 86400;
    qint64 hours = (totalSeconds % 86400) / 3600;
    qint64 minutes = (totalSeconds % 3600) / 60;
    if (days > 0)
        return QObject::tr("%1天 %2小时 %3分钟").arg(days).arg(hours).arg(minutes);
    if (hours > 0)
        return QObject::tr("%1小时 %2分钟").arg(hours).arg(minutes);
    return QObject::tr("%1分钟").arg(minutes);
}

// 将 DayTime 游戏刻转为可读的「小时:分钟」表示（0 刻 = 6:00）
QString formatDayTimeClock(qint64 ticks)
{
    const qint64 t = ((ticks % 24000) + 24000) % 24000;
    qint64 hour = (t / 1000 + 6) % 24;
    qint64 minute = ((t % 1000) * 60) / 1000;
    return QStringLiteral("%1:%2")
        .arg(hour, 2, 10, QLatin1Char('0'))
        .arg(minute, 2, 10, QLatin1Char('0'));
}

// ============================================================================
// 游戏规则定义表（level.dat 中 Data.GameRules，值均为字符串）
// since 非空时表示该规则需要的最低游戏版本，旧版本游戏会忽略此规则
// ============================================================================
struct GameRuleDef
{
    const char *id;           ///< 规则 ID
    const char *zh;           ///< 中文名
    const char *group;        ///< 分组名
    const char *defaultValue; ///< 游戏默认值
    bool numeric;             ///< 是否为数值规则
    int minValue;             ///< 数值规则下限
    int maxValue;             ///< 数值规则上限
    const char *since;        ///< 需要的最低游戏版本（空表示不作说明）
    const char *desc;         ///< 说明（悬浮提示）
};

const GameRuleDef kGameRules[] = {
    // 玩家
    { "keepInventory",              QT_TRANSLATE_NOOP("SaveSettingsPage", "死亡不掉落"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "false", false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "死亡后保留物品栏和经验，不会掉落") },
    { "doImmediateRespawn",         QT_TRANSLATE_NOOP("SaveSettingsPage", "立即重生"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "false", false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "死亡后跳过死亡界面直接重生") },
    { "naturalRegeneration",        QT_TRANSLATE_NOOP("SaveSettingsPage", "自然回血"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "生命值会随时间自动恢复") },
    { "fallDamage",                 QT_TRANSLATE_NOOP("SaveSettingsPage", "摔落伤害"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "从高处摔落会造成伤害") },
    { "fireDamage",                 QT_TRANSLATE_NOOP("SaveSettingsPage", "火焰伤害"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "火焰和岩浆会造成伤害") },
    { "drowningDamage",             QT_TRANSLATE_NOOP("SaveSettingsPage", "溺水伤害"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "水下憋气耗尽后会造成伤害") },
    { "freezingDamage",             QT_TRANSLATE_NOOP("SaveSettingsPage", "冰冻伤害"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "true",  false, 0, 0, "1.17",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "在细雪中冰冻会造成伤害") },
    { "locatorBar",                 QT_TRANSLATE_NOOP("SaveSettingsPage", "队友定位条"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "true",  false, 0, 0, "1.21.6",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "屏幕上显示队友位置的定位条（联机用）") },
    { "enderPearlsVanishOnDeath",   QT_TRANSLATE_NOOP("SaveSettingsPage", "死亡珍珠消失"), QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "true",  false, 0, 0, "1.20.2",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家死亡时，正在飞行的末影珍珠立即消失") },
    { "spectatorsGenerateChunks",   QT_TRANSLATE_NOOP("SaveSettingsPage", "旁观者加载区块"),QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"),"true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "旁观者玩家也会促使周围区块加载生成") },
    { "playersSleepingPercentage",  QT_TRANSLATE_NOOP("SaveSettingsPage", "跳夜所需玩家"), QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "100",   true,  0, 100, "1.17",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "需要入睡的玩家比例（%）达到该数值才能跳过夜晚，0 表示无需有人入睡") },
    { "spawnRadius",                QT_TRANSLATE_NOOP("SaveSettingsPage", "出生散布半径"), QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家"), "10",    true,  0, 1024, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "新玩家和重生玩家围绕世界出生点散布的范围（格），0 表示固定出生在世界出生点") },
    // 世界
    { "doDaylightCycle",            QT_TRANSLATE_NOOP("SaveSettingsPage", "昼夜更替"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "太阳、月亮和昼夜照常流转") },
    { "doWeatherCycle",             QT_TRANSLATE_NOOP("SaveSettingsPage", "天气更替"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "晴天、雨天、雷雨会自然变化") },
    { "doFireTick",                 QT_TRANSLATE_NOOP("SaveSettingsPage", "火焰蔓延"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "火焰会蔓延、点燃方块并自然熄灭") },
    { "allowFireTicksAwayFromPlayer",QT_TRANSLATE_NOOP("SaveSettingsPage", "火焰远离燃烧"),QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "false", false, 0, 0, "1.21.5",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "允许火焰蔓延到离玩家很远的地方继续燃烧（25w44a 起已移除）") },
    { "mobGriefing",                QT_TRANSLATE_NOOP("SaveSettingsPage", "生物破坏方块"), QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "苦力怕爆炸毁地形、末影人搬方块、僵尸踩坏农田等行为") },
    { "doTileDrops",                QT_TRANSLATE_NOOP("SaveSettingsPage", "方块掉落"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "破坏方块后会掉落物品") },
    { "doEntityDrops",              QT_TRANSLATE_NOOP("SaveSettingsPage", "实体掉落"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "矿车、船、物品展示框等被破坏后掉落物品") },
    { "doVinesSpread",              QT_TRANSLATE_NOOP("SaveSettingsPage", "藤蔓蔓延"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "藤蔓会向四周生长蔓延") },
    { "projectilesCanBreakBlocks",  QT_TRANSLATE_NOOP("SaveSettingsPage", "投射物破坏植物"),QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"),"true",  false, 0, 0, "1.20.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "箭等投射物可以打碎紫水晶簇、滴水石锥等方块") },
    { "waterSourceConversion",      QT_TRANSLATE_NOOP("SaveSettingsPage", "水源汇聚"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "1.19.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "流水会互相汇聚成新水源，可以做无限水") },
    { "lavaSourceConversion",       QT_TRANSLATE_NOOP("SaveSettingsPage", "熔岩源汇聚"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "false", false, 0, 0, "1.19.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "熔岩会互相汇聚成新熔岩源（可做无限熔岩）") },
    { "snowAccumulationHeight",     QT_TRANSLATE_NOOP("SaveSettingsPage", "降雪堆积上限"), QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "1",     true,  0, 8, "1.19.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "下雪时地表最多能积几层雪，0 表示完全不积雪") },
    { "blockExplosionDropDecay",    QT_TRANSLATE_NOOP("SaveSettingsPage", "方块爆炸掉落衰减"),QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"),"true", false, 0, 0, "1.19.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "下界床等方块类爆炸炸毁方块后，一部分掉落物会消失") },
    { "mobExplosionDropDecay",      QT_TRANSLATE_NOOP("SaveSettingsPage", "生物爆炸掉落衰减"),QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"),"true", false, 0, 0, "1.19.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "苦力怕等生物爆炸炸毁方块后，一部分掉落物会消失") },
    { "tntExplosionDropDecay",      QT_TRANSLATE_NOOP("SaveSettingsPage", "TNT爆炸掉落衰减"),QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"),"false", false, 0, 0, "1.19.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "TNT 爆炸炸毁方块后，一部分掉落物会消失（默认全部掉落）") },
    { "tntExplodes",                QT_TRANSLATE_NOOP("SaveSettingsPage", "TNT爆炸"),      QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "true",  false, 0, 0, "1.21.5",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "点燃的 TNT 能否正常爆炸，关闭后 TNT 和 TNT 矿车都无法引爆") },
    { "spawnChunkRadius",           QT_TRANSLATE_NOOP("SaveSettingsPage", "出生点常加载"), QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "2",     true,  0, 32, "1.20.5",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "世界出生点周围始终保持加载的区块半径，0 表示不再常加载出生点区块（1.21.9 起已移除）") },
    { "randomTickSpeed",            QT_TRANSLATE_NOOP("SaveSettingsPage", "随机刻速度"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "世界"), "3",     true,  0, 4096, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "作物生长、树叶枯萎等随机事件的速度，0 为停止生长") },
    // 生物
    { "doMobSpawning",              QT_TRANSLATE_NOOP("SaveSettingsPage", "生物自然生成"), QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "动物和怪物会自然生成") },
    { "doMobLoot",                  QT_TRANSLATE_NOOP("SaveSettingsPage", "生物掉落物"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "生物死亡后掉落战利品") },
    { "doInsomnia",                 QT_TRANSLATE_NOOP("SaveSettingsPage", "幻翼生成"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家长时间不睡觉会生成幻翼") },
    { "doPatrolSpawning",           QT_TRANSLATE_NOOP("SaveSettingsPage", "巡逻队生成"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "灾厄巡逻队会生成") },
    { "doTraderSpawning",           QT_TRANSLATE_NOOP("SaveSettingsPage", "流浪商人生成"), QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "流浪商人和羊驼会生成") },
    { "doWardenSpawning",           QT_TRANSLATE_NOOP("SaveSettingsPage", "监守者生成"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "深暗之域中会生成监守者") },
    { "disableRaids",               QT_TRANSLATE_NOOP("SaveSettingsPage", "禁止袭击"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "false", false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "带着不祥之兆进入村庄不再触发袭击") },
    { "forgiveDeadPlayers",         QT_TRANSLATE_NOOP("SaveSettingsPage", "愤怒平息"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "被激怒的中立生物在目标玩家死亡后平息怒火") },
    { "universalAnger",             QT_TRANSLATE_NOOP("SaveSettingsPage", "迁怒所有人"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "false", false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "被激怒的中立生物会攻击所有玩家") },
    { "maxEntityCramming",          QT_TRANSLATE_NOOP("SaveSettingsPage", "实体挤压上限"), QT_TRANSLATE_NOOP("SaveSettingsPage", "生物"), "24",    true,  0, 1024, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "同一格内实体超过该数量时会受到挤压伤害") },
    // 交通与传送
    { "minecartMaxSpeed",           QT_TRANSLATE_NOOP("SaveSettingsPage", "矿车最大速度"), QT_TRANSLATE_NOOP("SaveSettingsPage", "交通与传送"), "8", true, 1, 1000, "1.21.2",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "普通矿车的最高速度（格/游戏刻）") },
    { "playersNetherPortalDefaultDelay", QT_TRANSLATE_NOOP("SaveSettingsPage", "传送门等待时间"), QT_TRANSLATE_NOOP("SaveSettingsPage", "交通与传送"), "80", true, 1, 10000, "1.20.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家（非创造）站在下界传送门里需要等待的时长（游戏刻，默认 80 即 4 秒）") },
    { "playersNetherPortalCreativeDelay",QT_TRANSLATE_NOOP("SaveSettingsPage", "创造传送门等待"), QT_TRANSLATE_NOOP("SaveSettingsPage", "交通与传送"), "0",  true, 0, 10000, "1.20.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "创造模式玩家在下界传送门里需要等待的时长（游戏刻），0 为即进即传") },
    // 界面与命令
    { "announceAdvancements",       QT_TRANSLATE_NOOP("SaveSettingsPage", "公告进度"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "解锁进度、达成挑战时在聊天栏公告") },
    { "showDeathMessages",          QT_TRANSLATE_NOOP("SaveSettingsPage", "死亡消息"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "玩家死亡时在聊天栏显示消息") },
    { "reducedDebugInfo",           QT_TRANSLATE_NOOP("SaveSettingsPage", "简化调试屏"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "false", false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "F3 调试界面隐藏坐标等详细信息") },
    { "doLimitedCrafting",          QT_TRANSLATE_NOOP("SaveSettingsPage", "限定合成"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "false", false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "只能合成已解锁配方的物品") },
    { "commandBlockOutput",         QT_TRANSLATE_NOOP("SaveSettingsPage", "命令方块播报"), QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "命令方块的执行结果播报到聊天栏") },
    { "sendCommandFeedback",        QT_TRANSLATE_NOOP("SaveSettingsPage", "命令反馈"),     QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "执行命令后在聊天栏显示反馈") },
    { "logAdminCommands",           QT_TRANSLATE_NOOP("SaveSettingsPage", "日志记录命令"), QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "true",  false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "把管理员命令写入日志文件") },
    { "disableElytraMovementCheck", QT_TRANSLATE_NOOP("SaveSettingsPage", "关闭鞘翅测速"), QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "false", false, 0, 0, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "服务器不再检测鞘翅飞行是否超速") },
    { "globalSoundEvents",          QT_TRANSLATE_NOOP("SaveSettingsPage", "全服播报音效"), QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "true",  false, 0, 0, "1.19.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "凋灵召唤、末影龙死亡、末地传送门激活等音效全服都能听到，关闭后只有附近玩家能听到") },
    { "commandModificationBlockLimit",QT_TRANSLATE_NOOP("SaveSettingsPage", "命令改方块上限"),QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"),"32768", true, 1, 2147483647, "1.19.4",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "/fill、/clone 等命令一次最多能改动的方块数") },
    { "maxCommandForkCount",        QT_TRANSLATE_NOOP("SaveSettingsPage", "命令分叉数"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "65536", true, 1, 2147483647, "1.20.3",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "一条命令一次最多能同时执行多少个分支") },
    { "maxCommandChainLength",      QT_TRANSLATE_NOOP("SaveSettingsPage", "命令链长度"),   QT_TRANSLATE_NOOP("SaveSettingsPage", "界面与命令"), "65536", true, 1, 65536, "",
      QT_TRANSLATE_NOOP("SaveSettingsPage", "一次命令方块链最多可执行的命令数") },
};

} // namespace

SaveSettingsPage::SaveSettingsPage(QWidget *parent)
    : QWidget(parent)
    , m_saveNameLabel(nullptr)
    , m_infoGrid(nullptr)
    , m_versionLabel(nullptr)
    , m_seedLabel(nullptr)
    , m_lastPlayedLabel(nullptr)
    , m_spawnLabel(nullptr)
    , m_gameModeLabel(nullptr)
    , m_playTimeLabel(nullptr)
    , m_dayTimeLabel(nullptr)
    , m_worldNameEdit(nullptr)
    , m_gameTypeCombo(nullptr)
    , m_allowCommandsCombo(nullptr)
    , m_difficultyCombo(nullptr)
    , m_lockDifficultyCheck(nullptr)
    , m_hardcoreCheck(nullptr)
    , m_dayTimeCombo(nullptr)
    , m_spawnXSpin(nullptr)
    , m_spawnYSpin(nullptr)
    , m_spawnZSpin(nullptr)
    , m_hardcoreHint(nullptr)
    , m_copySeedBtn(nullptr)
    , m_chunkbaseBtn(nullptr)
    , m_minecraftSearchBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_openFolderBtn(nullptr)
    , m_backupBtn(nullptr)
{
    initUI();
}

SaveSettingsPage::~SaveSettingsPage()
{
}

void SaveSettingsPage::initUI()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *contentWidget = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(20, 12, 20, 20);
    layout->setSpacing(12);

    // 存档名称
    m_saveNameLabel = new QLabel(m_saveName, contentWidget);
    QFont nameFont = m_saveNameLabel->font();
    nameFont.setBold(true);
    nameFont.setPixelSize(17);
    m_saveNameLabel->setFont(nameFont);
    layout->addWidget(m_saveNameLabel);

    // ── 存档信息卡片 ──
    QWidget *infoCard = new QWidget(contentWidget);
    infoCard->setObjectName("funcCard");
    auto *infoCardLayout = new QVBoxLayout(infoCard);
    infoCardLayout->setContentsMargins(16, 16, 16, 16);
    infoCardLayout->setSpacing(10);

    auto *infoHeader = new QLabel(tr("存档信息"), infoCard);
    QFont headerFont = infoHeader->font();
    headerFont.setBold(true);
    headerFont.setPixelSize(13);
    infoHeader->setFont(headerFont);
    infoCardLayout->addWidget(infoHeader);

    m_infoGrid = new QGridLayout();
    m_infoGrid->setHorizontalSpacing(16);
    m_infoGrid->setVerticalSpacing(8);
    m_infoGrid->setColumnStretch(1, 1);
    infoCardLayout->addLayout(m_infoGrid);

    m_versionLabel = new QLabel(infoCard);
    m_versionLabel->setObjectName("saveInfoValue");

    m_seedLabel = new QLabel(infoCard);
    m_seedLabel->setObjectName("saveInfoValue");
    m_seedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_lastPlayedLabel = new QLabel(infoCard);
    m_lastPlayedLabel->setObjectName("saveInfoValue");

    m_spawnLabel = new QLabel(infoCard);
    m_spawnLabel->setObjectName("saveInfoValue");

    m_gameModeLabel = new QLabel(infoCard);
    m_gameModeLabel->setObjectName("saveInfoValue");

    m_playTimeLabel = new QLabel(infoCard);
    m_playTimeLabel->setObjectName("saveInfoValue");

    m_dayTimeLabel = new QLabel(infoCard);
    m_dayTimeLabel->setObjectName("saveInfoValue");

    // 种子行：数值 + 复制 + Chunkbase
    auto *seedRow = new QHBoxLayout();
    seedRow->setSpacing(6);
    seedRow->addWidget(m_seedLabel, 1);
    m_copySeedBtn = new QPushButton(tr("复制"), infoCard);
    m_copySeedBtn->setObjectName("bottomActionBtn");
    m_copySeedBtn->setCursor(Qt::PointingHandCursor);
    m_copySeedBtn->setToolTip(tr("复制种子"));
    connect(m_copySeedBtn, &QPushButton::clicked, this, &SaveSettingsPage::onCopySeedClicked);
    seedRow->addWidget(m_copySeedBtn);
    m_chunkbaseBtn = new QPushButton(tr("Chunkbase"), infoCard);
    m_chunkbaseBtn->setObjectName("bottomActionBtn");
    m_chunkbaseBtn->setCursor(Qt::PointingHandCursor);
    m_chunkbaseBtn->setToolTip(tr("在 Chunkbase 网站查看种子地形"));
    connect(m_chunkbaseBtn, &QPushButton::clicked, this, &SaveSettingsPage::onOpenChunkbaseClicked);
    seedRow->addWidget(m_chunkbaseBtn);

    m_minecraftSearchBtn = new QPushButton(tr("种子地图"), infoCard);
    m_minecraftSearchBtn->setObjectName("bottomActionBtn");
    m_minecraftSearchBtn->setCursor(Qt::PointingHandCursor);
    m_minecraftSearchBtn->setToolTip(tr("在 MinecraftSearch 查看此种子地图"));
    connect(m_minecraftSearchBtn, &QPushButton::clicked, this, &SaveSettingsPage::onOpenMinecraftSearchClicked);
    seedRow->addWidget(m_minecraftSearchBtn);

    addInfoRow(tr("版本"), m_versionLabel);
    {
        auto *seedRowWidget = new QWidget(infoCard);
        seedRowWidget->setLayout(seedRow);
        addInfoRow(tr("种子"), seedRowWidget);
    }
    addInfoRow(tr("最后游玩"), m_lastPlayedLabel);
    addInfoRow(tr("出生点"), m_spawnLabel);
    addInfoRow(tr("游戏模式"), m_gameModeLabel);
    addInfoRow(tr("游玩时长"), m_playTimeLabel);
    addInfoRow(tr("游戏内时间"), m_dayTimeLabel);

    layout->addWidget(infoCard);

    // ── 设置卡片 ──
    QWidget *settingCard = new QWidget(contentWidget);
    settingCard->setObjectName("funcCard");
    auto *settingCardLayout = new QVBoxLayout(settingCard);
    settingCardLayout->setContentsMargins(16, 16, 16, 16);
    settingCardLayout->setSpacing(10);

    auto *settingHeader = new QLabel(tr("设置"), settingCard);
    settingHeader->setFont(headerFont);
    settingCardLayout->addWidget(settingHeader);

    // 世界名称
    auto *worldNameLabel = new QLabel(tr("世界名称"), settingCard);
    m_worldNameEdit = new QLineEdit(settingCard);
    m_worldNameEdit->setPlaceholderText(tr("存档目录名"));
    m_worldNameEdit->setToolTip(tr("修改后将重命名存档文件夹并同步 level.dat"));
    auto *worldNameRow = new QHBoxLayout();
    worldNameRow->setSpacing(12);
    worldNameRow->addWidget(worldNameLabel);
    worldNameRow->addWidget(m_worldNameEdit, 1);
    settingCardLayout->addLayout(worldNameRow);

    // 游戏模式
    auto *gameTypeLabel = new QLabel(tr("游戏模式"), settingCard);
    m_gameTypeCombo = new QComboBox(settingCard);
    m_gameTypeCombo->addItem(tr("生存模式"), 0);
    m_gameTypeCombo->addItem(tr("创造模式"), 1);
    m_gameTypeCombo->addItem(tr("冒险模式"), 2);
    m_gameTypeCombo->addItem(tr("观察者模式"), 3);
    auto *gameTypeRow = new QHBoxLayout();
    gameTypeRow->setSpacing(12);
    gameTypeRow->addWidget(gameTypeLabel);
    gameTypeRow->addWidget(m_gameTypeCombo);
    gameTypeRow->addStretch();
    settingCardLayout->addLayout(gameTypeRow);

    // 允许作弊
    auto *allowCommandsLabel = new QLabel(tr("允许作弊"), settingCard);
    m_allowCommandsCombo = new QComboBox(settingCard);
    m_allowCommandsCombo->addItem(tr("关闭"), 0);
    m_allowCommandsCombo->addItem(tr("开启"), 1);
    auto *allowRow = new QHBoxLayout();
    allowRow->setSpacing(12);
    allowRow->addWidget(allowCommandsLabel);
    allowRow->addWidget(m_allowCommandsCombo);
    allowRow->addStretch();
    settingCardLayout->addLayout(allowRow);

    // 难度 + 锁定
    auto *difficultyLabel = new QLabel(tr("难度"), settingCard);
    m_difficultyCombo = new QComboBox(settingCard);
    m_difficultyCombo->addItem(tr("和平"), 0);
    m_difficultyCombo->addItem(tr("简单"), 1);
    m_difficultyCombo->addItem(tr("普通"), 2);
    m_difficultyCombo->addItem(tr("困难"), 3);

    m_lockDifficultyCheck = new QCheckBox(tr("锁定难度"), settingCard);
    m_lockDifficultyCheck->setToolTip(tr("锁定后无法在游戏内修改难度"));
    connect(m_lockDifficultyCheck, &QCheckBox::toggled,
            this, &SaveSettingsPage::onHardcoreToggled);

    auto *difficultyRow = new QHBoxLayout();
    difficultyRow->setSpacing(12);
    difficultyRow->addWidget(difficultyLabel);
    difficultyRow->addWidget(m_difficultyCombo);
    difficultyRow->addWidget(m_lockDifficultyCheck);
    difficultyRow->addStretch();
    settingCardLayout->addLayout(difficultyRow);

    // 极限模式
    auto *hardcoreLabel = new QLabel(tr("极限模式"), settingCard);
    m_hardcoreCheck = new QCheckBox(tr("开启"), settingCard);
    m_hardcoreCheck->setToolTip(tr("极限模式：难度固定为困难并锁定，死亡即删除存档"));
    connect(m_hardcoreCheck, &QCheckBox::toggled,
            this, &SaveSettingsPage::onHardcoreToggled);
    auto *hardcoreRow = new QHBoxLayout();
    hardcoreRow->setSpacing(12);
    hardcoreRow->addWidget(hardcoreLabel);
    hardcoreRow->addWidget(m_hardcoreCheck);
    hardcoreRow->addStretch();
    settingCardLayout->addLayout(hardcoreRow);

    m_hardcoreHint = new QLabel(tr("极限模式存档的难度固定为困难，无法修改。"), settingCard);
    m_hardcoreHint->setStyleSheet(QStringLiteral("color: #888888; font-size: 12px;"));
    m_hardcoreHint->hide();
    settingCardLayout->addWidget(m_hardcoreHint);

    // 游戏内时间
    auto *dayTimeLabel = new QLabel(tr("游戏内时间"), settingCard);
    m_dayTimeCombo = new QComboBox(settingCard);
    m_dayTimeCombo->addItem(tr("6:00（黎明）"), 0);
    m_dayTimeCombo->addItem(tr("8:00（清晨）"), 2000);
    m_dayTimeCombo->addItem(tr("12:00（正午）"), 6000);
    m_dayTimeCombo->addItem(tr("18:00（黄昏）"), 12000);
    m_dayTimeCombo->addItem(tr("0:00（午夜）"), 18000);
    auto *dayTimeRow = new QHBoxLayout();
    dayTimeRow->setSpacing(12);
    dayTimeRow->addWidget(dayTimeLabel);
    dayTimeRow->addWidget(m_dayTimeCombo);
    dayTimeRow->addStretch();
    settingCardLayout->addLayout(dayTimeRow);

    // 出生点坐标
    auto *spawnLabel = new QLabel(tr("出生点"), settingCard);
    m_spawnXSpin = new QSpinBox(settingCard);
    m_spawnYSpin = new QSpinBox(settingCard);
    m_spawnZSpin = new QSpinBox(settingCard);
    const int spawnRange = 30000000;
    for (QSpinBox *spin : { m_spawnXSpin, m_spawnYSpin, m_spawnZSpin })
    {
        spin->setRange(-spawnRange, spawnRange);
        spin->setFixedWidth(110);
    }
    auto *spawnRow = new QHBoxLayout();
    spawnRow->setSpacing(8);
    spawnRow->addWidget(spawnLabel);
    spawnRow->addWidget(new QLabel(tr("X"), settingCard));
    spawnRow->addWidget(m_spawnXSpin);
    spawnRow->addWidget(new QLabel(tr("Y"), settingCard));
    spawnRow->addWidget(m_spawnYSpin);
    spawnRow->addWidget(new QLabel(tr("Z"), settingCard));
    spawnRow->addWidget(m_spawnZSpin);
    spawnRow->addStretch();
    settingCardLayout->addLayout(spawnRow);

    layout->addWidget(settingCard);

    // ── 游戏规则卡片 ──
    buildRulesCard(contentWidget, layout, headerFont);

    layout->addStretch();

    // ── 底部按钮 ──
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(10);
    m_openFolderBtn = new QPushButton(tr("打开存档文件夹"), contentWidget);
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &SaveSettingsPage::onOpenFolderClicked);
    bottomRow->addWidget(m_openFolderBtn);

    // 备份与恢复：随时给这个存档留一份、或退回之前的进度
    m_backupBtn = new QPushButton(tr("备份与恢复"), contentWidget);
    m_backupBtn->setObjectName("bottomActionBtn");
    m_backupBtn->setCursor(Qt::PointingHandCursor);
    m_backupBtn->setToolTip(tr("为这个存档创建备份，或恢复到之前的备份点"));
    connect(m_backupBtn, &QPushButton::clicked, this, [this]() {
        if (!m_saveName.isEmpty())
            emit backupRequested(m_saveName);
    });
    bottomRow->addWidget(m_backupBtn);

    bottomRow->addStretch();

    m_saveBtn = new QPushButton(tr("保存修改"), contentWidget);
    m_saveBtn->setObjectName("addFirstAccountBtn");
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, &SaveSettingsPage::onSaveClicked);
    bottomRow->addWidget(m_saveBtn);
    layout->addLayout(bottomRow);

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);

    // 初始占位
    m_versionLabel->setText(tr("—"));
    m_seedLabel->setText(tr("—"));
    m_lastPlayedLabel->setText(tr("—"));
    m_spawnLabel->setText(tr("—"));
    m_gameModeLabel->setText(tr("—"));
    m_playTimeLabel->setText(tr("—"));
    m_dayTimeLabel->setText(tr("—"));
    m_copySeedBtn->setEnabled(false);
    m_chunkbaseBtn->setEnabled(false);
    m_minecraftSearchBtn->setEnabled(false);
    m_worldNameEdit->setEnabled(false);
    m_gameTypeCombo->setEnabled(false);
    m_allowCommandsCombo->setEnabled(false);
    m_difficultyCombo->setEnabled(false);
    m_lockDifficultyCheck->setEnabled(false);
    m_hardcoreCheck->setEnabled(false);
    m_dayTimeCombo->setEnabled(false);
    m_spawnXSpin->setEnabled(false);
    m_spawnYSpin->setEnabled(false);
    m_spawnZSpin->setEnabled(false);
    m_saveBtn->setEnabled(false);
    setRulesEnabled(false);
}

void SaveSettingsPage::setCurrentSave(const QString &saveFolderPath, const QString &saveName)
{
    m_saveFolderPath = saveFolderPath;
    m_saveName = saveName;
    m_saveNameLabel->setText(saveName);
    m_loaded = false;
    loadSaveInfo();
}

void SaveSettingsPage::reset()
{
    m_saveFolderPath.clear();
    m_saveName.clear();
    m_loaded = false;
    m_info.gameRules.clear();
    if (m_otherRulesLayout)
        loadOtherRules();
    m_saveNameLabel->clear();
    m_versionLabel->setText(tr("—"));
    m_seedLabel->setText(tr("—"));
    m_lastPlayedLabel->setText(tr("—"));
    m_spawnLabel->setText(tr("—"));
    m_gameModeLabel->setText(tr("—"));
    m_playTimeLabel->setText(tr("—"));
    m_dayTimeLabel->setText(tr("—"));
    m_copySeedBtn->setEnabled(false);
    m_chunkbaseBtn->setEnabled(false);
    m_minecraftSearchBtn->setEnabled(false);
    m_worldNameEdit->clear();
    m_worldNameEdit->setEnabled(false);
    m_gameTypeCombo->setCurrentIndex(0);
    m_gameTypeCombo->setEnabled(false);
    m_allowCommandsCombo->setCurrentIndex(0);
    m_allowCommandsCombo->setEnabled(false);
    m_difficultyCombo->setCurrentIndex(1);
    m_difficultyCombo->setEnabled(false);
    m_lockDifficultyCheck->setChecked(false);
    m_lockDifficultyCheck->setEnabled(false);
    m_hardcoreCheck->setChecked(false);
    m_hardcoreCheck->setEnabled(false);
    m_dayTimeCombo->setCurrentIndex(0);
    m_dayTimeCombo->setEnabled(false);
    m_spawnXSpin->setValue(0);
    m_spawnYSpin->setValue(0);
    m_spawnZSpin->setValue(0);
    m_spawnXSpin->setEnabled(false);
    m_spawnYSpin->setEnabled(false);
    m_spawnZSpin->setEnabled(false);
    m_saveBtn->setEnabled(false);
    setRulesEnabled(false);
}

void SaveSettingsPage::addInfoRow(const QString &head, QWidget *content)
{
    int row = m_infoGrid->rowCount();
    auto *headLabel = new QLabel(head, this);
    headLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 13px;"));
    m_infoGrid->addWidget(headLabel, row, 0, Qt::AlignLeft | Qt::AlignTop);
    m_infoGrid->addWidget(content, row, 1);
}

void SaveSettingsPage::loadSaveInfo()
{
    if (m_loaded || m_saveFolderPath.isEmpty())
        return;

    auto result = LevelDatEditor::readSave(m_saveFolderPath);
    m_loaded = true;

    if (!result.has_value())
    {
        m_versionLabel->setText(tr("读取失败"));
        m_seedLabel->setText(tr("无法解析 level.dat"));
        m_lastPlayedLabel->setText(tr("—"));
        m_spawnLabel->setText(tr("—"));
        m_gameModeLabel->setText(tr("—"));
        m_playTimeLabel->setText(tr("—"));
        m_dayTimeLabel->setText(tr("—"));
        m_worldNameEdit->setEnabled(false);
        m_gameTypeCombo->setEnabled(false);
        m_allowCommandsCombo->setEnabled(false);
        m_difficultyCombo->setEnabled(false);
        m_lockDifficultyCheck->setEnabled(false);
        m_hardcoreCheck->setEnabled(false);
        m_dayTimeCombo->setEnabled(false);
        m_spawnXSpin->setEnabled(false);
        m_spawnYSpin->setEnabled(false);
        m_spawnZSpin->setEnabled(false);
        m_saveBtn->setEnabled(false);
        setRulesEnabled(false);
        return;
    }

    m_info = result.value();

    m_versionLabel->setText(m_info.versionName.isEmpty() ? tr("未知") : m_info.versionName);

    if (m_info.hasSeed)
    {
        m_seedLabel->setText(QString::number(m_info.seed));
        m_copySeedBtn->setEnabled(true);
        m_chunkbaseBtn->setEnabled(!versionForChunkbase().isEmpty());
        m_minecraftSearchBtn->setEnabled(!versionForMinecraftSearch().isEmpty());
    }
    else
    {
        m_seedLabel->setText(tr("无法读取"));
        m_copySeedBtn->setEnabled(false);
        m_chunkbaseBtn->setEnabled(false);
        m_minecraftSearchBtn->setEnabled(false);
    }

    m_lastPlayedLabel->setText(formatLastPlayed());
    m_spawnLabel->setText(m_info.hasSpawn
        ? QString("%1 / %2 / %3").arg(m_info.spawnX).arg(m_info.spawnY).arg(m_info.spawnZ)
        : tr("未知"));
    m_gameModeLabel->setText(m_info.hardcore ? tr("极限模式") : gameModeName(m_info.gameType));
    m_playTimeLabel->setText(formatPlayTime());
    m_dayTimeLabel->setText(m_info.hasDayTime
        ? formatDayTimeClock(m_info.dayTime)
        : tr("—"));

    // 世界名称（默认显示存档目录名）
    m_worldNameEdit->setText(m_saveName.isEmpty() ? m_info.levelName : m_saveName);
    m_worldNameEdit->setEnabled(true);

    // 游戏模式
    m_gameTypeCombo->setEnabled(true);
    const int gameTypeIdx = m_gameTypeCombo->findData(qBound(0, m_info.gameType, 3));
    if (m_info.hasGameType && gameTypeIdx >= 0)
        m_gameTypeCombo->setCurrentIndex(gameTypeIdx);

    // 游戏内时间
    m_dayTimeCombo->setEnabled(true);
    if (m_info.hasDayTime)
        setDayTimePreset(m_info.dayTime);

    m_allowCommandsCombo->setEnabled(true);
    m_difficultyCombo->setEnabled(true);
    m_lockDifficultyCheck->setEnabled(true);
    m_hardcoreCheck->setEnabled(true);
    m_saveBtn->setEnabled(true);

    m_allowCommandsCombo->setCurrentIndex(m_info.allowCommands ? 1 : 0);
    if (m_info.hasDifficulty)
    {
        m_difficultyCombo->setCurrentIndex(qBound(0, m_info.difficulty, 3));
    }
    m_lockDifficultyCheck->setChecked(m_info.difficultyLocked);
    m_hardcoreCheck->setChecked(m_info.hardcore);

    // 出生点
    m_spawnXSpin->setValue(m_info.hasSpawn ? m_info.spawnX : 0);
    m_spawnYSpin->setValue(m_info.hasSpawn ? m_info.spawnY : 0);
    m_spawnZSpin->setValue(m_info.hasSpawn ? m_info.spawnZ : 0);
    refreshSpawnSpinEnable();

    if (m_info.hardcore)
    {
        m_difficultyCombo->setEnabled(false);
        m_lockDifficultyCheck->setEnabled(false);
        m_hardcoreHint->show();
    }
    else
    {
        m_hardcoreHint->hide();
        refreshHardcoreState();
    }

    // 游戏规则
    loadRuleValues();
    setRulesEnabled(true);
}

void SaveSettingsPage::onHardcoreToggled()
{
    refreshHardcoreState();
    refreshSpawnSpinEnable();
}

void SaveSettingsPage::refreshHardcoreState()
{
    const bool hardcore = m_hardcoreCheck->isChecked();
    const bool locked = m_lockDifficultyCheck->isChecked();

    if (hardcore)
    {
        // 极限模式：难度固定为困难并锁定
        m_difficultyCombo->setCurrentIndex(3); // 困难
        m_difficultyCombo->setEnabled(false);
        m_lockDifficultyCheck->setChecked(true);
        m_lockDifficultyCheck->setEnabled(false);
        m_hardcoreHint->show();
    }
    else
    {
        m_hardcoreHint->hide();
        m_lockDifficultyCheck->setEnabled(true);
        m_difficultyCombo->setEnabled(!locked);
    }
}

void SaveSettingsPage::refreshSpawnSpinEnable()
{
    const bool enable = m_loaded && !m_saveFolderPath.isEmpty();
    m_spawnXSpin->setEnabled(enable);
    m_spawnYSpin->setEnabled(enable);
    m_spawnZSpin->setEnabled(enable);
}

// ============================================================================
// 游戏规则卡片
// ============================================================================
void SaveSettingsPage::buildRulesCard(QWidget *contentWidget, QVBoxLayout *layout,
                                      const QFont &headerFont)
{
    QWidget *rulesCard = new QWidget(contentWidget);
    rulesCard->setObjectName("funcCard");
    auto *cardLayout = new QVBoxLayout(rulesCard);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(10);

    // 标题行：标题 + 提示 + 恢复默认值
    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(8);
    auto *rulesHeader = new QLabel(tr("游戏规则"), rulesCard);
    rulesHeader->setFont(headerFont);
    headerRow->addWidget(rulesHeader);
    auto *rulesHint = new QLabel(tr("免命令调整常用规则，随「保存修改」一起写入存档"), rulesCard);
    rulesHint->setStyleSheet(QStringLiteral("color: #888888; font-size: 12px;"));
    headerRow->addWidget(rulesHint);
    headerRow->addStretch();
    m_rulesResetBtn = new QPushButton(tr("恢复默认值"), rulesCard);
    m_rulesResetBtn->setObjectName("bottomActionBtn");
    m_rulesResetBtn->setCursor(Qt::PointingHandCursor);
    m_rulesResetBtn->setToolTip(tr("把下方所有规则恢复为游戏默认值，点「保存修改」后写入存档"));
    connect(m_rulesResetBtn, &QPushButton::clicked, this, &SaveSettingsPage::onRulesResetClicked);
    headerRow->addWidget(m_rulesResetBtn);
    cardLayout->addLayout(headerRow);

    // 搜索框
    m_ruleSearchEdit = new QLineEdit(rulesCard);
    m_ruleSearchEdit->setPlaceholderText(tr("搜索规则，如：不掉落、keepInventory"));
    m_ruleSearchEdit->setClearButtonEnabled(true);
    connect(m_ruleSearchEdit, &QLineEdit::textChanged,
            this, &SaveSettingsPage::onRuleSearchChanged);
    cardLayout->addWidget(m_ruleSearchEdit);

    // 按定义表中出现的顺序分组展示
    QStringList groups;
    for (const GameRuleDef &def : kGameRules)
    {
        const QString group = tr(def.group);
        if (!groups.contains(group))
            groups.append(group);
    }

    for (const QString &group : groups)
    {
        auto *groupHeader = new QLabel(group, rulesCard);
        groupHeader->setStyleSheet(
            QStringLiteral("color: #888888; font-size: 12px; font-weight: bold;"));
        cardLayout->addWidget(groupHeader);
        m_groupHeaders.insert(group, groupHeader);

        auto *grid = new QGridLayout();
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(6);
        cardLayout->addLayout(grid);

        int row = 0;
        int col = 0;
        for (const GameRuleDef &def : kGameRules)
        {
            if (tr(def.group) != group)
                continue;

            RuleControl rule;
            rule.id = QString::fromLatin1(def.id);
            rule.zh = tr(def.zh);
            rule.group = group;
            rule.defaultValue = QString::fromLatin1(def.defaultValue);
            rule.numeric = def.numeric;
            QString tip = tr("%1\n规则 ID：%2").arg(tr(def.desc), rule.id);
            if (def.since && *def.since)
                tip += tr("\n需要 %1 或更新版本，旧版本游戏会忽略此规则")
                           .arg(QString::fromLatin1(def.since));

            if (rule.numeric)
            {
                // 数值规则独占一整行：若上一行右格已被占用则先另起一行
                if (col == 1)
                    ++row;
                col = 0;
                auto *rowWidget = new QWidget(rulesCard);
                auto *rowLayout = new QHBoxLayout(rowWidget);
                rowLayout->setContentsMargins(0, 0, 0, 0);
                rowLayout->setSpacing(8);
                auto *nameLabel = new QLabel(rule.zh, rowWidget);
                nameLabel->setToolTip(tip);
                rowLayout->addWidget(nameLabel);
                rule.spin = new QSpinBox(rowWidget);
                rule.spin->setRange(def.minValue, def.maxValue);
                rule.spin->setToolTip(tip);
                rowLayout->addWidget(rule.spin);
                rowLayout->addStretch();
                rule.row = rowWidget;
                grid->addWidget(rowWidget, row, 0, 1, 2);
                ++row;
            }
            else
            {
                rule.check = new QCheckBox(rule.zh, rulesCard);
                rule.check->setToolTip(tip);
                rule.row = rule.check;
                grid->addWidget(rule.check, row, col);
                if (col == 0)
                    col = 1;
                else
                {
                    col = 0;
                    ++row;
                }
            }
            m_rules.append(rule);
        }
    }

    // 其他规则：模组、数据包或更高版本游戏添加的规则，随存档动态填充
    auto *otherHeader = new QLabel(tr("其他规则"), rulesCard);
    otherHeader->setStyleSheet(
        QStringLiteral("color: #888888; font-size: 12px; font-weight: bold;"));
    otherHeader->setToolTip(tr("由模组、数据包或更高版本游戏添加的规则，改动会原样写入存档"));
    otherHeader->hide();
    cardLayout->addWidget(otherHeader);
    m_groupHeaders.insert(tr("其他规则"), otherHeader);

    m_otherRulesLayout = new QVBoxLayout();
    m_otherRulesLayout->setSpacing(6);
    cardLayout->addLayout(m_otherRulesLayout);

    layout->addWidget(rulesCard);
}

void SaveSettingsPage::loadRuleValues()
{
    m_ruleBaseline.clear();
    for (const RuleControl &rule : m_rules)
    {
        // level.dat 中缺省的规则按游戏默认值展示（游戏加载时对缺失规则同样取默认值）
        const QString fileValue = m_info.gameRules.value(rule.id);
        if (rule.numeric)
        {
            int value = rule.defaultValue.toInt();
            if (!fileValue.isEmpty())
            {
                bool ok = false;
                const int parsed = fileValue.toInt(&ok);
                if (ok)
                    value = parsed;
            }
            value = qBound(rule.spin->minimum(), value, rule.spin->maximum());
            rule.spin->setValue(value);
            m_ruleBaseline.insert(rule.id, QString::number(value));
        }
        else
        {
            const bool on = fileValue.isEmpty()
                ? (rule.defaultValue == QStringLiteral("true"))
                : (fileValue == QStringLiteral("true"));
            rule.check->setChecked(on);
            m_ruleBaseline.insert(rule.id,
                                  on ? QStringLiteral("true") : QStringLiteral("false"));
        }
    }

    loadOtherRules();
}

void SaveSettingsPage::loadOtherRules()
{
    // 清空上一次的行
    m_otherRules.clear();
    while (QLayoutItem *item = m_otherRulesLayout->takeAt(0))
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    // 已知规则之外的原样展示并允许编辑（模组 / 数据包 / 更高版本游戏添加）
    const QString otherGroup = tr("其他规则");
    for (auto it = m_info.gameRules.constBegin(); it != m_info.gameRules.constEnd(); ++it)
    {
        bool known = false;
        for (const RuleControl &rule : m_rules)
        {
            if (rule.id == it.key())
            {
                known = true;
                break;
            }
        }
        if (known)
            continue;

        RuleControl rule;
        rule.id = it.key();
        rule.zh = it.key();
        rule.group = otherGroup;
        rule.defaultValue = it.value();
        const QString tip = tr("该规则来自模组、数据包或更高版本的游戏，值会原样写入存档");

        auto *rowWidget = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto *idLabel = new QLabel(it.key(), rowWidget);
        idLabel->setToolTip(tip);
        rowLayout->addWidget(idLabel);
        rule.text = new QLineEdit(it.value(), rowWidget);
        rule.text->setToolTip(tip);
        rowLayout->addWidget(rule.text, 1);
        rule.row = rowWidget;
        m_otherRulesLayout->addWidget(rowWidget);
        m_otherRules.append(rule);
        m_ruleBaseline.insert(rule.id, it.value());
    }
}

void SaveSettingsPage::setRulesEnabled(bool enabled)
{
    if (m_ruleSearchEdit)
        m_ruleSearchEdit->setEnabled(enabled);
    if (m_rulesResetBtn)
        m_rulesResetBtn->setEnabled(enabled);
    auto setRowEnabled = [&enabled](const RuleControl &rule) {
        if (rule.numeric && rule.spin)
            rule.spin->setEnabled(enabled);
        else if (rule.text)
            rule.text->setEnabled(enabled);
        else if (rule.check)
            rule.check->setEnabled(enabled);
    };
    for (const RuleControl &rule : m_rules)
        setRowEnabled(rule);
    for (const RuleControl &rule : m_otherRules)
        setRowEnabled(rule);
}

void SaveSettingsPage::onRuleSearchChanged(const QString &text)
{
    const QString keyword = text.trimmed();
    QMap<QString, int> visibleCount;
    auto applyFilter = [this, &keyword, &visibleCount](const RuleControl &rule) {
        bool match = keyword.isEmpty()
            || rule.zh.contains(keyword, Qt::CaseInsensitive)
            || rule.id.contains(keyword, Qt::CaseInsensitive);
        if (!match && rule.text)
            match = rule.text->text().contains(keyword, Qt::CaseInsensitive);
        rule.row->setVisible(match);
        if (match)
            ++visibleCount[rule.group];
    };
    for (const RuleControl &rule : m_rules)
        applyFilter(rule);
    for (const RuleControl &rule : m_otherRules)
        applyFilter(rule);
    // 全组规则都被过滤掉时隐藏组标题
    for (auto it = m_groupHeaders.constBegin(); it != m_groupHeaders.constEnd(); ++it)
        it.value()->setVisible(visibleCount.value(it.key()) > 0);
}

void SaveSettingsPage::onRulesResetClicked()
{
    for (const RuleControl &rule : m_rules)
    {
        // 其他规则没有已知的默认值，恢复默认只作用于内置规则
        if (rule.text)
            continue;
        if (rule.numeric)
            rule.spin->setValue(rule.defaultValue.toInt());
        else
            rule.check->setChecked(rule.defaultValue == QStringLiteral("true"));
    }
}

void SaveSettingsPage::setDayTimePreset(qint64 ticks)
{
    // 先移除之前追加的自定义项（前 5 项为内置预设）
    while (m_dayTimeCombo->count() > 5)
        m_dayTimeCombo->removeItem(m_dayTimeCombo->count() - 1);

    int idx = m_dayTimeCombo->findData(ticks);
    if (idx >= 0)
    {
        m_dayTimeCombo->setCurrentIndex(idx);
    }
    else
    {
        // 自定义时间：追加临时项并选中
        m_dayTimeCombo->addItem(formatDayTimeClock(ticks), ticks);
        m_dayTimeCombo->setCurrentIndex(m_dayTimeCombo->count() - 1);
    }
}

qint64 SaveSettingsPage::currentDayTimeTicks() const
{
    const QVariant v = m_dayTimeCombo->currentData();
    if (v.isValid())
        return v.toLongLong();
    return 6000;
}

void SaveSettingsPage::onSaveClicked()
{
    // 1. 世界名称修改 → 先重命名文件夹并同步 level.dat
    const QString newName = m_worldNameEdit->text().trimmed();
    QString currentFolder = m_saveFolderPath;
    if (!newName.isEmpty() && newName != m_saveName)
    {
        QString newPath;
        QString error;
        if (!LevelDatEditor::renameWorld(m_saveFolderPath, newName, &newPath, &error))
        {
            AppMessageBox::warning(this, tr("重命名失败"),
                tr("无法重命名世界：\n%1").arg(error));
            return;
        }
        const QString oldName = m_saveName;
        currentFolder = newPath;
        m_saveFolderPath = newPath;
        m_saveName = newName;
        m_saveNameLabel->setText(newName);
        emit saveRenamed(oldName, newName);
    }

    // 2. 其它设置
    SaveChanges changes;
    changes.allowCommands = (m_allowCommandsCombo->currentIndex() == 1);
    changes.difficulty = m_difficultyCombo->currentData().toInt();
    changes.difficultyLocked = m_lockDifficultyCheck->isChecked();
    changes.gameType = m_gameTypeCombo->currentData().toInt();
    changes.hardcore = m_hardcoreCheck->isChecked();
    changes.dayTime = currentDayTimeTicks();
    changes.spawnX = m_spawnXSpin->value();
    changes.spawnY = m_spawnYSpin->value();
    changes.spawnZ = m_spawnZSpin->value();

    // 3. 游戏规则：只写入与载入基线不同的项，避免污染旧版本存档
    QMap<QString, QString> ruleChanges;
    auto collectRuleChange = [this, &ruleChanges](const RuleControl &rule) {
        QString current;
        if (rule.numeric && rule.spin)
            current = QString::number(rule.spin->value());
        else if (rule.text)
        {
            // 其他规则原样写入；清空视为不修改，避免生成空值规则
            current = rule.text->text();
            if (current.isEmpty())
                return;
        }
        else if (rule.check)
            current = rule.check->isChecked() ? QStringLiteral("true")
                                              : QStringLiteral("false");
        if (current != m_ruleBaseline.value(rule.id))
            ruleChanges.insert(rule.id, current);
    };
    for (const RuleControl &rule : m_rules)
        collectRuleChange(rule);
    for (const RuleControl &rule : m_otherRules)
        collectRuleChange(rule);
    if (!ruleChanges.isEmpty())
        changes.gameRules = ruleChanges;

    QString error;
    if (!LevelDatEditor::applyChanges(currentFolder, changes, &error))
    {
        AppMessageBox::warning(this, tr("保存失败"),
            tr("无法保存存档设置：\n%1").arg(error.isEmpty() ? tr("未做任何修改") : error));
        return;
    }

    AppMessageBox::information(this, tr("保存成功"),
        tr("存档设置已保存。\n注意：若游戏正在运行，修改可能需要重启游戏后生效。"));

    // 保存成功后重新读取，让顶部信息与规则基线同步
    m_loaded = false;
    loadSaveInfo();
}

void SaveSettingsPage::onOpenFolderClicked()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_saveFolderPath));
}

void SaveSettingsPage::onCopySeedClicked()
{
    if (m_info.hasSeed)
    {
        QApplication::clipboard()->setText(QString::number(m_info.seed));
    }
}

void SaveSettingsPage::onOpenChunkbaseClicked()
{
    if (!m_info.hasSeed)
        return;
    const QString url = chunkbaseUrl();
    if (url.isEmpty())
    {
        AppMessageBox::information(this, tr("无法打开"),
            tr("无法确定存档版本，请手动打开 Chunkbase 并输入种子。"));
        return;
    }
    QDesktopServices::openUrl(QUrl(url));
}

void SaveSettingsPage::onOpenMinecraftSearchClicked()
{
    if (!m_info.hasSeed)
        return;
    const QString url = minecraftSearchUrl();
    if (url.isEmpty())
    {
        AppMessageBox::information(this, tr("无法打开"),
            tr("无法确定存档版本，请手动打开 MinecraftSearch 种子地图并输入种子。"));
        return;
    }
    QDesktopServices::openUrl(QUrl(url));
}

QString SaveSettingsPage::formatPlayTime() const
{
    return m_info.playTimeTicks > 0
        ? formatTicksAsDuration(m_info.playTimeTicks)
        : tr("—");
}

QString SaveSettingsPage::formatLastPlayed() const
{
    if (m_info.lastPlayed <= 0)
        return tr("—");
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(m_info.lastPlayed);
    return dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}

QString SaveSettingsPage::versionForChunkbase() const
{
    const QString v = m_info.versionName;
    if (v.isEmpty() || v.contains(QLatin1Char(' ')))
        return QString();
    // 去掉可能的 "Java版 " 前缀
    QString clean = v;
    if (clean.startsWith(QStringLiteral("Java版")))
        clean = clean.mid(4).trimmed();

    // 快照/预览版本含字母，无法确定平台版本
    if (std::any_of(clean.begin(), clean.end(), [](const QChar &c) { return c.isLetter(); }))
        return QString();

    // 1.21+ 使用完整次版本号；其余使用主版本号
    if (clean.startsWith(QStringLiteral("1.21")))
        return clean.replace(QLatin1Char('.'), QLatin1Char('_'));
    if (clean.contains(QLatin1Char('.')))
    {
        QStringList parts = clean.split(QLatin1Char('.'));
        return parts.mid(0, 2).join(QLatin1Char('_'));
    }
    return clean;
}

QString SaveSettingsPage::chunkbaseUrl() const
{
    const QString ver = versionForChunkbase();
    if (ver.isEmpty())
        return QString();
    return QStringLiteral("https://www.chunkbase.com/apps/seed-map#seed=%1&platform=java_%2&dimension=overworld")
        .arg(m_info.seed)
        .arg(ver);
}

QString SaveSettingsPage::versionForMinecraftSearch() const
{
    const QString v = m_info.versionName;
    if (v.isEmpty() || v.contains(QLatin1Char(' ')))
        return QString();
    // 去掉可能的 "Java版 " 前缀
    QString clean = v;
    if (clean.startsWith(QStringLiteral("Java版")))
        clean = clean.mid(4).trimmed();

    // 快照/预览版本含字母，无法确定平台版本
    if (std::any_of(clean.begin(), clean.end(), [](const QChar &c) { return c.isLetter(); }))
        return QString();

    // MinecraftSearch 的 platform 参数去掉开头的 "1." 前缀，如 1.26.2 -> 26.2
    if (clean.startsWith(QStringLiteral("1.")))
        clean = clean.mid(2);
    return clean;
}

QString SaveSettingsPage::minecraftSearchUrl() const
{
    const QString ver = versionForMinecraftSearch();
    if (ver.isEmpty())
        return QString();
    QString url = QStringLiteral("https://minecraftsearch.com/zh-CN/工具/种子地图#seed=%1&platform=java_%2&dimension=overworld")
        .arg(m_info.seed)
        .arg(ver);
    // 带上出生点坐标，打开后直接定位到当前世界出生点附近
    if (m_info.hasSpawn)
        url += QStringLiteral("&x=%1&z=%2&zoom=12").arg(m_info.spawnX).arg(m_info.spawnZ);
    return url;
}
