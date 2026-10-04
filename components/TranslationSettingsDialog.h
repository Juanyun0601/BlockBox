/**
 * @file   TranslationSettingsDialog.h
 * @brief  翻译源配置对话框
 * @author BlockBox Team
 * @date   2026-08-24
 */

#ifndef TRANSLATIONSETTINGSDIALOG_H
#define TRANSLATIONSETTINGSDIALOG_H

#include "components/AppDialogBase.h"
#include "utils/translation/TranslationService.h"

class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QStackedWidget;
class QButtonGroup;

/**
 * @brief 翻译源配置对话框
 *
 * 继承 AppDialogBase，提供模糊背景居中卡片样式。
 * 支持配置 AI/DeepL/百度/Google/自定义 API 五种翻译源。
 */
class TranslationSettingsDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit TranslationSettingsDialog(QWidget *parent = nullptr);

signals:
    void configSaved();

private slots:
    void onSourceChanged(int index);
    void onSave();
    void onTestTranslation();

private:
    void initUI();
    void loadCurrentConfig();
    QWidget *createSourceSelector();
    QWidget *createAISettingsPage();
    QWidget *createDeepLSettingsPage();
    QWidget *createBaiduSettingsPage();
    QWidget *createGoogleSettingsPage();
    QWidget *createCustomSettingsPage();

    QWidget *m_card;

    // Source selector
    QButtonGroup *m_sourceGroup;
    QPushButton *m_aiBtn;
    QPushButton *m_deeplBtn;
    QPushButton *m_baiduBtn;
    QPushButton *m_googleBtn;
    QPushButton *m_customBtn;

    // Stacked settings pages
    QStackedWidget *m_settingsStack;

    // AI settings
    QLineEdit *m_aiApiUrlEdit;
    QLineEdit *m_aiApiKeyEdit;
    QLineEdit *m_aiModelEdit;

    // DeepL settings
    QLineEdit *m_deeplKeyEdit;
    QPushButton *m_deeplFreeBtn;
    QPushButton *m_deeplProBtn;

    // Baidu settings
    QLineEdit *m_baiduAppIdEdit;
    QLineEdit *m_baiduKeyEdit;

    // Google settings
    QLineEdit *m_googleKeyEdit;

    // Custom API settings
    QLineEdit *m_customUrlEdit;
    QLineEdit *m_customKeyEdit;
    QLineEdit *m_customModelEdit;

    // Test
    QLineEdit *m_testTextEdit;
    QLabel *m_testResultLabel;
    QPushButton *m_testBtn;

    // Actions
    QPushButton *m_saveBtn;
    QPushButton *m_closeBtn;

    TranslationConfig m_config;
};

#endif // TRANSLATIONSETTINGSDIALOG_H
