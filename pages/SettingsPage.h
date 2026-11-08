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

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage();

signals:
    void backToMainRequested();
    void downloadJavaRequested();
    void jumpToInstanceSettingsRequested(const QString &instancePath);
    /** 首页轮播图被修改（添加/移除/恢复默认），首页据此刷新轮播 */
    void carouselImagesChanged();
    /** 关于页点了「意见反馈」：跳到软件内反馈页 */
    void openFeedbackRequested();
    /** 通用设置里点了「打开新手引导」：跳到新手引导页（独立整页） */
    void openOnboardingRequested();

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
    void retranslateUi();
    void setCurrentSettingsTab(int index);

    /**
     * 子导航搜索框分发：对当前设置面板做行级过滤（卡片标题/设置行文本匹配）。
     * 切换设置子页（setCurrentSettingsTab）时自动重新应用。
     */
    Q_INVOKABLE void applyPageFilter(const QString &query);
    /** 基岩版模式：隐藏 Java 版相关设置页与设置行 */
    void setBedrockMode(bool bedrock);

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
    void initAboutSettings();
    void initProjectionEditorSettings();
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

    // AI 助手设置页：默认模型下拉（列出真实已配置模型）
    QComboBox       *m_aiDefaultModelCombo = nullptr;

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
