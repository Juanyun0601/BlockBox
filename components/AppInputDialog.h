#ifndef APPINPUTDIALOG_H
#define APPINPUTDIALOG_H

#include "AppDialogBase.h"

#include <QLineEdit>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QPushButton;

/**
 * @brief 软件内嵌输入弹窗，替代原生 QInputDialog（居中圆角模糊卡片）
 */
class AppInputDialog : public AppDialogBase
{
    Q_OBJECT

public:
    enum InputType {
        TextInput,
        ItemInput
    };

    static QString getText(QWidget *parent, const QString &title, const QString &label,
                           QLineEdit::EchoMode echo = QLineEdit::Normal,
                           const QString &text = QString(), bool *ok = nullptr);

    static QString getItem(QWidget *parent, const QString &title, const QString &label,
                           const QStringList &items, int current = 0, bool editable = true,
                           bool *ok = nullptr);

private:
    AppInputDialog(QWidget *parent, InputType type, const QString &title, const QString &label);

    void initUI();
    void initStyle();
    void onOk();

    InputType m_type;
    QString m_label;
    QString m_result;
    bool m_accepted;

    QLineEdit *m_lineEdit;
    QComboBox *m_comboBox;
    QPushButton *m_okBtn;
    QPushButton *m_cancelBtn;
};

#endif // APPINPUTDIALOG_H