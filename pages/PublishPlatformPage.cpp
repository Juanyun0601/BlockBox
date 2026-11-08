/**
 * @file   PublishPlatformPage.cpp
 * @brief  平台跳转页实现
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "PublishPlatformPage.h"

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/ForumKit.h"
#include "components/OutlinedLabel.h"
#include "layouts/FlowLayout.h"

PublishPlatformPage::PublishPlatformPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    setKind(PublishKit::ResourceKind::Mod);
}

void PublishPlatformPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(10);

    m_pageTitle = new OutlinedLabel(QString(), content);
    m_pageTitle->setObjectName(QStringLiteral("resourcesPageTitle"));
    layout->addWidget(m_pageTitle);

    m_subtitle = new QLabel(content);
    m_subtitle->setObjectName(QStringLiteral("resourcesPageSubtitle"));
    m_subtitle->setWordWrap(true);
    layout->addWidget(m_subtitle);

    layout->addSpacing(10);
    layout->addWidget(PublishKit::makeSectionTitle(content, tr("跳转平台")));

    m_cardsHost = new QWidget(content);
    m_cardsHost->setAttribute(Qt::WA_StyledBackground, false);
    m_cardsLayout = new FlowLayout(m_cardsHost, 0, 12, 12);
    layout->addWidget(m_cardsHost);

    layout->addSpacing(10);
    layout->addWidget(buildTipsCard(content));
    layout->addStretch(1);

    auto *hint = new QLabel(
        tr("平台页面会在浏览器里打开；发布成功后回到方块盒子，资源页就能搜到你的作品。"),
        content);
    hint->setObjectName(QStringLiteral("forumMuted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void PublishPlatformPage::setKind(PublishKit::ResourceKind kind)
{
    m_kind = kind;
    const QString name = PublishKit::kindName(kind);
    m_pageTitle->setText(tr("发布%1").arg(name));
    m_subtitle->setText(
        tr("方块盒子不托管%1文件，请选择下面的平台，在网页端完成发布").arg(name));
    rebuildPlatformCards();
}

void PublishPlatformPage::rebuildPlatformCards()
{
    if (!m_cardsLayout)
        return;

    ForumKit::clearLayout(m_cardsLayout);

    // ── CurseForge ──
    const QString cfUrl = PublishKit::curseForgeUrl(m_kind);
    auto *cfCard = PublishKit::makeChoiceCard(
        m_cardsHost, tr("跳转 CurseForge"),
        tr("在 CurseForge 官网发布%1 · curseforge.com").arg(PublishKit::kindName(m_kind)),
        QColor(QStringLiteral("#F97316")), QColor(QStringLiteral("#C2410C")),
        QStringLiteral(":/Images/Icons/external_link.svg"), 400, 108);
    cfCard->setToolTip(cfUrl);
    cfCard->onClick = [this, cfUrl]() { emit jumpRequested(cfUrl); };
    m_cardsLayout->addWidget(cfCard);

    // ── Modrinth ──
    const QString mrUrl = PublishKit::modrinthUrl(m_kind);
    if (!mrUrl.isEmpty())
    {
        auto *mrCard = PublishKit::makeChoiceCard(
            m_cardsHost, tr("跳转 Modrinth"),
            tr("在 Modrinth 官网发布%1 · modrinth.com").arg(PublishKit::kindName(m_kind)),
            QColor(QStringLiteral("#10B981")), QColor(QStringLiteral("#047857")),
            QStringLiteral(":/Images/Icons/external_link.svg"), 400, 108);
        mrCard->setToolTip(mrUrl);
        mrCard->onClick = [this, mrUrl]() { emit jumpRequested(mrUrl); };
        m_cardsLayout->addWidget(mrCard);
    }
    else
    {
        // Modrinth 没有「世界」分类：保留入口但置灰，说明原因
        auto *mrCard = PublishKit::makeChoiceCard(
            m_cardsHost, tr("Modrinth 暂不支持世界"),
            tr("Modrinth 没有「世界」分类，世界资源请通过 CurseForge 发布"),
            QColor(QStringLiteral("#9CA3AF")), QColor(QStringLiteral("#6B7280")),
            QStringLiteral(":/Images/Icons/external_link.svg"), 400, 108);
        mrCard->setEnabled(false);
        mrCard->setToolTip(tr("Modrinth 暂不支持发布世界资源"));
        auto *fade = new QGraphicsOpacityEffect(mrCard);
        fade->setOpacity(0.55);
        mrCard->setGraphicsEffect(fade);
        m_cardsLayout->addWidget(mrCard);
    }
}

QWidget *PublishPlatformPage::buildTipsCard(QWidget *parent) const
{
    auto *card = ForumKit::makeCard(parent);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(6);

    auto *title = new QLabel(tr("发布小提示"), card);
    title->setObjectName(QStringLiteral("forumSectionTitle"));
    layout->addWidget(title);

    const QStringList tips = {
        tr("1. 在平台登录创作者账号，填写资源信息并上传文件"),
        tr("2. 等待平台审核通过（通常几小时到几天）"),
        tr("3. 发布完成后回到方块盒子，在「资源」页就能搜到并下载"),
    };
    for (const QString &tip : tips)
    {
        auto *label = new QLabel(tip, card);
        label->setObjectName(QStringLiteral("forumMuted"));
        label->setWordWrap(true);
        layout->addWidget(label);
    }

    return card;
}
