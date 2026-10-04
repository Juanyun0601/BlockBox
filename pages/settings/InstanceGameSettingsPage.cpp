/**
 * @file   InstanceGameSettingsPage.cpp
 * @brief  单个实例游戏设置页实现
 * @author BlockBox Team
 * @date   2026-06-19
 */
#include "InstanceGameSettingsPage.h"

#include <QCoreApplication>
#include "components/AppFileDialog.h"
#include "components/CustomCheckBox.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include "components/AppMessageBox.h"
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolTip>
#include <QVBoxLayout>

#include "utils/GameLauncher.h"
#include "utils/SettingsManager.h"
#include "components/CollapsibleSectionCard.h"

InstanceGameSettingsPage::InstanceGameSettingsPage(QWidget *parent)
    : QWidget(parent)
    , m_instanceNameEdit(nullptr)
    , m_gameVersionLabel(nullptr)
    , m_loaderLabel(nullptr)
    , m_iconPreviewLabel(nullptr)
    , m_javaPathCombo(nullptr)
    , m_javaVersionLabel(nullptr)
    , m_maxMemorySlider(nullptr)
    , m_maxMemoryLabel(nullptr)
    , m_minMemoryEdit(nullptr)
    , m_jvmArgsEdit(nullptr)
    , m_windowWidthEdit(nullptr)
    , m_windowHeightEdit(nullptr)
    , m_fullscreenCheck(nullptr)
    , m_priorityUpgradeCheck(nullptr)
    , m_cautiousUpgradeCheck(nullptr)
{
    initUI();
}

InstanceGameSettingsPage::~InstanceGameSettingsPage()
{
    saveSettings();
}

void InstanceGameSettingsPage::setInstancePath(const QString &instancePath)
{
    if (m_instancePath == instancePath)
    {
        return;
    }
    saveSettings();
    m_instancePath = instancePath;
    loadSettings();
}

void InstanceGameSettingsPage::setInstanceName(const QString &name)
{
    m_instanceName = name;
    updateIconPreview();
    if (m_instanceNameEdit)
    {
        m_instanceNameEdit->setText(name);
    }
}

void InstanceGameSettingsPage::setInstanceIcon(const QString &iconPath)
{
    m_iconPath = iconPath;
    updateIconPreview();
}

void InstanceGameSettingsPage::chooseInstanceIcon()
{
    if (m_instancePath.isEmpty())
        return;
    QString iconFile = AppFileDialog::getOpenFileName(
        this, tr("选择实例图标"),
        m_instancePath, tr("图片文件 (*.png *.jpg *.jpeg *.ico *.webp *.svg)"));
    if (iconFile.isEmpty())
        return;

    // 将图标复制到实例目录下，保持图标随实例携带
    QDir dir(m_instancePath);
    if (!dir.exists())
        dir.mkpath(".");
    QFileInfo srcInfo(iconFile);
    QString ext = srcInfo.suffix().toLower();
    QString dstPath = m_instancePath + "/instanceIcon" + (ext.isEmpty() ? QString(".png") : QString("." + ext));
    QFile::remove(dstPath);
    if (QFile::copy(iconFile, dstPath))
    {
        m_iconPath = dstPath;
    }
    else
    {
        // 复制失败（如跨盘/权限问题），直接使用源文件路径
        m_iconPath = iconFile;
    }
    SettingsManager::instance()->setProperty("instance/" + m_instancePath + "/iconPath", m_iconPath);
    updateIconPreview();
    emit instanceInfoChanged(m_instanceNameEdit ? m_instanceNameEdit->text() : m_instanceName, m_iconPath);
}

void InstanceGameSettingsPage::resetInstanceIcon()
{
    if (m_instancePath.isEmpty())
        return;
    // 删除复制到实例目录的图标文件
    if (m_iconPath.startsWith(m_instancePath))
    {
        QFile::remove(m_iconPath);
    }
    m_iconPath.clear();
    SettingsManager::instance()->removeProperty("instance/" + m_instancePath + "/iconPath");
    updateIconPreview();
    emit instanceInfoChanged(m_instanceNameEdit ? m_instanceNameEdit->text() : m_instanceName, QString());
}

