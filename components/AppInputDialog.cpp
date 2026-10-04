#include "AppInputDialog.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

AppInputDialog::AppInputDialog(QWidget *parent, InputType type, const QString &title, const QString &label)
    : AppDialogBase(parent)
    , m_type(type)
    , m_label(label)
    , m_result()
    , m_accepted(false)
    , m_lineEdit(nullptr)
    , m_comboBox(nullptr)
    , m_okBtn(nullptr)
    , m_cancelBtn(nullptr)
{
    setObjectName(QStringLiteral("appInputDialog"));
    setWindowTitle(title);

    initUI();
    initStyle();
}

void AppInputDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumWidth(380);
    card->setMaximumWidth(480);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(26, 22, 26, 20);
    cardLayout->setSpacing(14);

    QLabel *titleLabel = new QLabel(windowTitle(), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    titleLabel->setWordWrap(true);

    QLabel *promptLabel = new QLabel(m_label, card);
    promptLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    promptLabel->setWordWrap(true);

    cardLayout->addWidget(titleLabel);
    cardLayout->addSpacing(2);
    cardLayout->addWidget(promptLabel);

    if (m_type == TextInput) {
        m_lineEdit = new QLineEdit(card);
        m_lineEdit->setObjectName(QStringLiteral("appDialogLineEdit"));
        cardLayout->addWidget(m_lineEdit);
        m_comboBox = nullptr;
    } else {
        m_comboBox = new QComboBox(card);
        m_comboBox->setObjectName(QStringLiteral("appDialogComboBox"));
        cardLayout->addWidget(m_comboBox);
        m_lineEdit = nullptr;
    }

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    btnRow->addStretch();

    m_okBtn = new QPushButton(card);
    m_okBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_okBtn->setText(tr("确定"));
    m_okBtn->setCursor(Qt::PointingHandCursor);
    m_okBtn->setDefault(true);

    m_cancelBtn = new QPushButton(card);
    m_cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_cancelBtn->setText(tr("取消"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);

    btnRow->addWidget(m_okBtn);
    btnRow->addWidget(m_cancelBtn);
    cardLayout->addLayout(btnRow);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(card, 0, 0, Qt::AlignCenter);

    connect(m_okBtn, &QPushButton::clicked, this, &AppInputDialog::onOk);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    if (m_lineEdit)
        connect(m_lineEdit, &QLineEdit::returnPressed, this, &AppInputDialog::onOk);
}

void AppInputDialog::initStyle()
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
        "QLineEdit#appDialogLineEdit, QComboBox#appDialogComboBox {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 10px;"
        "    color: %4;"
        "    padding: 8px 12px;"
        "    font-size: 13px;"
        "}"
        "QComboBox#appDialogComboBox::drop-down {"
        "    border: none;"
        "    width: 24px;"
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

void AppInputDialog::onOk()
{
    if (m_type == TextInput && m_lineEdit) {
        m_result = m_lineEdit->text();
    } else if (m_comboBox) {
        m_result = (m_comboBox->currentIndex() >= 0) ? m_comboBox->currentText() : QString();
    }
    m_accepted = true;
    accept();
}

QString AppInputDialog::getText(QWidget *parent, const QString &title, const QString &label,
                                QLineEdit::EchoMode echo, const QString &text, bool *ok)
{
    AppInputDialog dlg(parent, TextInput, title, label);
    dlg.m_lineEdit->setEchoMode(echo);
    dlg.m_lineEdit->setText(text);
    dlg.m_lineEdit->selectAll();

    const bool accepted = (dlg.exec() == Accepted);
    if (ok)
        *ok = accepted;
    return accepted ? dlg.m_result : QString();
}

QString AppInputDialog::getItem(QWidget *parent, const QString &title, const QString &label,
                                const QStringList &items, int current, bool editable, bool *ok)
{
    AppInputDialog dlg(parent, ItemInput, title, label);
    dlg.m_comboBox->addItems(items);
    if (current >= 0 && current < items.size())
        dlg.m_comboBox->setCurrentIndex(current);
    dlg.m_comboBox->setEditable(editable);

    const bool accepted = (dlg.exec() == Accepted);
    if (ok)
        *ok = accepted;
    return accepted ? dlg.m_result : QString();
}