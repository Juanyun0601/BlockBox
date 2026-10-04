/**
 * @file   FavoriteFolderDialog.h
 * @brief  新建/重命名收藏夹对话框
 * @author BlockBox Team
 * @date   2026-07-18
 */
#ifndef FAVORITEFOLDERDIALOG_H
#define FAVORITEFOLDERDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>

class QWidget;

class FavoriteFolderDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode
    {
        CreateMode,
        RenameMode
    };

    explicit FavoriteFolderDialog(Mode mode, QWidget *parent = nullptr);

    void setInitialName(const QString &name);
    QString folderName() const;

    /** 设置可点击的名称预设（点击即填入输入框），不设置则隐藏 */
    void setPresets(const QStringList &presets);

private slots:
    void onConfirm();
    void onNameChanged(const QString &text);

private:
    void initUI();

    Mode m_mode;
    QLineEdit *m_nameEdit;
    QPushButton *m_confirmBtn;
    QWidget *m_presetsContainer;
};

#endif // FAVORITEFOLDERDIALOG_H
