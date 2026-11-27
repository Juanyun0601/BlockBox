/**
 * @file   UserAgreementDialog.cpp
 * @brief  《方块盒子使用协议》《方块盒子隐私政策》的确认弹窗与全文阅读弹窗
 * @author BlockBox Team
 * @date   2026-11-20
 *
 * 首次启动方块盒子、登录 / 注册论坛账号前都会经过这里；两个入口各记一份
 * 同意状态，条款版本升级后重新询问。关于页也提供全文入口，方便随时回看。
 */
#include "UserAgreementDialog.h"

#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

const char kAgreementCardName[] = "legalAgreementCard";
const char kDocumentCardName[] = "legalDocumentCard";
const char kAppVersionKey[] = "legal/appAgreementVersion";
const char kForumVersionKey[] = "legal/forumAgreementVersion";

/// 条款有实质修改时把版本号 +1，用户下次启动 / 登录会重新看到确认弹窗
constexpr int kCurrentAgreementVersion = 2;

} // namespace

QString UserAgreementDialog::agreementVersionKey(Scope scope)
{
    return QString::fromLatin1(scope == ForumLogin ? kForumVersionKey : kAppVersionKey);
}

int UserAgreementDialog::currentAgreementVersion()
{
    return kCurrentAgreementVersion;
}

UserAgreementDialog::Decision UserAgreementDialog::decide(Scope scope, QWidget *parent)
{
    SettingsManager *settings = SettingsManager::instance();
    const QString key = agreementVersionKey(scope);
    if (settings->getProperty(key, 0).toInt() >= currentAgreementVersion())
        return Accepted;

    UserAgreementDialog dialog(scope, parent);
    if (dialog.exec() == QDialog::Accepted) {
        settings->setProperty(key, currentAgreementVersion());
        return Accepted;
    }

    // 用户点了「不同意并退出」才是真拒绝；点遮罩、返回键关掉弹窗都不算，
    // 否则启动瞬间的一次误触就会让整个软件直接退出（表现为点图标一闪就没）
    return dialog.m_declined ? Declined : Dismissed;
}

bool UserAgreementDialog::ensureAccepted(Scope scope, QWidget *parent)
{
    return decide(scope, parent) == Accepted;
}

void UserAgreementDialog::showDocument(Document document, QWidget *parent)
{
    UserAgreementDocumentDialog dialog(document, parent);
    dialog.exec();
}

QString UserAgreementDialog::documentText(Document document)
{
    return document == PrivacyPolicy ? privacyHtml() : termsHtml();
}

UserAgreementDialog::UserAgreementDialog(Scope scope, QWidget *parent)
    : AppDialogBase(parent)
    , m_scope(scope)
{
    setObjectName(QStringLiteral("userAgreementDialog"));
    setWindowTitle(scope == ForumLogin ? tr("登录前请阅读协议")
                                       : tr("方块盒子使用协议与隐私政策"));

    initUI();
    initStyle();
}

