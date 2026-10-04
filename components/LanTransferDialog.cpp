/**
 * @file   LanTransferDialog.cpp
 * @brief  文件传输弹窗实现：选设备 → 内嵌资源管理器选位置 → 发送
 * @author BlockBox Team
 * @date   2026-09-26
 */
#include "LanTransferDialog.h"

#include "AppMessageBox.h"
#include "NotificationManager.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/tunnel/TunnelEngine.h"
#include "utils/tunnel/TunnelManager.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const char kCardName[] = "lanTransferCard";

} // namespace

LanTransferDialog::LanTransferDialog(const QStringList &localPaths, QWidget *parent)
    : AppDialogBase(parent)
    , m_localPaths(localPaths)
{
    setObjectName(QStringLiteral("lanTransferDialog"));
    setWindowTitle(tr("文件传输"));

    initUI();
    initStyle();
    updateSourceSummary();

    LanTransfer *service = LanTransfer::instance();
    // 只负责搜索设备：接收服务由启动时常驻逻辑 / 实例管理页控制，避免弹窗擅自改状态
    service->startDiscovery();

    connect(service, &LanTransfer::devicesChanged,
            this, &LanTransferDialog::rebuildDeviceList);
    connect(service, &LanTransfer::rootsReceived, this,
            [this](const QString &deviceId, const QList<LanShareRoot> &roots) {
                if (deviceId != m_device.id || !m_device.isValid())
                    return;
                m_roots = roots;
                if (m_currentPath.isEmpty())
                    renderRoots();
            });
    connect(service, &LanTransfer::directoryReceived, this,
            [this](const QString &deviceId, const QString &path,
                   const QList<LanEntry> &entries) {
                if (deviceId != m_device.id || path != m_currentPath)
                    return;
                renderEntries(entries);
            });
    connect(service, &LanTransfer::browseFailed, this,
            [this](const QString &deviceId, const QString &error) {
                if (deviceId != m_device.id || !m_device.isValid())
                    return;
                m_entryStatus->setText(tr("读取失败：%1").arg(error));
                AppMessageBox::warning(this, tr("文件传输"),
                                       tr("无法读取对方目录：%1").arg(error));
            });

    rebuildDeviceList();
    showDeviceStep();
}

// ───────────────────────────── UI ─────────────────────────────

void LanTransferDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kCardName));
    card->setMinimumSize(720, 520);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 18, 24, 18);
    cardLayout->setSpacing(10);

    // ── 标题栏 ──
    QWidget *header = new QWidget(card);
    QHBoxLayout *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(2, 2, 2, 2);
    headerRow->setSpacing(10);

    QLabel *icon = new QLabel(header);
    icon->setObjectName(QStringLiteral("lanHeaderIcon"));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_StyledBackground, true);
    icon->setPixmap(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/share.svg"),
                                                ThemeManager::instance()->currentThemeColor(),
                                                18).pixmap(18, 18));
    headerRow->addWidget(icon);

    QWidget *titleBox = new QWidget(header);
    QVBoxLayout *titleCol = new QVBoxLayout(titleBox);
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(1);
    m_titleLabel = new QLabel(tr("文件传输"), titleBox);
    m_titleLabel->setObjectName(QStringLiteral("lanTitle"));
    m_subtitleLabel = new QLabel(tr("把本地资源发送到同一网络下的其他方块盒子"),
                                 titleBox);
    m_subtitleLabel->setObjectName(QStringLiteral("lanSubtitle"));
    titleCol->addWidget(m_titleLabel);
    titleCol->addWidget(m_subtitleLabel);
    headerRow->addWidget(titleBox, 1);

    m_closeBtn = new QToolButton(header);
    m_closeBtn->setObjectName(QStringLiteral("lanCloseBtn"));
    m_closeBtn->setAutoRaise(true);
    m_closeBtn->setIcon(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/close.svg"),
                                                    ThemeManager::instance()->currentTextColor(),
                                                    14));
    m_closeBtn->setIconSize(QSize(14, 14));
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setToolTip(tr("关闭"));
    connect(m_closeBtn, &QToolButton::clicked, this, &QDialog::reject);
    headerRow->addWidget(m_closeBtn);
    cardLayout->addWidget(header);

    // ── 步骤条 ──
    QWidget *steps = new QWidget(card);
    QHBoxLayout *stepRow = new QHBoxLayout(steps);
    stepRow->setContentsMargins(2, 4, 2, 4);
    stepRow->setSpacing(8);
    m_stepDevice = new QLabel(tr("1  选择设备"), steps);
    m_stepDevice->setObjectName(QStringLiteral("lanStep"));
    m_stepPath = new QLabel(tr("2  选择传输位置"), steps);
    m_stepPath->setObjectName(QStringLiteral("lanStep"));
    stepRow->addWidget(m_stepDevice);
    stepRow->addWidget(m_stepPath);
    stepRow->addStretch();
    cardLayout->addWidget(steps);

    // ── 主体 ──
    m_bodyStack = new QStackedWidget(card);
    m_bodyStack->setObjectName(QStringLiteral("lanBodyStack"));

    // 第一步：设备列表
    QWidget *devicePage = new QWidget(m_bodyStack);
    QVBoxLayout *deviceLayout = new QVBoxLayout(devicePage);
    deviceLayout->setContentsMargins(0, 6, 0, 0);
    deviceLayout->setSpacing(8);

    QWidget *deviceBar = new QWidget(devicePage);
    QHBoxLayout *deviceBarRow = new QHBoxLayout(deviceBar);
    deviceBarRow->setContentsMargins(0, 0, 0, 0);
    deviceBarRow->setSpacing(8);
    m_deviceStatusLabel = new QLabel(tr("正在搜索局域网设备…"), deviceBar);
    m_deviceStatusLabel->setObjectName(QStringLiteral("lanStatus"));
    deviceBarRow->addWidget(m_deviceStatusLabel, 1);
    m_refreshBtn = new QPushButton(tr("重新搜索"), deviceBar);
    m_refreshBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        LanTransfer::instance()->startDiscovery();
        m_deviceStatusLabel->setText(tr("正在搜索局域网设备…"));
    });
    deviceBarRow->addWidget(m_refreshBtn);
    deviceLayout->addWidget(deviceBar);

    m_deviceList = new QListWidget(devicePage);
    m_deviceList->setObjectName(QStringLiteral("lanDeviceList"));
    m_deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    deviceLayout->addWidget(m_deviceList, 1);

    m_deviceHint = new QLabel(
        tr("需要对方也打开方块盒子，并处于同一局域网。设备每 2 秒自动刷新一次。"),
        devicePage);
    m_deviceHint->setObjectName(QStringLiteral("lanHint"));
    m_deviceHint->setWordWrap(true);
    deviceLayout->addWidget(m_deviceHint);

    m_bodyStack->addWidget(devicePage);

    // 第二步：资源管理器
    QWidget *browserPage = new QWidget(m_bodyStack);
    QVBoxLayout *browserLayout = new QVBoxLayout(browserPage);
    browserLayout->setContentsMargins(0, 6, 0, 0);
    browserLayout->setSpacing(8);

    QWidget *navBar = new QWidget(browserPage);
    QHBoxLayout *navRow = new QHBoxLayout(navBar);
    navRow->setContentsMargins(0, 0, 0, 0);
    navRow->setSpacing(6);
    m_rootBtn = new QPushButton(tr("共享位置"), navBar);
    m_rootBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_rootBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rootBtn, &QPushButton::clicked, this, [this]() {
        m_currentPath.clear();
        updateLocationBar();
        renderRoots();
    });
    navRow->addWidget(m_rootBtn);
    m_upBtn = new QPushButton(tr("上一级"), navBar);
    m_upBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_upBtn->setCursor(Qt::PointingHandCursor);
    connect(m_upBtn, &QPushButton::clicked, this, &LanTransferDialog::goUpRemote);
    navRow->addWidget(m_upBtn);
    m_pathLabel = new QLabel(tr("共享位置"), navBar);
    m_pathLabel->setObjectName(QStringLiteral("lanPath"));
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    navRow->addWidget(m_pathLabel, 1);
    browserLayout->addWidget(navBar);

    m_entryList = new QListWidget(browserPage);
    m_entryList->setObjectName(QStringLiteral("lanEntryList"));
    m_entryList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_entryList, &QListWidget::itemDoubleClicked,
            this, &LanTransferDialog::onItemActivated);
    browserLayout->addWidget(m_entryList, 1);

    m_entryStatus = new QLabel(browserPage);
    m_entryStatus->setObjectName(QStringLiteral("lanStatus"));
    browserLayout->addWidget(m_entryStatus);

    m_bodyStack->addWidget(browserPage);
    cardLayout->addWidget(m_bodyStack, 1);

    // ── 摘要 ──
    m_sourceLabel = new QLabel(card);
    m_sourceLabel->setObjectName(QStringLiteral("lanSummary"));
    m_sourceLabel->setWordWrap(true);
    cardLayout->addWidget(m_sourceLabel);

    m_targetLabel = new QLabel(card);
    m_targetLabel->setObjectName(QStringLiteral("lanSummary"));
    m_targetLabel->setWordWrap(true);
    cardLayout->addWidget(m_targetLabel);

    // ── 底部按钮 ──
    QWidget *footer = new QWidget(card);
    QHBoxLayout *footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(2, 6, 2, 2);
    footerRow->setSpacing(8);
    m_cancelBtn = new QPushButton(tr("取消"), footer);
    m_cancelBtn->setObjectName(QStringLiteral("cancelButton"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    footerRow->addWidget(m_cancelBtn);
    footerRow->addStretch();
    m_backBtn = new QPushButton(tr("上一步"), footer);
    m_backBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_backBtn->setCursor(Qt::PointingHandCursor);
    connect(m_backBtn, &QPushButton::clicked, this, &LanTransferDialog::goPrevStep);
    footerRow->addWidget(m_backBtn);
    m_nextBtn = new QPushButton(tr("下一步"), footer);
    m_nextBtn->setObjectName(QStringLiteral("primaryButton"));
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_bodyStack->currentIndex() == 0)
            goNextStep();
        else
            startTransfer();
    });
    footerRow->addWidget(m_nextBtn);
    cardLayout->addWidget(footer);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);
}

void LanTransferDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";
    const QString subColor   = isLight ? "#64748B" : "#9aa3b2";
    const QString muted      = isLight ? "#94A3B8" : "#7a8290";
    const QString fieldBg    = isLight ? "#f4f4f4" : "#3b3b40";
    const QString fieldBorder= isLight ? "#d4d4d4" : "#55555a";
    const QString hoverBg    = isLight ? "rgba(15,23,42,0.06)" : "rgba(255,255,255,0.06)";
    const QString selBg      = isLight ? "rgba(16,185,129,0.14)" : "rgba(16,185,129,0.20)";
    const QString listBg     = isLight ? "#fafafa" : "#38383d";

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QLabel#lanTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#lanSubtitle { color: %6; font-size: 12px; background: transparent; }"
        "QLabel#lanHeaderIcon { background: %5; border-radius: 9px; }"
        "QToolButton#lanCloseBtn { background: transparent; border: none; border-radius: 8px; }"
        "QToolButton#lanCloseBtn:hover { background-color: rgba(239,68,68,0.15); }"
        "QLabel#lanStep {"
        "    background-color: %7; border: 1px solid %8; border-radius: 999px;"
        "    color: %9; padding: 5px 14px; font-size: 12px;"
        "}"
        "QLabel#lanStep[stepState=\"current\"] { background-color: %5; border-color: %5; color: #ffffff; font-weight: 700; }"
        "QLabel#lanStep[stepState=\"done\"] { background-color: %5; border-color: %5; color: #ffffff; font-weight: 600; }"
        "QLabel#lanStatus { color: %6; font-size: 12px; background: transparent; }"
        "QLabel#lanHint { color: %9; font-size: 11.5px; background: transparent; }"
        "QLabel#lanPath { color: %4; font-size: 12px; font-family: 'Consolas','Cascadia Code'; background: transparent; }"
        "QLabel#lanSummary { color: %6; font-size: 11.5px; background: transparent; }"
        "QListWidget#lanDeviceList, QListWidget#lanEntryList {"
        "    background-color: %10; border: 1px solid %8; border-radius: 10px;"
        "    color: %7; font-size: 12.5px; outline: none; padding: 4px;"
        "}"
        "QListWidget#lanDeviceList::item, QListWidget#lanEntryList::item {"
        "    border-radius: 7px; padding: 8px 10px; min-height: 24px;"
        "}"
        "QListWidget#lanDeviceList::item:hover, QListWidget#lanEntryList::item:hover { background-color: %11; }"
        "QListWidget#lanDeviceList::item:selected, QListWidget#lanEntryList::item:selected {"
        "    background-color: %12; color: %4;"
        "}"
        "QPushButton#lanGhostBtn {"
        "    background-color: %7; border: 1px solid %8; border-radius: 8px;"
        "    color: %6; padding: 6px 14px; font-size: 12px;"
        "}"
        "QPushButton#lanGhostBtn:hover { background-color: %11; color: %4; }"
        "QPushButton#lanGhostBtn:disabled { color: %9; }"
    ).arg(QString::fromLatin1(kCardName), cardBg, cardBorder, titleColor, themeColor,
          subColor, textColor, fieldBorder, muted, listBg, hoverBg, selBg, themeHover);

    setStyleSheet(style);
}

