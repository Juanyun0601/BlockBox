/**
 * @file   OnboardingWizard.cpp
 * @brief  新手引导向导实现（首次运行 6 步配置）
 * @author BlockBox Team
 * @date   2026-08-13
 *
 * 步骤：
 *   0. 语言设置（多语言页面，选定语言前展示多语提示）
 *   1. 下载源选择【仅选择繁體中文时出现，由用户决定官方源 / BMCL 镜像源】
 *   2. 引导方式（手动填写 / 从现有启动器导入 / 跳过）
 *   3. 分支 A：导入启动器（PCL / PCL CE、HMCL、MultiMC、BaKa XL）
 *      分支 B：账户管理（微软 / 离线 / 正版）
 *   4. Java 运行时管理（轻量扫描 + 官方下载入口）【仅手动填写】
 *   5. 实例文件夹管理（添加 / 默认）【仅手动填写】
 *   6. 样式设置（主题 / 背景 / 强调色）
 * 完成页：汇总本次引导完成的配置
 * 说明：从现有启动器导入时自动带回 Java 与实例文件夹，故跳过第 4、5 步。
 *   导入流程：语言 → [下载源] → 引导方式 → 导入启动器 → 样式设置 → 完成。
 * 下载源联动：选择非中文（English / Español）时自动切到官方源；选择繁體中文时
 *   插入「下载源选择」步骤，由用户自行决定。
 *
 * 全部样式位于 styles/style.qss Section 50（obw 前缀 objectName），
 * 跟随全局主题（浅色 / 深色 / 强调色）实时刷新。
 */

#include "OnboardingWizard.h"

#include "components/OutlinedLabel.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QDir>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QFrame>
#include <QPalette>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "utils/BackgroundManager.h"
#include "utils/LauncherImporter.h"
#include "utils/LanguageManager.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

namespace {
const char *kAccentNames[6] = {
    "\u7FE1\u7EFF",     // 翡翠绿
    "\u975B\u84DD",     // 靛蓝
    "\u7D2B\u7F57\u5170", // 紫罗兰
    "\u7C89\u7EA2",     // 粉红
    "\u7425\u73C0",     // 琥珀
    "\u5929\u9752",     // 天青
};
const char *kAccentHex[6] = {
    "#10B981", "#6366F1", "#8B5CF6", "#EC4899", "#F59E0B", "#06B6D4",
};
} // namespace

OnboardingWizard::OnboardingWizard(QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("OnboardingWizard"));
    setWindowTitle(tr("\u65B0\u624B\u5F15\u5BFC"));   // 新手引导
    setMinimumSize(1000, 720);
    // 当前语言为繁體中文时，引导中插入「下载源选择」步骤（切换语言重启后同样命中）。
    m_hasDlSourceStep = (LanguageManager::instance()->currentLanguage()
                         == LanguageManager::ChineseTraditional);
    initUI();
}

/* ============================================================
 * UI 骨架：卡片（header + stepper + stack + footer）
 * ============================================================ */
void OnboardingWizard::initUI()
{
    // 居中卡片（objectName 以 "Card" 结尾，AppDialogBase 自动定位关闭按钮）
    QFrame *card = new QFrame(this);
    card->setObjectName(QStringLiteral("OnboardingWizardCard"));
    card->setMinimumSize(960, 680);
    card->setMaximumWidth(1040);
    card->setMaximumHeight(740);   // 对齐 HTML .onboard: height min(740px, 94vh)

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    QWidget *header = buildHeader(card);   // 品牌 + 跳过
    cardLayout->addWidget(header, 0);
    buildStepper(card);                    // 5 步步骤条
    cardLayout->addWidget(m_stepperWidget, 0);

    m_stack = new QStackedWidget(card);
    m_stack->setObjectName(QStringLiteral("obwStack"));
    m_stack->setMinimumHeight(430);

    buildStep0();
    buildStepDlSource();
    buildStep1();
    buildStep2Import();
    buildStep2Manual();
    buildStep3();
    buildStep4();
    buildStep5();
    buildDonePage();

    // stack 页面顺序固定：0 语言 1 下载源 2 引导 3 导入 4 账户 5 Java 6 文件夹 7 样式 8 完成
    m_pages.clear();
    for (int i = 0; i < m_stack->count(); ++i)
        m_pages.append(m_stack->widget(i));

    cardLayout->addWidget(m_stack, 1);

    QWidget *footer = buildFooter(card);
    cardLayout->addWidget(footer, 0);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(card, 0, Qt::AlignCenter);

    // 初始化状态
    m_currentStep = 0;
    showStep(0);
}

/* ============================================================
 * 头部：品牌 + 跳过引导
 * ============================================================ */
QWidget *OnboardingWizard::buildHeader(QWidget *parent)
{
    QFrame *header = new QFrame(parent);
    header->setObjectName(QStringLiteral("obwHeader"));
    header->setFixedHeight(64);

    QHBoxLayout *lay = new QHBoxLayout(header);
    lay->setContentsMargins(24, 12, 20, 12);
    lay->setSpacing(12);

    m_brandLogo = new QLabel(QStringLiteral("\u65B9"), header);  // 方
    m_brandLogo->setObjectName(QStringLiteral("obwBrandLogo"));
    m_brandLogo->setFixedSize(40, 40);
    m_brandLogo->setAlignment(Qt::AlignCenter);

    QVBoxLayout *brandText = new QVBoxLayout();
    brandText->setSpacing(0);
    m_brandTitle = new QLabel(tr("\u65B9\u5757\u76D2\u5B50"), header);  // 方块盒子
    m_brandTitle->setObjectName(QStringLiteral("obwBrandTitle"));
    m_brandSub = new QLabel(tr("\u9996\u6B21\u4F7F\u7528\u5F15\u5BFC"), header);  // 首次使用引导
    m_brandSub->setObjectName(QStringLiteral("obwBrandSub"));
    brandText->addWidget(m_brandTitle);
    brandText->addWidget(m_brandSub);

    lay->addWidget(m_brandLogo);
    lay->addLayout(brandText);
    lay->addStretch();

    m_skipBtn = new QPushButton(tr("\u8DF3\u8FC7\u5F15\u5BFC"), header);  // 跳过引导
    m_skipBtn->setObjectName(QStringLiteral("obwSkipBtn"));
    m_skipBtn->setCursor(Qt::PointingHandCursor);
    connect(m_skipBtn, &QPushButton::clicked, this, &OnboardingWizard::onSkipClicked);
    lay->addWidget(m_skipBtn);

    return header;
}

/* ============================================================
 * 步骤条（5 节点 + 连接线，状态由 syncStepper 驱动）
 * ============================================================ */
void OnboardingWizard::buildStepper(QWidget *parent)
{
    m_stepperWidget = new QWidget(parent);
    m_stepperWidget->setObjectName(QStringLiteral("obwStepper"));
    m_stepperWidget->setFixedHeight(58);

    QHBoxLayout *lay = new QHBoxLayout(m_stepperWidget);
    lay->setContentsMargins(32, 8, 32, 8);
    lay->setSpacing(10);

    const QStringList labels = {
        tr("\u8BED\u8A00\u8BBE\u7F6E"),      // 语言设置
        tr("\u4E0B\u8F7D\u6E90"),            // 下载源（仅繁體中文显示）
        tr("\u5F15\u5BFC\u65B9\u5F0F"),      // 引导方式
        tr("\u8D26\u6237\u00B7\u5BFC\u5165"), // 账户·导入
        tr("Java \u7BA1\u7406"),             // Java 管理
        tr("\u5B9E\u4F8B\u6587\u4EF6\u5939"), // 实例文件夹
        tr("\u6837\u5F0F\u8BBE\u7F6E"),      // 样式设置
    };

    m_stepNodes.clear();
    m_stepDots.clear();
    m_stepLabels.clear();
    m_stepLines.clear();

    // 7 个节点 + 6 条连接线在 960px 卡片内刚好铺满，故连接线收窄到 40px
    for (int i = 0; i < 7; ++i) {
        if (i > 0) {
            QFrame *line = new QFrame(m_stepperWidget);
            line->setObjectName(QStringLiteral("obwStepLine"));
            line->setFixedSize(40, 2);
            lay->addWidget(line, 0, Qt::AlignVCenter);
            m_stepLines.append(line);
        }

        QWidget *node = new QWidget(m_stepperWidget);
        node->setObjectName(QStringLiteral("obwStepNode"));
        QVBoxLayout *nLay = new QVBoxLayout(node);
        nLay->setContentsMargins(2, 0, 2, 0);
        nLay->setSpacing(3);
        nLay->setAlignment(Qt::AlignCenter);

        QLabel *dot = new QLabel(QString::number(i + 1), node);
        dot->setObjectName(QStringLiteral("obwStepDot"));
        dot->setFixedSize(34, 34);
        dot->setAlignment(Qt::AlignCenter);
        nLay->addWidget(dot, 0, Qt::AlignHCenter);

        QLabel *label = new QLabel(labels.at(i), node);
        label->setObjectName(QStringLiteral("obwStepLabel"));
        nLay->addWidget(label, 0, Qt::AlignHCenter);

        lay->addWidget(node, 0, Qt::AlignVCenter);
        m_stepNodes.append(node);
        m_stepDots.append(dot);
        m_stepLabels.append(label);
    }
    lay->addStretch();
}

/* ============================================================
 * 底部导航
 * ============================================================ */
QWidget *OnboardingWizard::buildFooter(QWidget *parent)
{
    QFrame *footer = new QFrame(parent);
    footer->setObjectName(QStringLiteral("obwFooter"));
    footer->setFixedHeight(64);

    QHBoxLayout *lay = new QHBoxLayout(footer);
    lay->setContentsMargins(28, 10, 28, 10);
    lay->setSpacing(10);

    m_footerHint = new QLabel(tr("\u9009\u62E9\u4E00\u4E2A\u5F15\u5BFC\u65B9\u5F0F\u5F00\u59CB"), footer);
    m_footerHint->setObjectName(QStringLiteral("obwFooterHint"));
    lay->addWidget(m_footerHint);
    lay->addStretch();

    m_prevBtn = new QPushButton(tr("\u4E0A\u4E00\u6B65"), footer);
    m_prevBtn->setObjectName(QStringLiteral("obwBtnSecondary"));
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    m_prevBtn->setVisible(false);
    m_nextBtn = new QPushButton(tr("\u4E0B\u4E00\u6B65"), footer);
    m_nextBtn->setObjectName(QStringLiteral("obwBtnPrimary"));
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_prevBtn, &QPushButton::clicked, this, &OnboardingWizard::onPrevClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &OnboardingWizard::onNextClicked);
    lay->addWidget(m_prevBtn);
    lay->addWidget(m_nextBtn);

    return footer;
}

/* ============================================================
 * Step0 语言设置（多语言页面）
 * ============================================================ */
void OnboardingWizard::buildStep0()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPage0"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 20, 32, 16);
    lay->setSpacing(10);

    // 标题：选定语言前以多语言展示
    OutlinedLabel *title = new OutlinedLabel(
        tr("\u9009\u62E9\u8BED\u8A00") + QStringLiteral("  \u00B7  Select Language  \u00B7  \u3053\u3068\u3070\u3092\u9078\u629E"), page);
    title->setObjectName(QStringLiteral("obwPageTitle"));
    title->setWordWrap(true);
    lay->addWidget(title);

    QLabel *desc = new QLabel(
        tr("\u9009\u62E9\u4F60\u559C\u6B22\u7684\u8BED\u8A00\uFF0C\u968F\u65F6\u53EF\u5728\u8BBE\u7F6E\u4E2D\u4FEE\u6539\u3002")
        + QStringLiteral("  Choose your preferred language.  \u4F60\u53EF\u4EE5\u968F\u65F6\u66F4\u6539\u3002"), page);
    desc->setObjectName(QStringLiteral("obwPageDesc"));
    desc->setWordWrap(true);
    lay->addWidget(desc);
    lay->addSpacing(8);

    // 语言卡片：简体中文 / 繁體中文 / English / Español（母语名，未选定语言前用原生文字标识）
    QHBoxLayout *cards = new QHBoxLayout();
    cards->setSpacing(14);

    struct LangDef { LanguageManager::Language lang; QString nativeName; QString meta; QString icon; };
    const QVector<LangDef> defs = {
        { LanguageManager::Chinese,
          QStringLiteral("\u7B80\u4F53\u4E2D\u6587"),           // 简体中文
          QStringLiteral("Simplified Chinese"), QStringLiteral("\u4E2D") },
        { LanguageManager::ChineseTraditional,
          QStringLiteral("\u7E41\u9AD4\u4E2D\u6587"),           // 繁體中文
          QStringLiteral("Traditional Chinese"), QStringLiteral("\u6F22") },
        { LanguageManager::English,
          QStringLiteral("English"), QStringLiteral("English"), QStringLiteral("EN") },
        { LanguageManager::Spanish,
          QStringLiteral("Espa\u00F1ol"), QStringLiteral("Spanish"), QStringLiteral("ES") },
    };

    QButtonGroup *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < defs.size(); ++i) {
        const LangDef &def = defs.at(i);
        QPushButton *card = new QPushButton(page);
        card->setObjectName(QStringLiteral("obwMethodCard")); // 复用引导方式卡片样式
        card->setCheckable(true);
        card->setCursor(Qt::PointingHandCursor);
        card->setMinimumHeight(150);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QVBoxLayout *clay = new QVBoxLayout(card);
        clay->setContentsMargins(18, 20, 18, 18);
        clay->setSpacing(6);

        QLabel *icon = new QLabel(def.icon, card);
        icon->setObjectName(QStringLiteral("obwMethodIcon"));
        icon->setFixedSize(46, 46);
        icon->setAlignment(Qt::AlignCenter);
        {
            ThemeManager *tm = ThemeManager::instance();
            QString primary = tm->currentThemeColor();
            QColor soft = QColor(primary);
            soft.setAlphaF(tm->currentTheme() == ThemeManager::LightTheme ? 0.12 : 0.20);
            icon->setStyleSheet(QStringLiteral(
                "background-color: %1; border-radius: 11px; color: %2;"
                "font-size: 16px; font-weight: 700;")
                .arg(soft.name(QColor::HexArgb), primary));
        }
        clay->addWidget(icon, 0, Qt::AlignHCenter);
        clay->addSpacing(4);

        QLabel *name = new QLabel(def.nativeName, card);
        name->setObjectName(QStringLiteral("obwMethodName"));
        name->setAlignment(Qt::AlignHCenter);
        clay->addWidget(name);

        QLabel *meta = new QLabel(def.meta, card);
        meta->setObjectName(QStringLiteral("obwMethodDesc"));
        meta->setAlignment(Qt::AlignHCenter);
        meta->setWordWrap(true);
        clay->addWidget(meta);
        clay->addStretch();

        m_languageCards.append(card);
        group->addButton(card, i);
        cards->addWidget(card, 1);
    }

    connect(group, &QButtonGroup::idClicked, this, &OnboardingWizard::onPickLanguage);
    lay->addLayout(cards, 1);

    QLabel *tip = new QLabel(
        tr("\u672A\u9009\u62E9\u65F6\u9ED8\u8BA4\u4F7F\u7528\u4E2D\u6587\uFF0C\u540E\u7EED\u53EF\u5728\u300C\u8BBE\u7F6E > \u8BED\u8A00\u300D\u4E2D\u4FEE\u6539\u3002"), page);
    tip->setObjectName(QStringLiteral("obwTip"));
    lay->addWidget(tip);

    // 默认勾选当前语言
    LanguageManager *lm = LanguageManager::instance();
    const int curIdx = static_cast<int>(lm->currentLanguage());
    if (curIdx >= 0 && curIdx < m_languageCards.size())
        m_languageCards.at(curIdx)->setChecked(true);

    m_stack->addWidget(page);
}

/* ============================================================
 * Step1 下载源选择（仅選擇繁體中文时出现）
 * ============================================================ */
