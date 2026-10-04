/**
 * @file   TriviaWidget.cpp
 * @brief  冷知识显示组件实现
 * @author BlockBox Team
 */
#include "TriviaWidget.h"

#include <QHBoxLayout>
#include <QFont>

#include "utils/ThemeManager.h"

TriviaWidget::TriviaWidget(QWidget *parent)
    : QWidget(parent)
    , m_iconLabel(nullptr)
    , m_triviaLabel(nullptr)
    , m_currentIndex(0)
{
    initUI();
    loadTriviaList();

    m_triviaTimer.setInterval(10000);
    connect(&m_triviaTimer, &QTimer::timeout, this, &TriviaWidget::onTriviaTimer);
}

TriviaWidget::~TriviaWidget()
{
}

void TriviaWidget::initUI()
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(10);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setObjectName(QStringLiteral("triviaIcon"));
    m_iconLabel->setFixedSize(18, 18);
    m_iconLabel->setText(QStringLiteral("💡"));
    layout->addWidget(m_iconLabel);

    m_triviaLabel = new QLabel(this);
    m_triviaLabel->setObjectName(QStringLiteral("triviaLabel"));
    m_triviaLabel->setWordWrap(true);
    m_triviaLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_triviaLabel, 1);

    ThemeManager *tm = ThemeManager::instance();
    const bool isDark = (tm->currentTheme() != ThemeManager::LightTheme);

    setStyleSheet(QString(
        "QWidget { background-color: transparent; }"
        "QLabel#triviaIcon { background-color: transparent; font-size: 14px; }"
        "QLabel#triviaLabel {"
        "    color: %1;"
        "    background-color: transparent;"
        "    font-size: 12px;"
        "}"
    ).arg(isDark ? "#b0b0b0" : "#666666"));

    if (!m_triviaList.isEmpty()) {
        m_triviaLabel->setText(m_triviaList.first());
    }
}

void TriviaWidget::loadTriviaList()
{
    m_triviaList = {
        QStringLiteral("苦力怕（Creeper）的模型最初是 Notch 在做猪模型时代码写反而意外产生的。"),
        QStringLiteral("末影人会主动躲避雨水，因为水会对它们造成伤害。"),
        QStringLiteral("僵尸在困难难度下可以打开木门。"),
        QStringLiteral("骷髅会试图躲避仙人掌行走。"),
        QStringLiteral("蜘蛛在白天会变为中立状态，不再主动攻击玩家。"),
        QStringLiteral("蜜蜂在攻击玩家后会死亡，和现实中的蜜蜂一样。"),
        QStringLiteral("苦力怕非常害怕猫，会主动远离猫。"),
        QStringLiteral("狼被驯服后眼睛会从红色变为蓝色。"),
        QStringLiteral("狐狸会捡起并叼着物品，叼着附魔剑的狐狸攻击力更高。"),
        QStringLiteral("海豚会带领玩家寻找沉船和海底废墟。"),
        QStringLiteral("幼年僵尸的移动速度比成年僵尸快。"),
        QStringLiteral("幻翼会在玩家连续三天不睡觉后在夜间生成。"),
        QStringLiteral("村民在雷暴天气有概率被闪电击中变成女巫。"),
        QStringLiteral("铁傀儡会主动攻击敌对生物来保护村民。"),
        QStringLiteral("蜘蛛可以爬墙，是游戏中少数能爬墙的生物之一。"),
        QStringLiteral("钻石矿只在 Y=16 以下生成，最佳挖掘高度为 Y=-59。"),
        QStringLiteral("黑曜石需要用钻石镐才能挖掘，挖掘时间很长。"),
        QStringLiteral("附魔台需要周围放置 15 个书架才能解锁 30 级附魔。"),
        QStringLiteral("基岩在生存模式下无法被任何方法破坏。"),
        QStringLiteral("绿宝石矿只在山脉生物群系中自然生成。"),
        QStringLiteral("信标需要底部放置铁块/金块/钻石块/绿宝石块才能激活。"),
        QStringLiteral("仙人掌会破坏相邻放置的方块。"),
        QStringLiteral("灵魂沙可以让生物下沉，减缓移动速度。"),
        QStringLiteral("末影龙死亡后会掉落大量经验并开启返回主世界的传送门。"),
        QStringLiteral("下界合金是游戏中最耐久的材料。"),
        QStringLiteral("附魔金苹果是最强大的消耗品之一，在宝箱中非常稀有。"),
        QStringLiteral("鞘翅可以与烟花火箭结合实现无限飞行。"),
        QStringLiteral("三叉戟可以在水下投掷，且不会消失。"),
        QStringLiteral("钓鱼可以钓到附魔装备和附魔书。"),
        QStringLiteral("经验修复（修补）附魔是最有用的附魔之一。"),
        QStringLiteral("Minecraft 是历史上销量最高的电子游戏，已超过 3 亿份。"),
        QStringLiteral("Minecraft 最初由 Markus Notch Persson 在 2009 年开发。"),
        QStringLiteral("Minecraft 的音乐由 C418（Daniel Rosenfeld）创作。"),
        QStringLiteral("Minecraft 的默认玩家皮肤是 Steve 和 Alex。"),
        QStringLiteral("Minecraft 的 Java 版本是用 Java 编写的。"),
        QStringLiteral("Minecraft 的基岩版本是用 C++ 编写的。"),
        QStringLiteral("Minecraft 世界大小理论上是无限的。"),
        QStringLiteral("Minecraft 的区块大小是 16x16 方块。"),
        QStringLiteral("Minecraft 有超过 60 种生物群系。"),
        QStringLiteral("Minecraft 的蘑菇岛是最稀有的生物群系之一，不会生成敌对生物。"),
        QStringLiteral("饥饿值为 0 时玩家无法奔跑。"),
        QStringLiteral("玩家摔落高度超过 23 格会死亡。"),
        QStringLiteral("溺水伤害每秒为 2 颗心。"),
        QStringLiteral("钟可以显示当前游戏时间。"),
        QStringLiteral("指南针指向世界出生点。"),
        QStringLiteral("骨粉可以加速植物生长。"),
        QStringLiteral("营火可以同时烤 4 个食物。"),
        QStringLiteral("漏斗可以用来制作自动熔炉。"),
        QStringLiteral("绊线钩可以检测实体通过，用于制作陷阱。"),
        QStringLiteral("旗帜可以通过染料自定义图案。"),
    };
}

void TriviaWidget::onTriviaTimer()
{
    showNextTrivia();
}

void TriviaWidget::showNextTrivia()
{
    if (m_triviaList.isEmpty()) return;

    m_currentIndex = (m_currentIndex + 1) % m_triviaList.size();
    m_triviaLabel->setText(m_triviaList.at(m_currentIndex));
}

void TriviaWidget::start()
{
    if (!m_triviaList.isEmpty()) {
        m_triviaLabel->setText(m_triviaList.first());
    }
    m_triviaTimer.start();
}

void TriviaWidget::stop()
{
    m_triviaTimer.stop();
}
