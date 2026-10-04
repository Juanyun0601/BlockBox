/**
 * @file   LocalModelDialog.cpp
 * @brief  本地 AI 模型管理对话框实现
 * @author BlockBox Team
 * @date   2026-08-06
 */

#include "LocalModelDialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>

// ============================================================================
// 构造与显示
// ============================================================================

LocalModelDialog::LocalModelDialog(QWidget* parent)
    : AppDialogBase(parent)
    , m_manager(new LocalModelManager(this))
{
    setWindowTitle(tr("本地模型管理"));
    setWindowModality(Qt::NonModal);
    setObjectName("localModelDialog");

    initUI();

    // 连接 manager 的所有信号
    connect(m_manager, &LocalModelManager::serviceStatusChecked,
            this, &LocalModelDialog::onServiceStatusChecked);
    connect(m_manager, &LocalModelManager::serviceStarted,
            this, &LocalModelDialog::onServiceStarted);
    connect(m_manager, &LocalModelManager::serviceStopped,
            this, &LocalModelDialog::onServiceStopped);
    connect(m_manager, &LocalModelManager::pullProgress,
            this, &LocalModelDialog::onPullProgress);
    connect(m_manager, &LocalModelManager::pullFinished,
            this, &LocalModelDialog::onPullFinished);
    connect(m_manager, &LocalModelManager::pullFailed,
            this, &LocalModelDialog::onPullFailed);
    connect(m_manager, &LocalModelManager::modelListReady,
            this, &LocalModelDialog::onModelListReady);
    connect(m_manager, &LocalModelManager::modelRemoved,
            this, &LocalModelDialog::onModelRemoved);
    connect(m_manager, &LocalModelManager::installerDownloadProgress,
            this, &LocalModelDialog::onInstallerProgress);
    connect(m_manager, &LocalModelManager::installerDownloaded,
            this, &LocalModelDialog::onInstallerDownloaded);
    connect(m_manager, &LocalModelManager::installerDownloadFailed,
            this, &LocalModelDialog::onInstallerDownloadFailed);
}

void LocalModelDialog::showEvent(QShowEvent* event)
{
    AppDialogBase::showEvent(event);
    setWindowOpacity(0.0);
    auto* anim = new QPropertyAnimation(this, "windowOpacity");
    anim->setDuration(160);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    // 首次显示即触发服务状态检测与模型列表刷新
    if (m_currentStatus == LocalModelManager::ServiceStatus::Unknown)
    {
        onRefreshClicked();
    }
}

// ============================================================================
// UI 初始化
// ============================================================================