void UserAgreementDialog::initUI()
{
    const bool firstLaunch = (m_scope == FirstLaunch);

    auto *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kAgreementCardName));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setMinimumSize(500, 0);
    card->setMaximumWidth(560);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(10);

    // ── 标题栏 ──
    auto *header = new QWidget(card);
    header->setAttribute(Qt::WA_StyledBackground, false);
    auto *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(2, 2, 2, 2);
    headerRow->setSpacing(10);

    auto *icon = new QLabel(header);
    icon->setObjectName(QStringLiteral("legalHeaderIcon"));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_StyledBackground, true);
    icon->setPixmap(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/nav_about.svg"),
                                                QStringLiteral("#ffffff"),
                                                16).pixmap(16, 16));
    headerRow->addWidget(icon);

    auto *title = new QLabel(firstLaunch ? tr("欢迎使用方块盒子") : tr("登录方块盒子论坛"), header);
    title->setObjectName(QStringLiteral("legalTitle"));
    headerRow->addWidget(title, 1);
    cardLayout->addWidget(header);

    auto *intro = new QLabel(firstLaunch
                                 ? tr("开始使用前，请阅读并同意以下条款：")
                                 : tr("登录或注册论坛账号前，请阅读并同意以下条款："),
                             card);
    intro->setObjectName(QStringLiteral("legalIntro"));
    intro->setWordWrap(true);
    cardLayout->addWidget(intro);

    // ── 两份文档的入口 ──
    auto makeDocumentRow = [this, card](const QString &name, const QString &summary, Document document) {
        auto *row = new QWidget(card);
        row->setObjectName(QStringLiteral("legalDocRow"));
        row->setAttribute(Qt::WA_StyledBackground, true);

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(14, 10, 12, 10);
        rowLayout->setSpacing(12);

        auto *textColumn = new QWidget(row);
        textColumn->setAttribute(Qt::WA_StyledBackground, false);
        auto *textLayout = new QVBoxLayout(textColumn);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(3);

        auto *nameLabel = new QLabel(name, textColumn);
        nameLabel->setObjectName(QStringLiteral("legalDocName"));
        textLayout->addWidget(nameLabel);

        auto *summaryLabel = new QLabel(summary, textColumn);
        summaryLabel->setObjectName(QStringLiteral("legalDocSummary"));
        summaryLabel->setWordWrap(true);
        textLayout->addWidget(summaryLabel);

        rowLayout->addWidget(textColumn, 1);

        auto *viewButton = new QPushButton(tr("查看全文"), row);
        viewButton->setObjectName(QStringLiteral("legalDocLink"));
        viewButton->setCursor(Qt::PointingHandCursor);
        viewButton->setFlat(true);
        connect(viewButton, &QPushButton::clicked, this, [document]() {
            UserAgreementDialog::showDocument(document);
        });
        rowLayout->addWidget(viewButton, 0, Qt::AlignVCenter);

        return row;
    };

    cardLayout->addWidget(makeDocumentRow(
        tr("《方块盒子使用协议》"),
        tr("软件许可、账号使用、社区内容与免责说明。"), TermsOfUse));
    cardLayout->addWidget(makeDocumentRow(
        tr("《方块盒子隐私政策》"),
        tr("我们收集哪些信息、如何使用与保护，以及你可以行使的权利。"), PrivacyPolicy));

    // ── 同意勾选 ──
    m_agreeCheck = new QCheckBox(tr("我已阅读并同意《方块盒子使用协议》和《方块盒子隐私政策》"), card);
    m_agreeCheck->setObjectName(QStringLiteral("legalCheck"));
    m_agreeCheck->setCursor(Qt::PointingHandCursor);
    connect(m_agreeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_confirmButton->setEnabled(checked);
    });
    cardLayout->addWidget(m_agreeCheck);

    // ── 底部按钮 ──
    auto *buttonRow = new QWidget(card);
    buttonRow->setAttribute(Qt::WA_StyledBackground, false);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 4, 0, 0);
    buttonLayout->setSpacing(10);

    auto *declineButton = new QPushButton(firstLaunch ? tr("不同意并退出") : tr("暂不登录"), buttonRow);
    declineButton->setObjectName(QStringLiteral("legalDeclineButton"));
    declineButton->setCursor(Qt::PointingHandCursor);
    connect(declineButton, &QPushButton::clicked, this, [this]() {
        m_declined = true;
        reject();
    });
    buttonLayout->addWidget(declineButton);

    m_confirmButton = new QPushButton(tr("同意并继续"), buttonRow);
    m_confirmButton->setObjectName(QStringLiteral("primaryButton"));
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    m_confirmButton->setEnabled(false);
    connect(m_confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_confirmButton, 1);

    cardLayout->addWidget(buttonRow);

    auto *hint = new QLabel(firstLaunch
                                ? tr("不同意将无法使用方块盒子，之后可以重新打开软件再同意。")
                                : tr("不同意将无法登录论坛，随时可以再回来同意。"),
                            card);
    hint->setObjectName(QStringLiteral("legalNote"));
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(hint);

    auto *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    // 卡片按内容高度居中，避免被窗口拉高后各行之间出现大片空白
    main->addWidget(card, 0, 0, Qt::AlignCenter);
}

void UserAgreementDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg      = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder  = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor  = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor   = isLight ? "#333333" : "#e8e8e8";
    const QString subColor    = isLight ? "#64748B" : "#9aa3b2";
    const QString muted       = isLight ? "#94A3B8" : "#7a8290";
    const QString fieldBg     = isLight ? "#f7f7f7" : "#38383d";
    const QString fieldBorder = isLight ? "#e0e0e0" : "#55555a";
    const QString hoverBg     = isLight ? "rgba(15,23,42,0.06)" : "rgba(255,255,255,0.08)";

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        // 卡片内容被 AppDialogBase 包进滚动区后，卡片底色会被视口盖住；
        // 视口与内层容器补一份同样的底色和圆角，外观才与设计一致。
        "QScrollArea#appDialogCardScroll > QWidget {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QWidget#appDialogCardContent {"
        "    background-color: %2;"
        "    border-radius: 18px;"
        "}"
        "QLabel#legalTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#legalHeaderIcon { background: %5; border-radius: 9px; }"
        "QLabel#legalIntro { color: %6; font-size: 12.5px; background: transparent; }"
        "QWidget#legalDocRow { background: %7; border: 1px solid %8; border-radius: 10px; }"
        "QLabel#legalDocName { color: %4; font-size: 13px; font-weight: 700; background: transparent; }"
        "QLabel#legalDocSummary { color: %9; font-size: 12px; background: transparent; }"
        "QPushButton#legalDocLink { color: %5; font-size: 12.5px; background: transparent;"
        "    border: none; padding: 2px 0; }"
        "QPushButton#legalDocLink:hover { color: %4; }"
        "QCheckBox#legalCheck { color: %9; font-size: 12.5px; font-weight: 600; spacing: 7px; background: transparent; }"
        "QCheckBox#legalCheck::indicator { width: 18px; height: 18px; border-radius: 4px;"
        "    border: 1px solid %8; background: %7; }"
        "QCheckBox#legalCheck::indicator:checked { background: %5; border-color: %5; }"
        "QPushButton#legalDeclineButton { background: %7; color: %6; border: 1px solid %8;"
        "    border-radius: 12px; padding: 7px 18px; font-size: 13px; }"
        "QPushButton#legalDeclineButton:hover { background: %10; }"
        "QLabel#legalNote { color: %11; font-size: 11.5px; background: transparent; }"
    )
        // 同一个占位符会在多处复用，必须逐个 arg：一次传多个参数会按出现顺序
        // 而不是按编号替换，复用位置会错位，整张样式表随之失效。
        .arg(QString::fromLatin1(kAgreementCardName))
        .arg(cardBg)
        .arg(cardBorder)
        .arg(titleColor)
        .arg(themeColor)
        .arg(subColor)
        .arg(fieldBg)
        .arg(fieldBorder)
        .arg(textColor)
        .arg(hoverBg)
        .arg(muted);

    setStyleSheet(style);
}

void UserAgreementDialog::showEvent(QShowEvent *event)
{
    AppDialogBase::showEvent(event);
    // 卡片内容在这一步被 AppDialogBase 包进滚动区，重新套一次样式，
    // 让被重新挂载的控件（标题、按钮、勾选框）也拿到本弹窗的样式表。
    initStyle();
    styleScrollContent();
}

void UserAgreementDialog::styleScrollContent()
{
    // AppDialogBase 为了适配小屏，会把卡片布局包进 appDialogCardScroll。
    // 滚动区的视口默认是透明的，卡片自身底色会被它挡住，这里把底色和圆角
    // 补给视口与内层容器，卡片外观才真正显示出来。
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";

    if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("appDialogCardScroll")))
    {
        if (QWidget *viewport = scroll->viewport())
        {
            viewport->setAttribute(Qt::WA_StyledBackground, true);
            // 必须带选择器：直接写声明会连视口里的所有子控件一起涂成卡片底色，
            // 标题、按钮会各自变成一块白底圆角方块
            viewport->setStyleSheet(QStringLiteral(
                "QWidget#qt_scrollarea_viewport {"
                "    background-color: %1; border: 1px solid %2; border-radius: 18px;"
                "}")
                                        .arg(cardBg, cardBorder));
            viewport->update();
        }
    }
    if (QWidget *inner = findChild<QWidget *>(QStringLiteral("appDialogCardContent")))
    {
        inner->setAttribute(Qt::WA_StyledBackground, true);
        // 直接挂到内层容器上，确保包裹后的子控件全部命中本弹窗的样式规则
        inner->setStyleSheet(styleSheet());
        inner->update();
    }
}