void OnboardingWizard::buildStepDlSource()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPageDlSource"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 20, 32, 16);
    lay->setSpacing(10);

    OutlinedLabel *title = new OutlinedLabel(tr("\u4E0B\u8F7D\u6E90\u9009\u62E9"), page);
    title->setObjectName(QStringLiteral("obwPageTitle"));
    lay->addWidget(title);

    QLabel *desc = new QLabel(
        tr("\u9009\u62E9\u6E38\u620F\u4E0E\u5404\u7C7B\u52A0\u8F7D\u5668\u7684\u4E0B\u8F7D\u6765\u6E90\u3002"
           "\u5B98\u65B9\u6E90\u5168\u7403\u7A33\u5B9A\uFF1BBMCL \u955C\u50CF\u6E90\u9488\u5BF9\u4E2D\u56FD\u5927\u9646\u7F51\u7EDC\u4F18\u5316\u3002"
           "\u968F\u65F6\u53EF\u5728\u300C\u8BBE\u7F6E > \u4E0B\u8F7D\u300D\u4E2D\u4FEE\u6539\u3002"), page);
    desc->setObjectName(QStringLiteral("obwPageDesc"));
    desc->setWordWrap(true);
    lay->addWidget(desc);
    lay->addSpacing(8);

    QHBoxLayout *cards = new QHBoxLayout();
    cards->setSpacing(14);

    QButtonGroup *group = new QButtonGroup(this);
    group->setExclusive(true);

    struct DlSourceDef { bool official; QString icon; QString name; QString meta; };
    const QVector<DlSourceDef> defs = {
        { true,  QStringLiteral("O"),
          tr("\u5B98\u65B9\u6E90"),
          tr("Mojang \u4E0E\u5404\u52A0\u8F7D\u5668\u5B98\u7F51\u76F4\u8FDE\uFF0C\u5168\u7403\u53EF\u7528\u3002") },
        { false, QStringLiteral("B"),
          tr("BMCL \u955C\u50CF\u6E90"),
          tr("\u9488\u5BF9\u4E2D\u56FD\u5927\u9646\u4F18\u5316\u7684\u955C\u50CF\uFF0C\u4E0B\u8F7D\u66F4\u5FEB\u3002") },
    };

    for (int i = 0; i < defs.size(); ++i) {
        const DlSourceDef &def = defs.at(i);
        QPushButton *card = new QPushButton(page);
        card->setObjectName(QStringLiteral("obwMethodCard"));
        card->setCheckable(true);
        card->setCursor(Qt::PointingHandCursor);
        card->setMinimumHeight(190);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QVBoxLayout *clay = new QVBoxLayout(card);
        clay->setContentsMargins(18, 22, 18, 18);
        clay->setSpacing(8);

        QLabel *icon = new QLabel(def.icon, card);
        icon->setObjectName(QStringLiteral("obwMethodIcon"));
        icon->setFixedSize(50, 50);
        icon->setAlignment(Qt::AlignCenter);
        {
            ThemeManager *tm = ThemeManager::instance();
            QString primary = tm->currentThemeColor();
            QColor soft = QColor(primary);
            soft.setAlphaF(tm->currentTheme() == ThemeManager::LightTheme ? 0.12 : 0.20);
            icon->setStyleSheet(QStringLiteral(
                "background-color: %1; border-radius: 12px; color: %2;"
                "font-size: 18px; font-weight: 700;")
                .arg(soft.name(QColor::HexArgb), primary));
        }
        clay->addWidget(icon, 0, Qt::AlignHCenter);
        clay->addSpacing(4);

        QLabel *name = new QLabel(def.name, card);
        name->setObjectName(QStringLiteral("obwMethodName"));
        name->setAlignment(Qt::AlignHCenter);
        clay->addWidget(name);

        QLabel *meta = new QLabel(def.meta, card);
        meta->setObjectName(QStringLiteral("obwMethodDesc"));
        meta->setAlignment(Qt::AlignHCenter);
        meta->setWordWrap(true);
        clay->addWidget(meta);
        clay->addStretch();

        group->addButton(card, i);
        cards->addWidget(card, 1);
    }
    lay->addLayout(cards, 1);

    QLabel *tip = new QLabel(
        tr("\u4EC5\u7E41\u9AD4\u4E2D\u6587\u7528\u6237\u9700\u8981\u5728\u6B64\u9009\u62E9\uFF0C"
           "\u5176\u4ED6\u8BED\u8A00\u4E0B\u5C06\u81EA\u52A8\u4F7F\u7528\u9002\u5408\u7684\u4E0B\u8F7D\u6E90\u3002"), page);
    tip->setObjectName(QStringLiteral("obwTip"));
    lay->addWidget(tip);

    // 默认勾选当前原版下载源
    const bool currentOfficial =
        SettingsManager::instance()->getDownloadSource() != DownloadSource::BMCL;
    if (QAbstractButton *btn = group->button(currentOfficial ? 0 : 1))
        btn->setChecked(true);

    connect(group, &QButtonGroup::idClicked,
            this, &OnboardingWizard::onPickDownloadSource);

    m_stack->addWidget(page);
}

/* ============================================================
 * Step2 引导方式
 * ============================================================ */
void OnboardingWizard::buildStep1()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPage1"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 20, 32, 16);
    lay->setSpacing(10);

    OutlinedLabel *title = new OutlinedLabel(tr("\u6B22\u8FCE\u4F7F\u7528\u65B9\u5757\u76D2\u5B50\uFF01"), page);
    title->setObjectName(QStringLiteral("obwPageTitle"));
    lay->addWidget(title);

    QLabel *desc = new QLabel(
        tr("\u9009\u62E9\u4E00\u79CD\u65B9\u5F0F\u5B8C\u6210\u57FA\u7840\u8BBE\u7F6E\uFF0C\u5F00\u59CB\u4F60\u7684 Minecraft \u4E4B\u65C5\u3002"), page);
    desc->setObjectName(QStringLiteral("obwPageDesc"));
    desc->setWordWrap(true);
    lay->addWidget(desc);
    lay->addSpacing(8);

    QHBoxLayout *cards = new QHBoxLayout();
    cards->setSpacing(14);

    QButtonGroup *group = new QButtonGroup(this);
    group->setExclusive(true);

    struct MethodDef { Method method; QString icon; QString name; QString desc; QString tag; };
    const QVector<MethodDef> defs = {
        { MethodManual,
          QStringLiteral("M"),
          tr("\u624B\u52A8\u586B\u5199"),
          tr("\u81EA\u5DF1\u914D\u7F6E\u8D26\u6237\u3001Java \u4E0E\u5B9E\u4F8B\u6587\u4EF6\u5939\uFF0C\u5B8C\u6574\u638C\u63A7\u6BCF\u4E00\u4E2A\u9009\u9879\u3002"),
          tr("\u9010\u6B65\u914D\u7F6E \u00B7 \u5B8C\u6574\u53EF\u63A7") },
        { MethodImport,
          QStringLiteral("I"),
          tr("\u4ECE\u73B0\u6709\u542F\u52A8\u5668\u5BFC\u5165"),
          tr("\u81EA\u52A8\u8BC6\u522B\u7535\u8111\u4E0A\u5DF2\u5B89\u88C5\u7684\u542F\u52A8\u5668\uFF0C\u4E00\u952E\u8FC1\u79FB\u8D26\u6237\u4E0E\u7248\u672C\u3002"),
          tr("\u652F\u6301 PCL \u00B7 HMCL \u00B7 MultiMC \u7B49") },
        { MethodSkip,
          QStringLiteral("S"),
          tr("\u8DF3\u8FC7"),
          tr("\u76F4\u63A5\u8FDB\u5165\u542F\u52A8\u5668\uFF0C\u4F7F\u7528\u9ED8\u8BA4\u8BBE\u7F6E\uFF0C\u968F\u65F6\u53EF\u5728\u8BBE\u7F6E\u4E2D\u4FEE\u6539\u3002"),
          tr("\u7A0D\u540E\u518D\u8BF4 \u00B7 \u9ED8\u8BA4\u914D\u7F6E") },
    };

    for (const MethodDef &def : defs) {
        QPushButton *card = new QPushButton(page);
        card->setObjectName(QStringLiteral("obwMethodCard"));
        card->setCheckable(true);
        card->setCursor(Qt::PointingHandCursor);
        card->setMinimumHeight(210);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QVBoxLayout *clay = new QVBoxLayout(card);
        clay->setContentsMargins(22, 26, 22, 22);   // 对齐 HTML padding: 26px 22px 22px
        clay->setSpacing(8);

        // 选中时顶部 3px 渐变条（对应 HTML .ob-choose-card.selected::before）
        QFrame *topBar = new QFrame(card);
        topBar->setObjectName(QStringLiteral("obwMethodTopBar"));
        topBar->setFixedHeight(3);
        clay->addWidget(topBar);

        // 右上角对勾徽章（默认隐藏；选中时由 QButtonGroup::idClicked 控制显示）
        QLabel *check = new QLabel(QStringLiteral("\u2714"), card);
        check->setObjectName(QStringLiteral("obwMethodCheck"));
        check->setFixedSize(22, 22);
        check->setAlignment(Qt::AlignCenter);
        check->hide();
        check->move(0, 0); // 位置在首次显示时校正

        QLabel *icon = new QLabel(def.icon, card);
        icon->setObjectName(QStringLiteral("obwMethodIcon"));
        icon->setFixedSize(52, 52);
        icon->setAlignment(Qt::AlignCenter);
        // 默认态：accent-soft 浅绿底 + accent 色字母（对齐 HTML .ob-choose-icon 默认）；
        // setStyleSheet 是 widget 级不经过 ThemeManager 占位符替换，需用当前主题色动态构造
        // checked/hover 态由 QSS 后代选择器覆盖为渐变底白字
        {
            ThemeManager *tm = ThemeManager::instance();
            QString primary = tm->currentThemeColor();
            // 浅色主题用 12% 透明主色（accent-soft），深色主题用 20% 透明主色
            QColor soft = QColor(primary);
            soft.setAlphaF(tm->currentTheme() == ThemeManager::LightTheme ? 0.12 : 0.20);
            icon->setStyleSheet(QStringLiteral(
                "background-color: %1;"
                "border-radius: 12px; color: %2;"
                "font-size: 19px; font-weight: 700;")
                .arg(soft.name(QColor::HexArgb), primary));
        }
        clay->addWidget(icon, 0, Qt::AlignHCenter);
        clay->addSpacing(6);

        QLabel *name = new QLabel(def.name, card);
        name->setObjectName(QStringLiteral("obwMethodName"));
        name->setAlignment(Qt::AlignHCenter);
        clay->addWidget(name);

        QLabel *d = new QLabel(def.desc, card);
        d->setObjectName(QStringLiteral("obwMethodDesc"));
        d->setWordWrap(true);
        d->setAlignment(Qt::AlignHCenter);
        clay->addWidget(d);

        QLabel *tag = new QLabel(def.tag, card);
        tag->setObjectName(QStringLiteral("obwMethodTag"));
        // tag 胶囊：HBox 居中保证宽度自适应内容
        QHBoxLayout *tagRow = new QHBoxLayout();
        tagRow->setContentsMargins(0, 0, 0, 0);
        tagRow->addStretch();
        tagRow->addWidget(tag);
        tagRow->addStretch();
        clay->addLayout(tagRow);
        clay->addStretch();

        m_methodChecks.append(check);
        m_methodCards[static_cast<int>(def.method)] = card;
        group->addButton(card, static_cast<int>(def.method));
        cards->addWidget(card, 1);
    }

    connect(group, &QButtonGroup::idClicked,
            this, &OnboardingWizard::onMethodPicked);

    // 选中时显示对应卡片的右上角对勾徽章（默认隐藏，对齐 HTML .ob-check-badge）
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        for (int i = 0; i < m_methodChecks.size(); ++i)
            m_methodChecks.at(i)->setVisible(i == id);
    });

    lay->addLayout(cards);

    QLabel *tip = new QLabel(
        tr("\u65E0\u8BBA\u9009\u62E9\u54EA\u79CD\u65B9\u5F0F\uFF0C\u6240\u6709\u8BBE\u7F6E\u4E4B\u540E\u90FD\u53EF\u4EE5\u5728\u300C\u8BBE\u7F6E\u300D\u4E2D\u968F\u65F6\u4FEE\u6539\u3002"), page);
    tip->setObjectName(QStringLiteral("obwTip"));
    lay->addWidget(tip);
    lay->addStretch(1);

    m_stack->addWidget(page);
}

/* ============================================================
 * Step2 分支 A：导入启动器
 * ============================================================ */
