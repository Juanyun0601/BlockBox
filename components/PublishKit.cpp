/**
 * @file   PublishKit.cpp
 * @brief  发布中心共用部件实现
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "PublishKit.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QVBoxLayout>

#include "OutlinedLabel.h"
#include "ForumKit.h"
#include "utils/IconHelper.h"

namespace PublishKit
{

QString kindName(ResourceKind kind)
{
    switch (kind)
    {
    case ResourceKind::Mod:          return QCoreApplication::translate("PublishKit", "模组");
    case ResourceKind::Modpack:      return QCoreApplication::translate("PublishKit", "整合包");
    case ResourceKind::ResourcePack: return QCoreApplication::translate("PublishKit", "资源包");
    case ResourceKind::ShaderPack:   return QCoreApplication::translate("PublishKit", "光影包");
    case ResourceKind::DataPack:     return QCoreApplication::translate("PublishKit", "数据包");
    case ResourceKind::World:        return QCoreApplication::translate("PublishKit", "世界");
    case ResourceKind::Plugin:       return QCoreApplication::translate("PublishKit", "插件");
    case ResourceKind::Schematic:    return QCoreApplication::translate("PublishKit", "投影");
    }
    return QString();
}

QString kindDescription(ResourceKind kind)
{
    switch (kind)
    {
    case ResourceKind::Mod:
        return QCoreApplication::translate("PublishKit", "给游戏加新玩法、新物品与新机制");
    case ResourceKind::Modpack:
        return QCoreApplication::translate("PublishKit", "打包好的一整套模组，选版本就能玩");
    case ResourceKind::ResourcePack:
        return QCoreApplication::translate("PublishKit", "替换贴图、音效与界面外观");
    case ResourceKind::ShaderPack:
        return QCoreApplication::translate("PublishKit", "为游戏加上光影、水面与材质效果");
    case ResourceKind::DataPack:
        return QCoreApplication::translate("PublishKit", "用数据驱动的方式扩展原版玩法");
    case ResourceKind::World:
        return QCoreApplication::translate("PublishKit", "分享你制作的存档、地图与冒险世界");
    case ResourceKind::Plugin:
        // 「插件」指的是方块盒子自己的插件（.BlockBox 启动器扩展），不是给服务器用的插件
        return QCoreApplication::translate("PublishKit", "方块盒子的插件，上传 .BlockBox 包发到插件分享板块");
    case ResourceKind::Schematic:
        return QCoreApplication::translate("PublishKit", "分享你搭建的建筑投影，.litematic / .schem 都能发");
    }
    return QString();
}

QString kindIconPath(ResourceKind kind)
{
    switch (kind)
    {
    case ResourceKind::Mod:          return QStringLiteral(":/Images/Icons/nav_mods.svg");
    case ResourceKind::Modpack:      return QStringLiteral(":/Images/Icons/nav_download.svg");
    case ResourceKind::ResourcePack: return QStringLiteral(":/Images/Icons/nav_resourcepacks.svg");
    case ResourceKind::ShaderPack:   return QStringLiteral(":/Images/Icons/nav_shaders.svg");
    case ResourceKind::DataPack:     return QStringLiteral(":/Images/Icons/nav_datapacks.svg");
    case ResourceKind::World:        return QStringLiteral(":/Images/Icons/nav_worlds.svg");
    case ResourceKind::Plugin:       return QStringLiteral(":/Images/Icons/nav_plugin.svg");
    case ResourceKind::Schematic:    return QStringLiteral(":/Images/Icons/list.svg");
    }
    return QString();
}

QColor kindGradientStart(ResourceKind kind)
{
    switch (kind)
    {
    case ResourceKind::Mod:          return QColor(QStringLiteral("#10B981"));
    case ResourceKind::Modpack:      return QColor(QStringLiteral("#F59E0B"));
    case ResourceKind::ResourcePack: return QColor(QStringLiteral("#06B6D4"));
    case ResourceKind::ShaderPack:   return QColor(QStringLiteral("#8B5CF6"));
    case ResourceKind::DataPack:     return QColor(QStringLiteral("#84CC16"));
    case ResourceKind::World:        return QColor(QStringLiteral("#EC4899"));
    case ResourceKind::Plugin:       return QColor(QStringLiteral("#6366F1"));
    case ResourceKind::Schematic:    return QColor(QStringLiteral("#3B82F6"));
    }
    return QColor(QStringLiteral("#10B981"));
}

QColor kindGradientEnd(ResourceKind kind)
{
    switch (kind)
    {
    case ResourceKind::Mod:          return QColor(QStringLiteral("#047857"));
    case ResourceKind::Modpack:      return QColor(QStringLiteral("#B45309"));
    case ResourceKind::ResourcePack: return QColor(QStringLiteral("#0E7490"));
    case ResourceKind::ShaderPack:   return QColor(QStringLiteral("#6D28D9"));
    case ResourceKind::DataPack:     return QColor(QStringLiteral("#4D7C0F"));
    case ResourceKind::World:        return QColor(QStringLiteral("#BE185D"));
    case ResourceKind::Plugin:       return QColor(QStringLiteral("#4338CA"));
    case ResourceKind::Schematic:    return QColor(QStringLiteral("#1D4ED8"));
    }
    return QColor(QStringLiteral("#047857"));
}

QString curseForgeUrl(ResourceKind kind)
{
    switch (kind)
    {
    case ResourceKind::Mod:          return QStringLiteral("https://www.curseforge.com/minecraft/mc-mods");
    case ResourceKind::Modpack:      return QStringLiteral("https://www.curseforge.com/minecraft/modpacks");
    case ResourceKind::ResourcePack: return QStringLiteral("https://www.curseforge.com/minecraft/texture-packs");
    case ResourceKind::ShaderPack:   return QStringLiteral("https://www.curseforge.com/minecraft/shaders");
    case ResourceKind::DataPack:     return QStringLiteral("https://www.curseforge.com/minecraft/data-packs");
    case ResourceKind::World:        return QStringLiteral("https://www.curseforge.com/minecraft/worlds");
    // 插件是方块盒子插件，在社区插件发布页发布，不跳 CurseForge
    case ResourceKind::Plugin:       return QString();
    // 投影在社区投影分享板块站内发布，不跳 CurseForge
    case ResourceKind::Schematic:    return QString();
    }
    return QString();
}

QString modrinthUrl(ResourceKind kind)
{
    switch (kind)
    {
    case ResourceKind::Mod:          return QStringLiteral("https://modrinth.com/mods");
    case ResourceKind::Modpack:      return QStringLiteral("https://modrinth.com/modpacks");
    case ResourceKind::ResourcePack: return QStringLiteral("https://modrinth.com/resourcepacks");
    case ResourceKind::ShaderPack:   return QStringLiteral("https://modrinth.com/shaders");
    case ResourceKind::DataPack:     return QStringLiteral("https://modrinth.com/datapacks");
    case ResourceKind::World:        return QString();  // Modrinth 没有「世界」分类
    // 插件是方块盒子插件，在社区插件发布页发布，不跳 Modrinth
    case ResourceKind::Plugin:       return QString();
    // 投影在社区投影分享板块站内发布，不跳 Modrinth
    case ResourceKind::Schematic:    return QString();
    }
    return QString();
}

QPixmap makeGradientIconPixmap(const QColor &c1, const QColor &c2,
                               const QString &iconPath, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal radius = size * 0.23;

    // 底部柔和阴影（与资源页图标块同一套观感）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 30));
    p.drawRoundedRect(QRectF(0, 2.0, size, size), radius, radius);

    // 135° 渐变背景
    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0.0, c1);
    grad.setColorAt(1.0, c2);
    p.setBrush(grad);
    p.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

    // 白色图标居中。内置图标是 SVG（按主题色/白色重新渲染），
    // 插件图标多为 PNG：按原图等比缩放后居中，不做染色
    const int iconSize = qRound(size * 0.5);
    const bool rasterIcon = !iconPath.isEmpty()
        && QFileInfo(iconPath).suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) != 0;
    QPixmap iconPix;
    if (rasterIcon)
    {
        const QPixmap src(iconPath);
        if (!src.isNull())
            iconPix = src.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    else
    {
        const QIcon icon = IconHelper::loadColoredIcon(iconPath, Qt::white, iconSize);
        iconPix = icon.pixmap(iconSize, iconSize);
    }
    if (!iconPix.isNull())
    {
        const int x = (size - iconPix.width()) / 2;
        const int y = (size - iconPix.height()) / 2;
        p.drawPixmap(x, y, iconPix);
    }
    p.end();

    return pix;
}

ForumKit::ClickableCard *makeChoiceCard(QWidget *parent, const QString &title,
                                        const QString &desc, const QColor &grad1,
                                        const QColor &grad2, const QString &iconPath,
                                        int width, int height)
{
    auto *card = new ForumKit::ClickableCard(parent);
    card->setObjectName(QStringLiteral("resourcesCard"));
    card->setFixedSize(width, height);
    card->setCursor(Qt::PointingHandCursor);

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(14);

    const int iconSize = qBound(44, height - 28, 64);
    auto *iconBlock = new QLabel(card);
    iconBlock->setFixedSize(iconSize, iconSize);
    iconBlock->setPixmap(makeGradientIconPixmap(grad1, grad2, iconPath, iconSize));
    layout->addWidget(iconBlock, 0, Qt::AlignVCenter);

    auto *body = new QWidget(card);
    body->setAttribute(Qt::WA_StyledBackground, false);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(4);

    auto *titleLabel = new QLabel(title, body);
    titleLabel->setObjectName(QStringLiteral("resourcesCardTitle"));
    bodyLayout->addWidget(titleLabel);

    auto *descLabel = new QLabel(desc, body);
    descLabel->setObjectName(QStringLiteral("resourcesCardDesc"));
    descLabel->setWordWrap(true);
    bodyLayout->addWidget(descLabel);
    bodyLayout->addStretch(1);
    layout->addWidget(body, 1);

    auto *arrowLabel = new QLabel(QString::fromUtf8("\u276F"), card);
    arrowLabel->setObjectName(QStringLiteral("resourceArrow"));
    layout->addWidget(arrowLabel, 0, Qt::AlignVCenter);

    return card;
}

QWidget *makeSectionTitle(QWidget *parent, const QString &text)
{
    auto *label = new OutlinedLabel(text, parent);
    label->setObjectName(QStringLiteral("resourcesSectionTitle"));
    return label;
}

} // namespace PublishKit
