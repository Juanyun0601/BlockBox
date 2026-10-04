/**
 * @file   ModpackImportPage.h
 * @brief  整合包导入页面类声明
 * @author BlockBox Team
 * @date   2026-06-10
 */
#ifndef MODPACKIMPORTPAGE_H
#define MODPACKIMPORTPAGE_H

#include <QWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QStackedWidget>

#include "utils/modpack/ModpackInfo.h"

class ModpackImportPage : public QWidget
{
    Q_OBJECT

public:
    explicit ModpackImportPage(QWidget* parent = nullptr);

    /** 从外部加载整合包文件（供 MainWindow 文件拖入调用） */
    void loadModpackFile(const QString& filePath);

signals:
    void backRequested();
    void modpackInstalled();

private slots:
    void onSelectFileClicked();
    void onInstallClicked();

private:
    void initUI();

    QStackedWidget* m_stack;

    // 选择文件按钮
    QPushButton* m_selectBtn;

    // 整合包信息页
    QWidget* m_infoPage;
    QGroupBox* m_infoGroup;
    QLineEdit* m_nameEdit;
    QLineEdit* m_versionEdit;
    QLineEdit* m_authorEdit;
    QLineEdit* m_gameVersionEdit;
    QLineEdit* m_formatEdit;
    QLabel* m_descLabel;
    QLineEdit* m_instanceNameEdit;
    QPushButton* m_installBtn;

    QString m_currentFilePath;
    modpack::ModpackInfo m_currentInfo;
};

#endif // MODPACKIMPORTPAGE_H
