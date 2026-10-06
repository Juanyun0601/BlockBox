/**
 * @file   AccountManagePage.cpp
 * @brief  账户管理页面实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "AccountManagePage.h"
#include "MicrosoftLoginDialog.h"
#include "components/AppColorDialog.h"
#include "components/AppFileDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QShowEvent>
#include <QHideEvent>
#include <QTabBar>
#include <QImage>
#include "components/AppMessageBox.h"
#include <QPixmap>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSequentialAnimationGroup>
#include <QSpacerItem>
#include <QStyle>
#include <QTimer>

#include "components/NotificationManager.h"
#include "components/Skin3DWidget.h"
#include "utils/AuthManager.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/SkinDownloader.h"
#include "utils/ThemeManager.h"

AccountManagePage::AccountManagePage(QWidget *parent) : QWidget(parent)
{
    m_currentAccountName = "未登录";
    m_currentAccountType = "离线";
    m_currentUuid.clear();
    m_currentSkinUrl.clear();
    m_isDefaultAccount = false;
    m_accountCountLabel = nullptr;
    m_accountTypeDot = nullptr;
    m_skinActionWidget = nullptr;
    m_skinActionCombo = nullptr;
    m_skinExecuteActionBtn = nullptr;
    m_skinModelType = SkinModelType::Auto;
    m_modelActionWidget = nullptr;
    m_modelActionCombo = nullptr;
    m_modelExecuteActionBtn = nullptr;
    m_modelPose = SkinPose::Idle;
    m_bgWidget = nullptr;
    m_bgCombo = nullptr;
    m_customBgBtn = nullptr;
    m_bgImageBtn = nullptr;
    m_skinBackgroundKey = "theme";
    m_customBgColor = QColor(0x1e, 0x1e, 0x2e);
    m_bgImagePath.clear();
    m_iconNormalColor = QColor("#666666");
    m_iconSelectedColor = QColor("#ffffff");

    // 初始化皮肤下载器（必须在 initUI/loadAccounts 之前）
    m_skinDownloader = SkinDownloader::instance();

    initUI();
    loadAccounts();

    // 连接皮肤下载器信号
    connect(m_skinDownloader, &SkinDownloader::skinLoaded, this, [this](const QImage& texture)
    {
        if (m_skin3DWidget)
        {
            m_skin3DWidget->setSkin(texture);
        }
        // 同步截取皮肤头部正面作为顶栏账户头像
        applySkinAvatar(texture);
    });

    connect(m_skinDownloader, &SkinDownloader::skinLoadFailed, this, [this]()
    {
        // 加载失败时根据当前账户用户名回退到默认皮肤
        if (m_skin3DWidget)
        {
            m_skin3DWidget->setSkin(SkinDownloader::getDefaultSkinForUser(m_currentAccountName));
        }
    });

    // 连接主题变化信号（主题切换 + 颜色变化都更新背景色）
    if (ThemeManager::instance())
    {
        connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this, &AccountManagePage::onThemeColorChanged);
        connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](ThemeManager::ThemeType type)
        {
            // 仅在"跟随主题"背景下跟随主题色，否则保持用户选择的背景
            if (m_skin3DWidget && m_skinBackgroundKey == "theme")
                m_skin3DWidget->setThemeBackground(type == ThemeManager::DarkTheme);
        });
    }
}

AccountManagePage::~AccountManagePage()
{
}

void AccountManagePage::initUI()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Create stacked widget to switch between account management and login pages
    m_stackedWidget = new QStackedWidget(this);
    m_mainLayout->addWidget(m_stackedWidget);

    // Account management widget
    m_accountManageWidget = new QWidget();
    QHBoxLayout *accountManageLayout = new QHBoxLayout(m_accountManageWidget);
    accountManageLayout->setContentsMargins(0, 0, 0, 0);
    accountManageLayout->setSpacing(0);

    // Initialize left account list (悬浮气泡：不占布局，float 于左上)
    m_leftWidget = new QWidget(m_accountManageWidget);
    m_leftWidget->setObjectName("accountLeftSidebar");
    m_leftLayout = new QVBoxLayout(m_leftWidget);
    m_leftWidget->setAttribute(Qt::WA_StyledBackground);
    m_leftWidget->setFixedWidth(kBubbleWidth);
    initLeftAccountList();


    // 悬浮气泡外阴影（缩小模糊半径，避免在侧边栏与中间内容之间投出明显空隙）
    auto *bubbleShadow = new QGraphicsDropShadowEffect(m_leftWidget);
    bubbleShadow->setBlurRadius(8);
    bubbleShadow->setOffset(0, 3);
    bubbleShadow->setColor(QColor(0, 0, 0, 50));
    m_leftWidget->setGraphicsEffect(bubbleShadow);

    accountManageLayout->setContentsMargins(kBubbleWidth + kBubbleMargin, 0, 0, 0);

    // Initialize center skin display
    QWidget *centerWidget = new QWidget();
    m_centerLayout = new QVBoxLayout(centerWidget);
    initCenterSkinDisplay();
    centerWidget->setMinimumWidth(300);
    centerWidget->setMaximumWidth(400);

    // Initialize right account panel
    QWidget *rightWidget = new QWidget();
    m_rightLayout = new QVBoxLayout(rightWidget);
    initRightAccountPanel();
    rightWidget->setMinimumWidth(300);
    rightWidget->setMaximumWidth(400);

    // Add widgets to account management layout
    accountManageLayout->addWidget(centerWidget);
    accountManageLayout->addWidget(rightWidget);
    accountManageLayout->setStretchFactor(centerWidget, 1);
    accountManageLayout->setStretchFactor(rightWidget, 0);

    // 滑动高亮背景（与 InstanceSelectPage / SubNavPanel 一致的选中指示器）
    m_accountHighlight = new QWidget(m_accountScrollContent);
    m_accountHighlight->setObjectName("accountHighlight");
    m_accountHighlight->setVisible(false);
    // 不调用 lower()——否则会被布局内的 spacer/separator 完全遮挡

    // Login widget
    m_loginWidget = new QWidget();
    m_loginWidget->setObjectName("accountLoginPage");
    initLoginPage();

    // Add pages to stacked widget
    m_stackedWidget->addWidget(m_accountManageWidget);
    m_stackedWidget->addWidget(m_loginWidget);
}

void AccountManagePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLeftBubbleGeometry();
}

void AccountManagePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 页面切换动画期间尺寸可能变化，延迟到下一次事件循环重新贴靠气泡
    QTimer::singleShot(0, this, [this]() { updateLeftBubbleGeometry(); });
}

void AccountManagePage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
}

void AccountManagePage::updateLeftBubbleGeometry()
{
    if (!m_leftWidget)
    {
        return;
    }
    // 悬浮于账户管理页左上：x=间距，y=间距，宽度固定，高度 = 父容器高 - 上下间距
    const int x = kBubbleMargin;
    const int y = kBubbleMargin;
    const int h = qMax(120, m_accountManageWidget->height() - 2 * kBubbleMargin);
    m_leftWidget->setGeometry(x, y, kBubbleWidth, h);
    // 若正在展示登录页，气泡不应显示
    m_leftWidget->setVisible(m_stackedWidget->currentWidget() == m_accountManageWidget);
    m_leftWidget->raise();
}

void AccountManagePage::initLeftAccountList()
{
    // 与 InstanceSelectPage::initLeftSidebar 完全一致的布局参数
    m_leftLayout->setContentsMargins(8, 12, 8, 12);
    m_leftLayout->setSpacing(6);

    // --- 账户列表分组标题（含数量徽章） ---
    QWidget *accountsHeader = new QWidget();
    accountsHeader->setObjectName("accountSidebarHeader");
    QHBoxLayout *accountsHeaderLayout = new QHBoxLayout(accountsHeader);
    accountsHeaderLayout->setContentsMargins(4, 8, 4, 2);
    accountsHeaderLayout->setSpacing(6);

    QLabel *accountsTitle = new QLabel(tr("账户列表"));
    accountsTitle->setObjectName("accountSidebarSectionTitle");
    accountsHeaderLayout->addWidget(accountsTitle);
    accountsHeaderLayout->addStretch();

    m_accountCountLabel = new QLabel("0");
    m_accountCountLabel->setObjectName("accountSidebarCount");
    accountsHeaderLayout->addWidget(m_accountCountLabel);

    m_leftLayout->addWidget(accountsHeader);

    // --- 账户列表滚动区域（与 SubNavPanel 一致：可滚动、无边框） ---
    m_accountScrollArea = new QScrollArea();
    m_accountScrollArea->setObjectName("accountScrollArea");
    m_accountScrollArea->setWidgetResizable(true);
    m_accountScrollArea->setFrameShape(QFrame::NoFrame);
    m_accountScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_accountScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_accountScrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_accountScrollContent = new QWidget(m_accountScrollArea);
    m_accountScrollContent->setObjectName("accountScrollContent");
    m_accountContentLayout = new QVBoxLayout(m_accountScrollContent);
    m_accountContentLayout->setContentsMargins(0, 4, 0, 4);
    m_accountContentLayout->setSpacing(2);

    m_accountScrollArea->setWidget(m_accountScrollContent);
    m_leftLayout->addWidget(m_accountScrollArea);

    // --- 底部分隔线 + 添加账户按钮（与 instanceSidebarSeparator / instanceBindFolderBtn 一致） ---
    QFrame *bottomSeparator = new QFrame();
    bottomSeparator->setObjectName("accountSidebarSeparator");
    bottomSeparator->setFixedHeight(1);
    m_leftLayout->addWidget(bottomSeparator);

    // --- 皮肤制作按钮（位于创建账户按钮上方） ---
    m_skinEditorBtn = new QPushButton(tr("皮肤制作"));
    m_skinEditorBtn->setObjectName("accountSkinBtn");
    m_skinEditorBtn->setFixedHeight(34);
    m_skinEditorBtn->setIconSize(QSize(16, 16));
    m_skinEditorBtn->setIcon(loadColoredIcon(":/Images/Icons/nav_appearance.svg", m_iconNormalColor));
    m_skinEditorBtn->setCursor(Qt::PointingHandCursor);
    m_leftLayout->addWidget(m_skinEditorBtn);

    m_addAccountBtn = new QPushButton(tr("添加账户"));
    m_addAccountBtn->setObjectName("accountAddBtn");
    m_addAccountBtn->setFixedHeight(34);
    m_addAccountBtn->setIconSize(QSize(16, 16));
    m_addAccountBtn->setIcon(loadColoredIcon(":/Images/Icons/nav_folder_plus.svg", m_iconSelectedColor));
    m_addAccountBtn->setCursor(Qt::PointingHandCursor);
    m_leftLayout->addWidget(m_addAccountBtn);

    // Connect signals and slots
    connect(m_addAccountBtn, &QPushButton::clicked, this, &AccountManagePage::onAddAccountClicked);
    connect(m_skinEditorBtn, &QPushButton::clicked, this, [this]() {
        emit skinEditorRequested();
    });
}

void AccountManagePage::initCenterSkinDisplay()
{
    m_centerLayout->setContentsMargins(0, 0, 0, 0);
    m_centerLayout->setSpacing(0);

    // 3D 皮肤预览控件（填充整个中间区域）
    m_skin3DWidget = new Skin3DWidget();
    m_skin3DWidget->setObjectName("skin3DWidget");
    m_skin3DWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_skin3DWidget->setMinimumSize(300, 400);
    // 背景色根据当前主题自适应
    if (ThemeManager::instance())
    {
        const bool dark = (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
        m_skin3DWidget->setThemeBackground(dark);
    }
    else
    {
        m_skin3DWidget->setBackgroundColor(QColor(40, 40, 40));
    }

    // No account hint
    m_noAccountHint = new QLabel();
    m_noAccountHint->setObjectName("noAccountHint");
    m_noAccountHint->setText(tr("暂无账户"));
    m_noAccountHint->setAlignment(Qt::AlignCenter);

    m_addFirstAccountBtn = new QPushButton(tr("添加首个账户"));
    m_addFirstAccountBtn->setObjectName("addFirstAccountBtn");
    m_addFirstAccountBtn->setFixedHeight(40);
    // 宽度随文字长度自适应（不拉伸铺满）
    m_addFirstAccountBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    // 空态容器：提示与按钮在皮肤预览区中间垂直/水平居中
    m_emptyStateWidget = new QWidget();
    m_emptyStateWidget->setObjectName("accountEmptyState");
    QVBoxLayout *emptyStateLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyStateLayout->setContentsMargins(0, 0, 0, 0);
    emptyStateLayout->setSpacing(4);
    emptyStateLayout->addStretch(1);
    emptyStateLayout->addWidget(m_noAccountHint, 0, Qt::AlignHCenter);
    emptyStateLayout->addWidget(m_addFirstAccountBtn, 0, Qt::AlignHCenter);
    emptyStateLayout->addStretch(1);

    // Add to layout（skin3DWidget 占主区域；无账户时空态容器占同一区域）
    m_centerLayout->addWidget(m_skin3DWidget, 1);
    m_centerLayout->addWidget(m_emptyStateWidget, 1);
    m_emptyStateWidget->hide();

    // Connect signals and slots
    connect(m_addFirstAccountBtn, &QPushButton::clicked, this, &AccountManagePage::onAddAccountClicked);
}

void AccountManagePage::initSkinModelActions()
{
    // --- 皮肤动作（一行：标签 + 下拉 + 执行） ---
    m_skinActionWidget = new QWidget();
    QHBoxLayout *skinActionRow = new QHBoxLayout(m_skinActionWidget);
    skinActionRow->setContentsMargins(0, 0, 0, 0);
    skinActionRow->setSpacing(8);

    QLabel *skinActionTitle = new QLabel(tr("皮肤动作"));
    skinActionTitle->setObjectName("accountTypeLabel");
    skinActionRow->addWidget(skinActionTitle);

    m_skinActionCombo = new QComboBox();
    m_skinActionCombo->setObjectName("skinActionCombo");
    m_skinActionCombo->addItem(tr("向左旋转 15°"), "rotate_left");
    m_skinActionCombo->addItem(tr("向右旋转 15°"), "rotate_right");
    m_skinActionCombo->addItem(tr("放大"), "zoom_in");
    m_skinActionCombo->addItem(tr("缩小"), "zoom_out");
    m_skinActionCombo->addItem(tr("重置视角"), "reset_view");
    m_skinActionCombo->addItem(tr("自动旋转：开"), "auto_rotate_on");
    m_skinActionCombo->addItem(tr("自动旋转：关"), "auto_rotate_off");
    m_skinActionCombo->addItem(tr("切换模型 (Steve/Alex)"), "toggle_model");
    m_skinActionCombo->setCursor(Qt::PointingHandCursor);
    skinActionRow->addWidget(m_skinActionCombo, 1);

    m_skinExecuteActionBtn = new QPushButton(tr("执行"));
    m_skinExecuteActionBtn->setObjectName("actionButton");
    m_skinExecuteActionBtn->setFixedHeight(30);
    m_skinExecuteActionBtn->setCursor(Qt::PointingHandCursor);
    skinActionRow->addWidget(m_skinExecuteActionBtn);

    // --- 模型动作（一行：标签 + 下拉 + 执行） ---
    m_modelActionWidget = new QWidget();
    QHBoxLayout *modelActionRow = new QHBoxLayout(m_modelActionWidget);
    modelActionRow->setContentsMargins(0, 0, 0, 0);
    modelActionRow->setSpacing(8);

    QLabel *modelActionTitle = new QLabel(tr("模型动作"));
    modelActionTitle->setObjectName("accountTypeLabel");
    modelActionRow->addWidget(modelActionTitle);

    m_modelActionCombo = new QComboBox();
    m_modelActionCombo->setObjectName("skinActionCombo");
    m_modelActionCombo->addItem(tr("待机"), "pose_idle");
    m_modelActionCombo->addItem(tr("挥手"), "pose_wave");
    m_modelActionCombo->addItem(tr("走路"), "pose_walk");
    m_modelActionCombo->addItem(tr("跑步"), "pose_run");
    m_modelActionCombo->addItem(tr("骑马坐姿"), "pose_ride");
    m_modelActionCombo->setCursor(Qt::PointingHandCursor);
    modelActionRow->addWidget(m_modelActionCombo, 1);

    m_modelExecuteActionBtn = new QPushButton(tr("执行"));
    m_modelExecuteActionBtn->setObjectName("actionButton");
    m_modelExecuteActionBtn->setFixedHeight(30);
    m_modelExecuteActionBtn->setCursor(Qt::PointingHandCursor);
    modelActionRow->addWidget(m_modelExecuteActionBtn);

    // --- 模型背景（两行：标签+下拉+自定义颜色 / 选择图片） ---
    m_bgWidget = new QWidget();
    QVBoxLayout *bgLayout = new QVBoxLayout(m_bgWidget);
    bgLayout->setContentsMargins(0, 0, 0, 0);
    bgLayout->setSpacing(6);

    QHBoxLayout *bgSelectLayout = new QHBoxLayout();
    bgSelectLayout->setSpacing(8);
    QLabel *bgTitle = new QLabel(tr("模型背景"));
    bgTitle->setObjectName("accountTypeLabel");
    bgSelectLayout->addWidget(bgTitle);

    initBackgroundPresetCombo();
    m_bgCombo->setCursor(Qt::PointingHandCursor);
    bgSelectLayout->addWidget(m_bgCombo, 1);

    m_customBgBtn = new QPushButton(tr("自定义"));
    m_customBgBtn->setObjectName("actionButton");
    m_customBgBtn->setFixedHeight(30);
    m_customBgBtn->setCursor(Qt::PointingHandCursor);
    bgSelectLayout->addWidget(m_customBgBtn);
    bgLayout->addLayout(bgSelectLayout);

    // 图片背景选择按钮（铺满一行）
    m_bgImageBtn = new QPushButton(tr("选择图片..."));
    m_bgImageBtn->setObjectName("actionButton");
    m_bgImageBtn->setFixedHeight(30);
    m_bgImageBtn->setCursor(Qt::PointingHandCursor);
    bgLayout->addWidget(m_bgImageBtn);

    // Connect signals and slots
    connect(m_skinExecuteActionBtn, &QPushButton::clicked, this, &AccountManagePage::onExecuteSkinActionClicked);
    connect(m_modelExecuteActionBtn, &QPushButton::clicked, this, &AccountManagePage::onExecuteModelActionClicked);
    connect(m_bgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AccountManagePage::onBackgroundPresetChanged);
    connect(m_customBgBtn, &QPushButton::clicked, this, &AccountManagePage::onCustomBackgroundClicked);
    connect(m_bgImageBtn, &QPushButton::clicked, this, &AccountManagePage::onSelectBackgroundImageClicked);

    // 恢复上次选择的背景（selectBackgroundPreset 会触发 currentIndexChanged → 应用并持久化）
    if (SettingsManager::instance())
    {
        const QString saved = SettingsManager::instance()->getProperty("skinModelBackground", "theme").toString();
        selectBackgroundPreset(saved);
    }
}

void AccountManagePage::initRightAccountPanel()
{
    m_rightLayout->setContentsMargins(10, 10, 10, 10);
    m_rightLayout->setSpacing(10);

    // Account name
    m_accountNameLabel = new QLabel(m_currentAccountName);
    m_accountNameLabel->setObjectName("accountNameTitle");
    m_rightLayout->addWidget(m_accountNameLabel);

    // Account type and default tag
    QHBoxLayout *accountInfoLayout = new QHBoxLayout();
    accountInfoLayout->setSpacing(6);
    m_accountTypeDot = new QLabel();
    m_accountTypeDot->setObjectName("accountTypeDot");
    m_accountTypeDot->setFixedSize(8, 8);
    m_accountTypeDot->setStyleSheet("background-color: #F59E0B; border-radius: 4px;");
    accountInfoLayout->addWidget(m_accountTypeDot);

    m_accountTypeLabel = new QLabel(m_currentAccountType);
    m_accountTypeLabel->setObjectName("accountTypeLabel");
    accountInfoLayout->addWidget(m_accountTypeLabel);

    m_defaultAccountLabel = new QLabel(tr("默认账户"));
    m_defaultAccountLabel->setObjectName("defaultAccountLabel");
    m_defaultAccountLabel->hide();
    accountInfoLayout->addWidget(m_defaultAccountLabel);
    accountInfoLayout->addStretch();
    m_rightLayout->addLayout(accountInfoLayout);

    // 皮肤/模型动作与模型背景统一放入右侧账户面板区域
    initSkinModelActions();
    m_rightLayout->addWidget(m_skinActionWidget);
    m_rightLayout->addWidget(m_modelActionWidget);
    m_rightLayout->addWidget(m_bgWidget);

    // Add spacer
    m_rightLayout->addStretch();

    // Action buttons
    QVBoxLayout *actionButtonsLayout = new QVBoxLayout();
    actionButtonsLayout->setSpacing(10);

    m_switchAccountBtn = new QPushButton(tr("切换账户"));
    m_switchAccountBtn->setObjectName("primaryActionButton");
    m_switchAccountBtn->setFixedHeight(40);
    actionButtonsLayout->addWidget(m_switchAccountBtn);

    m_logoutBtn = new QPushButton(tr("登出"));
    m_logoutBtn->setObjectName("logoutButton");
    m_logoutBtn->setFixedHeight(36);
    actionButtonsLayout->addWidget(m_logoutBtn);

    m_setDefaultBtn = new QPushButton(tr("设为默认"));
    m_setDefaultBtn->setObjectName("actionButton");
    m_setDefaultBtn->setFixedHeight(36);
    actionButtonsLayout->addWidget(m_setDefaultBtn);

    // --- 更多操作（一行：标签 + 下拉 + 执行） ---
    QHBoxLayout *actionSelectLayout = new QHBoxLayout();
    actionSelectLayout->setSpacing(8);
    QLabel *actionTitle = new QLabel(tr("更多操作"));
    actionTitle->setObjectName("accountTypeLabel");
    actionSelectLayout->addWidget(actionTitle);

    m_actionCombo = new QComboBox();
    m_actionCombo->setObjectName("accountActionCombo");
    m_actionCombo->addItem(tr("复制用户名"), "copy_username");
    m_actionCombo->addItem(tr("复制 UUID"), "copy_uuid");
    m_actionCombo->addItem(tr("刷新皮肤"), "refresh_skin");
    m_actionCombo->addItem(tr("打开皮肤编辑器"), "open_skin_editor");
    m_actionCombo->addItem(tr("删除账户"), "delete_account");
    m_actionCombo->setCursor(Qt::PointingHandCursor);
    actionSelectLayout->addWidget(m_actionCombo, 1);

    m_executeActionBtn = new QPushButton(tr("执行"));
    m_executeActionBtn->setObjectName("actionButton");
    m_executeActionBtn->setFixedHeight(30);
    m_executeActionBtn->setCursor(Qt::PointingHandCursor);
    actionSelectLayout->addWidget(m_executeActionBtn);
    actionButtonsLayout->addLayout(actionSelectLayout);

    m_rightLayout->addLayout(actionButtonsLayout);

    // Connect signals and slots
    connect(m_switchAccountBtn, &QPushButton::clicked, this, &AccountManagePage::onSwitchAccountClicked);
    connect(m_logoutBtn, &QPushButton::clicked, this, &AccountManagePage::onLogoutClicked);
    connect(m_setDefaultBtn, &QPushButton::clicked, this, &AccountManagePage::onSetDefaultClicked);
    connect(m_executeActionBtn, &QPushButton::clicked, this, &AccountManagePage::onExecuteActionClicked);
}

namespace {

/** 表单字段：标签在上、控件在下（对照 HTML 原型 .form-field） */
QWidget *buildLoginField(const QString &labelText, QWidget *editor, QWidget *parent)
{
    auto *field = new QWidget(parent);
    auto *fieldLayout = new QVBoxLayout(field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(6);

    auto *label = new QLabel(labelText, field);
    label->setObjectName(QStringLiteral("loginFieldLabel"));
    fieldLayout->addWidget(label);
    fieldLayout->addWidget(editor);
    return field;
}

/** 表单提示条：说明该登录方式的能力与限制（对照 HTML 原型 .form-hint） */
QLabel *buildLoginHint(const QString &text, QWidget *parent)
{
    auto *hint = new QLabel(text, parent);
    hint->setObjectName(QStringLiteral("loginHint"));
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return hint;
}

} // namespace

