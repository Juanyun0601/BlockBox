/**
 * @file   ModpackExportPage.cpp
 * @brief  整合包导出页面实现
 * @author BlockBox Team
 * @date   2026-06-19
 */
#include "ModpackExportPage.h"

#include "components/NotificationManager.h"
#include "utils/ThemeManager.h"
#include "utils/modpack/ModpackFileAdviser.h"
#include "utils/modpack/ModpackExporter.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include "components/AppMessageBox.h"
#include <QScrollArea>
#include <QUrl>

ModpackExportPage::ModpackExportPage(QWidget* parent)
  : QWidget(parent)
  , m_stepStack(nullptr)
  , m_prevBtn(nullptr)
  , m_nextBtn(nullptr)
  , m_exportBtn(nullptr)
  , m_progressBar(nullptr)
  , m_progressLabel(nullptr)
  , m_currentStep(0)
  , m_formatGroup(nullptr)
  , m_selectedFormat("blockbox")
  , m_step2Page(nullptr)
  , m_nameEdit(nullptr)
  , m_authorEdit(nullptr)
  , m_versionEdit(nullptr)
  , m_descEdit(nullptr)
  , m_savePathEdit(nullptr)
  , m_mcbbsExtraWidget(nullptr)
  , m_forceUpdateCheck(nullptr)
  , m_authlibInjectorEdit(nullptr)
  , m_fileApiEdit(nullptr)
  , m_urlEdit(nullptr)
  , m_minMemorySlider(nullptr)
  , m_minMemoryEdit(nullptr)
  , m_javaArgsEdit(nullptr)
  , m_launchArgsEdit(nullptr)
  , m_serverExtraWidget(nullptr)
  , m_serverFileApiEdit(nullptr)
  , m_serverMinMemorySlider(nullptr)
  , m_serverMinMemoryEdit(nullptr)
  , m_serverJavaArgsEdit(nullptr)
  , m_serverLaunchArgsEdit(nullptr)
  , m_fileTree(nullptr)
  , m_moduleSelectionPage(nullptr)
  , m_moduleNextBtn(nullptr)
  , m_modulePrevBtn(nullptr)
{
  initUI();
}

void ModpackExportPage::setInstancePath(const QString& path)
{
  m_instancePath = path;
  if (m_fileTree)
  {
    m_fileTree->clear();
    if (!m_instancePath.isEmpty())
    {
      populateFileTree(m_instancePath, QString(), nullptr);
      m_fileTree->expandAll();
    }
  }

  // 自动填充默认名称
  if (m_nameEdit)
  {
    QFileInfo fi(path);
    m_nameEdit->setText(fi.fileName());
  }

  // 更新默认保存路径
  if (m_savePathEdit && !m_instancePath.isEmpty())
  {
    QString name = QFileInfo(m_instancePath).fileName();
    QString ext;
    if (m_selectedFormat == "modrinth")
    {
      ext = ".mrpack";
    }
    else if (m_selectedFormat == "blockbox")
    {
      ext = ".blockbox";
    }
    else if (m_selectedFormat == "packwiz")
    {
      ext = "";
    }
    else
    {
      ext = ".zip";
    }
    m_savePathEdit->setText(QDir::homePath() + "/" + name + ext);
  }

  // 填充模块大小信息
  if (!m_moduleChecks.isEmpty() && !m_instancePath.isEmpty())
  {
    auto modules = modpack::ModpackFileAdviser::getModules();
    for (const auto& module : modules)
    {
      int fileCount = 0;
      qint64 totalSize = 0;

      QDirIterator it(m_instancePath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
      while (it.hasNext())
      {
        it.next();
        QString relativePath = QDir(m_instancePath).relativeFilePath(it.filePath());
        if (modpack::ModpackFileAdviser::getModuleForFile(relativePath) == module.id)
        {
          fileCount++;
          totalSize += it.fileInfo().size();
        }
      }

      if (m_moduleSizeLabels.contains(module.id))
      {
        QString sizeStr;
        if (totalSize < 1024)
        {
          sizeStr = QString::number(totalSize) + " B";
        }
        else if (totalSize < 1024 * 1024)
        {
          sizeStr = QString::number(totalSize / 1024.0, 'f', 1) + " KB";
        }
        else
        {
          sizeStr = QString::number(totalSize / (1024.0 * 1024.0), 'f', 1) + " MB";
        }
        m_moduleSizeLabels[module.id]->setText(
          tr("%1 个文件, %2").arg(fileCount).arg(sizeStr));
      }
    }
  }
}

void ModpackExportPage::initUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 10, 20, 10);
  mainLayout->setSpacing(10);

  // 步骤内容区
  m_stepStack = new QStackedWidget(this);
  m_stepStack->addWidget(createStep1());
  m_stepStack->addWidget(createStep2());
  m_stepStack->addWidget(createModuleSelectionPage());
  m_stepStack->addWidget(createStep3());
  mainLayout->addWidget(m_stepStack, 1);

  // 进度条（隐藏）
  QHBoxLayout* progressLayout = new QHBoxLayout();
  m_progressLabel = new QLabel();
  m_progressLabel->setStyleSheet("font-size: 12px; color: #888;");
  m_progressLabel->hide();
    m_progressBar = new QProgressBar();
    m_progressBar->setObjectName("modpackExportProgress");
    m_progressBar->setFixedHeight(20);
  m_progressBar->setRange(0, 100);
  m_progressBar->hide();
  progressLayout->addWidget(m_progressLabel);
  progressLayout->addWidget(m_progressBar, 1);
  mainLayout->addLayout(progressLayout);

  // 底部按钮栏
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(10);
  btnLayout->setContentsMargins(0, 0, 0, 0);

  btnLayout->addStretch();

  QString themeColor = ThemeManager::instance()->currentThemeColor();
  QString themeHover = ThemeManager::instance()->getThemeColorHover();

  QString btnStyle = QString(
    "QPushButton {"
    "  font-size: 14px;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 6px;"
    "  padding: 8px 24px;"
    "  background-color: %1;"
    ""
    "}"
  ).arg(themeColor, themeHover);

  QString secondaryBtnStyle = QString(
    "QPushButton {"
    "  font-size: 14px;"
    "  color: %1;"
    "  border: 1px solid %1;"
    "  border-radius: 4px;"
    "  padding: 8px 24px;"
    "  background: transparent;"
    "}"
  ).arg(themeColor, ThemeManager::instance()->getThemeColorLight());

  QString secondaryBtnStyle2 = QString(
    "QPushButton {"
    "  font-size: 14px;"
    "  color: %1;"
    "  border: 1px solid %1;"
    "  border-radius: 6px;"
    "  padding: 8px 24px;"
    "  background: transparent;"
    "}"
  ).arg(themeColor, ThemeManager::instance()->getThemeColorLight());

  m_prevBtn = new QPushButton(tr("上一步"));
  m_prevBtn->setStyleSheet(secondaryBtnStyle2);
  m_prevBtn->setEnabled(false);
  connect(m_prevBtn, &QPushButton::clicked, this, &ModpackExportPage::onPrevClicked);
  btnLayout->addWidget(m_prevBtn);

  m_nextBtn = new QPushButton(tr("下一步"));
  m_nextBtn->setStyleSheet(btnStyle);
  m_nextBtn->setEnabled(false);
  connect(m_nextBtn, &QPushButton::clicked, this, &ModpackExportPage::onNextClicked);
  btnLayout->addWidget(m_nextBtn);

  m_exportBtn = new QPushButton(tr("导出"));
  m_exportBtn->setStyleSheet(btnStyle);
  m_exportBtn->hide();
  connect(m_exportBtn, &QPushButton::clicked, this, &ModpackExportPage::onExportClicked);
  btnLayout->addWidget(m_exportBtn);

  mainLayout->addLayout(btnLayout);

}