// ───────────────────────────── 交互 ─────────────────────────────

void LanTransferDialog::rebuildDeviceList()
{
    const QString previous = selectedDeviceId();
    m_deviceList->clear();
    m_remoteDevices.clear();

    int count = 0;

    const QList<LanDevice> devices = LanTransfer::instance()->devices();
    for (const LanDevice &dev : devices)
    {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1    %2").arg(dev.name, dev.address), m_deviceList);
        item->setData(Qt::UserRole, dev.id);
        item->setToolTip(tr("局域网设备 · 传输服务端口：%1\n共享位置：%2")
                             .arg(dev.port)
                             .arg(dev.roots.isEmpty()
                                      ? tr("读取中…")
                                      : [&]() {
                                            QStringList names;
                                            for (const LanShareRoot &root : dev.roots)
                                                names << root.name;
                                            return names.join(QStringLiteral("、"));
                                        }()));
        ++count;
    }

    // 远程互传（内网穿透）节点：来自 EasyTier 虚拟网络
    loadRemoteDevices();
    for (const LanDevice &dev : m_remoteDevices)
    {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1    %2（远程）").arg(dev.name, dev.address),
            m_deviceList);
        item->setData(Qt::UserRole, dev.id);
        item->setToolTip(tr("远程设备 · 通过内网穿透直连\n虚拟地址：%1\n"
                            "需要对方也开启「远程互传」并填入相同邀请码")
                             .arg(dev.address));
        ++count;
    }

    if (!previous.isEmpty())
    {
        for (int i = 0; i < m_deviceList->count(); ++i)
        {
            if (m_deviceList->item(i)->data(Qt::UserRole).toString() == previous)
            {
                m_deviceList->setCurrentRow(i);
                break;
            }
        }
    }

    if (count == 0)
    {
        m_deviceStatusLabel->setText(
            TunnelManager::instance()->isRunning()
                ? tr("暂未发现设备：确认对方已打开方块盒子或开启远程互传")
                : tr("未搜索到设备，请确认对方已打开方块盒子"));
    }
    else
    {
        m_deviceStatusLabel->setText(tr("发现 %1 台可用设备（含 %2 台远程）")
                                         .arg(count)
                                         .arg(m_remoteDevices.size()));
    }

    onDeviceSelectionChanged();
}

void LanTransferDialog::loadRemoteDevices()
{
    TunnelManager *tunnel = TunnelManager::instance();
    if (!tunnel->isRunning())
        return;

    const TunnelStatus st = tunnel->status();
    for (const TunnelPeer &peer : st.peers)
    {
        if (peer.isSelf || !peer.isValid())
            continue;
        LanDevice dev;
        dev.id = QStringLiteral("et:") + peer.virtualIp;
        dev.name = peer.hostname.isEmpty() ? peer.virtualIp : peer.hostname;
        dev.address = peer.virtualIp;
        dev.port = LanTransfer::kServicePort;
        dev.remote = true;
        dev.lastSeen = QDateTime::currentMSecsSinceEpoch();
        dev.roots = LanTransfer::instance()->shareRoots();
        m_remoteDevices.insert(dev.id, dev);
    }
}

void LanTransferDialog::onDeviceSelectionChanged()
{
    m_nextBtn->setEnabled(m_deviceList->currentItem() != nullptr);
}