void LocalModelDialog::initUI()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_card = new QWidget(this);
    m_card->setObjectName("localModelCard");
    m_card->setMinimumSize(620, 640);
    m_card->setMaximumSize(820, 800);
    outer->addWidget(m_card, 0, Qt::AlignCenter);

    m_mainLayout = new QVBoxLayout(m_card);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 顶部标题
    auto* titleBar = new QWidget();
    titleBar->setObjectName("localModelTitleBar");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 16, 16, 16);
    auto* titleLabel = new QLabel(tr("本地模型管理 (Ollama)"));
    titleLabel->setObjectName("localModelTitle");
    QFont f = titleLabel->font();
    f.setBold(true);
    f.setPixelSize(18);
    titleLabel->setFont(f);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    m_mainLayout->addWidget(titleBar);

    // 滚动区
    auto* scrollArea = new QScrollArea();
    scrollArea->setObjectName("localModelScroll");
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget();
    content->setObjectName("localModelContent");
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(20, 8, 20, 20);
    contentLayout->setSpacing(16);
    scrollArea->setWidget(content);
    m_mainLayout->addWidget(scrollArea, 1);

    // === 服务状态区 ===
    m_statusSection = new QWidget();
    m_statusSection->setObjectName("localModelStatusSection");
    auto* statusLayout = new QVBoxLayout(m_statusSection);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);

    auto* statusTopRow = new QHBoxLayout();
    statusTopRow->setSpacing(10);
    m_statusIconLabel = new QLabel();
    m_statusIconLabel->setFixedSize(14, 14);
    m_statusTextLabel = new QLabel(tr("正在检测..."));
    QFont statusFont = m_statusTextLabel->font();
    statusFont.setBold(true);
    statusFont.setPixelSize(14);
    m_statusTextLabel->setFont(statusFont);
    statusTopRow->addWidget(m_statusIconLabel);
    statusTopRow->addWidget(m_statusTextLabel, 1);
    statusTopRow->addStretch();

    m_refreshBtn = new QPushButton(tr("刷新"));
    m_refreshBtn->setObjectName("localModelRefreshBtn");
    statusTopRow->addWidget(m_refreshBtn);
    statusLayout->addLayout(statusTopRow);

    m_statusDetailLabel = new QLabel();
    m_statusDetailLabel->setWordWrap(true);
    m_statusDetailLabel->setObjectName("localModelStatusDetail");
    QFont detailFont = m_statusDetailLabel->font();
    detailFont.setPixelSize(12);
    m_statusDetailLabel->setFont(detailFont);
    statusLayout->addWidget(m_statusDetailLabel);

    auto* statusBtnRow = new QHBoxLayout();
    statusBtnRow->setSpacing(8);
    m_startStopBtn = new QPushButton(tr("启动服务"));
    m_startStopBtn->setObjectName("localModelStartBtn");
    m_installBtn = new QPushButton(tr("下载 Ollama 安装器"));
    m_installBtn->setObjectName("localModelInstallBtn");
    statusBtnRow->addWidget(m_startStopBtn);
    statusBtnRow->addWidget(m_installBtn);
    statusBtnRow->addStretch();
    statusLayout->addLayout(statusBtnRow);

    m_installerProgress = new QProgressBar();
    m_installerProgress->setObjectName("localModelInstallerProgress");
    m_installerProgress->setRange(0, 100);
    m_installerProgress->setVisible(false);
    statusLayout->addWidget(m_installerProgress);

    contentLayout->addWidget(m_statusSection);

    // === 拉取进度区（仅拉取时显示） ===
    m_pullSection = new QWidget();
    m_pullSection->setObjectName("localModelPullSection");
    m_pullSection->setVisible(false);
    auto* pullLayout = new QVBoxLayout(m_pullSection);
    pullLayout->setContentsMargins(0, 0, 0, 0);
    pullLayout->setSpacing(6);
    m_pullTagLabel = new QLabel();
    QFont pullTagFont = m_pullTagLabel->font();
    pullTagFont.setBold(true);
    m_pullTagLabel->setFont(pullTagFont);
    m_pullStatusLabel = new QLabel();
    m_pullStatusLabel->setWordWrap(true);
    m_pullProgress = new QProgressBar();
    m_pullProgress->setRange(0, 100);
    m_cancelPullBtn = new QPushButton(tr("取消拉取"));
    m_cancelPullBtn->setObjectName("localModelCancelPullBtn");
    pullLayout->addWidget(m_pullTagLabel);
    pullLayout->addWidget(m_pullProgress);
    pullLayout->addWidget(m_pullStatusLabel);
    auto* pullBtnRow = new QHBoxLayout();
    pullBtnRow->addStretch();
    pullBtnRow->addWidget(m_cancelPullBtn);
    pullLayout->addLayout(pullBtnRow);
    contentLayout->addWidget(m_pullSection);

    // === 已下载模型区 ===
    m_installedSection = new QWidget();
    m_installedSection->setObjectName("localModelInstalledSection");
    auto* installedOuterLayout = new QVBoxLayout(m_installedSection);
    installedOuterLayout->setContentsMargins(0, 0, 0, 0);
    installedOuterLayout->setSpacing(8);

    auto* installedHeader = new QLabel(tr("已下载的模型"));
    QFont ihFont = installedHeader->font();
    ihFont.setBold(true);
    ihFont.setPixelSize(15);
    installedHeader->setFont(ihFont);
    installedOuterLayout->addWidget(installedHeader);

    m_installedLayout = new QVBoxLayout();
    m_installedLayout->setSpacing(6);
    installedOuterLayout->addLayout(m_installedLayout);

    m_installedEmptyLabel = new QLabel(tr("暂无已下载的本地模型，请从下方选择并拉取"));
    m_installedEmptyLabel->setObjectName("localModelEmptyHint");
    m_installedEmptyLabel->setWordWrap(true);
    installedOuterLayout->addWidget(m_installedEmptyLabel);

    contentLayout->addWidget(m_installedSection);

    // === 预设模型区 ===
    m_presetSection = new QWidget();
    m_presetSection->setObjectName("localModelPresetSection");
    auto* presetOuterLayout = new QVBoxLayout(m_presetSection);
    presetOuterLayout->setContentsMargins(0, 0, 0, 0);
    presetOuterLayout->setSpacing(8);

    auto* presetHeader = new QLabel(tr("下载新模型"));
    QFont phFont = presetHeader->font();
    phFont.setBold(true);
    phFont.setPixelSize(15);
    presetHeader->setFont(phFont);
    presetOuterLayout->addWidget(presetHeader);

    auto* presetHint = new QLabel(
        tr("提示：Gemma 与 Qwen 系列均为开源模型，下载后完全离线运行。"
           "首次下载体积较大，建议在 Wi-Fi 环境下进行。"));
    presetHint->setWordWrap(true);
    QFont hintFont = presetHint->font();
    hintFont.setPixelSize(12);
    presetHint->setFont(hintFont);
    presetOuterLayout->addWidget(presetHint);

    m_presetLayout = new QVBoxLayout();
    m_presetLayout->setSpacing(6);
    presetOuterLayout->addLayout(m_presetLayout);

    contentLayout->addWidget(m_presetSection);
    contentLayout->addStretch();

    // 连接内部按钮
    connect(m_refreshBtn, &QPushButton::clicked, this, &LocalModelDialog::onRefreshClicked);
    connect(m_startStopBtn, &QPushButton::clicked, this, &LocalModelDialog::onStartStopServiceClicked);
    connect(m_installBtn, &QPushButton::clicked, this, &LocalModelDialog::onInstallClicked);
    connect(m_cancelPullBtn, &QPushButton::clicked, this, &LocalModelDialog::onCancelPullClicked);

    rebuildPresetSection();
    refreshServiceStatusUI();
}

