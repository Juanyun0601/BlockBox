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

class CustomCheckBox;

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

private slots:
    void onNavItemClicked(int index);
    void onRestoreDefaults();
    void onThemeChanged(int index);
    void onBackgroundModeChanged(int index);
    // Java 管理页相关槽
    void onScanJavaClicked();
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
    QWidget *m_imageSection;
    QWidget *m_bingSection;
    QPushButton *m_solidColorBtn;
    QLineEdit *m_imagePathEdit;
    QPushButton *m_browseImageBtn;

    // Java 管理页控件
    QListWidget *m_javaListWidget;
    QLabel *m_javaEmptyLabel;
    QComboBox *m_globalJavaCombo;
    CustomCheckBox *m_autoSelectJavaCheck;

    // 全局游戏设置中的 Java 相关卡片（基岩版模式隐藏）
    QWidget *m_gameInstallPathCard = nullptr;
    QWidget *m_gameJavaPathCard = nullptr;
    QWidget *m_gameMemoryCard = nullptr;

    bool m_bedrockMode = false;
};

#endif // SETTINGSPAGE_H
