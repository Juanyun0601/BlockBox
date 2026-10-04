/**
 * @file   WorldBackupDialog.h
 * @brief  世界备份与恢复弹窗声明
 * @author BlockBox Team
 * @date   2026-08-29
 *
 * 单个世界的备份管理界面：
 *   - 备份列表（时间、原始大小、压缩大小、自动/手动标记、备注）
 *   - 一键恢复到任意备份点（恢复前自动为当前世界创建快照）
 *   - 手动创建备份 / 删除备份 / 保留策略
 *   - 自动备份设置（开关、频率：每次启动 / 每小时 / 每天、保留数量）
 */
#ifndef WORLDBACKUPDIALOG_H
#define WORLDBACKUPDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

#include "AppDialogBase.h"
#include "utils/WorldBackupManager.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;

class WorldBackupDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 打开指定实例内指定世界的备份管理弹窗
     * @param parent       宿主窗口内的任意控件（弹窗将覆盖其顶层窗口）
     * @param instancePath 实例（版本文件夹）绝对路径
     * @param worldName    世界目录名
     */
    static void openForWorld(QWidget *parent, const QString &instancePath,
                             const QString &worldName);

private:
    explicit WorldBackupDialog(QWidget *parent, const QString &instancePath,
                               const QString &worldName);

    void initUI();
    void reloadBackups();
    void refreshSummary();
    QWidget *createBackupRow(const WorldBackupInfo &info);
    void createBackup();
    void restoreBackup(const WorldBackupInfo &info);
    void deleteBackup(const WorldBackupInfo &info);
    void openBackupFolder();
    void setBusy(bool busy, const QString &message = QString());
    void saveAutoSettings();

    QString m_instancePath; ///< 实例路径
    QString m_worldName;    ///< 世界目录名
    QList<WorldBackupInfo> m_backups; ///< 当前备份列表（新→旧）

    // UI
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QPushButton *m_createBtn = nullptr;
    QPushButton *m_folderBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QCheckBox *m_autoCheck = nullptr;
    QComboBox *m_freqCombo = nullptr;
    QSpinBox *m_keepSpin = nullptr;
    QCheckBox *m_snapshotCheck = nullptr;
    QList<QPushButton *> m_busyButtons; ///< 忙碌时统一禁用的按钮
};

#endif // WORLDBACKUPDIALOG_H