void OnboardingWizard::buildStep2Import()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPage2Import"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 20, 32, 16);
    lay->setSpacing(10);

    OutlinedLabel *title = new OutlinedLabel(tr("\u5BFC\u5165\u73B0\u6709\u542F\u52A8\u5668"), page);
    title->setObjectName(QStringLiteral("obwPageTitle"));
    lay->addWidget(title);

    QLabel *desc = new QLabel(
        tr("\u6B63\u5728\u626B\u63CF\u4F60\u7684\u7535\u8111\uFF0C\u9009\u62E9\u8981\u5BFC\u5165\u7684\u542F\u52A8\u5668\uFF0C\u5C06\u8FC1\u79FB\u5176\u4E2D\u7684\u8D26\u6237\u3001\u7248\u672C\u4E0E\u8BBE\u7F6E\u3002"), page);
    desc->setObjectName(QStringLiteral("obwPageDesc"));
    desc->setWordWrap(true);
    lay->addWidget(desc);
    lay->addSpacing(8);

    m_launcherKeys = { QStringLiteral("pcl"), QStringLiteral("hmcl"), QStringLiteral("multimc"), QStringLiteral("baka") };

    // 真实检测各启动器（参考 HMCL/PCL2/MultiMC/BakaXL 数据目录）
    const QList<LauncherDetect> detects = LauncherImporter::detectAll();
    auto findDetect = [&detects](const QString &key) -> const LauncherDetect & {
        for (const LauncherDetect &d : detects)
            if (d.key == key)
                return d;
        static const LauncherDetect emptyDetect;
        return emptyDetect;
    };

    struct LauncherDef {
        QString display, alias, desc, gradient;
        char letter;
        bool detected;
        QString detectedPath;
    };
    const QVector<LauncherDef> defs = {
        { QStringLiteral("PCL"), QStringLiteral("PCL CE"),
          tr("Plain Craft Launcher \u00B7 \u56FD\u5185\u6700\u6D41\u884C\u7684\u8F7B\u91CF\u542F\u52A8\u5668"),
          QStringLiteral("#F59E0B,#B45309"), 'P', findDetect(QStringLiteral("pcl")).detected,
          findDetect(QStringLiteral("pcl")).dataDir },
        { QStringLiteral("HMCL"), QStringLiteral("Hello Minecraft!"),
          tr("Hello Minecraft! Launcher \u00B7 \u5F00\u6E90\u8DE8\u5E73\u53F0\u542F\u52A8\u5668"),
          QStringLiteral("#6366F1,#4338CA"), 'H', findDetect(QStringLiteral("hmcl")).detected,
          findDetect(QStringLiteral("hmcl")).dataDir },
        { QStringLiteral("MultiMC"), QStringLiteral("Prism"),
          tr("MultiMC / Prism Launcher \u00B7 \u5F00\u6E90\u5B9E\u4F8B\u7BA1\u7406\u542F\u52A8\u5668"),
          QStringLiteral("#10B981,#047857"), 'M', findDetect(QStringLiteral("multimc")).detected,
          findDetect(QStringLiteral("multimc")).dataDir },
        { QStringLiteral("BaKa XL"), QString(),
          tr("BakaXL \u00B7 \u6CE8\u91CD\u6613\u7528\u6027\u7684\u542F\u52A8\u5668"),
          QStringLiteral("#EC4899,#BE185D"), 'B', findDetect(QStringLiteral("baka")).detected,
          findDetect(QStringLiteral("baka")).dataDir },
    };

    QHBoxLayout *cards = new QHBoxLayout();
    cards->setSpacing(14);
    for (int i = 0; i < defs.size(); ++i) {
        const LauncherDef &def = defs.at(i);
        QFrame *card = new QFrame(page);
        card->setObjectName(QStringLiteral("obwLauncherCard"));
        card->setMinimumHeight(200);
        QVBoxLayout *clay = new QVBoxLayout(card);
        clay->setContentsMargins(20, 20, 20, 20);
        clay->setSpacing(8);

        QLabel *logo = new QLabel(QString(QChar(def.letter)), card);
        logo->setObjectName(QStringLiteral("obwLauncherLogo"));
        logo->setFixedSize(52, 52);
        logo->setAlignment(Qt::AlignCenter);
        logo->setStyleSheet(QStringLiteral(
            "background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %1, stop:1 %2);"
            "border-radius: 12px; color: #FFFFFF; font-size: 21px; font-weight: bold;")
            .arg(def.gradient.section(QLatin1Char(','), 0, 0),
                 def.gradient.section(QLatin1Char(','), 1, 1)));
        clay->addWidget(logo);
        clay->addSpacing(14);

        QHBoxLayout *nameLay = new QHBoxLayout();
        nameLay->setSpacing(8);
        QLabel *name = new QLabel(def.display, card);
        name->setObjectName(QStringLiteral("obwLauncherName"));
        nameLay->addWidget(name);
        if (!def.alias.isEmpty()) {
            QLabel *alias = new QLabel(QStringLiteral(" / ") + def.alias, card);
            alias->setObjectName(QStringLiteral("obwLauncherAlias"));
            nameLay->addWidget(alias);
        }
        nameLay->addStretch();
        clay->addLayout(nameLay);

        QLabel *d = new QLabel(def.desc, card);
        d->setObjectName(QStringLiteral("obwLauncherDesc"));
        d->setWordWrap(true);
        clay->addWidget(d);

        QFrame *statusBox = new QFrame(card);
        statusBox->setObjectName(QStringLiteral("obwLauncherStatusBox"));
        QHBoxLayout *statusLay = new QHBoxLayout(statusBox);
        statusLay->setSpacing(6);
        statusLay->setContentsMargins(10, 5, 10, 5);
        m_launcherStatus[i] = new QLabel(QStringLiteral("\u25CF"), statusBox);
        m_launcherStatus[i]->setObjectName(QStringLiteral("obwLauncherStatus"));
        m_launcherStatus[i]->setFixedSize(8, 8);
        m_launcherStatus[i]->setAlignment(Qt::AlignCenter);
        m_launcherStatusText[i] = new QLabel(statusBox);
        m_launcherStatusText[i]->setObjectName(QStringLiteral("obwLauncherStatusText"));
        statusLay->addWidget(m_launcherStatus[i]);
        statusLay->addWidget(m_launcherStatusText[i], 1);
        clay->addWidget(statusBox);

        if (def.detected) {
            const LauncherDetect &det = findDetect(m_launcherKeys.at(i));
            m_launcherStatus[i]->setProperty("state", QStringLiteral("found"));
            m_launcherStatusText[i]->setText(tr("\u5DF2\u68C0\u6D4B\u5230 \u00B7 ")
                                             + (det.detail.isEmpty() ? def.detectedPath : det.detail));
        } else {
            m_launcherStatus[i]->setProperty("state", QStringLiteral("missing"));
            m_launcherStatusText[i]->setText(tr("\u672A\u68C0\u6D4B\u5230\u5B89\u88C5"));
        }

        m_launcherActions[i] = new QWidget(card);
        m_launcherActions[i]->setObjectName(QStringLiteral("obwLauncherActions"));
        QHBoxLayout *aLay = new QHBoxLayout(m_launcherActions[i]);
        aLay->setContentsMargins(0, 4, 0, 0);
        aLay->setSpacing(8);

        const QString key = m_launcherKeys.at(i);
        QPushButton *scanBtn = new QPushButton(tr("\u91CD\u65B0\u68C0\u6D4B"), m_launcherActions[i]);
        scanBtn->setObjectName(QStringLiteral("obwBtnSecondary"));
        scanBtn->setCursor(Qt::PointingHandCursor);
        QPushButton *importBtn = new QPushButton(tr("\u5BFC\u5165"), m_launcherActions[i]);
        importBtn->setObjectName(QStringLiteral("obwBtnPrimary"));
        importBtn->setCursor(Qt::PointingHandCursor);
        connect(scanBtn, &QPushButton::clicked, this, [this, key]() { onLauncherScan(key); });
        connect(importBtn, &QPushButton::clicked, this, [this, key]() { onLauncherImport(key); });
        aLay->addWidget(scanBtn, 1);
        aLay->addWidget(importBtn, 1);
        clay->addWidget(m_launcherActions[i]);
        clay->addStretch();

        cards->addWidget(card, 1);
    }
    lay->addLayout(cards, 1);

    QLabel *tip = new QLabel(
        tr("\u5BFC\u5165\u4E0D\u4F1A\u4FEE\u6539\u6216\u5220\u9664\u539F\u542F\u52A8\u5668\u4E2D\u7684\u4EFB\u4F55\u6570\u636E\uFF0C\u4EC5\u8BFB\u53D6\u5E76\u590D\u5236\u5230\u65B9\u5757\u76D2\u5B50\u7684\u76EE\u5F55\u4E2D\u3002"), page);
    tip->setObjectName(QStringLiteral("obwTip"));
    lay->addWidget(tip);

    m_stack->addWidget(page);
}

/* ============================================================
 * Step2 分支 B：账户管理
 * ============================================================ */
void OnboardingWizard::buildStep2Manual()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPage2Manual"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 20, 32, 16);
    lay->setSpacing(10);

    OutlinedLabel *title = new OutlinedLabel(tr("\u6DFB\u52A0\u8D26\u6237"), page);
    title->setObjectName(QStringLiteral("obwPageTitle"));
    lay->addWidget(title);

    QLabel *desc = new QLabel(
        tr("\u767B\u5F55\u5FAE\u8F6F\u8D26\u53F7\u4F53\u9A8C\u5B8C\u6574\u6B63\u7248\u529F\u80FD\uFF0C\u6216\u4F7F\u7528\u79BB\u7EBF\u6A21\u5F0F\u5FEB\u901F\u5F00\u59CB\u3002"), page);
    desc->setObjectName(QStringLiteral("obwPageDesc"));
    desc->setWordWrap(true);
    lay->addWidget(desc);
    lay->addSpacing(8);

    QHBoxLayout *toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    QPushButton *addBtn = new QPushButton(tr("\u6DFB\u52A0\u8D26\u6237"), page);
    addBtn->setObjectName(QStringLiteral("obwBtnPrimary"));
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, this, &OnboardingWizard::onToggleAddAccount);
    toolbar->addWidget(addBtn);
    QLabel *hint = new QLabel(tr("\u53EF\u6DFB\u52A0\u591A\u4E2A\u8D26\u6237\uFF0C\u6E38\u620F\u4E2D\u968F\u65F6\u5207\u6362"), page);
    hint->setObjectName(QStringLiteral("obwMutedText"));
    toolbar->addWidget(hint);
    toolbar->addStretch();
    lay->addLayout(toolbar);

    m_accountListWidget = new QWidget(page);
    m_accountListWidget->setObjectName(QStringLiteral("obwAccountList"));
    QVBoxLayout *listLay = new QVBoxLayout(m_accountListWidget);
    listLay->setContentsMargins(0, 8, 0, 0);
    listLay->setSpacing(8);
    // stretch=1 让 listWidget 占据 toolbar 与 panel 之间的中间区域，
    // 同时强制 vertical=Fixed 让 layout 不再把多余空间均匀分到 row 之间空隙
    // （否则实测 spacing 从 8 变 ~150+，卡片看起来"未置顶向下排列"）。
    lay->addWidget(m_accountListWidget, 1);
    {
        QSizePolicy sp = m_accountListWidget->sizePolicy();
        sp.setVerticalPolicy(QSizePolicy::Fixed);
        sp.setHorizontalPolicy(QSizePolicy::Expanding);
        m_accountListWidget->setSizePolicy(sp);
    }
    refreshAccountList();

    // 添加账户面板
    m_addAccountPanel = new QWidget(page);
    m_addAccountPanel->setObjectName(QStringLiteral("obwAccountPanel"));
    m_addAccountPanel->setVisible(false);
    QVBoxLayout *panelLay = new QVBoxLayout(m_addAccountPanel);
    panelLay->setContentsMargins(20, 14, 20, 14);
    panelLay->setSpacing(10);

    QHBoxLayout *tabs = new QHBoxLayout();
    tabs->setSpacing(6);
    QButtonGroup *tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);
    const QStringList tabNames = { tr("\u5FAE\u8F6F\u767B\u5F55"), tr("\u79BB\u7EBF\u6A21\u5F0F"), tr("\u6B63\u7248\u767B\u5F55") };
    QPushButton *tabBtns[3] = {nullptr, nullptr, nullptr};
    for (int i = 0; i < 3; ++i) {
        QPushButton *tab = new QPushButton(tabNames.at(i), m_addAccountPanel);
        tab->setObjectName(QStringLiteral("obwAccountTab"));
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tabGroup->addButton(tab, i);
        tabs->addWidget(tab);
        tabBtns[i] = tab;
    }
    tabs->addStretch();
    panelLay->addLayout(tabs);

    // 微软登录表单
    m_formMicrosoft = new QWidget(m_addAccountPanel);
    m_formMicrosoft->setObjectName(QStringLiteral("obwFormMicrosoft"));
    QVBoxLayout *mForm = new QVBoxLayout(m_formMicrosoft);
    mForm->setContentsMargins(0, 4, 0, 0);
    mForm->setSpacing(8);
    QPushButton *msLogin = new QPushButton(tr("\u4F7F\u7528\u5FAE\u8F6F\u8D26\u6237\u767B\u5F55"), m_formMicrosoft);
    msLogin->setObjectName(QStringLiteral("obwBtnPrimary"));
    msLogin->setCursor(Qt::PointingHandCursor);
    connect(msLogin, &QPushButton::clicked, this, [this]() {
        m_footerHint->setText(tr("\u5FAE\u8F6F\u6388\u6743\u6D41\u7A0B\u5C06\u5728\u5B8C\u6210\u5F15\u5BFC\u540E\u63D0\u4F9B\uFF0C\u53EF\u5728\u8D26\u6237\u7BA1\u7406\u4E2D\u5B8C\u6210\u3002"));
    });
    mForm->addWidget(msLogin);
    QLabel *msHint = new QLabel(
        tr("\u5C06\u6253\u5F00\u6D4F\u89C8\u5668\u5B8C\u6210 Xbox / Microsoft \u6388\u6743\uFF0C\u767B\u5F55\u540E\u53EF\u540C\u6B65\u6B63\u7248\u76AE\u80A4\u4E0E\u62AB\u98CE\u3002"), m_formMicrosoft);
    msHint->setObjectName(QStringLiteral("obwMutedText"));
    msHint->setWordWrap(true);
    mForm->addWidget(msHint);
    panelLay->addWidget(m_formMicrosoft);

    // 离线表单
    m_formOffline = new QWidget(m_addAccountPanel);
    m_formOffline->setObjectName(QStringLiteral("obwFormOffline"));
    m_formOffline->setVisible(false);
    QVBoxLayout *oForm = new QVBoxLayout(m_formOffline);
    oForm->setContentsMargins(0, 4, 0, 0);
    oForm->setSpacing(8);
    QLabel *fieldLabel = new QLabel(tr("\u6E38\u620F\u6635\u79F0"), m_formOffline);
    fieldLabel->setObjectName(QStringLiteral("obwFieldLabel"));
    oForm->addWidget(fieldLabel);
    m_offlineNameInput = new QLineEdit(m_formOffline);
    m_offlineNameInput->setObjectName(QStringLiteral("obwInput"));
    m_offlineNameInput->setPlaceholderText(tr("\u8F93\u5165\u6635\u79F0\uFF0C\u4F8B\u5982\uFF1ASteve"));
    m_offlineNameInput->setMaxLength(16);
    oForm->addWidget(m_offlineNameInput);
    QHBoxLayout *oBtns = new QHBoxLayout();
    oBtns->addStretch();
    QPushButton *offCancel = new QPushButton(tr("\u53D6\u6D88"), m_formOffline);
    offCancel->setObjectName(QStringLiteral("obwBtnSecondary"));
    offCancel->setCursor(Qt::PointingHandCursor);
    QPushButton *offAdd = new QPushButton(tr("\u6DFB\u52A0\u79BB\u7EBF\u8D26\u6237"), m_formOffline);
    offAdd->setObjectName(QStringLiteral("obwBtnPrimary"));
    offAdd->setCursor(Qt::PointingHandCursor);
    connect(offCancel, &QPushButton::clicked, this, &OnboardingWizard::onToggleAddAccount);
    connect(offAdd, &QPushButton::clicked, this, &OnboardingWizard::onAddOfflineAccount);
    oBtns->addWidget(offCancel);
    oBtns->addWidget(offAdd);
    oForm->addLayout(oBtns);
    panelLay->addWidget(m_formOffline);

    // 正版登录表单
    m_formLegacy = new QWidget(m_addAccountPanel);
    m_formLegacy->setObjectName(QStringLiteral("obwFormLegacy"));
    m_formLegacy->setVisible(false);
    QVBoxLayout *lForm = new QVBoxLayout(m_formLegacy);
    lForm->setContentsMargins(0, 4, 0, 0);
    lForm->setSpacing(8);
    QLabel *legacyHint = new QLabel(
        tr("\u65E7\u7248 Mojang \u8D26\u6237\u767B\u5F55\uFF0C\u9002\u7528\u4E8E 2020 \u5E74\u524D\u521B\u5EFA\u7684\u8D26\u6237\u3002"), m_formLegacy);
    legacyHint->setObjectName(QStringLiteral("obwMutedText"));
    legacyHint->setWordWrap(true);
    lForm->addWidget(legacyHint);
    QPushButton *legacyLogin = new QPushButton(tr("\u6B63\u7248\u767B\u5F55"), m_formLegacy);
    legacyLogin->setObjectName(QStringLiteral("obwBtnPrimary"));
    legacyLogin->setCursor(Qt::PointingHandCursor);
    connect(legacyLogin, &QPushButton::clicked, this, [this]() {
        m_footerHint->setText(tr("\u65E7\u7248\u6B63\u7248\u8D26\u6237\u9A8C\u8BC1\u5C06\u5728\u5B8C\u6210\u5F15\u5BFC\u540E\u63D0\u4F9B\u3002"));
    });
    lForm->addWidget(legacyLogin);
    panelLay->addWidget(m_formLegacy);

    lay->addWidget(m_addAccountPanel);

    connect(tabGroup, &QButtonGroup::idClicked,
            this, &OnboardingWizard::onSwitchAccountTab);
    tabBtns[0]->setChecked(true);

    m_stack->addWidget(page);
}

/* ============================================================
 * Step3 Java 管理
 * ============================================================ */