// ============================================================================
// 服务状态
// ============================================================================

void LocalModelDialog::onRefreshClicked()
{
    m_refreshBtn->setEnabled(false);
    m_statusTextLabel->setText(tr("正在检测..."));
    m_manager->checkServiceStatus();
}

void LocalModelDialog::onServiceStatusChecked(LocalModelManager::ServiceStatus status)
{
    m_currentStatus = status;
    m_refreshBtn->setEnabled(true);
    refreshServiceStatusUI();

    // 服务运行中或已安装：尝试列出已下载模型
    if (status == LocalModelManager::ServiceStatus::Running ||
        status == LocalModelManager::ServiceStatus::InstalledStopped)
    {
        m_manager->listModels();
    }
    else
    {
        m_installedModels.clear();
        m_localAiModels.clear();
        rebuildInstalledModelsSection();
    }
}

void LocalModelDialog::refreshServiceStatusUI()
{
    switch (m_currentStatus)
    {
    case LocalModelManager::ServiceStatus::Unknown:
        m_statusIconLabel->setStyleSheet("background: #999; border-radius: 7px;");
        m_statusTextLabel->setText(tr("正在检测 Ollama 状态..."));
        m_statusDetailLabel->setText(tr("正在尝试连接 http://127.0.0.1:11434"));
        m_startStopBtn->setEnabled(false);
        m_startStopBtn->setText(tr("启动服务"));
        m_installBtn->setVisible(false);
        m_installerProgress->setVisible(false);
        break;
    case LocalModelManager::ServiceStatus::NotInstalled:
        m_statusIconLabel->setStyleSheet("background: #e74c3c; border-radius: 7px;");
        m_statusTextLabel->setText(tr("未检测到 Ollama"));
        m_statusDetailLabel->setText(
            tr("Ollama 是本地大模型推理引擎，需先下载安装（约 600 MB）。"
               "安装完成后会自动注册到系统 PATH 并启动服务。"));
        m_startStopBtn->setEnabled(false);
        m_startStopBtn->setText(tr("启动服务"));
        m_installBtn->setVisible(true);
        m_installBtn->setText(tr("下载 Ollama 安装器"));
        m_installerProgress->setVisible(m_manager->isDownloadingInstaller());
        break;
    case LocalModelManager::ServiceStatus::InstalledStopped:
        m_statusIconLabel->setStyleSheet("background: #f39c12; border-radius: 7px;");
        m_statusTextLabel->setText(tr("Ollama 已安装，服务未运行"));
        m_statusDetailLabel->setText(
            tr("点击下方“启动服务”以运行 ollama serve。"
               "首次启动可能需要数秒初始化。"));
        m_startStopBtn->setEnabled(true);
        m_startStopBtn->setText(tr("启动服务"));
        m_installBtn->setVisible(false);
        m_installerProgress->setVisible(false);
        break;
    case LocalModelManager::ServiceStatus::Running:
        m_statusIconLabel->setStyleSheet("background: #2ecc71; border-radius: 7px;");
        m_statusTextLabel->setText(tr("Ollama 服务运行中"));
        m_statusDetailLabel->setText(
            tr("OpenAI 兼容端点：%1").arg(m_manager->ollamaOpenAiUrl()));
        m_startStopBtn->setEnabled(true);
        m_startStopBtn->setText(tr("停止服务"));
        m_installBtn->setVisible(false);
        m_installerProgress->setVisible(false);
        break;
    }
}

