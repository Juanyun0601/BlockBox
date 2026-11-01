/**
 * @file   FileOpStepsDialog.h
 * @brief  「文件操作步骤」弹窗：从运行日志整理步骤列表，只展示不执行
 * @author BlockBox Team
 * @date   2026-10-25
 */

#pragma once

#include <QList>

#include "components/AppDialogBase.h"
#include "utils/FileOpStepLog.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

/**
 * @brief 设置 → 高级设置 → 运行日志 →「文件操作步骤」
 *
 * 解析 launcher.log（含轮转备份），把下载 / 复制 / 解压 / 写入等
 * 相关日志整理成按时间排列的步骤列表。仅展示，不会重新执行任何操作。
 */
class FileOpStepsDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit FileOpStepsDialog(QWidget *parent = nullptr);

private slots:
    void onSessionChanged(int index);
    void onFilterChanged(int index);

private:
    void initUI();
    void reloadFromLogs();
    void refreshList();

    /// 当前筛选会话下的步骤（-1 = 全部会话）
    QList<FileOpStep> filteredSteps() const;

    QComboBox      *m_sessionCombo;
    QComboBox      *m_filterCombo;
    QListWidget    *m_list;
    QLabel         *m_countLabel;
    QLabel         *m_emptyHint;
    QPushButton    *m_closeBtn;
    QList<FileOpStep> m_allSteps;
};