void InstanceGameSettingsPage::updateIconPreview()
{
    if (!m_iconPreviewLabel)
        return;
    if (!m_iconPath.isEmpty() && QFile::exists(m_iconPath))
    {
        QPixmap pm(m_iconPath);
        if (!pm.isNull())
        {
            m_iconPreviewLabel->setPixmap(pm.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_iconPreviewLabel->setText(QString());
            return;
        }
    }
    m_iconPreviewLabel->setText(m_instanceName.isEmpty() ? tr("?") : m_instanceName.left(1).toUpper());
}

void InstanceGameSettingsPage::setGameVersion(const QString &version)
{
    m_gameVersion = version;
    if (m_gameVersionLabel)
    {
        m_gameVersionLabel->setText(version);
    }
}

void InstanceGameSettingsPage::setLoaderInfo(const QString &loader)
{
    m_loaderInfo = loader;
    if (m_loaderLabel)
    {
        m_loaderLabel->setText(loader.isEmpty() ? tr("原版") : loader);
    }
}

QString InstanceGameSettingsPage::javaPath() const
{
    if (!m_javaPathCombo || m_javaPathCombo->currentIndex() <= 0)
    {
        return QString();
    }
    return m_javaPathCombo->currentData().toString();
}

int InstanceGameSettingsPage::maxMemory() const
{
    if (!m_maxMemorySlider)
    {
        return SettingsManager::DEFAULT_MAX_MEMORY_MB;
    }
    return m_maxMemorySlider->value();
}

int InstanceGameSettingsPage::minMemory() const
{
    if (!m_minMemoryEdit)
    {
        return SettingsManager::DEFAULT_MIN_MEMORY_MB;
    }
    bool ok = false;
    int value = m_minMemoryEdit->text().toInt(&ok);
    return ok ? value : SettingsManager::DEFAULT_MIN_MEMORY_MB;
}

QString InstanceGameSettingsPage::jvmArgs() const
{
    if (!m_jvmArgsEdit)
    {
        return QString();
    }
    return m_jvmArgsEdit->text();
}

QString InstanceGameSettingsPage::windowWidth() const
{
    if (!m_windowWidthEdit)
    {
        return "1280";
    }
    return m_windowWidthEdit->text();
}

QString InstanceGameSettingsPage::windowHeight() const
{
    if (!m_windowHeightEdit)
    {
        return "720";
    }
    return m_windowHeightEdit->text();
}

bool InstanceGameSettingsPage::fullscreen() const
{
    if (!m_fullscreenCheck)
    {
        return false;
    }
    return m_fullscreenCheck->isChecked();
}

int InstanceGameSettingsPage::upgradeMode() const
{
    if (m_priorityUpgradeCheck && m_priorityUpgradeCheck->isChecked())
        return 0; // 保守升级
    if (m_cautiousUpgradeCheck && m_cautiousUpgradeCheck->isChecked())
        return 1; // 抢先升级
    return 0; // 默认保守升级
}

QVBoxLayout *InstanceGameSettingsPage::createSettingsCard(QVBoxLayout *parentLayout,
                                                          const QString &title,
                                                          bool startCollapsed)
{
    auto *card = new CollapsibleSectionCard(title, startCollapsed);
    parentLayout->addWidget(card);
    return card->contentLayout();
}

QHBoxLayout *InstanceGameSettingsPage::appendSettingRow(QVBoxLayout *cardLayout,
                                                        const QString &title,
                                                        const QString &desc,
                                                        const QString &help,
                                                        bool isLast)
{
    QFrame *row = new QFrame();
    row->setObjectName("settingRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (isLast)
    {
        row->setProperty("lastRow", true);
    }

    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 16, 24, 16);
    rowLayout->setSpacing(20);

    QWidget *info = new QWidget(row);
    info->setObjectName("settingInfo");
    QVBoxLayout *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    if (!title.isEmpty())
    {
        QHBoxLayout *titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(6);

        QLabel *titleLabel = new QLabel(title, info);
        titleLabel->setObjectName("settingTitle");
        titleRow->addWidget(titleLabel);

        if (!help.isEmpty())
        {
            QToolButton *helpBtn = new QToolButton();
            helpBtn->setText("i");
            helpBtn->setObjectName("helpButton");
            helpBtn->setToolTip(help);
            helpBtn->setCursor(Qt::PointingHandCursor);
            helpBtn->setFixedSize(18, 18);
            connect(helpBtn, &QToolButton::clicked, [help, helpBtn]()
            {
                QToolTip::showText(helpBtn->mapToGlobal(QPoint(helpBtn->width() + 4, -4)),
                                   help, helpBtn, QRect(), 5000);
            });
            titleRow->addWidget(helpBtn);
        }
        titleRow->addStretch();
        infoLayout->addLayout(titleRow);
    }

    if (!desc.isEmpty())
    {
        QLabel *descLabel = new QLabel(desc, info);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    rowLayout->addWidget(info, 1);

    cardLayout->addWidget(row);
    return rowLayout;
}

void InstanceGameSettingsPage::initUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *contentWidget = new QWidget(scrollArea);
    QVBoxLayout *layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // 顶部工具栏 — 对应原型 .list-toolbar
    QHBoxLayout *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    QLabel *titleLabel = new QLabel(tr("实例设置"), contentWidget);
    titleLabel->setObjectName("listTitle");
    toolbar->addWidget(titleLabel);
    toolbar->addStretch();

    QPushButton *resetBtn = new QPushButton(tr("重置"), contentWidget);
    resetBtn->setObjectName("restoreDefaultsBtn");
    QPushButton *saveBtn = new QPushButton(tr("保存设置"), contentWidget);
    saveBtn->setObjectName("saveButton");

    toolbar->addWidget(resetBtn);
    toolbar->addWidget(saveBtn);
    layout->addLayout(toolbar);

    // ── 实例信息 ──
    QVBoxLayout *infoCard = createSettingsCard(layout, tr("实例信息"));

    QHBoxLayout *iconRow = appendSettingRow(infoCard, tr("实例图标"), tr("显示在启动器和概览页中的图标"));
    m_iconPreviewLabel = new QLabel(contentWidget);
    m_iconPreviewLabel->setObjectName("instanceIconPreview");
    m_iconPreviewLabel->setFixedSize(56, 56);
    m_iconPreviewLabel->setAlignment(Qt::AlignCenter);
    QHBoxLayout *iconControl = new QHBoxLayout();
    iconControl->setContentsMargins(0, 0, 0, 0);
    iconControl->setSpacing(8);
    QPushButton *iconChooseBtn = new QPushButton(tr("选择图标"), contentWidget);
    iconChooseBtn->setObjectName("browseBtn");
    QPushButton *iconResetBtn = new QPushButton(tr("恢复默认"), contentWidget);
    iconResetBtn->setObjectName("restoreDefaultsBtn");
    iconControl->addWidget(iconChooseBtn);
    iconControl->addWidget(iconResetBtn);
    iconControl->addStretch();
    iconRow->addWidget(m_iconPreviewLabel);
    iconRow->addLayout(iconControl, 2);

    connect(iconChooseBtn, &QPushButton::clicked, this, &InstanceGameSettingsPage::chooseInstanceIcon);
    connect(iconResetBtn, &QPushButton::clicked, this, &InstanceGameSettingsPage::resetInstanceIcon);

    QHBoxLayout *nameRow = appendSettingRow(infoCard, tr("实例名称"), tr("显示在启动器和侧边栏中的名称"));
    m_instanceNameEdit = new QLineEdit(contentWidget);
    m_instanceNameEdit->setObjectName("instanceNameEdit");
    m_instanceNameEdit->setPlaceholderText(tr("实例名称"));
    m_instanceNameEdit->setClearButtonEnabled(true);
    nameRow->addWidget(m_instanceNameEdit, 2);

    connect(m_instanceNameEdit, &QLineEdit::textChanged, this, [this](const QString &text)
    {
        QString newName = text.trimmed();
        emit instanceInfoChanged(newName, m_iconPath);
    });
    connect(m_instanceNameEdit, &QLineEdit::editingFinished, this, [this]()
    {
        saveSettings();
        emit settingChanged();
    });

    QHBoxLayout *verRow = appendSettingRow(infoCard, tr("游戏版本"), QString());
    QLabel *verValue = new QLabel(tr("-"), contentWidget);
    verValue->setObjectName("settingValueLabel");
    verRow->addWidget(verValue);
    m_gameVersionLabel = verValue;

    QHBoxLayout *loaderRow = appendSettingRow(infoCard, tr("加载器"), QString(), QString(), true);
    QLabel *loaderValue = new QLabel(tr("-"), contentWidget);
    loaderValue->setObjectName("settingValueLabel");
    loaderRow->addWidget(loaderValue);
    m_loaderLabel = loaderValue;

    // ── Java 设置 ──
    QVBoxLayout *javaCard = createSettingsCard(layout, tr("Java 设置"));

    QHBoxLayout *javaRow = appendSettingRow(javaCard,
        tr("Java 版本"), tr("启动该实例所使用的 Java 运行时"));

    QHBoxLayout *javaControlRow = new QHBoxLayout();
    javaControlRow->setContentsMargins(0, 0, 0, 0);
    javaControlRow->setSpacing(8);

    m_javaPathCombo = new QComboBox(contentWidget);
    m_javaPathCombo->setObjectName("instanceJavaPathCombo");
    m_javaPathCombo->addItem(tr("使用全局设置"), QString());

    QList<GameLauncher::JavaInfo> javaList = GameLauncher::instance()->findAllJavaInstallations();
    for (const GameLauncher::JavaInfo &javaInfo : javaList)
    {
        m_javaPathCombo->addItem(
            tr("Java %1 (%2)").arg(javaInfo.version).arg(javaInfo.path), javaInfo.path);
    }

    QPushButton *javaBrowseBtn = new QPushButton(tr("浏览"), contentWidget);
    javaBrowseBtn->setObjectName("browseBtn");
    QPushButton *javaRefreshBtn = new QPushButton(tr("刷新"), contentWidget);
    javaRefreshBtn->setObjectName("refreshBtn");

    javaControlRow->addWidget(m_javaPathCombo, 1);
    javaControlRow->addWidget(javaBrowseBtn);
    javaControlRow->addWidget(javaRefreshBtn);
    javaRow->addLayout(javaControlRow, 2);

    m_javaVersionLabel = new QLabel(tr("使用全局 Java 设置"), contentWidget);
    m_javaVersionLabel->setObjectName("hintLabel");

    QHBoxLayout *javaHintRow = appendSettingRow(javaCard, QString(), QString(), QString(), true);
    javaHintRow->addWidget(m_javaVersionLabel);

    connect(m_javaPathCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index)
    {
        if (index <= 0)
        {
            m_javaVersionLabel->setText(tr("使用全局 Java 设置"));
        }
        else
        {
            QString path = m_javaPathCombo->currentData().toString();
            GameLauncher::JavaInfo info;
            if (GameLauncher::instance()->validateJavaPath(path, info))
            {
                m_javaVersionLabel->setText(tr("已选择: Java %1").arg(info.version));
            }
            else
            {
                m_javaVersionLabel->setText(tr("已选择: 无效路径"));
            }
        }
        saveSettings();
        emit settingChanged();
    });

    connect(javaBrowseBtn, &QPushButton::clicked, this, [this]()
    {
        QString path = AppFileDialog::getOpenFileName(
            this, tr("选择 Java 可执行文件"),
            QCoreApplication::applicationDirPath(), tr("可执行文件 (*.exe)"));
        if (!path.isEmpty())
        {
            GameLauncher::JavaInfo info;
            if (GameLauncher::instance()->validateJavaPath(path, info))
            {
                bool exists = false;
                for (int i = 1; i < m_javaPathCombo->count(); ++i)
                {
                    if (m_javaPathCombo->itemData(i).toString() == path)
                    {
                        m_javaPathCombo->setCurrentIndex(i);
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    m_javaPathCombo->addItem(
                        tr("Java %1 (%2)").arg(info.version).arg(path), path);
                    m_javaPathCombo->setCurrentIndex(m_javaPathCombo->count() - 1);
                }
            }
            else
            {
                AppMessageBox::warning(this, tr("警告"),
                                     tr("选择的文件不是有效的 Java 可执行文件！"));
            }
        }
    });

    connect(javaRefreshBtn, &QPushButton::clicked, this, [this]()
    {
        while (m_javaPathCombo->count() > 1)
        {
            m_javaPathCombo->removeItem(1);
        }
        QList<GameLauncher::JavaInfo> list = GameLauncher::instance()->findAllJavaInstallations();
        for (const GameLauncher::JavaInfo &info : list)
        {
            m_javaPathCombo->addItem(
                tr("Java %1 (%2)").arg(info.version).arg(info.path), info.path);
        }
        loadSettings();
    });

    // ── 内存设置 ──
    QVBoxLayout *memoryCard = createSettingsCard(layout, tr("内存设置"));

    QHBoxLayout *maxMemRow = appendSettingRow(memoryCard,
        tr("最大内存分配"), tr("分配给该实例运行时的最大内存"));

    QHBoxLayout *maxMemControl = new QHBoxLayout();
    maxMemControl->setContentsMargins(0, 0, 0, 0);
    maxMemControl->setSpacing(10);

    m_maxMemorySlider = new QSlider(Qt::Horizontal, contentWidget);
    m_maxMemorySlider->setObjectName("instanceMemorySlider");
    m_maxMemorySlider->setRange(SettingsManager::MIN_MEMORY_MB, SettingsManager::MAX_MEMORY_MB);
    m_maxMemorySlider->setSingleStep(SettingsManager::MEMORY_SLIDER_STEP);
    m_maxMemorySlider->setTickInterval(SettingsManager::MEMORY_SLIDER_STEP);

    m_maxMemoryLabel = new QLabel(contentWidget);
    m_maxMemoryLabel->setObjectName("settingValueLabel");

    maxMemControl->addWidget(m_maxMemorySlider, 1);
    maxMemControl->addWidget(m_maxMemoryLabel);
    maxMemRow->addLayout(maxMemControl, 2);

    connect(m_maxMemorySlider, &QSlider::valueChanged, this, [this](int value)
    {
        m_maxMemoryLabel->setText(tr("当前: %1 GB (%2 MB)").arg(value / 1024.0, 0, 'f', 1).arg(value));
        saveSettings();
        emit settingChanged();
    });

    QHBoxLayout *minMemRow = appendSettingRow(memoryCard,
        tr("最小内存 (MB)"), tr("分配给该实例运行时的最小内存"), QString(), true);

    m_minMemoryEdit = new QLineEdit(contentWidget);
    m_minMemoryEdit->setObjectName("instanceMinMemoryEdit");
    m_minMemoryEdit->setFixedWidth(100);
    m_minMemoryEdit->setPlaceholderText(QString::number(SettingsManager::DEFAULT_MIN_MEMORY_MB));

    QHBoxLayout *minMemControl = new QHBoxLayout();
    minMemControl->setContentsMargins(0, 0, 0, 0);
    minMemControl->setSpacing(8);
    minMemControl->addWidget(m_minMemoryEdit);
    minMemControl->addStretch();
    minMemRow->addLayout(minMemControl, 2);

    connect(m_minMemoryEdit, &QLineEdit::textChanged, this, [this]()
    {
        saveSettings();
        emit settingChanged();
    });

    // ── JVM 参数 ──
    QVBoxLayout *jvmCard = createSettingsCard(layout, tr("JVM 参数"));

    QHBoxLayout *jvmRow = appendSettingRow(jvmCard,
        tr("JVM 参数"), tr("自定义 JVM 启动参数，留空则使用默认参数"));

    m_jvmArgsEdit = new QLineEdit(contentWidget);
    m_jvmArgsEdit->setObjectName("instanceJvmArgsEdit");
    m_jvmArgsEdit->setPlaceholderText(tr("例如: -XX:+UseG1GC -XX:+UnlockExperimentalVMOptions"));
    jvmRow->addWidget(m_jvmArgsEdit, 2);

    connect(m_jvmArgsEdit, &QLineEdit::textChanged, this, [this]()
    {
        saveSettings();
        emit settingChanged();
    });

    // ── 游戏窗口设置 ──
    QVBoxLayout *windowCard = createSettingsCard(layout, tr("游戏窗口设置"));

    QHBoxLayout *sizeRow = appendSettingRow(windowCard,
        tr("窗口分辨率"), tr("游戏窗口的初始分辨率"));

    QHBoxLayout *sizeControl = new QHBoxLayout();
    sizeControl->setContentsMargins(0, 0, 0, 0);
    sizeControl->setSpacing(8);

    QLabel *widthLabel = new QLabel(tr("宽度:"), contentWidget);
    widthLabel->setObjectName("hintLabel");
    m_windowWidthEdit = new QLineEdit(contentWidget);
    m_windowWidthEdit->setObjectName("instanceWindowWidthEdit");
    m_windowWidthEdit->setFixedWidth(80);
    m_windowWidthEdit->setPlaceholderText("1280");

    QLabel *heightLabel = new QLabel(tr("高度:"), contentWidget);
    heightLabel->setObjectName("hintLabel");
    m_windowHeightEdit = new QLineEdit(contentWidget);
    m_windowHeightEdit->setObjectName("instanceWindowHeightEdit");
    m_windowHeightEdit->setFixedWidth(80);
    m_windowHeightEdit->setPlaceholderText("720");

    sizeControl->addWidget(widthLabel);
    sizeControl->addWidget(m_windowWidthEdit);
    sizeControl->addWidget(heightLabel);
    sizeControl->addWidget(m_windowHeightEdit);
    sizeControl->addStretch();
    sizeRow->addLayout(sizeControl, 2);

    QHBoxLayout *fsRow = appendSettingRow(windowCard,
        tr("全屏模式"), tr("以全屏模式启动游戏"), QString(), true);
    m_fullscreenCheck = new CustomCheckBox("", contentWidget);
    fsRow->addWidget(m_fullscreenCheck);

    connect(m_windowWidthEdit, &QLineEdit::textChanged, this, [this]()
    {
        saveSettings();
        emit settingChanged();
    });
    connect(m_windowHeightEdit, &QLineEdit::textChanged, this, [this]()
    {
        saveSettings();
        emit settingChanged();
    });
    connect(m_fullscreenCheck, &CustomCheckBox::toggled, this, [this]()
    {
        saveSettings();
        emit settingChanged();
    });

    // ── 游戏目录 ──
    QVBoxLayout *gameDirCard = createSettingsCard(layout, tr("游戏目录"));

    QHBoxLayout *gameDirRow = appendSettingRow(gameDirCard,
        tr("游戏目录"), tr("该实例的游戏运行目录路径"), QString(), true);

    QLabel *gameDirLabel = new QLabel(tr("与实例文件夹相同"), contentWidget);
    gameDirLabel->setObjectName("settingValueLabel");
    gameDirRow->addWidget(gameDirLabel, 0, Qt::AlignRight);

    // ── 升级策略 ──
    QVBoxLayout *upgradeCard = createSettingsCard(layout, tr("升级策略"));

    QHBoxLayout *priorityRow = appendSettingRow(upgradeCard,
        tr("保守升级"), tr("资源均使用正式版，确保稳定性。"),
        tr("启用后，更新模组、资源包等资源时仅选择正式发布版本（Release），跳过测试版和预览版。适合追求稳定、不希望被测试版影响的用户。"));

    m_priorityUpgradeCheck = new CustomCheckBox("", contentWidget);
    priorityRow->addWidget(m_priorityUpgradeCheck);

    QHBoxLayout *cautiousRow = appendSettingRow(upgradeCard,
        tr("抢先升级"), tr("包含所有版本（正式版、测试版、预览版等）。"),
        tr("启用后，更新资源时会包含所有可用版本，包括测试版（Beta）、预览版（Preview）等。适合希望第一时间体验新功能的用户。"),
        true);

    m_cautiousUpgradeCheck = new CustomCheckBox("", contentWidget);
    cautiousRow->addWidget(m_cautiousUpgradeCheck);

    // 互斥逻辑：保守升级与抢先升级不可同时开启
    connect(m_priorityUpgradeCheck, &CustomCheckBox::toggled, this, [this](bool checked)
    {
        if (checked)
            m_cautiousUpgradeCheck->setChecked(false);
        saveSettings();
        emit settingChanged();
    });
    connect(m_cautiousUpgradeCheck, &CustomCheckBox::toggled, this, [this](bool checked)
    {
        if (checked)
            m_priorityUpgradeCheck->setChecked(false);
        saveSettings();
        emit settingChanged();
    });

    layout->addStretch();

    connect(resetBtn, &QPushButton::clicked, this, [this]()
    {
        m_javaPathCombo->setCurrentIndex(0);
        m_maxMemorySlider->setValue(SettingsManager::DEFAULT_MAX_MEMORY_MB);
        m_minMemoryEdit->setText(QString::number(SettingsManager::DEFAULT_MIN_MEMORY_MB));
        m_jvmArgsEdit->clear();
        m_windowWidthEdit->setText("1280");
        m_windowHeightEdit->setText("720");
        m_fullscreenCheck->setChecked(false);
        m_priorityUpgradeCheck->setChecked(true);
        m_cautiousUpgradeCheck->setChecked(false);
        // 重置自定义名称与图标
        if (m_instanceNameEdit)
        {
            m_instanceNameEdit->setText(m_instanceName);
            SettingsManager::instance()->removeProperty("instance/" + m_instancePath + "/displayName");
        }
        resetInstanceIcon();
        saveSettings();
    });

    connect(saveBtn, &QPushButton::clicked, this, [this]()
    {
        saveSettings();
    });

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
}

void InstanceGameSettingsPage::loadSettings()
{
    if (m_instancePath.isEmpty())
    {
        return;
    }

    SettingsManager *settings = SettingsManager::instance();
    QString prefix = "instance/" + m_instancePath + "/";

    // 实例名称：若保存过自定义名称则恢复，否则沿用文件夹名
    QString savedName = settings->getProperty(prefix + "displayName").toString();
    if (!savedName.isEmpty() && m_instanceNameEdit)
    {
        m_instanceNameEdit->setText(savedName);
    }

    // 实例图标
    m_iconPath = settings->getProperty(prefix + "iconPath").toString();
    if (!m_iconPath.isEmpty() && !QFile::exists(m_iconPath))
    {
        m_iconPath.clear();
        settings->removeProperty(prefix + "iconPath");
    }
    updateIconPreview();

    // Java 路径
    QString savedJava = settings->getProperty(prefix + "javaPath").toString();
    if (savedJava.isEmpty())
    {
        m_javaPathCombo->setCurrentIndex(0);
    }
    else
    {
        int idx = m_javaPathCombo->findData(savedJava);
        if (idx >= 0)
        {
            m_javaPathCombo->setCurrentIndex(idx);
        }
        else
        {
            m_javaPathCombo->addItem(savedJava, savedJava);
            m_javaPathCombo->setCurrentIndex(m_javaPathCombo->count() - 1);
        }
    }

    // 最大内存
    int maxMem = settings->getProperty(prefix + "maxMemory",
                                       SettingsManager::DEFAULT_MAX_MEMORY_MB).toInt();
    m_maxMemorySlider->setValue(maxMem);
    m_maxMemoryLabel->setText(tr("当前: %1 GB (%2 MB)")
                              .arg(maxMem / 1024.0, 0, 'f', 1).arg(maxMem));

    // 最小内存
    int minMem = settings->getProperty(prefix + "minMemory",
                                       SettingsManager::DEFAULT_MIN_MEMORY_MB).toInt();
    m_minMemoryEdit->setText(QString::number(minMem));

    // JVM 参数
    QString jvmArgs = settings->getProperty(prefix + "jvmArgs").toString();
    m_jvmArgsEdit->setText(jvmArgs);

    // 窗口尺寸
    QString width = settings->getProperty(prefix + "windowWidth", "1280").toString();
    m_windowWidthEdit->setText(width);

    QString height = settings->getProperty(prefix + "windowHeight", "720").toString();
    m_windowHeightEdit->setText(height);

    // 全屏
    bool fs = settings->getProperty(prefix + "fullscreen", false).toBool();
    m_fullscreenCheck->setChecked(fs);

    // 升级策略: 0=保守升级(默认), 1=抢先升级
    int upgradeMode = settings->getProperty(prefix + "upgradeMode", 0).toInt();
    m_priorityUpgradeCheck->setChecked(upgradeMode == 0);
    m_cautiousUpgradeCheck->setChecked(upgradeMode == 1);
}

void InstanceGameSettingsPage::saveSettings()
{
    if (m_instancePath.isEmpty())
    {
        return;
    }

    SettingsManager *settings = SettingsManager::instance();
    QString prefix = "instance/" + m_instancePath + "/";

    // 自定义实例名称（为空表示使用文件夹名）
    QString displayName = m_instanceNameEdit ? m_instanceNameEdit->text().trimmed() : QString();
    if (displayName.isEmpty() || displayName == m_instanceName)
        settings->removeProperty(prefix + "displayName");
    else
        settings->setProperty(prefix + "displayName", displayName);

    // 实例图标
    if (m_iconPath.isEmpty())
        settings->removeProperty(prefix + "iconPath");
    else
        settings->setProperty(prefix + "iconPath", m_iconPath);

    settings->setProperty(prefix + "javaPath",
                          m_javaPathCombo->currentIndex() > 0
                              ? m_javaPathCombo->currentData().toString()
                              : QString());

    settings->setProperty(prefix + "maxMemory", m_maxMemorySlider->value());

    bool ok = false;
    int minMem = m_minMemoryEdit->text().toInt(&ok);
    if (ok && minMem >= SettingsManager::MIN_MEMORY_MB)
    {
        settings->setProperty(prefix + "minMemory", minMem);
    }

    settings->setProperty(prefix + "jvmArgs", m_jvmArgsEdit->text());

    settings->setProperty(prefix + "windowWidth", m_windowWidthEdit->text());
    settings->setProperty(prefix + "windowHeight", m_windowHeightEdit->text());
    settings->setProperty(prefix + "fullscreen", m_fullscreenCheck->isChecked());

    // 升级策略: 0=保守升级(默认), 1=抢先升级
    settings->setProperty(prefix + "upgradeMode", upgradeMode());
}