void OnboardingWizard::buildStep3()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPage3"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 20, 32, 16);
    lay->setSpacing(10);

    OutlinedLabel *title = new OutlinedLabel(tr("Java \u8FD0\u884C\u65F6\u7BA1\u7406"), page);
    title->setObjectName(QStringLiteral("obwPageTitle"));
    lay->addWidget(title);

    QLabel *desc = new QLabel(
        tr("Minecraft \u4F9D\u8D56 Java \u8FD0\u884C\u3002\u626B\u63CF\u672C\u673A\u5DF2\u5B89\u88C5\u7684 Java\uFF0C\u6216\u76F4\u63A5\u4E0B\u8F7D\u5B98\u65B9\u7248\u672C\u3002"), page);
    desc->setObjectName(QStringLiteral("obwPageDesc"));
    desc->setWordWrap(true);
    lay->addWidget(desc);
    lay->addSpacing(8);

    QHBoxLayout *toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    m_scanJavaBtn = new QPushButton(tr("\u626B\u63CF\u672C\u673A Java"), page);
    m_scanJavaBtn->setObjectName(QStringLiteral("obwBtnPrimary"));
    m_scanJavaBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanJavaBtn, &QPushButton::clicked, this, &OnboardingWizard::onScanJava);
    toolbar->addWidget(m_scanJavaBtn);
    m_scanJavaText = new QLabel(
        tr("\u5C1A\u672A\u626B\u63CF \u00B7 \u5DF2\u6709 %1 \u4E2A\u8FD0\u884C\u65F6")
            .arg(SettingsManager::instance()->getJavaInstallations().size()), page);
    m_scanJavaText->setObjectName(QStringLiteral("obwMutedText"));
    toolbar->addWidget(m_scanJavaText);
    toolbar->addStretch();
    lay->addLayout(toolbar);

    QLabel *secTitle = makeSectionTitle(tr("\u5DF2\u5B89\u88C5\u7684 Java"));
    lay->addWidget(secTitle);

    QScrollArea *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("obwScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMaximumHeight(180);
    m_javaListWidget = new QWidget(scroll);
    m_javaListWidget->setObjectName(QStringLiteral("obwJavaList"));
    QVBoxLayout *jList = new QVBoxLayout(m_javaListWidget);
    jList->setContentsMargins(0, 0, 0, 0);
    jList->setSpacing(8);
    scroll->setWidget(m_javaListWidget);
    lay->addWidget(scroll, 1);

    QLabel *dlTitle = makeSectionTitle(tr("\u4E0B\u8F7D Java\uFF08\u5B98\u65B9\u6E90\uFF09"));
    lay->addWidget(dlTitle);

    QHBoxLayout *dlCards = new QHBoxLayout();
    dlCards->setSpacing(12);
    struct DlInfo { QString ver; QString tag; QString size; };
    const QVector<DlInfo> dlInfos = {
        { QStringLiteral("Java 8"),  tr("\u517C\u5BB9 1.7 \u2013 1.16"),  QStringLiteral("JRE \u00B7 87 MB") },
        { QStringLiteral("Java 17"), tr("\u517C\u5BB9 1.17 \u2013 1.20"), QStringLiteral("JRE \u00B7 176 MB") },
        { QStringLiteral("Java 21"), tr("\u517C\u5BB9 1.20.5+"),          QStringLiteral("JRE \u00B7 190 MB") },
        { QStringLiteral("Java 24"), tr("\u6700\u65B0\u7248\u672C"),      QStringLiteral("JRE \u00B7 200 MB") },
    };
    for (const DlInfo &info : dlInfos) {
        QFrame *dlCard = new QFrame(page);
        dlCard->setObjectName(QStringLiteral("obwDlCard"));
        QVBoxLayout *dLay = new QVBoxLayout(dlCard);
        dLay->setContentsMargins(10, 12, 10, 10);
        dLay->setSpacing(2);
        QLabel *ver = new QLabel(info.ver, dlCard);
        ver->setObjectName(QStringLiteral("obwDlVer"));
        ver->setAlignment(Qt::AlignCenter);
        dLay->addWidget(ver);
        QLabel *tag = new QLabel(info.tag, dlCard);
        tag->setObjectName(QStringLiteral("obwDlTag"));
        tag->setAlignment(Qt::AlignCenter);
        dLay->addWidget(tag);
        QLabel *size = new QLabel(info.size, dlCard);
        size->setObjectName(QStringLiteral("obwDlSize"));
        size->setAlignment(Qt::AlignCenter);
        dLay->addWidget(size);
        QPushButton *dlBtn = new QPushButton(tr("\u4E0B\u8F7D"), dlCard);
        dlBtn->setObjectName(QStringLiteral("obwBtnPrimary"));
        dlBtn->setCursor(Qt::PointingHandCursor);
        connect(dlBtn, &QPushButton::clicked, this, [this, info]() {
            m_footerHint->setText(
                tr("\u5B8C\u6210\u5F15\u5BFC\u540E\u53EF\u5728\u300C\u8BBE\u7F6E > Java \u4E0B\u8F7D\u300D\u5B8C\u6210 %1 \u4E0B\u8F7D\u3002").arg(info.ver));
        });
        dLay->addWidget(dlBtn);
        dlCards->addWidget(dlCard, 1);
    }
    lay->addLayout(dlCards);

    m_stack->addWidget(page);
    refreshJavaList();
}

/* ============================================================
 * Step4 实例文件夹
 * ============================================================ */
void OnboardingWizard::buildStep4()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPage4"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 20, 32, 16);
    lay->setSpacing(10);

    OutlinedLabel *title = new OutlinedLabel(tr("\u5B9E\u4F8B\u6587\u4EF6\u5939\u7BA1\u7406"), page);
    title->setObjectName(QStringLiteral("obwPageTitle"));
    lay->addWidget(title);

    QLabel *desc = new QLabel(
        tr("\u6E38\u620F\u5B9E\u4F8B\u4F1A\u4FDD\u5B58\u5728\u5B9E\u4F8B\u6587\u4EF6\u5939\u4E2D\uFF0C\u53EF\u8BBE\u7F6E\u4E00\u4E2A\u9ED8\u8BA4\u4F4D\u7F6E\u3002"), page);
    desc->setObjectName(QStringLiteral("obwPageDesc"));
    desc->setWordWrap(true);
    lay->addWidget(desc);
    lay->addSpacing(8);

    QHBoxLayout *toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);
    QPushButton *addBtn = new QPushButton(tr("\u6DFB\u52A0\u6587\u4EF6\u5939"), page);
    addBtn->setObjectName(QStringLiteral("obwBtnPrimary"));
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, this, &OnboardingWizard::onToggleAddFolder);
    toolbar->addWidget(addBtn);
    QLabel *hint = new QLabel(tr("\u65B0\u5B9E\u4F8B\u9ED8\u8BA4\u521B\u5EFA\u5728\u6807\u6CE8\u300C\u9ED8\u8BA4\u300D\u7684\u6587\u4EF6\u5939\u4E2D"), page);
    hint->setObjectName(QStringLiteral("obwMutedText"));
    toolbar->addWidget(hint);
    toolbar->addStretch();
    lay->addLayout(toolbar);

    m_folderListWidget = new QWidget(page);
    m_folderListWidget->setObjectName(QStringLiteral("obwFolderList"));
    QVBoxLayout *fList = new QVBoxLayout(m_folderListWidget);
    fList->setContentsMargins(0, 8, 0, 0);
    fList->setSpacing(8);
    // 同 m_accountListWidget：stretch=0 + vertical=Fixed（见 buildStep2Manual 注释）
    lay->addWidget(m_folderListWidget, 0);
    {
        QSizePolicy sp = m_folderListWidget->sizePolicy();
        sp.setVerticalPolicy(QSizePolicy::Fixed);
        sp.setHorizontalPolicy(QSizePolicy::Expanding);
        m_folderListWidget->setSizePolicy(sp);
    }
    refreshFolderList();

    // 添加文件夹面板
    m_addFolderPanel = new QWidget(page);
    m_addFolderPanel->setObjectName(QStringLiteral("obwFolderPanel"));
    m_addFolderPanel->setVisible(false);
    QVBoxLayout *pLay = new QVBoxLayout(m_addFolderPanel);
    pLay->setContentsMargins(20, 14, 20, 14);
    pLay->setSpacing(8);

    QLabel *panelTitle = new QLabel(tr("\u65B0\u5EFA\u5B9E\u4F8B\u6587\u4EF6\u5939"), m_addFolderPanel);
    panelTitle->setObjectName(QStringLiteral("obwSectionTitleLabel"));
    pLay->addWidget(panelTitle);

    QLabel *nameLabel = new QLabel(tr("\u6587\u4EF6\u5939\u540D\u79F0"), m_addFolderPanel);
    nameLabel->setObjectName(QStringLiteral("obwFieldLabel"));
    pLay->addWidget(nameLabel);
    m_newFolderName = new QLineEdit(m_addFolderPanel);
    m_newFolderName->setObjectName(QStringLiteral("obwInput"));
    m_newFolderName->setPlaceholderText(tr("\u4F8B\u5982\uFF1A\u5149\u5F71\u6574\u5408\u5305\u5E93"));
    pLay->addWidget(m_newFolderName);

    QLabel *pathLabel = new QLabel(tr("\u8DEF\u5F84"), m_addFolderPanel);
    pathLabel->setObjectName(QStringLiteral("obwFieldLabel"));
    pLay->addWidget(pathLabel);
    QHBoxLayout *pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);
    m_newFolderPath = new QLineEdit(m_addFolderPanel);
    m_newFolderPath->setObjectName(QStringLiteral("obwInput"));
    m_newFolderPath->setText(QDir::homePath() + QStringLiteral("/.minecraft/instances"));
    pathRow->addWidget(m_newFolderPath, 1);
    QPushButton *browseBtn = new QPushButton(tr("\u6D4F\u89C8\u2026"), m_addFolderPanel);
    browseBtn->setObjectName(QStringLiteral("obwBtnSecondary"));
    browseBtn->setCursor(Qt::PointingHandCursor);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        m_footerHint->setText(tr("\u6587\u4EF6\u5939\u9009\u62E9\u5668\u5C06\u5728\u5B8C\u6210\u5F15\u5BFC\u540E\u63D0\u4F9B\uFF0C\u53EF\u624B\u52A8\u8F93\u5165\u8DEF\u5F84\u3002"));
    });
    pathRow->addWidget(browseBtn);
    pLay->addLayout(pathRow);

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    QPushButton *cancelBtn = new QPushButton(tr("\u53D6\u6D88"), m_addFolderPanel);
    cancelBtn->setObjectName(QStringLiteral("obwBtnSecondary"));
    cancelBtn->setCursor(Qt::PointingHandCursor);
    QPushButton *createBtn = new QPushButton(tr("\u521B\u5EFA\u5E76\u6DFB\u52A0"), m_addFolderPanel);
    createBtn->setObjectName(QStringLiteral("obwBtnPrimary"));
    createBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &OnboardingWizard::onToggleAddFolder);
    connect(createBtn, &QPushButton::clicked, this, &OnboardingWizard::onAddFolder);
    btns->addWidget(cancelBtn);
    btns->addWidget(createBtn);
    pLay->addLayout(btns);

    lay->addWidget(m_addFolderPanel);

    m_stack->addWidget(page);
}

/* ============================================================
 * Step5 样式设置
 * ============================================================ */
