/**
 * @file   ColorPickerPage.h
 * @brief  颜色选择页面类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef COLORPICKERPAGE_H
#define COLORPICKERPAGE_H

#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

/**
 * @class ColorPickerPage
 * @brief 自定义调色对话框，用于选择主题色
 *
 * 提供RGB、HSL颜色模式选择，支持颜色预览和预设颜色
 */
class ColorPickerPage : public QDialog
{
    Q_OBJECT

public:
    explicit ColorPickerPage(QWidget *parent = nullptr, const QColor &initialColor = QColor("#2E7D32"));

    QColor selectedColor() const;
    void setInitialColor(const QColor &color);

signals:
    void colorChanged(const QColor &color);

private slots:
    void onRGBChanged();
    void onHSLChanged();
    void onHexChanged();
    void onPresetColorClicked();
    void onOkClicked();
    void onCancelClicked();

private:
    void initUI();
    void updateColorDisplay();
    void updateRGBControls();
    void updateHSLControls();
    void updateHexControl();

    QColor m_currentColor;
    QColor m_initialColor;

    QLabel *m_colorPreview;
    QLabel *m_colorCodeLabel;

    QSlider *m_redSlider;
    QSlider *m_greenSlider;
    QSlider *m_blueSlider;
    QLineEdit *m_redEdit;
    QLineEdit *m_greenEdit;
    QLineEdit *m_blueEdit;

    QSlider *m_hueSlider;
    QSlider *m_saturationSlider;
    QSlider *m_lightnessSlider;
    QLineEdit *m_hueEdit;
    QLineEdit *m_saturationEdit;
    QLineEdit *m_lightnessEdit;

    QLineEdit *m_hexEdit;

    QPushButton *m_presetButtons[16];

    QDialogButtonBox *m_buttonBox;
};

/**
 * @class ColorPickerWidget
 * @brief 可嵌入的颜色选择器组件，用于设置页面中
 *
 * 继承自 QWidget，可直接嵌入到任何父容器中。提供 RGB/HSL/Hex
 * 多模式调色和 16 色预设，颜色变更实时通过信号通知。
 */
class ColorPickerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ColorPickerWidget(QWidget *parent = nullptr);

    QColor currentColor() const;
    void setCurrentColor(const QColor &color);

signals:
    void colorChanged(const QColor &color);

private slots:
    void onRGBChanged();
    void onHSLChanged();
    void onHexChanged();
    void onPresetColorClicked();

private:
    void initUI();
    void updateColorDisplay();
    void updateRGBControls();
    void updateHSLControls();
    void updateHexControl();

    QColor m_currentColor;

    QLabel *m_colorPreview;
    QLabel *m_colorCodeLabel;

    QSlider *m_redSlider;
    QSlider *m_greenSlider;
    QSlider *m_blueSlider;
    QLineEdit *m_redEdit;
    QLineEdit *m_greenEdit;
    QLineEdit *m_blueEdit;

    QSlider *m_hueSlider;
    QSlider *m_saturationSlider;
    QSlider *m_lightnessSlider;
    QLineEdit *m_hueEdit;
    QLineEdit *m_saturationEdit;
    QLineEdit *m_lightnessEdit;

    QLineEdit *m_hexEdit;

    QPushButton *m_presetButtons[16];
};

#endif // COLORPICKERPAGE_H