QString LanTransferDialog::selectedDeviceId() const
{
    const QListWidgetItem *item = m_deviceList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void LanTransferDialog::showDeviceStep()
{
    m_bodyStack->setCurrentIndex(0);
    m_stepDevice->setProperty("stepState", "current");
    m_stepPath->setProperty("stepState", "todo");
    m_stepDevice->style()->unpolish(m_stepDevice);
    m_stepDevice->style()->polish(m_stepDevice);
    m_stepPath->style()->unpolish(m_stepPath);
    m_stepPath->style()->polish(m_stepPath);

    m_backBtn->hide();
    m_nextBtn->setText(tr("下一步"));
    m_targetLabel->setText(tr("目标位置：尚未选择"));
    onDeviceSelectionChanged();
}

void LanTransferDialog::showExplorerStep()
{
    // 换设备前先释放上一次的转发
    releaseForward();

    const QString id = selectedDeviceId();
    m_device = LanDevice();

    const QList<LanDevice> devices = LanTransfer::instance()->devices();
    for (const LanDevice &dev : devices)
    {
        if (dev.id == id)
        {
            m_device = dev;
            break;
        }
    }

    if (!m_device.isValid() && m_remoteDevices.contains(id))
    {
        // 远程设备：经内网穿透把对方的传输端口映射到本机回环地址
        LanDevice remote = m_remoteDevices.value(id);
        TunnelEngine *engine = TunnelManager::instance()->engine();
        const QString forward =
            engine ? engine->openForward(remote.address, LanTransfer::kServicePort)
                   : QString();
        if (forward.isEmpty())
        {
            AppMessageBox::warning(
                this, tr("文件传输"),
                tr("无法连接到 %1，请确认双方都开启了远程互传且邀请码一致")
                    .arg(remote.name));
            return;
        }
        const int sep = forward.lastIndexOf(QLatin1Char(':'));
        remote.address = forward.left(sep);
        remote.port = static_cast<quint16>(forward.mid(sep + 1).toUShort());
        m_activeForward = forward;
        m_device = remote;
    }

    if (!m_device.isValid())
    {
        AppMessageBox::warning(this, tr("文件传输"), tr("设备已离线，请重新选择"));
        return;
    }

    m_bodyStack->setCurrentIndex(1);
    m_stepDevice->setProperty("stepState", "done");
    m_stepPath->setProperty("stepState", "current");
    m_stepDevice->style()->unpolish(m_stepDevice);
    m_stepDevice->style()->polish(m_stepDevice);
    m_stepPath->style()->unpolish(m_stepPath);
    m_stepPath->style()->polish(m_stepPath);

    m_backBtn->show();
    m_nextBtn->setText(tr("开始传输"));
    m_nextBtn->setEnabled(false);

    m_currentPath.clear();
    m_roots.clear();
    m_entryList->clear();
    m_entryStatus->setText(tr("正在读取 %1 的共享位置…").arg(m_device.name));
    updateLocationBar();
    LanTransfer::instance()->browseRoots(m_device);
}

void LanTransferDialog::goNextStep()
{
    if (selectedDeviceId().isEmpty())
    {
        AppMessageBox::information(this, tr("文件传输"), tr("请先选择一台可用设备"));
        return;
    }
    showExplorerStep();
}

void LanTransferDialog::goPrevStep()
{
    showDeviceStep();
    releaseForward();
}

void LanTransferDialog::releaseForward()
{
    if (m_activeForward.isEmpty())
        return;
    TunnelEngine *engine = TunnelManager::instance()->engine();
    if (engine)
        engine->closeForward(m_activeForward);
    m_activeForward.clear();
}

LanTransferDialog::~LanTransferDialog()
{
    // 传输已启动时转发交给 sendFinished 释放，这里只清理未交接的
    releaseForward();
}

void LanTransferDialog::renderRoots()
{
    m_entryList->clear();
    for (const LanShareRoot &root : m_roots)
    {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(root.name, root.path), m_entryList);
        item->setData(Qt::UserRole, root.path);
        item->setToolTip(root.path);
    }
    m_entryStatus->setText(tr("双击进入要传输到的文件夹（共 %1 个共享位置）")
                               .arg(m_roots.size()));
    if (m_roots.isEmpty())
        m_entryStatus->setText(tr("对方没有可写的共享位置"));
    updateLocationBar();
}

void LanTransferDialog::renderEntries(const QList<LanEntry> &entries)
{
    m_entryList->clear();
    for (const LanEntry &entry : entries)
    {
        const QString text = entry.isDir
            ? QStringLiteral("  %1").arg(entry.name)
            : QStringLiteral("  %1    %2").arg(entry.name, formatSize(entry.size));
        auto *item = new QListWidgetItem(text, m_entryList);
        item->setData(Qt::UserRole, entry.isDir
                        ? (m_currentPath + QLatin1Char('/') + entry.name)
                        : QString());
        if (entry.isDir)
            item->setToolTip(tr("双击进入 %1").arg(entry.name));
        else
            item->setToolTip(tr("%1（%2）").arg(entry.name, formatSize(entry.size)));
    }

    m_entryStatus->setText(tr("%1 项 · 双击文件夹进入，文件夹即为传输位置")
                               .arg(entries.size()));
    updateLocationBar();
}

