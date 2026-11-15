/**
 * @file   lobby_join_prep_probe.cpp
 * @brief  「一键加入准备」对话框探针：真实对话框 + 假计划数据，截图核对版式与默认勾选
 *
 * 不启动 MainWindow，也不联网：直接用构造出来的 JoinPlan 打开对话框，核对
 *   1. 有缺失内容时各分组的展开 / 默认勾选是否正确（必装 & 资源包默认勾、辅助 & 光影默认不勾）；
 *   2. 切换实例下拉框后，勾选清单会跟着换成那个实例缺的东西；
 *   3. 本机没有可用实例时只显示「先去安装对应版本」，其余按钮隐藏；
 *   4. 亮 / 暗主题下文字与按钮都可读，样式表里的 @TOKEN@ 全部解析掉。
 *
 * 输出：.tmp_joinprep_probe/probe_out/*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

#include <cstdio>

#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#define private public
#include "components/LobbyJoinPrepDialog.h"
#undef private

namespace
{
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-58s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

bool savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return false;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(), shot.height(),
            ok ? "ok" : "FAILED");
    return ok;
}

LobbyInstanceMatcher::InstanceCheck makeCheck(const QString &id, const QString &version,
                                             const QString &loader,
                                             const QStringList &missingMods,
                                             const QStringList &missingAux,
                                             const QStringList &missingPacks,
                                             const QStringList &missingShaders,
                                             int score)
{
    LobbyInstanceMatcher::InstanceCheck check;
    check.instance.id = id;
    check.instance.path = QStringLiteral("D:/fake/instances/") + id;
    check.instance.version = version;
    check.instance.loader = loader;
    check.level = missingMods.isEmpty() ? LobbyInstanceMatcher::MatchLevel::Mods
                                        : LobbyInstanceMatcher::MatchLevel::Loader;
    check.score = score;
    check.missingMods = missingMods;
    check.missingAuxMods = missingAux;
    check.missingResourcePacks = missingPacks;
    check.missingShaderPacks = missingShaders;
    return check;
}

LobbyInstanceMatcher::JoinPlan makePlan(bool found)
{
    LobbyInstanceMatcher::JoinPlan plan;
    plan.requirement = LobbyInstanceMatcher::requirementFor(
        QStringLiteral("1.20.1"), QStringLiteral("Fabric"),
        {QStringLiteral("sodium-fabric-0.5.11+mc1.20.1.jar"),
         QStringLiteral("lithium-fabric-0.12.1.jar"),
         QStringLiteral("fabric-api-0.92.2+1.20.1.jar")},
        {QStringLiteral("minimap-fabric-1.20.1.jar")},
        {QStringLiteral("Faithful-32x.zip")}, {QStringLiteral("Complementary-5.2.zip")});
    plan.found = found;
    if (found)
    {
        plan.best = makeCheck(QStringLiteral("1.20.1-fabric"),
                              QStringLiteral("1.20.1"), QStringLiteral("Fabric"),
                              {QStringLiteral("lithium-fabric-0.12.1.jar"),
                               QStringLiteral("fabric-api-0.92.2+1.20.1.jar")},
                              {QStringLiteral("minimap-fabric-1.20.1.jar")},
                              {QStringLiteral("Faithful-32x.zip")},
                              {QStringLiteral("Complementary-5.2.zip")}, 1500);
        plan.candidates.append(plan.best);
        plan.candidates.append(makeCheck(QStringLiteral("1.20.1-forge"),
                                         QStringLiteral("1.20.1"), QStringLiteral("Forge"),
                                         {QStringLiteral("sodium-fabric-0.5.11+mc1.20.1.jar")},
                                         {}, {}, {}, 1200));
    }
    else
    {
        plan.installHint =
            QStringLiteral("本机没有 1.20.1 + Fabric 的实例，装一个对应版本后再加入大厅房间。");
    }
    return plan;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // offscreen / 无字体环境下中文会画成方框，探针里手动挂一份系统字体
    for (const QString &fontFile : {QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                                    QStringLiteral("C:/Windows/Fonts/simhei.ttf")})
    {
        if (QFile::exists(fontFile))
        {
            QFontDatabase::addApplicationFont(fontFile);
            QFont font = app.font();
            font.setFamily(QStringLiteral("Microsoft YaHei"));
            app.setFont(font);
            break;
        }
    }

    const QString outDir = QStringLiteral(".tmp_joinprep_probe/probe_out");
    QDir().mkpath(outDir);

    ThemeManager::setTheme(&app, ThemeManager::LightTheme);
    QWidget host;
    host.resize(1100, 820);
    host.show();

    // ── 1. 有缺失内容：必装 / 辅助 / 资源包 / 光影四个分组 ──
    LobbyJoinPrepDialog dialog(&host);
    dialog.setPlan(makePlan(true), QStringLiteral("周末生存服"), QStringLiteral("BB-2048"));
    dialog.adjustSize();
    dialog.show();
    QApplication::processEvents();

    expect("beimod group visible", dialog.m_requiredGroup && dialog.m_requiredGroup->isVisible());
    expect("aux group visible", dialog.m_auxGroup && dialog.m_auxGroup->isVisible());
    expect("resource pack group visible", dialog.m_packGroup && dialog.m_packGroup->isVisible());
    expect("shader group visible", dialog.m_shaderGroup && dialog.m_shaderGroup->isVisible());
    expect("required checkboxes = 2", dialog.m_requiredBoxes.size() == 2);
    expect("required checked by default",
           dialog.m_requiredBoxes.size() == 2 && dialog.m_requiredBoxes.at(0)->isChecked()
               && dialog.m_requiredBoxes.at(1)->isChecked());
    expect("aux unchecked by default",
           dialog.m_auxBoxes.size() == 1 && !dialog.m_auxBoxes.at(0)->isChecked());
    expect("resource pack checked by default",
           dialog.m_packBoxes.size() == 1 && dialog.m_packBoxes.at(0)->isChecked());
    expect("shader unchecked by default",
           dialog.m_shaderBoxes.size() == 1 && !dialog.m_shaderBoxes.at(0)->isChecked());
    expect("instance combo has 2 candidates", dialog.m_instanceCombo->count() == 2);
    expect("selected requests = 3 (2 mods + 1 pack)", dialog.selectedRequests().size() == 3);
    expect("install button hidden when instance found", !dialog.m_installButton->isVisible());
    expect("download button enabled", dialog.m_downloadButton->isEnabled());
    expect("no unresolved @TOKEN@ in style", !dialog.m_downloadButton->styleSheet().contains(QLatin1Char('@')));
    savePng(&dialog, outDir + QStringLiteral("/joinprep_wide_light.png"));

    // ── 2. 换实例：清单换成第二个实例缺的东西 ──
    dialog.m_instanceCombo->setCurrentIndex(1);
    QApplication::processEvents();
    expect("switch instance -> 1 required box", dialog.m_requiredBoxes.size() == 1);
    expect("switch instance -> no aux box", dialog.m_auxBoxes.isEmpty());
    savePng(&dialog, outDir + QStringLiteral("/joinprep_second_instance.png"));

    // ── 3. 没有可用实例：只留安装按钮 ──
    dialog.m_instanceCombo->setCurrentIndex(0);
    LobbyJoinPrepDialog noInstance(&host);
    noInstance.setPlan(makePlan(false), QStringLiteral("周末生存服"), QString());
    noInstance.adjustSize();
    noInstance.show();
    QApplication::processEvents();
    expect("install button visible when no instance", noInstance.m_installButton->isVisible());
    expect("skip button hidden when no instance", !noInstance.m_skipButton->isVisible());
    expect("download disabled when no instance", !noInstance.m_downloadButton->isEnabled());
    expect("instance hint has install text",
           !noInstance.m_instanceHint->text().trimmed().isEmpty());
    savePng(&noInstance, outDir + QStringLiteral("/joinprep_no_instance.png"));

    // ── 4. 暗色主题 ──
    ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
    dialog.applyStyles();
    QApplication::processEvents();
    savePng(&dialog, outDir + QStringLiteral("/joinprep_wide_dark.png"));
    ThemeManager::setTheme(&app, ThemeManager::LightTheme);
    dialog.applyStyles();
    QApplication::processEvents();

    fprintf(stderr, "[probe] failures = %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
