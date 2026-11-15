/**
 * @file   OnboardingWizard.h
 * @brief  新手引导页面声明（首次运行配置：语言 → [下载源] → 引导方式 → 账户/导入 → Java → 文件夹 → 样式）
 * @author BlockBox Team
 * @date   2026-08-13
 */
#ifndef ONBOARDINGWIZARD_H
#define ONBOARDINGWIZARD_H

#include "utils/JavaDownloader.h"  // JavaBinaryInfo（Step3 下载回调参数）
#include "utils/SettingsManager.h" // AccountInfo

#include <QList>
#include <QLabel>
#include <QPair>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QButtonGroup;
class QStackedWidget;
class QWidget;
class QLineEdit;
class QFrame;
class QHBoxLayout;
struct LauncherDetect;

/**
 * @brief 首次运行新手引导（整页，占满主窗口内容区）：语言设置 → 引导方式 → 账户/导入 → Java → 实例文件夹 → 样式
 *
 * 作为一个独立页面放进 MainWindow 的页面栈（PageIndex::OnboardingPage），
 * 进入时隐藏侧边栏，顶部品牌 + 步骤条 + 底部导航铺满内容区。
 * 完成或跳过后写入 SettingsManager 的 onboarding/completed 标记，并发出 finished()，
 * 由主窗口决定回到首页还是设置页。
 * 第 1 步语言设置为多语言页面（用户在选定语言前需看到多语言提示）。
 * 下载源联动：选择非中文（English / Español）时自动切到官方源；
 * 选择繁體中文时，引导中会新增一步「下载源选择」由用户自行决定。
 */
class OnboardingWizard : public QWidget
{
    Q_OBJECT

public:
    explicit OnboardingWizard(QWidget *parent = nullptr);

    /** 引导方式枚举：手动填写 / 从现有启动器导入 / 跳过 */
    enum Method { MethodManual = 0, MethodImport = 1, MethodSkip = 2 };

    /** 重新从第一步开始（从设置里再次打开本页时调用，清掉上一次的完成态） */
    void resetToFirstStep();

    /** 完成页是否显示「返回设置」按钮（首次启动进入时没有「设置」这个来路，隐藏） */
    void setReturnToSettingsAvailable(bool available);

signals:
    /** 引导走完（完成页「开始使用方块盒子」/ 跳过引导后的完成页） */
    void finished();
    /** 完成页「返回设置」或顶栏返回：请求离开本页 */
    void backRequested();
    /** 语言切换已写入偏好：需要主程序重启进程才能干净加载新语言 */
    void restartRequested();

private slots:
    void onPickLanguage(int index);
    void onMethodPicked(int method);
    void onPickDownloadSource(int index);
    void onNextClicked();
    void onPrevClicked();
    void onSkipClicked();
    void onLauncherScan(const QString &key);
    void onLauncherImport(const QString &key);
    /// 异步完成首次检测后，把结果填进四张卡片
    void refreshLauncherCards();
    /// 把单个启动器的检测结果应用到对应卡片（状态点、文案、按钮可用性）
    void applyLauncherDetect(int idx, const LauncherDetect &det);
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
    /// 按当前主题色点亮强调色色板并更新名称（自定义色不点亮任何色块）
    void syncAccentSelection();

    // Step3 Java 下载（引导页内直接下载并安装官方 Java）
    void onJavaVersionListFetched(const QVector<JavaBinaryInfo> &binaries);
    void onJavaFetchError(const QString &error);
    void onJavaDownloadProgress(const QString &fileName, int percent);
    void onJavaDownloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onJavaDownloadFailed(const QString &error);
    void onJavaExtractCompleted(const QString &javaPath);
    void onJavaExtractFailed(const QString &error);

private:
    void initUI();
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
    /// 四张启动器卡片按可用宽度算一次固定宽度（四张始终等宽，不随内容变化）
    void updateLauncherCardWidths();

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
    /// 写入语言偏好并请求重启（发出 restartRequested()；由主程序启动新进程）
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
    /// 注册进实例文件夹时用的名字（启动器名 + 游戏目录所在盘符目录名）
    QString importedFolderName(const QString &key, const QString &dir) const;
    /// 把 java.exe 合并进 Java 列表（已存在返回 false）
    bool importJavaPath(const QString &javaExe);
    QList<QPair<QString, QString>> lightJavaDetect() const;

