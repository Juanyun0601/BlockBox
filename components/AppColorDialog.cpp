#include "AppColorDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

AppColorDialog::AppColorDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_color(Qt::white)
    , m_previewLabel(nullptr)
    , m_hueSlider(nullptr)
    , m_satSlider(nullptr)
    , m_valSlider(nullptr)
    , m_redSpin(nullptr)
    , m_greenSpin(nullptr)
    , m_blueSpin(nullptr)
    , m_hexEdit(nullptr)
    , m_okBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_syncing(false)
{
    setObjectName(QStringLiteral("appColorDialog"));
    initUI();
    initStyle();
}

void AppColorDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumWidth(420);
    card->setMaximumWidth(520);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(26, 22, 26, 20);
    cardLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel(tr("选择颜色"), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));

    m_previewLabel = new QLabel(card);
    m_previewLabel->setObjectName(QStringLiteral("appColorPreview"));
    m_previewLabel->setFixedHeight(44);

    auto addSliderRow = [this, card, cardLayout](const QString &text, QSlider *&slider) {
        QWidget *row = new QWidget(card);
        QHBoxLayout *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(10);
        QLabel *lab = new QLabel(text, row);
        lab->setObjectName(QStringLiteral("appDialogPromptLabel"));
        lab->setFixedWidth(28);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setObjectName(QStringLiteral("appColorSlider"));
        slider->setRange(0, 255);
        hl->addWidget(lab);
        hl->addWidget(slider, 1);
        cardLayout->addWidget(row);
    };

    addSliderRow(tr("色相"), m_hueSlider);
    addSliderRow(tr("饱和"), m_satSlider);
    addSliderRow(tr("亮度"), m_valSlider);

    QWidget *rgbRow = new QWidget(card);
    QHBoxLayout *rgbLayout = new QHBoxLayout(rgbRow);
    rgbLayout->setContentsMargins(0, 0, 0, 0);
    rgbLayout->setSpacing(8);

    m_redSpin = new QSpinBox(rgbRow);
    m_redSpin->setObjectName(QStringLiteral("appDialogSpin"));
    m_greenSpin = new QSpinBox(rgbRow);
    m_greenSpin->setObjectName(QStringLiteral("appDialogSpin"));
    m_blueSpin = new QSpinBox(rgbRow);
    m_blueSpin->setObjectName(QStringLiteral("appDialogSpin"));
    m_redSpin->setRange(0, 255);
    m_greenSpin->setRange(0, 255);
    m_blueSpin->setRange(0, 255);

    m_hexEdit = new QLineEdit(rgbRow);
    m_hexEdit->setObjectName(QStringLiteral("appDialogLineEdit"));
    m_hexEdit->setPlaceholderText(QStringLiteral("#RRGGBB"));
    m_hexEdit->setMaximumWidth(120);

    rgbLayout->addWidget(m_redSpin);
    rgbLayout->addWidget(m_greenSpin);
    rgbLayout->addWidget(m_blueSpin);
    rgbLayout->addWidget(m_hexEdit, 1);

    cardLayout->addWidget(m_previewLabel);
    cardLayout->addWidget(rgbRow);

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

    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_hueSlider, &QSlider::valueChanged, this, &AppColorDialog::syncFromHsv);
    connect(m_satSlider, &QSlider::valueChanged, this, &AppColorDialog::syncFromHsv);
    connect(m_valSlider, &QSlider::valueChanged, this, &AppColorDialog::syncFromHsv);

    connect(m_redSpin, &QSpinBox::valueChanged, this, &AppColorDialog::syncFromRgb);
    connect(m_greenSpin, &QSpinBox::valueChanged, this, &AppColorDialog::syncFromRgb);
    connect(m_blueSpin, &QSpinBox::valueChanged, this, &AppColorDialog::syncFromRgb);

    connect(m_hexEdit, &QLineEdit::editingFinished, this, &AppColorDialog::syncFromHex);
    connect(m_hexEdit, &QLineEdit::returnPressed, this, &AppColorDialog::syncFromHex);
}

