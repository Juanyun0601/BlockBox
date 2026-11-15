/**
 * @file   LobbyJoinPrepDialog.cpp
 * @brief  「一键加入联机房间」准备对话框实现
 * @author BlockBox Team
 * @date   2026-11-14
 */

#include "LobbyJoinPrepDialog.h"

#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"
#include "utils/TriviaBank.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPoint>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/** 内容类型 → 分组标题 / 说明 */
struct GroupMeta
{
    QString title;
    QString hint;
};

GroupMeta metaFor(ContentType type, bool optional)
{
    switch (type)
    {
    case ContentType::ResourcePack:
        return {QObject::tr("资源包"), QObject::tr("材质包，不装也能进")};
    case ContentType::ShaderPack:
        return {QObject::tr("光影包"), QObject::tr("体积大，低配可跳过")};
    default:
        return optional ? GroupMeta{QObject::tr("辅助模组"),
                                    QObject::tr("带「辅助」标签，可选")}
                        : GroupMeta{QObject::tr("必装模组"),
                                    QObject::tr("房主要求的，缺了可能进不去")};
    }
}

QString elideName(const QString &name, int maxChars = 46)
{
    return name.size() <= maxChars ? name : name.left(maxChars - 1) + QStringLiteral("…");
}

} // namespace

LobbyJoinPrepDialog::LobbyJoinPrepDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("lobbyJoinPrepDialog"));
    setWindowTitle(tr("准备加入"));
    setMinimumWidth(520);

    m_fetcher = new LobbyContentFetcher(this);
    connect(m_fetcher, &LobbyContentFetcher::progressChanged, this,
            [this](int done, int total, const QString &message) {
                onProgress(done, total, message);
            });
    connect(m_fetcher, &LobbyContentFetcher::itemFinished, this,
            &LobbyJoinPrepDialog::onItemFinished);
    connect(m_fetcher, &LobbyContentFetcher::finished, this,
            &LobbyJoinPrepDialog::onFetchFinished);

    buildUi();
    applyStyles();
}

LobbyJoinPrepDialog::~LobbyJoinPrepDialog() = default;

void LobbyJoinPrepDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("lobbyJoinPrepCard"));
    card->setMinimumWidth(520);
    m_card = card;
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 18, 20, 18);
    cardLayout->setSpacing(10);

    m_titleLabel = new QLabel(card);
    m_titleLabel->setObjectName(QStringLiteral("lobbyPrepTitle"));
    m_titleLabel->setWordWrap(true);
    cardLayout->addWidget(m_titleLabel);

    m_subTitleLabel = new QLabel(card);
    m_subTitleLabel->setObjectName(QStringLiteral("lobbyPrepMuted"));
    m_subTitleLabel->setWordWrap(true);
    cardLayout->addWidget(m_subTitleLabel);

    // 房主写的简要介绍：有就显示在副标题下面，没有就不占位置
    m_descLabel = new QLabel(card);
    m_descLabel->setObjectName(QStringLiteral("lobbyPrepDesc"));
    m_descLabel->setWordWrap(true);
    m_descLabel->setTextFormat(Qt::PlainText);
    m_descLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_descLabel->hide();
    cardLayout->addWidget(m_descLabel);

    m_pages = new QStackedWidget(card);
    cardLayout->addWidget(m_pages, 1);

    // ───────────── 配置页 ─────────────
    auto *configPage = new QWidget(m_pages);
    auto *configLayout = new QVBoxLayout(configPage);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setSpacing(8);

    m_instanceCaption = new QLabel(tr("用哪个实例加入"), configPage);
    m_instanceCaption->setObjectName(QStringLiteral("lobbyPrepSection"));
    configLayout->addWidget(m_instanceCaption);

    m_instanceCombo = new QComboBox(configPage);
    m_instanceCombo->setObjectName(QStringLiteral("lobbyPrepCombo"));
    m_instanceCombo->setMinimumHeight(34);
    connect(m_instanceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                rebuildContentGroups();
                updateFooterState();
            });
    configLayout->addWidget(m_instanceCombo);

    m_instanceHint = new QLabel(configPage);
    m_instanceHint->setObjectName(QStringLiteral("lobbyPrepMuted"));
    m_instanceHint->setWordWrap(true);
    configLayout->addWidget(m_instanceHint);

    m_installButton = new QPushButton(tr("先去安装对应版本"), configPage);
    m_installButton->setObjectName(QStringLiteral("lobbyPrepInstallBtn"));
    m_installButton->setCursor(Qt::PointingHandCursor);
    m_installButton->setFixedHeight(32);
    m_installButton->hide();
    connect(m_installButton, &QPushButton::clicked, this, &LobbyJoinPrepDialog::requestInstall);
    configLayout->addWidget(m_installButton);

    // 勾选清单放在一个只占「窗口剩下的高度」的滚动区里：内容少时整段铺开，
    // 缺十几项时才滚动，窗口本身始终按内容大小排版、不会顶出屏幕
    m_scroll = new QScrollArea(configPage);
    m_scroll->setObjectName(QStringLiteral("lobbyPrepScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 清单有几项就开多高，超出屏幕上限时才出现滚动条
    m_scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_scroll->setMinimumHeight(120);

    m_scrollBody = new QWidget(m_scroll);
    m_scrollBody->setObjectName(QStringLiteral("lobbyPrepScrollBody"));
    m_scrollLayout = new QVBoxLayout(m_scrollBody);
    m_scrollLayout->setContentsMargins(0, 0, 4, 0);
    m_scrollLayout->setSpacing(8);
    m_scroll->setWidget(m_scrollBody);
    configLayout->addWidget(m_scroll, 1);

    // 四个内容分组（空组隐藏）
    auto makeGroup = [this](const QString &title, const QString &hint,
                            QList<QCheckBox *> *boxes, QVBoxLayout **itemLayoutOut) {
        auto *group = new QWidget(m_scrollBody);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(4);

        auto *headerRow = new QHBoxLayout();
        headerRow->setContentsMargins(0, 0, 0, 0);
        headerRow->setSpacing(6);

        auto *titleLabel = new QLabel(title, group);
        titleLabel->setObjectName(QStringLiteral("lobbyPrepSection"));
        headerRow->addWidget(titleLabel);

        auto *hintLabel = new QLabel(hint, group);
        hintLabel->setObjectName(QStringLiteral("lobbyPrepMuted"));
        hintLabel->setWordWrap(true);
        headerRow->addWidget(hintLabel, 1);

        // 一次勾很多项时的快捷操作：全选 / 清空（只作用于本组）
        auto *allBtn = new QPushButton(tr("全选"), group);
        allBtn->setObjectName(QStringLiteral("lobbyPrepMiniBtn"));
        allBtn->setCursor(Qt::PointingHandCursor);
        allBtn->setFixedSize(42, 22);
        connect(allBtn, &QPushButton::clicked, this, [boxes]() {
            for (QCheckBox *box : *boxes)
            {
                if (box)
                    box->setChecked(true);
            }
        });
        headerRow->addWidget(allBtn);

        auto *noneBtn = new QPushButton(tr("清空"), group);
        noneBtn->setObjectName(QStringLiteral("lobbyPrepMiniBtn"));
        noneBtn->setCursor(Qt::PointingHandCursor);
        noneBtn->setFixedSize(42, 22);
        connect(noneBtn, &QPushButton::clicked, this, [boxes]() {
            for (QCheckBox *box : *boxes)
            {
                if (box)
                    box->setChecked(false);
            }
        });
        headerRow->addWidget(noneBtn);

        groupLayout->addLayout(headerRow);

        auto *items = new QVBoxLayout();
        items->setContentsMargins(0, 0, 0, 0);
        items->setSpacing(2);
        groupLayout->addLayout(items);

        if (itemLayoutOut)
            *itemLayoutOut = items;
        m_scrollLayout->addWidget(group);
        return group;
    };

    m_requiredGroup = makeGroup(metaFor(ContentType::Mod, false).title,
                                metaFor(ContentType::Mod, false).hint, &m_requiredBoxes,
                                &m_requiredLayout);
    m_auxGroup = makeGroup(metaFor(ContentType::Mod, true).title,
                           metaFor(ContentType::Mod, true).hint, &m_auxBoxes, &m_auxLayout);
    m_packGroup = makeGroup(metaFor(ContentType::ResourcePack, true).title,
                            metaFor(ContentType::ResourcePack, true).hint, &m_packBoxes,
                            &m_packLayout);
    m_shaderGroup = makeGroup(metaFor(ContentType::ShaderPack, true).title,
                              metaFor(ContentType::ShaderPack, true).hint, &m_shaderBoxes,
                              &m_shaderLayout);
    m_scrollLayout->addStretch(1);

    m_emptyHint = new QLabel(tr("本机实例已经齐了，直接进房就行。"), configPage);
    m_emptyHint->setObjectName(QStringLiteral("lobbyPrepMuted"));
    m_emptyHint->setWordWrap(true);
    m_emptyHint->hide();
    configLayout->addWidget(m_emptyHint);

    // 底部按钮：取消 / 跳过下载 / 下载并加入
    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 4, 0, 0);
    footer->setSpacing(8);
    footer->addStretch(1);

    m_cancelButton = new QPushButton(tr("取消"), configPage);
    m_cancelButton->setObjectName(QStringLiteral("lobbyPrepGhostBtn"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setFixedHeight(34);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(m_cancelButton);

    m_skipButton = new QPushButton(tr("跳过下载，直接加入"), configPage);
    m_skipButton->setObjectName(QStringLiteral("lobbyPrepGhostBtn"));
    m_skipButton->setCursor(Qt::PointingHandCursor);
    m_skipButton->setFixedHeight(34);
    connect(m_skipButton, &QPushButton::clicked, this, [this]() {
        m_pendingRequests.clear();
        accept();
    });
    footer->addWidget(m_skipButton);

    m_downloadButton = new QPushButton(tr("下载并加入"), configPage);
    m_downloadButton->setObjectName(QStringLiteral("lobbyPrepPrimaryBtn"));
    m_downloadButton->setCursor(Qt::PointingHandCursor);
    m_downloadButton->setFixedHeight(34);
    m_downloadButton->setDefault(true);
    connect(m_downloadButton, &QPushButton::clicked, this, &LobbyJoinPrepDialog::startDownload);
    footer->addWidget(m_downloadButton);

    configLayout->addLayout(footer);
    m_pages->addWidget(configPage);

    // ───────────── 进度页 ─────────────
    auto *progressPage = new QWidget(m_pages);
    auto *progressLayout = new QVBoxLayout(progressPage);
    progressLayout->setContentsMargins(0, 8, 0, 0);
    progressLayout->setSpacing(10);

    m_progressLabel = new QLabel(tr("正在准备…"), progressPage);
    m_progressLabel->setObjectName(QStringLiteral("lobbyPrepTitle"));
    m_progressLabel->setWordWrap(true);
    progressLayout->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(progressPage);
    m_progressBar->setObjectName(QStringLiteral("lobbyPrepProgress"));
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    progressLayout->addWidget(m_progressBar);

    m_progressDetail = new QLabel(progressPage);
    m_progressDetail->setObjectName(QStringLiteral("lobbyPrepMuted"));
    m_progressDetail->setWordWrap(true);
    progressLayout->addWidget(m_progressDetail);

    m_failListLabel = new QLabel(progressPage);
    m_failListLabel->setObjectName(QStringLiteral("lobbyPrepWarn"));
    m_failListLabel->setWordWrap(true);
    m_failListLabel->hide();
    progressLayout->addWidget(m_failListLabel);

    progressLayout->addStretch(1);

    // 「你知道吗」冷知识：等待下载时随进度页展示
    m_triviaLabel = new QLabel(progressPage);
    m_triviaLabel->setObjectName(QStringLiteral("lobbyPrepTrivia"));
    m_triviaLabel->setWordWrap(true);
    m_triviaLabel->setText(QStringLiteral("<b>%1</b>&nbsp;&nbsp;%2")
                               .arg(tr("你知道吗"), TriviaBank::randomTrivia()));
    progressLayout->addWidget(m_triviaLabel);

    auto *progressFooter = new QHBoxLayout();
    progressFooter->setContentsMargins(0, 4, 0, 0);
    progressFooter->setSpacing(8);
    progressFooter->addStretch(1);

    m_progressCancelButton = new QPushButton(tr("取消加入"), progressPage);
    m_progressCancelButton->setObjectName(QStringLiteral("lobbyPrepGhostBtn"));
    m_progressCancelButton->setCursor(Qt::PointingHandCursor);
    m_progressCancelButton->setFixedHeight(34);
    connect(m_progressCancelButton, &QPushButton::clicked, this, [this]() {
        if (m_fetcher)
            m_fetcher->cancel();
        reject();
    });
    progressFooter->addWidget(m_progressCancelButton);

    m_continueButton = new QPushButton(tr("仍然加入"), progressPage);
    m_continueButton->setObjectName(QStringLiteral("lobbyPrepPrimaryBtn"));
    m_continueButton->setCursor(Qt::PointingHandCursor);
    m_continueButton->setFixedHeight(34);
    m_continueButton->hide();
    connect(m_continueButton, &QPushButton::clicked, this, &QDialog::accept);
    progressFooter->addWidget(m_continueButton);

    progressLayout->addLayout(progressFooter);
    m_pages->addWidget(progressPage);

    outer->addWidget(card);
}

void LobbyJoinPrepDialog::applyStyles()
{
    setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QWidget#lobbyJoinPrepCard {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 14px;"
        "}"
        "QScrollArea#lobbyPrepScroll, QWidget#lobbyPrepScrollBody {"
        "  background: transparent; border: none;"
        "}"
        "QLabel#lobbyPrepTitle {"
        "  color: @TEXT_PRIMARY@; font-size: 16px; font-weight: 800;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#lobbyPrepSection {"
        "  color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#lobbyPrepMuted {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#lobbyPrepDesc {"
        "  color: @TEXT_SECONDARY@; font-size: 12.5px;"
        "  background: @BG_MUTED@; border: 1px solid @BORDER_LIGHT@; border-radius: 8px;"
        "  padding: 8px 10px;"
        "}"
        "QLabel#lobbyPrepWarn {"
        "  color: @WARNING_TEXT@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#lobbyPrepTrivia {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QScrollArea#lobbyPrepScroll, QWidget#lobbyPrepScrollBody {"
        "  background: transparent; border: none;"
        "}"
        "QComboBox#lobbyPrepCombo {"
        "  background: @BG_MUTED@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 4px 10px;"
        "}"
        "QComboBox#lobbyPrepCombo:hover { border-color: @BORDER_HOVER@; }"
        "QComboBox#lobbyPrepCombo:focus { border-color: @PRIMARY@; }"
        "QComboBox#lobbyPrepCombo QAbstractItemView {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 8px;"
        "  color: @TEXT_PRIMARY@; selection-background-color: @PRIMARY_RGBA_20@;"
        "  selection-color: @TEXT_ACCENT@; padding: 4px;"
        "}"
        "QPushButton#lobbyPrepPrimaryBtn {"
        "  background: @PRIMARY@; color: @TEXT_ON_PRIMARY@; border: none; border-radius: 9px;"
        "  padding: 0 16px; font-size: 13px; font-weight: 700;"
        "}"
        "QPushButton#lobbyPrepPrimaryBtn:hover { background: @PRIMARY_HOVER@; }"
        "QPushButton#lobbyPrepPrimaryBtn:pressed { background: @PRIMARY_PRESSED@; }"
        "QPushButton#lobbyPrepPrimaryBtn:disabled {"
        "  background: @BG_DISABLED@; color: @TEXT_DISABLED@;"
        "}"
        "QPushButton#lobbyPrepGhostBtn {"
        "  background: transparent; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_SECONDARY@; padding: 0 14px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton#lobbyPrepGhostBtn:hover {"
        "  color: @TEXT_PRIMARY@; border-color: @PRIMARY@; background: @BG_HOVER@;"
        "}"
        "QPushButton#lobbyPrepGhostBtn:pressed { background: @BG_PRESSED@; }"
        "QPushButton#lobbyPrepInstallBtn {"
        "  background: transparent; border: 1px solid @PRIMARY_RGBA_38@; border-radius: 9px;"
        "  color: @TEXT_ACCENT@; padding: 0 14px; font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton#lobbyPrepInstallBtn:hover {"
        "  background: @PRIMARY_RGBA_10@; border-color: @PRIMARY@;"
        "}"
        "QPushButton#lobbyPrepMiniBtn {"
        "  background: transparent; border: 1px solid @BORDER_LIGHT@; border-radius: 6px;"
        "  color: @TEXT_TERTIARY@; padding: 0 6px; font-size: 11px;"
        "  min-width: 0px; max-width: 42px; min-height: 0px; max-height: 22px;"
        "}"
        "QPushButton#lobbyPrepMiniBtn:hover {"
        "  color: @TEXT_PRIMARY@; border-color: @PRIMARY@; background: @BG_HOVER@;"
        "}"
        "QCheckBox { color: @TEXT_SECONDARY@; font-size: 12px; }"
        "QProgressBar#lobbyPrepProgress {"
        "  background: @BG_MUTED@; border: none; border-radius: 4px;"
        "}"
        "QProgressBar#lobbyPrepProgress::chunk {"
        "  background: @PRIMARY@; border-radius: 4px;"
        "}"
    )));
}