void LanTransferDialog::onItemActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
        return;

    m_currentPath = path;
    m_entryList->clear();
    m_entryStatus->setText(tr("正在读取…"));
    updateLocationBar();
    LanTransfer::instance()->browseDirectory(m_device, path);
}

void LanTransferDialog::goUpRemote()
{
    if (m_currentPath.isEmpty())
        return;

    bool atRoot = false;
    for (const LanShareRoot &root : m_roots)
    {
        if (root.path == m_currentPath)
        {
            atRoot = true;
            break;
        }
    }
    if (atRoot)
    {
        m_currentPath.clear();
        renderRoots();
        return;
    }

    const QString parent = QFileInfo(m_currentPath).path();
    if (parent == m_currentPath)
    {
        m_currentPath.clear();
        renderRoots();
        return;
    }

    m_currentPath = parent;
    m_entryList->clear();
    m_entryStatus->setText(tr("正在读取…"));
    updateLocationBar();
    LanTransfer::instance()->browseDirectory(m_device, parent);
}

void LanTransferDialog::updateLocationBar()
{
    m_pathLabel->setText(m_currentPath.isEmpty() ? tr("共享位置") : m_currentPath);
    if (m_currentPath.isEmpty())
        m_targetLabel->setText(tr("目标位置：请选择一个文件夹"));
    else
        m_targetLabel->setText(tr("目标位置：%1").arg(m_currentPath));

    const bool canSend = !m_currentPath.isEmpty() && !m_localPaths.isEmpty()
        && m_device.isValid();
    m_nextBtn->setEnabled(canSend);
    m_upBtn->setEnabled(!m_currentPath.isEmpty());
}

void LanTransferDialog::updateSourceSummary()
{
    if (m_localPaths.isEmpty())
    {
        m_sourceLabel->setText(tr("待发送：未选择资源"));
        return;
    }

    qint64 total = 0;
    int fileCount = 0;
    QStringList names;
    for (const QString &path : m_localPaths)
    {
        const QFileInfo fi(path);
        names << fi.fileName();
        if (fi.isDir())
        {
            QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
            {
                it.next();
                total += it.fileInfo().size();
                ++fileCount;
            }
        }
        else
        {
            total += fi.size();
            ++fileCount;
        }
    }

    QString displayName;
    if (names.size() <= 2)
        displayName = names.join(QStringLiteral("、"));
    else
        displayName = tr("%1 等 %2 项").arg(names.first()).arg(names.size());

    m_sourceLabel->setText(tr("待发送：%1 · 共 %2 个文件 · %3")
                               .arg(displayName)
                               .arg(fileCount)
                               .arg(formatSize(total)));
}

void LanTransferDialog::startTransfer()
{
    if (m_currentPath.isEmpty())
    {
        AppMessageBox::information(this, tr("文件传输"), tr("请先进入一个目标文件夹"));
        return;
    }
    if (m_localPaths.isEmpty())
    {
        AppMessageBox::information(this, tr("文件传输"), tr("没有可发送的资源"));
        return;
    }

    const QString taskId = LanTransfer::instance()->sendFiles(m_device, m_localPaths,
                                                              m_currentPath);
    if (taskId.isEmpty())
    {
        const QString reason = LanTransfer::instance()->isSending()
            ? tr("已有传输正在进行，请等待完成后再试")
            : tr("无法连接到 %1，请确认对方仍在运行方块盒子").arg(m_device.name);
        AppMessageBox::warning(this, tr("文件传输"), reason);
        return;
    }

    m_started = true;

    // 传输在后台进行，转发要在传输结束后再释放：
    // 交给 TunnelManager 作为接收对象，避免弹窗销毁时把连接掐断
    if (!m_activeForward.isEmpty())
    {
        const QString forward = m_activeForward;
        m_activeForward.clear();
        connect(LanTransfer::instance(), &LanTransfer::sendFinished,
                TunnelManager::instance(), [forward](const QString &, bool, const QString &) {
                    TunnelEngine *engine = TunnelManager::instance()->engine();
                    if (engine)
                        engine->closeForward(forward);
                },
                Qt::SingleShotConnection);
    }

    NotificationManager::showSuccess(window(), tr("已开始传输，进度可在任务卡片中查看"));
    accept();
}

QString LanTransferDialog::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    if (bytes < qint64(1024) * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0,
                                       'f', 2);
}