void AppColorDialog::initStyle()
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
        "QLabel#appColorPreview {"
        "    background-color: #ffffff;"
        "    border: 1px solid %8;"
        "    border-radius: 10px;"
        "}"
        "QSlider#appColorSlider::groove:horizontal {"
        "    height: 8px;"
        "    background: transparent;"
        "    border: none;"
        "}"
        "QSlider#appColorSlider::handle:horizontal {"
        "    width: 16px;"
        "    margin: -4px 0;"
        "    border-radius: 8px;"
        "    background: %6;"
        "}"
        "QLineEdit#appDialogLineEdit, QSpinBox#appDialogSpin {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 10px;"
        "    color: %4;"
        "    padding: 6px 10px;"
        "    font-size: 13px;"
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

void AppColorDialog::setCurrentColor(const QColor &color)
{
    m_syncing = true;
    m_color = color.isValid() ? color : Qt::white;

    m_hueSlider->setValue(qRound(m_color.hueF() * 255.0));
    m_satSlider->setValue(qRound(m_color.saturationF() * 255.0));
    m_valSlider->setValue(qRound(m_color.valueF() * 255.0));
    m_redSpin->setValue(m_color.red());
    m_greenSpin->setValue(m_color.green());
    m_blueSpin->setValue(m_color.blue());
    m_hexEdit->setText(m_color.name().toUpper());
    m_syncing = false;

    updatePreview();
}

void AppColorDialog::syncFromHsv()
{
    if (m_syncing)
        return;
    m_syncing = true;
    const int h = m_hueSlider->value();
    const int s = m_satSlider->value();
    const int v = m_valSlider->value();
    m_color = QColor::fromHsv(h * 360 / 255, s * 255 / 255, v * 255 / 255);
    m_color.setHsv(h * 360 / 255, s, v);
    m_redSpin->setValue(m_color.red());
    m_greenSpin->setValue(m_color.green());
    m_blueSpin->setValue(m_color.blue());
    m_hexEdit->setText(m_color.name().toUpper());
    m_syncing = false;
    updatePreview();
}

void AppColorDialog::syncFromRgb()
{
    if (m_syncing)
        return;
    m_syncing = true;
    m_color.setRgb(m_redSpin->value(), m_greenSpin->value(), m_blueSpin->value());
    m_hueSlider->setValue(qRound(m_color.hueF() * 255.0));
    m_satSlider->setValue(qRound(m_color.saturationF() * 255.0));
    m_valSlider->setValue(qRound(m_color.valueF() * 255.0));
    m_hexEdit->setText(m_color.name().toUpper());
    m_syncing = false;
    updatePreview();
}

void AppColorDialog::syncFromHex()
{
    if (m_syncing)
        return;
    QString text = m_hexEdit->text().trimmed();
    if (text.startsWith('#'))
        text = text.mid(1);
    if (text.length() == 6) {
        bool ok = false;
        const QColor c(text);
        if (c.isValid()) {
            m_syncing = true;
            m_color = c;
            m_redSpin->setValue(c.red());
            m_greenSpin->setValue(c.green());
            m_blueSpin->setValue(c.blue());
            m_hueSlider->setValue(qRound(c.hueF() * 255.0));
            m_satSlider->setValue(qRound(c.saturationF() * 255.0));
            m_valSlider->setValue(qRound(c.valueF() * 255.0));
            m_hexEdit->setText(c.name().toUpper());
            m_syncing = false;
            updatePreview();
            Q_UNUSED(ok);
        }
    }
}

void AppColorDialog::updatePreview()
{
    m_previewLabel->setStyleSheet(
        QStringLiteral("QLabel#appColorPreview { background-color: %1; border: 1px solid rgba(0,0,0,60); border-radius: 10px; }")
        .arg(m_color.name()));
}

QColor AppColorDialog::getColor(const QColor &initial, QWidget *parent, const QString &title)
{
    AppColorDialog dlg(parent);
    if (!title.isEmpty())
        dlg.setWindowTitle(title);
    dlg.setCurrentColor(initial.isValid() ? initial : Qt::white);
    if (dlg.exec() == Accepted)
        return dlg.m_color;
    return QColor();
}