QString UserAgreementDialog::termsHtml()
{
    QString body;
    body += tr("<p style='color:%1'>更新日期：2026 年 11 月 20 日</p>")
                .arg(ThemeManager::instance()->currentThemeColor());

    body += tr("<h3>一、关于本协议</h3>"
               "<p>本协议是你与方块盒子（BlockBox，以下简称“我们”）之间关于下载、安装和使用"
               "方块盒子启动器及相关服务（以下简称“本软件”）的约定。首次启动本软件或登录"
               "方块盒子论坛前，请仔细阅读并同意本协议与《方块盒子隐私政策》。</p>"
               "<p>方块盒子是第三方开源 Minecraft 启动器，与 Mojang Studios、Microsoft "
               "没有隶属或合作关系，“Minecraft”及相关商标归其权利人所有。</p>");

    body += tr("<h3>二、软件许可</h3>"
               "<p>本软件本体以 GNU 通用公共许可证第 3 版（GPL-3.0）发布，你可以在遵守该"
               "许可证的前提下使用、复制、修改和再发布；软件内附带的第三方组件、素材和游戏"
               "文件仍遵循各自的权利声明与许可。</p>");

    body += tr("<h3>三、账号与登录</h3>"
               "<p>1. 论坛账号由你自行注册，请妥善保管用户名和密码，因保管不善造成的损失"
               "由你自行承担。</p>"
               "<p>2. 微软账号、第三方认证服务器账号的登录由对应服务方完成，相关登录令牌"
               "仅保存在你的设备上，用于启动游戏和获取角色信息。</p>"
               "<p>3. 不得出借、出租或买卖账号，不得冒用他人身份。</p>");

    body += tr("<h3>四、使用规范</h3>"
               "<p>你在使用本软件时应当遵守法律法规和公序良俗，不得：</p>"
               "<p>1. 利用本软件传播违法违规、色情低俗、暴力恐怖、赌博诈骗等信息；</p>"
               "<p>2. 侵犯他人的著作权、商标权、隐私权等合法权益；</p>"
               "<p>3. 制作、传播外挂、作弊程序，或攻击、干扰游戏服务器和本软件服务；</p>"
               "<p>4. 发布垃圾广告、恶意引流、刷屏引战，或冒充官方、他人身份；</p>"
               "<p>5. 以技术手段绕过本软件或论坛的安全限制、访问控制。</p>");

    body += tr("<h3>五、你发布的内容</h3>"
               "<p>你在论坛发布帖子、回复、插件、投影、教程、图片等内容（以下简称“用户"
               "内容”）的著作权归你所有。你发布即授予我们一项免费的、非独占的许可，允许"
               "我们在本软件和官方网站中存储、展示、分发这些内容，并为适配不同终端做必要"
               "的格式转换。你应保证对用户内容拥有合法权利；内容违规或侵权时，我们有权"
               "删除、下架并视情节处理账号。</p>");

    body += tr("<h3>六、第三方服务</h3>"
               "<p>本软件会连接微软登录、Modrinth、CurseForge、BMCLAPI、联机服务、AI "
               "模型服务等第三方服务。这些服务由相应运营方提供并适用其自己的服务条款和"
               "隐私政策，请你在使用前自行了解。第三方资源（模组、整合包、光影、材质、"
               "地图等）的版权归原作者所有。</p>");

    body += tr("<h3>七、免责声明</h3>"
               "<p>1. 本软件按“现状”免费提供，我们会尽力保证稳定，但不承诺服务不中断、"
               "无错误，也不承诺能适配所有设备和游戏版本。</p>"
               "<p>2. 安装模组、整合包、光影或修改游戏文件可能造成游戏崩溃、存档损坏等"
               "风险，请自行备份重要存档。</p>"
               "<p>3. 在法律允许的范围内，我们不对因使用或无法使用本软件造成的间接损失"
               "承担责任；GPL-3.0 中关于无担保的条款同样适用于本软件本体。</p>");

    body += tr("<h3>八、服务变更与终止</h3>"
               "<p>我们可能根据运营需要调整、暂停或终止部分功能，重大变更会通过软件内"
               "公告提前告知。你违反本协议或社区规范的，我们可视情节采取警告、删除内容、"
               "限制功能、封禁账号等措施。</p>");

    body += tr("<h3>九、协议更新与其他</h3>"
               "<p>本协议可能随功能和服务调整而更新，更新后会在启动或登录时再次提示你"
               "阅读。继续使用即视为接受更新后的协议。对本协议有疑问，可通过软件内"
               "「问题反馈」或官方论坛联系我们。</p>");

    return body;
}