void LocalModelDialog::onStartStopServiceClicked()
{
    if (m_currentStatus == LocalModelManager::ServiceStatus::Running)
    {
        m_manager->stopService();
    }
    else if (m_currentStatus == LocalModelManager::ServiceStatus::InstalledStopped)
    {
        m_startStopBtn->setEnabled(false);
        m_startStopBtn->setText(tr("正在启动..."));
        m_manager->startService();
    }
}

void LocalModelDialog::onServiceStarted()
{
    m_currentStatus = LocalModelManager::ServiceStatus::Running;
    refreshServiceStatusUI();
    m_manager->listModels();
}

void LocalModelDialog::onServiceStopped(const QString& errorMessage)
{
    m_currentStatus = LocalModelManager::ServiceStatus::InstalledStopped;
    refreshServiceStatusUI();
    if (!errorMessage.isEmpty())
    {
        m_statusDetailLabel->setText(m_statusDetailLabel->text() + "\n" + errorMessage);
    }
}

// ============================================================================
// 安装器下载
// ============================================================================

void LocalModelDialog::onInstallClicked()
{
    // 已下载：直接运行安装器
    if (!m_manager->installerPath().isEmpty())
    {
        onRunInstallerClicked();
        return;
    }
    if (m_manager->isDownloadingInstaller())
    {
        return;
    }
    m_installBtn->setEnabled(false);
    m_installBtn->setText(tr("正在下载..."));
    m_installerProgress->setVisible(true);
    m_installerProgress->setValue(0);
    m_manager->downloadInstaller();
}

void LocalModelDialog::onRunInstallerClicked()
{
    if (!m_manager->runInstaller())
    {
        m_statusDetailLabel->setText(tr("运行安装器失败，请检查文件：") + m_manager->installerPath());
        return;
    }
    // 安装器是 detached 的，提示用户
    m_statusDetailLabel->setText(
        tr("已启动 Ollama 安装器，请在弹出的安装向导中完成安装。"
           "安装完成后请点击“刷新”重新检测服务状态。"));
    m_installBtn->setEnabled(false);
    m_installBtn->setText(tr("等待安装完成"));
}

void LocalModelDialog::onInstallerProgress(int percent, qint64 received, qint64 total)
{
    if (percent >= 0)
    {
        m_installerProgress->setValue(percent);
    }
    else
    {
        m_installerProgress->setRange(0, 0); // 不确定进度
    }
    if (total > 0)
    {
        constexpr qint64 MB = 1024 * 1024;
        m_statusDetailLabel->setText(
            tr("正在下载 Ollama 安装器：%1 / %2 MB")
                .arg(received / MB)
                .arg(total / MB));
    }
}