void AccountManagePage::initLoginPage()
{
    QVBoxLayout *loginLayout = new QVBoxLayout(m_loginWidget);
    loginLayout->setContentsMargins(24, 24, 24, 24);
    loginLayout->setSpacing(12);

    // 居中单列卡片：标题 + 分段登录方式 + 表单（对照 HTML 原型 .account-add）
    QWidget *column = new QWidget(m_loginWidget);
    column->setObjectName("loginColumn");
    column->setFixedWidth(560);
    QVBoxLayout *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(24, 22, 24, 24);
    columnLayout->setSpacing(0);

    QLabel *titleLabel = new QLabel(tr("添加账户"), column);
    titleLabel->setObjectName("loginTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    columnLayout->addWidget(titleLabel);

    QLabel *subtitleLabel = new QLabel(tr("选择一种登录方式，把新账户加入方块盒子"), column);
    subtitleLabel->setObjectName("loginSubtitle");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    columnLayout->addWidget(subtitleLabel);
    columnLayout->addSpacing(16);

    // 分段控件：三个登录方式等宽铺满（QTabWidget 自带 TabBar 高度不随容器拉伸，隐藏后自绘）
    QWidget *segBar = new QWidget(column);
    segBar->setObjectName("loginSegBar");
    QHBoxLayout *segLayout = new QHBoxLayout(segBar);
    segLayout->setContentsMargins(4, 4, 4, 4);
    segLayout->setSpacing(4);

    const QStringList segLabels = {tr("离线"), tr("微软正版"), tr("第三方")};
    QList<QPushButton *> segButtons;
    for (int i = 0; i < segLabels.size(); ++i)
    {
        QPushButton *segBtn = new QPushButton(segLabels.at(i), segBar);
        segBtn->setObjectName("loginSegBtn");
        segBtn->setCheckable(true);
        segBtn->setAutoExclusive(true);
        segBtn->setCursor(Qt::PointingHandCursor);
        segBtn->setChecked(i == 0);
        segLayout->addWidget(segBtn, 1);
        segButtons.append(segBtn);
    }
    columnLayout->addWidget(segBar);
    columnLayout->addSpacing(12);

    m_loginTabWidget = new QTabWidget(column);
    m_loginTabWidget->setObjectName("loginMethodTabs");
    m_loginTabWidget->tabBar()->hide();   // 用上面的分段控件切换登录方式
    columnLayout->addWidget(m_loginTabWidget);

    loginLayout->addStretch();
    loginLayout->addWidget(column, 0, Qt::AlignHCenter);
    loginLayout->addStretch();

    // Microsoft login tab（品牌化 hero：图标 + 标题 + 说明 + 能力标签）
    m_microsoftLoginTab = new QWidget();
    QVBoxLayout *microsoftLayout = new QVBoxLayout(m_microsoftLoginTab);
    microsoftLayout->setContentsMargins(0, 0, 0, 0);
    microsoftLayout->setSpacing(12);

    QLabel *heroTile = new QLabel(m_microsoftLoginTab);
    heroTile->setObjectName("loginHeroTile");
    heroTile->setFixedSize(64, 64);
    heroTile->setAlignment(Qt::AlignCenter);
    heroTile->setPixmap(
        IconHelper::loadColoredIcon(":/Images/Icons/grid.svg", QColor("#00A4EF"), 28).pixmap(28, 28));
    QHBoxLayout *heroIconRow = new QHBoxLayout();
    heroIconRow->addStretch();
    heroIconRow->addWidget(heroTile);
    heroIconRow->addStretch();
    microsoftLayout->addLayout(heroIconRow);

    QLabel *heroTitle = new QLabel(tr("微软正版登录"), m_microsoftLoginTab);
    heroTitle->setObjectName("loginHeroTitle");
    heroTitle->setAlignment(Qt::AlignCenter);
    microsoftLayout->addWidget(heroTitle);

    QLabel *heroDesc = new QLabel(tr("跳转浏览器完成授权，登录后自动同步正版皮肤、成就与联机身份。"),
                                  m_microsoftLoginTab);
    heroDesc->setObjectName("loginHeroDesc");
    heroDesc->setAlignment(Qt::AlignCenter);
    heroDesc->setWordWrap(true);
    microsoftLayout->addWidget(heroDesc);

    QHBoxLayout *chipRow = new QHBoxLayout();
    chipRow->setSpacing(8);
    chipRow->addStretch();
    const QStringList featureChips = {tr("正版联机"), tr("皮肤同步"), tr("成就同步")};
    for (const QString &chipText : featureChips)
    {
        QLabel *chip = new QLabel(chipText, m_microsoftLoginTab);
        chip->setObjectName("loginChip");
        chip->setAlignment(Qt::AlignCenter);
        chipRow->addWidget(chip);
    }
    chipRow->addStretch();
    microsoftLayout->addLayout(chipRow);
    microsoftLayout->addSpacing(4);

    // Login progress
    QProgressBar *microsoftProgressBar = new QProgressBar();
    microsoftProgressBar->setObjectName("microsoftProgressBar");
    microsoftProgressBar->setMinimum(0);
    microsoftProgressBar->setMaximum(100);
    microsoftProgressBar->setValue(0);
    microsoftProgressBar->setVisible(false);
    microsoftLayout->addWidget(microsoftProgressBar);

    // Status label
    QLabel *microsoftStatusLabel = new QLabel(tr(""));
    microsoftStatusLabel->setObjectName("microsoftStatusLabel");
    microsoftStatusLabel->setAlignment(Qt::AlignCenter);
    microsoftStatusLabel->setWordWrap(true);
    microsoftLayout->addWidget(microsoftStatusLabel);

    microsoftLayout->addStretch();
    const QString microsoftBtnLabel = tr("使用 Microsoft 账户登录");
    QPushButton *microsoftLoginBtn = new QPushButton(microsoftBtnLabel);
    microsoftLoginBtn->setObjectName("loginPrimaryBtn");
    microsoftLoginBtn->setMinimumHeight(44);
    microsoftLoginBtn->setCursor(Qt::PointingHandCursor);
    microsoftLayout->addWidget(microsoftLoginBtn);

    // Connect Microsoft login button
    connect(microsoftLoginBtn, &QPushButton::clicked, [=]() {
        // Disable button and show loading state
        microsoftLoginBtn->setEnabled(false);
        microsoftLoginBtn->setText(tr("登录中..."));
        microsoftProgressBar->setVisible(true);
        microsoftProgressBar->setValue(0);
        microsoftStatusLabel->setText(tr("正在启动登录流程..."));
        
        // Create and show Microsoft login dialog
        MicrosoftLoginDialog *loginDialog = new MicrosoftLoginDialog(this);
        
        // Connect dialog signals
        connect(loginDialog, &MicrosoftLoginDialog::loginSucceeded, [=](const QString &username, const QString &accessToken, const QString &refreshToken) {
            // Enable button and reset state
            microsoftLoginBtn->setEnabled(true);
            microsoftLoginBtn->setText(microsoftBtnLabel);
            microsoftProgressBar->setVisible(false);
            microsoftStatusLabel->setText(tr("登录成功！"));
            
            // Add Microsoft account
            if (SettingsManager::instance())
            {
                SettingsManager::instance()->addMicrosoftAccount(username, accessToken, refreshToken, "", "", true);
            }
            
            // Switch back to account list and reload accounts
            m_stackedWidget->setCurrentWidget(m_accountManageWidget);
            loadAccounts();
            
            AppMessageBox::information(this, tr("成功"), tr("Microsoft账户登录成功"));
            
            // Clean up
            loginDialog->deleteLater();
        });
        
        connect(loginDialog, &MicrosoftLoginDialog::loginFailed, [=]() {
            // Enable button and reset state
            microsoftLoginBtn->setEnabled(true);
            microsoftLoginBtn->setText(microsoftBtnLabel);
            microsoftProgressBar->setVisible(false);
            microsoftStatusLabel->setText(tr("登录失败"));
            
            // Clean up
            loginDialog->deleteLater();
        });
        
        connect(loginDialog, &MicrosoftLoginDialog::loginCanceled, [=]() {
            // Enable button and reset state
            microsoftLoginBtn->setEnabled(true);
            microsoftLoginBtn->setText(microsoftBtnLabel);
            microsoftProgressBar->setVisible(false);
            microsoftStatusLabel->setText(tr("登录已取消"));
            
            // Clean up
            loginDialog->deleteLater();
        });
        
        // Start login process
        loginDialog->startLogin();
        loginDialog->exec();
    });

    // Offline login tab
    m_offlineLoginTab = new QWidget();
    QVBoxLayout *offlineLayout = new QVBoxLayout(m_offlineLoginTab);
    offlineLayout->setContentsMargins(0, 0, 0, 0);
    offlineLayout->setSpacing(14);

    // Username input
    QLineEdit *usernameInput = new QLineEdit(m_offlineLoginTab);
    usernameInput->setObjectName("loginInput");
    usernameInput->setPlaceholderText(tr("输入游戏内显示的名称，例如 Steve"));
    usernameInput->setClearButtonEnabled(true);
    usernameInput->setMaxLength(16);
    offlineLayout->addWidget(buildLoginField(tr("玩家名"), usernameInput, m_offlineLoginTab));

    offlineLayout->addWidget(buildLoginHint(
        tr("离线账户无需联网验证，适合单机与局域网游戏；无法使用正版服务器、皮肤同步与成就。"),
        m_offlineLoginTab));

    offlineLayout->addStretch();
    QPushButton *offlineLoginBtn = new QPushButton(tr("创建离线账户"));
    offlineLoginBtn->setObjectName("loginPrimaryBtn");
    offlineLoginBtn->setMinimumHeight(44);
    offlineLoginBtn->setCursor(Qt::PointingHandCursor);
    offlineLayout->addWidget(offlineLoginBtn);
    
    // Connect offline login button
    connect(offlineLoginBtn, &QPushButton::clicked, [=]() {
        QString username = usernameInput->text().trimmed();
        if (username.isEmpty()) {
            NotificationManager::showError(this, tr("用户名不能为空"));
            return;
        }
        
        // Check if account already exists
        if (SettingsManager::instance() && SettingsManager::instance()->accountExists(username)) {
            NotificationManager::showError(this, tr("该用户名已存在"));
            return;
        }
        
        // Add offline account
        if (SettingsManager::instance())
        {
            SettingsManager::instance()->addAccount(username, tr("离线"), "", true);
        }
        
        // Switch back to account list and reload accounts
        m_stackedWidget->setCurrentWidget(m_accountManageWidget);
        loadAccounts();
        
        NotificationManager::showSuccess(this, tr("离线账户创建成功"));
    });

    // Third party login tab
    m_thirdPartyLoginTab = new QWidget();
    QVBoxLayout *thirdPartyLayout = new QVBoxLayout(m_thirdPartyLoginTab);
    thirdPartyLayout->setContentsMargins(0, 0, 0, 0);
    thirdPartyLayout->setSpacing(14);

    // Server selection（服务器 + 连通状态徽标）
    QComboBox *serverComboBox = new QComboBox(m_thirdPartyLoginTab);
    serverComboBox->setObjectName("loginCombo");
    serverComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    // Load auth servers from settings
    QList<AuthServerInfo> servers;
    if (SettingsManager::instance())
    {
        servers = SettingsManager::instance()->getAuthServers();
    }
    for (const AuthServerInfo &server : servers) {
        serverComboBox->addItem(server.name, server.url);
    }

    // Add option to add custom server
    serverComboBox->addItem(tr("添加自定义服务器"), "custom");

    QLabel *serverStatusLabel = new QLabel(tr("未检测"));
    serverStatusLabel->setObjectName("loginStatusPill");
    serverStatusLabel->setAlignment(Qt::AlignCenter);

    QWidget *serverFieldEditor = new QWidget(m_thirdPartyLoginTab);
    QHBoxLayout *serverFieldRow = new QHBoxLayout(serverFieldEditor);
    serverFieldRow->setContentsMargins(0, 0, 0, 0);
    serverFieldRow->setSpacing(8);
    serverFieldRow->addWidget(serverComboBox, 1);
    serverFieldRow->addWidget(serverStatusLabel, 0, Qt::AlignVCenter);
    thirdPartyLayout->addWidget(buildLoginField(tr("认证服务器"), serverFieldEditor, m_thirdPartyLoginTab));

    // Username input
    QLineEdit *thirdPartyUsernameInput = new QLineEdit(m_thirdPartyLoginTab);
    thirdPartyUsernameInput->setObjectName("loginInput");
    thirdPartyUsernameInput->setPlaceholderText(tr("认证服务器上的账户名或邮箱"));
    thirdPartyUsernameInput->setClearButtonEnabled(true);
    thirdPartyLayout->addWidget(
        buildLoginField(tr("用户名"), thirdPartyUsernameInput, m_thirdPartyLoginTab));

    // Password input
    QLineEdit *thirdPartyPasswordInput = new QLineEdit(m_thirdPartyLoginTab);
    thirdPartyPasswordInput->setObjectName("loginInput");
    thirdPartyPasswordInput->setEchoMode(QLineEdit::Password);
    thirdPartyPasswordInput->setPlaceholderText(tr("输入密码"));
    thirdPartyLayout->addWidget(
        buildLoginField(tr("密码"), thirdPartyPasswordInput, m_thirdPartyLoginTab));

    thirdPartyLayout->addWidget(buildLoginHint(
        tr("支持 LittleSkin 等 Yggdrasil 兼容认证服务器；账号密码仅发送至所选服务器。"),
        m_thirdPartyLoginTab));

    thirdPartyLayout->addStretch();
    QPushButton *thirdPartyLoginBtn = new QPushButton(tr("登录"));
    thirdPartyLoginBtn->setObjectName("loginPrimaryBtn");
    thirdPartyLoginBtn->setMinimumHeight(44);
    thirdPartyLoginBtn->setCursor(Qt::PointingHandCursor);
    thirdPartyLayout->addWidget(thirdPartyLoginBtn);
    
    // Connect server selection change
    connect(serverComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int) {
        QString serverUrl = serverComboBox->currentData().toString();
        if (serverUrl == "custom") {
            // Show dialog to add custom server
            QDialog customServerDialog(this);
            customServerDialog.setWindowTitle(tr("添加自定义服务器"));
            customServerDialog.resize(400, 200);
            
            QVBoxLayout *dialogLayout = new QVBoxLayout(&customServerDialog);
            
            QLabel *nameLabel = new QLabel(tr("服务器名称："));
            dialogLayout->addWidget(nameLabel);
            QLineEdit *nameInput = new QLineEdit();
            dialogLayout->addWidget(nameInput);
            
            QLabel *urlLabel = new QLabel(tr("服务器 URL："));
            dialogLayout->addWidget(urlLabel);
            QLineEdit *urlInput = new QLineEdit();
            urlInput->setPlaceholderText("例如：https://littleskin.cn/api/yggdrasil/");
            dialogLayout->addWidget(urlInput);
            
            QHBoxLayout *buttonLayout = new QHBoxLayout();
            QPushButton *cancelBtn = new QPushButton(tr("取消"));
            cancelBtn->setObjectName("cancelButton");
            QPushButton *okBtn = new QPushButton(tr("确定"));
            okBtn->setObjectName("okBtn");
            buttonLayout->addStretch();
            buttonLayout->addWidget(cancelBtn);
            buttonLayout->addWidget(okBtn);
            dialogLayout->addLayout(buttonLayout);
            
            connect(cancelBtn, &QPushButton::clicked, &customServerDialog, &QDialog::reject);
            connect(okBtn, &QPushButton::clicked, &customServerDialog, &QDialog::accept);
            
            if (customServerDialog.exec() == QDialog::Accepted) {
                QString customName = nameInput->text().trimmed();
                QString customUrl = urlInput->text().trimmed();
                
                if (!customName.isEmpty() && !customUrl.isEmpty()) {
                    // Add custom server to settings
                    if (SettingsManager::instance())
                    {
                        SettingsManager::instance()->addAuthServer(customUrl, customName);
                    }
                    
                    // Update server combo box
                    serverComboBox->insertItem(serverComboBox->count() - 1, customName, customUrl);
                    serverComboBox->setCurrentIndex(serverComboBox->count() - 2);
                }
            } else {
                // Reset to previous selection
                if (serverComboBox->count() > 1) {
                    serverComboBox->setCurrentIndex(0);
                }
            }
        } else if (!serverUrl.isEmpty()) {
            // Update server status
            serverStatusLabel->setText(tr("检测中..."));
            
            // TODO: Implement actual server status check
            // For now, just simulate a successful check
            QTimer::singleShot(1000, [=]() {
                serverStatusLabel->setText(tr("在线"));
            });
        } else {
            serverStatusLabel->setText(tr("未检测"));
        }
    });
    
    // 保存当前输入控件的指针，以便在槽函数中使用
    QLineEdit *usernameInputPtr = thirdPartyUsernameInput;
    QLineEdit *passwordInputPtr = thirdPartyPasswordInput;
    QPushButton *loginBtnPtr = thirdPartyLoginBtn;
    QComboBox *serverComboBoxPtr = serverComboBox;
    QLabel *statusLabelPtr = serverStatusLabel;
    
    // 连接认证成功信号
    if (AuthManager::instance())
    {
        connect(AuthManager::instance(), &AuthManager::authenticationSucceeded, this, [=](const QString &username, const QString &serverUrl) {
        // 恢复按钮状态
        loginBtnPtr->setText(tr("登录"));
        loginBtnPtr->setEnabled(true);
        
        // 获取服务器名称
        QString serverName = serverComboBoxPtr->currentText();
        
        // 检查是否账户已存在
        if (SettingsManager::instance() && SettingsManager::instance()->accountExists(username)) {
            NotificationManager::showError(this, tr("该用户名已存在"));
            return;
        }
        
        // 添加第三方账户
        if (SettingsManager::instance())
        {
            SettingsManager::instance()->addAccount(username, serverName, serverUrl, true);
        }
        
        // 切换回账户列表并重新加载账户
        m_stackedWidget->setCurrentWidget(m_accountManageWidget);
        loadAccounts();
        
        NotificationManager::showSuccess(this, tr("第三方账户登录成功"));
        
        // 清空输入字段
        usernameInputPtr->clear();
        passwordInputPtr->clear();
    });
    }
    
    // 连接认证失败信号
    if (AuthManager::instance())
    {
        connect(AuthManager::instance(), &AuthManager::authenticationFailed, this, [=](const QString &errorMessage) {
        // 恢复按钮状态
        loginBtnPtr->setText(tr("登录"));
        loginBtnPtr->setEnabled(true);
        
        // 显示错误信息
        NotificationManager::showError(this, errorMessage);
    });
    }
    
    // Connect third party login button
    connect(thirdPartyLoginBtn, &QPushButton::clicked, [=]() {
        QString username = thirdPartyUsernameInput->text().trimmed();
        QString password = thirdPartyPasswordInput->text();
        QString serverUrl = serverComboBox->currentData().toString();
        
        if (username.isEmpty() || password.isEmpty()) {
            NotificationManager::showError(this, tr("用户名和密码不能为空"));
            return;
        }
        
        if (serverUrl == "custom") {
            NotificationManager::showError(this, tr("请选择一个有效的服务器"));
            return;
        }
        
        // 显示加载状态
        thirdPartyLoginBtn->setText(tr("登录中..."));
        thirdPartyLoginBtn->setEnabled(false);
        statusLabelPtr->setText(tr("认证中..."));
        
        // 开始认证
        if (AuthManager::instance())
        {
            AuthManager::instance()->authenticate(username, password, serverUrl);
        }
    });

    // Add tabs to tab widget（顺序与 HTML 原型一致：离线 / 微软正版 / 第三方）
    m_loginTabWidget->addTab(m_offlineLoginTab, tr("离线"));
    m_loginTabWidget->addTab(m_microsoftLoginTab, tr("微软正版"));
    m_loginTabWidget->addTab(m_thirdPartyLoginTab, tr("第三方"));

    // Connect signals and slots
    connect(m_loginTabWidget, &QTabWidget::currentChanged, this, &AccountManagePage::onLoginMethodChanged);

    // 分段控件 <-> 登录方式页 双向同步
    for (int i = 0; i < segButtons.size(); ++i)
    {
        QPushButton *segBtn = segButtons.at(i);
        connect(segBtn, &QPushButton::clicked, this, [this, i]() {
            m_loginTabWidget->setCurrentIndex(i);
        });
    }
    connect(m_loginTabWidget, &QTabWidget::currentChanged, this,
            [segButtons](int index) {
                for (int i = 0; i < segButtons.size(); ++i)
                {
                    segButtons.at(i)->setChecked(i == index);
                }
            });

    updateLoginColumnHeight();
}

