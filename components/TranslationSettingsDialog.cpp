/**
 * @file   TranslationSettingsDialog.cpp
 * @brief  翻译源配置对话框实现
 * @author BlockBox Team
 * @date   2026-08-24
 */

#include "TranslationSettingsDialog.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/translation/TranslationService.h"

#include <QButtonGroup>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

TranslationSettingsDialog::TranslationSettingsDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_config(SettingsManager::instance()->getTranslationConfig())
{
    initUI();
    loadCurrentConfig();
}

void TranslationSettingsDialog::initUI()
{
    // 居中卡片
    m_card = new QWidget(this);
    m_card->setObjectName("translationSettingsCard");
    m_card->setFixedSize(560, 620);

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(m_card);
    shadow->setBlurRadius(40);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0, 0, 0, 60));
    m_card->setGraphicsEffect(shadow);

    QVBoxLayout *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(16);

    // 标题行
    QWidget *headerRow = new QWidget();
    QHBoxLayout *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *titleLabel = new QLabel(tr("翻译设置"));
    titleLabel->setObjectName("translationSettingsTitle");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    m_closeBtn = new QPushButton();
    m_closeBtn->setObjectName("translationSettingsCloseBtn");
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFlat(true);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    headerLayout->addWidget(m_closeBtn);

    cardLayout->addWidget(headerRow);

    // 翻译源选择器
    cardLayout->addWidget(createSourceSelector());

    // 设置页面（堆叠）
    m_settingsStack = new QStackedWidget();
    m_settingsStack->setObjectName("translationSettingsStack");
    m_settingsStack->addWidget(createAISettingsPage());
    m_settingsStack->addWidget(createDeepLSettingsPage());
    m_settingsStack->addWidget(createBaiduSettingsPage());
    m_settingsStack->addWidget(createGoogleSettingsPage());
    m_settingsStack->addWidget(createCustomSettingsPage());
    cardLayout->addWidget(m_settingsStack, 1);

    // 测试翻译区域
    QWidget *testSection = new QWidget();
    testSection->setObjectName("translationTestSection");
    QVBoxLayout *testLayout = new QVBoxLayout(testSection);
    testLayout->setContentsMargins(0, 0, 0, 0);
    testLayout->setSpacing(8);

    QLabel *testTitle = new QLabel(tr("测试翻译"));
    testTitle->setObjectName("translationTestTitle");
    testLayout->addWidget(testTitle);

    m_testTextEdit = new QLineEdit();
    m_testTextEdit->setObjectName("translationTestInput");
    m_testTextEdit->setPlaceholderText(tr("输入英文文本测试翻译效果..."));
    m_testTextEdit->setText("Create: Astral Sorcery is a magic mod about harnessing the power of the stars.");
    testLayout->addWidget(m_testTextEdit);

    QWidget *testActionRow = new QWidget();
    QHBoxLayout *testActionLayout = new QHBoxLayout(testActionRow);
    testActionLayout->setContentsMargins(0, 0, 0, 0);

    m_testBtn = new QPushButton(tr("测试"));
    m_testBtn->setObjectName("translationTestBtn");
    m_testBtn->setCursor(Qt::PointingHandCursor);
    connect(m_testBtn, &QPushButton::clicked, this, &TranslationSettingsDialog::onTestTranslation);
    testActionLayout->addWidget(m_testBtn);
    testActionLayout->addStretch();

    m_testResultLabel = new QLabel();
    m_testResultLabel->setObjectName("translationTestResult");
    m_testResultLabel->setWordWrap(true);
    m_testResultLabel->hide();
    testActionLayout->addWidget(m_testResultLabel, 1);

    testLayout->addWidget(testActionRow);
    cardLayout->addWidget(testSection);

    // 底部按钮
    QWidget *bottomRow = new QWidget();
    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomRow);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addStretch();

    m_saveBtn = new QPushButton(tr("保存"));
    m_saveBtn->setObjectName("translationSaveBtn");
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setFixedWidth(100);
    connect(m_saveBtn, &QPushButton::clicked, this, &TranslationSettingsDialog::onSave);
    bottomLayout->addWidget(m_saveBtn);

    cardLayout->addWidget(bottomRow);
}

