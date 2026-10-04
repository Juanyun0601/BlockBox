/**
 * @file   LanTransferPage.h
 * @brief  实例管理 - 传输页面：局域网发送 / 局域网接收 / 远程发送 / 远程接收
 * @author BlockBox Team
 * @date   2026-09-26
 *
 * 四个标签各自自成一体：发送类选资源 + 选设备，接收类开开关。
 * 远程两页共用同一套组网服务与接收服务，状态在两处同步显示。
 */
#ifndef LANTRANSFERPAGE_H
#define LANTRANSFERPAGE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class OutlinedLabel;
class QPushButton;
class QStackedWidget;

class LanTransferPage : public QWidget
{
    Q_OBJECT

public:
    explicit LanTransferPage(QWidget *parent = nullptr);
    ~LanTransferPage() override;

    /** 与其它实例子页面保持一致的懒加载接口（本页不依赖实例目录） */
    void setInstancePath(const QString &path);

private slots:
    void switchMode(int mode);
    void pickFiles();
    void pickFolder();
    void clearPicked();
    void refreshDevices();
    void onDeviceSelectionChanged();
    void openSendDialog();
    void openRemoteSendDialog();
    void toggleReceiver();
    /** 远程接收总开关：一次打开接收服务 + 组网（缺组件时自动下载） */
    void toggleRemoteReceive();
    // ── 远程互传（内网穿透）──
    void toggleTunnel();
    void downloadEngine();
    void updateRemoteUi();
    void rebuildPeerList();
    void onFileReceived(const QString &path, qint64 size);
    void openReceivedLocation();

private:
    void initUI();
    void initStyle();
    void rebuildDeviceList();
    void rebuildFileList();
    void updateReceiverInfo();
    void updateSendButtonState();
    /** 远程接收状态卡片：接收服务 + 组网状态 + 本机信息 */
    void updateRemoteRecvCard();
    QString localIPv4() const;
    static QString formatSize(qint64 bytes);

    /** 邀请码输入框的通用操作（远程发送 / 远程接收 各有一份控件） */
    void applyInviteCodeFor(QLineEdit *edit);
    /** 复制邀请码文本（展示用标签或输入框） */
    void copyInviteCode(const QString &code);
    /** 远程接收总开关状态（接收服务 + 组网 是否都已就绪） */
    void updateRemoteRecvSwitch();
    /** 组件下载提示：进度 + 当前线路文案 */
    void updateDownloadLabels();
    /** 同步一组「组网开关 + 状态 + 邀请码」控件的显示 */
    void syncTunnelUi(QPushButton *toggleBtn, QPushButton *downloadBtn,
                      QLabel *progressLabel, QLabel *statusLabel,
                      QLineEdit *inviteEdit, QPushButton *applyBtn,
                      bool syncInviteText = true);
    /** 填充一个资源列表（局域网发送 / 远程发送 各有一个） */
    void fillFileList(QListWidget *list);

    QString m_instancePath;

    // 模式切换（局域网发送 / 局域网接收 / 远程发送 / 远程接收）
    QPushButton *m_sendModeBtn;
    QPushButton *m_recvModeBtn;
    QPushButton *m_remoteModeBtn;
    QPushButton *m_remoteRecvModeBtn;
    QStackedWidget *m_modeStack;

    // 局域网发送面板
    QPushButton *m_pickFilesBtn;
    QPushButton *m_pickFolderBtn;
    QPushButton *m_clearBtn;
    QListWidget *m_fileList;
    OutlinedLabel *m_deviceHeader;
    QPushButton *m_refreshBtn;
    QListWidget *m_deviceList;
    QPushButton *m_sendBtn;
    QStringList m_filePaths;

    // 局域网接收面板
    QPushButton *m_recvToggleBtn;
    OutlinedLabel *m_recvStatusLabel;
    OutlinedLabel *m_recvNameLabel;
    OutlinedLabel *m_recvAddrLabel;
    OutlinedLabel *m_recvPortLabel;
    OutlinedLabel *m_recvRootsLabel;
    QListWidget *m_recvList;
    QPushButton *m_openRecvBtn;
    QList<QString> m_receivedPaths;

    // 远程发送面板（组网 + 资源 + 虚拟网络设备）
    QPushButton *m_pickFilesBtn2;
    QPushButton *m_pickFolderBtn2;
    QPushButton *m_clearBtn2;
    QListWidget *m_fileList2;
    OutlinedLabel *m_remoteStatusLabel;
    QPushButton *m_tunnelToggleBtn;
    QPushButton *m_downloadBtn;
    OutlinedLabel *m_installProgressLabel;
    QLineEdit *m_inviteEdit;
    QPushButton *m_inviteCopyBtn;
    QPushButton *m_inviteApplyBtn;
    QListWidget *m_peerList;
    QPushButton *m_remoteSendBtn;

    // 远程接收面板（邀请码一个入口 + 状态卡片）
    QLabel *m_remoteRecvCard = nullptr;      ///< 接收状态卡片（状态 + 本机信息）
    QPushButton *m_remoteMasterBtn = nullptr;///< 开启 / 关闭 / 应用 集成按钮
    OutlinedLabel *m_installProgressLabel2 = nullptr;///< 组件下载进度提示
    QLineEdit *m_inviteEdit2 = nullptr;      ///< 粘贴邀请码（始终可输入）
    bool m_pendingRemoteEnable = false;      ///< 组件下载完成后自动开启
    QString m_downloadProgressText;          ///< 「下载中 45%」
    QString m_downloadInfo;                  ///< 「线路 cdn.gh-proxy.org」
};

#endif // LANTRANSFERPAGE_H
