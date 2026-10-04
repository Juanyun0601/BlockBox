/**
 * @file   ColorPickerPage.cpp
 * @brief  颜色选择页面实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "ColorPickerPage.h"

#include <QDebug>
#include <QGroupBox>
#include <QIntValidator>
#include <QMouseEvent>
#include <QPainter>
#include <QTabWidget>

ColorPickerPage::ColorPickerPage(QWidget *parent, const QColor &initialColor)
    : QDialog(parent), 
      m_currentColor(initialColor),
      m_initialColor(initialColor)
{
    setWindowTitle(tr("颜色选择器"));
    setMinimumSize(500, 400);
    setModal(true);
    
    initUI();
    updateColorDisplay();
}

QColor ColorPickerPage::selectedColor() const
{
    return m_currentColor;
}

void ColorPickerPage::setInitialColor(const QColor &color)
{
    m_initialColor = color;
    m_currentColor = color;
    updateColorDisplay();
}

void ColorPickerPage::initUI()
{
    // 主布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // 颜色预览区域
    QHBoxLayout *previewLayout = new QHBoxLayout();
    m_colorPreview = new QLabel(this);
    m_colorPreview->setFixedSize(100, 100);
    m_colorPreview->setStyleSheet("border: 2px solid #ddd; border-radius: 12px;");
    
    m_colorCodeLabel = new QLabel(this);
    m_colorCodeLabel->setObjectName("colorCodeLabel");
    m_colorCodeLabel->setFont(QFont("Consolas", 12));
    m_colorCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    
    previewLayout->addWidget(m_colorPreview);
    previewLayout->addWidget(m_colorCodeLabel);
    previewLayout->addStretch();
    
    // 颜色模式选择
    QTabWidget *colorModeTab = new QTabWidget(this);
    colorModeTab->setObjectName("colorModeTab");
    
    // RGB模式页面
    QWidget *rgbPage = new QWidget();
    QVBoxLayout *rgbLayout = new QVBoxLayout(rgbPage);
    
    // 红色通道
    QHBoxLayout *redLayout = new QHBoxLayout();
    QLabel *redLabel = new QLabel(tr("红色:"), this);
    redLabel->setObjectName("colorLabel");
    m_redSlider = new QSlider(Qt::Horizontal, this);
    m_redSlider->setObjectName("colorSlider");
    m_redSlider->setRange(0, 255);
    m_redEdit = new QLineEdit(this);
    m_redEdit->setObjectName("colorEdit");
    m_redEdit->setFixedWidth(50);
    m_redEdit->setValidator(new QIntValidator(0, 255, this));
    
    redLayout->addWidget(redLabel);
    redLayout->addWidget(m_redSlider);
    redLayout->addWidget(m_redEdit);
    
    // 绿色通道
    QHBoxLayout *greenLayout = new QHBoxLayout();
    QLabel *greenLabel = new QLabel(tr("绿色:"), this);
    greenLabel->setObjectName("colorLabel");
    m_greenSlider = new QSlider(Qt::Horizontal, this);
    m_greenSlider->setObjectName("colorSlider");
    m_greenSlider->setRange(0, 255);
    m_greenEdit = new QLineEdit(this);
    m_greenEdit->setObjectName("colorEdit");
    m_greenEdit->setFixedWidth(50);
    m_greenEdit->setValidator(new QIntValidator(0, 255, this));
    
    greenLayout->addWidget(greenLabel);
    greenLayout->addWidget(m_greenSlider);
    greenLayout->addWidget(m_greenEdit);
    
    // 蓝色通道
    QHBoxLayout *blueLayout = new QHBoxLayout();
    QLabel *blueLabel = new QLabel(tr("蓝色:"), this);
    blueLabel->setObjectName("colorLabel");
    m_blueSlider = new QSlider(Qt::Horizontal, this);
    m_blueSlider->setObjectName("colorSlider");
    m_blueSlider->setRange(0, 255);
    m_blueEdit = new QLineEdit(this);
    m_blueEdit->setObjectName("colorEdit");
    m_blueEdit->setFixedWidth(50);
    m_blueEdit->setValidator(new QIntValidator(0, 255, this));
    
    blueLayout->addWidget(blueLabel);
    blueLayout->addWidget(m_blueSlider);
    blueLayout->addWidget(m_blueEdit);
    
    rgbLayout->addLayout(redLayout);
    rgbLayout->addLayout(greenLayout);
    rgbLayout->addLayout(blueLayout);
    
    // HSL模式页面
    QWidget *hslPage = new QWidget();
    QVBoxLayout *hslLayout = new QVBoxLayout(hslPage);
    
    // 色相通道
    QHBoxLayout *hueLayout = new QHBoxLayout();
    QLabel *hueLabel = new QLabel(tr("色相:"), this);
    hueLabel->setObjectName("colorLabel");
    m_hueSlider = new QSlider(Qt::Horizontal, this);
    m_hueSlider->setObjectName("colorSlider");
    m_hueSlider->setRange(0, 360);
    m_hueEdit = new QLineEdit(this);
    m_hueEdit->setObjectName("colorEdit");
    m_hueEdit->setFixedWidth(50);
    m_hueEdit->setValidator(new QIntValidator(0, 360, this));
    
    hueLayout->addWidget(hueLabel);
    hueLayout->addWidget(m_hueSlider);
    hueLayout->addWidget(m_hueEdit);
    
    // 饱和度通道
    QHBoxLayout *saturationLayout = new QHBoxLayout();
    QLabel *saturationLabel = new QLabel(tr("饱和度:"), this);
    saturationLabel->setObjectName("colorLabel");
    m_saturationSlider = new QSlider(Qt::Horizontal, this);
    m_saturationSlider->setObjectName("colorSlider");
    m_saturationSlider->setRange(0, 100);
    m_saturationEdit = new QLineEdit(this);
    m_saturationEdit->setObjectName("colorEdit");
    m_saturationEdit->setFixedWidth(50);
    m_saturationEdit->setValidator(new QIntValidator(0, 100, this));
    
    saturationLayout->addWidget(saturationLabel);
    saturationLayout->addWidget(m_saturationSlider);
    saturationLayout->addWidget(m_saturationEdit);
    
    // 亮度通道
    QHBoxLayout *lightnessLayout = new QHBoxLayout();
    QLabel *lightnessLabel = new QLabel(tr("亮度:"), this);
    lightnessLabel->setObjectName("colorLabel");
    m_lightnessSlider = new QSlider(Qt::Horizontal, this);
    m_lightnessSlider->setObjectName("colorSlider");
    m_lightnessSlider->setRange(0, 100);
    m_lightnessEdit = new QLineEdit(this);
    m_lightnessEdit->setObjectName("colorEdit");
    m_lightnessEdit->setFixedWidth(50);
    m_lightnessEdit->setValidator(new QIntValidator(0, 100, this));
    
    lightnessLayout->addWidget(lightnessLabel);
    lightnessLayout->addWidget(m_lightnessSlider);
    lightnessLayout->addWidget(m_lightnessEdit);
    
    hslLayout->addLayout(hueLayout);
    hslLayout->addLayout(saturationLayout);
    hslLayout->addLayout(lightnessLayout);
    
    // 十六进制颜色输入
    QHBoxLayout *hexLayout = new QHBoxLayout();
    QLabel *hexLabel = new QLabel(tr("十六进制:"), this);
    hexLabel->setObjectName("colorLabel");
    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setObjectName("colorHexEdit");
    m_hexEdit->setFixedWidth(100);
    
    hexLayout->addWidget(hexLabel);
    hexLayout->addWidget(m_hexEdit);
    hexLayout->addStretch();
    
    // 颜色预设
    QGroupBox *presetGroup = new QGroupBox(tr("预设颜色"), this);
    presetGroup->setObjectName("presetGroup");
    QGridLayout *presetLayout = new QGridLayout(presetGroup);
    
    // 预设颜色列表
    QStringList presetColors = {
        "#2E7D32", "#2196F3", "#FF9800", "#F44336",
        "#9C27B0", "#00BCD4", "#FFC107", "#795548",
        "#607D8B", "#E91E63", "#3F51B5", "#009688",
        "#FF5722", "#673AB7", "#8BC34A", "#CDDC39"
    };
    
    for (int i = 0; i < 16; i++) {
        m_presetButtons[i] = new QPushButton(this);
        m_presetButtons[i]->setObjectName("presetButton");
        m_presetButtons[i]->setFixedSize(40, 40);
        m_presetButtons[i]->setStyleSheet(QString("background-color: %1; border: 2px solid #ddd; border-radius: 12px;").arg(presetColors[i]));
        m_presetButtons[i]->setToolTip(presetColors[i]);
        m_presetButtons[i]->setProperty("color", presetColors[i]);
        connect(m_presetButtons[i], &QPushButton::clicked, this, &ColorPickerPage::onPresetColorClicked);
        presetLayout->addWidget(m_presetButtons[i], i / 4, i % 4);
    }
    
    // 添加标签页
    colorModeTab->addTab(rgbPage, tr("RGB"));
    colorModeTab->addTab(hslPage, tr("HSL"));
    
    // 按钮框
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->setObjectName("colorButtonBox");
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &ColorPickerPage::onOkClicked);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &ColorPickerPage::onCancelClicked);
    
    // 连接信号槽
    connect(m_redSlider, &QSlider::valueChanged, this, &ColorPickerPage::onRGBChanged);
    connect(m_greenSlider, &QSlider::valueChanged, this, &ColorPickerPage::onRGBChanged);
    connect(m_blueSlider, &QSlider::valueChanged, this, &ColorPickerPage::onRGBChanged);
    connect(m_redEdit, &QLineEdit::textChanged, this, &ColorPickerPage::onRGBChanged);
    connect(m_greenEdit, &QLineEdit::textChanged, this, &ColorPickerPage::onRGBChanged);
    connect(m_blueEdit, &QLineEdit::textChanged, this, &ColorPickerPage::onRGBChanged);
    
    connect(m_hueSlider, &QSlider::valueChanged, this, &ColorPickerPage::onHSLChanged);
    connect(m_saturationSlider, &QSlider::valueChanged, this, &ColorPickerPage::onHSLChanged);
    connect(m_lightnessSlider, &QSlider::valueChanged, this, &ColorPickerPage::onHSLChanged);
    connect(m_hueEdit, &QLineEdit::textChanged, this, &ColorPickerPage::onHSLChanged);
    connect(m_saturationEdit, &QLineEdit::textChanged, this, &ColorPickerPage::onHSLChanged);
    connect(m_lightnessEdit, &QLineEdit::textChanged, this, &ColorPickerPage::onHSLChanged);
    
    connect(m_hexEdit, &QLineEdit::textChanged, this, &ColorPickerPage::onHexChanged);
    
    // 添加到主布局
    mainLayout->addLayout(previewLayout);
    mainLayout->addLayout(hexLayout);
    mainLayout->addWidget(colorModeTab);
    mainLayout->addWidget(presetGroup);
    mainLayout->addWidget(m_buttonBox);
}

void ColorPickerPage::onRGBChanged()
{
    // 从滑块获取RGB值
    int red = m_redSlider->value();
    int green = m_greenSlider->value();
    int blue = m_blueSlider->value();
    
    // 更新颜色
    m_currentColor.setRgb(red, green, blue);
    updateColorDisplay();
    emit colorChanged(m_currentColor);
}

void ColorPickerPage::onHSLChanged()
{
    // 从滑块获取HSL值
    int hue = m_hueSlider->value();
    int saturation = m_saturationSlider->value();
    int lightness = m_lightnessSlider->value();
    
    // 更新颜色
    m_currentColor.setHsv(hue, saturation, lightness);
    updateColorDisplay();
    emit colorChanged(m_currentColor);
}

void ColorPickerPage::onHexChanged()
{
    // 从输入框获取十六进制颜色值
    QString hex = m_hexEdit->text();
    if (hex.startsWith('#')) {
        m_currentColor = QColor::fromString(hex);
        updateColorDisplay();
        emit colorChanged(m_currentColor);
    }
}

void ColorPickerPage::onPresetColorClicked()
{
    // 获取预设颜色
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (button) {
        QString colorStr = button->property("color").toString();
        m_currentColor = QColor::fromString(colorStr);
        updateColorDisplay();
        emit colorChanged(m_currentColor);
    }
}

void ColorPickerPage::onOkClicked()
{
    accept();
}

void ColorPickerPage::onCancelClicked()
{
    m_currentColor = m_initialColor;
    reject();
}

void ColorPickerPage::updateColorDisplay()
{
    // 更新颜色预览
    m_colorPreview->setStyleSheet(QString("background-color: %1; border: 2px solid #ddd; border-radius: 12px;")
                                 .arg(m_currentColor.name()));
    
    // 更新颜色代码标签
    m_colorCodeLabel->setText(m_currentColor.name());
    
    // 更新RGB控件
    updateRGBControls();
    
    // 更新HSL控件
    updateHSLControls();
    
    // 更新十六进制控件
    updateHexControl();
}

void ColorPickerPage::updateRGBControls()
{
    // 更新RGB滑块和输入框
    m_redSlider->blockSignals(true);
    m_greenSlider->blockSignals(true);
    m_blueSlider->blockSignals(true);
    m_redEdit->blockSignals(true);
    m_greenEdit->blockSignals(true);
    m_blueEdit->blockSignals(true);
    
    m_redSlider->setValue(m_currentColor.red());
    m_greenSlider->setValue(m_currentColor.green());
    m_blueSlider->setValue(m_currentColor.blue());
    m_redEdit->setText(QString::number(m_currentColor.red()));
    m_greenEdit->setText(QString::number(m_currentColor.green()));
    m_blueEdit->setText(QString::number(m_currentColor.blue()));
    
    m_redSlider->blockSignals(false);
    m_greenSlider->blockSignals(false);
    m_blueSlider->blockSignals(false);
    m_redEdit->blockSignals(false);
    m_greenEdit->blockSignals(false);
    m_blueEdit->blockSignals(false);
}

void ColorPickerPage::updateHSLControls()
{
    // 更新HSL滑块和输入框
    m_hueSlider->blockSignals(true);
    m_saturationSlider->blockSignals(true);
    m_lightnessSlider->blockSignals(true);
    m_hueEdit->blockSignals(true);
    m_saturationEdit->blockSignals(true);
    m_lightnessEdit->blockSignals(true);
    
    m_hueSlider->setValue(m_currentColor.hue());
    m_saturationSlider->setValue(m_currentColor.saturation());
    m_lightnessSlider->setValue(m_currentColor.lightness());
    m_hueEdit->setText(QString::number(m_currentColor.hue()));
    m_saturationEdit->setText(QString::number(m_currentColor.saturation()));
    m_lightnessEdit->setText(QString::number(m_currentColor.lightness()));
    
    m_hueSlider->blockSignals(false);
    m_saturationSlider->blockSignals(false);
    m_lightnessSlider->blockSignals(false);
    m_hueEdit->blockSignals(false);
    m_saturationEdit->blockSignals(false);
    m_lightnessEdit->blockSignals(false);
}

void ColorPickerPage::updateHexControl()
{
    // 更新十六进制输入框
    m_hexEdit->blockSignals(true);
    m_hexEdit->setText(m_currentColor.name());
    m_hexEdit->blockSignals(false);
}

// ============================================================================
// ColorPickerWidget 实现
// ============================================================================

ColorPickerWidget::ColorPickerWidget(QWidget *parent)
    : QWidget(parent), m_currentColor(QColor("#2E7D32"))
{
    initUI();
    updateColorDisplay();
}

QColor ColorPickerWidget::currentColor() const
{
    return m_currentColor;
}

void ColorPickerWidget::setCurrentColor(const QColor &color)
{
    if (color.isValid() && m_currentColor != color)
    {
        m_currentColor = color;
        updateColorDisplay();
    }
}

void ColorPickerWidget::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // 颜色预览区域
    QHBoxLayout *previewLayout = new QHBoxLayout();
    m_colorPreview = new QLabel(this);
    m_colorPreview->setFixedSize(60, 60);
    m_colorPreview->setStyleSheet("border: 2px solid #ddd; border-radius: 10px;");

    m_colorCodeLabel = new QLabel(this);
    m_colorCodeLabel->setObjectName("colorCodeLabel");
    m_colorCodeLabel->setFont(QFont("Consolas", 11));
    m_colorCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    previewLayout->addWidget(m_colorPreview);
    previewLayout->addWidget(m_colorCodeLabel);
    previewLayout->addStretch();

    // 十六进制颜色输入
    QHBoxLayout *hexLayout = new QHBoxLayout();
    QLabel *hexLabel = new QLabel(tr("十六进制:"), this);
    hexLabel->setObjectName("colorLabel");
    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setObjectName("colorHexEdit");
    m_hexEdit->setFixedWidth(100);

    hexLayout->addWidget(hexLabel);
    hexLayout->addWidget(m_hexEdit);
    hexLayout->addStretch();

    // 颜色模式选择
    QTabWidget *colorModeTab = new QTabWidget(this);
    colorModeTab->setObjectName("colorModeTab");

    // RGB模式页面
    QWidget *rgbPage = new QWidget();
    QVBoxLayout *rgbLayout = new QVBoxLayout(rgbPage);
    rgbLayout->setContentsMargins(8, 8, 8, 8);

    auto createRGBRow = [&](const QString &labelText, QSlider *&slider, QLineEdit *&edit,
                             int maxVal, QVBoxLayout *layout) {
        QHBoxLayout *row = new QHBoxLayout();
        QLabel *label = new QLabel(labelText, this);
        label->setObjectName("colorLabel");
        label->setFixedWidth(40);
        slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName("colorSlider");
        slider->setRange(0, maxVal);
        edit = new QLineEdit(this);
        edit->setObjectName("colorEdit");
        edit->setFixedWidth(50);
        edit->setValidator(new QIntValidator(0, maxVal, this));
        row->addWidget(label);
        row->addWidget(slider);
        row->addWidget(edit);
        layout->addLayout(row);
    };

    createRGBRow(tr("红色:"), m_redSlider, m_redEdit, 255, rgbLayout);
    createRGBRow(tr("绿色:"), m_greenSlider, m_greenEdit, 255, rgbLayout);
    createRGBRow(tr("蓝色:"), m_blueSlider, m_blueEdit, 255, rgbLayout);

    // HSL模式页面
    QWidget *hslPage = new QWidget();
    QVBoxLayout *hslLayout = new QVBoxLayout(hslPage);
    hslLayout->setContentsMargins(8, 8, 8, 8);

    auto createHSLRow = [&](const QString &labelText, QSlider *&slider, QLineEdit *&edit,
                             int maxVal, QVBoxLayout *layout) {
        QHBoxLayout *row = new QHBoxLayout();
        QLabel *label = new QLabel(labelText, this);
        label->setObjectName("colorLabel");
        label->setFixedWidth(40);
        slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName("colorSlider");
        slider->setRange(0, maxVal);
        edit = new QLineEdit(this);
        edit->setObjectName("colorEdit");
        edit->setFixedWidth(50);
        edit->setValidator(new QIntValidator(0, maxVal, this));
        row->addWidget(label);
        row->addWidget(slider);
        row->addWidget(edit);
        layout->addLayout(row);
    };

    createHSLRow(tr("色相:"), m_hueSlider, m_hueEdit, 360, hslLayout);
    createHSLRow(tr("饱和度:"), m_saturationSlider, m_saturationEdit, 100, hslLayout);
    createHSLRow(tr("亮度:"), m_lightnessSlider, m_lightnessEdit, 100, hslLayout);

    colorModeTab->addTab(rgbPage, tr("RGB"));
    colorModeTab->addTab(hslPage, tr("HSL"));

    // 颜色预设
    QGroupBox *presetGroup = new QGroupBox(tr("预设颜色"), this);
    presetGroup->setObjectName("presetGroup");
    QGridLayout *presetLayout = new QGridLayout(presetGroup);

    QStringList presetColors = {
        "#2E7D32", "#2196F3", "#FF9800", "#F44336",
        "#9C27B0", "#00BCD4", "#FFC107", "#795548",
        "#607D8B", "#E91E63", "#3F51B5", "#009688",
        "#FF5722", "#673AB7", "#8BC34A", "#CDDC39"
    };

    for (int i = 0; i < 16; i++)
    {
        m_presetButtons[i] = new QPushButton(this);
        m_presetButtons[i]->setObjectName("presetButton");
        m_presetButtons[i]->setFixedSize(32, 32);
        m_presetButtons[i]->setStyleSheet(QString("background-color: %1; border: 2px solid #ddd; border-radius: 8px;")
                                         .arg(presetColors[i]));
        m_presetButtons[i]->setToolTip(presetColors[i]);
        m_presetButtons[i]->setProperty("color", presetColors[i]);
        connect(m_presetButtons[i], &QPushButton::clicked, this, &ColorPickerWidget::onPresetColorClicked);
        presetLayout->addWidget(m_presetButtons[i], i / 4, i % 4);
    }

    // 连接信号槽
    connect(m_redSlider, &QSlider::valueChanged, this, &ColorPickerWidget::onRGBChanged);
    connect(m_greenSlider, &QSlider::valueChanged, this, &ColorPickerWidget::onRGBChanged);
    connect(m_blueSlider, &QSlider::valueChanged, this, &ColorPickerWidget::onRGBChanged);
    connect(m_redEdit, &QLineEdit::textChanged, this, &ColorPickerWidget::onRGBChanged);
    connect(m_greenEdit, &QLineEdit::textChanged, this, &ColorPickerWidget::onRGBChanged);
    connect(m_blueEdit, &QLineEdit::textChanged, this, &ColorPickerWidget::onRGBChanged);

    connect(m_hueSlider, &QSlider::valueChanged, this, &ColorPickerWidget::onHSLChanged);
    connect(m_saturationSlider, &QSlider::valueChanged, this, &ColorPickerWidget::onHSLChanged);
    connect(m_lightnessSlider, &QSlider::valueChanged, this, &ColorPickerWidget::onHSLChanged);
    connect(m_hueEdit, &QLineEdit::textChanged, this, &ColorPickerWidget::onHSLChanged);
    connect(m_saturationEdit, &QLineEdit::textChanged, this, &ColorPickerWidget::onHSLChanged);
    connect(m_lightnessEdit, &QLineEdit::textChanged, this, &ColorPickerWidget::onHSLChanged);

    connect(m_hexEdit, &QLineEdit::textChanged, this, &ColorPickerWidget::onHexChanged);

    // 添加到主布局
    mainLayout->addLayout(previewLayout);
    mainLayout->addLayout(hexLayout);
    mainLayout->addWidget(colorModeTab);
    mainLayout->addWidget(presetGroup);
}

void ColorPickerWidget::onRGBChanged()
{
    int red = m_redSlider->value();
    int green = m_greenSlider->value();
    int blue = m_blueSlider->value();

    m_currentColor.setRgb(red, green, blue);
    updateColorDisplay();
    emit colorChanged(m_currentColor);
}

void ColorPickerWidget::onHSLChanged()
{
    int hue = m_hueSlider->value();
    int saturation = m_saturationSlider->value();
    int lightness = m_lightnessSlider->value();

    m_currentColor.setHsv(hue, saturation, lightness);
    updateColorDisplay();
    emit colorChanged(m_currentColor);
}

void ColorPickerWidget::onHexChanged()
{
    QString hex = m_hexEdit->text();
    if (hex.startsWith('#'))
    {
        QColor color = QColor::fromString(hex);
        if (color.isValid())
        {
            m_currentColor = color;
            updateColorDisplay();
            emit colorChanged(m_currentColor);
        }
    }
}

void ColorPickerWidget::onPresetColorClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (button)
    {
        QString colorStr = button->property("color").toString();
        m_currentColor = QColor::fromString(colorStr);
        updateColorDisplay();
        emit colorChanged(m_currentColor);
    }
}

void ColorPickerWidget::updateColorDisplay()
{
    m_colorPreview->setStyleSheet(QString("background-color: %1; border: 2px solid #ddd; border-radius: 10px;")
                                 .arg(m_currentColor.name()));
    m_colorCodeLabel->setText(m_currentColor.name());
    updateRGBControls();
    updateHSLControls();
    updateHexControl();
}

void ColorPickerWidget::updateRGBControls()
{
    m_redSlider->blockSignals(true);
    m_greenSlider->blockSignals(true);
    m_blueSlider->blockSignals(true);
    m_redEdit->blockSignals(true);
    m_greenEdit->blockSignals(true);
    m_blueEdit->blockSignals(true);

    m_redSlider->setValue(m_currentColor.red());
    m_greenSlider->setValue(m_currentColor.green());
    m_blueSlider->setValue(m_currentColor.blue());
    m_redEdit->setText(QString::number(m_currentColor.red()));
    m_greenEdit->setText(QString::number(m_currentColor.green()));
    m_blueEdit->setText(QString::number(m_currentColor.blue()));

    m_redSlider->blockSignals(false);
    m_greenSlider->blockSignals(false);
    m_blueSlider->blockSignals(false);
    m_redEdit->blockSignals(false);
    m_greenEdit->blockSignals(false);
    m_blueEdit->blockSignals(false);
}

void ColorPickerWidget::updateHSLControls()
{
    m_hueSlider->blockSignals(true);
    m_saturationSlider->blockSignals(true);
    m_lightnessSlider->blockSignals(true);
    m_hueEdit->blockSignals(true);
    m_saturationEdit->blockSignals(true);
    m_lightnessEdit->blockSignals(true);

    m_hueSlider->setValue(m_currentColor.hue());
    m_saturationSlider->setValue(m_currentColor.saturation());
    m_lightnessSlider->setValue(m_currentColor.lightness());
    m_hueEdit->setText(QString::number(m_currentColor.hue()));
    m_saturationEdit->setText(QString::number(m_currentColor.saturation()));
    m_lightnessEdit->setText(QString::number(m_currentColor.lightness()));

    m_hueSlider->blockSignals(false);
    m_saturationSlider->blockSignals(false);
    m_lightnessSlider->blockSignals(false);
    m_hueEdit->blockSignals(false);
    m_saturationEdit->blockSignals(false);
    m_lightnessEdit->blockSignals(false);
}

void ColorPickerWidget::updateHexControl()
{
    m_hexEdit->blockSignals(true);
    m_hexEdit->setText(m_currentColor.name());
    m_hexEdit->blockSignals(false);
}