QWidget* ModpackExportPage::createStep1()
{
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);

  QWidget* page = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 10, 0, 10);
  layout->setSpacing(16);

  QLabel* titleLabel = new QLabel(tr("选择导出格式"));
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
  layout->addWidget(titleLabel);

  QLabel* descLabel = new QLabel(tr("请选择您要导出的整合包格式，不同格式适用于不同的启动器平台。"));
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet("font-size: 12px; color: #888;");
  layout->addWidget(descLabel);

  m_formatGroup = new QButtonGroup(this);
  m_formatGroup->setExclusive(true);

  QVector<FormatCardInfo> formats = {
    {tr("方块盒子格式"), tr("BlockBox 原生格式，支持模块化打包，兼顾性能与完整性"), tr("输出: .blockbox"), "blockbox"},
    {tr("CurseForge 格式"), tr("最通用的国际格式，可直接上传到 CurseForge 平台"), tr("输出: .zip"), "curseforge"},
    {tr("Packwiz 格式"), tr("Git 友好的 TOML 格式，模组包开发者首选"), tr("输出: 目录"), "packwiz"},
    {tr("MCBBS 格式"), tr("国内最流行的格式，同时生成 MCBBS 和 CurseForge 兼容清单"), tr("输出: .zip"), "mcbbs"},
    {tr("MultiMC 格式"), tr("MultiMC / PrismLauncher 原生格式"), tr("输出: .zip"), "multimc"},
    {tr("自安装服务器包"), tr("HMCL 自安装格式，含自动安装脚本"), tr("输出: .zip"), "server"},
    {tr("Modrinth 格式"), tr("Modrinth 原生格式，支持模组自动下载"), tr("输出: .mrpack"), "modrinth"}
  };

  QString themeColor = ThemeManager::instance()->currentThemeColor();
  QString themeLight = ThemeManager::instance()->getThemeColorLight();

  for (int i = 0; i < formats.size(); ++i)
  {
    const FormatCardInfo& info = formats[i];

    QPushButton* card = new QPushButton();
    card->setObjectName("exportFormatCard");
    card->setCheckable(true);
    card->setCursor(Qt::PointingHandCursor);
    card->setFixedHeight(100);
    // 卡片带背景（对应 @BG_CARD@），不再透明贴在页面底色上
    const QString cardBg = (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme)
                               ? QStringLiteral("#ffffff")
                               : QStringLiteral("#2d2d2d");
    card->setStyleSheet(QString(
      "QPushButton#exportFormatCard {"
      "  text-align: left;"
      "  padding: 16px;"
      "  border: 2px solid #d8d8d8;"
      "  border-radius: 8px;"
      "  background: %3;"
      "}"
      "QPushButton#exportFormatCard:hover {"
      "  border-color: %1;"
      "  background: %2;"
      "}"
      "QPushButton#exportFormatCard:checked {"
      "  border-color: %1;"
      "  background: %2;"
      "}"
    ).arg(themeColor, themeLight, cardBg));

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(4);

    QLabel* cardTitle = new QLabel(info.title);
    cardTitle->setStyleSheet("font-size: 15px; font-weight: bold; border: none; background: transparent;");
    cardLayout->addWidget(cardTitle);

    QLabel* cardDesc = new QLabel(info.description);
    cardDesc->setWordWrap(true);
    cardDesc->setStyleSheet("font-size: 12px; color: #888; border: none; background: transparent;");
    cardLayout->addWidget(cardDesc);

    QLabel* cardOutput = new QLabel(info.outputHint);
    cardOutput->setStyleSheet("font-size: 11px; color: #aaa; border: none; background: transparent;");
    cardLayout->addWidget(cardOutput);

    m_formatGroup->addButton(card, i);
    layout->addWidget(card);
  }

  connect(m_formatGroup, &QButtonGroup::idClicked, this, &ModpackExportPage::onFormatCardClicked);

  // 默认选中第一个
  m_formatGroup->button(0)->setChecked(true);
  m_selectedFormat = "blockbox";

  layout->addStretch();
  scrollArea->setWidget(page);
  return scrollArea;
}