QString UserAgreementDialog::privacyHtml()
{
    QString body;
    body += tr("<p style='color:%1'>更新日期：2026 年 11 月 27 日</p>")
                .arg(ThemeManager::instance()->currentThemeColor());

    body += tr("<h3>一、我们收集哪些信息</h3>"
               "<p>我们只收集为提供服务所必需的信息：</p>"
               "<p>1. 论坛账号信息：注册时填写的用户名、密码（加密保存）、邮箱、头像、"
               "简介，以及你发布的帖子、回复、收藏、签到、反馈和评论。</p>"
               "<p>2. 登录与安全日志：登录时间、IP 地址、客户端版本、操作记录等，用于"
               "账号安全、风险控制和故障排查。</p>"
               "<p>3. 游戏账号信息：使用微软账号或第三方认证登录时产生的角色名、UUID、"
               "皮肤地址和登录令牌。令牌仅保存在你的设备上，用于启动游戏和自动登录。</p>"
               "<p>4. AI 助手内容：你在 AI 助手中输入的对话、选择的文件和实例日志等，"
               "仅在你主动发送时传输给你配置的模型服务商；你填写的 API Key 只保存在"
               "本机。</p>"
               "<p>5. 本地数据：实例、存档、模组、设置、缓存和运行日志保存在你的设备上，"
               "不会自动上传。</p>"
               "<p>6. 诊断与统计（默认关闭，可随时在「设置 → 高级设置 → 诊断与统计」"
               "里关闭）：开启匿名使用统计后，会上报启动次数、会话时长、启动器版本、"
               "系统与平台类型、各页面的访问次数；崩溃报告只在你明确同意后上传，"
               "包含崩溃原因、调用栈和出事前最后一段运行日志，上传前会自动隐藏用户名、"
               "用户目录、邮箱、IP 和登录令牌。这两类信息都不包含账号、实例名、存档、"
               "聊天或任何文件内容。</p>");

    body += tr("<h3>二、我们如何使用这些信息</h3>"
               "<p>用于提供登录、发帖、下载、联机、AI 助手等功能；保障账号与软件安全；"
               "处理反馈、举报和申诉；改进产品体验；履行法律法规要求的义务。我们不会"
               "出售你的个人信息，也不会把论坛密码以明文形式保存。</p>");

    body += tr("<h3>三、设备权限</h3>"
               "<p>1. 存储权限：用于创建实例、安装模组、导入导出存档和备份（安卓 / "
               "鸿蒙）。</p>"
               "<p>2. 麦克风权限：仅在你使用语音输入时申请，用于把你的语音转成文字。</p>"
               "<p>3. 网络权限：用于登录、下载资源、检查更新和联机。</p>"
               "<p>你可以随时在系统设置中关闭这些权限，关闭后对应功能将无法使用。</p>");

    body += tr("<h3>四、信息的共享与第三方</h3>"
               "<p>为实现功能，部分信息会发送给以下第三方：微软账号登录服务、你选择的"
               "第三方认证服务器、Modrinth / CurseForge 等资源平台、联机服务、你配置的"
               "AI 模型服务商，以及为你提供下载加速的镜像源。我们只提供完成对应功能所"
               "必需的信息。除法律法规要求或你明确同意外，我们不会向其他第三方提供你的"
               "个人信息。</p>");

    body += tr("<h3>五、信息的存储与保护</h3>"
               "<p>论坛账号信息存储在方块盒子论坛的服务器上，保存至你注销账号或服务"
               "终止；本地数据保存在你的设备上，卸载软件或清除数据后会被删除。我们采取"
               "加密传输、密码哈希、访问控制等措施保护信息安全，但互联网环境不存在"
               "绝对安全，请妥善保管账号密码。</p>");

    body += tr("<h3>六、你的权利</h3>"
               "<p>你可以随时查看和修改论坛资料、删除自己发布的内容；可以退出登录、"
               "清除本地数据；可以要求注销论坛账号或撤回同意。操作入口在「论坛账号」"
               "设置页，也可以通过软件内「问题反馈」联系我们。注销后，相关账号信息将按"
               "法律法规要求删除或匿名化处理。</p>");

    body += tr("<h3>七、未成年人保护</h3>"
               "<p>未满 14 周岁的未成年人应在监护人陪同下阅读本政策并使用本软件。我们"
               "不会主动收集未成年人的额外个人信息；如发现未经监护人同意收集了未成年人"
               "信息，会尽快删除。</p>");

    body += tr("<h3>八、政策更新与联系我们</h3>"
               "<p>本政策可能随功能调整而更新，更新后会在启动或登录时提示你阅读。对"
               "本政策有任何疑问、意见或投诉，可以通过软件内「问题反馈」或官方论坛联系"
               "我们，我们会尽快处理。</p>");

    return body;
}