QWidget *TranslationSettingsDialog::createSourceSelector()
{
    QWidget *container = new QWidget();
    container->setObjectName("translationSourceSelector");
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_sourceGroup = new QButtonGroup(this);
    m_sourceGroup->setExclusive(true);

    auto createBtn = [&](const QString &text, int id) -> QPushButton* {
        QPushButton *btn = new QPushButton(text);
        btn->setObjectName("translationSourceBtn");
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        m_sourceGroup->addButton(btn, id);
        layout->addWidget(btn);
        return btn;
    };

    m_aiBtn = createBtn(tr("AI 翻译"), 0);
    m_deeplBtn = createBtn(tr("DeepL"), 1);
    m_baiduBtn = createBtn(tr("百度翻译"), 2);
    m_googleBtn = createBtn(tr("Google"), 3);
    m_customBtn = createBtn(tr("自定义"), 4);

    layout->addStretch();

    connect(m_sourceGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &TranslationSettingsDialog::onSourceChanged);

    return container;
}

QWidget *TranslationSettingsDialog::createAISettingsPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *desc = new QLabel(tr("使用已配置的 AI 模型进行翻译。请在下方填写 AI 服务端点信息。"));
    desc->setObjectName("translationDesc");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    QLabel *urlLabel = new QLabel(tr("API 端点"));
    urlLabel->setObjectName("translationFieldLabel");
    layout->addWidget(urlLabel);

    m_aiApiUrlEdit = new QLineEdit();
    m_aiApiUrlEdit->setObjectName("translationInput");
    m_aiApiUrlEdit->setPlaceholderText("https://api.deepseek.com");
    layout->addWidget(m_aiApiUrlEdit);

    QLabel *keyLabel = new QLabel(tr("API Key"));
    keyLabel->setObjectName("translationFieldLabel");
    layout->addWidget(keyLabel);

    m_aiApiKeyEdit = new QLineEdit();
    m_aiApiKeyEdit->setObjectName("translationInput");
    m_aiApiKeyEdit->setPlaceholderText("sk-...");
    m_aiApiKeyEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(m_aiApiKeyEdit);

    QLabel *modelLabel = new QLabel(tr("模型名称"));
    modelLabel->setObjectName("translationFieldLabel");
    layout->addWidget(modelLabel);

    m_aiModelEdit = new QLineEdit();
    m_aiModelEdit->setObjectName("translationInput");
    m_aiModelEdit->setPlaceholderText("deepseek-chat");
    layout->addWidget(m_aiModelEdit);

    layout->addStretch();
    return page;
}

QWidget *TranslationSettingsDialog::createDeepLSettingsPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *desc = new QLabel(tr("DeepL 提供高质量的机器翻译。免费版每月 50 万字符。"));
    desc->setObjectName("translationDesc");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    QLabel *keyLabel = new QLabel(tr("API Key"));
    keyLabel->setObjectName("translationFieldLabel");
    layout->addWidget(keyLabel);

    m_deeplKeyEdit = new QLineEdit();
    m_deeplKeyEdit->setObjectName("translationInput");
    m_deeplKeyEdit->setPlaceholderText("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx:fx");
    m_deeplKeyEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(m_deeplKeyEdit);

    QLabel *typeLabel = new QLabel(tr("账户类型"));
    typeLabel->setObjectName("translationFieldLabel");
    layout->addWidget(typeLabel);

    QWidget *typeRow = new QWidget();
    QHBoxLayout *typeLayout = new QHBoxLayout(typeRow);
    typeLayout->setContentsMargins(0, 0, 0, 0);
    typeLayout->setSpacing(6);

    m_deeplFreeBtn = new QPushButton(tr("Free（免费版）"));
    m_deeplFreeBtn->setObjectName("translationTypeBtn");
    m_deeplFreeBtn->setCheckable(true);
    m_deeplFreeBtn->setCursor(Qt::PointingHandCursor);

    m_deeplProBtn = new QPushButton(tr("Pro（专业版）"));
    m_deeplProBtn->setObjectName("translationTypeBtn");
    m_deeplProBtn->setCheckable(true);
    m_deeplProBtn->setCursor(Qt::PointingHandCursor);

    QButtonGroup *deeplTypeGroup = new QButtonGroup(this);
    deeplTypeGroup->addButton(m_deeplFreeBtn, 0);
    deeplTypeGroup->addButton(m_deeplProBtn, 1);
    deeplTypeGroup->setExclusive(true);

    typeLayout->addWidget(m_deeplFreeBtn);
    typeLayout->addWidget(m_deeplProBtn);
    typeLayout->addStretch();

    layout->addWidget(typeRow);
    layout->addStretch();
    return page;
}