QWidget* ModpackExportPage::createStep2()
{
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);

  m_step2Page = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(m_step2Page);
  layout->setContentsMargins(0, 10, 0, 10);
  layout->setSpacing(12);

  QLabel* titleLabel = new QLabel(tr("整合包信息"));
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
  layout->addWidget(titleLabel);

  // 使用 QFormLayout 风格的布局
  auto createField = [this](const QString& label, QWidget* edit, bool required = false) -> QHBoxLayout* {
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(10);

    QLabel* lbl = new QLabel(label);
    lbl->setFixedWidth(80);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setStyleSheet("font-size: 13px;");
    if (required)
    {
      lbl->setText(label + " *");
    }
    row->addWidget(lbl);
    row->addWidget(edit, 1);
    return row;
  };

  QString lineEditStyle =
    "QLineEdit {"
    "  font-size: 13px;"
    "  padding: 8px 12px;"
    "  border: 1px solid #d8d8d8;"
    "  border-radius: 6px;"
    ""
    "}"
    "QLineEdit:focus {"
    "  border-color: " + ThemeManager::instance()->currentThemeColor() + ";"
    ""
    "}";

  // 名称
  m_nameEdit = new QLineEdit();
  m_nameEdit->setPlaceholderText(tr("整合包名称"));
  m_nameEdit->setStyleSheet(lineEditStyle);
  m_nameEdit->setFixedHeight(34);
  layout->addLayout(createField(tr("名称"), m_nameEdit, true));

  // 作者
  m_authorEdit = new QLineEdit();
  m_authorEdit->setText("Anonymous");
  m_authorEdit->setStyleSheet(lineEditStyle);
  m_authorEdit->setFixedHeight(34);
  layout->addLayout(createField(tr("作者"), m_authorEdit, true));

  // 版本号
  m_versionEdit = new QLineEdit();
  m_versionEdit->setText("1.0");
  m_versionEdit->setStyleSheet(lineEditStyle);
  m_versionEdit->setFixedHeight(34);
  layout->addLayout(createField(tr("版本号"), m_versionEdit, true));

  // 描述
  QLabel* descTitle = new QLabel(tr("描述"));
  descTitle->setStyleSheet("font-size: 13px;");
  layout->addWidget(descTitle);

  m_descEdit = new QTextEdit();
  m_descEdit->setPlaceholderText(tr("整合包描述（可选）"));
  m_descEdit->setFixedHeight(80);
  m_descEdit->setStyleSheet(
    "QTextEdit {"
    "  font-size: 13px;"
    "  padding: 8px 12px;"
    "  border: 1px solid #d8d8d8;"
    "  border-radius: 6px;"
    "}"
    "QTextEdit:focus {"
    "  border-color: " + ThemeManager::instance()->currentThemeColor() + ";"
    "}"
  );
  layout->addWidget(m_descEdit);

  // 保存路径
  QHBoxLayout* savePathRow = new QHBoxLayout();
  savePathRow->setSpacing(10);

  QLabel* saveLbl = new QLabel(tr("保存路径") + " *");
  saveLbl->setFixedWidth(80);
  saveLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  saveLbl->setStyleSheet("font-size: 13px;");
  savePathRow->addWidget(saveLbl);

  m_savePathEdit = new QLineEdit();
  m_savePathEdit->setStyleSheet(lineEditStyle);
  m_savePathEdit->setFixedHeight(34);
  savePathRow->addWidget(m_savePathEdit, 1);

  QPushButton* browseBtn = new QPushButton(tr("浏览"));
  browseBtn->setFixedHeight(34);
  browseBtn->setFixedWidth(70);
  browseBtn->setStyleSheet(
    "QPushButton {"
    "  font-size: 13px;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 6px;"
    "  padding: 6px 12px;"
    "  background-color: #4CAF50;"
    "}"
  );
  connect(browseBtn, &QPushButton::clicked, this, &ModpackExportPage::onBrowseSavePath);
  savePathRow->addWidget(browseBtn);

  layout->addLayout(savePathRow);

  // MCBBS 额外字段
  m_mcbbsExtraWidget = new QWidget();
  QVBoxLayout* mcbbsLayout = new QVBoxLayout(m_mcbbsExtraWidget);
  mcbbsLayout->setContentsMargins(0, 0, 0, 0);
  mcbbsLayout->setSpacing(8);

  QLabel* mcbbsTitle = new QLabel(tr("MCBBS 额外选项"));
  mcbbsTitle->setStyleSheet("font-size: 14px; font-weight: bold; margin-top: 8px;");
  mcbbsLayout->addWidget(mcbbsTitle);

  m_fileApiEdit = new QLineEdit();
  m_fileApiEdit->setPlaceholderText(tr("文件 API 地址"));
  m_fileApiEdit->setStyleSheet(lineEditStyle);
  m_fileApiEdit->setFixedHeight(34);
  mcbbsLayout->addLayout(createField(tr("文件API"), m_fileApiEdit, true));

  m_urlEdit = new QLineEdit();
  m_urlEdit->setPlaceholderText(tr("项目主页 URL"));
  m_urlEdit->setStyleSheet(lineEditStyle);
  m_urlEdit->setFixedHeight(34);
  mcbbsLayout->addLayout(createField(tr("项目主页"), m_urlEdit, true));

  m_forceUpdateCheck = new QCheckBox(tr("强制更新"));
  m_forceUpdateCheck->setStyleSheet("font-size: 13px;");
  mcbbsLayout->addWidget(m_forceUpdateCheck);

  m_authlibInjectorEdit = new QLineEdit();
  m_authlibInjectorEdit->setPlaceholderText(tr("authlib-injector 服务器地址（可选）"));
  m_authlibInjectorEdit->setStyleSheet(lineEditStyle);
  m_authlibInjectorEdit->setFixedHeight(34);
  mcbbsLayout->addLayout(createField(tr("authlib"), m_authlibInjectorEdit));

  // 最低内存
  QHBoxLayout* mcbbsMemRow = new QHBoxLayout();
  mcbbsMemRow->setSpacing(10);
  QLabel* mcbbsMemLbl = new QLabel(tr("最低内存"));
  mcbbsMemLbl->setFixedWidth(80);
  mcbbsMemLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  mcbbsMemLbl->setStyleSheet("font-size: 13px;");
  mcbbsMemRow->addWidget(mcbbsMemLbl);

  m_minMemorySlider = new QSlider(Qt::Horizontal);
  m_minMemorySlider->setRange(512, 16384);
  m_minMemorySlider->setValue(2048);
  m_minMemorySlider->setTickInterval(512);
  mcbbsMemRow->addWidget(m_minMemorySlider, 1);

  m_minMemoryEdit = new QLineEdit();
  m_minMemoryEdit->setText("2048");
  m_minMemoryEdit->setFixedWidth(60);
  m_minMemoryEdit->setAlignment(Qt::AlignCenter);
  m_minMemoryEdit->setStyleSheet(lineEditStyle);
  mcbbsMemRow->addWidget(m_minMemoryEdit);

  QLabel* mcbbsMemUnit = new QLabel("MB");
  mcbbsMemUnit->setStyleSheet("font-size: 13px;");
  mcbbsMemRow->addWidget(mcbbsMemUnit);

  mcbbsLayout->addLayout(mcbbsMemRow);

  connect(m_minMemorySlider, &QSlider::valueChanged, this, &ModpackExportPage::onMinMemorySliderChanged);

  // 支持的 Java 版本
  QHBoxLayout* javaVersionRow = new QHBoxLayout();
  javaVersionRow->setSpacing(8);
  QLabel* javaVersionLbl = new QLabel(tr("Java版本"));
  javaVersionLbl->setFixedWidth(80);
  javaVersionLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  javaVersionLbl->setStyleSheet("font-size: 13px;");
  javaVersionRow->addWidget(javaVersionLbl);

  QVector<int> javaVersions = {8, 11, 16, 17, 21};
  for (int ver : javaVersions)
  {
    QCheckBox* check = new QCheckBox(QString("Java %1").arg(ver));
    check->setStyleSheet("font-size: 13px;");
    javaVersionRow->addWidget(check);
    m_javaVersionChecks.append(check);
  }
  javaVersionRow->addStretch();
  mcbbsLayout->addLayout(javaVersionRow);

  // JVM 参数
  m_javaArgsEdit = new QLineEdit();
  m_javaArgsEdit->setPlaceholderText(tr("JVM 参数（可选）"));
  m_javaArgsEdit->setStyleSheet(lineEditStyle);
  m_javaArgsEdit->setFixedHeight(34);
  mcbbsLayout->addLayout(createField(tr("JVM参数"), m_javaArgsEdit));

  // 启动参数
  m_launchArgsEdit = new QLineEdit();
  m_launchArgsEdit->setPlaceholderText(tr("Minecraft 启动参数（可选）"));
  m_launchArgsEdit->setStyleSheet(lineEditStyle);
  m_launchArgsEdit->setFixedHeight(34);
  mcbbsLayout->addLayout(createField(tr("启动参数"), m_launchArgsEdit));

  layout->addWidget(m_mcbbsExtraWidget);
  m_mcbbsExtraWidget->hide();

  // Server 额外字段
  m_serverExtraWidget = new QWidget();
  QVBoxLayout* svLayout = new QVBoxLayout(m_serverExtraWidget);
  svLayout->setContentsMargins(0, 0, 0, 0);
  svLayout->setSpacing(8);

  QLabel* svTitle = new QLabel(tr("服务器包额外选项"));
  svTitle->setStyleSheet("font-size: 14px; font-weight: bold; margin-top: 8px;");
  svLayout->addWidget(svTitle);

  m_serverFileApiEdit = new QLineEdit();
  m_serverFileApiEdit->setPlaceholderText(tr("文件 API 地址（可选）"));
  m_serverFileApiEdit->setStyleSheet(lineEditStyle);
  m_serverFileApiEdit->setFixedHeight(34);
  svLayout->addLayout(createField(tr("文件API"), m_serverFileApiEdit));

  // 最低内存
  QHBoxLayout* svMemRow = new QHBoxLayout();
  svMemRow->setSpacing(10);
  QLabel* svMemLbl = new QLabel(tr("最低内存"));
  svMemLbl->setFixedWidth(80);
  svMemLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  svMemLbl->setStyleSheet("font-size: 13px;");
  svMemRow->addWidget(svMemLbl);

  m_serverMinMemorySlider = new QSlider(Qt::Horizontal);
  m_serverMinMemorySlider->setRange(512, 16384);
  m_serverMinMemorySlider->setValue(2048);
  m_serverMinMemorySlider->setTickInterval(512);
  svMemRow->addWidget(m_serverMinMemorySlider, 1);

  m_serverMinMemoryEdit = new QLineEdit();
  m_serverMinMemoryEdit->setText("2048");
  m_serverMinMemoryEdit->setFixedWidth(60);
  m_serverMinMemoryEdit->setAlignment(Qt::AlignCenter);
  m_serverMinMemoryEdit->setStyleSheet(lineEditStyle);
  svMemRow->addWidget(m_serverMinMemoryEdit);

  QLabel* svMemUnit = new QLabel("MB");
  svMemUnit->setStyleSheet("font-size: 13px;");
  svMemRow->addWidget(svMemUnit);

  svLayout->addLayout(svMemRow);

  connect(m_serverMinMemorySlider, &QSlider::valueChanged, this, [this](int value) {
    if (m_serverMinMemoryEdit)
    {
      m_serverMinMemoryEdit->setText(QString::number(value));
    }
  });

  // JVM 参数
  m_serverJavaArgsEdit = new QLineEdit();
  m_serverJavaArgsEdit->setPlaceholderText(tr("JVM 参数（可选）"));
  m_serverJavaArgsEdit->setStyleSheet(lineEditStyle);
  m_serverJavaArgsEdit->setFixedHeight(34);
  svLayout->addLayout(createField(tr("JVM参数"), m_serverJavaArgsEdit));

  // 启动参数
  m_serverLaunchArgsEdit = new QLineEdit();
  m_serverLaunchArgsEdit->setPlaceholderText(tr("启动参数（可选）"));
  m_serverLaunchArgsEdit->setStyleSheet(lineEditStyle);
  m_serverLaunchArgsEdit->setFixedHeight(34);
  svLayout->addLayout(createField(tr("启动参数"), m_serverLaunchArgsEdit));

  layout->addWidget(m_serverExtraWidget);
  m_serverExtraWidget->hide();

  layout->addStretch();
  scrollArea->setWidget(m_step2Page);
  return scrollArea;
}

