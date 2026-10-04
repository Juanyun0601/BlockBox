/**
 * @file   OnboardingWizard.h
 * @brief  新手引导向导声明（首次运行配置：语言 → [下载源] → 引导方式 → 账户/导入 → Java → 文件夹 → 样式）
 * @author BlockBox Team
 * @date   2026-08-13
 */
#ifndef ONBOARDINGWIZARD_H
#define ONBOARDINGWIZARD_H

#include "components/AppDialogBase.h"
#include "utils/SettingsManager.h" // AccountInfo

#include <QList>
#include <QLabel>
#include <QPair>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QStringList>

class QLabel;
class QPushButton;
class QStackedWidget;
class QWidget;
class QLineEdit;
class QFrame;

/**
 * @brief 首次运行新手引导：语言设置 → 引导方式 → 账户/导入 → Java → 实例文件夹 → 样式
 *
 * 覆盖宿主窗口的模态向导（基于 AppDialogBase 模糊遮罩），
 * 顶部品牌 + 步骤条 + 底部导航，完成或跳过后写入
 * SettingsManager 的 onboarding/completed 标记。
 * 第 1 步语言设置为多语言页面（用户在选定语言前需看到多语言提示）。
 * 下载源联动：选择非中文（English / Español）时自动切到官方源；
 * 选择繁體中文时，引导中会新增一步「下载源选择」由用户自行决定。
 */
class OnboardingWizard : public AppDialogBase
{
    Q_OBJECT

public:
    explicit OnboardingWizard(QWidget *parent = nullptr);

    /** 在向导 exec() 返回后查询：是否请求重启（语言切换后由主程序重启以干净加载语言） */
    bool restartRequested() const { return m_restartRequested; }

    /** 引导方式枚举：手动填写 / 从现有启动器导入 / 跳过 */
    enum Method { MethodManual = 0, MethodImport = 1, MethodSkip = 2 };

private slots:
    void onPickLanguage(int index);
    void onMethodPicked(int method);
    void onPickDownloadSource(int index);
    void onNextClicked();
    void onPrevClicked();
    void onSkipClicked();
    void onLauncherScan(const QString &key);
    void onLauncherImport(const QString &key);
    void onToggleAddAccount();
    void onSwitchAccountTab(int index);
    void onAddOfflineAccount();
    void onScanJava();
    void onToggleAddFolder();
    void onAddFolder();
    void onPickTheme(bool dark);
    void onPickBg(int index);
    void onPickSolidColor(const QString &hex);
    void onBrowseImage();
    void onPickAccent(const QString &colorName);

private:
    void initUI();
    QWidget *buildHeader(QWidget *parent);
    void buildStepper(QWidget *parent);
    void buildStep0();
    void buildStepDlSource();
    void buildStep1();
    void buildStep2Import();
    void buildStep2Manual();
    void buildStep3();
    void buildStep4();
    void buildStep5();
    void buildDonePage();
    QWidget *buildFooter(QWidget *parent);

protected:
    void resizeEvent(QResizeEvent *event) override;

    /** 显示逻辑步骤：0=语言 1=引导方式 2=导入/账户 3=Java 4=文件夹 5=样式 6=完成；
     *  导入方式跳过 Java(3)/文件夹(4)：0=语言 1=引导方式 2=导入 3=样式 4=完成；
     *  选择繁體中文时在语言之后插入下载源步骤（各流程逻辑步骤整体 +1）。 */
    void showStep(int logicalStep);
    /** 由逻辑步骤映射到 stack 索引 */
    int stackIndexForStep(int logicalStep) const;
    /** 完成页逻辑步骤（导入=4/5，手动=6/7，繁体中文多一步下载源） */
    int doneStep() const;
    /** 步骤条进度（导入方式只计 4 步，跳过的步骤不计入） */
    int stepperProgress() const;
    void goDone();
    void updateFooterState();
    void syncStepper();
    /// 写入语言偏好并请求重启（reject 本对话框；由主程序启动新进程）
    void requestLanguageRestart(int langIndex);
    /** 将原版 + 各加载器下载源统一切到官方源（选择英文/西班牙文时调用） */
    void setAllDownloadSourcesOfficial();
    QLabel *makeSectionTitle(const QString &text);
    void refreshJavaList();
    void refreshFolderList();
    void refreshAccountList();
    void addAccountRow(const QString &name, const QString &typeBadge,
                       const QString &typeClass, const QString &meta, bool isDefault);
    void updateImagePreview(const QString &path);
    QList<AccountInfo> importAccountsFor(const QString &key);
    QString launcherDisplayName(const QString &key) const;
    void importJavaPath(const QString &javaExe);
    QList<QPair<QString, QString>> lightJavaDetect() const;
    QString javaVersionFromPath(const QString &javaExe) const;