QWidget *TranslationSettingsDialog::createBaiduSettingsPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *desc = new QLabel(tr("百度翻译 API。请在 <a href='https://fanyi-api.baidu.com/'>fanyi-api.baidu.com</a> 注册获取 App ID。"));
    desc->setObjectName("translationDesc");
    desc->setWordWrap(true);
    desc->setOpenExternalLinks(true);
    layout->addWidget(desc);

    QLabel *appIdLabel = new QLabel(tr("App ID"));
    appIdLabel->setObjectName("translationFieldLabel");
    layout->addWidget(appIdLabel);

    m_baiduAppIdEdit = new QLineEdit();
    m_baiduAppIdEdit->setObjectName("translationInput");
    m_baiduAppIdEdit->setPlaceholderText("20240801000012345");
    layout->addWidget(m_baiduAppIdEdit);

    QLabel *keyLabel = new QLabel(tr("密钥"));
    keyLabel->setObjectName("translationFieldLabel");
    layout->addWidget(keyLabel);

    m_baiduKeyEdit = new QLineEdit();
    m_baiduKeyEdit->setObjectName("translationInput");
    m_baiduKeyEdit->setPlaceholderText("xxxxxxxx");
    m_baiduKeyEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(m_baiduKeyEdit);

    layout->addStretch();
    return page;
}

QWidget *TranslationSettingsDialog::createGoogleSettingsPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *desc = new QLabel(tr("Google Cloud Translation API。请在 <a href='https://cloud.google.com/translate'>Google Cloud Console</a> 启用并获取 API Key。"));
    desc->setObjectName("translationDesc");
    desc->setWordWrap(true);
    desc->setOpenExternalLinks(true);
    layout->addWidget(desc);

    QLabel *keyLabel = new QLabel(tr("API Key"));
    keyLabel->setObjectName("translationFieldLabel");
    layout->addWidget(keyLabel);

    m_googleKeyEdit = new QLineEdit();
    m_googleKeyEdit->setObjectName("translationInput");
    m_googleKeyEdit->setPlaceholderText("AIza...");
    m_googleKeyEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(m_googleKeyEdit);

    layout->addStretch();
    return page;
}

QWidget *TranslationSettingsDialog::createCustomSettingsPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *desc = new QLabel(tr("配置自定义的 OpenAI 兼容翻译 API 端点。支持任意 OpenAI 格式的 API 服务。"));
    desc->setObjectName("translationDesc");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    QLabel *urlLabel = new QLabel(tr("API 端点"));
    urlLabel->setObjectName("translationFieldLabel");
    layout->addWidget(urlLabel);

    m_customUrlEdit = new QLineEdit();
    m_customUrlEdit->setObjectName("translationInput");
    m_customUrlEdit->setPlaceholderText("https://api.example.com");
    layout->addWidget(m_customUrlEdit);

    QLabel *keyLabel = new QLabel(tr("API Key"));
    keyLabel->setObjectName("translationFieldLabel");
    layout->addWidget(keyLabel);

    m_customKeyEdit = new QLineEdit();
    m_customKeyEdit->setObjectName("translationInput");
    m_customKeyEdit->setPlaceholderText("sk-...");
    m_customKeyEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(m_customKeyEdit);

    QLabel *modelLabel = new QLabel(tr("模型名称"));
    modelLabel->setObjectName("translationFieldLabel");
    layout->addWidget(modelLabel);

    m_customModelEdit = new QLineEdit();
    m_customModelEdit->setObjectName("translationInput");
    m_customModelEdit->setPlaceholderText("gpt-3.5-turbo");
    layout->addWidget(m_customModelEdit);

    layout->addStretch();
    return page;
}

