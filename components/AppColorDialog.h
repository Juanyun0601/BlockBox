#ifndef APPCOLORDIALOG_H
#define APPCOLORDIALOG_H

#include "AppDialogBase.h"

#include <QColor>

class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

/**
 * @brief 软件内嵌颜色选择弹窗，替代原生 QColorDialog（居中圆角模糊卡片）
 */
class AppColorDialog : public AppDialogBase
{
    Q_OBJECT

public:
    static QColor getColor(const QColor &initial = Qt::white, QWidget *parent = nullptr,
                           const QString &title = QString());

private:
    explicit AppColorDialog(QWidget *parent = nullptr);

    void initUI();
    void initStyle();
    void setCurrentColor(const QColor &color);
    void syncFromHsv();
    void syncFromRgb();
    void syncFromHex();
    void updatePreview();

    QColor m_color;

    QLabel *m_previewLabel;
    QSlider *m_hueSlider;
    QSlider *m_satSlider;
    QSlider *m_valSlider;
    QSpinBox *m_redSpin;
    QSpinBox *m_greenSpin;
    QSpinBox *m_blueSpin;
    QLineEdit *m_hexEdit;
    QPushButton *m_okBtn;
    QPushButton *m_cancelBtn;

    bool m_syncing;
};

#endif // APPCOLORDIALOG_H