void LobbyJoinPrepDialog::setPlan(const LobbyInstanceMatcher::JoinPlan &plan,
                                  const QString &roomTitle, const QString &joinTarget,
                                  const QString &description)
{
    m_plan = plan;
    m_roomTitle = roomTitle.trimmed();
    m_joinTarget = joinTarget.trimmed();

    m_titleLabel->setText(m_roomTitle.isEmpty()
                              ? tr("准备加入这个房间")
                              : tr("准备加入「%1」").arg(elideName(m_roomTitle, 28)));

    const QString intro = description.trimmed();
    m_descLabel->setText(intro);
    m_descLabel->setVisible(!intro.isEmpty());

    QStringList subParts;
    if (!m_joinTarget.isEmpty())
        subParts << tr("邀请码 / 地址 %1").arg(elideName(m_joinTarget, 24));
    const QString version = plan.requirement.version;
    const QString loader = plan.requirement.loader;
    if (!version.isEmpty())
        subParts << tr("MC %1").arg(version);
    if (!loader.isEmpty())
        subParts << loader;
    m_subTitleLabel->setText(subParts.join(QStringLiteral(" · ")));
    m_subTitleLabel->setVisible(!subParts.isEmpty());

    rebuildInstanceCombo();
    rebuildContentGroups();
    updateFooterState();
}

