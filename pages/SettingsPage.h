/**
 * @file   SettingsPage.h
 * @brief  Settings page class declaration
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "utils/PageFilter.h"

class CustomCheckBox;
class FlowLayout;
class QProgressBar;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage();

    /** 首页下载完预设封面 / 导入了自己的风景图后，同步「界面设置」里的轮播图卡片 */
    void refreshCarouselEditor();

signals:
    void backToMainRequested();
    void downloadJavaRequested();
    /** 常规设置里点了「网络连通性自检」：弹出网络自检对话框 */
    void networkCheckRequested();
    void jumpToInstanceSettingsRequested(const QString &instancePath);
    /** 首页轮播图被修改（添加/移除/恢复默认），首页据此刷新轮播 */
    void carouselImagesChanged();
    /** 关于页点了「意见反馈」：跳到软件内反馈页 */
    void openFeedbackRequested();
    /** 关于页点了「文档中心」：跳到软件内方块图书馆 */
    void openLibraryRequested();
    /** 设置页里带目标的文档入口：打开内置图书馆并跳到指定卷 / 章（如 MCP 接入说明） */
    void openLibraryDocRequested(const QString &volume, const QString &chapter);
    /** 通用设置里点了「打开新手引导」：跳到新手引导页（独立整页） */
    void openOnboardingRequested();
    /** AI 助手设置里改了本地 MCP 服务器开关 / 端口 / 令牌 / 写权限 */
    void mcpServerSettingsChanged();

private slots:
    void onNavItemClicked(int index);
    void onRestoreDefaults();
    void onThemeChanged(int index);
    void onBackgroundModeChanged(int index);
    // Java 管理页相关槽
    void onDetectJavaClicked();
    void onAddJavaClicked();
    void onRemoveJavaClicked(const QString& javaPath);
    void onOpenJavaFolderClicked(const QString& javaPath);
    void onGlobalJavaChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

public:
    /** 设置子页标识：主窗口按页面定位页签，避免硬编码索引随版本模式错位 */
    enum SettingsTabId {
        GeneralTab = 0,
        InterfaceTab,
        GameTab,
        InstanceTab,
        JavaTab,
        AdvancedTab,
        KeyBindTab,
        SystemInfoTab,
        AiAssistantTab,
        AboutTab,
        ProjectionEditorTab,
        ControlTab
    };

    void retranslateUi();
    void setCurrentSettingsTab(int index);
    /** 当前版本模式下某个设置子页的页签索引；该页不存在时返回 -1 */
    int tabIndex(SettingsTabId id) const;
    /** 页签对应的顶栏标题（随 Java / 基岩版模式自动取对应页面） */
    QString tabTitle(int index) const;

    /**
     * 子导航搜索框分发：对当前设置面板做行级过滤（卡片标题/设置行文本匹配）。
     * 切换设置子页（setCurrentSettingsTab）时自动重新应用。
     */
    Q_INVOKABLE void applyPageFilter(const QString &query);
    /** 基岩版模式：隐藏 Java 版相关设置页与设置行 */
    void setBedrockMode(bool bedrock);

    /**
     * @brief 更新「本地 MCP 服务器」卡片的状态文字
     * @param running 是否正在监听
     * @param port    实际端口
     * @param detail  失败原因或附加说明
     */
    void setMcpServerStatus(bool running, quint16 port, const QString &detail);

