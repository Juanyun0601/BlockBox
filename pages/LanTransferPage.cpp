/**
 * @file   LanTransferPage.cpp
 * @brief  实例管理 - 文件传输页面实现
 * @author BlockBox Team
 * @date   2026-09-26
 */
#include "LanTransferPage.h"

#include "components/AppFileDialog.h"
#include "components/AppMessageBox.h"
#include "components/LanTransferDialog.h"
#include "components/NotificationManager.h"
#include "components/OutlinedLabel.h"
#include "utils/IconHelper.h"
#include "utils/LanTransfer.h"
#include "utils/ThemeManager.h"
#include "utils/tunnel/TunnelEngine.h"
#include "utils/tunnel/TunnelManager.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkInterface>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include "utils/FileExplorer.h"

LanTransferPage::LanTransferPage(QWidget *parent)
    : QWidget(parent)
    , m_sendModeBtn(nullptr)
    , m_recvModeBtn(nullptr)
    , m_modeStack(nullptr)
{
    initUI();
    initStyle();

    LanTransfer *service = LanTransfer::instance();
    connect(service, &LanTransfer::devicesChanged,
            this, &LanTransferPage::rebuildDeviceList);
    connect(service, &LanTransfer::receivingChanged, this,
            [this](bool running) { updateReceiverInfo(); Q_UNUSED(running); });
    connect(service, &LanTransfer::fileReceived,
            this, &LanTransferPage::onFileReceived);
    connect(service, &LanTransfer::sendFinished, this,
            [this](const QString &, bool, const QString &) { updateSendButtonState(); });

    // 远程互传（内网穿透）
    TunnelManager *tunnel = TunnelManager::instance();
    connect(tunnel, &TunnelManager::statusChanged, this,
            &LanTransferPage::updateRemoteUi);
    connect(tunnel, &TunnelManager::peersChanged, this,
            &LanTransferPage::rebuildPeerList);
    connect(tunnel, &TunnelManager::installStateChanged, this,
            &LanTransferPage::updateRemoteUi);
    connect(tunnel, &TunnelManager::downloadProgressChanged, this,
            [this](qreal progress) {
                m_downloadProgressText = progress < 0
                    ? QString()
                    : tr("下载中 %1%").arg(qRound(progress * 100));
                updateDownloadLabels();
            });
    connect(tunnel, &TunnelManager::downloadInfoChanged, this,
            [this](const QString &info) {
                m_downloadInfo = info;
                updateDownloadLabels();
            });
    connect(tunnel, &TunnelManager::installFinished, this,
            [this](bool success, const QString &message) {
                m_downloadProgressText.clear();
                m_downloadInfo.clear();
                updateDownloadLabels();
                if (success)
                    NotificationManager::showSuccess(window(), message);
                else
                    NotificationManager::showError(window(), message);

                // 首次下载组件后，继续完成「开启远程接收」
                if (success && m_pendingRemoteEnable)
                {
                    m_pendingRemoteEnable = false;
                    LanTransfer::instance()->startService();
                    TunnelManager::instance()->start();
                }
                else if (!success)
                {
                    m_pendingRemoteEnable = false;
                }
                updateReceiverInfo();
                updateRemoteUi();
            });
    connect(tunnel, &TunnelManager::errorOccurred, this,
            [this](const QString &message) { m_remoteStatusLabel->setText(message); });
    connect(tunnel, &TunnelManager::peerUrlsChanged, this,
            [this]() { updateRemoteUi(); });

    m_inviteEdit->setText(tunnel->inviteCode());
    updateRemoteUi();
    rebuildPeerList();

    rebuildDeviceList();
    rebuildFileList();

    // 进入页面即开始搜索局域网设备（接收服务在启动时常驻，状态见下方开关）
    service->startDiscovery();
    updateReceiverInfo();
}

LanTransferPage::~LanTransferPage() = default;

void LanTransferPage::setInstancePath(const QString &path)
{
    m_instancePath = path;
}

// ───────────────────────────── UI ─────────────────────────────

void LanTransferPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 16, 24, 16);
    mainLayout->setSpacing(12);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("文件传输"), this);
    titleLabel->setObjectName(QStringLiteral("lanPageTitle"));
    mainLayout->addWidget(titleLabel);

    OutlinedLabel *descLabel = new OutlinedLabel(
        tr("同一局域网内直接互传；不在同一网络时用「远程」标签组网后互传。"
           "每个标签都可以独立完成自己的事：发送要选资源和设备，接收要开开关。"),
        this);
    descLabel->setObjectName(QStringLiteral("lanPageDesc"));
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // 模式切换：局域网发送 / 局域网接收 / 远程发送 / 远程接收
    QWidget *modeBar = new QWidget(this);
    QHBoxLayout *modeRow = new QHBoxLayout(modeBar);
    modeRow->setContentsMargins(0, 4, 0, 4);
    modeRow->setSpacing(6);

    auto createModeBtn = [this, modeBar, modeRow](const QString &text,
                                                  const QString &icon, int mode,
                                                  bool checked) {
        auto *btn = new QPushButton(text, modeBar);
        btn->setObjectName(QStringLiteral("lanModeBtn"));
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setCursor(Qt::PointingHandCursor);

        // 未选中时按钮底色是浅色，白色图标会看不见：按选中态切换图标颜色
        ThemeManager *tm = ThemeManager::instance();
        const QColor idleColor(tm->currentTheme() == ThemeManager::LightTheme
                                   ? QStringLiteral("#333333")
                                   : QStringLiteral("#e8e8e8"));
        const QColor checkedColor(QStringLiteral("#ffffff"));
        auto applyIcon = [btn, icon, idleColor, checkedColor](bool on) {
            btn->setIcon(IconHelper::loadColoredIcon(
                icon, on ? checkedColor : idleColor, 16));
            btn->setIconSize(QSize(16, 16));
        };
        applyIcon(checked);
        connect(btn, &QPushButton::toggled, btn, applyIcon);

        connect(btn, &QPushButton::clicked, this, [this, mode]() { switchMode(mode); });
        modeRow->addWidget(btn);
        return btn;
    };

    m_sendModeBtn = createModeBtn(tr("局域网发送"),
                                  QStringLiteral(":/Images/Icons/share.svg"), 0, true);
    m_recvModeBtn = createModeBtn(tr("局域网接收"),
                                  QStringLiteral(":/Images/Icons/download.svg"), 1, false);
    m_remoteModeBtn = createModeBtn(tr("远程发送"),
                                    QStringLiteral(":/Images/Icons/globe.svg"), 2, false);
    m_remoteRecvModeBtn = createModeBtn(tr("远程接收"),
                                        QStringLiteral(":/Images/Icons/globe.svg"), 3,
                                        false);

    modeRow->addStretch();
    mainLayout->addWidget(modeBar);

    m_modeStack = new QStackedWidget(this);

    // ── 局域网发送面板 ──
    QWidget *sendPage = new QWidget(m_modeStack);
    QVBoxLayout *sendLayout = new QVBoxLayout(sendPage);
    sendLayout->setContentsMargins(0, 6, 0, 0);
    sendLayout->setSpacing(10);

    OutlinedLabel *sendHint = new OutlinedLabel(
        tr("先选择要发送的本地资源，再选择同一局域网中的目标设备；"
           "进入下一步后可用资源管理器挑选对方设备上的传输位置。"), sendPage);
    sendHint->setObjectName(QStringLiteral("lanPageHint"));
    sendHint->setWordWrap(true);
    sendLayout->addWidget(sendHint);

    QWidget *pickBar = new QWidget(sendPage);
    QHBoxLayout *pickRow = new QHBoxLayout(pickBar);
    pickRow->setContentsMargins(0, 0, 0, 0);
    pickRow->setSpacing(8);
    m_pickFilesBtn = new QPushButton(tr("选择文件"), pickBar);
    m_pickFilesBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_pickFilesBtn->setCursor(Qt::PointingHandCursor);
    connect(m_pickFilesBtn, &QPushButton::clicked, this, &LanTransferPage::pickFiles);
    m_pickFolderBtn = new QPushButton(tr("选择文件夹"), pickBar);
    m_pickFolderBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_pickFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_pickFolderBtn, &QPushButton::clicked, this, &LanTransferPage::pickFolder);
    m_clearBtn = new QPushButton(tr("清空"), pickBar);
    m_clearBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    connect(m_clearBtn, &QPushButton::clicked, this, &LanTransferPage::clearPicked);
    pickRow->addWidget(m_pickFilesBtn);
    pickRow->addWidget(m_pickFolderBtn);
    pickRow->addWidget(m_clearBtn);
    pickRow->addStretch();
    sendLayout->addWidget(pickBar);

    m_fileList = new QListWidget(sendPage);
    m_fileList->setObjectName(QStringLiteral("lanPageList"));
    m_fileList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileList->setMinimumHeight(120);
    sendLayout->addWidget(m_fileList, 1);

    QWidget *devBar = new QWidget(sendPage);
    QHBoxLayout *devRow = new QHBoxLayout(devBar);
    devRow->setContentsMargins(0, 4, 0, 0);
    devRow->setSpacing(8);
    m_deviceHeader = new OutlinedLabel(tr("局域网设备"), devBar);
    m_deviceHeader->setObjectName(QStringLiteral("lanPageSubtitle"));
    devRow->addWidget(m_deviceHeader);
    devRow->addStretch();
    m_refreshBtn = new QPushButton(tr("刷新设备"), devBar);
    m_refreshBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &LanTransferPage::refreshDevices);
    devRow->addWidget(m_refreshBtn);
    sendLayout->addWidget(devBar);

    m_deviceList = new QListWidget(sendPage);
    m_deviceList->setObjectName(QStringLiteral("lanPageList"));
    m_deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceList->setMinimumHeight(120);
    connect(m_deviceList, &QListWidget::itemSelectionChanged, this,
            &LanTransferPage::onDeviceSelectionChanged);
    connect(m_deviceList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { openSendDialog(); });
    sendLayout->addWidget(m_deviceList, 1);

    QWidget *sendFooter = new QWidget(sendPage);
    QHBoxLayout *sendFooterRow = new QHBoxLayout(sendFooter);
    sendFooterRow->setContentsMargins(0, 4, 0, 0);
    sendFooterRow->addStretch();
    m_sendBtn = new QPushButton(tr("发送到设备…"), sendFooter);
    m_sendBtn->setObjectName(QStringLiteral("primaryButton"));
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sendBtn, &QPushButton::clicked, this, &LanTransferPage::openSendDialog);
    sendFooterRow->addWidget(m_sendBtn);
    sendLayout->addWidget(sendFooter);

    m_modeStack->addWidget(sendPage);

    // ── 局域网接收面板 ──
    QWidget *recvPage = new QWidget(m_modeStack);
    QVBoxLayout *recvLayout = new QVBoxLayout(recvPage);
    recvLayout->setContentsMargins(0, 6, 0, 0);
    recvLayout->setSpacing(10);

    QWidget *switchBar = new QWidget(recvPage);
    QHBoxLayout *switchRow = new QHBoxLayout(switchBar);
    switchRow->setContentsMargins(0, 0, 0, 0);
    switchRow->setSpacing(10);
    m_recvToggleBtn = new QPushButton(tr("开启接收"), switchBar);
    m_recvToggleBtn->setObjectName(QStringLiteral("primaryButton"));
    m_recvToggleBtn->setCheckable(true);
    m_recvToggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_recvToggleBtn, &QPushButton::clicked, this,
            &LanTransferPage::toggleReceiver);
    switchRow->addWidget(m_recvToggleBtn);
    m_recvStatusLabel = new OutlinedLabel(QString(), switchBar);
    m_recvStatusLabel->setObjectName(QStringLiteral("lanPageHint"));
    switchRow->addWidget(m_recvStatusLabel, 1);
    recvLayout->addWidget(switchBar);

    QLabel *infoCard = new QLabel(recvPage);
    infoCard->setObjectName(QStringLiteral("lanInfoCard"));
    infoCard->setWordWrap(true);
    recvLayout->addWidget(infoCard);

    m_recvNameLabel = new OutlinedLabel(QString(), recvPage);
    m_recvNameLabel->setObjectName(QStringLiteral("lanPageHint"));
    m_recvAddrLabel = new OutlinedLabel(QString(), recvPage);
    m_recvAddrLabel->setObjectName(QStringLiteral("lanPageHint"));
    m_recvPortLabel = new OutlinedLabel(QString(), recvPage);
    m_recvPortLabel->setObjectName(QStringLiteral("lanPageHint"));
    m_recvRootsLabel = new OutlinedLabel(QString(), recvPage);
    m_recvRootsLabel->setObjectName(QStringLiteral("lanPageHint"));
    m_recvRootsLabel->setWordWrap(true);
    recvLayout->addWidget(m_recvNameLabel);
    recvLayout->addWidget(m_recvAddrLabel);
    recvLayout->addWidget(m_recvPortLabel);
    recvLayout->addWidget(m_recvRootsLabel);

    OutlinedLabel *recvHint = new OutlinedLabel(
        tr("对方在本地资源卡片上点击文件传输图标，选择你的设备后，"
           "即可浏览本机共享位置并把文件发送过来。传输进度会显示在任务卡片中。"),
        recvPage);
    recvHint->setObjectName(QStringLiteral("lanPageHint"));
    recvHint->setWordWrap(true);
    recvLayout->addWidget(recvHint);

    OutlinedLabel *recvListHeader = new OutlinedLabel(tr("最近接收"), recvPage);
    recvListHeader->setObjectName(QStringLiteral("lanPageSubtitle"));
    recvLayout->addWidget(recvListHeader);

    m_recvList = new QListWidget(recvPage);
    m_recvList->setObjectName(QStringLiteral("lanPageList"));
    m_recvList->setMinimumHeight(140);
    recvLayout->addWidget(m_recvList, 1);

    QWidget *recvFooter = new QWidget(recvPage);
    QHBoxLayout *recvFooterRow = new QHBoxLayout(recvFooter);
    recvFooterRow->setContentsMargins(0, 0, 0, 0);
    recvFooterRow->addStretch();
    m_openRecvBtn = new QPushButton(tr("打开最近接收位置"), recvFooter);
    m_openRecvBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_openRecvBtn->setCursor(Qt::PointingHandCursor);
    m_openRecvBtn->setEnabled(false);
    connect(m_openRecvBtn, &QPushButton::clicked, this,
            &LanTransferPage::openReceivedLocation);
    recvFooterRow->addWidget(m_openRecvBtn);
    recvLayout->addWidget(recvFooter);

    m_modeStack->addWidget(recvPage);

    // ── 远程发送面板（组网 + 资源 + 虚拟网络设备）──
    QWidget *remotePage = new QWidget(m_modeStack);
    QVBoxLayout *remoteLayout = new QVBoxLayout(remotePage);
    remoteLayout->setContentsMargins(0, 6, 0, 0);
    remoteLayout->setSpacing(10);

    OutlinedLabel *remoteHint = new OutlinedLabel(
        tr("不在同一局域网时，先用内网穿透把两台设备接进同一个虚拟网络："
           "双方都开启、并填入相同的邀请码即可。全程 P2P 直连，不经过付费服务器。"),
        remotePage);
    remoteHint->setObjectName(QStringLiteral("lanPageHint"));
    remoteHint->setWordWrap(true);
    remoteLayout->addWidget(remoteHint);

    // 开关 + 状态
    QWidget *tunnelBar = new QWidget(remotePage);
    QHBoxLayout *tunnelRow = new QHBoxLayout(tunnelBar);
    tunnelRow->setContentsMargins(0, 0, 0, 0);
    tunnelRow->setSpacing(8);
    m_tunnelToggleBtn = new QPushButton(tr("开启远程互传"), tunnelBar);
    m_tunnelToggleBtn->setObjectName(QStringLiteral("primaryButton"));
    m_tunnelToggleBtn->setCheckable(true);
    m_tunnelToggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_tunnelToggleBtn, &QPushButton::clicked, this,
            &LanTransferPage::toggleTunnel);
    tunnelRow->addWidget(m_tunnelToggleBtn);

    m_downloadBtn = new QPushButton(tr("下载传输组件"), tunnelBar);
    m_downloadBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadBtn, &QPushButton::clicked, this,
            &LanTransferPage::downloadEngine);
    tunnelRow->addWidget(m_downloadBtn);

    m_installProgressLabel = new OutlinedLabel(QString(), tunnelBar);
    m_installProgressLabel->setObjectName(QStringLiteral("lanPageHint"));
    tunnelRow->addWidget(m_installProgressLabel);
    tunnelRow->addStretch();
    remoteLayout->addWidget(tunnelBar);

    m_remoteStatusLabel = new OutlinedLabel(QString(), remotePage);
    m_remoteStatusLabel->setObjectName(QStringLiteral("lanPageHint"));
    m_remoteStatusLabel->setWordWrap(true);
    remoteLayout->addWidget(m_remoteStatusLabel);

    // 邀请码
    QWidget *inviteBar = new QWidget(remotePage);
    QHBoxLayout *inviteRow = new QHBoxLayout(inviteBar);
    inviteRow->setContentsMargins(0, 4, 0, 0);
    inviteRow->setSpacing(8);
    OutlinedLabel *inviteLabel = new OutlinedLabel(tr("邀请码"), inviteBar);
    inviteLabel->setObjectName(QStringLiteral("lanPageSubtitle"));
    inviteRow->addWidget(inviteLabel);
    m_inviteEdit = new QLineEdit(inviteBar);
    m_inviteEdit->setObjectName(QStringLiteral("lanInviteEdit"));
    m_inviteEdit->setPlaceholderText(tr("网络名|密钥（把这一串发给对方填入）"));
    inviteRow->addWidget(m_inviteEdit, 1);
    m_inviteApplyBtn = new QPushButton(tr("应用"), inviteBar);
    m_inviteApplyBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_inviteApplyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_inviteApplyBtn, &QPushButton::clicked, this,
            [this]() { applyInviteCodeFor(m_inviteEdit); });
    inviteRow->addWidget(m_inviteApplyBtn);
    m_inviteCopyBtn = new QPushButton(tr("复制"), inviteBar);
    m_inviteCopyBtn->setObjectName(QStringLiteral("lanGhostBtn"));
    m_inviteCopyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_inviteCopyBtn, &QPushButton::clicked, this,
            [this]() { copyInviteCode(m_inviteEdit->text()); });
    inviteRow->addWidget(m_inviteCopyBtn);
    remoteLayout->addWidget(inviteBar);

    // 待发送资源（与局域网发送共用同一份已选资源）
    OutlinedLabel *resHeader = new OutlinedLabel(tr("待发送资源"), remotePage);
    resHeader->setObjectName(QStringLiteral("lanPageSubtitle"));
    remoteLayout->addWidget(resHeader);

    QWidget *pickBar2 = new QWidget(remotePage);
    QHBoxLayout *pickRow2 = new QHBoxLayout(pickBar2);
    pickRow2->setContentsMargins(0, 0, 0, 0);
    pickRow2->setSpacing(8);
    m_pickFilesBtn2 = new QPushButton(tr("选择文件"), pickBar2);
    m_pickFilesBtn2->setObjectName(QStringLiteral("lanGhostBtn"));
    m_pickFilesBtn2->setCursor(Qt::PointingHandCursor);
    connect(m_pickFilesBtn2, &QPushButton::clicked, this, &LanTransferPage::pickFiles);
    m_pickFolderBtn2 = new QPushButton(tr("选择文件夹"), pickBar2);
    m_pickFolderBtn2->setObjectName(QStringLiteral("lanGhostBtn"));
    m_pickFolderBtn2->setCursor(Qt::PointingHandCursor);
    connect(m_pickFolderBtn2, &QPushButton::clicked, this, &LanTransferPage::pickFolder);
    m_clearBtn2 = new QPushButton(tr("清空"), pickBar2);
    m_clearBtn2->setObjectName(QStringLiteral("lanGhostBtn"));
    m_clearBtn2->setCursor(Qt::PointingHandCursor);
    connect(m_clearBtn2, &QPushButton::clicked, this, &LanTransferPage::clearPicked);
    pickRow2->addWidget(m_pickFilesBtn2);
    pickRow2->addWidget(m_pickFolderBtn2);
    pickRow2->addWidget(m_clearBtn2);
    pickRow2->addStretch();
    remoteLayout->addWidget(pickBar2);

    m_fileList2 = new QListWidget(remotePage);
    m_fileList2->setObjectName(QStringLiteral("lanPageList"));
    m_fileList2->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileList2->setMinimumHeight(70);
    remoteLayout->addWidget(m_fileList2);

    // 虚拟网络中的设备
    OutlinedLabel *peerHeader = new OutlinedLabel(tr("虚拟网络中的设备"), remotePage);
    peerHeader->setObjectName(QStringLiteral("lanPageSubtitle"));
    remoteLayout->addWidget(peerHeader);

    m_peerList = new QListWidget(remotePage);
    m_peerList->setObjectName(QStringLiteral("lanPageList"));
    m_peerList->setMinimumHeight(90);
    m_peerList->setSelectionMode(QAbstractItemView::SingleSelection);
    remoteLayout->addWidget(m_peerList, 1);

    OutlinedLabel *peerHint = new OutlinedLabel(
        tr("仅显示对方也开启了远程接收的设备；点击下方按钮打开弹窗，"
           "在弹窗里选中要发送的目标设备。"), remotePage);
    peerHint->setObjectName(QStringLiteral("lanPageHint"));
    peerHint->setWordWrap(true);
    remoteLayout->addWidget(peerHint);

    QWidget *remoteFooter = new QWidget(remotePage);
    QHBoxLayout *remoteFooterRow = new QHBoxLayout(remoteFooter);
    remoteFooterRow->setContentsMargins(0, 0, 0, 0);
    remoteFooterRow->addStretch();
    m_remoteSendBtn = new QPushButton(tr("发送到远程设备…"), remoteFooter);
    m_remoteSendBtn->setObjectName(QStringLiteral("primaryButton"));
    m_remoteSendBtn->setCursor(Qt::PointingHandCursor);
    connect(m_remoteSendBtn, &QPushButton::clicked, this,
            &LanTransferPage::openRemoteSendDialog);
    remoteFooterRow->addWidget(m_remoteSendBtn);
    remoteLayout->addWidget(remoteFooter);

    m_modeStack->addWidget(remotePage);

    // ── 远程接收面板：只填邀请码，一键开启 ──
    QWidget *remoteRecvPage = new QWidget(m_modeStack);
    QVBoxLayout *remoteRecvLayout = new QVBoxLayout(remoteRecvPage);
    remoteRecvLayout->setContentsMargins(0, 6, 0, 0);
    remoteRecvLayout->setSpacing(12);

    OutlinedLabel *remoteRecvHint = new OutlinedLabel(
        tr("把对方发来的邀请码粘贴进框里，点右边按钮即可开始接收——"
           "接收服务与组网会一并自动打开，首次使用会自动下载传输组件。"),
        remoteRecvPage);
    remoteRecvHint->setObjectName(QStringLiteral("lanPageHint"));
    remoteRecvHint->setWordWrap(true);
    remoteRecvLayout->addWidget(remoteRecvHint);

    // 唯一入口：邀请码输入 + 集成按钮
    OutlinedLabel *inputTitle = new OutlinedLabel(tr("对方的邀请码"), remoteRecvPage);
    inputTitle->setObjectName(QStringLiteral("lanPageSubtitle"));
    remoteRecvLayout->addWidget(inputTitle);

    QWidget *inviteBar2 = new QWidget(remoteRecvPage);
    QHBoxLayout *inviteRow2 = new QHBoxLayout(inviteBar2);
    inviteRow2->setContentsMargins(0, 0, 0, 0);
    inviteRow2->setSpacing(8);
    m_inviteEdit2 = new QLineEdit(inviteBar2);
    m_inviteEdit2->setObjectName(QStringLiteral("lanInviteEdit"));
    m_inviteEdit2->setPlaceholderText(tr("网络名|密钥，例如 blockbox|a1b2c3…"));
    m_inviteEdit2->setToolTip(tr("粘贴对方发来的邀请码，回车即可开启"));
    inviteRow2->addWidget(m_inviteEdit2, 1);

    m_remoteMasterBtn = new QPushButton(tr("开启远程接收"), inviteBar2);
    m_remoteMasterBtn->setObjectName(QStringLiteral("primaryButton"));
    m_remoteMasterBtn->setCursor(Qt::PointingHandCursor);
    connect(m_remoteMasterBtn, &QPushButton::clicked, this,
            &LanTransferPage::toggleRemoteReceive);
    inviteRow2->addWidget(m_remoteMasterBtn);
    remoteRecvLayout->addWidget(inviteBar2);

    connect(m_inviteEdit2, &QLineEdit::returnPressed, m_remoteMasterBtn,
            &QPushButton::click);
    connect(m_inviteEdit2, &QLineEdit::textChanged, this,
            [this](const QString &) { updateRemoteRecvSwitch(); });

    m_installProgressLabel2 = new OutlinedLabel(QString(), remoteRecvPage);
    m_installProgressLabel2->setObjectName(QStringLiteral("lanPageHint"));
    m_installProgressLabel2->hide();
    remoteRecvLayout->addWidget(m_installProgressLabel2);

    // 接收状态卡片（状态 + 本机信息，一屏看全）
    m_remoteRecvCard = new QLabel(remoteRecvPage);
    m_remoteRecvCard->setObjectName(QStringLiteral("lanInfoCard"));
    m_remoteRecvCard->setWordWrap(true);
    m_remoteRecvCard->setTextFormat(Qt::RichText);
    remoteRecvLayout->addWidget(m_remoteRecvCard);

    OutlinedLabel *remoteRecvFootHint = new OutlinedLabel(
        tr("对方开启「远程发送」后，会在设备列表里看到本机（%1），"
           "选中即可浏览共享位置并把文件传进来，进度显示在任务卡片中。")
            .arg(LanTransfer::instance()->deviceName()),
        remoteRecvPage);
    remoteRecvFootHint->setObjectName(QStringLiteral("lanPageHint"));
    remoteRecvFootHint->setWordWrap(true);
    remoteRecvLayout->addWidget(remoteRecvFootHint);
    remoteRecvLayout->addStretch();

    m_modeStack->addWidget(remoteRecvPage);

    mainLayout->addWidget(m_modeStack, 1);
}

