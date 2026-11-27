/**
 * @file   MigrationWizardDialog.h
 * @brief  高级迁移向导：并行扫描旧启动器 → 按目录/账户/Java 挑选 → 登记或复制 → 体检汇总
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 与首次引导里的「一键导入」不同，这里把每一步拆开给用户看：
 *  1. 扫描：PCL / HMCL / MultiMC / BakaXL / 官方启动器同时探测，找到一家就点亮一家；
 *  2. 选择：可以只搬某一个游戏目录，也可以只带账户或 Java；
 *  3. 方式：默认「只登记」（原目录不动、立刻可用），也可以整份复制到方块盒子自己的目录；
 *  4. 完成：逐项列出真正新增了什么，并做一次轻量体检（版本能否识别、Java 还在不在）。
 *
 * 原启动器的数据全程只读，不做任何修改或删除。
 */
#ifndef MIGRATIONWIZARDDIALOG_H
#define MIGRATIONWIZARDDIALOG_H

#include <QHash>
#include <QFutureWatcher>
#include <QList>
#include <QSet>
#include <QStringList>

#include "components/AppDialogBase.h"
#include "utils/LauncherImporter.h"
#include "utils/MigrationService.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QScrollArea;
class QStackedWidget;
class QTextEdit;
class QTimer;
class QVBoxLayout;
class QWidget;
class QFrame;

class MigrationWizardDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit MigrationWizardDialog(QWidget *parent = nullptr);
    ~MigrationWizardDialog() override;

    /** 结果页点了「查看实例列表」：调用方据此跳转 */
    bool wantsOpenInstances() const { return m_wantsOpenInstances; }

    /** 隐藏结果页的「查看实例列表」（引导页里没有实例列表来路时用） */
    void setOpenInstancesAvailable(bool available) { m_openInstancesAvailable = available; }

protected:
    /** 迁移进行中不允许点遮罩 / 按返回键把窗口关掉 */
    void reject() override;

private:
    /** 一个可迁移来源（启动器或手动添加的目录） */
    struct SourceState {
        QString key;
        LauncherDetect detect;
        bool custom = false;
        bool selected = true;
        bool accounts = true;
        bool java = true;
        bool authServers = true;
        bool downloadSettings = false;
        bool memory = true;
        bool expanded = false;
        QSet<QString> dirs; ///< 勾选的游戏目录（绝对路径）
    };

    /** 扫描页每一行的控件（边扫边更新，不整页重建） */
    struct ScanRow {
        QLabel *status = nullptr;
        QLabel *detail = nullptr;
        QFrame *frame = nullptr;
    };

    void initUI();
    void initStyle();
    void buildStepper(QVBoxLayout *parentLayout);
    QWidget *buildScanPage();
    QWidget *buildSelectPage();
    QWidget *buildOptionsPage();
    QWidget *buildProgressPage();
    QWidget *buildResultPage();

    void startScan();
    void onScanFinished(const QString &key, const LauncherDetect &detect);
    void addScanRow(const QString &key, const QString &name, bool custom);
    void finishScanIfDone();
    void onAddCustomDir();

    void rebuildSourceCards();
    QWidget *createSourceCard(int index);
    void updateSelectionSummary();
    QString sourceSummary(const SourceState &source) const;
    /** 跨来源去重后的游戏目录（多个启动器共用同一个 .minecraft 时只算一次） */
    QStringList selectedGameDirs() const;
    int selectedDirCount() const;
    int selectedVersionCount() const;
    int selectedAccountCount() const;
    int selectedJavaCount() const;
    int selectedAuthCount() const;
    bool hasAnySelection() const;

    void onNextClicked();
    void onPrevClicked();
    void showStep(int step);
    void updateNavState();
    void updateStepPills();

    void refreshOptionsPage();
    void updateCopyEstimate();
    void updateDiskInfo();
    void startMigration();
    void startNextCopy();
    void onCopyProgress(qint64 copied, qint64 total, const QString &file);
    void onCopyFinished(const QString &destination, const QString &error);
    void finishMigration();
    void buildResultText();
    void appendLog(const QString &text);
    void setMigrating(bool migrating);

    MigrationSelection selectionFor(const SourceState &source) const;
    void updateCopyWarning();

    // ---- 流程状态 ----
    int m_step = 0;
    bool m_scanning = false;
    bool m_scanDone = false;
    int m_scanPending = 0;
    bool m_migrating = false;
    bool m_migrationDone = false;
    bool m_cancelRequested = false;
    bool m_wantsOpenInstances = false;
    bool m_openInstancesAvailable = true;

    // ---- 数据 ----
    QList<SourceState> m_sources;     ///< 已检测到的来源（含未找到的，选择页只列找到的）
    QList<QString> m_scanKeys;
    QHash<QString, int> m_sourceIndex;
    QHash<QString, ScanRow> m_scanRows;
    MigrationApplyResult m_result;
    QStringList m_migrationLines;
    QStringList m_copyJavaPaths;

    // ---- 复制队列 ----
    struct CopyJob {
        QString key;
        QString sourceDir;
    };
    QList<CopyJob> m_copyQueue;
    int m_copyIndex = 0;
    GameDirCopyTask *m_copyTask = nullptr;
    qint64 m_totalEstimate = 0;

    // ---- 界面 ----
    QWidget *m_card = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QList<QLabel *> m_stepPills;
    QList<QFrame *> m_stepLines;

    // 扫描页
    QWidget *m_scanListHost = nullptr;
    QVBoxLayout *m_scanListLayout = nullptr;
    QLabel *m_scanStatus = nullptr;
    QProgressBar *m_scanBar = nullptr;
    QPushButton *m_rescanBtn = nullptr;
    QPushButton *m_addDirBtn = nullptr;
    QTimer *m_scanDotTimer = nullptr;
    int m_scanDots = 0;

    // 选择页
    QWidget *m_selectListHost = nullptr;
    QVBoxLayout *m_selectListLayout = nullptr;
    QLabel *m_selectSummary = nullptr;
    QPushButton *m_selectAllBtn = nullptr;
    QPushButton *m_clearAllBtn = nullptr;

    // 方式页
    QWidget *m_optionsPage = nullptr;
    QFrame *m_registerCard = nullptr;
    QFrame *m_copyCard = nullptr;
    QRadioButton *m_registerRadio = nullptr;
    QRadioButton *m_copyRadio = nullptr;
    QLineEdit *m_copyRootEdit = nullptr;
    QPushButton *m_browseCopyRootBtn = nullptr;
    QLabel *m_copyEstimate = nullptr;
    QLabel *m_diskInfo = nullptr;
    QLabel *m_optionSummary = nullptr;
    QLabel *m_optionWarning = nullptr;
    qint64 m_estimateGeneration = 0;
    bool m_estimating = false;

    // 进度页
    QWidget *m_progressPage = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;
    QTextEdit *m_logView = nullptr;

    // 结果页
    QLabel *m_resultTitle = nullptr;
    QLabel *m_resultSummary = nullptr;
    QLabel *m_resultDetail = nullptr;

    // 底栏
    QLabel *m_footerHint = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_doneBtn = nullptr;
    QPushButton *m_instancesBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
};

#endif // MIGRATIONWIZARDDIALOG_H