void LobbyJoinPrepDialog::rebuildInstanceCombo()
{
    m_instanceCombo->blockSignals(true);
    m_instanceCombo->clear();

    if (m_plan.candidates.isEmpty())
    {
        m_instanceCombo->addItem(tr("本机没有可用的实例"));
        m_instanceCombo->setEnabled(false);
        m_instanceCombo->blockSignals(false);
        m_instanceHint->setText(m_plan.installHint);
        m_installButton->setVisible(true);
        m_skipButton->setVisible(false);
        return;
    }

    m_instanceCombo->setEnabled(true);
    m_installButton->hide();
    m_skipButton->setVisible(true);

    for (int i = 0; i < m_plan.candidates.size(); ++i)
    {
        const LobbyInstanceMatcher::InstanceCheck &check = m_plan.candidates.at(i);
        QString label = check.instance.id;
        if (!check.instance.version.isEmpty())
            label += QStringLiteral("  ·  ") + check.instance.version;
        if (!check.instance.loader.isEmpty())
            label += QStringLiteral("  ·  ") + check.instance.loader;
        if (check.hasAnythingToFetch())
            label += tr("  ·  缺 %1 项").arg(check.missingTotal());
        else
            label += tr("  ·  已齐全");
        m_instanceCombo->addItem(label);
    }
    m_instanceCombo->setCurrentIndex(0);
    m_instanceCombo->blockSignals(false);
}

