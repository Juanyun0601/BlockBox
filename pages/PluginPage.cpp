/**
 * @file   PluginPage.cpp
 * @brief  插件页面类实现
 * @author BlockBox Team
 * @date   2026-08-05
 */
#include "PluginPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSpacerItem>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/PluginRunnerDialog.h"
#include "components/ProjectionBlockEditorDialog.h"
#include "layouts/FlowLayout.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/plugin/PluginManager.h"
#include "utils/plugin/PluginZip.h"
#include "utils/plugin/ServerStatusChecker.h"

PluginPage::PluginPage(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(nullptr)
    , m_countLabel(nullptr)
    , m_importBtn(nullptr)
    , m_createBtn(nullptr)
    , m_openDirBtn(nullptr)
    , m_stack(nullptr)
    , m_overviewScroll(nullptr)
    , m_overviewContent(nullptr)
    , m_cardsLayout(nullptr)
    , m_emptyState(nullptr)
    , m_emptyLabel(nullptr)
    , m_detailPage(nullptr)
    , m_detailScroll(nullptr)
    , m_detailIcon(nullptr)
    , m_detailName(nullptr)
    , m_detailMeta(nullptr)
    , m_detailDesc(nullptr)
    , m_detailPath(nullptr)
    , m_detailStyle(nullptr)
    , m_detailWarn(nullptr)
    , m_enableBtn(nullptr)
    , m_settingsBtn(nullptr)
    , m_updateBtn(nullptr)
    , m_exportBtn(nullptr)
    , m_extractBtn(nullptr)
    , m_homeBtn(nullptr)
    , m_removeBtn(nullptr)
    , m_openFileBtn(nullptr)
    , m_commandsRow(nullptr)
    , m_uiSection(nullptr)
    , m_uiFormLayout(nullptr)
    , m_uiActionRow(nullptr)
    , m_uiResult(nullptr)
    , m_uiProc(nullptr)
    , m_settingsPage(nullptr)
    , m_settingsScroll(nullptr)
    , m_settingsTitle(nullptr)
    , m_settingsFormLayout(nullptr)
    , m_settingsResetBtn(nullptr)
    , m_settingsSaveBtn(nullptr)
{
    initUI();

    // 服务器列表检测器与自动刷新定时器
    m_statusChecker = new ServerStatusChecker(this);
    connect(m_statusChecker, &ServerStatusChecker::statusReady,
            this, &PluginPage::onServerStatusReady);
    connect(m_statusChecker, &ServerStatusChecker::allDone,
            this, &PluginPage::onServerCheckAllDone);

    m_serverRefreshTimer = new QTimer(this);
    m_serverRefreshTimer->setInterval(120000); // 120s 自动刷新
    connect(m_serverRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_serverChecking)
            onServerRefreshClicked();
    });

    connect(PluginManager::instance(), &PluginManager::pluginsChanged,
            this, &PluginPage::refreshList);
    connect(PluginManager::instance(), &PluginManager::pluginEnabledChanged,
            this, [this](const QString &id, bool) {
        // 启用状态变化时刷新详情页按钮状态与卡片
        rebuildCards();
        if (m_currentDetailIndex >= 0 && m_currentDetailIndex < m_plugins.size()
                && m_plugins[m_currentDetailIndex].id == id) {
            updateDetailView(m_plugins[m_currentDetailIndex]);
        }
    });

    // 首次进入即加载插件列表
    refreshList();
}

void PluginPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== 顶部操作栏 ==========
    QWidget *header = new QWidget(this);
    header->setObjectName(QStringLiteral("pluginPageHeader"));
    header->setFixedHeight(64);

    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 12, 24, 12);
    headerLayout->setSpacing(10);

    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(tr("插件"), header);
    m_titleLabel->setObjectName(QStringLiteral("pluginPageTitle"));
    titleLayout->addWidget(m_titleLabel);

    m_countLabel = new QLabel(header);
    m_countLabel->setObjectName(QStringLiteral("pluginPageCount"));
    titleLayout->addWidget(m_countLabel);

    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    m_importBtn = new QPushButton(tr("导入插件"), header);
    m_createBtn = new QPushButton(tr("制作插件"), header);
    m_openDirBtn = new QPushButton(tr("插件文件夹"), header);

    for (QPushButton *btn : {m_importBtn, m_createBtn, m_openDirBtn}) {
        btn->setObjectName(QStringLiteral("pluginPageActionBtn"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(32);
    }
    m_createBtn->setObjectName(QStringLiteral("pluginPageActionBtnPrimary"));

    headerLayout->addWidget(m_importBtn);
    headerLayout->addWidget(m_createBtn);
    headerLayout->addWidget(m_openDirBtn);

    mainLayout->addWidget(header);

    // ========== 内容区 ==========
    m_stack = new QStackedWidget(this);

    // --- 总览页 ---
    m_overviewScroll = new QScrollArea(m_stack);
    m_overviewScroll->setWidgetResizable(true);
    m_overviewScroll->setFrameShape(QFrame::NoFrame);
    m_overviewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_overviewContent = new QWidget(m_overviewScroll);
    m_overviewContent->setObjectName(QStringLiteral("pluginOverviewContent"));
    // 垂直布局：卡片置顶向下排列（不随容器高度均匀拉伸）
    m_cardsLayout = new QVBoxLayout(m_overviewContent);
    m_cardsLayout->setContentsMargins(14, 4, 14, 4);
    m_cardsLayout->setSpacing(8);
    m_overviewScroll->setWidget(m_overviewContent);
    m_stack->addWidget(m_overviewScroll);

    // 空状态
    m_emptyState = new QWidget(m_stack);
    QVBoxLayout *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(24, 24, 24, 24);
    m_emptyLabel = new QLabel(tr("还没有安装任何插件\n\n点击上方「导入插件」选择 .BlockBox 插件文件，\n或点击「制作插件」创建你的第一个插件"), m_emptyState);
    m_emptyLabel->setObjectName(QStringLiteral("pluginPageEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyLabel);
    m_stack->addWidget(m_emptyState);

    // --- 详情页 ---
    m_detailPage = new QWidget(m_stack);
    m_detailPage->setObjectName(QStringLiteral("pluginDetailPage"));
    QVBoxLayout *detailLayout = new QVBoxLayout(m_detailPage);
    detailLayout->setContentsMargins(24, 16, 24, 24);
    detailLayout->setSpacing(12);

    QWidget *detailCard = new QWidget(m_detailPage);
    detailCard->setObjectName(QStringLiteral("pluginDetailCard"));
    QVBoxLayout *cardLayout = new QVBoxLayout(detailCard);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(12);

    QHBoxLayout *iconRow = new QHBoxLayout();
    iconRow->setSpacing(16);
    m_detailIcon = new QLabel(detailCard);
    m_detailIcon->setFixedSize(72, 72);
    m_detailIcon->setAlignment(Qt::AlignCenter);
    m_detailIcon->setObjectName(QStringLiteral("pluginDetailIcon"));
    iconRow->addWidget(m_detailIcon);

    QVBoxLayout *nameLayout = new QVBoxLayout();
    nameLayout->setSpacing(4);
    m_detailName = new QLabel(detailCard);
    m_detailName->setObjectName(QStringLiteral("pluginDetailName"));
    m_detailMeta = new QLabel(detailCard);
    m_detailMeta->setObjectName(QStringLiteral("pluginDetailMeta"));
    m_detailMeta->setWordWrap(true);
    nameLayout->addWidget(m_detailName);
    nameLayout->addWidget(m_detailMeta);
    iconRow->addLayout(nameLayout, 1);
    cardLayout->addLayout(iconRow);

    m_detailWarn = new QLabel(detailCard);
    m_detailWarn->setObjectName(QStringLiteral("pluginDetailWarn"));
    m_detailWarn->setWordWrap(true);
    m_detailWarn->setVisible(false);
    cardLayout->addWidget(m_detailWarn);

    m_detailDesc = new QLabel(detailCard);
    m_detailDesc->setObjectName(QStringLiteral("pluginDetailDesc"));
    m_detailDesc->setWordWrap(true);
    cardLayout->addWidget(m_detailDesc);

    m_detailPath = new QLabel(detailCard);
    m_detailPath->setObjectName(QStringLiteral("pluginDetailPath"));
    m_detailPath->setWordWrap(true);
    cardLayout->addWidget(m_detailPath);

    // 插件样式贡献提示（manifest.style）
    m_detailStyle = new QLabel(detailCard);
    m_detailStyle->setObjectName(QStringLiteral("pluginDetailStyle"));
    m_detailStyle->setWordWrap(true);
    m_detailStyle->setVisible(false);
    cardLayout->addWidget(m_detailStyle);

    // 操作按钮行（固定靠左，不随页面切换右移）
    QHBoxLayout *detailBtnRow = new QHBoxLayout();
    detailBtnRow->setSpacing(10);

    m_enableBtn = new QPushButton(detailCard);
    m_enableBtn->setObjectName(QStringLiteral("pluginPageActionBtn"));
    m_enableBtn->setCursor(Qt::PointingHandCursor);

    m_settingsBtn = new QPushButton(tr("插件设置"), detailCard);
    m_settingsBtn->setObjectName(QStringLiteral("pluginPageActionBtn"));
    m_settingsBtn->setCursor(Qt::PointingHandCursor);

    m_updateBtn = new QPushButton(tr("检查更新"), detailCard);
    m_updateBtn->setObjectName(QStringLiteral("pluginPageActionBtn"));
    m_updateBtn->setCursor(Qt::PointingHandCursor);

    m_exportBtn = new QPushButton(tr("导出插件"), detailCard);
    m_exportBtn->setObjectName(QStringLiteral("pluginPageActionBtn"));
    m_exportBtn->setCursor(Qt::PointingHandCursor);

    m_extractBtn = new QPushButton(tr("查看源码"), detailCard);
    m_extractBtn->setObjectName(QStringLiteral("pluginPageActionBtn"));
    m_extractBtn->setCursor(Qt::PointingHandCursor);

    m_homeBtn = new QPushButton(tr("主页"), detailCard);
    m_homeBtn->setObjectName(QStringLiteral("pluginPageActionBtn"));
    m_homeBtn->setCursor(Qt::PointingHandCursor);

    m_removeBtn = new QPushButton(tr("删除插件"), detailCard);
    m_removeBtn->setObjectName(QStringLiteral("pluginPageDangerBtn"));
    m_removeBtn->setCursor(Qt::PointingHandCursor);

    m_openFileBtn = new QPushButton(tr("打开所在文件夹"), detailCard);
    m_openFileBtn->setObjectName(QStringLiteral("pluginPageActionBtn"));
    m_openFileBtn->setCursor(Qt::PointingHandCursor);

    for (QPushButton *btn : {m_enableBtn, m_settingsBtn, m_updateBtn, m_exportBtn,
                             m_extractBtn, m_homeBtn, m_openFileBtn}) {
        btn->setFixedHeight(30);
    }
    m_removeBtn->setFixedHeight(30);

    detailBtnRow->addWidget(m_enableBtn);
    detailBtnRow->addWidget(m_settingsBtn);
    detailBtnRow->addWidget(m_updateBtn);
    detailBtnRow->addWidget(m_exportBtn);
    detailBtnRow->addWidget(m_extractBtn);
    detailBtnRow->addWidget(m_homeBtn);
    detailBtnRow->addWidget(m_removeBtn);
    detailBtnRow->addWidget(m_openFileBtn);
    cardLayout->addLayout(detailBtnRow);

    detailLayout->addWidget(detailCard);

    // 插件命令/动作（直接展示在详情卡片下方，不再有独立"动作"区域）
    // 使用 QVBoxLayout 作为容器：分类标题(QLabel)独占一行，每组按钮用 FlowLayout 自动换行
    m_commandsRow = new QWidget(m_detailPage);
    m_commandsRow->setObjectName(QStringLiteral("pluginCommandsRow"));
    QVBoxLayout *cmdRowLayout = new QVBoxLayout(m_commandsRow);
    cmdRowLayout->setContentsMargins(0, 0, 0, 0);
    cmdRowLayout->setSpacing(12);
    m_commandsRow->setVisible(false);
    detailLayout->addWidget(m_commandsRow);

    // 插件内嵌界面（manifest.ui：表单 + 动作按钮 + 结果区，不弹独立窗口）
    m_uiSection = new QWidget(m_detailPage);
    m_uiSection->setObjectName(QStringLiteral("pluginUiSection"));
    QVBoxLayout *uiLayout = new QVBoxLayout(m_uiSection);
    uiLayout->setContentsMargins(18, 16, 18, 16);
    uiLayout->setSpacing(12);

    QLabel *uiTitle = new QLabel(tr("插件界面"), m_uiSection);
    uiTitle->setObjectName(QStringLiteral("pluginSettingsTitle"));
    uiLayout->addWidget(uiTitle);

    m_uiFormLayout = new QFormLayout();
    m_uiFormLayout->setContentsMargins(0, 0, 0, 0);
    m_uiFormLayout->setSpacing(12);
    m_uiFormLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_uiFormLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    uiLayout->addLayout(m_uiFormLayout);

    m_uiActionRow = new QHBoxLayout();
    m_uiActionRow->setSpacing(8);
    m_uiActionRow->setContentsMargins(0, 0, 0, 0);
    uiLayout->addLayout(m_uiActionRow);

    m_uiResult = new QPlainTextEdit(m_uiSection);
    m_uiResult->setObjectName(QStringLiteral("pluginUiResult"));
    m_uiResult->setReadOnly(true);
    m_uiResult->setFixedHeight(120);
    m_uiResult->setPlaceholderText(tr("插件执行结果将显示在这里"));
    uiLayout->addWidget(m_uiResult);

    m_uiSection->setVisible(false);
    detailLayout->addWidget(m_uiSection);

    detailLayout->addStretch();

    // 详情页放入滚动容器
    m_detailScroll = new QScrollArea(m_stack);
    m_detailScroll->setWidgetResizable(true);
    m_detailScroll->setFrameShape(QFrame::NoFrame);
    m_detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailScroll->setWidget(m_detailPage);
    m_stack->addWidget(m_detailScroll);

    // --- 插件设置界面（点击"插件设置"进入的新界面，非弹窗） ---
    m_settingsPage = new QWidget(m_stack);
    m_settingsPage->setObjectName(QStringLiteral("pluginDetailPage"));
    QVBoxLayout *settingsPageLayout = new QVBoxLayout(m_settingsPage);
    settingsPageLayout->setContentsMargins(24, 16, 24, 24);
    settingsPageLayout->setSpacing(12);

    QWidget *settingsCard = new QWidget(m_settingsPage);
    settingsCard->setObjectName(QStringLiteral("pluginSettingsSection"));
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsCard);
    settingsLayout->setContentsMargins(24, 20, 24, 20);
    settingsLayout->setSpacing(12);
    m_settingsTitle = new QLabel(tr("插件设置"), settingsCard);
    m_settingsTitle->setObjectName(QStringLiteral("pluginSettingsTitle"));
    settingsLayout->addWidget(m_settingsTitle);
    m_settingsFormLayout = new QFormLayout();
    m_settingsFormLayout->setContentsMargins(0, 0, 0, 0);
    m_settingsFormLayout->setSpacing(12);
    m_settingsFormLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_settingsFormLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    settingsLayout->addLayout(m_settingsFormLayout);

    QHBoxLayout *settingsBtnRow = new QHBoxLayout();
    settingsBtnRow->setSpacing(10);
    settingsBtnRow->addStretch();
    m_settingsResetBtn = new QPushButton(tr("恢复默认"), settingsCard);
    m_settingsResetBtn->setObjectName(QStringLiteral("pluginPageActionBtn"));
    m_settingsResetBtn->setCursor(Qt::PointingHandCursor);
    m_settingsSaveBtn = new QPushButton(tr("保存设置"), settingsCard);
    m_settingsSaveBtn->setObjectName(QStringLiteral("pluginPageActionBtnPrimary"));
    m_settingsSaveBtn->setCursor(Qt::PointingHandCursor);
    m_settingsSaveBtn->setDefault(true);
    settingsBtnRow->addWidget(m_settingsResetBtn);
    settingsBtnRow->addWidget(m_settingsSaveBtn);
    settingsLayout->addLayout(settingsBtnRow);

    settingsPageLayout->addWidget(settingsCard);
    settingsPageLayout->addStretch();

    // 设置页放入滚动容器
    m_settingsScroll = new QScrollArea(m_stack);
    m_settingsScroll->setWidgetResizable(true);
    m_settingsScroll->setFrameShape(QFrame::NoFrame);
    m_settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_settingsScroll->setWidget(m_settingsPage);
    m_stack->addWidget(m_settingsScroll);

    mainLayout->addWidget(m_stack, 1);

    // 信号
    connect(m_importBtn, &QPushButton::clicked, this, &PluginPage::importPluginRequested);
    connect(m_createBtn, &QPushButton::clicked, this, &PluginPage::createPluginRequested);
    connect(m_openDirBtn, &QPushButton::clicked, this, &PluginPage::openPluginsDirRequested);
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDetailIndex >= 0 && m_currentDetailIndex < m_plugins.size())
            emit removePluginRequested(m_plugins[m_currentDetailIndex].id);
    });
    connect(m_openFileBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDetailIndex >= 0 && m_currentDetailIndex < m_plugins.size()) {
            const QString path = m_plugins[m_currentDetailIndex].filePath;
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        }
    });
    connect(m_enableBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDetailIndex >= 0 && m_currentDetailIndex < m_plugins.size()) {
            const PluginInfo &info = m_plugins[m_currentDetailIndex];
            PluginManager *pm = PluginManager::instance();
            pm->setEnabled(info.id, !pm->isEnabled(info.id));
        }
    });
    connect(m_settingsBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDetailIndex >= 0 && m_currentDetailIndex < m_plugins.size())
            showSettingsPage(m_currentDetailIndex);
    });
    connect(m_settingsSaveBtn, &QPushButton::clicked, this, &PluginPage::onSettingsSave);
    connect(m_settingsResetBtn, &QPushButton::clicked, this, &PluginPage::onSettingsReset);
    connect(m_updateBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDetailIndex < 0 || m_currentDetailIndex >= m_plugins.size())
            return;
        const PluginInfo info = m_plugins[m_currentDetailIndex];
        if (info.updateUrl.isEmpty()) {
            AppMessageBox::information(this, tr("检查更新"),
                                       tr("该插件未配置 updateUrl 更新地址，无法在线更新。"));
            return;
        }
        // 异步更新：下载在工作线程执行，完成后通过 pluginUpdateFinished 回调提示
        m_updateBtn->setEnabled(false);
        PluginManager::instance()->updatePluginAsync(info.id);
    });
    connect(PluginManager::instance(), &PluginManager::pluginUpdateFinished, this, [this](const QString &id, bool success, const QString &error) {
        m_updateBtn->setEnabled(true);
        QString pluginName = id;
        for (const PluginInfo &info : m_plugins) {
            if (info.id == id) {
                pluginName = info.name;
                break;
            }
        }
        if (!success) {
            AppMessageBox::warning(this, tr("更新失败"), error);
            return;
        }
        AppMessageBox::information(this, tr("检查更新"),
                                   tr("插件「%1」已更新到最新版本。").arg(pluginName));
    });
    connect(m_exportBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDetailIndex < 0 || m_currentDetailIndex >= m_plugins.size())
            return;
        const PluginInfo info = m_plugins[m_currentDetailIndex];
        const QString dir = AppFileDialog::getExistingDirectory(this, tr("导出插件到"));
        if (dir.isEmpty())
            return;
        QString outPath, error;
        if (!PluginManager::instance()->exportPlugin(info.id, dir, &outPath, &error)) {
            AppMessageBox::warning(this, tr("导出失败"), error);
            return;
        }
        AppMessageBox::information(this, tr("导出成功"),
                                   tr("插件已导出到：\n%1").arg(outPath));
    });
    connect(m_extractBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDetailIndex < 0 || m_currentDetailIndex >= m_plugins.size())
            return;
        const PluginInfo info = m_plugins[m_currentDetailIndex];
        const QString dir = AppFileDialog::getExistingDirectory(this, tr("解包插件源码到"));
        if (dir.isEmpty())
            return;
        const QString dest = QDir::cleanPath(dir) + QStringLiteral("/")
                           + info.id + QStringLiteral("-src");
        QString error;
        if (!PluginManager::instance()->extractPlugin(info.id, dest, &error)) {
            AppMessageBox::warning(this, tr("解包失败"), error);
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(dest));
    });
    connect(m_homeBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentDetailIndex >= 0 && m_currentDetailIndex < m_plugins.size()) {
            const QString url = m_plugins[m_currentDetailIndex].homepage;
            if (!url.isEmpty())
                QDesktopServices::openUrl(QUrl(url));
        }
    });

    applyTheme();
}

