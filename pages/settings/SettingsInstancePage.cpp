/**
 * @file   SettingsInstancePage.cpp
 * @brief  实例设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "utils/SettingsManager.h"

namespace {

/**
 * @brief 枚举实例文件夹下所有真实安装的实例（versions 下的有效版本目录）
 * @return name(实例名) -> path(实例目录)
 */
QList<QPair<QString, QString>> scanInstalledInstances()
{
    QList<QPair<QString, QString>> result;
    QSet<QString> seen;

    QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    if (folders.isEmpty()) {
        InstanceFolderInfo info;
        info.name = QObject::tr("默认实例文件夹");
        info.path = QCoreApplication::applicationDirPath() + QStringLiteral("/.minecraft");
        folders.append(info);
    }

    for (const InstanceFolderInfo &folder : folders) {
        QDir versionsDir(folder.path + QStringLiteral("/versions"));
        if (!versionsDir.exists())
            continue;

        const QFileInfoList dirs =
            versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &dirInfo : dirs) {
            const QString dirPath = dirInfo.absoluteFilePath();
            if (seen.contains(dirPath))
                continue;

            const QString name = dirInfo.fileName();
            QString jsonPath = dirPath + QLatin1Char('/') + name + QStringLiteral(".json");
            if (!QFile::exists(jsonPath))
                jsonPath = dirPath + QStringLiteral("/version.json");
            if (!QFile::exists(jsonPath))
                continue;

            seen.insert(dirPath);
            result.append(qMakePair(name, dirPath));
        }
    }

    return result;
}

} // namespace

void SettingsPage::initInstanceSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_instanceSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    createSettingsHeader(layout, tr("实例设置"));

    // ── 选择实例 ──
    QVBoxLayout *instanceSelectCard = createSettingsCard(layout, tr("选择实例"));

    QComboBox *instanceCombo = new QComboBox();
    disableWheelEffect(instanceCombo);

    // 填充真实安装的实例（而非模拟数据）
    const QList<QPair<QString, QString>> instances = scanInstalledInstances();
    if (instances.isEmpty()) {
        instanceCombo->addItem(tr("尚未安装实例"), QString());
    } else {
        for (const auto &inst : instances) {
            instanceCombo->addItem(inst.first, inst.second);
        }
    }

    QPushButton *manageInstancesBtn = new QPushButton(tr("管理实例"));
    manageInstancesBtn->setObjectName("manageInstancesBtn");

    QHBoxLayout *instanceSelectRow = appendSettingRow(instanceSelectCard,
        tr("当前实例"), tr("选择要配置的实例。"),
        tr("选择要配置的实例，每个实例可以拥有独立的游戏设置、模组和资源包。切换实例以查看和修改其专属配置。"));
    instanceSelectRow->addWidget(instanceCombo);

    QHBoxLayout *manageRow = appendSettingRow(instanceSelectCard,
        tr("管理实例"), tr("新建、删除或复制实例。"), QString(), true);
    manageRow->addWidget(manageInstancesBtn);

    QPushButton *jumpToInstanceSettingsBtn = new QPushButton(tr("跳转到实例设置"));
    jumpToInstanceSettingsBtn->setObjectName("jumpToInstanceSettingsBtn");

    QHBoxLayout *jumpRow = appendSettingRow(instanceSelectCard,
        tr("实例详细设置"), tr("跳转到当前实例的详细设置页面。"),
        tr("前往实例管理页面的设置标签页，配置 Java 路径、内存、JVM 参数、窗口大小等实例专属选项。"), true);
    jumpRow->addWidget(jumpToInstanceSettingsBtn);

    connect(jumpToInstanceSettingsBtn, &QPushButton::clicked, this, [this, instanceCombo]() {
        QString instancePath = instanceCombo->currentData().toString();
        if (!instancePath.isEmpty()) {
            emit jumpToInstanceSettingsRequested(instancePath);
        }
    });

    // ── 实例设置 ──
    QVBoxLayout *instanceSettingsCard = createSettingsCard(layout, tr("实例设置"));

    CustomCheckBox *instanceAutoUpdateCheck = new CustomCheckBox();
    instanceAutoUpdateCheck->setChecked(true);

    QHBoxLayout *instanceSettingsRow = appendSettingRow(instanceSettingsCard,
        tr("自动更新此实例"), tr("有更新可用时自动下载并安装。"), QString(), true);
    instanceSettingsRow->addWidget(instanceAutoUpdateCheck);

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();
}