void LobbyJoinPrepDialog::clearGroup(QVBoxLayout *layout, QList<QCheckBox *> *boxes)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    if (boxes)
        boxes->clear();
}

QCheckBox *LobbyJoinPrepDialog::makeCheckRow(QVBoxLayout *layout, const QString &fileName,
                                             bool checked, const QString &tooltip)
{
    auto *box = new QCheckBox(elideName(fileName), m_scrollBody);
    box->setChecked(checked);
    box->setToolTip(tooltip);
    box->setProperty("lobbyFileName", fileName);
    box->setCursor(Qt::PointingHandCursor);
    connect(box, &QCheckBox::toggled, this, &LobbyJoinPrepDialog::updateFooterState);
    layout->addWidget(box);
    return box;
}

void LobbyJoinPrepDialog::rebuildContentGroups()
{
    clearGroup(m_requiredLayout, &m_requiredBoxes);
    clearGroup(m_auxLayout, &m_auxBoxes);
    clearGroup(m_packLayout, &m_packBoxes);
    clearGroup(m_shaderLayout, &m_shaderBoxes);

    const int index = m_instanceCombo->currentIndex();
    const bool haveCandidate = index >= 0 && index < m_plan.candidates.size();
    if (m_scroll)
        m_scroll->setVisible(haveCandidate);
    if (!haveCandidate)
    {
        m_requiredGroup->hide();
        m_auxGroup->hide();
        m_packGroup->hide();
        m_shaderGroup->hide();
        m_emptyHint->hide();
        m_instanceHint->setText(m_plan.installHint);
        return;
    }

    const LobbyInstanceMatcher::InstanceCheck &check = m_plan.candidates.at(index);

    // 实例说明：匹配等级 + 还缺什么（不铺完整路径，太长会挤掉有用信息）
    QStringList hintParts;
    hintParts << LobbyInstanceMatcher::levelText(check.level);
    if (check.missingMods.isEmpty())
        hintParts << tr("必装模组都齐了");
    else
        hintParts << tr("还缺 %1 个必装模组").arg(check.missingMods.size());
    if (!check.missingAuxMods.isEmpty())
        hintParts << tr("辅助模组缺 %1 个").arg(check.missingAuxMods.size());
    if (!check.missingResourcePacks.isEmpty() || !check.missingShaderPacks.isEmpty())
    {
        hintParts << tr("资源包 / 光影缺 %1 个")
                         .arg(check.missingResourcePacks.size() + check.missingShaderPacks.size());
    }
    m_instanceHint->setText(hintParts.join(QStringLiteral(" · ")));

    // 必装模组：默认全勾
    for (const QString &name : check.missingMods)
        m_requiredBoxes.append(
            makeCheckRow(m_requiredLayout, name, true, tr("房间要求这个模组：%1").arg(name)));
    m_requiredGroup->setVisible(!m_requiredBoxes.isEmpty());

    // 辅助模组：默认不勾
    for (const QString &name : check.missingAuxMods)
        m_auxBoxes.append(makeCheckRow(m_auxLayout, name, false,
                                       tr("辅助模组（可选）：%1").arg(name)));
    m_auxGroup->setVisible(!m_auxBoxes.isEmpty());

    // 资源包：默认勾（画面更接近房主）
    for (const QString &name : check.missingResourcePacks)
        m_packBoxes.append(
            makeCheckRow(m_packLayout, name, true, tr("房主使用的资源包：%1").arg(name)));
    m_packGroup->setVisible(!m_packBoxes.isEmpty());

    // 光影包：默认不勾（体积大、吃性能）
    for (const QString &name : check.missingShaderPacks)
        m_shaderBoxes.append(
            makeCheckRow(m_shaderLayout, name, false, tr("房主使用的光影包：%1").arg(name)));
    m_shaderGroup->setVisible(!m_shaderBoxes.isEmpty());

    const bool nothingMissing = check.missingTotal() == 0;
    m_emptyHint->setVisible(nothingMissing);
    if (m_scrollLayout)
        m_scrollLayout->invalidate();

    // 勾选项数量变了，窗口高度跟着重算（延迟到本轮事件循环，等布局生效）
    QTimer::singleShot(0, this, [this]() { updateContentHeight(); });
}