private:
    void initUI();
    void initRightContent();
    void rebuildEditionStack();
    void updateGameSettingsForEdition();
    // 切换设置子页时重置外层滚动区域到顶部
    void resetSettingsScroll();
    
    // Settings module initializers
    void initGeneralSettings();
    void initInterfaceSettings();
    void initGlobalGameSettings();
    void initBedrockGameSettings();
    void initInstanceSettings();
    void initJavaManagerSettings();
    void initAdvancedSettings();
    void initKeyBindSettings();
    void initSystemInfoSettings();
    void initAiAssistantSettings();
    /** 重新扫描本机 DSH 配置，刷新「DSH 插件」卡片的下拉和状态 */
    void refreshDshPluginCard();
    /** 按当前选中的 DSH 配置更新「DSH 插件」卡片的状态文字与按钮 */
    void updateDshPluginStatus();
    /** 探测本机 ZCode 数据目录，刷新「ZCode 插件」卡片的状态 */
    void refreshZcodePluginCard();
    /** 更新「ZCode 插件」卡片的状态文字与按钮 */
    void updateZcodePluginStatus();
    void initAboutSettings();
    void initProjectionEditorSettings();
    /** 移动端「操作设置」：触屏方案、手柄与视角参数 */
    void initControlSettings();
    /** 将存储的投影编辑器设置同步到设置页控件（初始化 / 恢复默认后调用） */
    void refreshProjectionEditorSettingsControls();
    /** 刷新 AI 助手设置页「默认模型」下拉：列出已配置/已添加的真实模型 */
    void refreshAiDefaultModelCombo();

    // Help tooltip — inserts a help button after the label, click to show description
    void addHelpTooltip(QBoxLayout *layout, QLabel *label, const QString &text);

    // Disables mouse wheel on a widget to prevent accidental value changes
    void disableWheelEffect(QWidget *widget);

    // Creates a settings card and appends it to parentLayout.
    // Returns the card's content layout for adding setting rows.
    // When a title is provided, the card gets a clickable header that
    // collapses / expands its content.
    QVBoxLayout *createSettingsCard(QBoxLayout *parentLayout,
                                    const QString &title = QString(),
                                    bool startCollapsed = false);

    // Creates a settings header card (title on the left + restore defaults
    // button on the right) and appends it to parentLayout.
    QFrame *createSettingsHeader(QBoxLayout *parentLayout, const QString &title);

    // Appends a setting row (title + optional description + optional help button
    // on the left, control area on the right) to cardLayout.
    // Returns the control area layout for the caller to add widgets to.
    QHBoxLayout *appendSettingRow(QVBoxLayout *cardLayout,
                                  const QString &title,
                                  const QString &desc = QString(),
                                  const QString &help = QString(),
                                  bool isLast = false);

    // Appends a setting block: title + description on top, with the control
    // area stacked underneath at full row width (for multi-line editors).
    // Returns the block's vertical layout for the caller to add widgets to.
    QVBoxLayout *appendSettingBlock(QVBoxLayout *cardLayout,
                                    const QString &title,
                                    const QString &desc = QString(),
                                    const QString &help = QString(),
                                    bool isLast = false);

    // Java 管理页辅助
    void refreshJavaList();
    void refreshGlobalJavaCombo();
    QWidget* createJavaItemWidget(const QString& path, const QString& version);
    
    QVBoxLayout *m_mainLayout;
    QVBoxLayout *m_rightLayout;
    
    QFrame *m_rightFrame;
    
    QStackedWidget *m_contentStack;
    QWidget *m_generalSettings;
    QWidget *m_interfaceSettings;
    QWidget *m_globalGameSettings;
    QWidget *m_bedrockGameSettings;
    QWidget *m_instanceSettings;
    QWidget *m_javaManagerSettings;
    QWidget *m_advancedSettings;
    QWidget *m_keyBindSettings;
    QWidget *m_systemInfoSettings;
    QWidget *m_aiAssistantSettings;
    QWidget *m_aboutSettings;
    QWidget *m_projectionEditorSettings = nullptr;
    QWidget *m_controlSettings = nullptr;

    // AI 助手设置页：默认模型下拉（列出真实已配置模型）
    QComboBox       *m_aiDefaultModelCombo = nullptr;
    // AI 助手设置页：本地 MCP 服务器状态标签
    QLabel          *m_mcpStatusLabel = nullptr;
    // AI 助手设置页：DSH 插件一键安装（目标配置下拉、状态、两个按钮）
    QComboBox       *m_dshProfileCombo = nullptr;
    QLabel          *m_dshStatusLabel = nullptr;
    QPushButton     *m_dshInstallButton = nullptr;
    QPushButton     *m_dshUninstallButton = nullptr;
    // AI 助手设置页：ZCode 插件一键安装（状态、两个按钮；无下拉，只有一个用户级安装位置）
    QLabel          *m_zcodeStatusLabel = nullptr;
    QPushButton     *m_zcodeInstallButton = nullptr;
    QPushButton     *m_zcodeUninstallButton = nullptr;

    // 投影编辑器内置设置控件
    QComboBox       *m_projToolCombo = nullptr;
    CustomCheckBox  *m_projShowAirCheck = nullptr;
    CustomCheckBox  *m_projTextureCheck = nullptr;
    CustomCheckBox  *m_projShapesCheck = nullptr;
    CustomCheckBox  *m_projInvertYCheck = nullptr;
    QSlider         *m_projSensitivitySlider = nullptr;
    QLabel          *m_projSensitivityValue = nullptr;
    QLineEdit       *m_projSourceInstanceEdit = nullptr;
    
    int m_currentIndex;
    
    // Forge download source selector
    QComboBox *m_forgeDownloadSourceCombo;
    // Fabric download source selector
    QComboBox *m_fabricDownloadSourceCombo;
    // OptiFine download source selector
    QComboBox *m_optiFineDownloadSourceCombo;
    // NeoForge download source selector
    QComboBox *m_neoForgeDownloadSourceCombo;

    // Background settings
    QComboBox *m_backgroundModeCombo;
    QWidget *m_solidColorSection;
    QWidget *m_rotatingSection;
    QWidget *m_imageSection;
    QWidget *m_blurSection;
    QWidget *m_bingSection;
    QComboBox *m_sceneCombo;
    // 全景场景按需下载的状态行
    QLabel *m_sceneStatusLabel = nullptr;
    QProgressBar *m_sceneProgressBar = nullptr;
    QPushButton *m_sceneRetryBtn = nullptr;
    QString m_pendingPanoramaScene;
    QString m_sceneDownloadError;
    /** 刷新「全景场景」下载状态文字 / 进度条 / 重试按钮 */
    void updatePanoramaDownloadStatus();
    QPushButton *m_solidColorBtn;
    QLineEdit *m_imagePathEdit;
    QPushButton *m_browseImageBtn;

    // 首页轮播图（界面设置）：自定义图片的管理控件
    QLabel *m_homeCarouselStatusLabel = nullptr;
    QWidget *m_homeCarouselStrip = nullptr;
    FlowLayout *m_homeCarouselStripLayout = nullptr;
    void refreshHomeCarouselRows();
    void addHomeCarouselImages();
    void clearHomeCarouselImages();

    // 语言设置：不随安装包内置的语言在选中后要联网下载语言包
    QComboBox *m_languageCombo = nullptr;
    QFrame *m_languagePackRow = nullptr;
    QLabel *m_languagePackStatusLabel = nullptr;
    QProgressBar *m_languagePackProgressBar = nullptr;
    QPushButton *m_languagePackActionBtn = nullptr;
    /** 正在下载 / 待下载的语言（LanguageManager::Language 的枚举值，-1 表示没有） */
    int m_languagePackTarget = -1;
    QString m_languagePackError;
    void refreshLanguageComboLabels();
    void updateLanguagePackStatus();
    void startLanguagePackDownload(int langIndex);

    // Java 管理页控件
    QListWidget *m_javaListWidget;
    QLabel *m_javaEmptyLabel;
    QComboBox *m_globalJavaCombo;
    CustomCheckBox *m_autoSelectJavaCheck;
    QPushButton *m_javaDetectBtn = nullptr;
    bool m_javaDetecting = false;

    // 全局游戏设置中的 Java 相关卡片（基岩版模式隐藏）
    QWidget *m_gameInstallPathCard = nullptr;
    QWidget *m_gameJavaPathCard = nullptr;
    QWidget *m_gameMemoryCard = nullptr;

    bool m_bedrockMode = false;

    // 子导航搜索：当前面板行级过滤器 + 当前查询（切页重应用）
    PageFilter::PanelFilter m_panelFilter;
    QString m_pageFilterQuery;
};

#endif // SETTINGSPAGE_H
