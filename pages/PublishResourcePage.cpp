/**
 * @file   PublishResourcePage.cpp
 * @brief  发布资源页实现：八种资源类型卡片
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "PublishResourcePage.h"

#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>

#include "components/ForumKit.h"
#include "components/OutlinedLabel.h"
#include "layouts/FlowLayout.h"

PublishResourcePage::PublishResourcePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void PublishResourcePage::buildUi()
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

    auto *pageTitle = new OutlinedLabel(tr("发布资源"), content);
    pageTitle->setObjectName(QStringLiteral("resourcesPageTitle"));
    layout->addWidget(pageTitle);

    auto *subtitle = new QLabel(
        tr("先选资源类型：模组、资源包等会带你前往对应平台发布；插件与投影是站内发布，可直接在这里上传分享"),
        content);
    subtitle->setObjectName(QStringLiteral("resourcesPageSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    layout->addSpacing(10);
    layout->addWidget(PublishKit::makeSectionTitle(content, tr("资源类型")));

    auto *cardsHost = new QWidget(content);
    cardsHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *flow = new FlowLayout(cardsHost, 0, 12, 12);

    const QVector<PublishKit::ResourceKind> kinds = {
        PublishKit::ResourceKind::Mod,
        PublishKit::ResourceKind::Modpack,
        PublishKit::ResourceKind::ResourcePack,
        PublishKit::ResourceKind::ShaderPack,
        PublishKit::ResourceKind::DataPack,
        PublishKit::ResourceKind::World,
        PublishKit::ResourceKind::Plugin,
        PublishKit::ResourceKind::Schematic,
    };

    for (PublishKit::ResourceKind kind : kinds)
    {
        auto *card = PublishKit::makeChoiceCard(
            cardsHost, PublishKit::kindName(kind), PublishKit::kindDescription(kind),
            PublishKit::kindGradientStart(kind), PublishKit::kindGradientEnd(kind),
            PublishKit::kindIconPath(kind), 300, 108);
        card->onClick = [this, kind]() { emit kindSelected(kind); };
        flow->addWidget(card);
    }

    layout->addWidget(cardsHost);
    layout->addStretch(1);

    auto *hint = new QLabel(
        tr("插件会发布到社区的「插件分享」板块，要上传 .BlockBox 插件包；投影会发布到「投影分享」板块，要上传投影文件；只想交流讨论的话，发帖时选对应板块即可。"),
        content);
    hint->setObjectName(QStringLiteral("forumMuted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}
