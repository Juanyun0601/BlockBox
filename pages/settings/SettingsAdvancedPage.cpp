/**
 * @file   SettingsAdvancedPage.cpp
 * @brief  高级设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QFileDialog>
#include "components/CustomCheckBox.h"
#include "components/FileOpStepsDialog.h"
#include "components/OutlinedLabel.h"
#include "platform.h"
#include "utils/LauncherLogger.h"
#include "utils/SettingsManager.h"

namespace {

// 在路径的分隔符/连接符后插入零宽空格，让 QLabel 能在路径中间折行：
// 否则日志路径这种超长“单词”会把设置页主栏的最小宽度顶宽
QString breakablePathText(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    QString result;
    result.reserve(native.size() * 2);
    for (const QChar ch : native) {
        result.append(ch);
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('/') || ch == QLatin1Char('-')
            || ch == QLatin1Char('_') || ch == QLatin1Char('.'))
            result.append(QChar(0x200B));
    }
    return result;
}

} // namespace

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
    jvmPresetCombo->setFixedWidth(160);
    disableWheelEffect(jvmPresetCombo);

    QHBoxLayout *jvmPresetRow = appendSettingRow(jvmParamsCard,
        tr("常用预设"), tr("快速应用经过优化的 JVM 参数组合。"), QString());
    jvmPresetRow->addWidget(jvmPresetCombo);

    QTextEdit *jvmParamsEdit = new QTextEdit();
    jvmParamsEdit->setObjectName("jvmParamsEdit");
    jvmParamsEdit->setPlaceholderText(tr("请输入自定义 JVM 参数，每行一个"));
    jvmParamsEdit->setFixedHeight(120);
    jvmParamsEdit->setAcceptRichText(false);

    QVBoxLayout *jvmParamsBlock = appendSettingBlock(jvmParamsCard,
        tr("自定义 JVM 参数"), tr("手动编辑传递给 Java 虚拟机的参数，每行一个。"),
        tr("JVM参数用于调整Java虚拟机的运行方式，不正确的设置可能导致游戏无法启动或性能下降。建议仅在了解其含义的情况下修改。"),
        true);
    jvmParamsBlock->addWidget(jvmParamsEdit);

    // ── 运行日志 ──
    QVBoxLayout *runtimeLogCard = createSettingsCard(layout, tr("运行日志"));

    CustomCheckBox *runtimeLogCheck = new CustomCheckBox();
    runtimeLogCheck->setChecked(LauncherLogger::isEnabled());

    QHBoxLayout *runtimeLogRow = appendSettingRow(runtimeLogCard,
        tr("记录运行日志"),
        tr("把启动器的启动、下载、安装和报错过程记进日志文件，遇到问题时方便排查。"));
    runtimeLogRow->addWidget(runtimeLogCheck);

    connect(runtimeLogCheck, &CustomCheckBox::toggled, [](bool checked) {
        LauncherLogger::setEnabled(checked);
    });

    CustomCheckBox *verboseLogCheck = new CustomCheckBox();
    verboseLogCheck->setChecked(LauncherLogger::isVerbose());

    QHBoxLayout *verboseLogRow = appendSettingRow(runtimeLogCard,
        tr("记录详细调试信息"),
        tr("额外记下每条日志来自哪个文件、哪个线程，日志更完整，文件也会更大。"));
    verboseLogRow->addWidget(verboseLogCheck);

    connect(verboseLogCheck, &CustomCheckBox::toggled, [](bool checked) {
        LauncherLogger::setVerbose(checked);
    });

    QHBoxLayout *logFileRow = appendSettingRow(runtimeLogCard,
        tr("日志文件"),
        tr("当前日志：%1").arg(breakablePathText(LauncherLogger::logFilePath())),
        tr("反馈问题时把导出的日志一并附上；点「操作步骤」可查看日志里整理出的文件操作过程。"), true);

    QPushButton *openLogDirBtn = new QPushButton(tr("打开文件夹"), m_advancedSettings);
    openLogDirBtn->setObjectName("primaryButton");
    openLogDirBtn->setCursor(Qt::PointingHandCursor);
    openLogDirBtn->setFixedWidth(96);
    openLogDirBtn->setVisible(!Platform::isMobile());
    connect(openLogDirBtn, &QPushButton::clicked, this, []() {
        const QString dir = LauncherLogger::logDirectory();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    logFileRow->addWidget(openLogDirBtn);

    QPushButton *opStepsBtn = new QPushButton(tr("操作步骤"), m_advancedSettings);
    opStepsBtn->setObjectName("primaryButton");
    opStepsBtn->setCursor(Qt::PointingHandCursor);
    opStepsBtn->setFixedWidth(96);
    opStepsBtn->setToolTip(tr("从运行日志整理下载、复制、解压等文件操作步骤，仅供查看"));
    connect(opStepsBtn, &QPushButton::clicked, this, [this]() {
        FileOpStepsDialog dlg(this);
        dlg.exec();
    });
    logFileRow->addWidget(opStepsBtn);

    QPushButton *exportLogBtn = new QPushButton(tr("导出日志"), m_advancedSettings);
    exportLogBtn->setObjectName("primaryButton");
    exportLogBtn->setCursor(Qt::PointingHandCursor);
    exportLogBtn->setFixedWidth(96);
    connect(exportLogBtn, &QPushButton::clicked, this, [this]() {
        const QString defaultName = QStringLiteral("BlockBox_Log_%1.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));

        const QString path = QFileDialog::getSaveFileName(
            this,
            tr("导出运行日志"),
            defaultName,
            tr("日志文件 (*.log);;文本文件 (*.txt);;所有文件 (*)"));
        if (path.isEmpty())
            return;

        QString error;
        if (!LauncherLogger::exportLog(path, &error)) {
            QMessageBox::warning(this, tr("导出运行日志"),
                tr("导出失败：%1").arg(error));
            return;
        }

        QMessageBox::information(this, tr("导出运行日志"),
            tr("运行日志已导出到：\n%1").arg(QDir::toNativeSeparators(path)));
    });
    logFileRow->addWidget(exportLogBtn);

    // ── 启动前进程检查 ──
    QVBoxLayout *processCheckCard = createSettingsCard(layout, tr("启动前进程检查"));

    CustomCheckBox *processCheckCheck = new CustomCheckBox();
    processCheckCheck->setChecked(true);

    QHBoxLayout *processCheckRow = appendSettingRow(processCheckCard,
        tr("启动前检查相关进程"), tr("游戏启动前检测并提示可能冲突的进程。"), QString(), true);
    processCheckRow->addWidget(processCheckCheck);

    // ── 实验性内容 ──
    QVBoxLayout *experimentalCard = createSettingsCard(layout, tr("实验性内容"));

    CustomCheckBox *bedrockCheck = new CustomCheckBox();
    bedrockCheck->setChecked(SettingsManager::instance()->isExperimentalBedrockEnabled());

    QHBoxLayout *bedrockRow = appendSettingRow(experimentalCard,
        tr("基岩版"),
        tr("开启后顶栏会显示 Java 版 / 基岩版 切换入口。"),
        tr("基岩版相关功能仍在开发中，部分页面可能还不完整；关闭后启动器只显示 Java 版。"),
        true);
    bedrockRow->addWidget(bedrockCheck);

    connect(bedrockCheck, &CustomCheckBox::toggled, [](bool checked) {
        SettingsManager::instance()->setExperimentalBedrockEnabled(checked);
    });

    CustomCheckBox *map3dCheck = new CustomCheckBox();
    map3dCheck->setChecked(SettingsManager::instance()->isExperimentalMap3dEnabled());

    QHBoxLayout *map3dRow = appendSettingRow(experimentalCard,
        tr("存档地图立体视图"),
        tr("开启后存档地图页会多一个「3D」开关，把同一片地表按高度立起来看。"),
        tr("立体视图仍在打磨中，大存档拖动视角时可能偶尔掉帧；关闭后地图页只保留俯视图。"),
        true);
    map3dRow->addWidget(map3dCheck);

    connect(map3dCheck, &CustomCheckBox::toggled, [](bool checked) {
        SettingsManager::instance()->setExperimentalMap3dEnabled(checked);
    });

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();
}
