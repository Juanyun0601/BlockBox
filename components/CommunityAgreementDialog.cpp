/**
 * @file   CommunityAgreementDialog.cpp
 * @brief  首次发布前的「社区规范」确认弹窗与规范全文查看弹窗
 * @author BlockBox Team
 * @date   2026-11-15
 *
 * 规则条目与举报分类（ForumReportPage）对齐：违法违规、垃圾广告导流、
 * 人身攻击辱骂、色情低俗、侵权盗搬、泄露隐私、引战刷屏；处罚从删除内容
 * 到封禁账号，与论坛账号的「已封禁」状态一致。
 */
#include "CommunityAgreementDialog.h"

#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

const char kAgreementCardName[] = "communityAgreementCard";
const char kGuidelinesCardName[] = "communityGuidelinesCard";

/** 确认卡片上的核心规则摘要（与规范全文逐条对应） */
QStringList coreRules()
{
    return {
        QStringLiteral("遵守法律法规，不发布违法违规内容"),
        QStringLiteral("不发垃圾广告，不恶意导流"),
        QStringLiteral("友善讨论：不辱骂、不引战、不刷屏"),
        QStringLiteral("内容健康：不发色情低俗等不良内容"),
        QStringLiteral("尊重原创：不盗搬他人作品，转载需授权"),
        QStringLiteral("保护隐私：不泄露自己与他人的隐私信息"),
    };
}

/** 违规处罚一句话摘要 */
QString penaltySummary()
{
    return QStringLiteral("违规内容会被删除或下架；情节严重的将被警告、禁言、"
                          "取消发布权限，直至封禁账号。");
}

} // namespace

// ───────────────────────── 首次发布确认弹窗 ─────────────────────────

bool CommunityAgreementDialog::ensureAccepted(QWidget *parent)
{
    if (SettingsManager::instance()->isCommunityAgreementAccepted())
        return true;

    CommunityAgreementDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    SettingsManager::instance()->setCommunityAgreementAccepted(true);
    return true;
}

CommunityAgreementDialog::CommunityAgreementDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("communityAgreementDialog"));
    setWindowTitle(tr("方块盒子社区规范"));

    initUI();
    initStyle();
}

void CommunityAgreementDialog::initUI()
{
    auto *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kAgreementCardName));
    card->setMinimumSize(460, 0);
    card->setMaximumWidth(500);

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
    icon->setObjectName(QStringLiteral("agreementHeaderIcon"));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_StyledBackground, true);
    icon->setPixmap(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/announce.svg"),
                                                ThemeManager::instance()->currentThemeColor(),
                                                16).pixmap(16, 16));
    headerRow->addWidget(icon);

    auto *title = new QLabel(tr("方块盒子社区规范"), header);
    title->setObjectName(QStringLiteral("agreementTitle"));
    headerRow->addWidget(title, 1);
    cardLayout->addWidget(header);

    auto *intro = new QLabel(tr("首次发布内容前，请先读一遍社区规范："), card);
    intro->setObjectName(QStringLiteral("agreementIntro"));
    cardLayout->addWidget(intro);

    // ── 核心规则摘要 ──
    auto *rulesCard = new QWidget(card);
    rulesCard->setObjectName(QStringLiteral("agreementRules"));
    auto *rulesLayout = new QVBoxLayout(rulesCard);
    rulesLayout->setContentsMargins(14, 10, 14, 10);
    rulesLayout->setSpacing(5);
    const QStringList rules = coreRules();
    for (int i = 0; i < rules.size(); ++i)
    {
        auto *row = new QLabel(QStringLiteral("%1. %2").arg(i + 1).arg(rules.at(i)), rulesCard);
        row->setObjectName(QStringLiteral("agreementRule"));
        row->setWordWrap(true);
        rulesLayout->addWidget(row);
    }
    cardLayout->addWidget(rulesCard);

    // ── 处罚摘要 ──
    auto *penalty = new QLabel(penaltySummary(), card);
    penalty->setObjectName(QStringLiteral("agreementPenalty"));
    penalty->setWordWrap(true);
    cardLayout->addWidget(penalty);

    // ── 查看全文入口 ──
    auto *fullTextButton = new QPushButton(tr("查看完整社区规范"), card);
    fullTextButton->setObjectName(QStringLiteral("agreementLink"));
    fullTextButton->setCursor(Qt::PointingHandCursor);
    fullTextButton->setFlat(true);
    connect(fullTextButton, &QPushButton::clicked, this, []() {
        CommunityGuidelinesDialog::show();
    });
    cardLayout->addWidget(fullTextButton, 0, Qt::AlignLeft);

    // ── 同意勾选 ──
    m_agreeCheck = new QCheckBox(tr("我已阅读并同意《方块盒子社区规范》"), card);
    m_agreeCheck->setObjectName(QStringLiteral("agreementCheck"));
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

    auto *declineButton = new QPushButton(tr("暂不同意"), buttonRow);
    declineButton->setObjectName(QStringLiteral("agreementDeclineButton"));
    declineButton->setCursor(Qt::PointingHandCursor);
    connect(declineButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(declineButton);

    m_confirmButton = new QPushButton(tr("同意并继续"), buttonRow);
    m_confirmButton->setObjectName(QStringLiteral("primaryButton"));
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    m_confirmButton->setEnabled(false);
    connect(m_confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_confirmButton, 1);

    cardLayout->addWidget(buttonRow);

    auto *hint = new QLabel(tr("不同意就无法发布内容，随时可以再回来同意"), card);
    hint->setObjectName(QStringLiteral("agreementNote"));
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(hint);

    auto *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);
}

void CommunityAgreementDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";
    const QString subColor   = isLight ? "#64748B" : "#9aa3b2";
    const QString muted      = isLight ? "#94A3B8" : "#7a8290";
    const QString fieldBg    = isLight ? "#f4f4f4" : "#3b3b40";
    const QString fieldBorder= isLight ? "#d4d4d4" : "#55555a";

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QLabel#agreementTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#agreementHeaderIcon { background: %5; border-radius: 9px; }"
        "QLabel#agreementIntro { color: %6; font-size: 12.5px; background: transparent; }"
        "QWidget#agreementRules { background: %7; border: 1px solid %8; border-radius: 10px; }"
        "QLabel#agreementRule { color: %9; font-size: 12.5px; background: transparent; }"
        "QLabel#agreementPenalty { color: %10; font-size: 12px; background: transparent; }"
        "QPushButton#agreementLink { color: %5; font-size: 12.5px; background: transparent;"
        "    border: none; padding: 2px 0; text-align: left; }"
        "QPushButton#agreementLink:hover { color: %4; }"
        "QCheckBox#agreementCheck { color: %9; font-size: 12.5px; font-weight: 600; spacing: 7px; background: transparent; }"
        "QCheckBox#agreementCheck::indicator { width: 18px; height: 18px; border-radius: 4px;"
        "    border: 1px solid %8; background: %7; }"
        "QCheckBox#agreementCheck::indicator:checked { background: %5; border-color: %5; }"
        "QPushButton#agreementDeclineButton { background: %7; color: %6; border: 1px solid %8;"
        "    border-radius: 12px; padding: 7px 20px; font-size: 13px; }"
        "QPushButton#agreementDeclineButton:hover { background: %11; }"
        "QLabel#agreementNote { color: %12; font-size: 11.5px; background: transparent; }"
    ).arg(QString::fromLatin1(kAgreementCardName), cardBg, cardBorder, titleColor, themeColor,
          subColor, fieldBg, fieldBorder, textColor, muted,
          isLight ? "rgba(15,23,42,0.08)" : "rgba(255,255,255,0.08)", muted);

    setStyleSheet(style);
}

// ───────────────────────── 规范全文查看弹窗 ─────────────────────────

void CommunityGuidelinesDialog::show(QWidget *parent)
{
    CommunityGuidelinesDialog dialog(parent);
    dialog.exec();
}

CommunityGuidelinesDialog::CommunityGuidelinesDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("communityGuidelinesDialog"));
    setWindowTitle(tr("方块盒子社区规范（全文）"));

    initUI();
    initStyle();
}

void CommunityGuidelinesDialog::initUI()
{
    auto *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kGuidelinesCardName));
    card->setMinimumSize(620, 520);
    card->setMaximumSize(680, 640);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 18);
    cardLayout->setSpacing(10);

    auto *title = new QLabel(tr("方块盒子社区规范"), card);
    title->setObjectName(QStringLiteral("guidelinesTitle"));
    cardLayout->addWidget(title);

    // 正文放在滚动区里，规则与处罚看全不挤
    auto *scroll = new QScrollArea(card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_bodyLabel = new QLabel(scroll);
    m_bodyLabel->setObjectName(QStringLiteral("guidelinesBody"));
    m_bodyLabel->setWordWrap(true);
    m_bodyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bodyLabel->setAlignment(Qt::AlignTop);
    scroll->setWidget(m_bodyLabel);
    cardLayout->addWidget(scroll, 1);

    auto *closeButton = new QPushButton(tr("我知道了"), card);
    closeButton->setObjectName(QStringLiteral("primaryButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    cardLayout->addWidget(closeButton);

    const QStringList rules = coreRules();
    QString body;
    body += tr("<h3>一、适用范围</h3>"
               "<p>本规范适用于你在方块盒子社区发布的一切内容：论坛的帖子与评论、"
               "插件与投影等资源帖，以及联机大厅里的房间信息与介绍。首次发布前需要"
               "同意本规范，同意一次长期有效。</p>");
    body += tr("<h3>二、发布规则</h3>");
    for (int i = 0; i < rules.size(); ++i)
        body += tr("<p>%1. %2。</p>").arg(i + 1).arg(rules.at(i));
    body += tr("<h3>三、违规处罚</h3>"
               "<p>按情节轻重，管理会采取以下一项或多项处罚：</p>"
               "<p>1. 删除或下架违规内容；</p>"
               "<p>2. 警告并要求整改；</p>"
               "<p>3. 禁言；</p>"
               "<p>4. 暂停或取消发布权限；</p>"
               "<p>5. 封禁账号，多次违规或情节严重的永久封禁。</p>");
    body += tr("<h3>四、举报与申诉</h3>"
               "<p>发现违规内容，可以在对应帖子或资料页点「举报」提交，会有管理跟进"
               "处理；对处罚结果有异议，可以通过软件里的「问题反馈」申诉。</p>");
    body += tr("<p style='color:%1'>本规范会随社区发展更新，重要变更会在公告里提示，"
               "继续使用发布功能即视为接受最新版本。</p>")
                .arg(ThemeManager::instance()->currentThemeColor());
    m_bodyLabel->setText(body);

    auto *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);
}

void CommunityGuidelinesDialog::initStyle()
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
        "QLabel#guidelinesTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QScrollArea { background: transparent; }"
        "QLabel#guidelinesBody { color: %5; font-size: 13px; background: transparent; }"
        "QLabel#guidelinesBody h3 { color: %4; font-size: 13.5px; margin: 10px 0 4px 0; }"
        "QLabel#guidelinesBody p { margin: 4px 0; line-height: 150%; }"
    ).arg(QString::fromLatin1(kGuidelinesCardName), cardBg, cardBorder, titleColor, textColor);

    setStyleSheet(style);
}