void LobbyJoinPrepDialog::updateFooterState()
{
    if (!m_downloadButton)
        return;

    const int selected = selectedRequests().size();
    const bool haveCandidate = !selectedInstancePath().isEmpty();
    m_downloadButton->setEnabled(haveCandidate && selected > 0);
    m_downloadButton->setText(selected > 0 ? tr("下载 %1 项并加入").arg(selected)
                                           : tr("下载并加入"));

    // 什么都不缺时干脆不给「下载」按钮，只留「直接加入」
    if (m_emptyHint && m_emptyHint->isVisible())
        m_downloadButton->setVisible(false);
    else
        m_downloadButton->setVisible(true);

    if (m_skipButton)
    {
        m_skipButton->setEnabled(haveCandidate);
        m_skipButton->setText(selected > 0 ? tr("先不下载，直接加入") : tr("直接加入"));
    }
}

QString LobbyJoinPrepDialog::selectedInstancePath() const
{
    const int index = m_instanceCombo ? m_instanceCombo->currentIndex() : -1;
    if (index < 0 || index >= m_plan.candidates.size())
        return QString();
    return m_plan.candidates.at(index).instance.path;
}

QList<LobbyFetchRequest> LobbyJoinPrepDialog::selectedRequests() const
{
    QList<LobbyFetchRequest> requests;

    auto collect = [&requests](const QList<QCheckBox *> &boxes, ContentType type, bool optional) {
        for (QCheckBox *box : boxes)
        {
            if (!box || !box->isChecked())
                continue;
            LobbyFetchRequest request;
            request.type = type;
            request.optional = optional;
            request.fileName = box->property("lobbyFileName").toString();
            if (!request.fileName.trimmed().isEmpty())
                requests.append(request);
        }
    };

    // 顺序：必装模组 → 辅助模组 → 资源包 → 光影包（先保证能进房的东西）
    collect(m_requiredBoxes, ContentType::Mod, false);
    collect(m_auxBoxes, ContentType::Mod, true);
    collect(m_packBoxes, ContentType::ResourcePack, true);
    collect(m_shaderBoxes, ContentType::ShaderPack, true);
    return requests;
}