void ModpackExportPage::updateStep2Fields()
{
  if (m_mcbbsExtraWidget)
  {
    m_mcbbsExtraWidget->setVisible(m_selectedFormat == "mcbbs");
  }
  if (m_serverExtraWidget)
  {
    m_serverExtraWidget->setVisible(m_selectedFormat == "server");
  }

  // 更新默认保存路径
  if (m_savePathEdit && !m_instancePath.isEmpty())
  {
    QString name = QFileInfo(m_instancePath).fileName();
    QString ext;
    if (m_selectedFormat == "modrinth")
    {
      ext = ".mrpack";
    }
    else if (m_selectedFormat == "blockbox")
    {
      ext = ".blockbox";
    }
    else if (m_selectedFormat == "packwiz")
    {
      ext = "";
    }
    else
    {
      ext = ".zip";
    }
    m_savePathEdit->setText(QDir::homePath() + "/" + name + ext);
  }
}

QWidget* ModpackExportPage::createStep3()
{
  QWidget* page = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 10, 0, 10);
  layout->setSpacing(8);

  QLabel* titleLabel = new QLabel(tr("选择导出文件"));
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
  layout->addWidget(titleLabel);

  QLabel* descLabel = new QLabel(tr("勾选要包含在整合包中的文件。建议的文件已默认勾选，大型文件（如 libraries、assets）默认不勾选。"));
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet("font-size: 12px; color: #888;");
  layout->addWidget(descLabel);

  // 全选/取消全选按钮
  QHBoxLayout* selectAllLayout = new QHBoxLayout();
  selectAllLayout->setSpacing(10);

  QPushButton* selectAllBtn = new QPushButton(tr("全选"));
  selectAllBtn->setStyleSheet(
    "QPushButton {"
    "  font-size: 12px;"
    "  padding: 4px 12px;"
    "  border: 1px solid #ccc;"
    "  border-radius: 4px;"
    "  background: transparent;"
    "}"
  );
  connect(selectAllBtn, &QPushButton::clicked, this, [this]() {
    std::function<void(QTreeWidgetItem*)> checkAll = [&](QTreeWidgetItem* item) {
      item->setCheckState(0, Qt::Checked);
      for (int i = 0; i < item->childCount(); ++i)
      {
        checkAll(item->child(i));
      }
    };
    for (int i = 0; i < m_fileTree->topLevelItemCount(); ++i)
    {
      checkAll(m_fileTree->topLevelItem(i));
    }
  });
  selectAllLayout->addWidget(selectAllBtn);

  QPushButton* deselectAllBtn = new QPushButton(tr("取消全选"));
  deselectAllBtn->setStyleSheet(
    "QPushButton {"
    "  font-size: 12px;"
    "  padding: 4px 12px;"
    "  border: 1px solid #ccc;"
    "  border-radius: 4px;"
    "  background: transparent;"
    "}"
  );
  connect(deselectAllBtn, &QPushButton::clicked, this, [this]() {
    std::function<void(QTreeWidgetItem*)> uncheckAll = [&](QTreeWidgetItem* item) {
      item->setCheckState(0, Qt::Unchecked);
      for (int i = 0; i < item->childCount(); ++i)
      {
        uncheckAll(item->child(i));
      }
    };
    for (int i = 0; i < m_fileTree->topLevelItemCount(); ++i)
    {
      uncheckAll(m_fileTree->topLevelItem(i));
    }
  });
  selectAllLayout->addWidget(deselectAllBtn);

  selectAllLayout->addStretch();
  layout->addLayout(selectAllLayout);

  // 文件树
  m_fileTree = new QTreeWidget();
  m_fileTree->setHeaderLabels({tr("文件/文件夹"), tr("大小")});
  m_fileTree->setColumnWidth(0, 400);
  m_fileTree->setColumnWidth(1, 80);
  layout->addWidget(m_fileTree, 1);

  return page;
}

