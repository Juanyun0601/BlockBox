/**
 * @file   ModpackImportPage.cpp
 * @brief  整合包导入页面实现
 * @author BlockBox Team
 * @date   2026-06-10
 */
#include "ModpackImportPage.h"

#include <QFileInfo>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/modpack/ModpackDetector.h"
#include "utils/modpack/ModpackImporter.h"
#include "components/NotificationManager.h"

#include "components/AppFileDialog.h"
#include "components/AppMessageBox.h"
#include "components/ProgressDialog.h"

ModpackImportPage::ModpackImportPage(QWidget* parent)
    : QWidget(parent)
    , m_stack(nullptr)
    , m_selectBtn(nullptr)
    , m_infoPage(nullptr)
    , m_infoGroup(nullptr)
    , m_nameEdit(nullptr)
    , m_versionEdit(nullptr)
    , m_authorEdit(nullptr)
    , m_gameVersionEdit(nullptr)
    , m_formatEdit(nullptr)
    , m_descLabel(nullptr)
    , m_instanceNameEdit(nullptr)
    , m_installBtn(nullptr)
{
  initUI();
}

void ModpackImportPage::initUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 10, 20, 10);
  mainLayout->setSpacing(10);

  m_stack = new QStackedWidget(this);

  // --- Page 0: 选择文件按钮 ---
  QWidget* selectPage = new QWidget();
  QVBoxLayout* selectLayout = new QVBoxLayout(selectPage);
  selectLayout->setAlignment(Qt::AlignCenter);

  m_selectBtn = new QPushButton(tr("选择整合包文件"));
  m_selectBtn->setObjectName("modpackSelectBtn");
  m_selectBtn->setCursor(Qt::PointingHandCursor);
  m_selectBtn->setFixedSize(220, 50);
  m_selectBtn->setStyleSheet(
    "QPushButton {"
    "  font-size: 15px;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 8px;"
    "  padding: 10px 30px;"
    "  background-color: " + ThemeManager::instance()->currentThemeColor() + ";"
    "}"
    "QPushButton:hover {"
    "  background-color: " + ThemeManager::instance()->getThemeColorHover() + ";"
    "}"
  );
  connect(m_selectBtn, &QPushButton::clicked, this, &ModpackImportPage::onSelectFileClicked);
  selectLayout->addWidget(m_selectBtn);

  m_stack->addWidget(selectPage);

  // --- Page 1: 整合包信息 ---
  m_infoPage = new QWidget();
  QVBoxLayout* infoLayout = new QVBoxLayout(m_infoPage);
  infoLayout->setContentsMargins(0, 0, 0, 0);
  infoLayout->setSpacing(10);

  QPushButton* reselectBtn = new QPushButton(tr("< 重新选择文件"));
  reselectBtn->setCursor(Qt::PointingHandCursor);
  reselectBtn->setFixedHeight(28);
  reselectBtn->setStyleSheet(
    "QPushButton {"
    "  font-size: 13px;"
    "  color: " + ThemeManager::instance()->currentThemeColor() + ";"
    "  border: none;"
    "  background: transparent;"
    "  text-align: left;"
    "  padding: 0;"
    "}"
    "QPushButton:hover {"
    "  color: " + ThemeManager::instance()->getThemeColorHover() + ";"
    "}"
  );
  connect(reselectBtn, &QPushButton::clicked, this, [this]() {
    m_currentFilePath.clear();
    m_stack->setCurrentIndex(0);
    m_installBtn->setEnabled(false);
  });
  infoLayout->addWidget(reselectBtn);

  // 整合包信息
  m_infoGroup = new QGroupBox(tr("整合包信息"));
  m_infoGroup->setObjectName("modpackInfoGroup");
  QFormLayout* formLayout = new QFormLayout(m_infoGroup);
  formLayout->setSpacing(8);
  formLayout->setContentsMargins(12, 16, 12, 12);

  auto createReadOnlyLineEdit = [this]() -> QLineEdit*
  {
    QLineEdit* edit = new QLineEdit();
    edit->setReadOnly(true);
    edit->setStyleSheet(
      "QLineEdit {"
      "  font-size: 13px;"
      "  border: none;"
      "  background: transparent;"
      "  color: " + ThemeManager::instance()->currentThemeColor() + ";"
      "}"
    );
    return edit;
  };

  m_nameEdit = createReadOnlyLineEdit();
  formLayout->addRow(tr("名称:"), m_nameEdit);

  m_versionEdit = createReadOnlyLineEdit();
  formLayout->addRow(tr("版本:"), m_versionEdit);

  m_authorEdit = createReadOnlyLineEdit();
  formLayout->addRow(tr("作者:"), m_authorEdit);

  m_gameVersionEdit = createReadOnlyLineEdit();
  formLayout->addRow(tr("游戏版本:"), m_gameVersionEdit);

  m_formatEdit = createReadOnlyLineEdit();
  formLayout->addRow(tr("格式类型:"), m_formatEdit);

  m_descLabel = new QLabel();
  m_descLabel->setWordWrap(true);
  m_descLabel->setStyleSheet(
    "font-size: 13px;"
    "color: " + ThemeManager::instance()->currentThemeColor() + ";"
    "padding: 2px 0;"
  );
  formLayout->addRow(tr("描述:"), m_descLabel);

  infoLayout->addWidget(m_infoGroup);

  // 实例名称和安装按钮
  QHBoxLayout* instanceLayout = new QHBoxLayout();
  instanceLayout->setSpacing(10);

  m_instanceNameEdit = new QLineEdit();
  m_instanceNameEdit->setObjectName("modpackInstNameEdit");
  m_instanceNameEdit->setPlaceholderText(tr("实例名称"));
  m_instanceNameEdit->setFixedHeight(36);
  m_instanceNameEdit->setStyleSheet(
    "QLineEdit {"
    "  font-size: 14px;"
    "  padding: 4px 8px;"
    "  border: 1px solid #ccc;"
    "  border-radius: 4px;"
    "}"
  );
  instanceLayout->addWidget(m_instanceNameEdit, 1);

  m_installBtn = new QPushButton(tr("安装"));
  m_installBtn->setObjectName("modpackLocalInstallBtn");
  m_installBtn->setFixedHeight(36);
  m_installBtn->setFixedWidth(100);
  m_installBtn->setEnabled(false);
  m_installBtn->setStyleSheet(
    "QPushButton {"
    "  font-size: 14px;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  padding: 6px 20px;"
    "  background-color: " + ThemeManager::instance()->currentThemeColor() + ";"
    "}"
    "QPushButton:hover {"
    "  background-color: " + ThemeManager::instance()->getThemeColorHover() + ";"
    "}"
    "QPushButton:disabled {"
    "  background-color: #888888;"
    "}"
  );
  connect(m_installBtn, &QPushButton::clicked, this, &ModpackImportPage::onInstallClicked);
  instanceLayout->addWidget(m_installBtn);

  infoLayout->addLayout(instanceLayout);

  m_stack->addWidget(m_infoPage);

  mainLayout->addWidget(m_stack);
}

