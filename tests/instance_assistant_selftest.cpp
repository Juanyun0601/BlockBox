/**
 * @file   instance_assistant_selftest.cpp
 * @brief  实例助手自测探针：打开真实窗口，逐个标签页 / 尺寸 / 主题 / 指令翻译做检查
 *
 * 输出：
 *   .tmp_ia_selftest/report.txt   逐项检查结果（UTF-8）
 *   .tmp_ia_selftest/out/*.png    各状态截图
 */
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextStream>
#include <QThread>
#include <QTime>

#include <cstdio>

#define private public
#include "components/InstanceAssistantWindow.h"
#undef private

#include "utils/ThemeManager.h"
#include "utils/SettingsManager.h"
#include "utils/CommandAssistant/CommandDatabase.h"

namespace
{
QTextStream *g_out = nullptr;

void logLine(const QString &line)
{
    if (g_out)
    {
        *g_out << line << QLatin1Char('\n');
        g_out->flush();
    }
}

void pump()
{
    for (int i = 0; i < 10; ++i)
        QApplication::processEvents();
}

void waitMs(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

QString oneline(const QString &text)
{
    QString copy = text;
    copy.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (copy.size() > 300)
        copy = copy.left(300) + QStringLiteral("...");
    return copy;
}

QString effectsOf(QWidget *page)
{
    if (!page)
        return QStringLiteral("null");
    return page->graphicsEffect() ? QStringLiteral("effect") : QStringLiteral("none");
}

void shot(QWidget *widget, const QString &name)
{
    widget->grab().save(QStringLiteral(".tmp_ia_selftest/out/%1.png").arg(name));
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    for (const QString &path : {QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                                QStringLiteral("C:/Windows/Fonts/msyhbd.ttc")})
        QFontDatabase::addApplicationFont(path);
    QFont probeFont(QStringLiteral("Microsoft YaHei"));
    probeFont.setPixelSize(13);
    app.setFont(probeFont);

    QDir().mkpath(QStringLiteral(".tmp_ia_selftest/out"));
    QFile report(QStringLiteral(".tmp_ia_selftest/report.txt"));
    report.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream out(&report);
    out.setEncoding(QStringConverter::Utf8);
    g_out = &out;

    logLine(QStringLiteral("=== 实例助手自测 ==="));
    // 记录原始主题，跑完恢复，避免探针把用户设置改成浅色
    const QString originalTheme = ThemeManager::instance()->currentThemeName();
    logLine(QStringLiteral("theme=%1").arg(originalTheme));

    QElapsedTimer buildTimer;
    buildTimer.start();
    auto *window = new InstanceAssistantWindow;
    const qint64 buildMs = buildTimer.elapsed();

    // 模拟真实调用：注入一个不存在的实例路径（走「无 level.dat」的兜底分支）
    window->setInstanceContext(QStringLiteral("D:/BlockBox/C-BlockBox/.tmp_ia_selftest/instance"),
                               QStringLiteral("1.20.4"), QStringLiteral("Forge"));
    window->resize(474, 784);
    window->show();
    waitMs(900);
    pump();

    logLine(QStringLiteral("build_ms=%1 floating=%2 size=%3x%4 min=%5x%6 max=%7x%8")
                .arg(buildMs)
                .arg(window->m_floating)
                .arg(window->width()).arg(window->height())
                .arg(window->minimumWidth()).arg(window->minimumHeight())
                .arg(window->maximumWidth()).arg(window->maximumHeight()));
    logLine(QStringLiteral("nav: buttons=%1 scrollViewportW=%2 containerW=%3 highlight(geom=%4,w=%5)")
                .arg(window->m_navButtons.size())
                .arg(window->m_scrollArea->viewport()->width())
                .arg(window->m_navContainer->width())
                .arg(QStringLiteral("%1,%2").arg(window->m_selectionHighlight->x())
                         .arg(window->m_selectionHighlight->y()))
                .arg(window->m_selectionHighlight->width()));

    // ---- 1. 逐个标签页：截图 + 尺寸 ----
    logLine(QStringLiteral("\n[1] 标签页默认尺寸 474x784"));
    for (int tab = 0; tab < window->m_navButtons.size(); ++tab)
    {
        window->switchToTab(tab);
        waitMs(650);
        pump();
        QWidget *page = window->m_stackedWidget->currentWidget();
        const QSize hint = page ? page->sizeHint() : QSize();
        logLine(QStringLiteral("tab=%1 title=%2 page=%3x%4 hint=%5x%6 effect=%7")
                    .arg(tab)
                    .arg(window->m_navButtons[tab]->text())
                    .arg(page ? page->width() : -1).arg(page ? page->height() : -1)
                    .arg(hint.width()).arg(hint.height())
                    .arg(effectsOf(page)));
        shot(window, QStringLiteral("tab%1_default").arg(tab));
    }

    // 切一圈后检查「离屏不透明特效」是否残留（QGraphicsOpacityEffect 会让整页走离屏渲染）
    int pagesWithEffect = 0;
    for (int i = 0; i < window->m_stackedWidget->count(); ++i)
        if (window->m_stackedWidget->widget(i)->graphicsEffect())
            ++pagesWithEffect;
    logLine(QStringLiteral("pagesWithOpacityEffect=%1 / %2")
                .arg(pagesWithEffect).arg(window->m_stackedWidget->count()));

    // ---- 2. 收起态：每个标签页的标题栏数据 ----
    logLine(QStringLiteral("\n[2] 收起态实时数据"));
    const QSize expandedSize = window->size();
    for (int tab = 0; tab < window->m_navButtons.size(); ++tab)
    {
        window->switchToTab(tab);
        waitMs(300);
        window->setCollapsed(true);
        waitMs(1600);
        pump();
        const QString fps = window->m_statFps->text();
        const QString cpu = window->m_statCpu->text();
        const QString gpu = window->m_statGpu->text();
        const QString mem = window->m_statMem->text();
        logLine(QStringLiteral("tab=%1 collapsed h=%2 hostVisible=%3 labels=%4%5%6%7")
                    .arg(tab).arg(window->height())
                    .arg(window->m_statsHost->isVisible())
                    .arg(window->m_statFps->isVisible() ? 1 : 0)
                    .arg(window->m_statCpu->isVisible() ? 1 : 0)
                    .arg(window->m_statGpu->isVisible() ? 1 : 0)
                    .arg(window->m_statMem->isVisible() ? 1 : 0));
        logLine(QStringLiteral("    fps=%1").arg(oneline(fps)));
        logLine(QStringLiteral("    cpu=%1").arg(oneline(cpu)));
        logLine(QStringLiteral("    gpu=%1").arg(oneline(gpu)));
        logLine(QStringLiteral("    mem=%1").arg(oneline(mem)));
        shot(window, QStringLiteral("collapse_tab%1").arg(tab));

        window->setCollapsed(false);
        waitMs(450);
        pump();
        logLine(QStringLiteral("    restore=%1x%2 (before %3x%4)")
                    .arg(window->width()).arg(window->height())
                    .arg(expandedSize.width()).arg(expandedSize.height()));
    }

    // ---- 3. 快捷指令页：翻译 / 候选 ----
    logLine(QStringLiteral("\n[3] 快捷指令：翻译与候选"));
    window->switchToTab(2);
    waitMs(500);
    const QStringList inputs = {
        QString(),
        QStringLiteral("给"),
        QStringLiteral("给予 @s 钻石剑 1"),
        QStringLiteral("给我 钻石 64"),
        QStringLiteral("传送到 0 64 0"),
        QStringLiteral("时间设置 白天"),
        QStringLiteral("设置时间 夜晚"),
        QStringLiteral("召唤 僵尸 0 64 0"),
        QStringLiteral("游戏模式 创造 @s"),
        QStringLiteral("杀死 @p"),
        QStringLiteral("天气 晴"),
        QStringLiteral("哈哈哈"),
        QStringLiteral("给予 @a minecraft:diamond 1"),
        QStringLiteral("execute as @a run say hi")
    };
    for (const QString &input : inputs)
    {
        window->m_commandInput->setText(input);
        waitMs(260);
        pump();
        const int count = window->m_completionList->count();
        QStringList cands;
        for (int i = 0; i < qMin(3, count); ++i)
            cands << window->m_completionList->item(i)->text();
        logLine(QStringLiteral("input=[%1] output=[%2] copy=%3 inject=%4 cand=%5 [%6]")
                    .arg(oneline(input), oneline(window->m_englishOutput->text()))
                    .arg(window->m_copyBtn->isEnabled())
                    .arg(window->m_injectBtn->isEnabled())
                    .arg(count)
                    .arg(oneline(cands.join(QStringLiteral(" / ")))));
    }

    // 点击第一个候选：应补全/追加到输入框
    window->m_commandInput->setText(QStringLiteral("给予 @s "));
    waitMs(300);
    if (window->m_completionList->count() > 0)
    {
        QListWidgetItem *item = window->m_completionList->item(0);
        logLine(QStringLiteral("click candidate=[%1] insert=%2 before=[%3]")
                    .arg(oneline(item->text()), oneline(item->data(Qt::UserRole).toString()),
                         oneline(window->m_commandInput->text())));
        QMetaObject::invokeMethod(window->m_completionList, "itemClicked",
                                  Q_ARG(QListWidgetItem *, item));
        waitMs(150);
        logLine(QStringLiteral("click candidate after=[%1] output=[%2]")
                    .arg(oneline(window->m_commandInput->text()),
                         oneline(window->m_englishOutput->text())));
    }
    else
    {
        logLine(QStringLiteral("click candidate: list empty!"));
    }

    // 预设：填入后 60ms 防抖不能把英文输出覆盖掉
    const QStringList presetCats = window->m_commandDb->presetCategories();
    if (!presetCats.isEmpty())
    {
        const QList<CommandPreset> presets =
            window->m_commandDb->findPresetsByCategory(presetCats.first());
        if (!presets.isEmpty())
        {
            window->applyPreset(presets.first());
            waitMs(320);
            logLine(QStringLiteral("preset cat=[%1] chinese=[%2] english=[%3] out=[%4] copy=%5")
                        .arg(oneline(presetCats.first()), oneline(presets.first().chinese),
                             oneline(presets.first().english),
                             oneline(window->m_englishOutput->text()))
                        .arg(window->m_copyBtn->isEnabled()));
        }
    }
    else
    {
        logLine(QStringLiteral("preset: no categories"));
    }

    // 复制按钮
    window->m_commandInput->setText(QStringLiteral("给予 @s 苹果 1"));
    waitMs(300);
    window->m_copyBtn->click();
    waitMs(120);
    logLine(QStringLiteral("copy: btnText=[%1] clipboard=[%2]")
                .arg(oneline(window->m_copyBtn->text()),
                     oneline(QApplication::clipboard()->text())));

    // 服务端 / 单机切换
    window->m_serverToggleBtn->setChecked(true);
    waitMs(150);
    logLine(QStringLiteral("server toggle: checked=%1 comboEnabled=%2 op=%3 context=[%4]")
                .arg(window->m_serverToggleBtn->isChecked())
                .arg(window->m_opLevelCombo->isEnabled())
                .arg(window->m_commandContext.opLevel)
                .arg(oneline(window->m_contextLabel->text())));
    window->m_opLevelCombo->setCurrentIndex(2);
    waitMs(150);
    logLine(QStringLiteral("op combo idx=2 op=%1 text=[%2]")
                .arg(window->m_commandContext.opLevel)
                .arg(oneline(window->m_contextLabel->text())));
    window->m_serverToggleBtn->setChecked(false);
    waitMs(150);
    logLine(QStringLiteral("server toggle off: comboEnabled=%1 op=%2 text=[%3]")
                .arg(window->m_opLevelCombo->isEnabled())
                .arg(window->m_commandContext.opLevel)
                .arg(oneline(window->m_contextLabel->text())));
    shot(window, QStringLiteral("command_default"));

    // ---- 4. 最窄 / 最宽尺寸 ----
    logLine(QStringLiteral("\n[4] 尺寸边界"));
    window->resize(window->minimumWidth(), 700);
    waitMs(400);
    pump();
    logLine(QStringLiteral("narrow window=%1x%2 navViewport=%3 container=%4 outputW=%5 "
                           "copyW=%6 injectW=%7 presetW=%8 historyW=%9 packW=%10")
                .arg(window->width()).arg(window->height())
                .arg(window->m_scrollArea->viewport()->width())
                .arg(window->m_navContainer->width())
                .arg(window->m_englishOutput->width())
                .arg(window->m_copyBtn->width())
                .arg(window->m_injectBtn->width())
                .arg(window->m_presetBtn->width())
                .arg(window->m_historyBtn->width())
                .arg(window->m_packBtn->width()));
    shot(window, QStringLiteral("command_narrow"));

    window->resize(window->maximumWidth(), 700);
    waitMs(400);
    pump();
    logLine(QStringLiteral("wide window=%1x%2 navViewport=%3 container=%4")
                .arg(window->width()).arg(window->height())
                .arg(window->m_scrollArea->viewport()->width())
                .arg(window->m_navContainer->width()));
    shot(window, QStringLiteral("command_wide"));

    // ---- 5. 深色主题 ----
    logLine(QStringLiteral("\n[5] 主题"));
    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    waitMs(500);
    window->resize(474, 784);
    waitMs(400);
    shot(window, QStringLiteral("tab2_dark"));
    window->switchToTab(0);
    waitMs(500);
    shot(window, QStringLiteral("tab0_dark"));
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    waitMs(400);
    logLine(QStringLiteral("theme switched dark->light ok"));

    // ---- 6. 隐藏 / 重新显示 / ESC ----
    logLine(QStringLiteral("\n[6] 隐藏与恢复"));
    window->hide();
    waitMs(300);
    logLine(QStringLiteral("after hide: visible=%1 collapsed=%2")
                .arg(window->isVisible()).arg(window->m_collapsed));
    window->show();
    waitMs(400);
    logLine(QStringLiteral("after show: visible=%1 collapsed=%2 size=%3x%4")
                .arg(window->isVisible()).arg(window->m_collapsed)
                .arg(window->width()).arg(window->height()));

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(window, &esc);
    waitMs(200);
    logLine(QStringLiteral("after ESC: visible=%1").arg(window->isVisible()));
    window->show();
    waitMs(300);

    // ---- 7. 多实例窗口 ----
    logLine(QStringLiteral("\n[7] 多开"));
    QElapsedTimer secondTimer;
    secondTimer.start();
    auto *second = new InstanceAssistantWindow;
    const qint64 secondMs = secondTimer.elapsed();
    second->setInstanceContext(QStringLiteral("D:/BlockBox/C-BlockBox/.tmp_ia_selftest/instance2"),
                               QStringLiteral("1.21"), QStringLiteral("Fabric"));
    second->show();
    waitMs(600);
    logLine(QStringLiteral("second window build_ms=%1 visible=%2 title=[%3]")
                .arg(secondMs).arg(second->isVisible()).arg(oneline(second->windowTitle())));
    shot(second, QStringLiteral("second_window"));
    delete second;
    waitMs(200);
    logLine(QStringLiteral("second window deleted ok"));

    // ---- 8. 指令翻译补充用例 ----
    logLine(QStringLiteral("\n[8] 翻译补充用例"));
    window->switchToTab(2);
    waitMs(300);
    const QStringList moreInputs = {
        QStringLiteral("给予 @s 苹果 3"),
        QStringLiteral("传送到 @s 100 64 -100"),
        QStringLiteral("把我传送到 10 70 10"),
        QStringLiteral("传送 @s ~ ~ ~"),
        QStringLiteral("模式 生存 @a"),
        QStringLiteral("设置游戏模式 冒险"),
        QStringLiteral("天气 雨"),
        QStringLiteral("天气 雷暴"),
        QStringLiteral("时间 正午"),
        QStringLiteral("给予 速度 效果"),
        QStringLiteral("召唤 闪电"),
        QStringLiteral("经验 10"),
        QStringLiteral("附魔 锋利 5"),
        QStringLiteral("清空 @s 背包"),
        QStringLiteral("填充 石头"),
        QStringLiteral("设置方块 石头"),
        QStringLiteral("gamerule keepInventory true"),
        QStringLiteral("difficulty hard"),
        QStringLiteral("say hello world"),
        QStringLiteral("/give @s apple 1"),
        QStringLiteral("title @a subtitle"),
        QStringLiteral("setblock 0 64 0 石头")
    };
    for (const QString &input : moreInputs)
    {
        window->m_commandInput->setText(input);
        waitMs(240);
        pump();
        const QString out = window->m_englishOutput->text();
        bool hasCjk = false;
        for (const QChar &ch : out)
            if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF)
            {
                hasCjk = true;
                break;
            }
        logLine(QStringLiteral("input=[%1] output=[%2] cjk=%3 tip=[%4]")
                    .arg(oneline(input), oneline(out))
                    .arg(hasCjk ? 1 : 0)
                    .arg(oneline(window->m_englishOutput->toolTip())));
    }

    // 失败时的提示：输入不认识的中文，看是否有任何解释
    window->m_commandInput->setText(QStringLiteral("哈哈哈"));
    waitMs(250);
    logLine(QStringLiteral("unrecognized: out=[%1] tip=[%2] style=[%3]")
                .arg(oneline(window->m_englishOutput->text()),
                     oneline(window->m_englishOutput->toolTip()),
                     oneline(window->m_englishOutput->styleSheet())));

    // ---- 9. 候选点击 / 重复项 ----
    logLine(QStringLiteral("\n[9] 候选点击与重复"));
    window->m_commandInput->setText(QStringLiteral("给"));
    waitMs(300);
    logLine(QStringLiteral("candidates for [给]: count=%1").arg(window->m_completionList->count()));
    for (int i = 0; i < window->m_completionList->count(); ++i)
    {
        QListWidgetItem *item = window->m_completionList->item(i);
        logLine(QStringLiteral("  #%1 text=[%2] insert=[%3] tip=[%4]")
                    .arg(i)
                    .arg(oneline(item->text()), oneline(item->data(Qt::UserRole).toString()),
                         oneline(item->toolTip())));
    }
    if (window->m_completionList->count() > 0)
    {
        QListWidgetItem *item = window->m_completionList->item(0);
        QMetaObject::invokeMethod(window->m_completionList, "itemClicked",
                                  Q_ARG(QListWidgetItem *, item));
        waitMs(200);
        logLine(QStringLiteral("after click #0: input=[%1] output=[%2] cursor=%3")
                    .arg(oneline(window->m_commandInput->text()),
                         oneline(window->m_englishOutput->text()))
                    .arg(window->m_commandInput->cursorPosition()));
    }

    // ---- 9b. 输入框快捷操作：清空按钮 / ↑↓ 选候选 / 回车复制 / Esc 清空 ----
    logLine(QStringLiteral("\n[9b] 输入框快捷操作"));
    logLine(QStringLiteral("clear button: exists=%1 visible=%2")
                .arg(window->m_clearInputBtn != nullptr)
                .arg(window->m_clearInputBtn && window->m_clearInputBtn->isVisible()));

    window->m_commandInput->setText(QStringLiteral("给予 @s "));
    waitMs(260);
    const int shortcutCandidates = window->m_completionList->count();
    QKeyEvent keyDown(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(window->m_commandInput, &keyDown);
    waitMs(80);
    logLine(QStringLiteral("after Down: candidates=%1 currentRow=%2")
                .arg(shortcutCandidates)
                .arg(window->m_completionList->currentRow()));

    QKeyEvent keyEnter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(window->m_commandInput, &keyEnter);
    waitMs(150);
    logLine(QStringLiteral("after Enter: input=[%1] output=[%2] clipboard=[%3] copyBtn=[%4]")
                .arg(oneline(window->m_commandInput->text()),
                     oneline(window->m_englishOutput->text()),
                     oneline(QApplication::clipboard()->text()),
                     oneline(window->m_copyBtn->text())));

    QKeyEvent keyEsc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(window->m_commandInput, &keyEsc);
    waitMs(120);
    logLine(QStringLiteral("after Esc: input=[%1] output=[%2] windowVisible=%3")
                .arg(oneline(window->m_commandInput->text()),
                     oneline(window->m_englishOutput->text()))
                .arg(window->isVisible()));

    window->m_commandInput->setText(QStringLiteral("给 钻石 一组"));
    waitMs(260);
    logLine(QStringLiteral("quantity: [%1] -> [%2]")
                .arg(oneline(window->m_commandInput->text()),
                     oneline(window->m_englishOutput->text())));

    if (window->m_clearInputBtn)
    {
        window->m_clearInputBtn->click();
        waitMs(200);
        logLine(QStringLiteral("after clear button: input=[%1] output=[%2] candidates=%3")
                    .arg(oneline(window->m_commandInput->text()),
                         oneline(window->m_englishOutput->text()))
                    .arg(window->m_completionList->count()));
    }
    shot(window, QStringLiteral("command_shortcuts"));

    // ---- 10. 历史记录 ----
    logLine(QStringLiteral("\n[10] 历史记录"));
    const QList<QPair<QString, QString>> history =
        SettingsManager::instance()->commandHistory();
    logLine(QStringLiteral("history count=%1").arg(history.size()));
    for (int i = 0; i < qMin(3, history.size()); ++i)
        logLine(QStringLiteral("  #%1 [%2] -> [%3]")
                    .arg(i).arg(oneline(history.at(i).first), oneline(history.at(i).second)));

    // 复制按钮 800ms 后应恢复
    window->m_commandInput->setText(QStringLiteral("给予 @s 苹果 3"));
    waitMs(260);
    window->m_copyBtn->click();
    waitMs(1000);
    logLine(QStringLiteral("copy btn after 1s text=[%1]").arg(oneline(window->m_copyBtn->text())));

    // 未运行游戏时点「一键注入」
    window->m_injectBtn->click();
    waitMs(300);
    logLine(QStringLiteral("inject (no game) btn text=[%1]").arg(oneline(window->m_injectBtn->text())));
    waitMs(1600);
    logLine(QStringLiteral("inject btn after reset text=[%1]").arg(oneline(window->m_injectBtn->text())));

    // ---- 11. 窄窗口状态栏是否被截断 ----
    logLine(QStringLiteral("\n[11] 窄窗口文字截断"));
    window->resize(window->minimumWidth(), 700);
    waitMs(350);
    logLine(QStringLiteral("context label: width=%1 sizeHint=%2 text=[%3]")
                .arg(window->m_contextLabel->width())
                .arg(window->m_contextLabel->sizeHint().width())
                .arg(oneline(window->m_contextLabel->text())));
    logLine(QStringLiteral("title label: width=%1 sizeHint=%2")
                .arg(window->m_titleLabel->width())
                .arg(window->m_titleLabel->sizeHint().width()));
    logLine(QStringLiteral("englishOutput: width=%1 sizeHint=%2 text=[%3]")
                .arg(window->m_englishOutput->width())
                .arg(window->m_englishOutput->sizeHint().width())
                .arg(oneline(window->m_englishOutput->text())));

    // ---- 12. 下拉箭头渲染：独立对照 ----
    logLine(QStringLiteral("\n[12] 下拉框箭头渲染"));
    QWidget arrowHost;
    arrowHost.resize(320, 100);
    auto *comboDefault = new QComboBox(&arrowHost);
    comboDefault->addItems({QStringLiteral("默认箭头")});
    comboDefault->setGeometry(10, 10, 140, 30);
    auto *comboFilter = new QComboBox(&arrowHost);
    comboFilter->setObjectName(QStringLiteral("filterCombo"));
    comboFilter->addItems({QStringLiteral("filterCombo")});
    comboFilter->setGeometry(160, 10, 140, 30);
    arrowHost.show();
    waitMs(400);
    arrowHost.grab().scaled(640, 200, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .save(QStringLiteral(".tmp_ia_selftest/out/combos_dark.png"));
    logLine(QStringLiteral("combo screenshots saved (dark)"));

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    waitMs(400);
    arrowHost.grab().scaled(640, 200, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .save(QStringLiteral(".tmp_ia_selftest/out/combos_light.png"));
    logLine(QStringLiteral("combo screenshots saved (light)"));

    // ---- 12.5 输出提示行截图 ----
    window->resize(474, 784);
    waitMs(350);
    window->m_commandInput->setText(QStringLiteral("天气 晴"));
    waitMs(300);
    shot(window, QStringLiteral("command_valid"));
    window->m_commandInput->setText(QStringLiteral("填充 石头"));
    waitMs(300);
    shot(window, QStringLiteral("command_warning"));

    // ---- 13. 快速切换 / 收起时关闭 ----    logLine(QStringLiteral("\n[13] 压力与收起后关闭"));
    for (int i = 0; i < 240; ++i)
    {
        window->switchToTab(i % window->m_navButtons.size());
        if (i % 5 == 0)
            window->setCollapsed(!window->m_collapsed);
        QApplication::processEvents();
    }
    waitMs(800);
    logLine(QStringLiteral("stress: visible=%1 collapsed=%2 size=%3x%4 currentTab=%5")
                .arg(window->isVisible()).arg(window->m_collapsed)
                .arg(window->width()).arg(window->height())
                .arg(window->m_currentIndex));
    window->setCollapsed(false);
    waitMs(400);

    window->setCollapsed(true);
    waitMs(500);
    logLine(QStringLiteral("collapse before hide: h=%1 collapsed=%2")
                .arg(window->height()).arg(window->m_collapsed));
    window->hide();
    waitMs(400);
    window->show();
    waitMs(500);
    logLine(QStringLiteral("show after collapse+hide: visible=%1 collapsed=%2 size=%3x%4")
                .arg(window->isVisible()).arg(window->m_collapsed)
                .arg(window->width()).arg(window->height()));
    shot(window, QStringLiteral("after_reopen"));

    // 恢复探针改动过的主题，保证不影响用户原本的设置
    if (!originalTheme.isEmpty()
        && originalTheme != ThemeManager::instance()->currentThemeName())
    {
        ThemeManager::instance()->loadTheme(originalTheme);
        waitMs(300);
        logLine(QStringLiteral("theme restored to %1").arg(originalTheme));
    }

    logLine(QStringLiteral("\n=== 完成 ==="));
    report.close();
    return 0;
}

