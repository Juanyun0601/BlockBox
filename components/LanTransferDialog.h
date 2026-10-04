/**
 * @file   LanTransferDialog.h
 * @brief  文件传输弹窗：先选设备，再用内嵌资源管理器选传输位置
 * @author BlockBox Team
 * @date   2026-09-26
 */
#ifndef LANTRANSFERDIALOG_H
#define LANTRANSFERDIALOG_H

#include "AppDialogBase.h"
#include "utils/LanTransfer.h"

#include <QHash>
#include <QList>
#include <QStringList>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QToolButton;

class LanTransferDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit LanTransferDialog(const QStringList &localPaths, QWidget *parent = nullptr);
    ~LanTransferDialog() override;

    /** 传输是否已成功发起 */
    bool transferStarted() const { return m_started; }
    /** 已选择的远端目录 */
    QString remoteDir() const { return m_currentPath; }

private slots:
    void rebuildDeviceList();
    void onDeviceSelectionChanged();
    void goNextStep();
    void goPrevStep();
    void startTransfer();
    void onItemActivated(QListWidgetItem *item);
    void goUpRemote();

private:
    void initUI();
    void initStyle();
    void showDeviceStep();
    void showExplorerStep();
    void renderRoots();
    void renderEntries(const QList<LanEntry> &entries);
    void updateLocationBar();
    void updateSourceSummary();
    QString selectedDeviceId() const;
    /** 把远程互传（内网穿透）节点合入设备列表 */
    void loadRemoteDevices();
    /** 释放为远程设备建立的端口转发 */
    void releaseForward();
    static QString formatSize(qint64 bytes);

    // 数据
    QStringList m_localPaths;
    LanDevice m_device;             ///< 当前选中的目标设备
    QHash<QString, LanDevice> m_remoteDevices; ///< 远程互传节点（id → 设备）
    QString m_activeForward;        ///< 本机转发地址 "127.0.0.1:port"
    QList<LanShareRoot> m_roots;
    QString m_currentPath;   ///< 空 = 处于共享根目录选择层
    bool m_started = false;

    // 控件
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QToolButton *m_closeBtn = nullptr;
    QLabel *m_stepDevice = nullptr;
    QLabel *m_stepPath = nullptr;
    QStackedWidget *m_bodyStack = nullptr;

    // 第一步：设备
    QLabel *m_deviceStatusLabel = nullptr;
    QListWidget *m_deviceList = nullptr;
    QLabel *m_deviceHint = nullptr;
    QPushButton *m_refreshBtn = nullptr;

    // 第二步：资源管理器
    QPushButton *m_rootBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QLabel *m_pathLabel = nullptr;
    QListWidget *m_entryList = nullptr;
    QLabel *m_entryStatus = nullptr;

    // 通用
    QLabel *m_sourceLabel = nullptr;
    QLabel *m_targetLabel = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_backBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_sendBtn = nullptr;
};

#endif // LANTRANSFERDIALOG_H

