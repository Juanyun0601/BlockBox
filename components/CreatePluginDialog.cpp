/**
 * @file   CreatePluginDialog.cpp
 * @brief  制作插件对话框类实现
 * @author BlockBox Team
 * @date   2026-08-05
 */
#include "CreatePluginDialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

#include "components/AppFileDialog.h"
#include "components/AppMessageBox.h"
#include "utils/ThemeManager.h"
#include "utils/plugin/PluginManager.h"

CreatePluginDialog::CreatePluginDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_formPage(nullptr)
    , m_resultPage(nullptr)
    , m_nameEdit(nullptr)
    , m_idEdit(nullptr)
    , m_versionEdit(nullptr)
    , m_authorEdit(nullptr)
    , m_descEdit(nullptr)
    , m_dirEdit(nullptr)
    , m_browseBtn(nullptr)
    , m_generateBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_resultIconLabel(nullptr)
    , m_resultTextLabel(nullptr)
    , m_packageBtn(nullptr)
    , m_openDirBtn(nullptr)
    , m_doneBtn(nullptr)
    , m_backBtn(nullptr)
    , m_stack(nullptr)
{
    setObjectName(QStringLiteral("createPluginDialog"));
    setWindowTitle(tr("制作插件"));

    initUI();
    initStyle();
}

void CreatePluginDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumWidth(440);
    card->setMaximumWidth(560);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(26, 22, 26, 20);
    cardLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel(tr("制作插件"), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));

    cardLayout->addWidget(titleLabel);
    cardLayout->addSpacing(2);

    // ========== 表单页 ==========
    m_formPage = new QWidget(card);
    QVBoxLayout *formLayout = new QVBoxLayout(m_formPage);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(10);

    auto addField = [this, formLayout](QLabel **labelOut, QLineEdit **editOut,
                                       const QString &labelText, const QString &placeholder)
    {
        QLabel *lbl = new QLabel(labelText, m_formPage);
        lbl->setObjectName(QStringLiteral("createPluginFieldLabel"));
        if (labelOut) *labelOut = lbl;

        QLineEdit *edit = new QLineEdit(m_formPage);
        edit->setObjectName(QStringLiteral("appDialogLineEdit"));
        edit->setPlaceholderText(placeholder);
        if (editOut) *editOut = edit;

        formLayout->addWidget(lbl);
        formLayout->addWidget(edit);
    };

    addField(nullptr, &m_nameEdit, tr("插件名称"), tr("例如：快捷工具栏"));
    addField(nullptr, &m_idEdit, tr("插件 ID（英文/数字/下划线）"), tr("例如：quick_toolbar"));
    addField(nullptr, &m_versionEdit, tr("版本号"), tr("1.0.0"));
    addField(nullptr, &m_authorEdit, tr("作者"), tr("你的名字"));

    QLabel *descLabel = new QLabel(tr("描述"), m_formPage);
    descLabel->setObjectName(QStringLiteral("createPluginFieldLabel"));
    m_descEdit = new QTextEdit(m_formPage);
    m_descEdit->setObjectName(QStringLiteral("createPluginDescEdit"));
    m_descEdit->setPlaceholderText(tr("一句话介绍这个插件做什么"));
    m_descEdit->setFixedHeight(64);
    formLayout->addWidget(descLabel);
    formLayout->addWidget(m_descEdit);

    QLabel *dirLabel = new QLabel(tr("模板保存位置"), m_formPage);
    dirLabel->setObjectName(QStringLiteral("createPluginFieldLabel"));
    formLayout->addWidget(dirLabel);

    QHBoxLayout *dirRow = new QHBoxLayout();
    dirRow->setSpacing(8);
    m_dirEdit = new QLineEdit(m_formPage);
    m_dirEdit->setObjectName(QStringLiteral("appDialogLineEdit"));
    m_dirEdit->setReadOnly(true);
    m_dirEdit->setText(QDir::homePath());
    m_browseBtn = new QPushButton(tr("浏览…"), m_formPage);
    m_browseBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(m_browseBtn);
    formLayout->addLayout(dirRow);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    btnRow->addStretch();

    m_cancelBtn = new QPushButton(tr("取消"), m_formPage);
    m_cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);

    m_generateBtn = new QPushButton(tr("生成模板"), m_formPage);
    m_generateBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_generateBtn->setCursor(Qt::PointingHandCursor);
    m_generateBtn->setDefault(true);

    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_generateBtn);
    formLayout->addLayout(btnRow);

    // ========== 结果页 ==========
    m_resultPage = new QWidget(card);
    QVBoxLayout *resultLayout = new QVBoxLayout(m_resultPage);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(12);

    m_resultIconLabel = new QLabel(m_resultPage);
    m_resultIconLabel->setObjectName(QStringLiteral("createPluginResultIcon"));
    m_resultIconLabel->setAlignment(Qt::AlignCenter);
    m_resultIconLabel->setFixedHeight(56);

    m_resultTextLabel = new QLabel(m_resultPage);
    m_resultTextLabel->setObjectName(QStringLiteral("createPluginResultText"));
    m_resultTextLabel->setWordWrap(true);
    m_resultTextLabel->setAlignment(Qt::AlignCenter);

    m_packageBtn = new QPushButton(tr("打包为 BlockBox 文件"), m_resultPage);
    m_packageBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_packageBtn->setCursor(Qt::PointingHandCursor);

    m_openDirBtn = new QPushButton(tr("打开模板文件夹"), m_resultPage);
    m_openDirBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_openDirBtn->setCursor(Qt::PointingHandCursor);

    m_backBtn = new QPushButton(tr("继续制作"), m_resultPage);
    m_backBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_backBtn->setCursor(Qt::PointingHandCursor);

    m_doneBtn = new QPushButton(tr("完成"), m_resultPage);
    m_doneBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_doneBtn->setCursor(Qt::PointingHandCursor);
    m_doneBtn->setDefault(true);

    resultLayout->addWidget(m_resultIconLabel);
    resultLayout->addWidget(m_resultTextLabel);
    resultLayout->addSpacing(4);
    resultLayout->addWidget(m_packageBtn);
    resultLayout->addWidget(m_openDirBtn);
    resultLayout->addWidget(m_backBtn);
    resultLayout->addWidget(m_doneBtn);

    m_stack = new QStackedWidget(card);
    m_stack->addWidget(m_formPage);
    m_stack->addWidget(m_resultPage);
    cardLayout->addWidget(m_stack);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(card, 0, 0, Qt::AlignCenter);

    // 连接
    connect(m_browseBtn, &QPushButton::clicked, this, &CreatePluginDialog::onBrowseDir);
    connect(m_generateBtn, &QPushButton::clicked, this, &CreatePluginDialog::onGenerate);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_packageBtn, &QPushButton::clicked, this, &CreatePluginDialog::onPackage);
    connect(m_openDirBtn, &QPushButton::clicked, this, &CreatePluginDialog::onOpenTemplateDir);
    connect(m_backBtn, &QPushButton::clicked, this, &CreatePluginDialog::onBackToForm);
    connect(m_doneBtn, &QPushButton::clicked, this, &QDialog::accept);

    // 名称变化时自动补全 ID（用户未手动修改时）
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_idEdit && m_idEdit->text().trimmed().isEmpty())
            validateIdFromName(text);
    });
    connect(m_versionEdit, &QLineEdit::returnPressed, this, &CreatePluginDialog::onGenerate);
}

void CreatePluginDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";
    const QString fieldBg    = isLight ? "#f5f5f5" : "#3b3b40";
    const QString fieldBorder = isLight ? "#d4d4d4" : "#55555a";
    const QString btnText    = isLight ? "#333333" : "#e8e8e8";

    const QString style = QString(
        "QWidget#appDialogCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 16px;"
        "}"
        "QLabel#appDialogTitleLabel {"
        "    color: %3;"
        "    background-color: transparent;"
        "    font-size: 15px;"
        "    font-weight: bold;"
        "}"
        "QPushButton#appDialogBtn {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 12px;"
        "    color: %5;"
        "    padding: 7px 22px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtn:hover {"
        "    background-color: rgba(0,0,0,28);"
        "}"
        "QPushButton#appDialogBtnPrimary {"
        "    background-color: %6;"
        "    border: 1px solid %6;"
        "    border-radius: 12px;"
        "    color: #ffffff;"
        "    padding: 7px 26px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtnPrimary:hover {"
        "    background-color: %9;"
        "    border-color: %9;"
        "}"
        "QLabel#createPluginFieldLabel {"
        "    color: %4;"
        "    font-size: 13px;"
        "    font-weight: 600;"
        "    background-color: transparent;"
        "}"
        "QLineEdit#appDialogLineEdit {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 8px;"
        "    padding: 6px 10px;"
        "    color: %4;"
        "    font-size: 13px;"
        "}"
        "QLineEdit#appDialogLineEdit:focus {"
        "    border: 1px solid %6;"
        "}"
        "QTextEdit#createPluginDescEdit {"
        "    background-color: %7;"
        "    border: 1px solid %8;"
        "    border-radius: 8px;"
        "    padding: 6px 10px;"
        "    color: %4;"
        "    font-size: 13px;"
        "}"
        "QTextEdit#createPluginDescEdit:focus {"
        "    border: 1px solid %6;"
        "}"
        "QLabel#createPluginResultIcon {"
        "    font-size: 30px;"
        "    background-color: transparent;"
        "}"
        "QLabel#createPluginResultText {"
        "    color: %4;"
        "    font-size: 13px;"
        "    background-color: transparent;"
        "}")
        .arg(cardBg, cardBorder, titleColor, textColor)
        .arg(btnText, themeColor, fieldBg, fieldBorder)
        .arg(themeHover);

    setStyleSheet(style);
}