void LocalModelDialog::onInstallerDownloaded(const QString& localPath)
{
    m_installerProgress->setVisible(false);
    m_installerProgress->setRange(0, 100);
    m_statusDetailLabel->setText(tr("安装器已下载完成：%1").arg(localPath));
    m_installBtn->setEnabled(true);
    m_installBtn->setText(tr("运行安装器"));
}

void LocalModelDialog::onInstallerDownloadFailed(const QString& errorMessage)
{
    m_installerProgress->setVisible(false);
    m_installerProgress->setRange(0, 100);
    m_statusDetailLabel->setText(tr("安装器下载失败：%1").arg(errorMessage));
    m_installBtn->setEnabled(true);
    m_installBtn->setText(tr("重试下载 Ollama 安装器"));
}

// ============================================================================
// 模型拉取
// ============================================================================

void LocalModelDialog::onPullPreset(const LocalModelPreset& preset)
{
    if (m_manager->isPulling())
    {
        return;
    }
    m_manager->pullModel(preset.tag);
    setPullInProgress(true, preset.tag);
}

void LocalModelDialog::onCancelPullClicked()
{
    m_manager->cancelPull();
    setPullInProgress(false);
}

void LocalModelDialog::setPullInProgress(bool inProgress, const QString& tag)
{
    m_pullSection->setVisible(inProgress);
    if (inProgress)
    {
        m_currentPullTag = tag;
        m_pullTagLabel->setText(tr("正在拉取：%1").arg(tag));
        m_pullStatusLabel->setText(tr("准备中..."));
        m_pullProgress->setRange(0, 100);
        m_pullProgress->setValue(0);
    }
    else
    {
        m_currentPullTag.clear();
    }
}

void LocalModelDialog::onPullProgress(const QString& tag, int percent, const QString& speedText,
                                       const QString& statusText)
{
    Q_UNUSED(speedText)
    if (percent < 0)
    {
        m_pullProgress->setRange(0, 0); // 不确定进度
    }
    else
    {
        m_pullProgress->setRange(0, 100);
        m_pullProgress->setValue(percent);
    }
    QString text = statusText;
    if (!tag.isEmpty() && !text.contains(tag))
    {
        text = QStringLiteral("[%1] %2").arg(tag, text);
    }
    m_pullStatusLabel->setText(text);
}

void LocalModelDialog::onPullFinished(const QString& tag)
{
    setPullInProgress(false);
    m_statusDetailLabel->setText(tr("模型 %1 拉取完成").arg(tag));
    m_manager->listModels();
    emit localModelsChanged();
}

void LocalModelDialog::onPullFailed(const QString& tag, const QString& errorMessage)
{
    setPullInProgress(false);
    m_statusDetailLabel->setText(
        tr("拉取 %1 失败：%2").arg(tag.isEmpty() ? tr("(未知)") : tag, errorMessage));
}

// ============================================================================
// 模型列表与操作
// ============================================================================

void LocalModelDialog::onModelListReady(const QList<LocalModelInfo>& models)
{
    m_installedModels = models;
    m_localAiModels.clear();
    for (const LocalModelInfo& info : models)
    {
        AiModel m;
        m.id = "local-" + info.tag;
        m.id.replace(':', '-'); // 转换为合法 id（不含冒号）
        m.displayName = QStringLiteral("%1 (%2)").arg(info.tag, tr("本地"));
        m.apiUrl = LocalModelManager::ollamaOpenAiUrl();
        m.apiKey = QStringLiteral("ollama"); // Ollama 不校验 key，但需非空以通过现有校验
        m.websiteUrl = QStringLiteral("https://ollama.com");
        m.isLocal = true;
        m.localTag = info.tag;
        m_localAiModels.append(m);
    }
    rebuildInstalledModelsSection();
    emit localModelsChanged();
}