void PluginPage::applyTheme()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QColor themeColor(tm->currentThemeColor());
    const QString themeHex = tm->currentThemeColor();

    const QString pageTitle = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString subText   = isLight ? "#888888" : "#9a9aa2";
    const QString cardBg    = isLight ? "#ffffff" : "#2e2e32";
    const QString cardBorder= isLight ? "#e6e6e6" : "#45454a";
    const QString cardHover = isLight ? "#f6f8ff" : "#38383d";
    const QString textMain  = isLight ? "#202124" : "#ececef";
    const QString textSub   = isLight ? "#6b6b6b" : "#a5a5ad";
    const QString fieldBg   = isLight ? "#f7f7f7" : "#3b3b40";

    const QString style = QString(
        "QLabel#pluginPageTitle { font-size: 18px; font-weight: 600; color: %1; background: transparent; }"
        "QLabel#pluginPageCount { font-size: 12px; color: %2; background: transparent; }"
        "QLabel#pluginPageEmpty { color: %2; font-size: 14px; background: transparent; }"
        "QPushButton#pluginPageActionBtn {"
        "  background-color: %3; color: %4; border: 1px solid %5; border-radius: 8px;"
        "  padding: 5px 14px; font-size: 12px;"
        "}"
        "QPushButton#pluginPageActionBtn:hover { background-color: %6; border-color: %7; }"
        "QPushButton#pluginPageActionBtnPrimary {"
        "  background-color: %7; color: #ffffff; border: none; border-radius: 8px;"
        "  padding: 5px 14px; font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton#pluginPageActionBtnPrimary:hover { background-color: %8; }"
        "QPushButton#pluginPageDangerBtn {"
        "  background-color: %3; color: #e5484d; border: 1px solid #e5b0b2; border-radius: 8px;"
        "  padding: 5px 14px; font-size: 12px;"
        "}"
        "QPushButton#pluginPageDangerBtn:hover { background-color: #fdf0f0; border-color: #e5484d; }"
        "QWidget#pluginDetailPage { background: transparent; }"
        "QWidget#pluginDetailCard { background-color: %3; border: 1px solid %5; border-radius: 12px; }"
        "QLabel#pluginDetailName { color: %9; font-size: 20px; font-weight: 700; background: transparent; }"
        "QLabel#pluginDetailMeta { color: %10; font-size: 12px; background: transparent; }"
        "QLabel#pluginDetailDesc { color: %4; font-size: 13px; background: transparent; }"
        "QLabel#pluginDetailPath { color: %2; font-size: 11px; background: transparent; }"
        "QLabel#pluginDetailWarn { color: #b45309; font-size: 12px; background: #fef3c7; border-radius: 6px; padding: 6px 10px; }"
        "QLabel#pluginDetailStyle { color: %7; font-size: 12px; background: %6; border-radius: 6px; padding: 6px 10px; }"
        "QLabel#pluginDetailIcon { background: %11; border-radius: 14px; }"
        "QWidget#pluginCommandsRow { background: transparent; }"
        "QWidget#pluginSettingsSection { background-color: %3; border: 1px solid %5; border-radius: 12px; }"
        "QWidget#pluginUiSection { background-color: %3; border: 1px solid %5; border-radius: 12px; }"
        "QPlainTextEdit#pluginUiResult {"
        "  background-color: %11; border: 1px solid %5; border-radius: 8px;"
        "  color: %4; font-size: 12px; font-family: Consolas, 'Courier New', monospace;"
        "}"
        "QLabel#pluginSettingsTitle { color: %9; font-size: 14px; font-weight: 600; background: transparent; }"
        "QLabel#pluginSettingLabel { color: %4; font-size: 13px; background: transparent; }"
        "QLabel#pluginSettingEmpty { color: #999999; font-size: 13px; background: transparent; padding: 30px 0; }"
        "QCheckBox#pluginSettingCheck { color: %4; font-size: 13px; }"
        "QComboBox#pluginSettingCombo {"
        "  background-color: %11; border: 1px solid %5; border-radius: 8px;"
        "  padding: 5px 10px; color: %4; font-size: 13px;"
        "}"
        "QComboBox#pluginSettingCombo::drop-down { border: none; width: 22px; }"
        "QDoubleSpinBox#pluginSettingSpin {"
        "  background-color: %11; border: 1px solid %5; border-radius: 8px;"
        "  padding: 5px 10px; color: %4; font-size: 13px;"
        "}"
        "QLineEdit#pluginSettingEdit {"
        "  background-color: %11; border: 1px solid %5; border-radius: 8px;"
        "  padding: 5px 10px; color: %4; font-size: 13px;"
        "}"
        "QLineEdit#pluginSettingEdit:focus { border: 1px solid %7; }"
        "QScrollArea { background: transparent; }"
        "QWidget#pluginOverviewContent { background: transparent; }")
        .arg(pageTitle, subText)
        .arg(cardBg, textMain, cardBorder)
        .arg(cardHover, themeHex)
        .arg(tm->getThemeColorHover())
        .arg(textMain, textSub)
        .arg(fieldBg);

    setStyleSheet(style);
}

void PluginPage::refreshList()
{
    m_plugins = PluginManager::instance()->plugins();
    m_currentDetailIndex = -1;
    updateHeaderCount();
    rebuildCards();

    if (m_plugins.isEmpty()) {
        m_stack->setCurrentWidget(m_emptyState);
    } else if ((m_stack->currentWidget() == m_detailScroll || m_stack->currentWidget() == m_settingsScroll)
               && m_currentDetailIndex < 0) {
        // 详情/设置数据已失效，回到总览
        m_stack->setCurrentWidget(m_overviewScroll);
    }
}

void PluginPage::updateHeaderCount()
{
    int enabledCount = 0;
    PluginManager *pm = PluginManager::instance();
    for (const PluginInfo &p : m_plugins) {
        if (pm->isEnabled(p.id))
            ++enabledCount;
    }
    m_countLabel->setText(tr("共 %1 个插件 · 已启用 %2 个").arg(m_plugins.size()).arg(enabledCount));
}