// ───────────────────────── 协议全文阅读弹窗 ─────────────────────────

void UserAgreementDocumentDialog::show(UserAgreementDialog::Document document, QWidget *parent)
{
    UserAgreementDocumentDialog dialog(document, parent);
    dialog.exec();
}

UserAgreementDocumentDialog::UserAgreementDocumentDialog(UserAgreementDialog::Document document,
                                                         QWidget *parent)
    : AppDialogBase(parent)
    , m_document(document)
{
    const bool terms = (document == UserAgreementDialog::TermsOfUse);
    setObjectName(terms ? QStringLiteral("userAgreementTermsDialog")
                        : QStringLiteral("userAgreementPrivacyDialog"));
    setWindowTitle(terms ? tr("方块盒子使用协议") : tr("方块盒子隐私政策"));

    initUI();
    initStyle();
}

void UserAgreementDocumentDialog::initUI()
{
    const bool terms = (m_document == UserAgreementDialog::TermsOfUse);

    auto *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kDocumentCardName));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setMinimumSize(620, 520);
    card->setMaximumSize(700, 660);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 18);
    cardLayout->setSpacing(10);

    auto *title = new QLabel(terms ? tr("方块盒子使用协议") : tr("方块盒子隐私政策"), card);
    title->setObjectName(QStringLiteral("legalDocumentTitle"));
    cardLayout->addWidget(title);

    // 正文放在滚动区里，长条款也不挤
    auto *scroll = new QScrollArea(card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_bodyLabel = new QLabel(scroll);
    m_bodyLabel->setObjectName(QStringLiteral("legalDocumentBody"));
    m_bodyLabel->setWordWrap(true);
    m_bodyLabel->setTextFormat(Qt::RichText);
    m_bodyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bodyLabel->setAlignment(Qt::AlignTop);
    scroll->setWidget(m_bodyLabel);
    cardLayout->addWidget(scroll, 1);

    auto *closeButton = new QPushButton(tr("我知道了"), card);
    closeButton->setObjectName(QStringLiteral("primaryButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    cardLayout->addWidget(closeButton);

    m_bodyLabel->setText(UserAgreementDialog::documentText(m_document));

    auto *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    // 卡片按内容高度居中，避免被窗口拉高后各行之间出现大片空白
    main->addWidget(card, 0, 0, Qt::AlignCenter);
}

void UserAgreementDocumentDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QScrollArea#appDialogCardScroll > QWidget {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QWidget#appDialogCardContent {"
        "    background-color: %2;"
        "    border-radius: 18px;"
        "}"
        "QLabel#legalDocumentTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QScrollArea { background: transparent; }"
        "QLabel#legalDocumentBody { color: %5; font-size: 13px; background: transparent; }"
        "QLabel#legalDocumentBody h3 { color: %4; font-size: 13.5px; margin: 10px 0 4px 0; }"
        "QLabel#legalDocumentBody p { margin: 4px 0; line-height: 150%; }"
    ).arg(QString::fromLatin1(kDocumentCardName), cardBg, cardBorder, titleColor, textColor);

    setStyleSheet(style);
}

void UserAgreementDocumentDialog::showEvent(QShowEvent *event)
{
    AppDialogBase::showEvent(event);
    initStyle();
    styleScrollContent();
}

void UserAgreementDocumentDialog::styleScrollContent()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";

    if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("appDialogCardScroll")))
    {
        if (QWidget *viewport = scroll->viewport())
        {
            viewport->setAttribute(Qt::WA_StyledBackground, true);
            // 必须带选择器：直接写声明会连视口里的所有子控件一起涂成卡片底色，
            // 标题、按钮会各自变成一块白底圆角方块
            viewport->setStyleSheet(QStringLiteral(
                "QWidget#qt_scrollarea_viewport {"
                "    background-color: %1; border: 1px solid %2; border-radius: 18px;"
                "}")
                                        .arg(cardBg, cardBorder));
            viewport->update();
        }
    }
    if (QWidget *inner = findChild<QWidget *>(QStringLiteral("appDialogCardContent")))
    {
        inner->setAttribute(Qt::WA_StyledBackground, true);
        inner->setStyleSheet(styleSheet());
        inner->update();
    }
}
