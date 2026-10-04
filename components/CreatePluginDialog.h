/**
 * @file   CreatePluginDialog.h
 * @brief  制作插件对话框类声明
 * @author BlockBox Team
 * @date   2026-08-05
 *
 * 填写插件信息 → 生成插件项目模板（plugin.json 清单 + main.js + README + 图标）
 * → 一键打包为 .BlockBox 文件（本质 zip，后缀不同）并复制到插件目录。
 */
#ifndef CREATEPLUGINDIALOG_H
#define CREATEPLUGINDIALOG_H

#include "components/AppDialogBase.h"

#include <QString>

class QLineEdit;
class QPushButton;
class QTextEdit;
class QLabel;
class QStackedWidget;

class CreatePluginDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit CreatePluginDialog(QWidget *parent = nullptr);

    /** 生成的模板目录（成功生成后有效） */
    QString templateDir() const { return m_templateDir; }
    /** 打包后的 .BlockBox 文件路径（成功打包后有效） */
    QString packagedPath() const { return m_packagedPath; }
    /** 是否已打包为 .BlockBox */
    bool packaged() const { return m_packaged; }

private slots:
    void onBrowseDir();
    void onGenerate();
    void onPackage();
    void onOpenTemplateDir();
    void onBackToForm();

private:
    void initUI();
    void initStyle();
    void showResultView(const QString &msg, bool withPackageBtn);
    void validateIdFromName(const QString &name);

    QWidget *m_formPage;
    QWidget *m_resultPage;

    QLineEdit *m_nameEdit;
    QLineEdit *m_idEdit;
    QLineEdit *m_versionEdit;
    QLineEdit *m_authorEdit;
    QTextEdit *m_descEdit;
    QLineEdit *m_dirEdit;
    QPushButton *m_browseBtn;
    QPushButton *m_generateBtn;
    QPushButton *m_cancelBtn;

    QLabel *m_resultIconLabel;
    QLabel *m_resultTextLabel;
    QPushButton *m_packageBtn;
    QPushButton *m_openDirBtn;
    QPushButton *m_doneBtn;
    QPushButton *m_backBtn;

    QStackedWidget *m_stack;

    QString m_templateDir;
    QString m_packagedPath;
    bool m_packaged = false;
};

#endif // CREATEPLUGINDIALOG_H
