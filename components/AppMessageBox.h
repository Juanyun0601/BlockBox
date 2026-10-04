#ifndef APPMESSAGEBOX_H
#define APPMESSAGEBOX_H

#include "AppDialogBase.h"

#include <QFlags>
#include <QMap>
#include <QPixmap>
#include <QPushButton>
#include <QString>

class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

/**
 * @brief 软件内嵌的居中弹窗，替代原生 QMessageBox
 *
 * 显示为覆盖整个主窗口的半透明模糊背景 + 居中圆角卡片，不再是系统对话框。
 * API 与 QMessageBox 兼容，可平滑替换 QMessageBox::information / warning / question / critical。
 */
class AppMessageBox : public AppDialogBase
{
    Q_OBJECT

public:
    enum Icon {
        NoIcon = 0,
        Question = 1,
        Information = 2,
        Warning = 3,
        Critical = 4
    };

    enum StandardButton {
        NoButton = 0x00000000,
        Ok = 0x00000400,
        Save = 0x00000800,
        SaveAll = 0x00001000,
        Open = 0x00002000,
        Yes = 0x00004000,
        YesToAll = 0x00008000,
        No = 0x00010000,
        NoToAll = 0x00020000,
        Abort = 0x00040000,
        Retry = 0x00080000,
        Ignore = 0x00100000,
        Close = 0x00200000,
        Cancel = 0x00400000,
        Discard = 0x00800000,
        Help = 0x01000000,
        Apply = 0x02000000,
        Reset = 0x04000000,
        RestoreDefaults = 0x08000000
    };
    Q_DECLARE_FLAGS(StandardButtons, StandardButton)
    Q_FLAG(StandardButtons)

    enum ButtonRole {
        InvalidRole = -1,
        AcceptRole = 0,
        RejectRole = 1,
        DestructiveRole = 2,
        ActionRole = 3,
        HelpRole = 4,
        YesRole = 5,
        NoRole = 6,
        ResetRole = 7,
        ApplyRole = 8
    };

    explicit AppMessageBox(QWidget *parent = nullptr);
    ~AppMessageBox() override;

    void setText(const QString &text);
    QString text() const;
    void setInformativeText(const QString &text);
    void setIcon(Icon icon);
    void setIconPixmap(const QPixmap &pixmap);
    void setStandardButtons(StandardButtons buttons);
    void setDefaultButton(StandardButton button);
    void setDefaultButton(QPushButton *button);
    QPushButton *addButton(const QString &text, ButtonRole role);
    QPushButton *button(StandardButton which) const;
    QPushButton *clickedButton() const;
    void setButtonText(StandardButton which, const QString &text);

    static StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                   StandardButtons buttons = StandardButtons(Yes | No),
                                   StandardButton defaultButton = NoButton);
    static void information(QWidget *parent, const QString &title, const QString &text,
                            StandardButtons buttons = Ok,
                            StandardButton defaultButton = NoButton);
    static void warning(QWidget *parent, const QString &title, const QString &text,
                        StandardButtons buttons = Ok,
                        StandardButton defaultButton = NoButton);
    static void critical(QWidget *parent, const QString &title, const QString &text,
                         StandardButtons buttons = Ok,
                         StandardButton defaultButton = NoButton);

    void setWindowTitle(const QString &title);
    int exec() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onButtonClicked();

private:
    void initUI();
    void initStyle();
    void buildButtons();
    void captureBlurBackground();
    void positionOverWindow();
    void refreshTitleLabel();
    QPushButton *createStandardButton(StandardButton button);
    QString standardButtonText(StandardButton button) const;
    ButtonRole standardButtonRole(StandardButton button) const;
    QPixmap makeIconPixmap(Icon icon) const;
    void closeWithButton(QPushButton *button);

    QWidget *m_card;
    QLabel *m_iconLabel;
    QLabel *m_titleLabel;
    QLabel *m_textLabel;
    QLabel *m_informativeLabel;
    QWidget *m_buttonRow;
    QHBoxLayout *m_buttonLayout;

    QPixmap m_customPixmap;
    QMap<QPushButton *, StandardButton> m_buttonMap;
    QMap<StandardButton, QPushButton *> m_buttonByStandard;
    QPushButton *m_defaultButton;
    QPushButton *m_clickedButton;
    StandardButton m_resultButton;

    QString m_text;
    QString m_informativeText;
    Icon m_icon;
    StandardButtons m_buttons;
    bool m_hasCustomButtons;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(AppMessageBox::StandardButtons)

#endif // APPMESSAGEBOX_H