/**
 * 卡片高度 = 标题区 + 当前登录方式表单的实测高度 + 内边距。
 *
 * 直接依赖 QWidget::sizeHint() 不行：它会被缓存成构建期的旧值（按所有页的最大高度算），
 * 而表单实际是按 512px 固定宽度换行的，两者差出一大截，离线页因此空出两百多像素。
 */
void AccountManagePage::updateLoginColumnHeight()
{
    if (!m_loginWidget || !m_loginTabWidget)
    {
        return;
    }
    QWidget *column = m_loginWidget->findChild<QWidget *>(QStringLiteral("loginColumn"));
    if (!column)
    {
        return;
    }
    QLayout *columnLayout = column->layout();
    if (!columnLayout)
    {
        return;
    }

    // 卡片宽度固定 560，表单区宽度 = 卡片宽 - 左右内边距
    const QMargins cm = columnLayout->contentsMargins();
    int paneWidth = column->width() - cm.left() - cm.right();
    if (paneWidth <= 0)
    {
        paneWidth = column->minimumWidth() - cm.left() - cm.right();
    }
    if (paneWidth <= 0)
    {
        paneWidth = 560 - cm.left() - cm.right();
    }

    QWidget *page = m_loginTabWidget->currentWidget();
    int pageHeight = (page && page->layout()) ? page->layout()->heightForWidth(paneWidth) : 0;
    if (pageHeight <= 0 && page)
    {
        pageHeight = page->sizeHint().height();
    }
    if (pageHeight <= 0)
    {
        pageHeight = 200;
    }
    m_loginTabWidget->setFixedHeight(pageHeight);

    // 逐项累加：除表单区外各项高度稳定，避开 QWidget 的 sizeHint 缓存
    int totalHeight = cm.top() + cm.bottom();
    for (int i = 0; i < columnLayout->count(); ++i)
    {
        QLayoutItem *item = columnLayout->itemAt(i);
        if (!item)
        {
            continue;
        }
        const bool isPane = item->widget() && item->widget() == m_loginTabWidget;
        totalHeight += isPane ? pageHeight : item->sizeHint().height();
    }
    totalHeight += (columnLayout->count() - 1) * qMax(0, columnLayout->spacing());

    column->setFixedHeight(totalHeight);
}

