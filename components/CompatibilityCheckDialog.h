/**
 * @file   CompatibilityCheckDialog.h
 * @brief  实例体检对话框：本地问题报告 + 在线兼容矩阵
 * @author BlockBox Team
 * @date   2026-11-22
 */
#ifndef COMPATIBILITYCHECKDIALOG_H
#define COMPATIBILITYCHECKDIALOG_H

#include <QList>
#include <QString>

#include "AppDialogBase.h"
#include "utils/compat/CompatMatrix.h"
#include "utils/compat/LocalCompatibilityChecker.h"
#include "utils/mod/ModData.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class CompatMatrixFetcher;

/**
 * @brief 实例体检对话框
 *
 * 两页：
 *  - 「体检报告」：本地离线检查结果（加载器不匹配、前置缺失、资源包格式不符等）；
 *  - 「兼容矩阵」：把每个模组对上 Modrinth 资源页，标出「当前版本可用 / 仅支持旧版本 /
 *    已停更 / 未收录」，并给出「这个包建议停在哪个版本」。
 *
 * 体检本身不联网也能出报告；兼容矩阵依赖 Modrinth，查不到时明确显示「未收录」而不猜。
 */
class CompatibilityCheckDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit CompatibilityCheckDialog(QWidget *parent = nullptr);

    /**
     * @brief 便捷入口：创建对话框并立即开始体检，模态执行
     * @param parent       宿主窗口（通常为主窗口或实例管理页）
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
    void onLocalReady(const CompatReport &report, const QList<ModInfo> &mods);
    void onMatrixReady(const CompatMatrixReport &report);
    void onMatrixFailed(const QString &error);
    void onProgress(const QString &text);
    void onTabClicked(int index);
    void onTargetVersionChanged(int index);

private:
    void initUI();
    void initStyle();
    void buildReportPage(QVBoxLayout *parentLayout);
    void buildMatrixPage(QVBoxLayout *parentLayout);

    void showRunning(bool running);
    void populateReport(const CompatReport &report);
    void populateMatrix();
    void rebuildMatrixRows();
    void clearLayout(QLayout *layout, int keepTail = 0);

    QWidget *createIssueCard(const CompatIssue &issue);
    QWidget *createMatrixRow(const CompatMatrixRow &row, const QString &targetVersion);

    QString severityColor(int severity) const;
    QString statusColor(int status) const;

    QString m_instancePath;
    QString m_gameVersion;
    QString m_loaderType;

    // ---- 标题与页签 ----
    QLabel *m_metaLabel;
    QPushButton *m_reportTabBtn;
    QPushButton *m_matrixTabBtn;
    QStackedWidget *m_tabStack;

    // ---- 体检报告页 ----
    QLabel *m_errorChip;
    QLabel *m_warningChip;
    QLabel *m_infoChip;
    QComboBox *m_filterCombo;
    QScrollArea *m_scrollArea;
    QWidget *m_issueContainer;
    QVBoxLayout *m_issueLayout;
    QLabel *m_runningLabel;

    // ---- 兼容矩阵页 ----
    QLabel *m_matrixSummary;
    QComboBox *m_targetCombo;
    QCheckBox *m_onlyUnsupportedBox;
    QScrollArea *m_matrixScroll;
    QWidget *m_matrixContainer;
    QVBoxLayout *m_matrixLayout;
    QLabel *m_matrixStatus;

    QPushButton *m_recheckBtn;

    CompatMatrixFetcher *m_fetcher;
    CompatReport m_lastReport;
    CompatMatrixReport m_matrixReport;
    bool m_localDone = false;
};

#endif // COMPATIBILITYCHECKDIALOG_H
