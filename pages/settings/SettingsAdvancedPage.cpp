/**
 * @file   SettingsAdvancedPage.cpp
 * @brief  高级设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"

void SettingsPage::initAdvancedSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_advancedSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    createSettingsHeader(layout, tr("高级设置"));

    QLabel *advancedHintLabel = new QLabel(tr("建议仅高级用户修改"), m_advancedSettings);
    advancedHintLabel->setObjectName("advancedHintLabel");
    layout->addWidget(advancedHintLabel);

    // ── JVM参数 ──
    QVBoxLayout *jvmParamsCard = createSettingsCard(layout, tr("JVM 参数"));

    QComboBox *jvmPresetCombo = new QComboBox();
    jvmPresetCombo->setObjectName("jvmPresetCombo");
    jvmPresetCombo->addItems({tr("默认"), tr("优化GC"), tr("优化内存")});
    disableWheelEffect(jvmPresetCombo);

    QHBoxLayout *jvmPresetRow = appendSettingRow(jvmParamsCard,
        tr("常用预设"), tr("快速应用经过优化的 JVM 参数组合。"), QString());
    jvmPresetRow->addWidget(jvmPresetCombo);

    QTextEdit *jvmParamsEdit = new QTextEdit();
    jvmParamsEdit->setObjectName("jvmParamsEdit");
    jvmParamsEdit->setPlaceholderText(tr("请输入自定义JVM参数，每行一个"));
    jvmParamsEdit->setFixedHeight(120);
    jvmParamsEdit->setAcceptRichText(false);

    QHBoxLayout *jvmParamsRow = appendSettingRow(jvmParamsCard,
        tr("自定义JVM参数"), tr("手动编辑传递给 Java 虚拟机的参数。"),
        tr("JVM参数用于调整Java虚拟机的运行方式，不正确的设置可能导致游戏无法启动或性能下降。建议仅在了解其含义的情况下修改。"),
        true);
    jvmParamsRow->addWidget(jvmParamsEdit, 1);

    // ── 调试日志 ──
    QVBoxLayout *debugLogCard = createSettingsCard(layout, tr("调试日志"));

    CustomCheckBox *debugLogCheck = new CustomCheckBox();
    debugLogCheck->setChecked(false);

    QHBoxLayout *debugLogRow = appendSettingRow(debugLogCard,
        tr("启用调试日志"), tr("记录启动器与游戏的详细运行日志，便于排查问题。"), QString(), true);
    debugLogRow->addWidget(debugLogCheck);

    // ── 启动前进程检查 ──
    QVBoxLayout *processCheckCard = createSettingsCard(layout, tr("启动前进程检查"));

    CustomCheckBox *processCheckCheck = new CustomCheckBox();
    processCheckCheck->setChecked(true);

    QHBoxLayout *processCheckRow = appendSettingRow(processCheckCard,
        tr("启动前检查相关进程"), tr("游戏启动前检测并提示可能冲突的进程。"), QString(), true);
    processCheckRow->addWidget(processCheckCheck);

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();
}