void AccountManagePage::loadAccounts()
{
    // 清空旧的账户按钮
    clearAccountButtons();

    // Load accounts from settings
    QList<AccountInfo> accounts;
    if (SettingsManager::instance())
    {
        accounts = SettingsManager::instance()->getAccounts();
    }

    // 更新左侧栏账户数量徽章
    if (m_accountCountLabel)
    {
        m_accountCountLabel->setText(QString::number(accounts.count()));
    }

    // Add each account to the list
    for (const AccountInfo &account : accounts) {
        addAccountItem(account.username, account.type, account.serverUrl, account.isDefault, account.skinUrl, account.uuid);
    }

    // 底部弹簧，保持账户按钮顶部对齐
    m_accountContentLayout->addStretch();

    // 重置滚动位置到顶部
    m_accountScrollArea->verticalScrollBar()->setValue(0);

    // Update UI based on whether there are accounts
    if (m_accountButtons.isEmpty()) {
        m_skin3DWidget->hide();
        if (m_skinActionWidget) m_skinActionWidget->hide();
        if (m_modelActionWidget) m_modelActionWidget->hide();
        if (m_bgWidget) m_bgWidget->hide();
        if (m_emptyStateWidget) m_emptyStateWidget->show();
        m_accountHighlight->setVisible(false);
    } else {
        m_skin3DWidget->show();
        if (m_skinActionWidget) m_skinActionWidget->show();
        if (m_modelActionWidget) m_modelActionWidget->show();
        if (m_bgWidget) m_bgWidget->show();
        if (m_emptyStateWidget) m_emptyStateWidget->hide();

        // 入场动画：与 InstanceSelectPage / SubNavPanel 一致的交错淡入
        animateAccountEntrance();

        // 默认选中默认账户（或第一个账户），延迟到下次事件循环以确保 geometry 就绪
        QPushButton *defaultBtn = nullptr;
        QPushButton *firstBtn = m_accountButtons.first();
        for (QPushButton *btn : m_accountButtons)
        {
            if (btn->property("isDefault").toBool())
            {
                defaultBtn = btn;
                break;
            }
        }
        QPushButton *targetBtn = defaultBtn ? defaultBtn : firstBtn;

        QTimer::singleShot(0, this, [this, targetBtn]()
        {
            setSelectedAccount(targetBtn);
            // 触发选中后的数据加载（皮肤等）
            onAccountItemClicked(targetBtn);
        });
    }
}

