/**
 * @file   VoiceSettingsDialog.cpp
 * @brief  语音识别服务配置对话框实现
 * @author BlockBox Team
 * @date   2026-09-12
 */

#include "VoiceSettingsDialog.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

VoiceSettingsDialog::VoiceSettingsDialog(QWidget *parent, const VoiceInputController::Config &config)
    : AppDialogBase(parent)
    , m_config(config)
{
    setObjectName(QStringLiteral("voiceSettingsDialog"));
    setWindowTitle(tr("语音识别设置"));

    initUI();
    initStyle();
}

void VoiceSettingsDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumWidth(440);
    card->setMaximumWidth(520);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(26, 22, 26, 20);
    cardLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel(windowTitle(), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    titleLabel->setWordWrap(true);
    cardLayout->addWidget(titleLabel);

    QLabel *descLabel = new QLabel(
        tr("语音输入使用 OpenAI 兼容的转写接口（/v1/audio/transcriptions）。"
           "支持 SiliconFlow、OpenAI、Groq 及本地 whisper.cpp 服务；本地服务 API Key 可随意填写。"),
        card);
    descLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    descLabel->setWordWrap(true);
    cardLayout->addWidget(descLabel);

    auto addField = [this, card, cardLayout](const QString &caption, const QString &initialValue) -> QLineEdit *
    {
        QLabel *label = new QLabel(caption, card);
        label->setObjectName(QStringLiteral("appDialogPromptLabel"));
        cardLayout->addWidget(label);

        QLineEdit *edit = new QLineEdit(card);
        edit->setObjectName(QStringLiteral("appDialogLineEdit"));
        edit->setText(initialValue);
        edit->setPlaceholderText(caption);
        cardLayout->addWidget(edit);
        return edit;
    };

    m_apiUrlEdit = addField(tr("API 地址"), m_config.apiUrl);
    m_modelEdit = addField(tr("模型 ID"), m_config.model);
    m_keyEdit = addField(tr("API Key"), m_config.apiKey);
    m_keyEdit->setEchoMode(QLineEdit::Password);

    m_showKeyCheck = new QCheckBox(tr("显示 API Key"), card);
    m_showKeyCheck->setObjectName(QStringLiteral("voiceShowKeyCheck"));
    cardLayout->addWidget(m_showKeyCheck);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    btnRow->addStretch();

    QPushButton *okBtn = new QPushButton(card);
    okBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    okBtn->setText(tr("保存"));
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setDefault(true);

    QPushButton *cancelBtn = new QPushButton(card);
    cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    cancelBtn->setText(tr("取消"));
    cancelBtn->setCursor(Qt::PointingHandCursor);

    btnRow->addWidget(okBtn);
    btnRow->addWidget(cancelBtn);
    cardLayout->addLayout(btnRow);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(card, 0, 0, Qt::AlignCenter);

    connect(okBtn, &QPushButton::clicked, this, &VoiceSettingsDialog::onOk);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apiUrlEdit, &QLineEdit::returnPressed, this, &VoiceSettingsDialog::onOk);
    connect(m_keyEdit, &QLineEdit::returnPressed, this, &VoiceSettingsDialog::onOk);
    connect(m_showKeyCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        m_keyEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });
}

void VoiceSettingsDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";
    const QString fieldBg    = isLight ? "#f5f5f5" : "#3b3b40";
    const QString fieldBorder = isLight ? "#d4d4d4" : "#55555a";
    const QString btnText    = isLight ? "#333333" : "#e8e8e8";

    const QString style = QString(
        "QWidget#appDialogCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 16px;"
        "}"
        "QLabel#appDialogTitleLabel {"
        "    color: %3;"
        "    background-color: transparent;"
        "    font-size: 15px;"
        "    font-weight: bold;"
        "}"
        "QLabel#appDialogPromptLabel {"
        "    color: %4;"
        "    background-color: transparent;"
        "    font-size: 13px;"
        "}"
        "QLineEdit#appDialogLineEdit {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 10px;"
        "    color: %4;"
        "    padding: 8px 12px;"
        "    font-size: 13px;"
        "}"
        "QCheckBox#voiceShowKeyCheck {"
        "    color: %4;"
        "    background-color: transparent;"
        "    font-size: 12px;"
        "}"
        "QPushButton#appDialogBtn {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 12px;"
        "    color: %5;"
        "    padding: 7px 22px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtn:hover {"
        "    background-color: rgba(0,0,0,28);"
        "}"
        "QPushButton#appDialogBtnPrimary {"
        "    background-color: %6;"
        "    border: 1px solid %6;"
        "    border-radius: 12px;"
        "    color: #ffffff;"
        "    padding: 7px 26px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtnPrimary:hover {"
        "    background-color: %9;"
        "    border-color: %9;"
        "}"
    ).arg(cardBg, cardBorder, titleColor, textColor, btnText, themeColor, fieldBg, fieldBorder, themeHover);

    setStyleSheet(style);
}

void VoiceSettingsDialog::onOk()
{
    m_config.apiUrl = m_apiUrlEdit->text().trimmed();
    m_config.model = m_modelEdit->text().trimmed();
    m_config.apiKey = m_keyEdit->text().trimmed();
    m_accepted = true;
    accept();
}

bool VoiceSettingsDialog::configure(QWidget *parent, VoiceInputController::Config &config)
{
    VoiceSettingsDialog dlg(parent, config);
    const bool accepted = (dlg.exec() == QDialog::Accepted);
    if (accepted && dlg.m_accepted)
    {
        config = dlg.m_config;
    }
    return accepted;
}