QWidget* ModpackExportPage::createModuleSelectionPage()
{
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);

  QWidget* page = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 10, 0, 10);
  layout->setSpacing(10);

  QLabel* titleLabel = new QLabel(tr("选择打包模块"));
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
  layout->addWidget(titleLabel);

  QLabel* descLabel = new QLabel(tr("选择要打包的内容模块，未勾选的模块将不会被导出"));
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet("font-size: 12px; color: #888;");
  layout->addWidget(descLabel);

  QString themeColor = ThemeManager::instance()->currentThemeColor();

  // 模块列表
  auto modules = modpack::ModpackFileAdviser::getModules();
  for (const auto& module : modules)
  {
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(10);

    QCheckBox* check = new QCheckBox(module.name);
    check->setChecked(module.defaultSelected);
    connect(check, &QCheckBox::toggled, this, [this, id = module.id](bool checked) {
      onModuleChecked(id, checked);
    });

    QLabel* sizeLabel = new QLabel();
    sizeLabel->setStyleSheet("font-size: 12px; color: #888;");
    sizeLabel->setMinimumWidth(150);

    row->addWidget(check);
    row->addWidget(sizeLabel);
    row->addStretch();

    layout->addLayout(row);

    m_moduleChecks[module.id] = check;
    m_moduleSizeLabels[module.id] = sizeLabel;
  }

  layout->addSpacing(8);

  // 全选/全不选按钮
  QHBoxLayout* selectAllLayout = new QHBoxLayout();
  selectAllLayout->setSpacing(10);

  QPushButton* selectAllBtn = new QPushButton(tr("全选"));
  selectAllBtn->setStyleSheet(
    "QPushButton {"
    "  font-size: 12px;"
    "  padding: 4px 12px;"
    "  border: 1px solid #ccc;"
    "  border-radius: 4px;"
    "  background: transparent;"
    "}"
  );
  connect(selectAllBtn, &QPushButton::clicked, this, [this]() {
    for (auto it = m_moduleChecks.begin(); it != m_moduleChecks.end(); ++it)
    {
      it.value()->setChecked(true);
    }
  });
  selectAllLayout->addWidget(selectAllBtn);

  QPushButton* deselectAllBtn = new QPushButton(tr("全不选"));
  deselectAllBtn->setStyleSheet(
    "QPushButton {"
    "  font-size: 12px;"
    "  padding: 4px 12px;"
    "  border: 1px solid #ccc;"
    "  border-radius: 4px;"
    "  background: transparent;"
    "}"
  );
  connect(deselectAllBtn, &QPushButton::clicked, this, [this]() {
    for (auto it = m_moduleChecks.begin(); it != m_moduleChecks.end(); ++it)
    {
      it.value()->setChecked(false);
    }
  });
  selectAllLayout->addWidget(deselectAllBtn);
  selectAllLayout->addStretch();
  layout->addLayout(selectAllLayout);

  layout->addStretch();

  // 底部导航按钮
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(10);
  btnLayout->addStretch();

  QString secondaryBtnStyle = QString(
    "QPushButton {"
    "  font-size: 14px;"
    "  color: %1;"
    "  border: 1px solid %1;"
    "  border-radius: 6px;"
    "  padding: 8px 24px;"
    "  background: transparent;"
    ""
    "}"
  ).arg(themeColor, ThemeManager::instance()->getThemeColorLight());

  QString btnStyle = QString(
    "QPushButton {"
    "  font-size: 14px;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 6px;"
    "  padding: 8px 24px;"
    "  background-color: %1;"
    ""
    "}"
  ).arg(themeColor, ThemeManager::instance()->getThemeColorHover());

  m_modulePrevBtn = new QPushButton(tr("上一步"));
  m_modulePrevBtn->setStyleSheet(secondaryBtnStyle);
  connect(m_modulePrevBtn, &QPushButton::clicked, this, &ModpackExportPage::onPrevClicked);
  btnLayout->addWidget(m_modulePrevBtn);

  m_moduleNextBtn = new QPushButton(tr("下一步"));
  m_moduleNextBtn->setStyleSheet(btnStyle);
  connect(m_moduleNextBtn, &QPushButton::clicked, this, &ModpackExportPage::onNextClicked);
  btnLayout->addWidget(m_moduleNextBtn);

  layout->addLayout(btnLayout);

  scrollArea->setWidget(page);
  return scrollArea;
}