void LanTransferPage::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString textColor = isLight ? "#333333" : "#e8e8e8";
    const QString subColor = isLight ? "#64748B" : "#9aa3b2";
    const QString muted = isLight ? "#94A3B8" : "#7a8290";
    const QString fieldBg = isLight ? "#f4f4f4" : "#3b3b40";
    const QString fieldBorder = isLight ? "#d4d4d4" : "#55555a";
    const QString hoverBg = isLight ? "rgba(15,23,42,0.06)" : "rgba(255,255,255,0.06)";
    const QString selBg = isLight ? "rgba(16,185,129,0.14)" : "rgba(16,185,129,0.20)";
    const QString listBg = isLight ? "#fafafa" : "#38383d";

    const QString style = QString(
        "QLabel#lanPageTitle { color: %1; font-size: 18px; font-weight: 700; background: transparent; }"
        "QLabel#lanPageSubtitle { color: %1; font-size: 13px; font-weight: 600; background: transparent; }"
        "QLabel#lanPageDesc { color: %2; font-size: 12.5px; background: transparent; }"
        "QLabel#lanPageHint { color: %3; font-size: 12px; background: transparent; }"
        "QLabel#lanInfoCard {"
        "    background-color: %4; border: 1px solid %5; border-radius: 10px;"
        "    color: %3; padding: 10px 12px; font-size: 12px;"
        "}"
        "QPushButton#lanModeBtn {"
        "    background-color: %4; border: 1px solid %5; border-radius: 9px;"
        "    color: %2; padding: 7px 12px; font-size: 12.5px; font-weight: 600;"
        "}"
        "QPushButton#lanModeBtn:hover { background-color: %6; }"
        "QPushButton#lanModeBtn:checked { background-color: %7; border-color: %7; color: #ffffff; }"
        "QPushButton#lanGhostBtn {"
        "    background-color: %4; border: 1px solid %5; border-radius: 8px;"
        "    color: %2; padding: 7px 16px; font-size: 12.5px;"
        "}"
        "QPushButton#lanGhostBtn:hover { background-color: %6; color: %1; }"
        "QListWidget#lanPageList {"
        "    background-color: %8; border: 1px solid %5; border-radius: 10px;"
        "    color: %2; font-size: 12.5px; outline: none; padding: 4px;"
        "}"
        "QListWidget#lanPageList::item { border-radius: 7px; padding: 7px 10px; }"
        "QListWidget#lanPageList::item:hover { background-color: %6; }"
        "QListWidget#lanPageList::item:selected { background-color: %9; color: %1; }"
        "QLineEdit#lanInviteEdit {"
        "    background-color: %4; border: 1px solid %5; border-radius: 8px;"
        "    color: %1; padding: 7px 10px; font-size: 12.5px;"
        "}"
        "QLineEdit#lanInviteEdit:focus { border-color: %7; }"
        "QLineEdit#lanInviteShow {"
        "    background-color: %8; border: 1px solid %5; border-radius: 8px;"
        "    color: %7; padding: 9px 12px; font-size: 13px; font-weight: 600;"
        "    letter-spacing: 1px;"
        "}"
    ).arg(textColor, subColor, muted, fieldBg, fieldBorder, hoverBg, themeColor,
          listBg, selBg);

    setStyleSheet(style);
}