void PluginPage::rebuildCards()
{
    // 清理旧卡片与底部弹性占位（一次性取空，避免残留 spacing 项）
    while (QLayoutItem *item = m_cardsLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_cardWidgets.clear();

    PluginManager *pm = PluginManager::instance();
    const QColor themeColor(ThemeManager::instance()->currentThemeColor());

    for (int i = 0; i < m_plugins.size(); ++i) {
        const PluginInfo &info = m_plugins[i];
        const bool enabled = pm->isEnabled(info.id);

        // 卡片样式与实例管理的资源管理卡片（modCardListItem）一致
        QWidget *card = new QWidget(m_overviewContent);
        card->setObjectName(QStringLiteral("modCardListItem"));
        card->setProperty("cardRole", "container");
        card->setFixedHeight(76);
        card->setCursor(Qt::PointingHandCursor);

        QHBoxLayout *rowLayout = new QHBoxLayout(card);
        rowLayout->setContentsMargins(14, 12, 14, 12);
        rowLayout->setSpacing(14);

        // 图标
        QLabel *iconLabel = new QLabel(card);
        iconLabel->setObjectName(QStringLiteral("modCardIcon"));
        iconLabel->setProperty("cardRole", "icon");
        iconLabel->setFixedSize(48, 48);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setPixmap(loadPluginIcon(info, 44));
        rowLayout->addWidget(iconLabel);

        // 信息区
        QVBoxLayout *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(6);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        QLabel *nameLabel = new QLabel(info.name, card);
        nameLabel->setObjectName(QStringLiteral("modCardName"));
        nameLabel->setProperty("cardRole", "name");
        infoLayout->addWidget(nameLabel);

        // 标签行：版本 / 分类 / 作者 / ID / 状态（都在同一行小卡片）
        QWidget *chipsWidget = new QWidget(card);
        QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
        chipsLayout->setContentsMargins(0, 0, 0, 0);
        chipsLayout->setSpacing(6);

        auto addChip = [&](const QString &text) {
            QLabel *chip = new QLabel(text, chipsWidget);
            chip->setObjectName(QStringLiteral("modChip"));
            chip->setProperty("cardRole", "chip");
            chipsLayout->addWidget(chip);
        };
        if (!info.version.isEmpty())
            addChip(tr("v%1").arg(info.version));
        if (!info.category.isEmpty())
            addChip(info.category);
        if (!info.author.isEmpty())
            addChip(info.author);
        if (!info.id.isEmpty())
            addChip(info.id);

        QLabel *stateChip = new QLabel(enabled ? tr("已启用") : tr("已停用"), chipsWidget);
        stateChip->setObjectName(QStringLiteral("modChip"));
        stateChip->setProperty("cardRole", "chip");
        stateChip->setStyleSheet(enabled
            ? QStringLiteral("background-color: #dcfce7; color: #16a34a; border: 1px solid #bbf7d0; border-radius: 10px; padding: 3px 10px; font-size: 11px;")
            : QStringLiteral("background-color: #f3f4f6; color: #9ca3af; border: 1px solid #e5e7eb; border-radius: 10px; padding: 3px 10px; font-size: 11px;"));
        chipsLayout->addWidget(stateChip);
        chipsLayout->addStretch();
        infoLayout->addWidget(chipsWidget);

        rowLayout->addLayout(infoLayout, 1);

        // 右侧操作图标按钮
        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(4);
        btnLayout->setContentsMargins(0, 0, 0, 0);

        auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton* {
            QPushButton *btn = new QPushButton(card);
            btn->setObjectName(QStringLiteral("modCardActionBtn"));
            btn->setProperty("cardRole", "actionBtn");
            btn->setFixedSize(32, 32);
            btn->setToolTip(tip);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
            btn->setIconSize(QSize(18, 18));
            return btn;
        };

        const QString pluginId = info.id;
        const QString pluginHomepage = info.homepage;

        // 插件设置（进入设置界面）
        QPushButton *settingsBtn = createBtn(":/Images/Icons/instance_settings.svg", tr("插件设置"));
        connect(settingsBtn, &QPushButton::clicked, this, [this, i]() {
            showSettingsPage(i);
        });
        btnLayout->addWidget(settingsBtn);

        // 导出
        QPushButton *exportBtn = createBtn(":/Images/Icons/export.svg", tr("导出插件"));
        connect(exportBtn, &QPushButton::clicked, this, [this, i]() {
            if (i < 0 || i >= m_plugins.size())
                return;
            const QString dir = AppFileDialog::getExistingDirectory(this, tr("导出插件到"));
            if (dir.isEmpty())
                return;
            QString outPath, error;
            if (!PluginManager::instance()->exportPlugin(m_plugins[i].id, dir, &outPath, &error)) {
                AppMessageBox::warning(this, tr("导出失败"), error);
                return;
            }
            AppMessageBox::information(this, tr("导出成功"),
                                       tr("插件已导出到：\n%1").arg(outPath));
        });
        btnLayout->addWidget(exportBtn);

        // 删除
        QPushButton *removeBtn = createBtn(":/Images/Icons/delete.svg", tr("删除插件"));
        connect(removeBtn, &QPushButton::clicked, this, [this, pluginId]() {
            emit removePluginRequested(pluginId);
        });
        btnLayout->addWidget(removeBtn);

        // 主页（有条件显示）
        if (!info.homepage.isEmpty()) {
            QPushButton *homeBtn = createBtn(":/Images/Icons/nav_home.svg", tr("打开主页"));
            connect(homeBtn, &QPushButton::clicked, this, [pluginHomepage]() {
                QDesktopServices::openUrl(QUrl(pluginHomepage));
            });
            btnLayout->addWidget(homeBtn);
        }

        // 查看详情
        QPushButton *detailBtn = createBtn(":/Images/Icons/list.svg", tr("查看详情"));
        connect(detailBtn, &QPushButton::clicked, this, [this, i]() {
            showPluginDetail(i);
        });
        btnLayout->addWidget(detailBtn);

        // 启用/停用
        QPushButton *toggleBtn = createBtn(
            enabled ? ":/Images/Icons/toggle_on.svg" : ":/Images/Icons/toggle_off.svg",
            enabled ? tr("停用") : tr("启用"));
        {
            QColor toggleColor = enabled ? QColor("#4CAF50") : QColor("#9E9E9E");
            toggleBtn->setIcon(IconHelper::loadColoredIcon(
                enabled ? ":/Images/Icons/toggle_on.svg" : ":/Images/Icons/toggle_off.svg",
                toggleColor, 18));
        }
        connect(toggleBtn, &QPushButton::clicked, this, [this, pluginId]() {
            PluginManager *mgr = PluginManager::instance();
            mgr->setEnabled(pluginId, !mgr->isEnabled(pluginId));
            rebuildCards();  // 立即刷新卡片启用状态
        });
        btnLayout->addWidget(toggleBtn);

        rowLayout->addLayout(btnLayout);

        // 点击卡片空白处 → 详情
        const int idx = i;
        card->setProperty("pluginIndex", idx);
        card->installEventFilter(this);

        m_cardWidgets.append(card);
        m_cardsLayout->addWidget(card);
    }

    // 底部弹性占位：吸收滚动区多余高度，保证卡片置顶向下排列而非居中/摊开
    m_cardsLayout->addStretch(1);
}

bool PluginPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        // 仅处理卡片本身（避免误吞卡片内操作按钮的点击事件）
        QVariant idxVar = watched->property("pluginIndex");
        if (idxVar.isValid()) {
            const int idx = idxVar.toInt();
            if (idx >= 0 && idx < m_plugins.size()) {
                showPluginDetail(idx);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PluginPage::showOverview()
{
    m_currentDetailIndex = -1;
    updateHeaderCount();
    rebuildCards();
    if (m_plugins.isEmpty())
        m_stack->setCurrentWidget(m_emptyState);
    else
        m_stack->setCurrentWidget(m_overviewScroll);
}

void PluginPage::showPluginDetail(int index)
{
    if (index < 0 || index >= m_plugins.size()) {
        showOverview();
        return;
    }
    m_currentDetailIndex = index;
    updateDetailView(m_plugins[index]);
    m_stack->setCurrentWidget(m_detailScroll);
}

void PluginPage::updateDetailView(const PluginInfo &info)
{
    PluginManager *pm = PluginManager::instance();
    const bool enabled = pm->isEnabled(info.id);

    m_detailIcon->setPixmap(loadPluginIcon(info, 64));

    m_detailName->setText(info.name);
    QString meta = tr("ID: %1").arg(info.id);
    if (!info.version.isEmpty())
        meta += QStringLiteral("    ") + tr("版本: v%1").arg(info.version);
    if (!info.author.isEmpty())
        meta += QStringLiteral("    ") + tr("作者: %1").arg(info.author);
    if (!info.apiVersion.isEmpty())
        meta += QStringLiteral("    ") + tr("API: %1").arg(info.apiVersion);
    if (!info.category.isEmpty())
        meta += QStringLiteral("    ") + tr("分类: %1").arg(info.category);
    if (!info.license.isEmpty())
        meta += QStringLiteral("    ") + tr("许可证: %1").arg(info.license);
    m_detailMeta->setText(meta);

    // 兼容性/依赖警告
    QStringList warns;
    QString apiReason;
    if (!pm->isApiCompatible(info, &apiReason))
        warns << tr("⚠ 不兼容：%1").arg(apiReason);
    const QStringList missing = pm->missingDependencies(info);
    if (!missing.isEmpty())
        warns << tr("⚠ 缺少依赖插件：%1").arg(missing.join(QStringLiteral(", ")));
    if (info.loadError.isEmpty() && !info.entry.isEmpty())
        warns.clear(); // 有入口脚本时视为功能完整，隐藏占位警告
    m_detailWarn->setVisible(!warns.isEmpty());
    m_detailWarn->setText(warns.join(QStringLiteral("\n")));

    m_detailDesc->setText(info.description.isEmpty()
        ? tr("（该插件未填写描述）") : info.description);

    QString pathText = tr("文件位置：%1").arg(info.filePath);
    if (!info.updateUrl.isEmpty())
        pathText += QStringLiteral("\n") + tr("更新地址：%1").arg(info.updateUrl);
    m_detailPath->setText(pathText);

    // 插件样式贡献（manifest.style）：启用时由宿主应用到启动器界面
    if (info.style.isValid()) {
        const bool styleEnabled = pm->isEnabled(info.id);
        QString styleText = tr("◈ 本插件提供启动器界面样式");
        styleText += styleEnabled
            ? tr("（已启用，样式当前生效）")
            : tr("（已停用，样式未生效）");
        m_detailStyle->setText(styleText);
        m_detailStyle->setVisible(true);
    } else {
        m_detailStyle->setVisible(false);
    }

    // 启用/停用按钮
    m_enableBtn->setText(enabled ? tr("停用插件") : tr("启用插件"));
    m_enableBtn->setStyleSheet(enabled
        ? QStringLiteral("color: #b45309;")
        : QStringLiteral("color: #16a34a; font-weight: 600;"));

    // 条件按钮
    m_updateBtn->setVisible(!info.updateUrl.isEmpty());
    m_homeBtn->setVisible(!info.homepage.isEmpty());
    m_extractBtn->setVisible(true);

    // 插件内嵌界面：声明 ui 的插件渲染 表单+动作+结果区（不弹窗），
    // 未声明 ui 的插件显示命令按钮行；声明了服务器列表命令(kind=server-list)
    // 的插件在命令区域渲染内置服务器列表（原生并发检测延迟/玩家数）。
    bool hasServerList = false;
    for (const PluginCommand &c : info.commands) {
        if (c.kind == QLatin1String("server-list")) {
            hasServerList = true;
            break;
        }
    }
    m_serverRefreshTimer->stop();

    if (info.ui.isValid()) {
        m_commandsRow->setVisible(false);
        rebuildUiSection(info);
    } else if (hasServerList) {
        m_uiSection->setVisible(false);
        rebuildServerList(info);
    } else {
        m_uiSection->setVisible(false);
        rebuildCommands(info);
    }
}

void PluginPage::rebuildUiSection(const PluginInfo &info)
{
    // 清空表单与动作行
    while (QLayoutItem *item = m_uiFormLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    while (QLayoutItem *item = m_uiActionRow->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_uiFields.clear();
    m_uiResult->clear();

    // 界面样式统一跟随应用主题（applyTheme 中的 #pluginUiSection 规则），
    // 不再应用插件自带的 style，避免五颜六色破坏整体风格。

    for (const PluginUiField &field : info.ui.fields) {
        QWidget *widget = createUiFieldWidget(field);
        if (!widget)
            continue;
        QLabel *label = new QLabel(field.label.isEmpty() ? field.key : field.label, m_uiSection);
        label->setObjectName(QStringLiteral("pluginSettingLabel"));
        label->setWordWrap(true);
        m_uiFormLayout->addRow(label, widget);
        m_uiFields.insert(field.key, widget);
    }

    for (const PluginUiAction &action : info.ui.actions) {
        QPushButton *btn = new QPushButton(action.label.isEmpty() ? action.id : action.label, m_uiSection);
        btn->setObjectName(QStringLiteral("pluginPageActionBtnPrimary"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(32);
        connect(btn, &QPushButton::clicked, this, [this, info, action]() {
            runUiAction(info, action);
        });
        m_uiActionRow->addWidget(btn);
    }
    m_uiActionRow->addStretch();

    m_uiSection->setVisible(true);
}

QWidget *PluginPage::createUiFieldWidget(const PluginUiField &field)
{
    const QString type = field.type;

    if (type == QLatin1String("bool")) {
        QCheckBox *box = new QCheckBox(m_uiSection);
        box->setObjectName(QStringLiteral("pluginSettingCheck"));
        box->setChecked(field.defaultValue == QLatin1String("true")
                        || field.defaultValue == QLatin1String("1"));
        return box;
    }

    if (type == QLatin1String("select")) {
        QComboBox *combo = new QComboBox(m_uiSection);
        combo->setObjectName(QStringLiteral("pluginSettingCombo"));
        // 选项支持 "值|显示文本" 格式；纯文本则值与显示相同
        for (const QString &opt : field.options) {
            QString value = opt, display = opt;
            const int sep = opt.indexOf(QLatin1Char('|'));
            if (sep >= 0) {
                value = opt.left(sep);
                display = opt.mid(sep + 1);
            }
            combo->addItem(display, value);
        }
        int idx = combo->findData(field.defaultValue);
        if (idx < 0)
            idx = combo->findText(field.defaultValue);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        return combo;
    }

    if (type == QLatin1String("number")) {
        QSpinBox *spin = new QSpinBox(m_uiSection);
        spin->setObjectName(QStringLiteral("pluginSettingSpin"));
        spin->setRange(0, 100000);
        spin->setValue(field.defaultValue.toInt());
        return spin;
    }

    if (type == QLatin1String("path")) {
        // 路径选择：输入框 + 浏览按钮
        QWidget *host = new QWidget(m_uiSection);
        QHBoxLayout *row = new QHBoxLayout(host);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        QLineEdit *edit = new QLineEdit(host);
        edit->setObjectName(QStringLiteral("pluginSettingEdit"));
        edit->setReadOnly(true);
        edit->setText(field.defaultValue);
        QPushButton *browse = new QPushButton(tr("浏览…"), host);
        browse->setObjectName(QStringLiteral("pluginPageActionBtn"));
        browse->setCursor(Qt::PointingHandCursor);
        browse->setFixedHeight(28);
        row->addWidget(edit, 1);
        row->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, edit]() {
            const QString path = AppFileDialog::getSaveFileName(this, tr("选择保存路径"));
            if (!path.isEmpty())
                edit->setText(path);
        });
        return host;
    }

    if (type == QLatin1String("time")) {
        QTimeEdit *timeEdit = new QTimeEdit(m_uiSection);
        timeEdit->setObjectName(QStringLiteral("pluginSettingEdit"));
        timeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
        const QTime t = QTime::fromString(field.defaultValue, QStringLiteral("HH:mm"));
        timeEdit->setTime(t.isValid() ? t : QTime(22, 0));
        return timeEdit;
    }

    // text / 其他 → QLineEdit
    QLineEdit *edit = new QLineEdit(m_uiSection);
    edit->setObjectName(QStringLiteral("pluginSettingEdit"));
    edit->setText(field.defaultValue);
    if (!field.placeholder.isEmpty())
        edit->setPlaceholderText(field.placeholder);
    return edit;
}

QString PluginPage::uiFieldValue(const QString &key) const
{
    QWidget *w = m_uiFields.value(key);
    if (!w)
        return key;  // 未找到字段时按字面值传递

    if (QCheckBox *box = qobject_cast<QCheckBox *>(w))
        return box->isChecked() ? QStringLiteral("true") : QStringLiteral("false");

    if (QComboBox *combo = qobject_cast<QComboBox *>(w)) {
        const QVariant data = combo->currentData();
        return data.isValid() ? data.toString() : combo->currentText();
    }

    if (QSpinBox *spin = qobject_cast<QSpinBox *>(w))
        return QString::number(spin->value());

    if (QTimeEdit *timeEdit = qobject_cast<QTimeEdit *>(w))
        return timeEdit->time().toString(QStringLiteral("HH:mm"));

    if (QLineEdit *edit = qobject_cast<QLineEdit *>(w))
        return edit->text().trimmed();

    // path 类型：容器里存了输入框
    if (QLineEdit *edit = w->findChild<QLineEdit *>())
        return edit->text().trimmed();

    return QString();
}

void PluginPage::runUiAction(const PluginInfo &info, const PluginUiAction &action)
{
    if (m_uiProc && m_uiProc->state() != QProcess::NotRunning) {
        AppMessageBox::information(this, tr("插件界面"),
                                   tr("上一个任务仍在执行，请稍候。"));
        return;
    }

    // 必填字段校验
    for (const PluginUiField &field : info.ui.fields) {
        if (field.required && uiFieldValue(field.key).isEmpty()) {
            AppMessageBox::warning(this, tr("插件界面"),
                                   tr("请先填写「%1」。").arg(field.label.isEmpty() ? field.key : field.label));
            return;
        }
    }

    // 组装参数：参数名 -> 字段值（或字面值）。
    // 布尔语义：true 传纯 switch（-Name），false 传 -Name:$false；
    // 其余值用 -Name value 格式。
    QStringList extraArgs;
    for (auto it = action.args.cbegin(); it != action.args.cend(); ++it) {
        const QString argName = QStringLiteral("-") + it.key();
        const QString value = uiFieldValue(it.value());
        if (value == QLatin1String("true"))
            extraArgs << argName;
        else if (value == QLatin1String("false"))
            extraArgs << (argName + QStringLiteral(":$false"));
        else
            extraArgs << argName << value;
    }

    m_uiResult->clear();
    m_uiResult->appendPlainText(tr("正在执行 %1 …").arg(action.label.isEmpty() ? action.id : action.label));
    runScriptDetached(info, action.script, extraArgs);
}

void PluginPage::runScriptDetached(const PluginInfo &info, const QString &scriptRel,
                                   const QStringList &extraArgs)
{
    // 解包到临时目录
    const QString tmpDir = QDir::tempPath() + QStringLiteral("/BlockBox/") + info.id;
    QString error;
    if (!PluginManager::instance()->extractPlugin(info.id, tmpDir, &error)) {
        m_uiResult->appendPlainText(tr("解包插件失败：%1").arg(error));
        return;
    }
    const QString scriptPath = QDir::cleanPath(tmpDir + QStringLiteral("/") + scriptRel);
    if (!QFileInfo::exists(scriptPath)) {
        m_uiResult->appendPlainText(tr("包内未找到脚本：%1").arg(scriptRel));
        return;
    }

    if (m_uiProc)
        m_uiProc->deleteLater();
    m_uiProc = new QProcess(this);

    connect(m_uiProc, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString out = QString::fromLocal8Bit(m_uiProc->readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            // 多线程下载等脚本输出 PROGRESS:xx:done:total，直接显示
            m_uiResult->appendPlainText(out);
        }
    });
    connect(m_uiProc, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = QString::fromLocal8Bit(m_uiProc->readAllStandardError()).trimmed();
        if (!err.isEmpty())
            m_uiResult->appendPlainText(err);
    });
    connect(m_uiProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        m_uiResult->appendPlainText(code == 0
            ? tr("—— 执行完成 ——")
            : tr("—— 执行结束（退出码 %1）——").arg(code));
    });

    QStringList psArgs;
    psArgs << QStringLiteral("-NoProfile") << QStringLiteral("-ExecutionPolicy")
           << QStringLiteral("Bypass") << QStringLiteral("-WindowStyle")
           << QStringLiteral("Hidden") << QStringLiteral("-File")
           << QDir::toNativeSeparators(scriptPath);
    psArgs << extraArgs;

    m_uiProc->start(QStringLiteral("powershell"), psArgs);
}

void PluginPage::clearCommandsRow()
{
    // 容器布局为 QVBoxLayout：分类标题(QLabel)独占一行，每组按钮用 FlowLayout 自动换行
    QVBoxLayout *containerLayout = qobject_cast<QVBoxLayout *>(m_commandsRow->layout());
    if (!containerLayout)
        return;

    // 清空旧内容（按钮 widget / 子 FlowLayout / spacer）
    while (QLayoutItem *item = containerLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            containerLayout->removeWidget(w);
            w->deleteLater();
        } else if (QLayout *sub = item->layout()) {
            while (QLayoutItem *subItem = sub->takeAt(0)) {
                if (QWidget *sw = subItem->widget())
                    sw->deleteLater();
                delete subItem;
            }
            delete sub;
        }
        delete item;
    }
    m_commandBtns.clear();
    m_serverRows.clear();
    m_serverTotal = 0;
    m_serverDone = 0;
    m_serverOnline = 0;
    m_serverChecking = false;
    m_serverSearch = nullptr;
    m_serverRefreshBtn = nullptr;
    m_serverStat = nullptr;
}

void PluginPage::rebuildCommands(const PluginInfo &info)
{
    QVBoxLayout *containerLayout = qobject_cast<QVBoxLayout *>(m_commandsRow->layout());
    if (!containerLayout)
        return;

    clearCommandsRow();

    if (info.commands.isEmpty()) {
        m_commandsRow->setVisible(false);
        return;
    }

    FlowLayout *currentFlow = nullptr;
    bool hasAny = false;

    for (const PluginCommand &cmd : info.commands) {
        // kind == "label"：渲染为分类标题，独占一行，并开启新的按钮流
        if (cmd.kind == QLatin1String("label")) {
            QLabel *titleLabel = new QLabel(cmd.label.isEmpty() ? cmd.id : cmd.label, m_commandsRow);
            titleLabel->setObjectName(QStringLiteral("pluginCommandsLabel"));
            containerLayout->addWidget(titleLabel);
            currentFlow = new FlowLayout(nullptr, 0, 8, 8);
            containerLayout->addLayout(currentFlow);
            hasAny = true;
            continue;
        }

        // kind == "native"：原生插件动作，由宿主提供界面（不执行脚本）
        if (cmd.kind == QLatin1String("native")) {
            const QString target = cmd.target;
            QPushButton *nbtn = new QPushButton(cmd.label.isEmpty() ? cmd.id : cmd.label, m_commandsRow);
            nbtn->setObjectName(QStringLiteral("pluginPageActionBtnPrimary"));
            nbtn->setCursor(Qt::PointingHandCursor);
            nbtn->setFixedHeight(32);
            connect(nbtn, &QPushButton::clicked, this, [this, info, target]() {
                handleNativeCommand(info, target);
            });
            if (!currentFlow) {
                currentFlow = new FlowLayout(nullptr, 0, 8, 8);
                containerLayout->addLayout(currentFlow);
            }
            currentFlow->addWidget(nbtn);
            hasAny = true;
            continue;
        }

        // 普通命令按钮
        QPushButton *btn = new QPushButton(cmd.label.isEmpty() ? cmd.id : cmd.label, m_commandsRow);
        btn->setObjectName(QStringLiteral("pluginPageActionBtn"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(30);
        connect(btn, &QPushButton::clicked, this, [this, cmd]() {
            if (m_currentDetailIndex >= 0 && m_currentDetailIndex < m_plugins.size())
                runCommand(m_plugins[m_currentDetailIndex], cmd);
        });

        // 若尚未有分类标题，则创建默认按钮流
        if (!currentFlow) {
            currentFlow = new FlowLayout(nullptr, 0, 8, 8);
            containerLayout->addLayout(currentFlow);
        }
        currentFlow->addWidget(btn);
        m_commandBtns.append(btn);
        hasAny = true;
    }

    containerLayout->addStretch();
    m_commandsRow->setVisible(hasAny);
}

// ============================================================================
//  插件内置服务器列表（kind == "server-list"）
//  数据来源：插件包内 data/servers.json
//  {
//    "meta": {...},
//    "categories": ["国内·生存", ...],
//    "servers": [{"name","address","port","category","version","desc"}, ...]
//  }
// ============================================================================
void PluginPage::rebuildServerList(const PluginInfo &info)
{
    QVBoxLayout *containerLayout = qobject_cast<QVBoxLayout *>(m_commandsRow->layout());
    if (!containerLayout)
        return;

    clearCommandsRow();

    // ── 1. 解包插件并读取服务器数据文件 ──
    QString dataJson;
    for (const PluginCommand &cmd : info.commands) {
        if (cmd.kind != QLatin1String("server-list"))
            continue;
        QString rel = cmd.target.trimmed();
        if (rel.isEmpty())
            rel = QStringLiteral("data/servers.json");

        QString tmpDir = QDir::tempPath() + QStringLiteral("/BlockBox/") + info.id;
        QString error;
        PluginManager::instance()->extractPlugin(info.id, tmpDir, &error);

        QFile f(QDir::cleanPath(tmpDir + QStringLiteral("/") + rel));
        if (f.open(QIODevice::ReadOnly)) {
            dataJson = QString::fromUtf8(f.readAll());
            f.close();
        }
        break;
    }

    // ── 2. 解析数据 ──
    QJsonDocument doc = QJsonDocument::fromJson(dataJson.toUtf8());
    if (!doc.isObject()) {
        QLabel *err = new QLabel(tr("服务器列表数据解析失败（data/servers.json 缺失或损坏）"), m_commandsRow);
        err->setObjectName(QStringLiteral("pluginSettingEmpty"));
        containerLayout->addWidget(err);
        containerLayout->addStretch();
        m_commandsRow->setVisible(true);
        return;
    }
    QJsonObject root = doc.object();
    QJsonArray categories = root.value(QLatin1String("categories")).toArray();
    QJsonArray servers = root.value(QLatin1String("servers")).toArray();

    // 服务器按分类分组（保持 categories 声明的顺序）
    QHash<QString, QList<int>> catIndex;
    for (int i = 0; i < servers.size(); ++i) {
        QJsonObject s = servers.at(i).toObject();
        QString cat = s.value(QLatin1String("category")).toString();
        catIndex[cat].append(i);
    }

    // ── 3. 顶部工具行：搜索 + 刷新 + 状态 ──
    QWidget *toolRow = new QWidget(m_commandsRow);
    QHBoxLayout *toolLayout = new QHBoxLayout(toolRow);
    toolLayout->setContentsMargins(0, 4, 0, 8);
    toolLayout->setSpacing(8);

    QLabel *searchLabel = new QLabel(tr("搜索:"), toolRow);
    searchLabel->setObjectName(QStringLiteral("pluginSettingLabel"));
    m_serverSearch = new QLineEdit(toolRow);
    m_serverSearch->setPlaceholderText(tr("按服务器名称 / 地址过滤"));
    m_serverSearch->setFixedWidth(220);
    m_serverSearch->setClearButtonEnabled(true);
    connect(m_serverSearch, &QLineEdit::textChanged,
            this, &PluginPage::onServerSearchChanged);

    m_serverRefreshBtn = new QPushButton(tr("⟳ 刷新全部"), toolRow);
    m_serverRefreshBtn->setObjectName(QStringLiteral("pluginPageActionBtnPrimary"));
    m_serverRefreshBtn->setCursor(Qt::PointingHandCursor);
    m_serverRefreshBtn->setFixedHeight(28);
    connect(m_serverRefreshBtn, &QPushButton::clicked,
            this, &PluginPage::onServerRefreshClicked);

    m_serverStat = new QLabel(tr("共 %1 台服务器").arg(servers.size()), toolRow);
    m_serverStat->setObjectName(QStringLiteral("pluginServerStatLabel"));
    m_serverStat->setStyleSheet(QStringLiteral("color: #8a94a6; font-size: 12px;"));

    toolLayout->addWidget(searchLabel);
    toolLayout->addWidget(m_serverSearch);
    toolLayout->addWidget(m_serverRefreshBtn);
    toolLayout->addSpacing(8);
    toolLayout->addWidget(m_serverStat);
    toolLayout->addStretch();
    containerLayout->addWidget(toolRow);

    // ── 4. 渲染分类标题 + 服务器行 ──
    QList<QString> renderedCats;
    for (const QJsonValue &cv : categories) {
        QString cat = cv.toString();
        if (renderedCats.contains(cat))
            continue;
        renderedCats.append(cat);
        if (!catIndex.contains(cat))
            continue;

        QLabel *title = new QLabel(cat, m_commandsRow);
        title->setObjectName(QStringLiteral("pluginCommandsLabel"));
        containerLayout->addWidget(title);

        for (int idx : catIndex.value(cat)) {
            QJsonObject s = servers.at(idx).toObject();
            QString name = s.value(QLatin1String("name")).toString();
            QString address = s.value(QLatin1String("address")).toString();
            int port = s.value(QLatin1String("port")).toInt(25565);
            QString version = s.value(QLatin1String("version")).toString();
            QString desc = s.value(QLatin1String("desc")).toString();
            if (name.isEmpty())
                name = address;

            ServerRowData row;
            row.host = address;
            row.port = static_cast<quint16>(port);
            row.name = name;
            row.addressText = (port != 25565) ? QStringLiteral("%1:%2").arg(address).arg(port) : address;
            row.version = version;
            row.desc = desc;

            // 行容器
            QWidget *rowWidget = new QWidget(m_commandsRow);
            rowWidget->setObjectName(QStringLiteral("pluginServerRow"));
            rowWidget->setStyleSheet(QStringLiteral(
                "QWidget#pluginServerRow { background: transparent; }"
                "QWidget#pluginServerRow:hover { background: #f2f6fb; border-radius: 6px; }"));
            rowWidget->setFixedHeight(36);
            QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
            rowLayout->setContentsMargins(8, 0, 8, 0);
            rowLayout->setSpacing(10);

            // 状态点
            row.statusLbl = new QLabel(QStringLiteral("…"), rowWidget);
            row.statusLbl->setFixedWidth(18);
            row.statusLbl->setAlignment(Qt::AlignCenter);
            row.statusLbl->setStyleSheet(QStringLiteral("color: #c3ccd8; font-size: 14px; font-weight: 600;"));

            // 名称
            QLabel *nameLbl = new QLabel(name, rowWidget);
            nameLbl->setObjectName(QStringLiteral("pluginServerNameLabel"));
            nameLbl->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 13px;"));
            nameLbl->setMinimumWidth(140);

            // 地址
            QLabel *addrLbl = new QLabel(row.addressText, rowWidget);
            addrLbl->setStyleSheet(QStringLiteral("color: #7a8699; font-size: 12px; font-family: Consolas, monospace;"));

            // 版本
            QLabel *verLbl = new QLabel(version.isEmpty() ? QStringLiteral("-") : version, rowWidget);
            verLbl->setStyleSheet(QStringLiteral("color: #8a94a6; font-size: 12px;"));
            verLbl->setFixedWidth(70);

            // 延迟 / 玩家
            row.pingLbl = new QLabel(QStringLiteral("-"), rowWidget);
            row.pingLbl->setFixedWidth(70);
            row.pingLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row.pingLbl->setStyleSheet(QStringLiteral("color: #8a94a6; font-size: 12px;"));

            row.playersLbl = new QLabel(QStringLiteral("-"), rowWidget);
            row.playersLbl->setFixedWidth(80);
            row.playersLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row.playersLbl->setStyleSheet(QStringLiteral("color: #8a94a6; font-size: 12px;"));

            // 加入按钮
            QPushButton *joinBtn = new QPushButton(tr("▶ 加入"), rowWidget);
            joinBtn->setObjectName(QStringLiteral("pluginServerJoinBtn"));
            joinBtn->setCursor(Qt::PointingHandCursor);
            joinBtn->setFixedSize(72, 26);
            joinBtn->setStyleSheet(QStringLiteral(
                "QPushButton { background: #00a854; color: white; border: none; border-radius: 5px; font-size: 12px; font-weight: 600; }"
                "QPushButton:hover { background: #00b85f; }"
                "QPushButton:pressed { background: #00964a; }"));
            const int rowIdx = m_serverRows.size();
            connect(joinBtn, &QPushButton::clicked, this, [this, rowIdx]() {
                onServerJoinClicked(rowIdx);
            });

            if (!desc.isEmpty())
                rowWidget->setToolTip(desc);

            rowLayout->addWidget(row.statusLbl);
            rowLayout->addWidget(nameLbl);
            rowLayout->addWidget(addrLbl);
            rowLayout->addStretch();
            rowLayout->addWidget(verLbl);
            rowLayout->addWidget(row.pingLbl);
            rowLayout->addWidget(row.playersLbl);
            rowLayout->addWidget(joinBtn);

            row.rowWidget = rowWidget;
            containerLayout->addWidget(rowWidget);
            m_serverRows.append(row);
        }
    }

    containerLayout->addStretch();
    m_commandsRow->setVisible(true);

    // ── 5. 启动并发检测 ──
    m_serverTotal = m_serverRows.size();
    m_serverDone = 0;
    m_serverOnline = 0;
    m_serverChecking = true;
    m_serverStat->setText(tr("正在检测 0/%1 台...").arg(m_serverTotal));

    QList<ServerStatusChecker::Target> targets;
    for (int i = 0; i < m_serverRows.size(); ++i) {
        ServerStatusChecker::Target t;
        t.host = m_serverRows.at(i).host;
        t.port = m_serverRows.at(i).port;
        t.row = i;
        targets.append(t);
    }
    m_statusChecker->checkAll(targets, 3000, 24);
    m_serverRefreshTimer->start();
}

void PluginPage::onServerStatusReady(int row, bool ok, int pingMs, int online, int max)
{
    if (row < 0 || row >= m_serverRows.size())
        return;
    ServerRowData &r = m_serverRows[row];
    if (!r.statusLbl || !r.pingLbl || !r.playersLbl)
        return;

    ++m_serverDone;
    if (ok) {
        ++m_serverOnline;
        r.statusLbl->setText(QStringLiteral("●"));
        r.statusLbl->setStyleSheet(QStringLiteral("color: #00a854; font-size: 14px; font-weight: 600;"));
        r.pingLbl->setText(QStringLiteral("%1 ms").arg(pingMs));
        r.pingLbl->setStyleSheet(QStringLiteral("color: #00a854; font-size: 12px;"));
        if (online >= 0)
            r.playersLbl->setText(QStringLiteral("%1/%2").arg(online).arg(max));
        else
            r.playersLbl->setText(QStringLiteral("-"));
        r.playersLbl->setStyleSheet(QStringLiteral("color: #4a90d9; font-size: 12px;"));
    } else {
        r.statusLbl->setText(QStringLiteral("○"));
        r.statusLbl->setStyleSheet(QStringLiteral("color: #d9534f; font-size: 14px; font-weight: 600;"));
        r.pingLbl->setText(tr("超时"));
        r.pingLbl->setStyleSheet(QStringLiteral("color: #d9534f; font-size: 12px;"));
        r.playersLbl->setText(QStringLiteral("-"));
        r.playersLbl->setStyleSheet(QStringLiteral("color: #8a94a6; font-size: 12px;"));
    }

    if (m_serverStat)
        m_serverStat->setText(tr("检测中 %1/%2 台...").arg(m_serverDone).arg(m_serverTotal));
}

void PluginPage::onServerCheckAllDone()
{
    m_serverChecking = false;
    if (m_serverStat)
        m_serverStat->setText(tr("在线 %1 / %2 台  |  双击/点「加入」进服")
                                  .arg(m_serverOnline)
                                  .arg(m_serverTotal));
    if (m_serverRefreshBtn)
        m_serverRefreshBtn->setEnabled(true);
}

void PluginPage::onServerSearchChanged(const QString &text)
{
    const QString kw = text.trimmed();
    for (ServerRowData &r : m_serverRows) {
        if (!r.rowWidget)
            continue;
        bool match = kw.isEmpty()
                     || r.name.contains(kw, Qt::CaseInsensitive)
                     || r.host.contains(kw, Qt::CaseInsensitive)
                     || r.addressText.contains(kw, Qt::CaseInsensitive);
        r.rowWidget->setVisible(match);
    }
}

void PluginPage::onServerRefreshClicked()
{
    if (m_serverRows.isEmpty() || m_serverChecking)
        return;
    if (m_serverRefreshBtn)
        m_serverRefreshBtn->setEnabled(false);

    m_serverDone = 0;
    m_serverOnline = 0;
    m_serverChecking = true;
    if (m_serverStat)
        m_serverStat->setText(tr("正在检测 0/%1 台...").arg(m_serverTotal));

    for (ServerRowData &r : m_serverRows) {
        if (r.statusLbl) {
            r.statusLbl->setText(QStringLiteral("…"));
            r.statusLbl->setStyleSheet(QStringLiteral("color: #c3ccd8; font-size: 14px; font-weight: 600;"));
        }
        if (r.pingLbl) {
            r.pingLbl->setText(QStringLiteral("-"));
            r.pingLbl->setStyleSheet(QStringLiteral("color: #8a94a6; font-size: 12px;"));
        }
        if (r.playersLbl) {
            r.playersLbl->setText(QStringLiteral("-"));
            r.playersLbl->setStyleSheet(QStringLiteral("color: #8a94a6; font-size: 12px;"));
        }
    }

    QList<ServerStatusChecker::Target> targets;
    for (int i = 0; i < m_serverRows.size(); ++i) {
        ServerStatusChecker::Target t;
        t.host = m_serverRows.at(i).host;
        t.port = m_serverRows.at(i).port;
        t.row = i;
        targets.append(t);
    }
    m_statusChecker->checkAll(targets, 3000, 24);
}

void PluginPage::onServerJoinClicked(int row)
{
    if (row < 0 || row >= m_serverRows.size())
        return;
    const ServerRowData &r = m_serverRows.at(row);
    if (r.host.isEmpty())
        return;
    emit serverJoinRequested(r.host, r.port);
}

void PluginPage::showSettingsPage(int index)
{
    if (index < 0 || index >= m_plugins.size()) {
        showOverview();
        return;
    }
    m_currentDetailIndex = index;
    m_currentSettingsPlugin = m_plugins[index];
    m_settingsTitle->setText(tr("「%1」插件设置").arg(m_currentSettingsPlugin.name));
    rebuildSettingsForm(m_currentSettingsPlugin);
    m_stack->setCurrentWidget(m_settingsScroll);
}

void PluginPage::rebuildSettingsForm(const PluginInfo &info)
{
    // 清空旧表单
    while (QLayoutItem *item = m_settingsFormLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_settingFields.clear();

    if (info.settings.isEmpty()) {
        QLabel *empty = new QLabel(tr("该插件没有可配置的设置项。"), m_settingsPage);
        empty->setObjectName(QStringLiteral("pluginSettingEmpty"));
        empty->setAlignment(Qt::AlignCenter);
        m_settingsFormLayout->addRow(empty);
        return;
    }

    PluginManager *pm = PluginManager::instance();
    for (const PluginSettingItem &item : info.settings) {
        const QString current = pm->pluginSetting(info, item.key);
        const QString value = current.isEmpty() ? item.defaultValue : current;

        QWidget *field = createSettingField(item, value);
        if (!field)
            continue;

        QLabel *label = new QLabel(item.label.isEmpty() ? item.key : item.label, m_settingsPage);
        label->setObjectName(QStringLiteral("pluginSettingLabel"));
        label->setWordWrap(true);
        m_settingsFormLayout->addRow(label, field);
        m_settingFields.insert(item.key, field);
    }
}

QWidget *PluginPage::createSettingField(const PluginSettingItem &item,
                                        const QString &currentValue)
{
    const QString type = item.type;

    if (type == QLatin1String("bool")) {
        QCheckBox *box = new QCheckBox(m_settingsPage);
        box->setObjectName(QStringLiteral("pluginSettingCheck"));
        box->setChecked(currentValue == QLatin1String("true") || currentValue == QLatin1String("1"));
        return box;
    }

    if (type == QLatin1String("select")) {
        QComboBox *combo = new QComboBox(m_settingsPage);
        combo->setObjectName(QStringLiteral("pluginSettingCombo"));
        combo->addItems(item.options);
        const int idx = combo->findText(currentValue);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        else if (!currentValue.isEmpty())
            combo->addItem(currentValue);
        return combo;
    }

    if (type == QLatin1String("number")) {
        QDoubleSpinBox *spin = new QDoubleSpinBox(m_settingsPage);
        spin->setObjectName(QStringLiteral("pluginSettingSpin"));
        spin->setRange(-1e9, 1e9);
        spin->setDecimals(2);
        bool ok = false;
        const double d = currentValue.toDouble(&ok);
        spin->setValue(ok ? d : 0);
        return spin;
    }

    // text / color / 其他 → QLineEdit
    QLineEdit *edit = new QLineEdit(m_settingsPage);
    edit->setObjectName(QStringLiteral("pluginSettingEdit"));
    edit->setText(currentValue);
    return edit;
}

QString PluginPage::settingFieldValue(const PluginSettingItem &item) const
{
    QWidget *w = m_settingFields.value(item.key);
    if (!w)
        return QString();

    if (QCheckBox *box = qobject_cast<QCheckBox *>(w))
        return box->isChecked() ? QStringLiteral("true") : QStringLiteral("false");

    if (QComboBox *combo = qobject_cast<QComboBox *>(w))
        return combo->currentText();

    if (QDoubleSpinBox *spin = qobject_cast<QDoubleSpinBox *>(w))
        return QString::number(spin->value(), 'f', spin->decimals());

    if (QLineEdit *edit = qobject_cast<QLineEdit *>(w))
        return edit->text().trimmed();

    return QString();
}

void PluginPage::onSettingsSave()
{
    PluginManager *pm = PluginManager::instance();
    for (const PluginSettingItem &item : m_currentSettingsPlugin.settings)
        pm->setPluginSetting(m_currentSettingsPlugin, item.key, settingFieldValue(item));
    AppMessageBox::information(this, tr("插件设置"),
                               tr("「%1」的设置已保存。").arg(m_currentSettingsPlugin.name));
}

void PluginPage::onSettingsReset()
{
    // 清空存储值后按清单默认值重建表单
    PluginManager *pm = PluginManager::instance();
    for (const PluginSettingItem &item : m_currentSettingsPlugin.settings)
        pm->setPluginSetting(m_currentSettingsPlugin, item.key, QString());
    rebuildSettingsForm(m_currentSettingsPlugin);
}

void PluginPage::runCommand(const PluginInfo &info, const PluginCommand &cmd)
{
    // kind == "label"：仅作分类标题展示，不执行任何动作
    if (cmd.kind == QLatin1String("label"))
        return;

    if (cmd.kind == QLatin1String("url")) {
        if (!cmd.target.isEmpty())
            QDesktopServices::openUrl(QUrl(cmd.target));
        return;
    }

    if (cmd.kind == QLatin1String("script")) {
        // 把包内脚本解压到临时目录后打开
        if (cmd.target.isEmpty())
            return;
        const QString tmpDir = QDir::tempPath() + QStringLiteral("/BlockBox/")
                             + info.id;
        QString error;
        if (!PluginManager::instance()->extractPlugin(info.id, tmpDir, &error)) {
            AppMessageBox::warning(this, tr("打开脚本失败"), error);
            return;
        }
        const QString scriptPath = QDir::cleanPath(tmpDir + QStringLiteral("/") + cmd.target);
        if (!QFileInfo::exists(scriptPath)) {
            AppMessageBox::warning(this, tr("打开脚本失败"),
                                   tr("包内未找到脚本：%1").arg(cmd.target));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(scriptPath));
        return;
    }

    // command：
    //   - 以 "run:" 开头表示执行包内脚本（解压到临时目录后按扩展名选择解释器）
    //   - 否则视为命令输入框的快捷输入，通过系统 shell 执行
    if (cmd.kind == QLatin1String("command")) {
        if (cmd.target.startsWith(QLatin1String("run:"))) {
            const QString rel = cmd.target.mid(4).trimmed();
            if (rel.isEmpty()) {
                AppMessageBox::warning(this, tr("插件命令"), tr("命令目标为空"));
                return;
            }
            const QString tmpDir = QDir::tempPath() + QStringLiteral("/BlockBox/")
                                 + info.id;
            QString error;
            if (!PluginManager::instance()->extractPlugin(info.id, tmpDir, &error)) {
                AppMessageBox::warning(this, tr("插件命令"),
                                       tr("解包插件失败：%1").arg(error));
                return;
            }
            const QString scriptPath = QDir::cleanPath(tmpDir + QStringLiteral("/") + rel);
            if (!QFileInfo::exists(scriptPath)) {
                AppMessageBox::warning(this, tr("插件命令"),
                                       tr("包内未找到脚本：%1").arg(rel));
                return;
            }

            const QString nativePath = QDir::toNativeSeparators(scriptPath);
            const QString ext = QFileInfo(scriptPath).suffix().toLower();
            if (ext == QLatin1String("ps1")) {
                // 弹出进度对话框，同步执行并捕获 stdout 显示进度
                PluginRunnerDialog *dlg = new PluginRunnerDialog(this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->setWindowTitle(tr("「%1」- %2").arg(info.name,
                    cmd.label.isEmpty() ? cmd.id : cmd.label));
                dlg->setDialogTitle(tr("正在执行：%1").arg(cmd.label.isEmpty() ? cmd.id : cmd.label));
                dlg->startPs1(scriptPath);
                dlg->exec();
            } else if (ext == QLatin1String("bat") || ext == QLatin1String("cmd")) {
                QProcess::startDetached("cmd", QStringList() << "/c" << nativePath);
            } else {
                QDesktopServices::openUrl(QUrl::fromLocalFile(scriptPath));
            }
            return;
        }

        // 直接执行系统命令：信任已在添加插件时确认，此处直接执行
        if (!cmd.target.isEmpty()) {
            QProcess::startDetached(cmd.target);
            return;
        }
    }

    AppMessageBox::information(this, tr("插件命令"),
                               tr("插件「%1」的命令「%2」执行完成。")
                               .arg(info.name, cmd.label.isEmpty() ? cmd.id : cmd.label));
}

void PluginPage::handleNativeCommand(const PluginInfo &info, const QString &target)
{
    // 原生插件动作：仅当前插件为投影方块编辑器时提供"选择投影并编辑"
    if (info.id == QLatin1String("projection_editor")
        && (target == QLatin1String("native://select_projection")
            || target == QLatin1String("native://projection_editor")))
    {
        openProjectionInEditor(info);
        return;
    }
    AppMessageBox::information(this, tr("插件动作"),
                               tr("该原生插件暂不支持此操作：%1").arg(target));
}

void PluginPage::openProjectionInEditor(const PluginInfo &info)
{
    PluginManager *pm = PluginManager::instance();
    const QString defaultProj = pm->pluginSetting(info, QStringLiteral("defaultProjection")).trimmed();

    QString file;
    if (!defaultProj.isEmpty() && QFileInfo::exists(defaultProj)
        && defaultProj.endsWith(QStringLiteral(".litematic"), Qt::CaseInsensitive))
    {
        // 已配置默认投影且存在，直接打开
        file = defaultProj;
    }
    else
    {
        const QFileInfo def(defaultProj);
        const QString startDir = def.isDir() ? defaultProj
                                : (def.exists() ? def.absolutePath() : QString());
        file = AppFileDialog::getOpenFileName(this, tr("选择投影以编辑方块"),
                                              startDir,
                                              tr("投影文件 (*.litematic *.nbt *.schematic *.schem);;Litematica (*.litematic);;全部文件 (*)"));
        if (file.isEmpty())
            return;
    }

    if (!file.endsWith(QStringLiteral(".litematic"), Qt::CaseInsensitive))
    {
        AppMessageBox::information(this, tr("提示"),
            tr("仅支持编辑 .litematic 格式的投影。\n请选择 .litematic 文件，或先用「编辑投影」转换格式。"));
        return;
    }

    // 从插件详情页进入：让用户选择材质来源实例（可选，用于加载方块贴图）
    QString instancePath = pm->pluginSetting(info, QStringLiteral("sourceInstance")).trimmed();
    if (!QDir(instancePath).exists())
    {
        const QString picked = AppFileDialog::getExistingDirectory(
            this, tr("选择游戏实例（用于加载方块材质，可取消）"),
            instancePath);
        if (!picked.isEmpty())
        {
            instancePath = picked;
            pm->setPluginSetting(info, QStringLiteral("sourceInstance"), instancePath);
        }
    }

    // 由插件设置初始化编辑器（显示空气 / 默认工具等），并应用实例材质
    ProjectionBlockEditorDialog dlg(this);
    if (!instancePath.isEmpty())
        dlg.setTextureSource(instancePath);
    if (!dlg.loadProjection(file))
        return;
    dlg.exec();

    // 若发生了保存，把保存结果同步到"默认投影文件"设置
    const QString saved = dlg.lastSavedPath();
    if (!saved.isEmpty() && saved != defaultProj)
    {
        pm->setPluginSetting(info, QStringLiteral("defaultProjection"), saved);
    }
}

QPixmap PluginPage::loadPluginIcon(const PluginInfo &info, int size)
{
    QPixmap fallback(size, size);
    fallback.fill(Qt::transparent);
    {
        QPainter p(&fallback);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(ThemeManager::instance()->currentThemeColor()));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(0, 0, size, size, size / 4, size / 4);
        p.setPen(Qt::white);
        QFont font;
        font.setPixelSize(size * 3 / 5);
        font.setBold(true);
        p.setFont(font);
        const QString letter = info.name.isEmpty() ? QStringLiteral("?") : info.name.left(1).toUpper();
        p.drawText(fallback.rect(), Qt::AlignCenter, letter);
    }

    // 从包内提取图标
    if (!info.icon.isEmpty() && !info.filePath.isEmpty()) {
        QByteArray data;
        if (PluginZip::extractEntryToMemory(info.filePath, info.icon, data)) {
            QImage img;
            if (img.loadFromData(data)) {
                QPixmap px = QPixmap::fromImage(img)
                        .scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
                // 裁成圆形
                QPixmap rounded(size, size);
                rounded.fill(Qt::transparent);
                QPainter p(&rounded);
                p.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addEllipse(0, 0, size, size);
                p.setClipPath(path);
                p.drawPixmap((size - px.width()) / 2, (size - px.height()) / 2, px);
                return rounded;
            }
        }
    }

    return fallback;
}