void LobbyJoinPrepDialog::startDownload()
{
    const QString instancePath = selectedInstancePath();
    if (instancePath.isEmpty())
        return;

    const QList<LobbyFetchRequest> requests = selectedRequests();
    if (requests.isEmpty())
    {
        accept();
        return;
    }

    m_pendingRequests = requests;
    m_failedCount = 0;
    m_downloading = true;
    m_failNames.clear();

    m_pages->setCurrentIndex(1);
    if (m_progressBar)
    {
        m_progressBar->setRange(0, requests.size());
        m_progressBar->setValue(0);
    }
    if (m_progressLabel)
        m_progressLabel->setText(tr("正在准备 %1 个文件…").arg(requests.size()));
    if (m_progressDetail)
        m_progressDetail->setText(tr("先跟下载源核对版本和加载器，再逐个下载。"));
    if (m_failListLabel)
        m_failListLabel->hide();
    if (m_continueButton)
        m_continueButton->hide();
    if (m_progressCancelButton)
        m_progressCancelButton->setText(tr("取消加入"));

    m_fetcher->start(instancePath, m_plan.requirement.version, m_plan.requirement.loader,
                     requests);
}

void LobbyJoinPrepDialog::onProgress(int done, int total, const QString &message)
{
    if (m_progressBar)
    {
        m_progressBar->setRange(0, qMax(1, total));
        m_progressBar->setValue(done);
    }
    if (m_progressDetail)
        m_progressDetail->setText(message);
}