void AccountManagePage::addAccountItem(const QString &accountName, const QString &accountType, const QString &serverUrl, bool isDefault, const QString &skinUrl, const QString &uuid)
{
    // 与 InstanceSelectPage 文件夹按钮一致的创建方式
    QPushButton *btn = new QPushButton(accountName);
    btn->setObjectName("accountNavBtn");
    btn->setFixedHeight(34);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIconSize(QSize(16, 16));
    btn->setProperty("accountName", accountName);
    btn->setProperty("accountType", accountType);
    btn->setProperty("serverUrl", serverUrl);
    btn->setProperty("isDefault", isDefault);
    btn->setProperty("skinUrl", skinUrl);
    btn->setProperty("uuid", uuid);
    btn->setProperty("iconPath", ":/Images/Icons/nav_folder.svg");
    btn->setIcon(loadColoredIcon(":/Images/Icons/nav_folder.svg", m_iconNormalColor));

    QString tooltip = accountType + (!serverUrl.isEmpty() ? " - " + serverUrl : "");
    if (isDefault)
    {
        tooltip += tr("（默认）");
    }
    btn->setToolTip(tooltip);

    connect(btn, &QPushButton::clicked, this, [this, btn]()
    {
        setSelectedAccount(btn);
        onAccountItemClicked(btn);
    });

    m_accountButtons.append(btn);
    m_accountContentLayout->addWidget(btn);
}

