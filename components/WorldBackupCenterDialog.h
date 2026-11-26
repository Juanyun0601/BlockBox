/**
 * @file   WorldBackupCenterDialog.h
 * @brief  实例级「存档备份」总览弹窗声明
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 从实例管理的「存档」页进入，汇总该实例下所有已有备份的世界：
 *   - 世界还在：显示备份份数、占用空间、最新备份时间，点「管理备份」进单世界弹窗
 *   - 世界已被删除：标出「存档已删除」，仍可从这里把备份恢复回来
 */
#ifndef WORLDBACKUPCENTERDIALOG_H
#define WORLDBACKUPCENTERDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

#include "AppDialogBase.h"
#include "utils/WorldBackupManager.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class WorldBackupCenterDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /** @brief 打开指定实例的存档备份总览 */
    static void openForInstance(QWidget *parent, const QString &instancePath);

private:
    explicit WorldBackupCenterDialog(QWidget *parent, const QString &instancePath);

    void initUI();
    void reload();
    QWidget *createWorldRow(const QString &worldName,
                            const QList<WorldBackupInfo> &backups, bool worldExists);
    /** 某世界备份包占用空间合计 */
    static qint64 backupsSize(const QList<WorldBackupInfo> &backups);

    QString m_instancePath;

    QLabel *m_summaryLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QPushButton *m_closeBtn = nullptr;
};

#endif // WORLDBACKUPCENTERDIALOG_H