void LocalModelDialog::rebuildInstalledModelsSection()
{
    // 清空旧的条目（保留 header 和 empty label）
    QLayoutItem* item = nullptr;
    while ((item = m_installedLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    m_installedEmptyLabel->setVisible(m_installedModels.isEmpty());
    if (m_installedModels.isEmpty())
    {
        return;
    }

    for (const LocalModelInfo& info : m_installedModels)
    {
        auto* row = new QWidget();
        row->setObjectName("localModelInstalledRow");
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(10);

        auto* nameIcon = new QLabel();
        nameIcon->setFixedSize(8, 8);
        nameIcon->setStyleSheet("background: #2ecc71; border-radius: 4px;");
        rowLayout->addWidget(nameIcon);

        auto* nameLbl = new QLabel(info.tag);
        QFont nf = nameLbl->font();
        nf.setBold(true);
        nameLbl->setFont(nf);
        rowLayout->addWidget(nameLbl, 1);

        auto* sizeLbl = new QLabel(info.size);
        QFont sf = sizeLbl->font();
        sf.setPixelSize(12);
        sizeLbl->setFont(sf);
        sizeLbl->setStyleSheet("color: #888;");
        rowLayout->addWidget(sizeLbl);

        auto* useBtn = new QPushButton(tr("使用"));
        useBtn->setObjectName("localModelUseBtn");
        useBtn->setProperty("tag", info.tag);
        connect(useBtn, &QPushButton::clicked, this, [this, tag = info.tag]() {
            onUseModelClicked(tag);
        });
        rowLayout->addWidget(useBtn);

        auto* delBtn = new QPushButton(tr("删除"));
        delBtn->setObjectName("localModelDelBtn");
        delBtn->setProperty("tag", info.tag);
        connect(delBtn, &QPushButton::clicked, this, [this, tag = info.tag]() {
            onDeleteModelClicked(tag);
        });
        rowLayout->addWidget(delBtn);

        m_installedLayout->addWidget(row);
    }
}

void LocalModelDialog::onUseModelClicked(const QString& tag)
{
    for (const AiModel& m : m_localAiModels)
    {
        if (m.localTag == tag)
        {
            emit localModelSelected(m);
            close();
            return;
        }
    }
}

void LocalModelDialog::onDeleteModelClicked(const QString& tag)
{
    m_manager->removeModel(tag);
}

void LocalModelDialog::onModelRemoved(const QString& tag, bool success, const QString& errorMessage)
{
    if (success)
    {
        m_statusDetailLabel->setText(tr("模型 %1 已删除").arg(tag));
        m_manager->listModels();
        emit localModelsChanged();
    }
    else
    {
        m_statusDetailLabel->setText(
            tr("删除 %1 失败：%2").arg(tag, errorMessage));
    }
}

void LocalModelDialog::rebuildPresetSection()
{
    QLayoutItem* item = nullptr;
    while ((item = m_presetLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    const QList<LocalModelPreset> presets = m_manager->presetModels();
    QString lastFamily;
    for (const LocalModelPreset& p : presets)
    {
        if (p.family != lastFamily)
        {
            auto* familyLbl = new QLabel(p.family);
            QFont ff = familyLbl->font();
            ff.setBold(true);
            ff.setPixelSize(13);
            familyLbl->setFont(ff);
            familyLbl->setStyleSheet("color: #888; margin-top: 6px;");
            m_presetLayout->addWidget(familyLbl);
            lastFamily = p.family;
        }

        auto* row = new QWidget();
        row->setObjectName("localModelPresetRow");
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(10);

        auto* textBox = new QVBoxLayout();
        textBox->setSpacing(2);
        auto* nameLbl = new QLabel(p.displayName);
        QFont nf = nameLbl->font();
        nf.setBold(true);
        nameLbl->setFont(nf);
        auto* descLbl = new QLabel(
            QStringLiteral("%1 · %2").arg(p.description, p.sizeHint));
        QFont df = descLbl->font();
        df.setPixelSize(12);
        descLbl->setFont(df);
        descLbl->setStyleSheet("color: #888;");
        textBox->addWidget(nameLbl);
        textBox->addWidget(descLbl);
        rowLayout->addLayout(textBox, 1);

        auto* dlBtn = new QPushButton(tr("下载"));
        dlBtn->setObjectName("localModelPullBtn");
        connect(dlBtn, &QPushButton::clicked, this, [this, p]() {
            onPullPreset(p);
        });
        rowLayout->addWidget(dlBtn);

        m_presetLayout->addWidget(row);
    }
}