void ModpackImportPage::onSelectFileClicked()
{
  QString filePath = AppFileDialog::getOpenFileName(
    this,
    tr("选择整合包文件"),
    QString(),
    tr("整合包文件 (*.zip *.mrpack *.blockbox)")
  );

  if (filePath.isEmpty())
  {
    return;
  }

  loadModpackFile(filePath);
}

void ModpackImportPage::loadModpackFile(const QString& filePath)
{
  m_currentFilePath = filePath;

  m_currentInfo = modpack::ModpackDetector::parse(filePath);

  m_nameEdit->setText(m_currentInfo.name);
  m_versionEdit->setText(m_currentInfo.version);
  m_authorEdit->setText(m_currentInfo.author);
  m_gameVersionEdit->setText(m_currentInfo.gameVersion);
  m_formatEdit->setText(m_currentInfo.typeName());
  m_descLabel->setText(m_currentInfo.description);
  m_instanceNameEdit->setText(m_currentInfo.name);

  // 通用/未知格式的包没有游戏版本：把字段放开让用户填，否则装完也没有可启动的版本
  const bool needManualVersion = m_currentInfo.gameVersion.trimmed().isEmpty();
  m_gameVersionEdit->setReadOnly(!needManualVersion);
  m_gameVersionEdit->setPlaceholderText(needManualVersion
                                            ? tr("必填，例如 1.20.1")
                                            : QString());

  if (m_currentInfo.type == modpack::ModpackType::Unknown)
  {
    AppMessageBox::warning(
      this,
      tr("无法识别"),
      tr("未检测到标准整合包格式，将作为通用整合包导入。\n请手动填写游戏版本。")
    );
    m_nameEdit->setText(QFileInfo(filePath).completeBaseName());
    m_instanceNameEdit->setText(QFileInfo(filePath).completeBaseName());
  }
  else if (m_currentInfo.type == modpack::ModpackType::Generic)
  {
    NotificationManager::showInfo(
      this,
      tr("检测为通用整合包，将解压内容到实例目录。\n请确认游戏版本。")
    );
  }
  else
  {
    NotificationManager::showSuccess(
      this,
      tr("成功识别整合包格式: %1").arg(m_currentInfo.typeName())
    );
  }

  m_installBtn->setEnabled(true);
  m_stack->setCurrentIndex(1);
}