void LobbyJoinPrepDialog::onItemFinished(int index, const LobbyFetchResult &result)
{
    Q_UNUSED(index);
    if (m_progressBar)
        m_progressBar->setValue(m_progressBar->value() + 1);

    if (!result.ok)
    {
        m_failedCount++;
        m_failNames.append(QStringLiteral("%1（%2）")
                               .arg(elideName(result.request.fileName, 32),
                                    result.message.trimmed().isEmpty() ? tr("下载失败")
                                                                       : result.message));
    }
}

void LobbyJoinPrepDialog::onFetchFinished(int okCount, int failCount)
{
    m_downloading = false;

    if (m_progressBar)
        m_progressBar->setValue(m_progressBar->maximum());
    if (m_progressLabel)
    {
        m_progressLabel->setText(failCount == 0
                                     ? tr("准备好了，正在进房…")
                                     : tr("装好了 %1 项，还有 %2 项没装上").arg(okCount).arg(failCount));
    }
    if (m_progressDetail)
    {
        m_progressDetail->setText(failCount == 0
                                      ? tr("马上启动游戏直连房间。")
                                      : tr("没装上的不影响进房，只是画面 / 玩法可能和房主不完全一样。"));
    }
    if (m_progressCancelButton)
        m_progressCancelButton->setText(tr("取消加入"));

    if (failCount > 0)
    {
        if (m_failListLabel)
        {
            const int show = qMin(m_failNames.size(), 8);
            QStringList lines = m_failNames.mid(0, show);
            if (m_failNames.size() > show)
                lines << tr("…还有 %1 项").arg(m_failNames.size() - show);
            m_failListLabel->setText(tr("这些没装上：\n%1").arg(lines.join(QLatin1Char('\n'))));
            m_failListLabel->show();
        }
        if (m_continueButton)
        {
            m_continueButton->setText(tr("仍然加入"));
            m_continueButton->show();
        }
        return;
    }

    // 全部成功：直接继续进房
    accept();
}

void LobbyJoinPrepDialog::requestInstall()
{
    m_installRequested = true;
    done(kResultInstall);
}

QList<LobbyFetchResult> LobbyJoinPrepDialog::fetchResults() const
{
    return m_fetcher ? m_fetcher->results() : QList<LobbyFetchResult>();
}

void LobbyJoinPrepDialog::closeEvent(QCloseEvent *event)
{
    if (m_downloading && m_fetcher)
        m_fetcher->cancel();
    QDialog::closeEvent(event);
}

void LobbyJoinPrepDialog::updateContentHeight()
{
    if (!m_scroll || !m_scrollBody || !m_scroll->isVisible())
        return;
    if (!m_scrollBody->layout())
        return;

    // 清单有几项就开多高；超过「屏幕高度减掉标题和按钮」时才滚动
    m_scroll->setMaximumHeight(QWIDGETSIZE_MAX);
    m_scrollBody->layout()->activate();
    const int contentHeight = m_scrollBody->layout()->sizeHint().height() + 8;

    int maxScroll = 460;
    if (QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center())))
    {
        layout()->activate();
        const int topOffset = m_scroll->mapTo(this, QPoint(0, 0)).y();
        const int bottomChrome = height() - (topOffset + m_scroll->height());
        const int maxDialogHeight = int(screen->availableGeometry().height() * 0.86);
        maxScroll = qMax(150, maxDialogHeight - topOffset - bottomChrome);
    }
    m_scroll->setFixedHeight(qBound(120, contentHeight, maxScroll));
    adjustSize();
}

void LobbyJoinPrepDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    // 布局还没激活时尺寸都是旧的，先强制排一遍再按内容定高
    if (m_scrollBody && m_scrollBody->layout())
        m_scrollBody->layout()->activate();
    if (layout())
        layout()->activate();
    adjustSize();
    updateContentHeight();
}