void AccountManagePage::clearAccountButtons()
{
    if (m_accountHighlight)
    {
        m_accountHighlight->setVisible(false);
    }

    QLayoutItem *item;
    while ((item = m_accountContentLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
        {
            delete item->widget();
        }
        delete item;
    }
    m_accountButtons.clear();
}

void AccountManagePage::updateCurrentAccountInfo(const QString &accountName, const QString &accountType, bool isDefault)
{
    m_accountNameLabel->setText(accountName);
    m_accountTypeLabel->setText(accountType);

    // 账户类型彩色圆点：微软=蓝 / 第三方=紫 / 离线=琥珀
    if (m_accountTypeDot)
    {
        QColor dotColor("#F59E0B");
        if (accountType.contains(tr("微软")))
            dotColor = QColor("#00A4EF");
        else if (accountType.contains(tr("第三方")))
            dotColor = QColor("#8B5CF6");
        m_accountTypeDot->setStyleSheet(QString("background-color: %1; border-radius: 4px;").arg(dotColor.name()));
    }

    if (isDefault) {
        m_defaultAccountLabel->show();
    } else {
        m_defaultAccountLabel->hide();
    }
}

void AccountManagePage::onAccountItemClicked(QPushButton *btn)
{
    if (!btn) return;

    // Get account information from button properties
    QString accountName = btn->property("accountName").toString();
    QString accountType = btn->property("accountType").toString();
    QString serverUrl = btn->property("serverUrl").toString();
    bool isDefault = btn->property("isDefault").toBool();
    QString skinUrl = btn->property("skinUrl").toString();
    QString uuid = btn->property("uuid").toString();

    // Update current account info
    updateCurrentAccountInfo(accountName, accountType, isDefault);

    // Update member variables
    m_currentAccountName = accountName;
    m_currentAccountType = accountType;
    m_currentUuid = uuid;
    m_currentSkinUrl = skinUrl;
    m_isDefaultAccount = isDefault;

    // Emit account selected signal
    emit accountSelected(accountName);

    // 多来源优先级加载链路：UUID > URL > 本地离线文件 > 默认皮肤
    if (m_skin3DWidget)
    {
        if (!uuid.isEmpty() && m_skinDownloader)
        {
            // 优先级 1：通过 UUID 调用 Crafatar API 下载正版/第三方账户皮肤
            m_skinDownloader->downloadSkinByUuid(uuid);
        }
        else if (!skinUrl.isEmpty() && m_skinDownloader)
        {
            // 优先级 2：通过用户名 URL 下载第三方账户皮肤
            m_skinDownloader->downloadSkin(accountName, skinUrl);
        }
        else
        {
            // 优先级 3：从本地离线皮肤目录 Images/Skins/{用户名}.png 加载
            QImage offlineSkin;
            if (SkinDownloader::loadOfflineSkin(accountName, offlineSkin))
            {
                m_skin3DWidget->setSkin(offlineSkin);
                applySkinAvatar(offlineSkin);
                return;
            }
            // 优先级 4：回退到默认皮肤（根据用户名匹配官方皮肤，否则返回 Steve）
            QImage defaultSkin = SkinDownloader::getDefaultSkinForUser(accountName);
            m_skin3DWidget->setSkin(defaultSkin);
            applySkinAvatar(defaultSkin);
        }
    }
}

void AccountManagePage::onAddAccountClicked()
{
    // Switch to login page
    m_stackedWidget->setCurrentWidget(m_loginWidget);
    updateLoginColumnHeight();

    // Emit signal to notify that add account page is opened
    emit addAccountPageOpened();
}

void AccountManagePage::onSwitchAccountClicked()
{
    // Account switching is handled by clicking on account items in the list
    // This function could be used for additional switch logic if needed
    AppMessageBox::information(this, tr("切换账户"), tr("请在左侧账户列表中选择要切换的账户"));
}

void AccountManagePage::onLogoutClicked()
{
    // Show confirmation dialog
    AppMessageBox::StandardButton reply;
    reply = AppMessageBox::question(this, tr("确认删除"), tr("确定要删除当前账户吗？"),
                                  AppMessageBox::Yes|AppMessageBox::No);
    if (reply == AppMessageBox::Yes) {
        // Remove account from settings
        QString username = m_accountNameLabel->text();
        if (SettingsManager::instance())
        {
            SettingsManager::instance()->removeAccount(username);
        }

        // Reload accounts (内部会清空旧按钮)
        loadAccounts();

        // Reset current account info
        m_currentAccountName = "未登录";
        m_currentAccountType = "离线";
        m_currentUuid.clear();
        m_currentSkinUrl.clear();
        m_isDefaultAccount = false;
        // 重置模型动作到待机
        m_modelPose = SkinPose::Idle;
        if (m_skin3DWidget)
            m_skin3DWidget->setPose(SkinPose::Idle);
        updateCurrentAccountInfo(m_currentAccountName, m_currentAccountType, m_isDefaultAccount);

        AppMessageBox::information(this, tr("删除成功"), tr("已成功删除当前账户"));
    }
}

void AccountManagePage::onSetDefaultClicked()
{
    // Set current account as default
    QString username = m_accountNameLabel->text();
    if (SettingsManager::instance())
    {
        SettingsManager::instance()->setDefaultAccount(username);
    }

    // Reload accounts to update default status (内部会清空旧按钮)
    loadAccounts();

    // Update UI
    m_defaultAccountLabel->show();
    AppMessageBox::information(this, tr("设置成功"), tr("已将当前账户设为默认账户"));
}

void AccountManagePage::onExecuteActionClicked()
{
    if (!m_actionCombo)
    {
        return;
    }

    // 未选中任何账户时（"未登录"状态）不允许执行需要账户的动作
    const bool hasAccount = (m_currentAccountName != "未登录" && !m_accountButtons.isEmpty());
    const QString action = m_actionCombo->currentData().toString();

    if (action == "copy_username")
    {
        if (!hasAccount)
        {
            NotificationManager::showError(this, tr("请先选择一个账户"));
            return;
        }
        QApplication::clipboard()->setText(m_currentAccountName);
        NotificationManager::showSuccess(this, tr("已复制用户名：%1").arg(m_currentAccountName));
    }
    else if (action == "copy_uuid")
    {
        if (!hasAccount)
        {
            NotificationManager::showError(this, tr("请先选择一个账户"));
            return;
        }
        if (m_currentUuid.isEmpty())
        {
            NotificationManager::showError(this, tr("当前账户没有 UUID"));
            return;
        }
        QApplication::clipboard()->setText(m_currentUuid);
        NotificationManager::showSuccess(this, tr("已复制 UUID：%1").arg(m_currentUuid));
    }
    else if (action == "refresh_skin")
    {
        if (!hasAccount)
        {
            NotificationManager::showError(this, tr("请先选择一个账户"));
            return;
        }
        refreshSkinForAccount(m_currentAccountName);
        NotificationManager::showSuccess(this, tr("已发送皮肤刷新请求"));
    }
    else if (action == "open_skin_editor")
    {
        emit skinEditorRequested();
    }
    else if (action == "delete_account")
    {
        // 复用登出按钮的确认 + 删除逻辑（含成员变量清空与界面刷新）
        onLogoutClicked();
    }
}

void AccountManagePage::onExecuteSkinActionClicked()
{
    if (!m_skinActionCombo || !m_skin3DWidget)
    {
        return;
    }

    const QString action = m_skinActionCombo->currentData().toString();

    if (action == "rotate_left")
    {
        // 参考 skinview3d 的旋转步进（15° ≈ 0.26 rad）
        m_skin3DWidget->rotateView(-0.26f, 0.0f);
    }
    else if (action == "rotate_right")
    {
        m_skin3DWidget->rotateView(0.26f, 0.0f);
    }
    else if (action == "zoom_in")
    {
        m_skin3DWidget->zoomView(1.15f);
    }
    else if (action == "zoom_out")
    {
        m_skin3DWidget->zoomView(0.87f);
    }
    else if (action == "reset_view")
    {
        m_skin3DWidget->resetView();
    }
    else if (action == "auto_rotate_on")
    {
        m_skin3DWidget->setAutoRotate(true);
        NotificationManager::showSuccess(this, tr("已开启自动旋转"));
    }
    else if (action == "auto_rotate_off")
    {
        m_skin3DWidget->setAutoRotate(false);
        NotificationManager::showSuccess(this, tr("已关闭自动旋转"));
    }
    else if (action == "toggle_model")
    {
        // Steve(宽型) <-> Alex(纤细) 交替切换
        m_skinModelType = (m_skinModelType == SkinModelType::Slim)
                              ? SkinModelType::Default
                              : SkinModelType::Slim;
        m_skin3DWidget->setModelType(m_skinModelType);
        NotificationManager::showSuccess(this,
            m_skinModelType == SkinModelType::Slim ? tr("已切换为纤细模型 (Alex)") : tr("已切换为宽型模型 (Steve)"));
    }
}

void AccountManagePage::onExecuteModelActionClicked()
{
    if (!m_modelActionCombo || !m_skin3DWidget)
    {
        return;
    }

    const QString action = m_modelActionCombo->currentData().toString();

    if (action == "pose_idle")
    {
        m_modelPose = SkinPose::Idle;
        m_skin3DWidget->setPose(SkinPose::Idle);
        NotificationManager::showSuccess(this, tr("已切换为待机"));
    }
    else if (action == "pose_wave")
    {
        m_modelPose = SkinPose::Wave;
        m_skin3DWidget->setPose(SkinPose::Wave);
        NotificationManager::showSuccess(this, tr("已切换为挥手"));
    }
    else if (action == "pose_walk")
    {
        m_modelPose = SkinPose::Walk;
        m_skin3DWidget->setPose(SkinPose::Walk);
        NotificationManager::showSuccess(this, tr("已切换为走路"));
    }
    else if (action == "pose_run")
    {
        m_modelPose = SkinPose::Run;
        m_skin3DWidget->setPose(SkinPose::Run);
        NotificationManager::showSuccess(this, tr("已切换为跑步"));
    }
    else if (action == "pose_ride")
    {
        m_modelPose = SkinPose::Ride;
        m_skin3DWidget->setPose(SkinPose::Ride);
        NotificationManager::showSuccess(this, tr("已切换为骑马坐姿"));
    }
}

void AccountManagePage::onLoginMethodChanged(int index)
{
    // 切换登录方式后收缩/撑开卡片，避免短表单留下空白
    updateLoginColumnHeight();
    qDebug() << "[AccountManagePage]" << "Login method changed to:" << index;
}

void AccountManagePage::onBackToAccountList()
{
    // Switch back to account management page
    m_stackedWidget->setCurrentWidget(m_accountManageWidget);
    
    // Emit signal to notify that account manage page is opened
    emit accountManagePageOpened();
}

void AccountManagePage::onThemeColorChanged(const QString &color)
{
    // 更新默认账户标签的颜色
    if (m_defaultAccountLabel) {
        m_defaultAccountLabel->setStyleSheet(QString("background-color: %1; color: white; padding: 2px 8px; border-radius: 10px; font-size: 12px;").arg(color));
    }
    // 3D 皮肤预览背景色随主题切换（仅在"跟随主题"背景下生效）
    if (m_skin3DWidget && ThemeManager::instance() && m_skinBackgroundKey == "theme")
    {
        const bool dark = (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
        m_skin3DWidget->setThemeBackground(dark);
    }
}

// =============================================================================
// 皮肤模型背景选择（模型背景下拉框）
// =============================================================================

namespace {

struct SkinBgPreset
{
    const char *key;
    const char *label;      // 需经 tr() 翻译
    bool gradient;
    const char *color1;     // 纯色 / 渐变顶部色（#RRGGBB）
    const char *color2;     // 渐变底部色（#RRGGBB），纯色时为空
};

const SkinBgPreset kSkinBackgroundPresets[] = {
    {"theme",       "跟随主题",   false, "#1e1e2e", ""},
    {"solid_dark",  "深色",       false, "#1e1e2e", ""},
    {"solid_light", "浅色",       false, "#d9d9e0", ""},
    {"solid_black", "纯黑",       false, "#000000", ""},
    {"solid_white", "纯白",       false, "#ffffff", ""},
    {"grad_ocean",  "海洋蓝渐变", true,  "#1a2980", "#26d0ce"},
    {"grad_forest", "森林绿渐变", true,  "#0f3443", "#34e89e"},
    {"grad_sunset", "日落橙渐变", true,  "#ff9966", "#ff5e62"},
    {"grad_night",  "夜幕紫渐变", true,  "#0f0c29", "#302b63"},
};

} // namespace

void AccountManagePage::initBackgroundPresetCombo()
{
    m_bgCombo = new QComboBox();
    m_bgCombo->setObjectName("skinActionCombo");
    for (const SkinBgPreset &preset : kSkinBackgroundPresets)
    {
        m_bgCombo->addItem(tr(preset.label), QString::fromLatin1(preset.key));
    }
}

void AccountManagePage::applySkinBackgroundPreset(const QString &presetKey)
{
    if (!m_skin3DWidget)
    {
        return;
    }

    // 自定义纯色
    if (presetKey.startsWith("custom:"))
    {
        const QColor color(presetKey.mid(7));
        if (color.isValid())
        {
            m_customBgColor = color;
        }
        m_skin3DWidget->setBackgroundColor(m_customBgColor);
        return;
    }

    // 图片背景
    if (presetKey.startsWith("image:"))
    {
        const QString path = presetKey.mid(6);
        if (QFile::exists(path))
        {
            QImage image(path);
            if (!image.isNull())
            {
                m_bgImagePath = path;
                m_skin3DWidget->setBackgroundImage(image);
                return;
            }
        }
        // 图片缺失或无法加载：回退到跟随主题
        m_bgImagePath.clear();
        const bool dark = ThemeManager::instance()
                          && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
        m_skin3DWidget->setThemeBackground(dark);
        return;
    }

    for (const SkinBgPreset &preset : kSkinBackgroundPresets)
    {
        if (presetKey == QString::fromLatin1(preset.key))
        {
            if (presetKey == "theme")
            {
                const bool dark = ThemeManager::instance()
                                  && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
                m_skin3DWidget->setThemeBackground(dark);
            }
            else if (preset.gradient)
            {
                m_skin3DWidget->setBackgroundGradient(QColor(QString::fromLatin1(preset.color1)),
                                                      QColor(QString::fromLatin1(preset.color2)));
            }
            else
            {
                m_skin3DWidget->setBackgroundColor(QColor(QString::fromLatin1(preset.color1)));
            }
            return;
        }
    }
}

void AccountManagePage::selectBackgroundPreset(const QString &presetKey)
{
    if (!m_bgCombo)
    {
        return;
    }

    for (int i = 0; i < m_bgCombo->count(); ++i)
    {
        if (m_bgCombo->itemData(i).toString() == presetKey)
        {
            m_bgCombo->setCurrentIndex(i);
            return;
        }
    }

    // 自定义颜色：更新已有自定义项或追加
    if (presetKey.startsWith("custom:"))
    {
        for (int i = 0; i < m_bgCombo->count(); ++i)
        {
            if (m_bgCombo->itemData(i).toString().startsWith("custom:"))
            {
                m_bgCombo->setItemData(i, presetKey);
                m_bgCombo->setCurrentIndex(i);
                return;
            }
        }
        m_bgCombo->addItem(tr("自定义颜色"), presetKey);
        m_bgCombo->setCurrentIndex(m_bgCombo->count() - 1);
        return;
    }

    // 自定义图片：更新已有图片项或追加
    if (presetKey.startsWith("image:"))
    {
        for (int i = 0; i < m_bgCombo->count(); ++i)
        {
            if (m_bgCombo->itemData(i).toString().startsWith("image:"))
            {
                m_bgCombo->setItemData(i, presetKey);
                m_bgCombo->setCurrentIndex(i);
                return;
            }
        }
        m_bgCombo->addItem(tr("自定义图片"), presetKey);
        m_bgCombo->setCurrentIndex(m_bgCombo->count() - 1);
    }
}

void AccountManagePage::onBackgroundPresetChanged(int index)
{
    if (index < 0 || !m_bgCombo)
    {
        return;
    }
    const QString key = m_bgCombo->itemData(index).toString();
    if (key.isEmpty())
    {
        return;
    }
    m_skinBackgroundKey = key;
    applySkinBackgroundPreset(key);
    saveSkinBackgroundPreset();
}

void AccountManagePage::onCustomBackgroundClicked()
{
    QColor color = AppColorDialog::getColor(m_customBgColor, this, tr("自定义背景颜色"));
    if (!color.isValid())
    {
        return;
    }
    m_customBgColor = color;
    const QString key = QString("custom:%1").arg(color.name());
    m_skinBackgroundKey = key;
    applySkinBackgroundPreset(key);
    selectBackgroundPreset(key);
    saveSkinBackgroundPreset();
}

void AccountManagePage::onSelectBackgroundImageClicked()
{
    const QString filePath = AppFileDialog::getOpenFileName(
        this, tr("选择背景图片"),
        m_bgImagePath,
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*)"));

    if (filePath.isEmpty())
    {
        return;
    }

    QImage image(filePath);
    if (image.isNull())
    {
        NotificationManager::showError(this, tr("无法加载该图片文件"));
        return;
    }

    m_bgImagePath = filePath;
    const QString key = QString("image:%1").arg(filePath);
    m_skinBackgroundKey = key;
    m_skin3DWidget->setBackgroundImage(image);
    selectBackgroundPreset(key);
    saveSkinBackgroundPreset();
}

void AccountManagePage::saveSkinBackgroundPreset()
{
    if (SettingsManager::instance())
    {
        SettingsManager::instance()->setProperty("skinModelBackground", m_skinBackgroundKey);
    }
}

// =============================================================================
// 以下方法的实现与 InstanceSelectPage / SubNavPanel 完全一致：
// - setSelectedAccount: 清除其他按钮选中态 + 切换图标颜色 + 滑动高亮到目标
// - slideHighlightTo:   高亮滑块的几何动画（OutCubic, 250ms）
// - animateAccountEntrance: 交错淡入入场动画（OutCubic, 120ms, 间隔 25ms）
// - loadColoredIcon:    通过 IconHelper 加载着色后的图标
// =============================================================================

void AccountManagePage::setSelectedAccount(QPushButton *target)
{
    if (!target) return;

    // 清除所有按钮的选中状态与图标颜色
    for (QPushButton *btn : m_accountButtons)
    {
        btn->setChecked(false);
        QString iconPath = btn->property("iconPath").toString();
        if (!iconPath.isEmpty())
        {
            btn->setIcon(loadColoredIcon(iconPath, m_iconNormalColor));
        }
    }

    // 设置目标按钮选中 + 切换图标为选中色
    target->setChecked(true);
    QString iconPath = target->property("iconPath").toString();
    if (!iconPath.isEmpty())
    {
        target->setIcon(loadColoredIcon(iconPath, m_iconSelectedColor));
    }

    // 滑动高亮到目标按钮
    slideHighlightTo(target);
}

void AccountManagePage::slideHighlightTo(QPushButton *target)
{
    if (!target || !m_accountHighlight) return;

    QRect endRect = target->geometry();
    // 按钮刚创建时 geometry 可能尚未就绪，延迟到下一次事件循环重试，
    // 确保高亮定位准确（调用方无需关心时序）
    if (!endRect.isValid() || endRect.width() <= 0)
    {
        QPushButton *btnPtr = target;
        QTimer::singleShot(0, this, [this, btnPtr]() { slideHighlightTo(btnPtr); });
        return;
    }

    m_accountHighlight->setVisible(true);

    auto *anim = new QPropertyAnimation(m_accountHighlight, "geometry");
    anim->setDuration(250);
    anim->setStartValue(m_accountHighlight->geometry());
    anim->setEndValue(endRect);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AccountManagePage::animateAccountEntrance()
{
    if (m_accountButtons.isEmpty()) return;

    auto *group = new QSequentialAnimationGroup(this);

    for (QPushButton *btn : m_accountButtons)
    {
        auto *effect = new QGraphicsOpacityEffect(btn);
        effect->setOpacity(0.0);
        btn->setGraphicsEffect(effect);

        auto *anim = new QPropertyAnimation(effect, "opacity");
        anim->setDuration(120);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);

        group->addPause(25);
        group->addAnimation(anim);
    }

    connect(group, &QSequentialAnimationGroup::finished, this, [this]()
    {
        for (QPushButton *btn : m_accountButtons)
        {
            if (btn)
            {
                btn->setGraphicsEffect(nullptr);
            }
        }
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

QIcon AccountManagePage::loadColoredIcon(const QString &path, const QColor &color) const
{
    return IconHelper::loadColoredIcon(path, color, 16);
}

QString AccountManagePage::currentAccountName() const
{
    return m_currentAccountName;
}

void AccountManagePage::refreshSkinForAccount(const QString &username)
{
    // 优先级 1：从离线皮肤目录 Images/Skins/{用户名}.png 加载（离线账户首选来源）
    QImage offlineSkin;
    if (SkinDownloader::loadOfflineSkin(username, offlineSkin) && m_skin3DWidget)
    {
        m_skin3DWidget->setSkin(offlineSkin);
        applySkinAvatar(offlineSkin);
        return;
    }
    // 优先级 2：从 SkinDownloader 缓存加载（正版/第三方账户下载缓存的皮肤）
    QString cachePath = SkinDownloader::getCachePath(username);
    QFileInfo fileInfo(cachePath);
    if (fileInfo.exists())
    {
        QImage skin(cachePath);
        if (!skin.isNull() && m_skin3DWidget)
        {
            m_skin3DWidget->setSkin(skin);
            applySkinAvatar(skin);
            return;
        }
    }
    // 优先级 3：回退到默认皮肤
    if (m_skin3DWidget)
    {
        QImage defaultSkin = SkinDownloader::getDefaultSkinForUser(username);
        m_skin3DWidget->setSkin(defaultSkin);
        applySkinAvatar(defaultSkin);
    }
}

void AccountManagePage::applySkinAvatar(const QImage &texture)
{
    QImage head = SkinDownloader::cropHeadPortrait(texture);
    if (!head.isNull())
    {
        emit accountSkinUpdated(QPixmap::fromImage(head));
    }
}

void AccountManagePage::refreshSkin3DWidget()
{
    if (m_skin3DWidget)
    {
        // 同步重绘：立即触发 paintGL，确保 GL 渲染在 QGraphicsEffect 移除后立即恢复
        m_skin3DWidget->repaint();
    }
}