void OnboardingWizard::buildStep5()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPage5"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(32, 20, 32, 16);
    lay->setSpacing(10);

    OutlinedLabel *title = new OutlinedLabel(tr("\u4E2A\u6027\u5316\u5916\u89C2"), page);
    title->setObjectName(QStringLiteral("obwPageTitle"));
    lay->addWidget(title);

    QLabel *desc = new QLabel(
        tr("\u9009\u62E9\u4E3B\u9898\u3001\u80CC\u666F\u4E0E\u5F3A\u8C03\u8272\uFF0C\u8BA9\u65B9\u5757\u76D2\u5B50\u770B\u8D77\u6765\u50CF\u4F60\u7684\u542F\u52A8\u5668\u3002\u6240\u6709\u8C03\u6574\u5B9E\u65F6\u751F\u6548\u3002"), page);
    desc->setObjectName(QStringLiteral("obwPageDesc"));
    desc->setWordWrap(true);
    lay->addWidget(desc);
    lay->addSpacing(8);

    QHBoxLayout *cols = new QHBoxLayout();
    cols->setSpacing(14);

    // 左列：主题 + 背景
    QVBoxLayout *leftCol = new QVBoxLayout();
    leftCol->setSpacing(14);

    QFrame *themeBlock = new QFrame(page);
    themeBlock->setObjectName(QStringLiteral("obwStyleBlock"));
    QVBoxLayout *tLay = new QVBoxLayout(themeBlock);
    tLay->setContentsMargins(18, 14, 18, 14);
    tLay->setSpacing(10);
    QLabel *tTitle = new QLabel(tr("\u4E3B\u9898\u6A21\u5F0F"), themeBlock);
    tTitle->setObjectName(QStringLiteral("obwStyleBlockTitle"));
    tLay->addWidget(tTitle);

    QHBoxLayout *themeRow = new QHBoxLayout();
    themeRow->setSpacing(12);
    const QStringList themeNames = { tr("\u6D45\u8272"), tr("\u6DF1\u8272") };
    QButtonGroup *themeGroup = new QButtonGroup(this);
    themeGroup->setExclusive(true);
    for (int i = 0; i < 2; ++i) {
        // 主题卡片：顶部 mini UI 缩略图 + 底部标签
        QPushButton *card = new QPushButton(themeBlock);
        card->setObjectName(QStringLiteral("obwThemeCard"));
        card->setCheckable(true);
        card->setCursor(Qt::PointingHandCursor);

        QVBoxLayout *cLay = new QVBoxLayout(card);
        cLay->setContentsMargins(8, 8, 8, 8);
        cLay->setSpacing(6);

        // mini UI 预览（对应 HTML .theme-preview：顶栏 + 侧栏 + 内容行）
        QWidget *preview = new QWidget(card);
        preview->setObjectName(QStringLiteral("obwThemePreview"));
        preview->setProperty("mode", i == 0 ? QStringLiteral("light") : QStringLiteral("dark"));
        preview->setFixedHeight(78);
        QVBoxLayout *pLay = new QVBoxLayout(preview);
        pLay->setContentsMargins(6, 5, 6, 5);
        pLay->setSpacing(5);

        QHBoxLayout *bar = new QHBoxLayout();
        bar->setSpacing(4);
        for (int c = 0; c < 3; ++c) {
            QLabel *dot = new QLabel(preview);
            dot->setObjectName(QStringLiteral("obwPreviewDot"));
            dot->setFixedSize(8, 8);
            bar->addWidget(dot);
        }
        bar->addStretch();
        pLay->addLayout(bar);

        QHBoxLayout *body = new QHBoxLayout();
        body->setSpacing(6);
        QLabel *side = new QLabel(preview);
        side->setObjectName(QStringLiteral("obwPreviewSide"));
        side->setFixedSize(28, 44);
        body->addWidget(side);
        // 用 QWidget 容器避免嵌套 layout 引用问题
        QWidget *mainContainer = new QWidget(preview);
        QVBoxLayout *main = new QVBoxLayout(mainContainer);
        main->setContentsMargins(0, 0, 0, 0);
        main->setSpacing(5);
        QLabel *line1 = new QLabel(mainContainer);
        line1->setObjectName(QStringLiteral("obwPreviewLine"));
        line1->setFixedHeight(8);
        QLabel *line2 = new QLabel(mainContainer);
        line2->setObjectName(QStringLiteral("obwPreviewLine"));
        line2->setFixedSize(54, 8);
        QLabel *accent = new QLabel(mainContainer);
        accent->setObjectName(QStringLiteral("obwPreviewAccent"));
        accent->setFixedHeight(8);
        main->addWidget(line1);
        main->addWidget(line2);
        main->addWidget(accent);
        body->addWidget(mainContainer, 1);
        pLay->addLayout(body, 1);

        cLay->addWidget(preview);

        QLabel *label = new QLabel(themeNames.at(i), card);
        label->setObjectName(QStringLiteral("obwThemeCardLabel"));
        label->setAlignment(Qt::AlignCenter);
        cLay->addWidget(label);

        themeGroup->addButton(card, i);
        themeRow->addWidget(card, 1);
    }
    tLay->addLayout(themeRow);
    connect(themeGroup, &QButtonGroup::idClicked, this,
            [this](int idx) { onPickTheme(idx == 1); });
    const bool isDark = (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
    if (isDark) {
        themeGroup->button(1)->setChecked(true);
    } else {
        themeGroup->button(0)->setChecked(true);
    }
    leftCol->addWidget(themeBlock);

    QFrame *bgBlock = new QFrame(page);
    bgBlock->setObjectName(QStringLiteral("obwStyleBlock"));
    QVBoxLayout *bLay = new QVBoxLayout(bgBlock);
    bLay->setContentsMargins(18, 14, 18, 14);
    bLay->setSpacing(10);
    QLabel *bTitle = new QLabel(tr("\u7A97\u53E3\u80CC\u666F"), bgBlock);
    bTitle->setObjectName(QStringLiteral("obwStyleBlockTitle"));
    bLay->addWidget(bTitle);

    // ── 三种背景模式：经典 / 纯色 / 图片（并排 tab 卡片） ──
    QHBoxLayout *modeRow = new QHBoxLayout();
    modeRow->setSpacing(8);
    QButtonGroup *bgGroup = new QButtonGroup(this);
    bgGroup->setExclusive(true);
    struct BgModeDef { QString name; QString hint; QString icon; };
    const QVector<BgModeDef> modeDefs = {
        { tr("\u7ECF\u5178"), tr("\u9ED8\u8BA4\u6E10\u53D8\u80CC\u666F"), QStringLiteral("\u25C6") },
        { tr("\u7EAF\u8272"), tr("\u5355\u8272\u80CC\u666F"), QStringLiteral("\u25CF") },
        { tr("\u56FE\u7247"), tr("\u81EA\u5B9A\u4E49\u56FE\u7247"), QStringLiteral("\u25A3") },
        { tr("\u6D41\u5149"), tr("\u52A8\u6001\u6D41\u5149\u80CC\u666F"), QStringLiteral("\u2726") },
        { tr("\u65CB\u8F6C"), tr("\u65CB\u8F6C\u5168\u666F\u80CC\u666F"), QStringLiteral("\u21BB") },
    };
    for (int i = 0; i < modeDefs.size(); ++i) {
        QPushButton *modeBtn = new QPushButton(bgBlock);
        modeBtn->setObjectName(QStringLiteral("obwBgModeBtn"));
        modeBtn->setCheckable(true);
        modeBtn->setCursor(Qt::PointingHandCursor);
        modeBtn->setFlat(true);
        modeBtn->setMinimumHeight(68);
        modeBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QVBoxLayout *mLay = new QVBoxLayout(modeBtn);
        mLay->setContentsMargins(6, 8, 6, 8);
        mLay->setSpacing(2);
        QLabel *iconL = new QLabel(modeDefs.at(i).icon, modeBtn);
        iconL->setObjectName(QStringLiteral("obwBgModeIcon"));
        iconL->setAlignment(Qt::AlignCenter);
        mLay->addWidget(iconL);
        QLabel *nameL = new QLabel(modeDefs.at(i).name, modeBtn);
        nameL->setObjectName(QStringLiteral("obwBgModeName"));
        nameL->setAlignment(Qt::AlignCenter);
        mLay->addWidget(nameL);
        QLabel *hintL = new QLabel(modeDefs.at(i).hint, modeBtn);
        hintL->setObjectName(QStringLiteral("obwBgModeHint"));
        hintL->setAlignment(Qt::AlignCenter);
        mLay->addWidget(hintL);

        bgGroup->addButton(modeBtn, i);
        modeRow->addWidget(modeBtn, 1);
    }
    bLay->addLayout(modeRow);
    connect(bgGroup, &QButtonGroup::idClicked, this, [this](int idx) {
        onPickBg(idx);
        if (m_bgSubStack)
            m_bgSubStack->setCurrentIndex(idx);
    });

    // ── 背景子项区（随模式切换） ──
    m_bgSubStack = new QStackedWidget(bgBlock);
    m_bgSubStack->setObjectName(QStringLiteral("obwBgSubStack"));

    // 子项 0：经典 —— 说明
    {
        QWidget *classicPage = new QWidget(m_bgSubStack);
        QVBoxLayout *cpLay = new QVBoxLayout(classicPage);
        cpLay->setContentsMargins(4, 2, 4, 2);
        QLabel *classicDesc = new QLabel(
            tr("\u4F7F\u7528\u7ECF\u5178\u7684\u6E10\u53D8\u80CC\u666F\uFF0C\u7B80\u6D01\u8212\u9002\uFF0C\u9002\u5408\u5927\u591A\u6570\u4EBA\u3002"),
            classicPage);
        classicDesc->setObjectName(QStringLiteral("obwBgSubDesc"));
        classicDesc->setWordWrap(true);
        cpLay->addWidget(classicDesc);
        m_bgSubStack->addWidget(classicPage);
    }

    // 子项 1：纯色 —— 预设色 + 自定义
    {
        QWidget *solidPage = new QWidget(m_bgSubStack);
        QVBoxLayout *spLay = new QVBoxLayout(solidPage);
        spLay->setContentsMargins(4, 2, 4, 2);
        spLay->setSpacing(8);

        QHBoxLayout *colorRow = new QHBoxLayout();
        colorRow->setSpacing(10);
        QButtonGroup *colorGroup = new QButtonGroup(this);
        colorGroup->setExclusive(true);
        const QStringList presetColors = {
            QStringLiteral("#10B981"), QStringLiteral("#6366F1"),
            QStringLiteral("#EC4899"), QStringLiteral("#F59E0B"),
            QStringLiteral("#06B6D4"), QStringLiteral("#64748B"),
        };
        for (int i = 0; i < presetColors.size(); ++i) {
            QPushButton *sw = new QPushButton(solidPage);
            sw->setObjectName(QStringLiteral("obwColorSwatch"));
            sw->setFixedSize(30, 30);
            sw->setCheckable(true);
            sw->setCursor(Qt::PointingHandCursor);
            sw->setProperty("hex", presetColors.at(i));
            const QString hex = presetColors.at(i);
            sw->setStyleSheet(QStringLiteral(
                "background-color: %1; border-radius: 15px;"
                "border: 2px solid transparent; color: #FFFFFF; font-size: 13px; font-weight: 800;")
                .arg(hex));
            colorGroup->addButton(sw, i);
            m_colorSwatches.append(sw);
            colorRow->addWidget(sw);
            connect(sw, &QPushButton::toggled, this, [this, sw, hex](bool on) {
                sw->setText(on ? QStringLiteral("\u2714") : QString());
                sw->setStyleSheet(QStringLiteral(
                    "background-color: %1; border-radius: 15px;"
                    "border: 2px solid %2; color: #FFFFFF; font-size: 13px; font-weight: 800;")
                    .arg(hex, on ? QStringLiteral("#FFFFFF") : QStringLiteral("transparent")));
            });
        }
        colorRow->addStretch();
        spLay->addLayout(colorRow);

        QPushButton *customBtn = new QPushButton(tr("\u81EA\u5B9A\u4E49\u989C\u8272\u2026"), solidPage);
        customBtn->setObjectName(QStringLiteral("obwBtnSecondary"));
        customBtn->setCursor(Qt::PointingHandCursor);
        connect(customBtn, &QPushButton::clicked, this, [this]() {
            const QColor cur = QColor(BackgroundManager::instance()->solidColor());
            const QColor c = QColorDialog::getColor(cur.isValid() ? cur : QColor(QStringLiteral("#10B981")),
                                                    this, tr("\u9009\u62E9\u80CC\u666F\u989C\u8272"));
            if (!c.isValid())
                return;
            onPickSolidColor(c.name());
            // 若与预设色匹配则同步选中色块，否则取消全部选中
            bool matched = false;
            for (int i = 0; i < m_colorSwatches.size(); ++i) {
                const QString hex = m_colorSwatches.at(i)->property("hex").toString();
                const bool on = hex.compare(c.name(), Qt::CaseInsensitive) == 0;
                m_colorSwatches.at(i)->setChecked(on);
                if (on)
                    matched = true;
            }
            Q_UNUSED(matched);
        });
        spLay->addWidget(customBtn, 0, Qt::AlignLeft);
        m_bgSubStack->addWidget(solidPage);
    }

    // 子项 2：图片 —— 预览 + 路径 + 浏览
    {
        QWidget *imagePage = new QWidget(m_bgSubStack);
        QHBoxLayout *ipLay = new QHBoxLayout(imagePage);
        ipLay->setContentsMargins(4, 2, 4, 2);
        ipLay->setSpacing(10);

        m_imagePreview = new QLabel(imagePage);
        m_imagePreview->setObjectName(QStringLiteral("obwImagePreview"));
        m_imagePreview->setFixedSize(104, 64);
        m_imagePreview->setAlignment(Qt::AlignCenter);
        m_imagePreview->setText(tr("\u672A\u9009\u62E9\u56FE\u7247"));
        ipLay->addWidget(m_imagePreview);

        QWidget *imgInfo = new QWidget(imagePage);
        QVBoxLayout *iiLay = new QVBoxLayout(imgInfo);
        iiLay->setContentsMargins(0, 0, 0, 0);
        iiLay->setSpacing(6);
        m_imagePathLabel = new QLabel(tr("\u9009\u62E9\u4E00\u5F20\u56FE\u7247\u4F5C\u4E3A\u542F\u52A8\u5668\u80CC\u666F"), imgInfo);
        m_imagePathLabel->setObjectName(QStringLiteral("obwBgPathLabel"));
        m_imagePathLabel->setWordWrap(true);
        iiLay->addWidget(m_imagePathLabel);
        QPushButton *browseBtn = new QPushButton(tr("\u9009\u62E9\u56FE\u7247\u2026"), imgInfo);
        browseBtn->setObjectName(QStringLiteral("obwBtnSecondary"));
        browseBtn->setCursor(Qt::PointingHandCursor);
        connect(browseBtn, &QPushButton::clicked, this, &OnboardingWizard::onBrowseImage);
        iiLay->addWidget(browseBtn);
        ipLay->addWidget(imgInfo, 1);
        m_bgSubStack->addWidget(imagePage);
    }

    // 子项 3：流光 —— 动态流光背景说明
    {
        QWidget *flowPage = new QWidget(m_bgSubStack);
        QVBoxLayout *fpLay = new QVBoxLayout(flowPage);
        fpLay->setContentsMargins(4, 2, 4, 2);
        QLabel *flowDesc = new QLabel(
            tr("\u4F7F\u7528\u52A8\u6001\u6D41\u5149\u80CC\u666F\uFF0C\u5F69\u8272\u5149\u6591\u7F13\u6162\u6F02\u79FB\uFF0C\u4EA4\u9523\u6253\u626B\u7684\u5149\u675F\u589E\u6DFB\u6D3B\u529B\u3002"),
            flowPage);
        flowDesc->setObjectName(QStringLiteral("obwBgSubDesc"));
        flowDesc->setWordWrap(true);
        fpLay->addWidget(flowDesc);
        m_bgSubStack->addWidget(flowPage);
    }

    // 子项 4：旋转 —— 旋转全景背景说明 + 选图
    {
        QWidget *rotatePage = new QWidget(m_bgSubStack);
        QVBoxLayout *rpLay = new QVBoxLayout(rotatePage);
        rpLay->setContentsMargins(4, 2, 4, 2);
        rpLay->setSpacing(8);
        QLabel *rotateDesc = new QLabel(
            tr("\u80CC\u666F\u56FE\u7247\u5C06\u7F13\u6162\u65CB\u8F6C\uFF0C\u5982\u300A\u6211\u7684\u4E16\u754C\u300B\u542F\u52A8\u754C\u9762\u822C\u6C89\u6D78\uFF0C\u65CB\u8F6C\u4F7F\u7528\u300C\u56FE\u7247\u300D\u6A21\u5F0F\u4E2D\u9009\u62E9\u7684\u56FE\u7247\u3002"),
            rotatePage);
        rotateDesc->setObjectName(QStringLiteral("obwBgSubDesc"));
        rotateDesc->setWordWrap(true);
        rpLay->addWidget(rotateDesc);
        QPushButton *rotateBrowse = new QPushButton(tr("\u9009\u62E9\u56FE\u7247\u2026"), rotatePage);
        rotateBrowse->setObjectName(QStringLiteral("obwBtnSecondary"));
        rotateBrowse->setCursor(Qt::PointingHandCursor);
        connect(rotateBrowse, &QPushButton::clicked, this, [this]() {
            const QString path = AppFileDialog::getOpenFileName(
                this, tr("\u9009\u62E9\u80CC\u666F\u56FE\u7247"), QDir::homePath(),
                tr("\u56FE\u7247\u6587\u4EF6 (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (path.isEmpty())
                return;
            BackgroundManager *bg = BackgroundManager::instance();
            bg->setMode(BackgroundManager::Rotating);
            bg->setImagePath(path);
            bg->saveToSettings();
            updateImagePreview(path);
            m_footerHint->setText(tr("\u65CB\u8F6C\u80CC\u666F\u56FE\u7247\u5DF2\u5E94\u7528\u3002"));
        });
        rpLay->addWidget(rotateBrowse, 0, Qt::AlignLeft);
        m_bgSubStack->addWidget(rotatePage);
    }

    bLay->addWidget(m_bgSubStack);

    // ── 初始化：同步 BackgroundManager 当前状态 ──
    {
        BackgroundManager *bgm = BackgroundManager::instance();
        switch (bgm->currentMode()) {
        case BackgroundManager::SolidColor: {
            bgGroup->button(1)->setChecked(true);
            m_bgSubStack->setCurrentIndex(1);
            const QString cur = bgm->solidColor();
            for (int i = 0; i < m_colorSwatches.size(); ++i) {
                const QString hex = m_colorSwatches.at(i)->property("hex").toString();
                m_colorSwatches.at(i)->setChecked(hex.compare(cur, Qt::CaseInsensitive) == 0);
            }
            break;
        }
        case BackgroundManager::Image: {
            bgGroup->button(2)->setChecked(true);
            m_bgSubStack->setCurrentIndex(2);
            updateImagePreview(bgm->imagePath());
            break;
        }
        case BackgroundManager::FlowLight: {
            bgGroup->button(3)->setChecked(true);
            m_bgSubStack->setCurrentIndex(3);
            break;
        }
        case BackgroundManager::Rotating: {
            bgGroup->button(4)->setChecked(true);
            m_bgSubStack->setCurrentIndex(4);
            updateImagePreview(bgm->imagePath());
            break;
        }
        case BackgroundManager::Classic:
        default:
            bgGroup->button(0)->setChecked(true);
            m_bgSubStack->setCurrentIndex(0);
            break;
        }
    }
    leftCol->addWidget(bgBlock);
    cols->addLayout(leftCol, 1);

    // 右列：强调色 + 界面细节
    QVBoxLayout *rightCol = new QVBoxLayout();
    rightCol->setSpacing(14);

    QFrame *accentBlock = new QFrame(page);
    accentBlock->setObjectName(QStringLiteral("obwStyleBlock"));
    QVBoxLayout *aLay = new QVBoxLayout(accentBlock);
    aLay->setContentsMargins(18, 14, 18, 14);
    aLay->setSpacing(10);
    QLabel *aTitle = new QLabel(tr("\u5F3A\u8C03\u8272"), accentBlock);
    aTitle->setObjectName(QStringLiteral("obwStyleBlockTitle"));
    aLay->addWidget(aTitle);

    QHBoxLayout *swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(10);
    QButtonGroup *accentGroup = new QButtonGroup(this);
    accentGroup->setExclusive(true);
    for (int i = 0; i < 6; ++i) {
        QPushButton *swatch = new QPushButton(accentBlock);
        swatch->setObjectName(QStringLiteral("obwAccentSwatch"));
        swatch->setFixedSize(36, 36);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setCheckable(true);
        swatch->setStyleSheet(QStringLiteral(
            "background-color: %1; border-radius: 18px; border: 2px solid transparent; color: #FFFFFF; font-size: 15px; font-weight: 800;")
            .arg(QString::fromLatin1(kAccentHex[i])));
        accentGroup->addButton(swatch, i);
        swatchRow->addWidget(swatch);
    }
    swatchRow->addStretch();
    aLay->addLayout(swatchRow);

    m_accentNameLabel = new QLabel(accentBlock);
    m_accentNameLabel->setObjectName(QStringLiteral("obwAccentName"));
    aLay->addWidget(m_accentNameLabel);

    const QString curColor = ThemeManager::instance()->currentThemeColor();
    int curIdx = 0;
    for (int i = 0; i < 6; ++i) {
        if (QString::fromLatin1(kAccentHex[i]).compare(curColor, Qt::CaseInsensitive) == 0) {
            curIdx = i;
            break;
        }
    }
    accentGroup->button(curIdx)->setChecked(true);
    m_accentNameLabel->setText(QString::fromUtf8(kAccentNames[curIdx])
                               + QStringLiteral(" \u00B7 ")
                               + QString::fromLatin1(kAccentHex[curIdx]));
    // 选中色块显示对勾 + 白色描边（内联 QSS 无法写 :checked，用代码切换）
    for (int i = 0; i < 6; ++i) {
        QPushButton *swatch = qobject_cast<QPushButton *>(accentGroup->button(i));
        if (!swatch)
            continue;
        const QString hex = QString::fromLatin1(kAccentHex[i]);
        const auto applyStyle = [swatch, hex](bool on) {
            swatch->setText(on ? QStringLiteral("\u2714") : QString());
            swatch->setStyleSheet(QStringLiteral(
                "background-color: %1; border-radius: 18px;"
                "border: 2px solid %2; color: #FFFFFF; font-size: 15px; font-weight: 800;")
                .arg(hex, on ? QStringLiteral("#FFFFFF") : QStringLiteral("transparent")));
        };
        applyStyle(swatch->isChecked());
        connect(swatch, &QPushButton::toggled, this, [applyStyle](bool on) { applyStyle(on); });
    }
    connect(accentGroup, &QButtonGroup::idClicked, this,
            [this](int idx) { onPickAccent(QString::fromLatin1(kAccentHex[idx])); });
    rightCol->addWidget(accentBlock);

    QFrame *detailBlock = new QFrame(page);
    detailBlock->setObjectName(QStringLiteral("obwStyleBlock"));
    QVBoxLayout *dLay = new QVBoxLayout(detailBlock);
    dLay->setContentsMargins(18, 14, 18, 14);
    dLay->setSpacing(6);
    QLabel *dTitle = new QLabel(tr("\u754C\u9762\u7EC6\u8282"), detailBlock);
    dTitle->setObjectName(QStringLiteral("obwStyleBlockTitle"));
    dLay->addWidget(dTitle);

    CustomCheckBox *fps = new CustomCheckBox(tr("\u542F\u52A8\u6E38\u620F\u65F6\u663E\u793A FPS"), detailBlock);
    fps->setChecked(true);
    CustomCheckBox *glass = new CustomCheckBox(tr("\u73BB\u7483\u62DF\u6001\u6548\u679C"), detailBlock);
    glass->setChecked(true);
    CustomCheckBox *anim = new CustomCheckBox(tr("\u5E73\u6ED1\u52A8\u753B"), detailBlock);
    anim->setChecked(true);
    dLay->addWidget(fps);
    dLay->addWidget(glass);
    dLay->addWidget(anim);
    rightCol->addWidget(detailBlock);
    cols->addLayout(rightCol, 1);

    lay->addLayout(cols, 1);

    m_stack->addWidget(page);
}

/* ============================================================
 * 完成页
 * ============================================================ */
void OnboardingWizard::buildDonePage()
{
    QWidget *page = new QWidget(m_stack);
    page->setObjectName(QStringLiteral("obwPageDone"));
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(40, 32, 40, 20);
    lay->setSpacing(4);
    lay->addStretch();

    QLabel *icon = new QLabel(QStringLiteral("\u2714"), page);
    icon->setObjectName(QStringLiteral("obwDoneIcon"));
    icon->setFixedSize(96, 96);
    icon->setAlignment(Qt::AlignCenter);
    lay->addWidget(icon, 0, Qt::AlignHCenter);

    QLabel *doneTitle = new QLabel(tr("\u4E00\u5207\u5C31\u7EEA\uFF01"), page);
    doneTitle->setObjectName(QStringLiteral("obwDoneTitle"));
    doneTitle->setAlignment(Qt::AlignCenter);
    lay->addWidget(doneTitle);

    QLabel *doneDesc = new QLabel(
        tr("\u65B9\u5757\u76D2\u5B50\u5DF2\u5B8C\u6210\u57FA\u7840\u914D\u7F6E\uFF0C\u53EF\u4EE5\u5F00\u59CB\u5B89\u88C5\u4E0E\u73A9\u4F60\u7684\u7B2C\u4E00\u4E2A Minecraft \u5B9E\u4F8B\u4E86\u3002"), page);
    doneDesc->setObjectName(QStringLiteral("obwDoneDesc"));
    doneDesc->setAlignment(Qt::AlignCenter);
    doneDesc->setWordWrap(true);
    lay->addWidget(doneDesc);

    m_doneSummaryWidget = new QWidget(page);
    m_doneSummaryWidget->setObjectName(QStringLiteral("obwDoneSummary"));
    QHBoxLayout *sumLay = new QHBoxLayout(m_doneSummaryWidget);
    sumLay->setContentsMargins(0, 14, 0, 0);
    sumLay->setSpacing(8);
    sumLay->addStretch();
    lay->addWidget(m_doneSummaryWidget);
    lay->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    actions->setSpacing(12);
    actions->addStretch();
    QPushButton *backBtn = new QPushButton(tr("\u8FD4\u56DE\u8BBE\u7F6E"), page);
    backBtn->setObjectName(QStringLiteral("obwBtnSecondary"));
    backBtn->setCursor(Qt::PointingHandCursor);
    QPushButton *startBtn = new QPushButton(tr("\u5F00\u59CB\u4F7F\u7528\u65B9\u5757\u76D2\u5B50"), page);
    startBtn->setObjectName(QStringLiteral("obwBtnPrimary"));
    startBtn->setCursor(Qt::PointingHandCursor);
    connect(backBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(startBtn, &QPushButton::clicked, this, &QDialog::accept);
    actions->addWidget(backBtn);
    actions->addWidget(startBtn);
    actions->addStretch();
    lay->addLayout(actions);

    m_stack->addWidget(page);
}

/* ============================================================
 * 步骤导航与状态
 * ============================================================ */
int OnboardingWizard::stackIndexForStep(int logicalStep) const
{
    // 语言恒为 stack 0
    if (logicalStep == 0)
        return 0;
    // 下载源步骤仅在选择繁體中文时出现，位于 stack 1
    if (m_hasDlSourceStep && logicalStep == 1)
        return 1;

    // 引导方式（繁體中文时逻辑步骤为 2，否则为 1）
    const int guide = m_hasDlSourceStep ? 2 : 1;
    if (logicalStep == guide)
        return 2;   // stack 2 引导

    if (m_method == MethodImport) {
        // 导入 → 样式 → 完成
        if (logicalStep == guide + 1)
            return 3;   // stack 3 导入
        if (logicalStep == guide + 2)
            return 7;   // stack 7 样式
        if (logicalStep == guide + 3)
            return 8;   // stack 8 完成
    } else {
        // 账户 → Java → 文件夹 → 样式 → 完成
        if (logicalStep == guide + 1)
            return 4;   // stack 4 账户
        if (logicalStep == guide + 2)
            return 5;   // stack 5 Java
        if (logicalStep == guide + 3)
            return 6;   // stack 6 文件夹
        if (logicalStep == guide + 4)
            return 7;   // stack 7 样式
        if (logicalStep == guide + 5)
            return 8;   // stack 8 完成
    }
    return 0;
}

int OnboardingWizard::doneStep() const
{
    // 选择繁體中文时多一步「下载源选择」，各流程完成步骤 +1
    if (m_method == MethodImport)
        return m_hasDlSourceStep ? 5 : 4;
    return m_hasDlSourceStep ? 7 : 6;
}

int OnboardingWizard::stepperProgress() const
{
    // 完成页（导入=4 / 手动=6）时全部可见步骤标记完成
    return qMin(m_currentStep, doneStep());
}

void OnboardingWizard::showStep(int logicalStep)
{
    if (logicalStep < 0 || logicalStep > doneStep())
        return;
    m_currentStep = logicalStep;
    m_stack->setCurrentIndex(stackIndexForStep(logicalStep));
    syncStepper();
    updateFooterState();
}

void OnboardingWizard::syncStepper()
{
    const bool isImport = (m_method == MethodImport);
    const int progress = stepperProgress();

    // 账户/导入节点（node 3）标签按引导方式动态显示：手动 → 账户管理；导入 → 导入启动器
    if (m_stepLabels.size() >= 4) {
        m_stepLabels.at(3)->setText(isImport
                                        ? tr("\u5BFC\u5165\u542F\u52A8\u5668")
                                        : tr("\u8D26\u6237\u7BA1\u7406"));
    }

    // 可见节点：0 语言 / 1 下载源（仅繁體中文）/ 2 引导方式 / 3 账户·导入 / 4 Java / 5 文件夹 / 6 样式
    // 导入方式隐藏 Java(4) 与文件夹(5)，非繁体隐藏下载源(1)
    QList<int> order;
    if (isImport) {
        order = m_hasDlSourceStep ? (QList<int>{0, 1, 2, 3, 6})
                                  : (QList<int>{0, 2, 3, 6});
    } else {
        order = m_hasDlSourceStep ? (QList<int>{0, 1, 2, 3, 4, 5, 6})
                                  : (QList<int>{0, 2, 3, 4, 5, 6});
    }
    QSet<int> visible;
    for (int idx : order)
        visible.insert(idx);

    for (int i = 0; i < m_stepNodes.size(); ++i)
        m_stepNodes.at(i)->setVisible(visible.contains(i));

    // 连接线：相邻可见节点之间显示"起点节点之后"的那条线段
    // （导入方式跳过 Java/文件夹时，导入→样式之间仍保留一条连线，对齐 HTML 原型）
    QSet<int> visibleLines;
    for (int k = 0; k + 1 < order.size(); ++k) {
        const int from = order.at(k);
        if (from < m_stepLines.size())
            visibleLines.insert(from);
    }
    for (int i = 0; i < m_stepLines.size(); ++i)
        m_stepLines.at(i)->setVisible(visibleLines.contains(i));

    // 可见节点重排编号并按顺序标记状态
    int n = 0;
    for (int idx : order) {
        QLabel *dot = m_stepDots.at(idx);
        QLabel *label = m_stepLabels.at(idx);
        const bool done = progress > n;
        const bool active = progress == n;
        dot->setText(QString::number(n + 1));
        dot->setProperty("state", done ? QStringLiteral("done")
                                       : (active ? QStringLiteral("active") : QStringLiteral("todo")));
        label->setProperty("state", done || active ? QStringLiteral("on") : QStringLiteral("off"));
        dot->style()->unpolish(dot);
        dot->style()->polish(dot);
        label->style()->unpolish(label);
        label->style()->polish(label);
        ++n;
    }
    // 可见节点之间的连接线
    n = 0;
    for (int i = 0; i < m_stepLines.size(); ++i) {
        if (!visibleLines.contains(i))
            continue;
        QFrame *line = m_stepLines.at(i);
        line->setProperty("state", progress > n ? QStringLiteral("done") : QStringLiteral("todo"));
        line->style()->unpolish(line);
        line->style()->polish(line);
        ++n;
    }
}

QLabel *OnboardingWizard::makeSectionTitle(const QString &text)
{
    QLabel *label = new QLabel(text, m_stack);
    label->setObjectName(QStringLiteral("obwSectionTitle"));
    return label;
}

void OnboardingWizard::updateFooterState()
{
    if (m_currentStep >= doneStep()) {
        m_prevBtn->setVisible(false);
        m_nextBtn->setVisible(false);
        return;
    }
    m_prevBtn->setVisible(m_currentStep != 0);
    m_nextBtn->setVisible(true);
    // 最后一个配置步骤（完成页前一步）：手动为样式，导入为样式（跳过 Java/文件夹）
    const int lastConfig = doneStep() - 1;
    if (m_currentStep == 0) {
        m_nextBtn->setText(tr("\u4E0B\u4E00\u6B65"));
        m_footerHint->setText(tr("\u9009\u62E9\u8BED\u8A00\u540E\u5F00\u59CB\u5F15\u5BFC"));
    } else if (m_currentStep == lastConfig) {
        m_nextBtn->setText(tr("\u5B8C\u6210"));
        m_footerHint->setText(tr("\u5B8C\u6210\u540E\u5373\u53EF\u8FDB\u5165\u65B9\u5757\u76D2\u5B50"));
    } else {
        m_nextBtn->setText(tr("\u4E0B\u4E00\u6B65"));
        m_footerHint->setText(tr("\u70B9\u51FB\u300C\u4E0B\u4E00\u6B65\u300D\u7EE7\u7EED"));
    }
}

void OnboardingWizard::onPickLanguage(int index)
{
    if (index < 0 || index >= m_languageCards.size())
        return;
    const LanguageManager::Language selected = static_cast<LanguageManager::Language>(index);
    // 非中文（English / Español）用户默认使用官方下载源；
    // 繁體中文由新增的「下载源选择」步骤由用户自行决定。
    if (selected == LanguageManager::English || selected == LanguageManager::Spanish)
        setAllDownloadSourcesOfficial();
    // 与当前已加载语言相同则无需重启，留在向导内继续
    if (selected == LanguageManager::instance()->currentLanguage()) {
        m_footerHint->setText(tr("\u8BED\u8A00\u672A\u53D8\u66F4\uFF0C\u70B9\u51FB\u300C\u4E0B\u4E00\u6B65\u300D\u7EE7\u7EED\u3002"));
        return;
    }
    requestLanguageRestart(index);
}

void OnboardingWizard::setAllDownloadSourcesOfficial()
{
    SettingsManager *sm = SettingsManager::instance();
    sm->setDownloadSource(DownloadSource::Official);
    sm->setForgeDownloadSource(ForgeDownloadSource::Official);
    sm->setFabricDownloadSource(FabricDownloadSource::Official);
    sm->setOptiFineDownloadSource(OptiFineDownloadSource::Official);
    sm->setNeoForgeDownloadSource(NeoForgeDownloadSource::Official);
}

void OnboardingWizard::onPickDownloadSource(int index)
{
    const bool official = (index == 0);
    SettingsManager *sm = SettingsManager::instance();
    sm->setDownloadSource(official ? DownloadSource::Official : DownloadSource::BMCL);
    sm->setForgeDownloadSource(official ? ForgeDownloadSource::Official : ForgeDownloadSource::BMCL);
    sm->setFabricDownloadSource(official ? FabricDownloadSource::Official : FabricDownloadSource::BMCL);
    sm->setOptiFineDownloadSource(official ? OptiFineDownloadSource::Official : OptiFineDownloadSource::BMCL);
    sm->setNeoForgeDownloadSource(official ? NeoForgeDownloadSource::Official : NeoForgeDownloadSource::BMCL);
    m_footerHint->setText(official
                              ? tr("\u5DF2\u9009\u62E9\u5B98\u65B9\u6E90\u3002")
                              : tr("\u5DF2\u9009\u62E9 BMCL \u955C\u50CF\u6E90\u3002"));
}

void OnboardingWizard::requestLanguageRestart(int langIndex)
{
    // 仅持久化偏好（不调用 setLanguage，避免在已构建的向导上触发
    // LanguageChange 实时重译导致崩溃）；由主程序重启并在下次启动 loadLanguageSync() 应用。
    LanguageManager::instance()->setLanguagePreference(
        static_cast<LanguageManager::Language>(langIndex));
    m_restartRequested = true;
    m_footerHint->setText(tr("\u6B63\u5728\u91CD\u542F\u4EE5\u5E94\u7528\u8BED\u8A00\u2026"));
    // reject() 让 exec() 返回；主程序检测 restartRequested() 后启动新进程并退出当前进程。
    // 注意：此处不经过 goDone()，故不打 onboarding/completed 标记，下次启动将再次弹出引导以继续。
    QDialog::reject();
}

void OnboardingWizard::onMethodPicked(int method)
{
    m_method = method;
    if (method == MethodSkip) {
        goDone();
        return;
    }
    syncStepper(); // 立即刷新步骤条第 2 步标签（账户管理 / 导入启动器）
    updateFooterState();
}

void OnboardingWizard::resizeEvent(QResizeEvent *event)
{
    AppDialogBase::resizeEvent(event);
    // 将 Step1 卡片右上角对勾徽章钉在卡片右上角内侧
    for (int i = 0; i < 3; ++i) {
        if (m_methodCards[i] && i < m_methodChecks.size()) {
            QLabel *check = m_methodChecks.at(i);
            check->move(m_methodCards[i]->width() - check->width() - 12, 12);
        }
    }
}

void OnboardingWizard::onNextClicked()
{
    if (m_currentStep >= doneStep())
        return;
    // 完成页前一步点击「完成」/「开始使用」，其余按流程线性前进
    if (m_currentStep == doneStep() - 1) {
        goDone();
        return;
    }
    showStep(m_currentStep + 1);
}

void OnboardingWizard::onPrevClicked()
{
    if (m_currentStep <= 0)
        return;
    // 引导方式的逻辑步骤（繁體中文多一步下载源）
    const int guideStep = m_hasDlSourceStep ? 2 : 1;
    if (m_currentStep == 1) {
        showStep(0);  // 下载源 / 引导方式 → 语言
        return;
    }
    // 手动流程：账户 / Java → 引导方式（便于重新选择引导方式）
    if (m_method == MethodManual
        && (m_currentStep == guideStep + 1 || m_currentStep == guideStep + 2)) {
        showStep(guideStep);
        return;
    }
    showStep(m_currentStep - 1);
}

void OnboardingWizard::onSkipClicked()
{
    goDone();
}

void OnboardingWizard::goDone()
{
    m_currentStep = doneStep();
    m_stack->setCurrentIndex(stackIndexForStep(m_currentStep));
    syncStepper();
    updateFooterState();

    // 到达完成页即标记引导完成（完成页上的"开始使用"/"返回设置"都不会再让引导重复）。
    // 语言切换走 requestLanguageRestart()->reject()，不经过这里，故不会标记完成，重启后继续引导。
    SettingsManager::instance()->setProperty("onboarding/completed", true);

    // 完成页汇总 chips
    if (m_doneSummaryWidget) {
        QLayout *oldLay = m_doneSummaryWidget->layout();
        if (oldLay) {
            while (QLayoutItem *item = oldLay->takeAt(0)) {
                if (QWidget *w = item->widget())
                    w->deleteLater();
                delete item;
            }
            delete oldLay;
        }
        QHBoxLayout *sum = new QHBoxLayout(m_doneSummaryWidget);
        sum->setContentsMargins(0, 14, 0, 0);
        sum->setSpacing(8);
        sum->addStretch();

        QStringList chips;
        if (m_method == MethodImport) {
            chips << (m_imported.isEmpty() ? tr("\u5BFC\u5165\uFF1A\u5DF2\u8DF3\u8FC7\u5BFC\u5165")
                                            : tr("\u5BFC\u5165\uFF1A%1 \u4E2A\u542F\u52A8\u5668").arg(m_imported.size()));
        } else if (m_method == MethodManual) {
            const int n = SettingsManager::instance()->getAccounts().size();
            chips << tr("\u8D26\u6237\uFF1A%1 \u4E2A").arg(n);
        }
        const int javaN = SettingsManager::instance()->getJavaInstallations().size();
        chips << tr("Java\uFF1A%1 \u4E2A\u8FD0\u884C\u65F6").arg(javaN);
        const int folderN = SettingsManager::instance()->getInstanceFolders().size();
        chips << tr("\u5B9E\u4F8B\u6587\u4EF6\u5939\uFF1A%1 \u4E2A").arg(folderN);
        chips << (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme
                      ? tr("\u5916\u89C2\uFF1A\u6DF1\u8272\u4E3B\u9898")
                      : tr("\u5916\u89C2\uFF1A\u6D45\u8272\u4E3B\u9898"));

        for (const QString &chip : chips) {
            QLabel *c = new QLabel(QStringLiteral("\u2714 ") + chip, m_doneSummaryWidget);
            c->setObjectName(QStringLiteral("obwDoneChip"));
            sum->addWidget(c);
        }
        sum->addStretch();
    }
    m_prevBtn->setVisible(false);
    m_nextBtn->setVisible(false);
    m_footerHint->setText(tr("\u5DF2\u5B8C\u6210\u5168\u90E8\u5F15\u5BFC"));
}

/* ============================================================
 * Step2 导入启动器
 * ============================================================ */
void OnboardingWizard::onLauncherScan(const QString &key)
{
    const int idx = m_launcherKeys.indexOf(key);
    if (idx < 0)
        return;
    m_launcherStatusText[idx]->setText(tr("\u6B63\u5728\u626B\u63CF\u2026"));
    m_launcherStatus[idx]->setProperty("state", QStringLiteral("scanning"));
    m_launcherStatus[idx]->style()->unpolish(m_launcherStatus[idx]);
    m_launcherStatus[idx]->style()->polish(m_launcherStatus[idx]);

    // 真实检测：读取数据目录
    QTimer::singleShot(150, this, [this, idx, key]() {
        const LauncherDetect det = LauncherImporter::detectOne(key);
        const bool found = det.detected;
        if (found) {
            m_launcherStatus[idx]->setProperty("state", QStringLiteral("found"));
            m_launcherStatusText[idx]->setText(tr("\u5DF2\u68C0\u6D4B\u5230 \u00B7 ")
                                               + (det.detail.isEmpty() ? det.dataDir : det.detail));
        } else {
            m_launcherStatus[idx]->setProperty("state", QStringLiteral("missing"));
            m_launcherStatusText[idx]->setText(tr("\u672A\u68C0\u6D4B\u5230\u5B89\u88C5"));
        }
        m_launcherStatusText[idx]->setProperty("state", found ? QStringLiteral("found")
                                                              : QStringLiteral("missing"));
        m_launcherStatus[idx]->style()->unpolish(m_launcherStatus[idx]);
        m_launcherStatus[idx]->style()->polish(m_launcherStatus[idx]);
        m_launcherStatusText[idx]->style()->unpolish(m_launcherStatusText[idx]);
        m_launcherStatusText[idx]->style()->polish(m_launcherStatusText[idx]);
    });
}

void OnboardingWizard::onLauncherImport(const QString &key)
{
    if (m_imported.contains(key))
        return;
    const int idx = m_launcherKeys.indexOf(key);
    if (idx < 0)
        return;

    // 真实导入：账户 + 版本目录 + Java
    const int addedAccounts = importAccountsFor(key).size();
    const LauncherImportResult r = LauncherImporter::importFrom(key);
    SettingsManager *sm = SettingsManager::instance();

    // 注册实例文件夹
    if (key == QStringLiteral("multimc")) {
        // MultiMC：每个 instance 的 .minecraft 单独注册
        for (const QString &instDir : r.instanceDirs) {
            QString instName = instDir;
            instName = instName.section(QStringLiteral("/instances/"), 1, 1)
                               .section(QLatin1Char('/'), 0, 0);
            if (instName.isEmpty())
                instName = tr("MultiMC \u5B9E\u4F8B");
            sm->addInstanceFolder(instName, instDir, false);
        }
    } else if (!r.minecraftDir.isEmpty()) {
        const QString name = tr("%1 \u5BFC\u5165\u7684\u5B9E\u4F8B\u5E93").arg(launcherDisplayName(key));
        sm->addInstanceFolder(name, r.minecraftDir, false);
    }

    // 导入启动器配置的 Java 路径（合并去重）
    if (!r.javaPath.isEmpty())
        importJavaPath(r.javaPath);

    // 导入启动器配置的内存（仅当启动器明确设置了最大内存时）
    if (r.javaMaxMemoryMb > 0)
        sm->setMaxMemory(r.javaMaxMemoryMb);
    if (r.javaMinMemoryMb > 0)
        sm->setMinMemory(r.javaMinMemoryMb);

    m_imported.append(key);
    if (m_launcherActions[idx]) {
        QLayout *oldLay = m_launcherActions[idx]->layout();
        if (oldLay) {
            while (QLayoutItem *item = oldLay->takeAt(0)) {
                if (QWidget *w = item->widget())
                    w->deleteLater();
                delete item;
            }
            delete oldLay;
        }
        QHBoxLayout *aLay = new QHBoxLayout(m_launcherActions[idx]);
        aLay->setContentsMargins(0, 4, 0, 0);
        QLabel *done = new QLabel(tr("\u2714 \u5DF2\u5BFC\u5165"), m_launcherActions[idx]);
        done->setObjectName(QStringLiteral("obwImportedBadge"));
        done->setAlignment(Qt::AlignCenter);
        aLay->addWidget(done);
    }

    // footer 汇总：账户 / 版本（含名称）/ Java
    QStringList parts;
    if (addedAccounts > 0)
        parts << tr("%1 \u4E2A\u8D26\u6237").arg(addedAccounts);
    if (r.versionCount > 0) {
        QStringList shown = r.versionNames.mid(0, 3);
        if (shown.isEmpty() && r.versionCount > 0)
            shown << tr("%1 \u4E2A\u7248\u672C").arg(r.versionCount);
        const QString verText = shown.join(QStringLiteral(", "));
        if (r.versionCount > 3)
            parts << tr("%1 \u4E2A\u7248\u672C\uFF08%2 \u7B49\uFF09").arg(r.versionCount).arg(verText);
        else
            parts << tr("%1 \u4E2A\u7248\u672C\uFF08%2\uFF09").arg(r.versionCount).arg(verText);
    }
    if (!r.javaPath.isEmpty())
        parts << tr("Java \u5DF2\u5BFC\u5165");
    if (r.javaMaxMemoryMb > 0)
        parts << tr("\u5185\u5B58 %1 MB").arg(r.javaMaxMemoryMb);
    m_footerHint->setText(parts.isEmpty()
                              ? tr("\u672A\u627E\u5230\u53EF\u5BFC\u5165\u7684\u8D26\u6237\u6216\u7248\u672C")
                              : tr("\u5DF2\u4ECE\u542F\u52A8\u5668\u5BFC\u5165\uFF1A") + parts.join(QStringLiteral(" \u00B7 ")));
}

void OnboardingWizard::importJavaPath(const QString &javaExe)
{
    SettingsManager *sm = SettingsManager::instance();
    const QList<QPair<QString, QString>> existing = sm->getJavaInstallations();
    for (const auto &inst : existing) {
        if (inst.first.compare(javaExe, Qt::CaseInsensitive) == 0)
            return; // 已存在
    }
    // 版本标签：取父目录名（如 jdk-17.0.2 → jdk-17.0.2），保留原名即可
    QList<QPair<QString, QString>> updated = existing;
    updated.append(qMakePair(javaExe, QFileInfo(javaExe).absoluteDir().dirName()));
    sm->setJavaInstallations(updated);
}

QList<AccountInfo> OnboardingWizard::importAccountsFor(const QString &key)
{
    // 通过 LauncherImporter 解析账户并写入 SettingsManager（同名自动去重）
    const QList<AccountInfo> imported = LauncherImporter::importAccounts(key);
    int added = 0;
    for (const AccountInfo &acc : imported) {
        const bool isFirst = (added == 0);
        SettingsManager::instance()->addAccount(acc.username, acc.type, acc.serverUrl, isFirst);
        ++added;
    }
    return imported;
}

QString OnboardingWizard::launcherDisplayName(const QString &key) const
{
    if (key == QStringLiteral("pcl"))
        return QStringLiteral("PCL");
    if (key == QStringLiteral("hmcl"))
        return QStringLiteral("HMCL");
    if (key == QStringLiteral("baka"))
        return QStringLiteral("BakaXL");
    return key;
}

/* ============================================================
 * Step2 账户管理
 * ============================================================ */
void OnboardingWizard::refreshAccountList()
{
    if (!m_accountListWidget)
        return;
    QLayout *oldLay = m_accountListWidget->layout();
    if (oldLay) {
        while (QLayoutItem *item = oldLay->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
        delete oldLay;
    }
    QVBoxLayout *listLay = new QVBoxLayout(m_accountListWidget);
    listLay->setContentsMargins(0, 8, 0, 0);
    listLay->setSpacing(8);

    const QList<AccountInfo> accounts = SettingsManager::instance()->getAccounts();
    if (accounts.isEmpty()) {
        QLabel *empty = new QLabel(tr("\u5C1A\u65E0\u8D26\u6237\uFF0C\u70B9\u51FB\u300C\u6DFB\u52A0\u8D26\u6237\u300D\u5F00\u59CB"), m_accountListWidget);
        empty->setObjectName(QStringLiteral("obwEmptyText"));
        empty->setAlignment(Qt::AlignCenter);
        listLay->addWidget(empty);
        return;
    }
    for (const AccountInfo &acc : accounts) {
        const bool isMs = (acc.type == QStringLiteral("microsoft"));
        addAccountRow(acc.username,
                      isMs ? tr("\u5FAE\u8F6F") : tr("\u79BB\u7EBF"),
                      isMs ? QStringLiteral("microsoft") : QStringLiteral("offline"),
                      isMs ? tr("Xbox \u6B63\u7248\u8D26\u6237 \u00B7 \u76AE\u80A4\u540C\u6B65")
                           : tr("\u79BB\u7EBF\u8D26\u6237 \u00B7 \u672C\u5730\u4F7F\u7528"),
                      acc.isDefault);
    }
}

void OnboardingWizard::addAccountRow(const QString &name, const QString &typeBadge,
                                     const QString &typeClass, const QString &meta,
                                     bool isDefault)
{
    if (!m_accountListWidget)
        return;
    // 用 QWidget 容器 + setAutoFillBackground + setStyleSheet 强制背景/边框/圆角
    QWidget *row = new QWidget(m_accountListWidget);
    row->setObjectName(QStringLiteral("obwAccountItem"));
    row->setMinimumHeight(64);
    row->setMaximumHeight(64);
    {
        ThemeManager *tm = ThemeManager::instance();
        const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
        const QString bg = isLight ? QStringLiteral("#ffffff") : QStringLiteral("#2d2d2d");
        const QString border = isLight ? QStringLiteral("#e8eaed") : QStringLiteral("#3a3a3a");
        row->setAutoFillBackground(true);
        row->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: 12px;").arg(bg, border));
    }

    QHBoxLayout *lay = new QHBoxLayout(row);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(12);

    QLabel *avatar = new QLabel(name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper(), row);
    avatar->setObjectName(QStringLiteral("obwAccountAvatar"));
    avatar->setFixedSize(38, 38);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(
        typeClass == QStringLiteral("microsoft")
            ? QStringLiteral("background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #60A5FA, stop:1 #2563EB); border-radius: 8px; color: #FFFFFF; font-weight: bold;")
            : QStringLiteral("background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #34D399, stop:1 #059669); border-radius: 8px; color: #FFFFFF; font-weight: bold;"));
    lay->addWidget(avatar);

    // info 容器（用 QWidget 避免跨级 layout 嵌套的 parent 歧义）
    QWidget *infoContainer = new QWidget(row);
    QVBoxLayout *infoLay = new QVBoxLayout(infoContainer);
    infoLay->setContentsMargins(0, 0, 0, 0);
    infoLay->setSpacing(2);

    // 名称行：用户名 + 徽章（同 HBox）
    QWidget *nameRow = new QWidget(infoContainer);
    QHBoxLayout *nameBox = new QHBoxLayout(nameRow);
    nameBox->setContentsMargins(0, 0, 0, 0);
    nameBox->setSpacing(8);
    QLabel *n = new QLabel(name, nameRow);
    n->setObjectName(QStringLiteral("obwAccountName"));
    nameBox->addWidget(n);
    QLabel *badge = new QLabel(typeBadge, nameRow);
    badge->setObjectName(isDefault ? QStringLiteral("obwBadgeDefault")
                                   : (typeClass == QStringLiteral("microsoft")
                                          ? QStringLiteral("obwBadgeMs")
                                          : QStringLiteral("obwBadgeOff")));
    nameBox->addWidget(badge);
    nameBox->addStretch();
    infoLay->addWidget(nameRow);

    QLabel *m = new QLabel(meta, infoContainer);
    m->setObjectName(QStringLiteral("obwAccountMeta"));
    infoLay->addWidget(m);

    lay->addWidget(infoContainer, 1);

    if (!isDefault) {
        QPushButton *setDefault = new QPushButton(tr("\u8BBE\u4E3A\u9ED8\u8BA4"), row);
        setDefault->setObjectName(QStringLiteral("obwBtnSecondary"));
        setDefault->setCursor(Qt::PointingHandCursor);
        connect(setDefault, &QPushButton::clicked, this, [this, name]() {
            SettingsManager::instance()->setDefaultAccount(name);
            refreshAccountList();
        });
        lay->addWidget(setDefault);
    }
    m_accountListWidget->layout()->addWidget(row);
}

void OnboardingWizard::onToggleAddAccount()
{
    if (!m_addAccountPanel)
        return;
    m_addAccountPanel->setVisible(!m_addAccountPanel->isVisible());
}

void OnboardingWizard::onSwitchAccountTab(int index)
{
    if (!m_formMicrosoft || !m_formOffline || !m_formLegacy)
        return;
    m_formMicrosoft->setVisible(index == 0);
    m_formOffline->setVisible(index == 1);
    m_formLegacy->setVisible(index == 2);
}

void OnboardingWizard::onAddOfflineAccount()
{
    if (!m_offlineNameInput)
        return;
    const QString name = m_offlineNameInput->text().trimmed();
    if (name.isEmpty()) {
        m_footerHint->setText(tr("\u8BF7\u8F93\u5165\u6E38\u620F\u6635\u79F0\u3002"));
        return;
    }
    SettingsManager::instance()->addAccount(name, QStringLiteral("offline"), QString());
    m_offlineNameInput->clear();
    m_addAccountPanel->setVisible(false);
    refreshAccountList();
    m_footerHint->setText(tr("\u5DF2\u6DFB\u52A0\u79BB\u7EBF\u8D26\u6237\u300C%1\u300D").arg(name));
}

/* ============================================================
 * Step3 Java
 * ============================================================ */
void OnboardingWizard::refreshJavaList()
{
    if (!m_javaListWidget)
        return;
    QLayout *oldLay = m_javaListWidget->layout();
    if (oldLay) {
        while (QLayoutItem *item = oldLay->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
        delete oldLay;
    }
    QVBoxLayout *listLay = new QVBoxLayout(m_javaListWidget);
    listLay->setContentsMargins(0, 0, 0, 0);
    listLay->setSpacing(8);
    // m_javaListWidget 是被 QScrollArea 包裹的，scroll 内容按 listWidget 实际高度展开

    const QList<QPair<QString, QString>> javas = SettingsManager::instance()->getJavaInstallations();
    if (javas.isEmpty()) {
        QLabel *empty = new QLabel(
            tr("\u5C1A\u672A\u53D1\u73B0 Java\uFF0C\u70B9\u51FB\u300C\u626B\u63CF\u672C\u673A Java\u300D\u6216\u4E0B\u65B9\u4E0B\u8F7D\u3002"), m_javaListWidget);
        empty->setObjectName(QStringLiteral("obwEmptyText"));
        empty->setAlignment(Qt::AlignCenter);
        listLay->addWidget(empty);
        return;
    }
    for (const auto &java : javas) {
        QWidget *row = new QWidget(m_javaListWidget);
        row->setObjectName(QStringLiteral("obwJavaItem"));
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->setFixedHeight(64);
        row->setAutoFillBackground(true);
        {
            ThemeManager *tm = ThemeManager::instance();
            const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
            const QString bg = isLight ? QStringLiteral("#ffffff") : QStringLiteral("#2d2d2d");
            const QString border = isLight ? QStringLiteral("#e8eaed") : QStringLiteral("#3a3a3a");
            row->setAutoFillBackground(true);
        row->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: 12px;").arg(bg, border));
        }
        QHBoxLayout *lay = new QHBoxLayout(row);
        lay->setContentsMargins(16, 12, 16, 12);
        lay->setSpacing(12);

        QLabel *icon = new QLabel(QStringLiteral("\u2615"), row);
        icon->setObjectName(QStringLiteral("obwJavaIcon"));
        icon->setFixedSize(38, 38);
        icon->setAlignment(Qt::AlignCenter);
        lay->addWidget(icon);

        QVBoxLayout *info = new QVBoxLayout();
        info->setSpacing(2);
        QLabel *ver = new QLabel(java.second, row);
        ver->setObjectName(QStringLiteral("obwJavaVer"));
        info->addWidget(ver);
        QLabel *path = new QLabel(java.first, row);
        path->setObjectName(QStringLiteral("obwJavaPath"));
        info->addWidget(path);
        lay->addLayout(info, 1);

        QPushButton *locate = new QPushButton(tr("\u5B9A\u4F4D"), row);
        locate->setObjectName(QStringLiteral("obwBtnSecondary"));
        locate->setCursor(Qt::PointingHandCursor);
        connect(locate, &QPushButton::clicked, this, [java]() {
            QProcess::startDetached(QStringLiteral("explorer.exe"),
                                    QStringList()
                                        << QStringLiteral("/select,")
                                        << QDir::toNativeSeparators(java.first));
        });
        lay->addWidget(locate);
        listLay->addWidget(row);
    }
}

QList<QPair<QString, QString>> OnboardingWizard::lightJavaDetect() const
{
    QList<QPair<QString, QString>> results;
    const QStringList roots = {
        QStringLiteral("C:/Program Files/Java"),
        QStringLiteral("C:/Program Files/Eclipse Adoptium"),
        QStringLiteral("C:/Program Files/Microsoft"),
        QStringLiteral("C:/Program Files/Zulu"),
        QStringLiteral("D:/Java"),
    };
    QStringList subDirs;
    for (const QString &root : roots) {
        const QDir d(root);
        if (d.exists())
            subDirs << d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    }
    for (const QString &sub : subDirs) {
        const QStringList cands = {
            QStringLiteral("C:/Program Files/Java/%1/bin/java.exe").arg(sub),
            QStringLiteral("C:/Program Files/Eclipse Adoptium/%1/bin/java.exe").arg(sub),
            QStringLiteral("C:/Program Files/Microsoft/%1/bin/java.exe").arg(sub),
            QStringLiteral("C:/Program Files/Zulu/%1/bin/java.exe").arg(sub),
            QStringLiteral("D:/Java/%1/bin/java.exe").arg(sub),
        };
        for (const QString &cand : cands) {
            if (QFileInfo::exists(cand)) {
                results.append(qMakePair(QDir::toNativeSeparators(cand),
                                         javaVersionFromPath(cand)));
                break;
            }
        }
    }
    return results;
}

QString OnboardingWizard::javaVersionFromPath(const QString &javaExe) const
{
    QProcess proc;
    proc.start(javaExe, QStringList() << QStringLiteral("-version"));
    if (!proc.waitForStarted(1500))
        return tr("Java \u672A\u77E5");
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        return tr("Java \u672A\u77E5");
    }
    const QString out = QString::fromUtf8(proc.readAllStandardError());
    QRegularExpression re(QStringLiteral("(\\d+)(?:\\.(\\d+))?"));
    QRegularExpressionMatch m = re.match(out);
    if (!m.hasMatch())
        return tr("Java \u672A\u77E5");
    const int major = m.captured(1).toInt();
    if (major == 1) {
        const int minor = m.captured(2).toInt(); // 1.8.0_xxx → Java 8
        return QStringLiteral("Java %1 (64-bit)").arg(minor);
    }
    return QStringLiteral("Java %1 (64-bit)").arg(major);
}

void OnboardingWizard::onScanJava()
{
    if (m_scanningJava)
        return;
    m_scanningJava = true;
    m_scanJavaBtn->setEnabled(false);
    m_scanJavaBtn->setText(tr("\u626B\u63CF\u4E2D\u2026"));
    m_scanJavaText->setText(tr("\u6B63\u5728\u626B\u63CF\u5E38\u89C1\u5B89\u88C5\u8DEF\u5F84\u2026"));

    QTimer::singleShot(600, this, [this]() {
        const auto found = lightJavaDetect();
        QList<QPair<QString, QString>> merged =
            SettingsManager::instance()->getJavaInstallations();
        QSet<QString> seen;
        for (const auto &e : merged)
            seen.insert(e.first);
        int added = 0;
        for (const auto &j : found) {
            if (!seen.contains(j.first)) {
                merged.append(j);
                seen.insert(j.first);
                ++added;
            }
        }
        SettingsManager::instance()->setJavaInstallations(merged);
        refreshJavaList();
        m_scanningJava = false;
        m_scanJavaBtn->setEnabled(true);
        m_scanJavaBtn->setText(tr("\u626B\u63CF\u672C\u673A Java"));
        m_scanJavaText->setText(tr("\u626B\u63CF\u5B8C\u6210 \u00B7 \u5171 %1 \u4E2A\u8FD0\u884C\u65F6").arg(merged.size()));
        m_footerHint->setText(added > 0
                                  ? tr("\u65B0\u53D1\u73B0 %1 \u4E2A Java\uFF0C\u5DF2\u52A0\u5165\u5217\u8868\u3002").arg(added)
                                  : tr("\u672A\u53D1\u73B0\u65B0\u7684 Java\uFF0C\u53EF\u4E0B\u65B9\u4E0B\u8F7D\u3002"));
    });
}

/* ============================================================
 * Step4 实例文件夹
 * ============================================================ */
void OnboardingWizard::refreshFolderList()
{
    if (!m_folderListWidget)
        return;
    QLayout *oldLay = m_folderListWidget->layout();
    if (oldLay) {
        while (QLayoutItem *item = oldLay->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
        delete oldLay;
    }
    QVBoxLayout *listLay = new QVBoxLayout(m_folderListWidget);
    listLay->setContentsMargins(0, 8, 0, 0);
    listLay->setSpacing(8);

    const QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    if (folders.isEmpty()) {
        QLabel *empty = new QLabel(
            tr("\u5C1A\u65E0\u5B9E\u4F8B\u6587\u4EF6\u5939\uFF0C\u70B9\u51FB\u300C\u6DFB\u52A0\u6587\u4EF6\u5939\u300D\u521B\u5EFA\u3002"), m_folderListWidget);
        empty->setObjectName(QStringLiteral("obwEmptyText"));
        empty->setAlignment(Qt::AlignCenter);
        listLay->addWidget(empty);
        return;
    }
    for (const InstanceFolderInfo &f : folders) {
        QWidget *row = new QWidget(m_folderListWidget);
        row->setObjectName(QStringLiteral("obwFolderItem"));
        row->setMinimumHeight(64);
        row->setMaximumHeight(64);
        row->setAutoFillBackground(true);
        if (f.isDefault)
            row->setProperty("state", QStringLiteral("default"));
        {
            ThemeManager *tm = ThemeManager::instance();
            const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
            const QString bg = isLight ? QStringLiteral("#ffffff") : QStringLiteral("#2d2d2d");
            const QString border = isLight ? QStringLiteral("#e8eaed") : QStringLiteral("#3a3a3a");
            row->setAutoFillBackground(true);
        row->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: 12px;").arg(bg, border));
        }
        QHBoxLayout *lay = new QHBoxLayout(row);
        lay->setContentsMargins(16, 14, 16, 14);
        lay->setSpacing(12);

        QLabel *icon = new QLabel(QStringLiteral("\u25A0"), row);
        icon->setObjectName(QStringLiteral("obwFolderIcon"));
        icon->setFixedSize(40, 40);
        icon->setAlignment(Qt::AlignCenter);
        lay->addWidget(icon);

        QVBoxLayout *info = new QVBoxLayout();
        info->setSpacing(2);
        QHBoxLayout *nameLay = new QHBoxLayout();
        nameLay->setSpacing(8);
        QLabel *n = new QLabel(f.name, row);
        n->setObjectName(QStringLiteral("obwFolderName"));
        nameLay->addWidget(n);
        if (f.isDefault) {
            QLabel *badge = new QLabel(tr("\u9ED8\u8BA4"), row);
            badge->setObjectName(QStringLiteral("obwBadgeDefault"));
            nameLay->addWidget(badge);
        }
        nameLay->addStretch();
        info->addLayout(nameLay);
        QLabel *path = new QLabel(f.path, row);
        path->setObjectName(QStringLiteral("obwFolderPath"));
        info->addWidget(path);
        lay->addLayout(info, 1);

        if (!f.isDefault) {
            QPushButton *setDefault = new QPushButton(tr("\u8BBE\u4E3A\u9ED8\u8BA4"), row);
            setDefault->setObjectName(QStringLiteral("obwBtnSecondary"));
            setDefault->setCursor(Qt::PointingHandCursor);
            connect(setDefault, &QPushButton::clicked, this, [this, path = f.path]() {
                SettingsManager::instance()->setDefaultInstanceFolder(path);
                refreshFolderList();
            });
            lay->addWidget(setDefault);
        }
        listLay->addWidget(row);
    }
}

void OnboardingWizard::onToggleAddFolder()
{
    if (!m_addFolderPanel)
        return;
    m_addFolderPanel->setVisible(!m_addFolderPanel->isVisible());
}

void OnboardingWizard::onAddFolder()
{
    if (!m_newFolderName || !m_newFolderPath)
        return;
    const QString name = m_newFolderName->text().trimmed();
    const QString path = m_newFolderPath->text().trimmed();
    if (name.isEmpty() || path.isEmpty()) {
        m_footerHint->setText(tr("\u8BF7\u586B\u5199\u6587\u4EF6\u5939\u540D\u79F0\u4E0E\u8DEF\u5F84\u3002"));
        return;
    }
    SettingsManager::instance()->addInstanceFolder(name, path, false);
    m_newFolderName->clear();
    m_addFolderPanel->setVisible(false);
    refreshFolderList();
    m_footerHint->setText(tr("\u5DF2\u6DFB\u52A0\u5B9E\u4F8B\u6587\u4EF6\u5939\u300C%1\u300D").arg(name));
}

/* ============================================================
 * Step5 样式
 * ============================================================ */
void OnboardingWizard::onPickTheme(bool dark)
{
    ThemeManager *tm = ThemeManager::instance();
    tm->loadTheme(dark ? ThemeManager::DarkTheme : ThemeManager::LightTheme);
    m_footerHint->setText(dark ? tr("\u5DF2\u5207\u6362\u4E3A\u6DF1\u8272\u4E3B\u9898")
                                : tr("\u5DF2\u5207\u6362\u4E3A\u6D45\u8272\u4E3B\u9898"));
}

void OnboardingWizard::onPickBg(int index)
{
    BackgroundManager *bg = BackgroundManager::instance();
    switch (index) {
    case 0: // 经典：默认渐变背景
        bg->setMode(BackgroundManager::Classic);
        break;
    case 1: // 纯色：保持当前选择的纯色（未选时用主题色）
        if (bg->currentMode() != BackgroundManager::SolidColor) {
            QString cur = bg->solidColor();
            if (cur.isEmpty())
                cur = ThemeManager::instance()->currentThemeColor();
            bg->setSolidColor(cur);
        }
        bg->setMode(BackgroundManager::SolidColor);
        break;
    case 2: // 图片：保持当前图片路径
        bg->setMode(BackgroundManager::Image);
        if (m_bgSubStack)
            updateImagePreview(bg->imagePath());
        break;
    case 3: // 流光：动态流光背景
        bg->setMode(BackgroundManager::FlowLight);
        break;
    case 4: // 旋转：旋转全景背景
        bg->setMode(BackgroundManager::Rotating);
        if (m_bgSubStack)
            updateImagePreview(bg->imagePath());
        break;
    default:
        break;
    }
    bg->saveToSettings();
    const QStringList names = { tr("\u7ECF\u5178"), tr("\u7EAF\u8272"), tr("\u56FE\u7247"), tr("\u6D41\u5149"), tr("\u65CB\u8F6C") };
    m_footerHint->setText(tr("\u80CC\u666F\u5DF2\u5207\u6362\u4E3A\u300C%1\u300D").arg(names.at(index)));
}

void OnboardingWizard::onPickSolidColor(const QString &hex)
{
    BackgroundManager *bg = BackgroundManager::instance();
    bg->setMode(BackgroundManager::SolidColor);
    bg->setSolidColor(hex);
    bg->saveToSettings();
    m_footerHint->setText(tr("\u7EAF\u8272\u80CC\u666F\u5DF2\u5E94\u7528\uFF1A%1").arg(hex));
}

void OnboardingWizard::onBrowseImage()
{
    const QString path = AppFileDialog::getOpenFileName(
        this, tr("\u9009\u62E9\u80CC\u666F\u56FE\u7247"), QDir::homePath(),
        tr("\u56FE\u7247\u6587\u4EF6 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (path.isEmpty())
        return;
    BackgroundManager *bg = BackgroundManager::instance();
    bg->setMode(BackgroundManager::Image);
    bg->setImagePath(path);
    bg->saveToSettings();
    updateImagePreview(path);
    m_footerHint->setText(tr("\u80CC\u666F\u56FE\u7247\u5DF2\u5E94\u7528\u3002"));
}

void OnboardingWizard::updateImagePreview(const QString &path)
{
    if (!m_imagePreview || !m_imagePathLabel)
        return;
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        m_imagePreview->setText(tr("\u672A\u9009\u62E9\u56FE\u7247"));
        m_imagePathLabel->setText(tr("\u9009\u62E9\u4E00\u5F20\u56FE\u7247\u4F5C\u4E3A\u542F\u52A8\u5668\u80CC\u666F"));
        return;
    }
    QPixmap pm(path);
    if (!pm.isNull())
        m_imagePreview->setPixmap(pm.scaled(m_imagePreview->size(),
                                            Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation));
    else
        m_imagePreview->setText(tr("\u56FE\u7247\u65E0\u6548"));
    m_imagePathLabel->setText(path);
}

void OnboardingWizard::onPickAccent(const QString &colorName)
{
    ThemeManager::instance()->setThemeColor(colorName);
    for (int i = 0; i < 6; ++i) {
        if (QString::fromLatin1(kAccentHex[i]).compare(colorName, Qt::CaseInsensitive) == 0) {
            m_accentNameLabel->setText(QString::fromUtf8(kAccentNames[i])
                                       + QStringLiteral(" \u00B7 ")
                                       + colorName);
            break;
        }
    }
    m_footerHint->setText(tr("\u5F3A\u8C03\u8272\u5DF2\u5207\u6362\uFF0C\u5B9E\u65F6\u751F\u6548\u3002"));
}
