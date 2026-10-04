/**
 * @file   CompatibilityCheckDialog.h
 * @brief  本地资源兼容性检测结果对话框
 * @author BlockBox Team
 * @date   2026-08-30
 */
#ifndef COMPATIBILITYCHECKDIALOG_H
#define COMPATIBILITYCHECKDIALOG_H

#include <QFutureWatcher>
#include <QList>
#include <QString>

#include "AppDialogBase.h"
#include "utils/compat/LocalCompatibilityChecker.h"

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/**
 * @brief 展示实例内模组/资源包/光影包兼容性检测结果的居中卡片对话框
 *
 * 检测在后台线程执行（QtConcurrent），完成后按严重级别渲染问题列表，
 * 支持按严重级别筛选与重新检测。
 */
class CompatibilityCheckDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit CompatibilityCheckDialog(QWidget *parent = nullptr);

    /**
     * @brief 便捷入口：创建对话框并立即开始检测，模态执行
     * @param parent       宿主窗口（通常为主窗口或实例助手窗口）
     * @param instancePath 实例路径
     * @param gameVersion  实例 MC 版本（可为空）
     * @param loaderType   实例加载器（可为空，表示原版）
     */
    static void runCheck(QWidget *parent,
                         const QString &instancePath,
                         const QString &gameVersion,
                         const QString &loaderType);

private slots:
    void startCheck();
    void onCheckFinished();

private:
    void initUI();
    void initStyle();
    void showRunning(bool running);
    void populateReport(const CompatReport &report);
    QWidget *createIssueCard(const CompatIssue &issue);
    QString severityColor(int severity) const;
    QString severityName(int severity) const;

    QString m_instancePath;
    QString m_gameVersion;
    QString m_loaderType;

    QLabel *m_metaLabel;
    QLabel *m_errorChip;
    QLabel *m_warningChip;
    QLabel *m_infoChip;
    QComboBox *m_filterCombo;
    QScrollArea *m_scrollArea;
    QWidget *m_issueContainer;
    QVBoxLayout *m_issueLayout;
    QLabel *m_runningLabel;
    QPushButton *m_recheckBtn;

    QList<CompatIssue> m_issues;
    CompatReport m_lastReport;  ///< 最近一次完整检测报告（筛选切换时复用）
    QFutureWatcher<CompatReport> *m_watcher;
};

#endif // COMPATIBILITYCHECKDIALOG_H