void ModpackImportPage::onInstallClicked()
{
  QString instanceName = m_instanceNameEdit->text().trimmed();

  if (instanceName.isEmpty())
  {
    instanceName = m_currentInfo.name;
  }

  if (instanceName.isEmpty())
  {
    AppMessageBox::warning(this, tr("错误"), tr("请输入实例名称"));
    return;
  }

  if (m_currentFilePath.isEmpty())
  {
    AppMessageBox::warning(this, tr("错误"), tr("请先选择整合包文件"));
    return;
  }

  // 通用/未知包允许手填游戏版本，同步回清单
  m_currentInfo.gameVersion = m_gameVersionEdit->text().trimmed();
  if (m_currentInfo.gameVersion.isEmpty())
  {
    AppMessageBox::warning(this, tr("错误"), tr("请填写游戏版本，例如 1.20.1"));
    m_gameVersionEdit->setFocus();
    return;
  }

  // 创建导入器（MultiMC 模式：导入流程是一个 Task），交给进度对话框执行。
  // 实例路径由导入器按 <游戏根>/versions/<实例名> 自行计算，页面不再拼接
  modpack::ModpackImporter* importer = new modpack::ModpackImporter(this);
  importer->setModpackInfo(m_currentInfo);
  importer->setZipFilePath(m_currentFilePath);
  importer->setInstanceName(instanceName);

  // MultiMC 进度系统：非模态进度窗口实时显示阶段状态与百分比、可随时取消，
  // 安装期间软件其他部分照常可用
  m_installBtn->setEnabled(false);

  ProgressDialog* progressDlg = new ProgressDialog(this);
  progressDlg->setAttribute(Qt::WA_DeleteOnClose);
  progressDlg->setSkipButton(true, tr("取消"));

  connect(importer, &Task::finished, this, [this, importer]() {
    m_installBtn->setEnabled(true);
    if (importer->wasSuccessful())
    {
      NotificationManager::showSuccess(this, tr("整合包安装完成！"));
      emit modpackInstalled();
      emit backRequested();
    }
    else if (importer->wasAborted())
    {
      NotificationManager::showInfo(this, tr("整合包安装已取消"));
    }
    else
    {
      AppMessageBox::warning(this, tr("安装失败"),
                             importer->failReason().isEmpty()
                                 ? tr("整合包安装失败，请检查日志。")
                                 : importer->failReason());
    }
    importer->deleteLater();
  });

  progressDlg->showWithTask(importer);
}
