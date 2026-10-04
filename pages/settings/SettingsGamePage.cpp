/**
 * @file   SettingsGamePage.cpp
 * @brief  游戏设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include "components/AppFileDialog.h"
#include <QLabel>
#include <QLineEdit>
#include "components/AppMessageBox.h"
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "utils/SettingsManager.h"
#include "utils/GameLauncher.h"
#include "utils/MemoryAllocator.h"

void SettingsPage::initGlobalGameSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_globalGameSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("全局游戏设置"), m_globalGameSettings);
    titleLabel->setObjectName("sectionTitle");
    layout->addWidget(titleLabel);

    SettingsManager *m_settings = SettingsManager::instance();

    // ── 游戏安装路径 ──
    QVBoxLayout *installPathCard = createSettingsCard(layout, tr("游戏安装路径"));
    // parentWidget() 为内容容器，其父级即卡片本体（基岩版模式下整卡隐藏）
    m_gameInstallPathCard = installPathCard->parentWidget()->parentWidget();

    QLineEdit *pathEdit = new QLineEdit();
    pathEdit->setPlaceholderText(tr("请选择游戏安装路径"));
    QPushButton *browseBtn = new QPushButton(tr("浏览"));
    browseBtn->setObjectName("browseBtn");

    QHBoxLayout *pathRow = appendSettingRow(installPathCard,
        tr("游戏安装路径"), tr("所有游戏实例的默认安装位置。"), QString(), true);
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseBtn);

    // ── Java路径 ──
    QVBoxLayout *javaPathCard = createSettingsCard(layout, tr("Java路径"));
    m_gameJavaPathCard = javaPathCard->parentWidget()->parentWidget();

    QComboBox *javaPathCombo = new QComboBox();
    javaPathCombo->setObjectName("javaPathCombo");
    disableWheelEffect(javaPathCombo);

    javaPathCombo->addItem(tr("自动选择合适的Java"), "auto");

    QList<GameLauncher::JavaInfo> javaList = GameLauncher::instance()->findAllJavaInstallations();
    for (const GameLauncher::JavaInfo& javaInfo : javaList) {
        javaPathCombo->addItem(tr("Java %1 (%2)").arg(javaInfo.version).arg(javaInfo.path), javaInfo.path);
    }

    SettingsManager *settings = SettingsManager::instance();
    QString savedJavaPath = settings->getJavaPath();

    int savedIndex = javaPathCombo->findData(savedJavaPath);
    if (savedIndex != -1) {
        javaPathCombo->setCurrentIndex(savedIndex);
    } else {
        javaPathCombo->setCurrentIndex(0);
    }

    QPushButton *javaBrowseBtn = new QPushButton(tr("浏览"));
    javaBrowseBtn->setObjectName("javaBrowseBtn");
    QPushButton *javaRefreshBtn = new QPushButton(tr("刷新列表"));
    javaRefreshBtn->setObjectName("javaRefreshBtn");

    connect(javaPathCombo, &QComboBox::currentIndexChanged, [=](int index) {
        QString selectedJavaPath = javaPathCombo->itemData(index).toString();
        SettingsManager *settings = SettingsManager::instance();
        settings->setJavaPath(selectedJavaPath);
        qDebug() << "[SettingsPage]" << "Saved Java path:" << selectedJavaPath;
    });
    connect(javaBrowseBtn, &QPushButton::clicked, [=]() {
        QString javaPath = AppFileDialog::getOpenFileName(this, tr("选择Java可执行文件"), QCoreApplication::applicationDirPath(), tr("可执行文件 (*.exe)"));
        if (!javaPath.isEmpty()) {
            GameLauncher::JavaInfo javaInfo;
            if (GameLauncher::instance()->validateJavaPath(javaPath, javaInfo)) {
                bool alreadyExists = false;
                for (int i = 1; i < javaPathCombo->count(); ++i) {
                    if (javaPathCombo->itemData(i).toString() == javaPath) {
                        javaPathCombo->setCurrentIndex(i);
                        alreadyExists = true;
                        break;
                    }
                }
                if (!alreadyExists) {
                    javaPathCombo->addItem(tr("Java %1 (%2)").arg(javaInfo.version).arg(javaPath), javaPath);
                    javaPathCombo->setCurrentIndex(javaPathCombo->count() - 1);
                }
            } else {
                AppMessageBox::warning(this, tr("警告"), tr("选择的文件不是有效的Java可执行文件！"));
            }
        }
    });
    connect(javaRefreshBtn, &QPushButton::clicked, [=]() {
        while (javaPathCombo->count() > 1) {
            javaPathCombo->removeItem(1);
        }

        QList<GameLauncher::JavaInfo> javaList = GameLauncher::instance()->findAllJavaInstallations();
        for (const GameLauncher::JavaInfo& javaInfo : javaList) {
            javaPathCombo->addItem(tr("Java %1 (%2)").arg(javaInfo.version).arg(javaInfo.path), javaInfo.path);
        }
    });

    QHBoxLayout *javaPathRow = appendSettingRow(javaPathCard,
        tr("Java路径"), tr("用于启动游戏的 Java 运行时。"), QString());
    javaPathRow->addWidget(javaPathCombo, 1);
    javaPathRow->addWidget(javaBrowseBtn);
    javaPathRow->addWidget(javaRefreshBtn);

    QLabel *javaVersionLabel = new QLabel(tr("检测到的Java版本: 自动选择"));
    javaVersionLabel->setObjectName("javaVersionLabel");

    connect(javaPathCombo, &QComboBox::currentIndexChanged, [=](int index) {
        if (index == 0) {
            javaVersionLabel->setText(tr("检测到的Java版本: 自动选择"));
        } else {
            QString javaPath = javaPathCombo->itemData(index).toString();
            GameLauncher::JavaInfo javaInfo;
            if (GameLauncher::instance()->validateJavaPath(javaPath, javaInfo)) {
                javaVersionLabel->setText(tr("检测到的Java版本: %1").arg(javaInfo.version));
            } else {
                javaVersionLabel->setText(tr("检测到的Java版本: 无效"));
            }
        }
    });

    QHBoxLayout *javaVersionRow = appendSettingRow(javaPathCard,
        QString(), QString(), QString(), true);
    javaVersionRow->addWidget(javaVersionLabel);

    // ── 最大内存分配 ──
    QVBoxLayout *memoryCard = createSettingsCard(layout, tr("最大内存分配"));
    m_gameMemoryCard = memoryCard->parentWidget()->parentWidget();

    CustomCheckBox *autoMemoryCheck = new CustomCheckBox();
    autoMemoryCheck->setChecked(m_settings->isAutoMemoryEnabled());
    autoMemoryCheck->setToolTip(tr("根据系统内存自动计算最优分配值，推荐启用"));

    QHBoxLayout *autoMemoryRow = appendSettingRow(memoryCard,
        tr("自动分配内存"), tr("根据系统内存自动计算最优分配值。"), QString());
    autoMemoryRow->addWidget(autoMemoryCheck);

    QComboBox *modeCombo = new QComboBox();
    modeCombo->setObjectName("memoryModeCombo");
    modeCombo->addItem(tr("普通 — 保守分配，为系统预留更多内存"),
        static_cast<int>(MemoryAllocationMode::Normal));
    modeCombo->addItem(tr("优化 — 均衡分配，兼顾性能与稳定性"),
        static_cast<int>(MemoryAllocationMode::Optimized));
    modeCombo->addItem(tr("极致 — 激进分配，最大化游戏可用内存"),
        static_cast<int>(MemoryAllocationMode::Extreme));
    int currentModeIndex = modeCombo->findData(
        static_cast<int>(m_settings->getMemoryAllocationMode()));
    if (currentModeIndex >= 0)
    {
        modeCombo->setCurrentIndex(currentModeIndex);
    }
    disableWheelEffect(modeCombo);

    QHBoxLayout *modeRow = appendSettingRow(memoryCard,
        tr("分配模式"), tr("选择自动内存分配的策略。"), QString());
    modeRow->addWidget(modeCombo, 1);

    QLabel *autoMemoryInfoLabel = new QLabel();
    autoMemoryInfoLabel->setObjectName("autoMemoryInfoLabel");
    autoMemoryInfoLabel->setWordWrap(true);

    QHBoxLayout *autoMemoryInfoRow = appendSettingRow(memoryCard,
        tr("推荐分配"), tr("系统内存与自动分配推荐值。"));
    autoMemoryInfoRow->addWidget(autoMemoryInfoLabel, 1);

    QSlider *memorySlider = new QSlider(Qt::Horizontal);
    memorySlider->setObjectName("memorySlider");
    memorySlider->setRange(SettingsManager::MIN_MEMORY_MB, SettingsManager::MAX_MEMORY_MB);
    memorySlider->setValue(m_settings->getMaxMemory());
    disableWheelEffect(memorySlider);

    QLabel *memoryLabel = new QLabel(tr("当前内存: %1GB").arg(m_settings->getMaxMemory() / 1024));
    QLabel *memoryHintLabel = new QLabel(tr("推荐4GB-8GB"));
    memoryHintLabel->setObjectName("memoryHintLabel");

    // 获取当前模式并计算推荐值
    auto getCurrentMode = [modeCombo]() -> MemoryAllocationMode {
        return static_cast<MemoryAllocationMode>(
            modeCombo->currentData().toInt());
    };

    // 更新自动内存信息标签
    auto updateAutoMemoryInfo = [=]() {
        MemoryAllocationMode mode = getCurrentMode();
        MemoryAllocation alloc = MemoryAllocator::instance()->calculateRecommendedAllocation(mode);
        autoMemoryInfoLabel->setText(
            tr("系统总内存: %1 GB | 可用内存: %2 GB | 推荐分配: %3 GB ~ %4 GB")
                .arg(alloc.totalSystemMemoryMb / 1024)
                .arg(alloc.availableMemoryMb / 1024)
                .arg(alloc.minMemoryMb / 1024)
                .arg(alloc.maxMemoryMb / 1024));
    };
    updateAutoMemoryInfo();

    // 重新计算并更新滑块和标签
    auto recalcAndUpdate = [=]() {
        MemoryAllocationMode mode = getCurrentMode();
        MemoryAllocation alloc = MemoryAllocator::instance()->calculateRecommendedAllocation(mode);
        memorySlider->setValue(alloc.maxMemoryMb);
        memoryLabel->setText(tr("当前内存: %1GB (自动)").arg(alloc.maxMemoryMb / 1024));
        m_settings->setMaxMemory(alloc.maxMemoryMb);
        m_settings->setMinMemory(alloc.minMemoryMb);
        m_settings->setMemoryAllocationMode(mode);
        updateAutoMemoryInfo();
    };

    // 模式切换
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        [=](int /*index*/) {
            if (autoMemoryCheck->isChecked())
            {
                recalcAndUpdate();
            }
        });

    // 自动分配开关逻辑
    connect(autoMemoryCheck, &CustomCheckBox::toggled, [=](bool checked) {
        m_settings->setAutoMemoryEnabled(checked);
        memorySlider->setEnabled(!checked);
        modeCombo->setEnabled(checked);
        if (checked) {
            recalcAndUpdate();
        } else {
            memoryLabel->setText(tr("当前内存: %1GB").arg(memorySlider->value() / 1024));
        }
        updateAutoMemoryInfo();
    });

    connect(memorySlider, &QSlider::valueChanged, [=](int value) {
        if (!autoMemoryCheck->isChecked()) {
            memoryLabel->setText(tr("当前内存: %1GB").arg(value / 1024));
            m_settings->setMaxMemory(value);
            m_settings->setMinMemory(qMin(SettingsManager::DEFAULT_MIN_MEMORY_MB, value));
        }
    });

    // 初始状态: 根据 auto 开关设置 slider 和 mode combo 启用状态
    memorySlider->setEnabled(!autoMemoryCheck->isChecked());
    modeCombo->setEnabled(autoMemoryCheck->isChecked());
    if (autoMemoryCheck->isChecked()) {
        memoryLabel->setText(tr("当前内存: %1GB (自动)").arg(memorySlider->value() / 1024));
    }

    QHBoxLayout *memorySliderRow = appendSettingRow(memoryCard,
        tr("最大内存"), tr("调整游戏可使用的最大内存。"),
        tr("为游戏分配更多内存可以提高性能，但不应超过系统可用内存的80%。推荐值为4GB-8GB。"), true);
    memorySliderRow->addWidget(memorySlider, 1);
    memorySliderRow->addWidget(memoryLabel);
    memorySliderRow->addWidget(memoryHintLabel);

    // ── 游戏窗口尺寸 ──
    QVBoxLayout *windowSizeCard = createSettingsCard(layout, tr("游戏窗口尺寸"));

    QLineEdit *widthEdit = new QLineEdit("1280");
    widthEdit->setFixedWidth(80);
    QLineEdit *heightEdit = new QLineEdit("720");
    heightEdit->setFixedWidth(80);

    QHBoxLayout *windowSizeRow = appendSettingRow(windowSizeCard,
        tr("游戏窗口尺寸"), tr("以像素为单位设置启动时的窗口大小。"), QString(), true);
    windowSizeRow->addWidget(new QLabel(tr("宽")));
    windowSizeRow->addWidget(widthEdit);
    windowSizeRow->addWidget(new QLabel(tr("高")));
    windowSizeRow->addWidget(heightEdit);

    // ── 版本隔离 ──
    QVBoxLayout *isolationCard = createSettingsCard(layout, tr("版本隔离"));

    CustomCheckBox *isolationCheck = new CustomCheckBox();
    isolationCheck->setChecked(m_settings->isVersionIsolationEnabled());
    isolationCheck->setToolTip(tr("启用后，每个实例将使用独立的游戏文件，避免版本冲突。"));
    connect(isolationCheck, &CustomCheckBox::toggled, [=](bool checked) {
        m_settings->setVersionIsolationEnabled(checked);
    });

    QHBoxLayout *isolationRow = appendSettingRow(isolationCard,
        tr("版本隔离"), tr("每个实例使用独立的游戏文件，避免版本冲突。"), QString(), true);
    isolationRow->addWidget(isolationCheck);

    // ── 底部操作 ──
    QPushButton *restoreDefaultsBtn = new QPushButton(tr("恢复默认值"), m_globalGameSettings);
    restoreDefaultsBtn->setObjectName("restoreDefaultsBtn");
    connect(restoreDefaultsBtn, &QPushButton::clicked, this, &SettingsPage::onRestoreDefaults);

    layout->addStretch();
    layout->addWidget(restoreDefaultsBtn, 0, Qt::AlignRight);
}
