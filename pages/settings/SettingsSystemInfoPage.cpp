/**
 * @file   SettingsSystemInfoPage.cpp
 * @brief  系统信息设置页 — 展示检测到的当前操作系统信息
 * @author BlockBox Team
 */
#include "pages/SettingsPage.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/OutlinedLabel.h"
#include "utils/SystemInfo.h"

void SettingsPage::initSystemInfoSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_systemInfoSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("系统信息"), m_systemInfoSettings);
    titleLabel->setObjectName("sectionTitle");
    layout->addWidget(titleLabel);

    QLabel *subtitleLabel = new QLabel(
        tr("此处显示检测到的当前操作系统信息，启动器会根据系统名自动选择合适的安装类型。"),
        m_systemInfoSettings);
    subtitleLabel->setObjectName("sectionSubtitle");
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    // ── 检测结果卡片 ──
    const SystemInfoData info = SystemInfo::collect();

    QVBoxLayout *infoCard = createSettingsCard(layout, tr("检测结果"));

    QHBoxLayout *osRow = appendSettingRow(infoCard,
        tr("操作系统"), tr("当前运行的系统全名。"));
    QLabel *osValue = new QLabel(info.osFullName.isEmpty() ? tr("未知") : info.osFullName);
    osValue->setObjectName("settingValueLabel");
    osRow->addWidget(osValue);

    QHBoxLayout *nameRow = appendSettingRow(infoCard,
        tr("系统名称"), tr("用于自动选择安装类型的简短系统名。"));
    QLabel *nameValue = new QLabel(info.osName);
    nameValue->setObjectName("settingValueLabel");
    nameRow->addWidget(nameValue);

    QHBoxLayout *versionRow = appendSettingRow(infoCard,
        tr("版本 / 构建"), tr("操作系统内核版本与构建号。"));
    QString versionText = info.kernelVersion;
    if (!info.build.isEmpty())
        versionText += QStringLiteral("  (build %1)").arg(info.build);
    QLabel *versionValue = new QLabel(versionText.isEmpty() ? tr("未知") : versionText);
    versionValue->setObjectName("settingValueLabel");
    versionRow->addWidget(versionValue);

    if (!info.edition.isEmpty()) {
        QHBoxLayout *editionRow = appendSettingRow(infoCard,
            tr("版本类型"), tr("Windows 版本类型（家庭版/专业版等）。"));
        QLabel *editionValue = new QLabel(info.edition);
        editionValue->setObjectName("settingValueLabel");
        editionRow->addWidget(editionValue);
    }

    QHBoxLayout *archRow = appendSettingRow(infoCard,
        tr("CPU 架构"), tr("当前处理器架构。"));
    QLabel *archValue = new QLabel(info.architecture);
    archValue->setObjectName("settingValueLabel");
    archRow->addWidget(archValue);

    // ── 自动选择说明 ──
    QVBoxLayout *hintCard = createSettingsCard(layout, tr("自动选择说明"));

    QHBoxLayout *autoRow = appendSettingRow(hintCard,
        tr("安装类型自动选择"), tr("Windows 系统自动优先选择 Windows 版（mcappx 直链），"
                                   "其它系统自动选择 Android 版（mcapks 网盘链接）。"),
        tr("在基岩版下载页会自动套用该系统对应的安装来源，也可手动切换。"));
    QLabel *autoValue = new QLabel(
        SystemInfo::isWindows() ? tr("当前将自动选择：Windows 版（mcappx）")
                                : tr("当前将自动选择：Android 版（mcapks）"));
    autoValue->setObjectName("settingValueLabel");
    autoRow->addWidget(autoValue);

    // ── 操作 ──
    QHBoxLayout *actionRow = new QHBoxLayout();
    actionRow->addStretch();

    QPushButton *refreshBtn = new QPushButton(tr("重新检测"), m_systemInfoSettings);
    refreshBtn->setObjectName("primaryButton");
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setToolTip(tr("重新收集系统信息并刷新页面"));
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        const SystemInfoData d = SystemInfo::collect();
        AppMessageBox::information(this, tr("系统信息"),
            tr("操作系统: %1\n系统名称: %2\n版本: %3\n架构: %4")
                .arg(d.osFullName, d.osName, d.kernelVersion, d.architecture));
    });
    actionRow->addWidget(refreshBtn);

    layout->addLayout(actionRow);
    layout->addStretch();
}

