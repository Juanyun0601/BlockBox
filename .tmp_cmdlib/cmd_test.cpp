// 指令翻译/补全的离线小测试：直接读 JSON 文件，不依赖界面和资源系统
#include <QCoreApplication>
#include <QTextStream>
#include <QStringList>
#include <QDebug>

#include "utils/CommandAssistant/CommandCompleter.h"
#include "utils/CommandAssistant/CommandDatabase.h"
#include "utils/CommandAssistant/CommandTranslator.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    if (argc < 2)
    {
        out << "usage: cmd_test <command_database.json>\n";
        return 2;
    }

    CommandDatabase db;
    if (!db.loadFromFile(QString::fromUtf8(argv[1])))
    {
        out << "load failed\n";
        return 1;
    }

    CommandTranslator translator(&db);
    CommandCompleter completer(&db);

    const QStringList inputs = {
        QStringLiteral("给予 @s 钻石剑 1"),
        QStringLiteral("给 钻石 一组"),
        QStringLiteral("给予 @s 苹果 两把"),
        QStringLiteral("传送到 0,64,0"),
        QStringLiteral("传送到 10 70 10"),
        QStringLiteral("把我传送到 10 70 10"),
        QStringLiteral("给予 @a 速度 效果"),
        QStringLiteral("清除所有效果"),
        QStringLiteral("效果 速度 30"),
        QStringLiteral("效果 清除"),
        QStringLiteral("时间 正午"),
        QStringLiteral("天气 雷暴"),
        QStringLiteral("游戏模式 创造 @s"),
        QStringLiteral("附魔 锋利 5"),
        QStringLiteral("给我的钻石剑附魔锋利 5"),
        QStringLiteral("加 10 点经验"),
        QStringLiteral("难度改成困难"),
        QStringLiteral("回血"),
        QStringLiteral("把重生点设在这里"),
        QStringLiteral("寻找最近的村庄"),
        QStringLiteral("定位 要塞"),
        QStringLiteral("造成 10 点伤害"),
        QStringLiteral("杀死所有僵尸"),
        QStringLiteral("清空我的背包"),
        QStringLiteral("查看种子"),
        QStringLiteral("给我一把下界合金剑"),
        QStringLiteral("say hello world"),
        QStringLiteral("给我一个速度效果"),
        QStringLiteral("召唤 闪电"),
        QStringLiteral("填充 石头"),
        QStringLiteral("回家"),
        QStringLiteral("启动 故事模式"),
        QStringLiteral("路径点 保存 家"),
        QStringLiteral("给予 @s 风弹"),
        QStringLiteral("召唤 嘎枝"),
        QStringLiteral("附魔 绑定诅咒"),
        QStringLiteral("定位 古城"),
        QStringLiteral("天气 下雨"),
        QStringLiteral("时间 设置 夜晚"),
        QStringLiteral("给我 30 经验"),
        QStringLiteral("给我一个信标"),
        QStringLiteral("把难度设置为困难"),
        QStringLiteral("杀死所有僵尸"),
        QStringLiteral("清除所有僵尸"),
        QStringLiteral("给我一把重锤"),
        QStringLiteral("给予 @s 玫瑰丛 10"),
        QStringLiteral("设置 时钟"),
        QStringLiteral("给予 @s 钟 1"),
        QStringLiteral("传送到 ~ ~10 ~"),
        QStringLiteral("把我传送到 100 70 100"),
        QStringLiteral("给予所有玩家一把钻石剑"),
        QStringLiteral("清空所有玩家的效果"),
    };

    CommandContext modern;
    modern.version = QStringLiteral("1.20.4");
    modern.opLevel = 4;
    modern.isServer = false;

    out << "=== 翻译（1.20.4 / 单机 / OP4）===\n";
    for (const QString &input : inputs)
    {
        const TranslateResult result = translator.translate(input, modern);
        out << input << "\n    => " << (result.success ? result.englishCommand : QStringLiteral("<未识别>"))
            << "\n    警告: " << result.warnings.join(QStringLiteral(" / "))
            << "\n    不可用: " << result.unavailableReason << "\n";
    }

    out << "\n=== 版本 / 插件过滤 ===\n";
    CommandContext old = modern;
    old.version = QStringLiteral("1.12.2");
    for (const QString &input : {QStringLiteral("方块数据 ~ ~ ~ {}"),
                                 QStringLiteral("检测条件 @a"),
                                 QStringLiteral("定位生物群系 平原"),
                                 QStringLiteral("回家"),
                                 QStringLiteral("定位结构 村庄"),
                                 QStringLiteral("路径点 列表")})
    {
        const TranslateResult oldResult = translator.translate(input, old);
        const TranslateResult newResult = translator.translate(input, modern);
        out << input << "\n    1.12.2 => " << oldResult.englishCommand
            << " | " << oldResult.unavailableReason
            << "\n    1.20.4 => " << newResult.englishCommand
            << " | " << newResult.unavailableReason << "\n";
    }

    out << "\n=== 候选 ===\n";
    const QList<CompletionCandidate> candidates =
        completer.complete(QStringLiteral("给予 @s 钻"), modern);
    for (int i = 0; i < qMin(6, candidates.size()); ++i)
    {
        out << "  " << candidates.at(i).displayText << "  [插入 " << candidates.at(i).insertText
            << "]" << (candidates.at(i).available ? QString() : QStringLiteral("  (不可用)")) << "\n";
    }

    out << "\n=== 中文枚举候选 ===\n";
    for (const QString &input : {QStringLiteral("游戏模式 "), QStringLiteral("效果 "),
                                 QStringLiteral("天气 "), QStringLiteral("路径点 ")})
    {
        const QList<CompletionCandidate> list = completer.complete(input, modern);
        QStringList parts;
        for (int i = 0; i < qMin(5, list.size()); ++i)
        {
            parts << list.at(i).displayText;
        }
        out << input << " => " << parts.join(QStringLiteral(" | ")) << "\n";
    }

    out << "\n=== hidden 检查 ===\n";
    for (const QString &input : {QStringLiteral("私聊"), QStringLiteral("帮助")})
    {
        const QList<CompletionCandidate> list = completer.complete(input, modern);
        QStringList parts;
        for (const CompletionCandidate &c : list)
        {
            parts << c.displayText;
        }
        out << input << " => " << parts.join(QStringLiteral(" | ")) << "\n";
    }

    return 0;
}