void ModpackExportPage::onModuleChecked(const QString& moduleId, bool checked)
{
  Q_UNUSED(moduleId)
  Q_UNUSED(checked)
  // Module selection state will be synced with Step 3's file tree in populateFileTree
}

QStringList ModpackExportPage::getSelectedModules() const
{
  QStringList selected;
  for (auto it = m_moduleChecks.begin(); it != m_moduleChecks.end(); ++it)
  {
    if (it.value()->isChecked())
    {
      selected.append(it.key());
    }
  }
  return selected;
}

void ModpackExportPage::populateFileTree(const QString& basePath, const QString& relativePath, QTreeWidgetItem* parentItem)
{
  QDir dir(basePath);
  QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);

  for (const QFileInfo& entry : entries)
  {
    QString childRelativePath = relativePath.isEmpty() ? entry.fileName() : relativePath + "/" + entry.fileName();

    // 使用 ModpackFileAdviser 判断
    modpack::FileSuggestion suggestion = modpack::ModpackFileAdviser::suggest(childRelativePath, entry.isDir());

    if (suggestion == modpack::FileSuggestion::HIDDEN)
    {
      continue;
    }

    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, entry.fileName());
    item->setData(0, Qt::UserRole, childRelativePath);

    if (entry.isDir())
    {
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
      // 递归填充子目录
      populateFileTree(entry.absoluteFilePath(), childRelativePath, item);
    }
    else
    {
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      // 显示文件大小
      qint64 size = entry.size();
      if (size < 1024)
      {
        item->setText(1, QString::number(size) + " B");
      }
      else if (size < 1024 * 1024)
      {
        item->setText(1, QString::number(size / 1024.0, 'f', 1) + " KB");
      }
      else
      {
        item->setText(1, QString::number(size / (1024.0 * 1024.0), 'f', 1) + " MB");
      }
    }

    // 设置默认勾选状态：先检查模块选择
    Qt::CheckState checkState;
    if (!m_moduleChecks.isEmpty())
    {
      QString moduleId = modpack::ModpackFileAdviser::getModuleForFile(childRelativePath);
      if (!moduleId.isEmpty())
      {
        // 文件属于某个模块，根据模块选择状态决定
        if (m_moduleChecks.contains(moduleId) && !m_moduleChecks[moduleId]->isChecked())
        {
          checkState = Qt::Unchecked;
        }
        else
        {
          checkState = (suggestion == modpack::FileSuggestion::SUGGESTED)
            ? Qt::Checked : Qt::Unchecked;
        }
      }
      else
      {
        checkState = (suggestion == modpack::FileSuggestion::SUGGESTED)
          ? Qt::Checked : Qt::Unchecked;
      }
    }
    else
    {
      checkState = (suggestion == modpack::FileSuggestion::SUGGESTED)
        ? Qt::Checked : Qt::Unchecked;
    }
    item->setCheckState(0, checkState);

    if (parentItem)
    {
      parentItem->addChild(item);
    }
    else
    {
      m_fileTree->addTopLevelItem(item);
    }
  }
}