// ───────────────────────────── 模式切换 ─────────────────────────────

void LanTransferPage::switchMode(int mode)
{
    m_sendModeBtn->setChecked(mode == 0);
    m_recvModeBtn->setChecked(mode == 1);
    m_remoteModeBtn->setChecked(mode == 2);
    m_remoteRecvModeBtn->setChecked(mode == 3);
    m_modeStack->setCurrentIndex(mode);

    if (mode == 1 || mode == 3)
        updateReceiverInfo();
    if (mode == 2 || mode == 3)
    {
        updateRemoteUi();
        rebuildPeerList();
    }
}

// ───────────────────────────── 发送 ─────────────────────────────

void LanTransferPage::pickFiles()
{
    const QStringList paths = AppFileDialog::getOpenFileNames(
        this, tr("选择要发送的文件"));
    for (const QString &path : paths)
    {
        if (!path.isEmpty() && !m_filePaths.contains(path))
            m_filePaths << path;
    }
    rebuildFileList();
}

void LanTransferPage::pickFolder()
{
    const QString path = AppFileDialog::getExistingDirectory(
        this, tr("选择要发送的文件夹"));
    if (!path.isEmpty() && !m_filePaths.contains(path))
        m_filePaths << path;
    rebuildFileList();
}

void LanTransferPage::clearPicked()
{
    m_filePaths.clear();
    rebuildFileList();
}