void CreatePluginDialog::validateIdFromName(const QString &name)
{
    QString id;
    for (const QChar &c : name.trimmed()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-'))
            id.append(c);
    }
    if (!id.isEmpty())
        m_idEdit->setText(id.toLower());
}

void CreatePluginDialog::onBrowseDir()
{
    const QString dir = AppFileDialog::getExistingDirectory(
        this, tr("选择模板保存位置"), m_dirEdit->text());
    if (!dir.isEmpty())
        m_dirEdit->setText(dir);
}

void CreatePluginDialog::onGenerate()
{
    const QString name = m_nameEdit->text().trimmed();
    QString id = m_idEdit->text().trimmed();
    const QString version = m_versionEdit->text().trimmed();
    const QString author = m_authorEdit->text().trimmed();
    const QString description = m_descEdit->toPlainText().trimmed();
    const QString parentDir = m_dirEdit->text().trimmed();

    if (name.isEmpty()) {
        AppMessageBox::warning(this, tr("提示"), tr("请填写插件名称"));
        return;
    }
    if (id.isEmpty()) {
        // 未填写 ID 时由名称自动生成
        QString generated;
        for (const QChar &c : name) {
            if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-'))
                generated.append(c);
        }
        id = generated.isEmpty() ? QStringLiteral("my_plugin") : generated.toLower();
    }
    if (parentDir.isEmpty() || !QDir(parentDir).exists()) {
        AppMessageBox::warning(this, tr("提示"), tr("请选择有效的模板保存位置"));
        return;
    }

    QString outDir;
    QString error;
    if (!PluginManager::instance()->createTemplate(name, id, version, author, description,
                                                   parentDir, outDir, &error)) {
        AppMessageBox::warning(this, tr("生成失败"), error);
        return;
    }

    m_templateDir = outDir;
    showResultView(tr("插件项目模板已生成：\n%1\n\n可在模板目录中编辑代码，"
                      "然后点击下方按钮打包为 .BlockBox 插件文件。").arg(outDir), true);
}

void CreatePluginDialog::showResultView(const QString &msg, bool withPackageBtn)
{
    m_resultTextLabel->setText(msg);
    m_resultIconLabel->setText(withPackageBtn ? QStringLiteral("✅") : QStringLiteral("📦"));
    m_packageBtn->setVisible(withPackageBtn);
    m_stack->setCurrentWidget(m_resultPage);
    adjustSize();
}

void CreatePluginDialog::onPackage()
{
    if (m_templateDir.isEmpty())
        return;

    QString outPath;
    QString error;
    if (!PluginManager::instance()->packageToBlockBox(m_templateDir, outPath, &error)) {
        AppMessageBox::warning(this, tr("打包失败"), error);
        return;
    }

    m_packagedPath = outPath;
    m_packaged = true;
    showResultView(tr("打包成功！\n已生成 .BlockBox 插件文件并安装到插件目录：\n%1\n"
                      "可在左侧插件列表中看到该插件。").arg(outPath), false);
}

void CreatePluginDialog::onOpenTemplateDir()
{
    if (m_templateDir.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_templateDir));
}

void CreatePluginDialog::onBackToForm()
{
    m_stack->setCurrentWidget(m_formPage);
    adjustSize();
}