void ModpackExportPage::updateParentCheckState(QTreeWidgetItem* item)
{
  if (!item)
  {
    return;
  }

  QTreeWidgetItem* parent = item->parent();
  if (!parent)
  {
    return;
  }

  int checkedCount = 0;
  int uncheckedCount = 0;
  for (int i = 0; i < parent->childCount(); ++i)
  {
    Qt::CheckState state = parent->child(i)->checkState(0);
    if (state == Qt::Checked)
    {
      checkedCount++;
    }
    else if (state == Qt::Unchecked)
    {
      uncheckedCount++;
    }
  }

  if (checkedCount == parent->childCount())
  {
    parent->setCheckState(0, Qt::Checked);
  }
  else if (uncheckedCount == parent->childCount())
  {
    parent->setCheckState(0, Qt::Unchecked);
  }
  else
  {
    parent->setCheckState(0, Qt::PartiallyChecked);
  }

  updateParentCheckState(parent);
}

QStringList ModpackExportPage::collectSelectedFiles()
{
  QStringList files;

  std::function<void(QTreeWidgetItem*)> traverse = [&](QTreeWidgetItem* item) {
    if (item->childCount() == 0)
    {
      // 叶子节点（文件）
      if (item->checkState(0) == Qt::Checked)
      {
        files.append(item->data(0, Qt::UserRole).toString());
      }
    }
    else
    {
      // 目录节点：递归遍历
      for (int i = 0; i < item->childCount(); ++i)
      {
        traverse(item->child(i));
      }
    }
  };

  for (int i = 0; i < m_fileTree->topLevelItemCount(); ++i)
  {
    traverse(m_fileTree->topLevelItem(i));
  }

  return files;
}

bool ModpackExportPage::validateStep2()
{
  if (m_nameEdit->text().trimmed().isEmpty())
  {
    AppMessageBox::warning(this, tr("验证失败"), tr("请输入整合包名称"));
    m_nameEdit->setFocus();
    return false;
  }

  if (m_authorEdit->text().trimmed().isEmpty())
  {
    AppMessageBox::warning(this, tr("验证失败"), tr("请输入作者名称"));
    m_authorEdit->setFocus();
    return false;
  }

  if (m_versionEdit->text().trimmed().isEmpty())
  {
    AppMessageBox::warning(this, tr("验证失败"), tr("请输入版本号"));
    m_versionEdit->setFocus();
    return false;
  }

  if (m_savePathEdit->text().trimmed().isEmpty())
  {
    AppMessageBox::warning(this, tr("验证失败"), tr("请选择保存路径"));
    m_savePathEdit->setFocus();
    return false;
  }

  if (m_selectedFormat == "mcbbs")
  {
    if (m_fileApiEdit->text().trimmed().isEmpty())
    {
      AppMessageBox::warning(this, tr("验证失败"), tr("请输入文件 API 地址"));
      m_fileApiEdit->setFocus();
      return false;
    }

    if (m_urlEdit->text().trimmed().isEmpty())
    {
      AppMessageBox::warning(this, tr("验证失败"), tr("请输入项目主页 URL"));
      m_urlEdit->setFocus();
      return false;
    }
  }

  return true;
}

void ModpackExportPage::onFormatCardClicked(int id)
{
  switch (id)
  {
  case 0: m_selectedFormat = "blockbox"; break;
  case 1: m_selectedFormat = "curseforge"; break;
  case 2: m_selectedFormat = "packwiz"; break;
  case 3: m_selectedFormat = "mcbbs"; break;
  case 4: m_selectedFormat = "multimc"; break;
  case 5: m_selectedFormat = "server"; break;
  case 6: m_selectedFormat = "modrinth"; break;
  default: m_selectedFormat = "blockbox"; break;
  }

  if (m_nextBtn)
  {
    m_nextBtn->setEnabled(true);
  }
}

void ModpackExportPage::onPrevClicked()
{
  if (m_currentStep > 0)
  {
    // 非 BlockBox 格式从步骤 3 返回时跳过步骤 2.5
    if (m_currentStep == 3 && m_selectedFormat != "blockbox")
    {
      m_currentStep = 1;
    }
    else
    {
      m_currentStep--;
    }

    m_stepStack->setCurrentIndex(m_currentStep);

    m_nextBtn->setText(tr("下一步"));
    m_nextBtn->show();
    m_exportBtn->hide();
    m_prevBtn->setEnabled(m_currentStep > 0);
  }
}

void ModpackExportPage::onNextClicked()
{
  if (m_currentStep == 0)
  {
    // 从步骤1到步骤2：更新步骤2字段
    updateStep2Fields();
    m_currentStep = 1;
    m_stepStack->setCurrentIndex(1);
    m_prevBtn->setEnabled(true);
  }
  else if (m_currentStep == 1)
  {
    // 从步骤2：验证
    if (!validateStep2())
    {
      return;
    }

    if (m_selectedFormat == "blockbox")
    {
      // BlockBox 格式：进入步骤 2.5 模块选择
      m_currentStep = 2;
      m_stepStack->setCurrentIndex(2);
      m_nextBtn->setText(tr("下一步"));
      m_nextBtn->show();
      m_exportBtn->hide();
      m_prevBtn->setEnabled(true);
    }
    else
    {
      // 其他格式：直接进入步骤 3 文件选择
      m_currentStep = 3;
      m_stepStack->setCurrentIndex(3);
      m_nextBtn->hide();
      m_exportBtn->show();
      m_prevBtn->setEnabled(true);
    }
  }
  else if (m_currentStep == 2)
  {
    // 从步骤 2.5 到步骤 3：文件选择
    m_currentStep = 3;
    m_stepStack->setCurrentIndex(3);
    m_nextBtn->hide();
    m_exportBtn->show();
    m_prevBtn->setEnabled(true);
  }
}