    /// 发起 Java 下载（mirrorId 为空时按当前语言自动选择镜像源）
    void startJavaDownload(int majorVersion, const QString &mirrorId = QString());
    /// 下载/安装期间禁用版本卡按钮，结束后恢复
    void setJavaDownloadBusy(bool busy);
    /// 下载完成后把 java.exe 登记进 Java 列表，并刷新上方列表
    void registerDownloadedJava(const QString &javaPath);

    // 状态
    int m_method = MethodManual;      ///< 当前选择的引导方式
    int m_currentStep = 0;            ///< 当前逻辑步骤（语言 0 / 引导方式 1 / … / 完成页=doneStep()）
    bool m_scanningJava = false;      ///< Java 轻量检测进行中
    bool m_hasDlSourceStep = false;   ///< 当前语言为繁體中文，引导中插入「下载源选择」步骤
    QStringList m_imported;           ///< 已导入的启动器 key

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
    QPushButton *m_skipBtn = nullptr;   ///< 跳过引导（底部，紧挨「下一步」左侧）
    QPushButton *m_nextBtn = nullptr;

    // Step0 语言设置
    QList<QPushButton *> m_languageCards;   ///< 语言卡片（与 LanguageManager::Language 顺序一致）

    // Step1 引导方式
    QWidget *m_methodCards[3] = {nullptr, nullptr, nullptr};
    QList<QLabel *> m_methodChecks;   ///< Step1 卡片右上角对勾徽章

    // Step2 导入（桌面 4 张卡：PCL / HMCL / MultiMC / BakaXL；安卓 2 张卡：FCL / ZL2）
    QList<QString> m_launcherKeys;    ///< 启动器 key（与卡片顺序一致，随平台变化）
    QList<QWidget *> m_launcherCards; ///< 启动器卡片（固定等宽）
    QHBoxLayout *m_launcherCardsLayout = nullptr; ///< 卡片行布局（用来按可用宽度算固定宽度）
    QList<QWidget *> m_launcherActions;       ///< 每张卡的操作行
    QList<QLabel *> m_launcherStatus;         ///< 每张卡的状态圆点
    QList<QLabel *> m_launcherStatusText;     ///< 每张卡的检测摘要
    QList<QPushButton *> m_launcherImportBtn; ///< 每张卡的「导入」按钮（未检测到时置灰）

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
    QLabel *m_javaDlStatusLabel = nullptr;   ///< 下载/安装进度提示
    QList<QPushButton *> m_javaDlButtons;    ///< 四张版本卡的「下载」按钮（下载中禁用）
    bool m_javaDownloaderWired = false;      ///< 是否已把下载器信号接到本向导
    bool m_runtimeDepsWired = false;         ///< 安卓：运行依赖通道信号是否已接到本向导
    int m_javaDlMajor = 0;                   ///< 正在下载的 Java 主版本号（0=空闲）
    bool m_javaDlRetried = false;            ///< 镜像源失败后是否已回落官方源重试

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
    QButtonGroup *m_accentGroup = nullptr;            ///< 强调色色板（与设置里的主题色同一份预设）

    // 完成页
    QWidget *m_doneSummaryWidget = nullptr;
    QPushButton *m_doneBackBtn = nullptr;     ///< 完成页「返回设置」（首次启动进入时隐藏）
    bool m_doneBackAvailable = true;
};

#endif // ONBOARDINGWIZARD_H