void LanTransferPage::rebuildFileList()
{
    fillFileList(m_fileList);
    fillFileList(m_fileList2);
    updateSendButtonState();
}

void LanTransferPage::fillFileList(QListWidget *list)
{
    if (!list)
        return;
    list->clear();
    for (const QString &path : m_filePaths)
    {
        const QFileInfo fi(path);
        QString sizeText;
        if (fi.isDir())
        {
            qint64 total = 0;
            int count = 0;
            QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
            {
                it.next();
                total += it.fileInfo().size();
                ++count;
            }
            sizeText = tr("%1 个文件 · %2").arg(count).arg(formatSize(total));
        }
        else
        {
            sizeText = formatSize(fi.size());
        }

        auto *item = new QListWidgetItem(
            QStringLiteral("%1    %2").arg(fi.fileName(), sizeText), list);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }
}

void LanTransferPage::refreshDevices()
{
    LanTransfer::instance()->startDiscovery();
    m_deviceHeader->setText(tr("局域网设备 · 正在搜索…"));
}

void LanTransferPage::rebuildDeviceList()
{
    const QString previous = m_deviceList->currentItem()
        ? m_deviceList->currentItem()->data(Qt::UserRole).toString()
        : QString();

    m_deviceList->clear();
    const QList<LanDevice> devices = LanTransfer::instance()->devices();
    for (const LanDevice &dev : devices)
    {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1    %2").arg(dev.name, dev.address), m_deviceList);
        item->setData(Qt::UserRole, dev.id);
        item->setToolTip(tr("端口 %1").arg(dev.port));
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

    m_deviceHeader->setText(devices.isEmpty()
        ? tr("局域网设备 · 暂未发现（确认对方已打开方块盒子）")
        : tr("局域网设备 · %1 台").arg(devices.size()));
    updateSendButtonState();
}

void LanTransferPage::onDeviceSelectionChanged()
{
    updateSendButtonState();
}

void LanTransferPage::updateSendButtonState()
{
    const bool hasFiles = !m_filePaths.isEmpty();
    const bool sending = LanTransfer::instance()->isSending();

    // 局域网发送：需要选中局域网设备
    m_sendBtn->setEnabled(hasFiles && m_deviceList->currentItem() != nullptr
                          && !sending);

    // 资源选择按钮（局域网发送 / 远程发送 各一份）
    m_clearBtn->setEnabled(hasFiles);
    m_clearBtn2->setEnabled(hasFiles);
    m_pickFilesBtn->setEnabled(!sending);
    m_pickFolderBtn->setEnabled(!sending);
    m_pickFilesBtn2->setEnabled(!sending);
    m_pickFolderBtn2->setEnabled(!sending);

    // 远程发送：组网运行 + 有资源
    m_remoteSendBtn->setEnabled(TunnelManager::instance()->isRunning() && hasFiles
                                && !sending);
}

void LanTransferPage::openSendDialog()
{
    if (m_filePaths.isEmpty())
    {
        AppMessageBox::information(this, tr("文件传输"),
                                   tr("请先选择要发送的文件或文件夹"));
        return;
    }
    if (!m_deviceList->currentItem())
    {
        AppMessageBox::information(this, tr("文件传输"),
                                   tr("请先选择一台可用设备"));
        return;
    }

    LanTransferDialog dialog(m_filePaths, this);
    dialog.exec();
    updateSendButtonState();
}

void LanTransferPage::openRemoteSendDialog()
{
    if (m_filePaths.isEmpty())
    {
        AppMessageBox::information(this, tr("远程发送"),
                                   tr("请先选择要发送的文件或文件夹"));
        return;
    }
    if (!TunnelManager::instance()->isRunning())
    {
        AppMessageBox::information(this, tr("远程发送"),
                                   tr("请先开启远程互传，并与对方使用相同邀请码"));
        return;
    }

    LanTransferDialog dialog(m_filePaths, this);
    dialog.exec();
    updateSendButtonState();
}

// ───────────────────────────── 接收 ─────────────────────────────

void LanTransferPage::toggleReceiver()
{
    LanTransfer *service = LanTransfer::instance();
    if (service->isServiceRunning())
        service->stopService();
    else
        service->startService();
    updateReceiverInfo();
}

void LanTransferPage::updateReceiverInfo()
{
    LanTransfer *service = LanTransfer::instance();
    const bool running = service->isServiceRunning();

    m_recvToggleBtn->setChecked(running);
    m_recvToggleBtn->setText(running ? tr("关闭接收") : tr("开启接收"));
    m_recvStatusLabel->setText(running
        ? tr("接收服务运行中，局域网内的设备可以向本机发送资源")
        : tr("接收服务已关闭，其他设备无法发现本机"));

    m_recvNameLabel->setText(tr("设备名称：%1").arg(service->deviceName()));
    m_recvAddrLabel->setText(tr("本机地址：%1").arg(localIPv4()));
    m_recvPortLabel->setText(running
        ? tr("传输端口：%1").arg(service->servicePort())
        : tr("传输端口：—"));

    QStringList rootNames;
    const QList<LanShareRoot> roots = service->shareRoots();
    for (const LanShareRoot &root : roots)
        rootNames << QStringLiteral("%1（%2）").arg(root.name, root.path);
    m_recvRootsLabel->setText(tr("共享位置：%1").arg(rootNames.join(QStringLiteral("、"))));

    // 远程接收面板：状态集中显示在卡片，集成开关单独刷新
    updateRemoteRecvSwitch();
    updateRemoteRecvCard();
}

// ───────────────────────── 远程互传（内网穿透） ─────────────────────────

void LanTransferPage::toggleTunnel()
{
    TunnelManager *tunnel = TunnelManager::instance();
    if (!tunnel->isAvailable())
    {
        m_tunnelToggleBtn->setChecked(false);
        downloadEngine();
        return;
    }

    if (tunnel->isRunning())
        tunnel->stop();
    else
        tunnel->start();

    updateRemoteUi();
    rebuildPeerList();
    updateSendButtonState();
}

void LanTransferPage::downloadEngine()
{
    m_downloadInfo = tr("准备下载…");
    updateDownloadLabels();
    TunnelManager::instance()->ensureInstalled();
}

void LanTransferPage::updateDownloadLabels()
{
    QString text = m_downloadProgressText;
    if (!m_downloadInfo.isEmpty())
        text = text.isEmpty() ? m_downloadInfo
                              : text + QStringLiteral(" · ") + m_downloadInfo;

    m_installProgressLabel->setText(text);
    m_installProgressLabel2->setText(text);
    m_installProgressLabel2->setVisible(!text.isEmpty());
}

void LanTransferPage::applyInviteCodeFor(QLineEdit *edit)
{
    if (!edit)
        return;
    TunnelManager *tunnel = TunnelManager::instance();
    if (!tunnel->setInviteCode(edit->text()))
    {
        AppMessageBox::warning(this, tr("远程互传"),
                               tr("邀请码格式不正确，应为「网络名|密钥」"));
        m_inviteEdit->setText(tunnel->inviteCode());
        m_inviteEdit2->setText(tunnel->inviteCode());
        return;
    }

    const bool wasRunning = tunnel->isRunning();
    if (wasRunning)
        tunnel->stop();
    m_inviteEdit->setText(tunnel->inviteCode());
    m_inviteEdit2->setText(tunnel->inviteCode());
    if (wasRunning)
        tunnel->start();

    NotificationManager::showSuccess(window(), tr("邀请码已应用"));
    updateRemoteUi();
}

void LanTransferPage::copyInviteCode(const QString &code)
{
    if (code.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(code);
    NotificationManager::showSuccess(window(), tr("邀请码已复制，发给对方填入即可"));
}

void LanTransferPage::syncTunnelUi(QPushButton *toggleBtn, QPushButton *downloadBtn,
                                   QLabel *progressLabel, QLabel *statusLabel,
                                   QLineEdit *inviteEdit, QPushButton *applyBtn,
                                   bool syncInviteText)
{
    TunnelManager *tunnel = TunnelManager::instance();
    const bool available = tunnel->isAvailable();
    const bool running = tunnel->isRunning();

    downloadBtn->setVisible(!available);
    toggleBtn->setEnabled(available);
    toggleBtn->setChecked(running);
    toggleBtn->setText(running ? tr("关闭远程互传") : tr("开启远程互传"));

    // syncInviteText=false 时该输入框用于粘贴对方邀请码，不能被本机邀请码覆盖
    if (syncInviteText && !inviteEdit->hasFocus())
        inviteEdit->setText(tunnel->inviteCode());
    // 邀请码输入与应用不依赖组件是否已安装：填码只是写配置，任何时候都该可编辑
    Q_UNUSED(applyBtn);

    if (statusLabel)
    {
        if (!available)
        {
            statusLabel->setText(
                tr("需要先下载传输组件（约 30 MB，仅需一次，完全免费）"));
        }
        else if (running)
        {
            const TunnelStatus st = tunnel->status();
            statusLabel->setText(st.virtualIp.isEmpty()
                ? (st.message.isEmpty() ? tr("组网中…") : st.message)
                : tr("已就绪 · 虚拟地址 %1 · %2")
                      .arg(st.virtualIp,
                           st.message.isEmpty() ? tr("P2P 直连") : st.message));
        }
        else
        {
            statusLabel->setText(tr("远程互传已关闭，与远程设备的连接不可用"));
        }
    }

    Q_UNUSED(progressLabel);
}

void LanTransferPage::updateRemoteUi()
{
    syncTunnelUi(m_tunnelToggleBtn, m_downloadBtn, m_installProgressLabel,
                 m_remoteStatusLabel, m_inviteEdit, m_inviteApplyBtn);
    updateRemoteRecvSwitch();
    updateRemoteRecvCard();
    updateSendButtonState();
}

// ─────────────────── 远程接收：邀请码一个入口，一键开关 ───────────────────

void LanTransferPage::updateRemoteRecvSwitch()
{
    if (!m_remoteMasterBtn || !m_inviteEdit2)
        return;

    TunnelManager *tunnel = TunnelManager::instance();
    LanTransfer *service = LanTransfer::instance();
    const bool running = tunnel->isRunning() && service->isServiceRunning();

    const QString typed = m_inviteEdit2->text().trimmed();
    const QString current = tunnel->inviteCode();
    const bool codeChanged = !typed.isEmpty() && typed != current;

    if (!running)
    {
        m_remoteMasterBtn->setText(tr("开启远程接收"));
    }
    else if (codeChanged)
    {
        m_remoteMasterBtn->setText(tr("应用邀请码"));
        m_remoteMasterBtn->setEnabled(true);
    }
    else
    {
        m_remoteMasterBtn->setText(tr("关闭远程接收"));
        m_remoteMasterBtn->setEnabled(true);
    }
}

void LanTransferPage::toggleRemoteReceive()
{
    TunnelManager *tunnel = TunnelManager::instance();
    LanTransfer *service = LanTransfer::instance();

    const QString typed = m_inviteEdit2->text().trimmed();
    const QString current = tunnel->inviteCode();
    const bool running = tunnel->isRunning() && service->isServiceRunning();
    const bool codeChanged = !typed.isEmpty() && typed != current;

    // ── 已开启：改了码就应用重启，否则关闭 ──
    if (running)
    {
        if (codeChanged)
        {
            if (!tunnel->setInviteCode(typed))
            {
                AppMessageBox::warning(this, tr("远程接收"),
                                       tr("邀请码格式不正确，应为「网络名|密钥」"));
                return;
            }
            tunnel->stop();
            tunnel->start();
            NotificationManager::showSuccess(window(), tr("邀请码已应用"));
            updateRemoteUi();
            return;
        }

        // 关闭远程接收只停组网：接收服务是否关闭由「局域网接收」页独立控制
        tunnel->stop();
        updateReceiverInfo();
        updateRemoteUi();
        return;
    }

    // ── 开启：先应用邀请码（若填了且与当前不同）──
    if (!typed.isEmpty() && codeChanged)
    {
        if (!tunnel->setInviteCode(typed))
        {
            AppMessageBox::warning(this, tr("远程接收"),
                                   tr("邀请码格式不正确，应为「网络名|密钥」"));
            return;
        }
    }

    if (!tunnel->isAvailable())
    {
        // 组件缺失：先自动下载，完成后接着开启
        m_pendingRemoteEnable = true;
        m_downloadInfo = tr("首次使用，正在准备传输组件…");
        updateDownloadLabels();
        tunnel->ensureInstalled();
        updateRemoteRecvSwitch();
        updateRemoteRecvCard();
        return;
    }

    service->startService();
    tunnel->start();
    updateReceiverInfo();
    updateRemoteUi();
}

void LanTransferPage::updateRemoteRecvCard()
{
    if (!m_remoteRecvCard)
        return;

    LanTransfer *service = LanTransfer::instance();
    TunnelManager *tunnel = TunnelManager::instance();
    const bool recvOn = service->isServiceRunning();
    const bool netOn = tunnel->isRunning();
    const TunnelStatus st = tunnel->status();
    const bool ready = recvOn && netOn && !st.virtualIp.isEmpty();

    // 状态行
    QString statusLine;
    if (ready)
    {
        statusLine = QStringLiteral(
            "<b><span style='color:#2E7D32'>&#9679; 可被远程访问</span></b>");
    }
    else if (!recvOn && !netOn)
    {
        statusLine = QStringLiteral(
            "<span style='color:#94A3B8'>&#9675; 远程接收未开启</span>");
    }
    else if (!recvOn)
    {
        statusLine = QStringLiteral(
            "<span style='color:#FF9800'>&#9685; 还差一步：接收服务未开启</span>");
    }
    else if (!netOn)
    {
        statusLine = tunnel->isAvailable()
            ? QStringLiteral(
                  "<span style='color:#FF9800'>&#9685; 还差一步：远程互传未开启</span>")
            : QStringLiteral(
                  "<span style='color:#FF9800'>&#9685; 还差一步：传输组件未下载</span>");
    }
    else
    {
        statusLine = QStringLiteral(
            "<span style='color:#FF9800'>&#9685; 组网中，稍候即可被访问</span>");
    }

    // 本机信息行
    QStringList info;
    info << tr("设备名 %1").arg(service->deviceName().toHtmlEscaped());
    info << tr("虚拟地址 %1")
                .arg(netOn && !st.virtualIp.isEmpty() ? st.virtualIp.toHtmlEscaped()
                                                      : QStringLiteral("—"));
    info << tr("端口 %1")
                .arg(recvOn ? QString::number(service->servicePort())
                            : QStringLiteral("—"));

    QStringList roots;
    const QList<LanShareRoot> shareRoots = service->shareRoots();
    for (const LanShareRoot &root : shareRoots)
        roots << QStringLiteral("%1（%2）")
                     .arg(root.name.toHtmlEscaped(), root.path.toHtmlEscaped());

    m_remoteRecvCard->setText(
        statusLine + QStringLiteral("<br/>")
        + info.join(QStringLiteral("　·　")) + QStringLiteral("<br/>")
        + tr("共享位置：%1").arg(roots.join(QStringLiteral("、"))));
}

void LanTransferPage::rebuildPeerList()
{
    TunnelManager *tunnel = TunnelManager::instance();
    m_peerList->clear();

    if (tunnel->isRunning())
    {
        const TunnelStatus st = tunnel->status();
        for (const TunnelPeer &peer : st.peers)
        {
            if (peer.isSelf || !peer.isValid())
                continue;
            const QString link = peer.tunnelProto.isEmpty()
                ? peer.cost
                : QStringLiteral("%1 · %2")
                      .arg(peer.tunnelProto,
                           peer.latency.isEmpty() ? QStringLiteral("—")
                                                  : peer.latency + QStringLiteral(" ms"));
            auto *item = new QListWidgetItem(
                QStringLiteral("%1    %2    %3")
                    .arg(peer.hostname.isEmpty() ? tr("未知设备") : peer.hostname,
                         peer.virtualIp, link),
                m_peerList);
            item->setData(Qt::UserRole, peer.virtualIp);
            item->setToolTip(tr("虚拟地址：%1\n链路：%2").arg(peer.virtualIp, link));
        }
    }

    if (m_peerList->count() == 0)
    {
        auto *item = new QListWidgetItem(
            tunnel->isRunning()
                ? tr("暂无其他设备：让对方也开启远程互传并填入相同邀请码")
                : tr("开启远程互传后，这里会列出虚拟网络中的设备"),
            m_peerList);
        item->setFlags(Qt::NoItemFlags);
    }

    if (m_remoteSendBtn)
        m_remoteSendBtn->setEnabled(tunnel->isRunning() && !m_filePaths.isEmpty());
}

void LanTransferPage::onFileReceived(const QString &path, qint64 size)
{
    const QFileInfo fi(path);
    auto *item = new QListWidgetItem(
        QStringLiteral("%1    %2    %3")
            .arg(fi.fileName(), formatSize(size),
                 QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))));
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    m_recvList->insertItem(0, item);
    if (m_recvList->count() > 50)
        delete m_recvList->takeItem(m_recvList->count() - 1);

    m_receivedPaths.prepend(path);
    if (m_receivedPaths.size() > 50)
        m_receivedPaths.removeLast();
    m_openRecvBtn->setEnabled(true);
}

void LanTransferPage::openReceivedLocation()
{
    if (m_receivedPaths.isEmpty())
        return;
    const QFileInfo fi(m_receivedPaths.first());
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (!dir.isEmpty())
        FileExplorer::open(this, dir);
}

QString LanTransferPage::localIPv4() const
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : addresses)
    {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
            return addr.toString();
    }
    return tr("未知");
}

QString LanTransferPage::formatSize(qint64 bytes)
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