void ModpackExportPage::onBrowseSavePath()
{
  QString defaultName = m_nameEdit->text().trimmed();
  if (defaultName.isEmpty() && !m_instancePath.isEmpty())
  {
    defaultName = QFileInfo(m_instancePath).fileName();
  }

  QString ext;
  QString filter;
  if (m_selectedFormat == "modrinth")
  {
    ext = ".mrpack";
    filter = tr("Modrinth 整合包 (*.mrpack)");
  }
  else if (m_selectedFormat == "blockbox")
  {
    ext = ".blockbox";
    filter = tr("方块盒子 整合包 (*.blockbox)");
  }
  else if (m_selectedFormat == "packwiz")
  {
    // Packwiz 格式使用目录选择对话框
    QString dirPath = AppFileDialog::getExistingDirectory(
      this,
      tr("选择保存目录"),
      QDir::homePath() + "/" + defaultName
    );
    if (!dirPath.isEmpty())
    {
      m_savePathEdit->setText(dirPath);
    }
    return;
  }
  else if (m_selectedFormat == "curseforge")
  {
    ext = ".zip";
    filter = tr("CurseForge 整合包 (*.zip)");
  }
  else
  {
    ext = ".zip";
    filter = tr("ZIP 整合包 (*.zip)");
  }

  QString filePath = AppFileDialog::getSaveFileName(
    this,
    tr("选择保存路径"),
    QDir::homePath() + "/" + defaultName + ext,
    filter
  );

  if (!filePath.isEmpty())
  {
    m_savePathEdit->setText(filePath);
  }
}

void ModpackExportPage::onMinMemorySliderChanged(int value)
{
  if (m_minMemoryEdit)
  {
    m_minMemoryEdit->setText(QString::number(value));
  }
}

void ModpackExportPage::onExportClicked()
{
  if (m_instancePath.isEmpty())
  {
    AppMessageBox::warning(this, tr("错误"), tr("未设置实例路径"));
    return;
  }

  QStringList files = collectSelectedFiles();
  if (files.isEmpty())
  {
    AppMessageBox::warning(this, tr("错误"), tr("请至少选择一个文件导出"));
    return;
  }

  QString outputPath = m_savePathEdit->text().trimmed();
  if (outputPath.isEmpty())
  {
    AppMessageBox::warning(this, tr("错误"), tr("请指定保存路径"));
    return;
  }

  // 禁用导出按钮，显示进度
  m_exportBtn->setEnabled(false);
  m_prevBtn->setEnabled(false);
  m_progressBar->setValue(0);
  m_progressBar->show();
  m_progressLabel->setText(tr("正在准备导出..."));
  m_progressLabel->show();

  // 创建导出器
  modpack::ModpackExportInfo exportInfo;
  exportInfo.name = m_nameEdit->text().trimmed();
  exportInfo.version = m_versionEdit->text().trimmed();
  exportInfo.author = m_authorEdit->text().trimmed();
  exportInfo.description = m_descEdit->toPlainText().trimmed();

  modpack::ExportFormat exportFormat = modpack::ExportFormat::Mcbbs;

  // 设置格式
  if (m_selectedFormat == "mcbbs")
  {
    exportFormat = modpack::ExportFormat::Mcbbs;
    exportInfo.fileApi = m_fileApiEdit->text().trimmed();
    exportInfo.url = m_urlEdit->text().trimmed();
    exportInfo.minMemory = m_minMemorySlider->value();
    exportInfo.javaArgs = m_javaArgsEdit->text().trimmed();
    exportInfo.launchArgs = m_launchArgsEdit->text().trimmed();
  }
  else if (m_selectedFormat == "curseforge")
  {
    exportFormat = modpack::ExportFormat::CurseForge;
    exportInfo.url = m_urlEdit->text().trimmed();
  }
  else if (m_selectedFormat == "multimc")
  {
    exportFormat = modpack::ExportFormat::MultiMC;
  }
  else if (m_selectedFormat == "server")
  {
    exportFormat = modpack::ExportFormat::Server;
    exportInfo.fileApi = m_serverFileApiEdit->text().trimmed();
    exportInfo.minMemory = m_serverMinMemorySlider->value();
    exportInfo.javaArgs = m_serverJavaArgsEdit->text().trimmed();
    exportInfo.launchArgs = m_serverLaunchArgsEdit->text().trimmed();
  }
  else if (m_selectedFormat == "modrinth")
  {
    exportFormat = modpack::ExportFormat::Modrinth;
  }
  else if (m_selectedFormat == "packwiz")
  {
    exportFormat = modpack::ExportFormat::Packwiz;
  }
  else if (m_selectedFormat == "blockbox")
  {
    exportFormat = modpack::ExportFormat::BlockBox;
    exportInfo.selectedModules = getSelectedModules();
  }

  auto* exporter = new modpack::ModpackExporter(
    m_instancePath,
    exportFormat,
    exportInfo,
    files,
    outputPath,
    this
  );

  connect(exporter, &modpack::ModpackExporter::exportProgressChanged, this,
    [this](int percent, const QString& stage)
    {
      m_progressLabel->setText(stage);
      m_progressBar->setValue(percent);
    });

  connect(exporter, &modpack::ModpackExporter::exportFinished, this,
    [this, outputPath](bool success, const QString& /* exportPath */, const QString& errorMessage)
    {
      m_exportBtn->setEnabled(true);
      m_prevBtn->setEnabled(true);
      m_progressBar->hide();
      m_progressLabel->hide();

      if (success)
      {
        AppMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("导出成功"));
        msgBox.setText(tr("整合包已成功导出到:\n%1").arg(outputPath));
        msgBox.setIcon(AppMessageBox::Information);

        QPushButton* openBtn = msgBox.addButton(tr("打开文件位置"), AppMessageBox::AcceptRole);
        msgBox.addButton(tr("确定"), AppMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == openBtn)
        {
          QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputPath).absolutePath()));
        }

        NotificationManager::showSuccess(this, tr("整合包导出成功"));
        emit exportFinished();
        emit backRequested();
      }
      else
      {
        AppMessageBox::warning(this, tr("导出失败"), errorMessage.isEmpty() ? tr("导出失败") : errorMessage);
      }
    });

  exporter->startExport();
}