    // 状态
    int m_method = MethodManual;      ///< 当前选择的引导方式
    int m_currentStep = 0;            ///< 当前逻辑步骤（语言 0 / 引导方式 1 / … / 完成页=doneStep()）
    bool m_scanningJava = false;      ///< Java 轻量检测进行中
    bool m_restartRequested = false;  ///< 语言切换已请求重启
    bool m_hasDlSourceStep = false;   ///< 当前语言为繁體中文，引导中插入「下载源选择」步骤
    QStringList m_imported;           ///< 已导入的启动器 key

    // 头部
    QLabel *m_brandLogo = nullptr;
    QLabel *m_brandTitle = nullptr;
    QLabel *m_brandSub = nullptr;
    QPushButton *m_skipBtn = nullptr;

    // 步骤条
    QWidget *m_stepperWidget = nullptr;
    QList<QWidget *> m_stepNodes;     ///< 7 个步骤节点（导入方式隐藏 Java/文件夹，非繁体隐藏下载源，剩 4/6 个）
    QList<QLabel *> m_stepDots;       ///< 7 个步骤圆点
    QList<QLabel *> m_stepLabels;     ///< 7 个步骤文字
    QList<QFrame *> m_stepLines;      ///< 6 条连接线

    // 内容
    QStackedWidget *m_stack = nullptr;
    QList<QWidget *> m_pages;         ///< stack 页面：0 语言 1 下载源 2 引导 3 导入 4 账户 5 Java 6 文件夹 7 样式 8 完成

    // 底部
    QLabel *m_footerHint = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;

    // Step0 语言设置
    QList<QPushButton *> m_languageCards;   ///< 语言卡片（与 LanguageManager::Language 顺序一致）

    // Step1 引导方式
    QWidget *m_methodCards[3] = {nullptr, nullptr, nullptr};
    QList<QLabel *> m_methodChecks;   ///< Step1 卡片右上角对勾徽章

    // Step2 导入
    QList<QString> m_launcherKeys;    ///< 启动器 key（与卡片顺序一致）
    QWidget *m_launcherActions[3] = {nullptr, nullptr, nullptr};
    QLabel *m_launcherStatus[3] = {nullptr, nullptr, nullptr};
    QLabel *m_launcherStatusText[3] = {nullptr, nullptr, nullptr};

    // Step2 账户
    QWidget *m_accountListWidget = nullptr;
    QWidget *m_addAccountPanel = nullptr;
    QWidget *m_formMicrosoft = nullptr;
    QWidget *m_formOffline = nullptr;
    QWidget *m_formLegacy = nullptr;
    QLineEdit *m_offlineNameInput = nullptr;

    // Step3 Java
    QWidget *m_javaListWidget = nullptr;
    QPushButton *m_scanJavaBtn = nullptr;
    QLabel *m_scanJavaText = nullptr;

    // Step4 文件夹
    QWidget *m_folderListWidget = nullptr;
    QWidget *m_addFolderPanel = nullptr;
    QLineEdit *m_newFolderName = nullptr;
    QLineEdit *m_newFolderPath = nullptr;

    // Step5 样式
    QStackedWidget *m_bgSubStack = nullptr;           ///< 背景子项（经典/纯色/图片）
    QList<QPushButton *> m_colorSwatches;             ///< 纯色预设色块
    QLabel *m_imagePreview = nullptr;                 ///< 图片模式预览
    QLabel *m_imagePathLabel = nullptr;               ///< 图片模式路径
    QLabel *m_accentNameLabel = nullptr;

    // 完成页
    QWidget *m_doneSummaryWidget = nullptr;
};

#endif // ONBOARDINGWIZARD_H
