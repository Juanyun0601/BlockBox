/**
 * @file   SettingsAdvancedPage.cpp
 * @brief  高级设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QCoreApplication>
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
#include "components/AppMessageBox.h"
#include "components/CustomCheckBox.h"
#include "components/FileOpStepsDialog.h"
#include "components/NotificationManager.h"
#include "components/OutlinedLabel.h"
#include "components/UserAgreementDialog.h"
#include "platform.h"
#include "utils/CrashReporter.h"
#include "utils/LauncherLogger.h"
#include "utils/SettingsManager.h"
#include "utils/Telemetry.h"

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
            AppMessageBox::warning(this, tr("导出运行日志"),
                tr("导出失败：%1").arg(error));
            return;
        }

        AppMessageBox::information(this, tr("导出运行日志"),
            tr("运行日志已导出到：\n%1").arg(QDir::toNativeSeparators(path)));
    });
    logFileRow->addWidget(exportLogBtn);

    // ── 诊断与统计（崩溃自动上报 / 匿名使用统计）──
    QVBoxLayout *diagnosticsCard = createSettingsCard(layout, tr("诊断与统计"));

    CustomCheckBox *autoCrashCheck = new CustomCheckBox();
    autoCrashCheck->setChecked(CrashReporter::autoSendEnabled());
    QHBoxLayout *autoCrashRow = appendSettingRow(diagnosticsCard,
        tr("自动发送崩溃报告"),
        tr("方块盒子意外闪退后，下次启动自动把调用栈和出事前的最后一段日志发给我们，不用每次手动确认。"),
        tr("报告只包含版本、系统、调用栈和运行日志，上传前会自动隐藏用户名、文件路径、IP 和登录令牌，"
           "不包含账号、存档、聊天内容。"),
        true);
    autoCrashRow->addWidget(autoCrashCheck);
    connect(autoCrashCheck, &CustomCheckBox::toggled, [](bool checked) {
        CrashReporter::setAutoSendEnabled(checked);
        if (checked)
            CrashReporter::setSendConsent(true);
    });

    CustomCheckBox *telemetryCheck = new CustomCheckBox();
    telemetryCheck->setChecked(Telemetry::isEnabled());
    QHBoxLayout *telemetryRow = appendSettingRow(diagnosticsCard,
        tr("匿名使用统计"),
        tr("只上报启动次数、使用时长、版本 / 系统分布和各页面的访问次数，帮我们判断哪些功能值得优先优化。"),
        tr("默认关闭。开启后也不会上传账号、实例名、存档、聊天或任何文件内容；可以随时关闭，关闭后立即停止并清空本地待发送数据。"),
        true);
    telemetryRow->addWidget(telemetryCheck);
    connect(telemetryCheck, &CustomCheckBox::toggled, [](bool checked) {
        Telemetry::setEnabled(checked);
    });

    // 本机匿名标识：只用于把同一次崩溃 / 同一天的统计归到一起，可随时重置
    QLabel *installIdLabel = new QLabel(CrashReporter::installId(), m_advancedSettings);
    installIdLabel->setObjectName("diagnosticsInstallIdLabel");
    installIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    installIdLabel->setStyleSheet(QStringLiteral("color: #8a8a92; font-size: 11.5px;"));

    QHBoxLayout *installIdRow = appendSettingRow(diagnosticsCard,
        tr("本机匿名标识"),
        tr("一串随机字符，仅用于把同一天的上报归到一起；不关联账号，随时可以换新的。"),
        QString(), true);
    installIdRow->addWidget(installIdLabel);

    QPushButton *resetIdBtn = new QPushButton(tr("重置"), m_advancedSettings);
    resetIdBtn->setObjectName("primaryButton");
    resetIdBtn->setCursor(Qt::PointingHandCursor);
    resetIdBtn->setFixedWidth(72);
    connect(resetIdBtn, &QPushButton::clicked, this, [this, installIdLabel]() {
        const auto answer = AppMessageBox::question(
            this, tr("重置匿名标识"),
            tr("重置后会换一个新的随机标识，之前的统计不会再和这台设备关联。确定重置吗？"));
        if (answer != AppMessageBox::Yes)
            return;
        installIdLabel->setText(CrashReporter::resetInstallId());
        NotificationManager::showInfo(this, tr("已换用新的匿名标识"));
    });
    installIdRow->addWidget(resetIdBtn);

    // 崩溃记录：查看本地留下的报告 / 立即补发 / 打开文件夹
    QLabel *crashPendingLabel = new QLabel(m_advancedSettings);
    crashPendingLabel->setObjectName("diagnosticsCrashPendingLabel");
    crashPendingLabel->setStyleSheet(QStringLiteral("color: #8a8a92; font-size: 11.5px;"));
    auto refreshCrashPending = [crashPendingLabel]() {
        const int count = CrashReporter::instance()->pendingCount();
        crashPendingLabel->setText(count > 0
            ? QCoreApplication::translate("SettingsPage", "有 %1 份崩溃报告还没发送").arg(count)
            : QCoreApplication::translate("SettingsPage", "没有待发送的崩溃报告"));
    };
    refreshCrashPending();

    QHBoxLayout *crashRecordRow = appendSettingRow(diagnosticsCard,
        tr("崩溃记录"),
        tr("本地保存的崩溃报告只在这台电脑上，发送与否由你决定。"),
        QString(), true);
    crashRecordRow->addWidget(crashPendingLabel);

    QPushButton *sendCrashBtn = new QPushButton(tr("立即发送"), m_advancedSettings);
    sendCrashBtn->setObjectName("primaryButton");
    sendCrashBtn->setCursor(Qt::PointingHandCursor);
    sendCrashBtn->setFixedWidth(88);
    connect(sendCrashBtn, &QPushButton::clicked, this, [this, refreshCrashPending]() {
        if (CrashReporter::instance()->pendingCount() <= 0) {
            NotificationManager::showInfo(this, tr("没有待发送的崩溃报告"));
            return;
        }
        CrashReporter::setSendConsent(true);
        CrashReporter::instance()->uploadPendingReports(
            [this, refreshCrashPending](int okCount, int failedCount) {
                refreshCrashPending();
                if (failedCount == 0) {
                    NotificationManager::showInfo(
                        this, tr("崩溃报告已发送，感谢反馈（%1 份）").arg(okCount));
                } else {
                    NotificationManager::showError(
                        this, tr("有 %1 份崩溃报告暂时没发出去，已保留在本地").arg(failedCount));
                }
            });
    });
    crashRecordRow->addWidget(sendCrashBtn);

    QPushButton *openCrashDirBtn = new QPushButton(tr("打开文件夹"), m_advancedSettings);
    openCrashDirBtn->setObjectName("primaryButton");
    openCrashDirBtn->setCursor(Qt::PointingHandCursor);
    openCrashDirBtn->setFixedWidth(96);
    openCrashDirBtn->setVisible(!Platform::isMobile());
    connect(openCrashDirBtn, &QPushButton::clicked, this, []() {
        const QString dir = CrashReporter::instance()->crashDirectory();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    crashRecordRow->addWidget(openCrashDirBtn);

    connect(CrashReporter::instance(), &CrashReporter::pendingCountChanged,
            crashPendingLabel, [refreshCrashPending](int) { refreshCrashPending(); });

    QPushButton *privacyBtn = new QPushButton(tr("隐私政策"), m_advancedSettings);
    privacyBtn->setObjectName("primaryButton");
    privacyBtn->setCursor(Qt::PointingHandCursor);
    privacyBtn->setFixedWidth(96);
    connect(privacyBtn, &QPushButton::clicked, this, [this]() {
        UserAgreementDialog::showDocument(UserAgreementDialog::PrivacyPolicy, this);
    });
    diagnosticsCard->addWidget(privacyBtn, 0, Qt::AlignLeft);

    // ── 启动前进程检查 ──
    QVBoxLayout *processCheckCard = createSettingsCard(layout, tr("启动前进程检查"));

    CustomCheckBox *processCheckCheck = new CustomCheckBox();
    processCheckCheck->setChecked(true);

    QHBoxLayout *processCheckRow = appendSettingRow(processCheckCard,
        tr("启动前检查相关进程"), tr("游戏启动前检测并提示可能冲突的进程。"), QString(), true);
    processCheckRow->addWidget(processCheckCheck);

    // ── 第三方登录（外置登录注入）──
    QVBoxLayout *authlibCard = createSettingsCard(layout, tr("第三方登录"));

    CustomCheckBox *authlibCheck = new CustomCheckBox();
    authlibCheck->setChecked(SettingsManager::instance()->isAuthlibInjectorEnabled());

    QHBoxLayout *authlibRow = appendSettingRow(authlibCard,
        tr("游戏内自动注入外置登录"),
        tr("用皮肤站等第三方账户启动游戏时自动加载外置登录组件，游戏内能正确识别身份、显示皮肤。"),
        tr("首次启动会自动下载约 300KB 的组件。关闭后第三方账户按离线身份进入游戏，认证服务器可能拒绝登录。"),
        true);
    authlibRow->addWidget(authlibCheck);

    connect(authlibCheck, &CustomCheckBox::toggled, [](bool checked) {
        SettingsManager::instance()->setAuthlibInjectorEnabled(checked);
    });

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
