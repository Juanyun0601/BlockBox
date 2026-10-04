/**
 * @file   ModpackExportPage.h
 * @brief  整合包导出页面类声明
 * @author BlockBox Team
 * @date   2026-06-19
 */
#pragma once

#include <QWidget>
#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVector>

class ModpackExportPage : public QWidget
{
  Q_OBJECT

public:
  explicit ModpackExportPage(QWidget* parent = nullptr);

signals:
  void backRequested();
  void exportFinished();

public slots:
  void setInstancePath(const QString& path);

private slots:
  void onPrevClicked();
  void onNextClicked();
  void onExportClicked();
  void onBrowseSavePath();
  void onFormatCardClicked(int id);
  void onMinMemorySliderChanged(int value);

private:
  void initUI();
  QWidget* createStep1();
  QWidget* createStep2();
  QWidget* createStep3();
  QWidget* createModuleSelectionPage();
  void updateStep2Fields();
  QStringList collectSelectedFiles();
  bool validateStep2();
  void populateFileTree(const QString& basePath, const QString& relativePath, QTreeWidgetItem* parentItem);
  void updateParentCheckState(QTreeWidgetItem* item);
  void onModuleChecked(const QString& moduleId, bool checked);
  QStringList getSelectedModules() const;

  QString m_instancePath;
  QStackedWidget* m_stepStack;
  QPushButton* m_prevBtn;
  QPushButton* m_nextBtn;
  QPushButton* m_exportBtn;
  QProgressBar* m_progressBar;
  QLabel* m_progressLabel;
  int m_currentStep;

  // Step 1 - format selection
  QButtonGroup* m_formatGroup;
  QString m_selectedFormat;

  // Step 2 - info form
  QWidget* m_step2Page;
  QLineEdit* m_nameEdit;
  QLineEdit* m_authorEdit;
  QLineEdit* m_versionEdit;
  QTextEdit* m_descEdit;
  QLineEdit* m_savePathEdit;

  // MCBBS extra fields
  QWidget* m_mcbbsExtraWidget;
  QCheckBox* m_forceUpdateCheck;
  QLineEdit* m_authlibInjectorEdit;
  QVector<QCheckBox*> m_javaVersionChecks;
  QLineEdit* m_fileApiEdit;
  QLineEdit* m_urlEdit;
  QSlider* m_minMemorySlider;
  QLineEdit* m_minMemoryEdit;
  QLineEdit* m_javaArgsEdit;
  QLineEdit* m_launchArgsEdit;

  // Server extra fields
  QWidget* m_serverExtraWidget;
  QLineEdit* m_serverFileApiEdit;
  QSlider* m_serverMinMemorySlider;
  QLineEdit* m_serverMinMemoryEdit;
  QLineEdit* m_serverJavaArgsEdit;
  QLineEdit* m_serverLaunchArgsEdit;

  // Step 3 - file tree
  QTreeWidget* m_fileTree;

  // Step 2.5 - module selection
  QWidget* m_moduleSelectionPage;
  QMap<QString, QCheckBox*> m_moduleChecks;
  QMap<QString, QLabel*> m_moduleSizeLabels;
  QPushButton* m_moduleNextBtn;
  QPushButton* m_modulePrevBtn;

};

/**
 * @brief 格式卡片数据
 */
struct FormatCardInfo
{
  QString title;
  QString description;
  QString outputHint;
  QString formatKey;
};