void TranslationSettingsDialog::loadCurrentConfig()
{
    // 设置翻译源选择
    m_sourceGroup->button(static_cast<int>(m_config.source))->setChecked(true);
    m_settingsStack->setCurrentIndex(static_cast<int>(m_config.source));

    // AI
    m_aiApiUrlEdit->setText(m_config.customApiUrl);
    m_aiApiKeyEdit->setText(m_config.customApiKey);
    m_aiModelEdit->setText(m_config.customModel);

    // DeepL
    m_deeplKeyEdit->setText(m_config.deeplApiKey);
    if (m_config.deeplFree) {
        m_deeplFreeBtn->setChecked(true);
    } else {
        m_deeplProBtn->setChecked(true);
    }

    // 百度
    m_baiduAppIdEdit->setText(m_config.baiduAppId);
    m_baiduKeyEdit->setText(m_config.baiduSecretKey);

    // Google
    m_googleKeyEdit->setText(m_config.googleApiKey);

    // 自定义
    m_customUrlEdit->setText(m_config.customApiUrl);
    m_customKeyEdit->setText(m_config.customApiKey);
    m_customModelEdit->setText(m_config.customModel);
}

void TranslationSettingsDialog::onSourceChanged(int index)
{
    m_settingsStack->setCurrentIndex(index);
}

void TranslationSettingsDialog::onSave()
{
    TranslationSource source = static_cast<TranslationSource>(m_sourceGroup->checkedId());
    m_config.source = source;

    switch (source) {
    case TranslationSource::AI:
        m_config.customApiUrl = m_aiApiUrlEdit->text().trimmed();
        m_config.customApiKey = m_aiApiKeyEdit->text().trimmed();
        m_config.customModel = m_aiModelEdit->text().trimmed();
        break;
    case TranslationSource::DeepL:
        m_config.deeplApiKey = m_deeplKeyEdit->text().trimmed();
        m_config.deeplFree = m_deeplFreeBtn->isChecked();
        break;
    case TranslationSource::Baidu:
        m_config.baiduAppId = m_baiduAppIdEdit->text().trimmed();
        m_config.baiduSecretKey = m_baiduKeyEdit->text().trimmed();
        break;
    case TranslationSource::Google:
        m_config.googleApiKey = m_googleKeyEdit->text().trimmed();
        break;
    case TranslationSource::CustomAPI:
        m_config.customApiUrl = m_customUrlEdit->text().trimmed();
        m_config.customApiKey = m_customKeyEdit->text().trimmed();
        m_config.customModel = m_customModelEdit->text().trimmed();
        break;
    }

    SettingsManager::instance()->setTranslationConfig(m_config);
    emit configSaved();
    accept();
}

void TranslationSettingsDialog::onTestTranslation()
{
    QString text = m_testTextEdit->text().trimmed();
    if (text.isEmpty()) return;

    m_testBtn->setEnabled(false);
    m_testBtn->setText(tr("翻译中..."));
    m_testResultLabel->show();
    m_testResultLabel->setText(tr("正在翻译..."));

    // 先保存当前配置到临时 config
    TranslationConfig testConfig = m_config;
    testConfig.source = static_cast<TranslationSource>(m_sourceGroup->checkedId());

    TranslationService *service = new TranslationService(this);
    connect(service, &TranslationService::translationFinished, this,
        [this, service](const QString &result) {
            m_testBtn->setEnabled(true);
            m_testBtn->setText(tr("测试"));
            m_testResultLabel->setText(result);
            service->deleteLater();
        });
    connect(service, &TranslationService::translationError, this,
        [this, service](const QString &error) {
            m_testBtn->setEnabled(true);
            m_testBtn->setText(tr("测试"));
            m_testResultLabel->setText(tr("翻译失败: %1").arg(error));
            service->deleteLater();
        });

    service->translate(text, testConfig);
}
