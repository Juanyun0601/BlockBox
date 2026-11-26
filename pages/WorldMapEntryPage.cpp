/**
 * @file   WorldMapEntryPage.cpp
 * @brief  存档地图入口页实现（模块下载等待页 / 内置地图页两种形态）
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "WorldMapEntryPage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMetaObject>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "utils/IconHelper.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <cmath>

#if defined(BLOCKBOX_WORLDMAP_REMOTE_MODULE)
#include "utils/WorldMapModule/WorldMapModuleManager.h"
#endif

namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    constexpr double kMB = 1024.0 * 1024.0;
    if (bytes >= qint64(kMB))
        return QStringLiteral("%1 MB").arg(double(bytes) / kMB, 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 0);
}

} // namespace

#if defined(BLOCKBOX_WORLDMAP_REMOTE_MODULE)

// ═══════════════════════════════════════════════════════════════════
// 桌面端：按需下载模块
// ═══════════════════════════════════════════════════════════════════

WorldMapEntryPage::WorldMapEntryPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    applyTheme();

    WorldMapModuleManager *manager = WorldMapModuleManager::instance();
    connect(manager, &WorldMapModuleManager::stateChanged, this,
            [this](WorldMapModuleManager::State) { updateStateText(); });
    connect(manager, &WorldMapModuleManager::progressChanged, this,
            &WorldMapEntryPage::updateProgress);
    connect(manager, &WorldMapModuleManager::ready, this, [this]() {
        ensureModulePage();
        if (!m_modulePage)
            return;
        m_stack->setCurrentWidget(m_moduleHost);
        if (!m_instancePath.isEmpty())
            callPageInstancePath();
        applyPendingSave();
    });
    connect(manager, &WorldMapModuleManager::failed, this,
            &WorldMapEntryPage::showFailure);

    if (ThemeManager::instance()) {
        connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
                [this](ThemeManager::ThemeType) { applyTheme(); });
        connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this,
                [this](const QString &) { applyTheme(); });
    }
}

WorldMapEntryPage::~WorldMapEntryPage() = default;

void WorldMapEntryPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    // ── 等待页：居中一张卡片，说明首次需要下载 ──
    m_loadingPage = new QWidget(m_stack);
    auto *loadingLayout = new QVBoxLayout(m_loadingPage);
    loadingLayout->setContentsMargins(24, 24, 24, 24);
    loadingLayout->setSpacing(0);
    loadingLayout->addStretch(1);

    m_card = new QFrame(m_loadingPage);
    m_card->setObjectName("worldMapDownloadCard");
    // 固定一个可读的正文宽度：说明文字要换行，卡片太窄会变成细长一条
    m_card->setMinimumWidth(540);
    m_card->setMaximumWidth(620);
    m_card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(30, 26, 30, 26);
    cardLayout->setSpacing(10);

    m_iconLabel = new QLabel(m_card);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(tr("存档地图"), m_card);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(tr("首次使用需要下载地图模块"), m_card);
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_subtitleLabel);

    m_descLabel = new QLabel(
        tr("存档地图不随启动器一起安装。第一次打开需要下载一次地图模块，"
           "下载完成后会自动进入地图。"), m_card);
    m_descLabel->setWordWrap(true);
    m_descLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_descLabel);

    m_progress = new QProgressBar(m_card);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    cardLayout->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("正在准备地图模块…"), m_card);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_statusLabel);

    m_retryBtn = new QPushButton(tr("重试"), m_card);
    m_retryBtn->setCursor(Qt::PointingHandCursor);
    m_retryBtn->setVisible(false);
    connect(m_retryBtn, &QPushButton::clicked, this, [this]() {
        m_retryBtn->setVisible(false);
        m_progress->setVisible(true);
        m_statusLabel->setStyleSheet(StyleKit::secondaryLabel());
        m_statusLabel->setText(tr("正在准备地图模块…"));
        WorldMapModuleManager::instance()->retry();
    });
    cardLayout->addWidget(m_retryBtn, 0, Qt::AlignHCenter);

    m_hintLabel = new QLabel(
        tr("下载一次后，以后打开地图就不用再联网；地图模块也可以单独更新，"
           "不用等启动器更新。"), m_card);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_hintLabel);

    loadingLayout->addWidget(m_card, 0, Qt::AlignHCenter);
    loadingLayout->addStretch(1);
    m_stack->addWidget(m_loadingPage);

    // ── 模块页容器：下载完成后把地图页塞进来 ──
    m_moduleHost = new QWidget(m_stack);
    m_moduleLayout = new QVBoxLayout(m_moduleHost);
    m_moduleLayout->setContentsMargins(0, 0, 0, 0);
    m_moduleLayout->setSpacing(0);
    m_stack->addWidget(m_moduleHost);

    m_stack->setCurrentWidget(m_loadingPage);
}

void WorldMapEntryPage::applyTheme()
{
    if (!m_card)
        return;
    m_card->setStyleSheet(StyleKit::card());
    m_titleLabel->setStyleSheet(StyleKit::pageTitle());
    m_subtitleLabel->setStyleSheet(StyleKit::bodyLabel());
    m_descLabel->setStyleSheet(StyleKit::secondaryLabel());
    m_hintLabel->setStyleSheet(StyleKit::captionLabel());
    m_statusLabel->setStyleSheet(StyleKit::secondaryLabel());
    m_retryBtn->setStyleSheet(StyleKit::primaryButton());

    const QColor accent = ThemeManager::instance()
                              ? QColor(ThemeManager::instance()->currentThemeColor())
                              : QColor(0x3b, 0x82, 0xf6);
    m_iconLabel->setPixmap(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/map.svg"), accent, 56).pixmap(56, 56));
}

void WorldMapEntryPage::updateStateText()
{
    WorldMapModuleManager *manager = WorldMapModuleManager::instance();
    if (manager->state() == WorldMapModuleManager::State::Failed)
        return; // 失败文案由 showFailure 负责

    m_progress->setVisible(true);
    m_retryBtn->setVisible(false);

    switch (manager->state()) {
    case WorldMapModuleManager::State::Preparing:
        m_progress->setRange(0, 0);
        m_statusLabel->setText(tr("正在准备地图模块…"));
        break;
    case WorldMapModuleManager::State::Downloading:
        m_progress->setRange(0, 100);
        updateProgress(manager->bytesReceived(), manager->bytesTotal());
        break;
    case WorldMapModuleManager::State::Verifying:
        m_progress->setRange(0, 0);
        m_statusLabel->setText(tr("正在校验下载文件…"));
        break;
    case WorldMapModuleManager::State::Extracting:
        m_progress->setRange(0, 100);
        m_statusLabel->setText(tr("正在解压地图模块…"));
        break;
    case WorldMapModuleManager::State::Loading:
        m_progress->setRange(0, 0);
        m_statusLabel->setText(tr("正在加载地图模块…"));
        break;
    default:
        break;
    }
}

void WorldMapEntryPage::updateProgress(qint64 received, qint64 total)
{
    if (!m_progress || !m_progress->isVisible())
        return;
    if (total > 0) {
        const int percent = int(std::lround(double(received) * 100.0 / double(total)));
        m_progress->setRange(0, 100);
        m_progress->setValue(qBound(0, percent, 100));
        const QString sizeText = formatBytes(total);
        m_statusLabel->setText(sizeText.isEmpty()
                                   ? tr("正在下载地图模块… %1%").arg(percent)
                                   : tr("正在下载地图模块… %1%（%2）").arg(percent).arg(sizeText));
    } else {
        m_progress->setRange(0, 0);
        m_statusLabel->setText(tr("正在下载地图模块…"));
    }
}

void WorldMapEntryPage::showFailure(const QString &message)
{
    m_progress->setVisible(false);
    m_retryBtn->setVisible(true);
    m_statusLabel->setStyleSheet(StyleKit::dangerLabel());
    m_statusLabel->setText(message);
}

void WorldMapEntryPage::ensureModulePage()
{
    if (m_modulePage)
        return;
    m_modulePage = WorldMapModuleManager::instance()->createPage(m_moduleHost);
    if (!m_modulePage) {
        showFailure(WorldMapModuleManager::instance()->errorString());
        return;
    }
    m_moduleLayout->addWidget(m_modulePage);
}

void WorldMapEntryPage::callPageInstancePath()
{
    if (!m_modulePage || m_instancePath.isEmpty())
        return;
    QMetaObject::invokeMethod(m_modulePage, "setInstancePath", Qt::DirectConnection,
                              Q_ARG(QString, m_instancePath));
}

void WorldMapEntryPage::callPageCurrentSave()
{
    if (!m_modulePage || !m_hasPendingSave)
        return;
    QMetaObject::invokeMethod(m_modulePage, "setCurrentSave", Qt::DirectConnection,
                              Q_ARG(QString, m_pendingSavePath),
                              Q_ARG(QString, m_pendingSaveName));
}

void WorldMapEntryPage::callPageReset()
{
    if (!m_modulePage)
        return;
    QMetaObject::invokeMethod(m_modulePage, "reset", Qt::DirectConnection);
}

void WorldMapEntryPage::callPageRefresh()
{
    if (!m_modulePage)
        return;
    QMetaObject::invokeMethod(m_modulePage, "refresh", Qt::DirectConnection);
}

void WorldMapEntryPage::applyPendingSave()
{
    if (!m_modulePage || !m_hasPendingSave)
        return;
    callPageCurrentSave();
    m_hasPendingSave = false;
}

void WorldMapEntryPage::setCurrentSave(const QString &saveFolderPath,
                                       const QString &saveName)
{
    m_pendingSavePath = saveFolderPath;
    m_pendingSaveName = saveName;
    m_hasPendingSave = true;

    WorldMapModuleManager *manager = WorldMapModuleManager::instance();
    if (manager->isReady()) {
        ensureModulePage();
        if (!m_modulePage)
            return;
        m_stack->setCurrentWidget(m_moduleHost);
        if (!m_instancePath.isEmpty())
            callPageInstancePath();
        applyPendingSave();
        return;
    }

    m_stack->setCurrentWidget(m_loadingPage);
    m_statusLabel->setStyleSheet(StyleKit::secondaryLabel());
    m_retryBtn->setVisible(false);
    m_progress->setVisible(true);
    m_statusLabel->setText(tr("正在准备地图模块…"));
    manager->ensureReady();
}

void WorldMapEntryPage::setInstancePath(const QString &instancePath)
{
    m_instancePath = instancePath;
    callPageInstancePath();
}

void WorldMapEntryPage::reset()
{
    m_hasPendingSave = false;
    m_pendingSavePath.clear();
    m_pendingSaveName.clear();
    callPageReset();
    m_stack->setCurrentWidget(m_loadingPage);
}

void WorldMapEntryPage::refresh()
{
    callPageRefresh();
}

#else // !BLOCKBOX_WORLDMAP_REMOTE_MODULE

// ═══════════════════════════════════════════════════════════════════
// 移动端 / Windows ARM64：继续使用内置地图页
// ═══════════════════════════════════════════════════════════════════

WorldMapEntryPage::WorldMapEntryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
}

WorldMapPage *WorldMapEntryPage::ensurePage()
{
    if (m_page)
        return m_page;
    m_page = new WorldMapPage(this);
    layout()->addWidget(m_page);
    return m_page;
}

void WorldMapEntryPage::setCurrentSave(const QString &saveFolderPath,
                                       const QString &saveName)
{
    m_pendingSavePath = saveFolderPath;
    m_pendingSaveName = saveName;
    m_hasPendingSave = true;
    WorldMapPage *page = ensurePage();
    if (!m_instancePath.isEmpty())
        page->setInstancePath(m_instancePath);
    page->setCurrentSave(m_pendingSavePath, m_pendingSaveName);
    m_hasPendingSave = false;
}

void WorldMapEntryPage::setInstancePath(const QString &instancePath)
{
    m_instancePath = instancePath;
    if (m_page)
        m_page->setInstancePath(instancePath);
}

void WorldMapEntryPage::reset()
{
    m_hasPendingSave = false;
    m_pendingSavePath.clear();
    m_pendingSaveName.clear();
    if (m_page)
        m_page->reset();
}

void WorldMapEntryPage::refresh()
{
    if (m_page)
        m_page->refresh();
}

#endif // BLOCKBOX_WORLDMAP_REMOTE_MODULE
