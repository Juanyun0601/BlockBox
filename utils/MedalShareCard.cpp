#include "MedalShareCard.h"

#include "MedalCatalog.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace {

// ── 版式常量（都按 1080 宽的画布算，横向留白 72） ──
constexpr int kWidth = 1080;
constexpr int kMargin = 72;
constexpr int kContentWidth = kWidth - kMargin * 2;

constexpr int kCols = 4;
constexpr int kColGap = 16;
constexpr int kCellWidth = (kContentWidth - kColGap * (kCols - 1)) / kCols;
constexpr int kCellHeight = 158;
constexpr int kRowGap = 12;
constexpr int kGridTop = 730;
constexpr int kEmptyPanelHeight = 300;
/** 最多铺 4 行；再多的用最后一格写「还有 N 枚」，免得分享图拉成一条长图 */
constexpr int kMaxCells = 16;

const QColor kInk(0xF2, 0xFA, 0xF5);
const QColor kInkSoft(0xE6, 0xF5, 0xEB, 178);
const QColor kInkMuted(0xE6, 0xF5, 0xEB, 120);
const QColor kCardFill(255, 255, 255, 16);
const QColor kCardBorder(255, 255, 255, 26);

QColor withAlpha(const QColor &color, int alpha)
{
    QColor out = color;
    out.setAlpha(alpha);
    return out;
}

/** 分享图固定深绿底，点缀色取主题绿里偏亮的那一档（浅色主题的墨绿看不清） */
QColor accentFor(const QColor &brand)
{
    if (brand.isValid() && brand.lightnessF() > 0.55)
        return brand;
    return QColor(0x48, 0xC9, 0x7E);
}

/** 深色底上的稀有度色：比卡片标签用的原色再亮一点，小红字也看得清 */
QColor rarityAccent(const QString &rarity)
{
    if (rarity == QLatin1String("legendary"))
        return QColor(0xF5, 0xB0, 0x3A);
    if (rarity == QLatin1String("mythic"))
        return QColor(0xFF, 0x76, 0x76);
    if (rarity == QLatin1String("epic"))
        return QColor(0xB2, 0x8B, 0xFF);
    if (rarity == QLatin1String("rare"))
        return QColor(0x74, 0xB9, 0xFF);
    if (rarity == QLatin1String("game"))
        return QColor(0x62, 0xD4, 0xE2);
    return QColor(0x8D, 0xE0, 0xA8);
}

QString rarityLabel(const QString &rarity)
{
    if (rarity == QLatin1String("game"))
        return QStringLiteral("成就");
    return MedalCatalog::rarityText(rarity);
}

QFont pixelFont(int pixelSize, int weight = QFont::Normal)
{
    QFont font = QFont();   // 跟着软件当前字体走，中文不出方框
    font.setPixelSize(pixelSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

void drawTextElided(QPainter &painter, const QRectF &rect, int flags, const QString &text,
                    const QColor &color)
{
    if (text.isEmpty())
        return;
    painter.setPen(color);
    const QFontMetricsF metrics(painter.font());
    const QString shown = metrics.elidedText(text, Qt::ElideRight, rect.width());
    painter.drawText(rect, flags, shown);
}

/** 圆形头像：有原图就裁圆画原图，没有就按昵称画绿底首字 */
void paintAvatar(QPainter &painter, const QRectF &rect, const QPixmap &source,
                 const QString &name, const QColor &accent)
{
    QPainterPath circle;
    circle.addEllipse(rect);
    painter.save();
    painter.setClipPath(circle);
    if (!source.isNull())
    {
        const QPixmap scaled = source.scaled(rect.size().toSize(), Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation);
        painter.drawPixmap(QPointF(rect.center().x() - scaled.width() / 2.0,
                                   rect.center().y() - scaled.height() / 2.0),
                           scaled);
    }
    else
    {
        QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
        gradient.setColorAt(0.0, accent);
        gradient.setColorAt(1.0, accent.darker(135));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRect(rect);

        const QString trimmed = name.trimmed();
        QFont font = pixelFont(qMax(20, int(rect.width() * 0.42)), QFont::Bold);
        painter.setFont(font);
        painter.setPen(QColor(255, 255, 255, 236));
        painter.drawText(rect, Qt::AlignCenter,
                         trimmed.isEmpty() ? QStringLiteral("?") : trimmed.left(1).toUpper());
    }
    painter.restore();
}

/** 背景：深绿渐变 + 柔光 + 淡淡的方块网格，像一块竖起来的方块墙面 */
void paintBackground(QPainter &painter, int height, const QColor &accent,
                     const QPixmap &watermark)
{
    QLinearGradient base(0, 0, kWidth * 0.28, height);
    base.setColorAt(0.0, QColor(0x0C, 0x3A, 0x2A));
    base.setColorAt(0.55, QColor(0x08, 0x28, 0x1D));
    base.setColorAt(1.0, QColor(0x04, 0x14, 0x0E));
    painter.fillRect(QRectF(0, 0, kWidth, height), base);

    QRadialGradient glow(QPointF(kWidth * 0.82, height * 0.04), kWidth * 0.92);
    glow.setColorAt(0.0, withAlpha(accent, 66));
    glow.setColorAt(0.55, withAlpha(accent, 20));
    glow.setColorAt(1.0, withAlpha(accent, 0));
    painter.fillRect(QRectF(0, 0, kWidth, height), glow);

    QRadialGradient cool(QPointF(kWidth * 0.06, height * 0.86), kWidth * 0.7);
    cool.setColorAt(0.0, QColor(0x1E, 0x5C, 0x46, 70));
    cool.setColorAt(1.0, QColor(0x1E, 0x5C, 0x46, 0));
    painter.fillRect(QRectF(0, 0, kWidth, height), cool);

    // 砖块状网格：错行铺 72px 圆角方块，只当纹理，不能抢文字
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 7), 1));
    for (int y = 0; y < height; y += 72)
    {
        const int offset = ((y / 72) % 2 == 0) ? 0 : 36;
        for (int x = -72 + offset; x < kWidth; x += 72)
            painter.drawRoundedRect(QRectF(x + 4, y + 4, 64, 64), 8, 8);
    }
    painter.restore();

    // 右下角压一枚放大的勋章当水印：用「加色」混合，只提亮、不会在深底上留下一块脏影
    if (!watermark.isNull())
    {
        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
        painter.setOpacity(0.06);
        painter.translate(kWidth - 24, height - 14);
        painter.rotate(-12);
        const int size = 520;
        painter.drawPixmap(QRectF(-size * 0.76, -size * 1.02, size, size), watermark,
                           QRectF(watermark.rect()));
        painter.restore();
    }
}

} // namespace

namespace MedalShareCard {

QImage buildPoster(const Data &data)
{
    const QColor accent = accentFor(data.brand);

    // ── 先按条数算高度：4 列一行，多出来的收在最后一格 ──
    const int itemCount = data.items.size();
    const int cellCount = qMin(itemCount, kMaxCells);
    const int rows = itemCount == 0 ? 0 : qMax(1, (cellCount + kCols - 1) / kCols);
    const int gridHeight = itemCount == 0
                               ? kEmptyPanelHeight
                               : rows * kCellHeight + qMax(0, rows - 1) * kRowGap;
    const int gridBottom = kGridTop + gridHeight;

    // 勋章不多时补一条「下一枚目标」，把下方空白填起来，也让分享图更有内容
    constexpr int kNextStripHeight = 96;
    constexpr int kNextStripGap = 40;
    const bool hasNextTarget =
        itemCount > 0 && !data.next.name.trimmed().isEmpty() && rows < (kMaxCells / kCols);

    int height = gridBottom + 200;
    if (hasNextTarget)
        height += kNextStripGap + kNextStripHeight;
    height = qMax(height, 1320);

    QImage image(kWidth, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0x04, 0x14, 0x0E));

    // 水印用最稀有的那一枚（没有勋章时用默认的「方块新芽」图标）
    QString watermarkId = QStringLiteral("newcomer");
    int bestRank = -1;
    for (const Item &item : data.items)
    {
        const int rank = item.rarity == QLatin1String("mythic")   ? 5
                         : item.rarity == QLatin1String("legendary") ? 4
                         : item.rarity == QLatin1String("epic")    ? 3
                         : item.rarity == QLatin1String("rare")    ? 2
                         : item.rarity == QLatin1String("game")    ? 0
                                                                   : 1;
        if (rank > bestRank)
        {
            bestRank = rank;
            watermarkId = item.iconId;
        }
    }
    const QPixmap watermark = MedalCatalog::pixmapFor(watermarkId, 560);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    paintBackground(painter, height, accent, watermark);

    // 顶部一条亮绿细线，截图发出去有个「证件卡」的边框感
    painter.fillRect(QRectF(0, 0, kWidth, 5), withAlpha(accent, 210));

    // ── 头部：深色版品牌组合（绿方块 + 白字），深绿底上才看得清 ──
    const QPixmap lockup(QStringLiteral(":/Images/blockbox_icon_dark.png"));
    const QPixmap logo(QStringLiteral(":/Images/logo.png"));
    const qreal lockupHeight = 52;
    const qreal lockupTop = 58;
    qreal lockupRight = kMargin;
    if (!lockup.isNull())
    {
        // 组合 logo 本身是横版，等比缩放，不能塞进正方形里压扁
        const qreal lockupWidth = lockupHeight * lockup.width() / qMax(1, lockup.height());
        const QRectF lockupRect(kMargin, lockupTop, lockupWidth, lockupHeight);
        painter.drawPixmap(lockupRect, lockup, QRectF(lockup.rect()));
        lockupRight = lockupRect.right();
    }
    else if (!logo.isNull())
    {
        const qreal logoWidth = lockupHeight * logo.width() / qMax(1, logo.height());
        const QRectF logoRect(kMargin, lockupTop, logoWidth, lockupHeight);
        painter.drawPixmap(logoRect, logo, QRectF(logo.rect()));
        lockupRight = logoRect.right();
    }
    else
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(accent, 200));
        painter.drawRoundedRect(QRectF(kMargin, lockupTop, lockupHeight, lockupHeight), 12, 12);
        lockupRight = kMargin + lockupHeight;
    }

    const qreal brandCenterY = lockupTop + lockupHeight / 2.0;
    QFont chipFont = pixelFont(21, QFont::Bold);
    painter.setFont(chipFont);
    const QFontMetricsF chipMetrics(chipFont);
    const QString chipText = QStringLiteral("勋章墙");
    const qreal chipWidth = chipMetrics.horizontalAdvance(chipText) + 30;
    const QRectF chipRect(lockupRight + 16, brandCenterY - 18, chipWidth, 36);
    painter.setPen(QPen(withAlpha(accent, 110), 1));
    painter.setBrush(withAlpha(accent, 38));
    painter.drawRoundedRect(chipRect, 18, 18);
    painter.setPen(accent);
    painter.drawText(chipRect, Qt::AlignCenter, chipText);

    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    painter.setFont(pixelFont(22));
    painter.setPen(kInkMuted);
    painter.drawText(QRectF(kWidth - kMargin - 300, brandCenterY - 16, 300, 32),
                     Qt::AlignRight | Qt::AlignVCenter, today);

    painter.fillRect(QRectF(kMargin, 158, kContentWidth, 1), QColor(255, 255, 255, 24));

    // ── 账号：头像 + 昵称 + 账号名 / 注册时间 ──
    const qreal avatarSize = 132;
    const QRectF avatarRect(kMargin, 206, avatarSize, avatarSize);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(withAlpha(accent, 120), 3));
    painter.drawEllipse(avatarRect.adjusted(-8, -8, 8, 8));

    QString shownName = data.displayName.trimmed();
    if (shownName.isEmpty())
        shownName = data.username.trimmed();
    if (shownName.isEmpty())
        shownName = QStringLiteral("方块盒子玩家");
    paintAvatar(painter, avatarRect, data.avatar, shownName, accent);

    const qreal textLeft = avatarRect.right() + 32;
    const qreal textWidth = kWidth - kMargin - textLeft;

    QStringList meta;
    if (!data.username.trimmed().isEmpty())
        meta << QStringLiteral("@%1").arg(data.username.trimmed());
    if (data.joinedAt > 0)
        meta << QStringLiteral("加入于 %1").arg(QDateTime::fromSecsSinceEpoch(data.joinedAt)
                                                   .toString(QStringLiteral("yyyy-MM-dd")));

    // 没有账号名 / 注册时间时，昵称在两行的位置里居中，别让头像右边空出一大块
    const QRectF nameRect =
        meta.isEmpty() ? QRectF(textLeft, 232, textWidth, 62)
                       : QRectF(textLeft, 214, textWidth, 62);
    painter.setFont(pixelFont(46, QFont::Bold));
    drawTextElided(painter, nameRect, Qt::AlignLeft | Qt::AlignVCenter, shownName, kInk);

    if (!meta.isEmpty())
    {
        painter.setFont(pixelFont(24));
        drawTextElided(painter, QRectF(textLeft, 288, textWidth, 36),
                       Qt::AlignLeft | Qt::AlignVCenter, meta.join(QStringLiteral("  ·  ")),
                       kInkSoft);
    }

    // ── 数字：已点亮 / 总数，右侧单列游戏成就 ──
    QFont bigFont = pixelFont(128, QFont::Black);
    painter.setFont(bigFont);
    const QFontMetricsF bigMetrics(bigFont);
    const qreal bigBaseline = 400 + bigMetrics.ascent();
    const QString earnedText = QString::number(qMax(0, data.earned));
    painter.setPen(kInk);
    painter.drawText(QPointF(kMargin, bigBaseline), earnedText);
    const qreal earnedWidth = bigMetrics.horizontalAdvance(earnedText);

    QFont totalFont = pixelFont(34, QFont::Bold);
    painter.setFont(totalFont);
    const QFontMetricsF totalMetrics(totalFont);
    painter.setPen(kInkMuted);
    painter.drawText(QPointF(kMargin + earnedWidth + 16,
                             bigBaseline - (bigMetrics.descent() - totalMetrics.descent())),
                     QStringLiteral("/ %1 枚").arg(qMax(0, data.total)));

    const qreal rightEdge = kWidth - kMargin;
    painter.setFont(pixelFont(44, QFont::Bold));
    painter.setPen(accent);
    painter.drawText(QRectF(rightEdge - 260, bigBaseline - 78, 260, 56),
                     Qt::AlignRight | Qt::AlignBottom, QString::number(qMax(0, data.gameEarned)));
    painter.setFont(pixelFont(22));
    painter.setPen(kInkMuted);
    painter.drawText(QRectF(rightEdge - 260, bigBaseline - 20, 260, 34),
                     Qt::AlignRight | Qt::AlignTop, QStringLiteral("游戏成就"));

    // ── 完成度进度条 ──
    const qreal barY = 566;
    const qreal barHeight = 14;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 30));
    painter.drawRoundedRect(QRectF(kMargin, barY, kContentWidth, barHeight), 7, 7);

    const qreal ratio = data.total > 0
                            ? qBound(0.0, static_cast<double>(data.earned) / data.total, 1.0)
                            : 0.0;
    if (ratio > 0.0)
    {
        const qreal filled = qMax(barHeight, kContentWidth * ratio);
        QLinearGradient fill(kMargin, barY, kMargin + filled, barY);
        fill.setColorAt(0.0, accent.darker(115));
        fill.setColorAt(1.0, accent.lighter(118));
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(kMargin, barY, filled, barHeight), 7, 7);
    }

    painter.setFont(pixelFont(22));
    painter.setPen(kInkMuted);
    painter.drawText(QRectF(kMargin, barY + 24, 420, 32), Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("整墙完成度 %1%").arg(qRound(ratio * 100)));
    const QString remainText =
        ratio >= 1.0
            ? QStringLiteral("勋章墙已全部点亮")
            : QStringLiteral("还差 %1 枚点亮整墙").arg(qMax(0, data.total - data.earned));
    painter.drawText(QRectF(kWidth - kMargin - 420, barY + 24, 420, 32),
                     Qt::AlignRight | Qt::AlignTop, remainText);

    // ── 勋章区标题 ──
    painter.setFont(pixelFont(30, QFont::Bold));
    painter.setPen(kInk);
    painter.drawText(QPointF(kMargin, 690), QStringLiteral("已点亮勋章"));
    painter.setFont(pixelFont(22));
    painter.setPen(kInkMuted);
    const QString sectionMeta =
        itemCount == 0
            ? QStringLiteral("点亮后会记在这里")
            : QStringLiteral("论坛勋章 %1 枚 · 游戏成就 %2 个")
                  .arg(qMax(0, data.forumEarned))
                  .arg(qMax(0, data.gameEarned));
    painter.drawText(QRectF(kMargin + 220, 664, kContentWidth - 220, 32),
                     Qt::AlignRight | Qt::AlignVCenter, sectionMeta);

    // ── 勋章卡片瀑布：4 列，最多 4 行 ──
    if (itemCount == 0)
    {
        const QRectF panel(kMargin, kGridTop, kContentWidth, kEmptyPanelHeight);
        painter.setPen(QPen(kCardBorder, 1));
        painter.setBrush(kCardFill);
        painter.drawRoundedRect(panel, 26, 26);

        const QPixmap emptyIcon = MedalCatalog::pixmapFor(QStringLiteral("newcomer"), 116);
        if (!emptyIcon.isNull())
        {
            painter.save();
            painter.setOpacity(0.62);
            painter.drawPixmap(QPointF(panel.center().x() - 58, panel.top() + 28), emptyIcon);
            painter.restore();
        }

        painter.setFont(pixelFont(30, QFont::Bold));
        painter.setPen(kInk);
        painter.drawText(QRectF(panel.left(), panel.top() + 160, panel.width(), 40),
                         Qt::AlignCenter, QStringLiteral("还没有点亮勋章"));
        painter.setFont(pixelFont(22));
        painter.setPen(kInkMuted);
        painter.drawText(QRectF(panel.left(), panel.top() + 208, panel.width(), 40),
                         Qt::AlignCenter,
                         QStringLiteral("去社区发帖、答题或签到，点亮你的第一枚勋章"));
        painter.setFont(pixelFont(20));
        painter.drawText(QRectF(panel.left(), panel.top() + 246, panel.width(), 32),
                         Qt::AlignCenter,
                         QStringLiteral("勋章记在方块盒子账号上，换设备也不会丢"));
    }
    else
    {
        const bool overflow = itemCount > kMaxCells;
        const int drawnItems = overflow ? kMaxCells - 1 : itemCount;

        for (int index = 0; index < drawnItems; ++index)
        {
            const Item &item = data.items.at(index);
            const int row = index / kCols;
            const int col = index % kCols;
            const QRectF cell(kMargin + col * (kCellWidth + kColGap),
                              kGridTop + row * (kCellHeight + kRowGap), kCellWidth, kCellHeight);

            const QColor rarityColor = rarityAccent(item.rarity);
            const bool shiny = item.rarity == QLatin1String("legendary")
                               || item.rarity == QLatin1String("mythic");
            painter.setPen(QPen(shiny ? withAlpha(rarityColor, 96) : kCardBorder, 1));
            painter.setBrush(kCardFill);
            painter.drawRoundedRect(cell, 20, 20);

            const qreal iconSize = 56;
            if (shiny)
            {
                QRadialGradient halo(QPointF(cell.center().x(), cell.top() + 50), 56);
                halo.setColorAt(0.0, withAlpha(rarityColor, 66));
                halo.setColorAt(1.0, withAlpha(rarityColor, 0));
                painter.setPen(Qt::NoPen);
                painter.setBrush(halo);
                painter.drawEllipse(QPointF(cell.center().x(), cell.top() + 50), 56, 56);
            }

            const QPixmap icon = item.game ? MedalCatalog::gamePixmap(item.iconId, 56)
                                           : MedalCatalog::pixmapFor(item.iconId, 56);
            if (!icon.isNull())
                painter.drawPixmap(QPointF(cell.center().x() - iconSize / 2.0, cell.top() + 20),
                                   icon);

            painter.setFont(pixelFont(22, QFont::Bold));
            drawTextElided(painter, QRectF(cell.left() + 12, cell.top() + 84, cell.width() - 24, 32),
                           Qt::AlignHCenter | Qt::AlignVCenter, item.name, kInk);

            painter.setFont(pixelFont(17, QFont::Bold));
            painter.setPen(rarityColor);
            painter.drawText(QRectF(cell.left() + 10, cell.top() + 118, cell.width() - 20, 26),
                             Qt::AlignHCenter | Qt::AlignVCenter, rarityLabel(item.rarity));
        }

        if (overflow)
        {
            const int index = drawnItems;
            const int row = index / kCols;
            const int col = index % kCols;
            const QRectF cell(kMargin + col * (kCellWidth + kColGap),
                              kGridTop + row * (kCellHeight + kRowGap), kCellWidth, kCellHeight);
            painter.setPen(QPen(kCardBorder, 1));
            painter.setBrush(QColor(255, 255, 255, 10));
            painter.drawRoundedRect(cell, 20, 20);

            // 三张叠起来的卡片，暗示「后面还有」
            const qreal stackWidth = 30;
            const qreal stackHeight = 22;
            const qreal stackTop = cell.top() + 16;
            for (int layer = 2; layer >= 0; --layer)
            {
                const QRectF layerRect(cell.center().x() - stackWidth / 2.0,
                                       stackTop + layer * 6, stackWidth, stackHeight);
                painter.setPen(QPen(withAlpha(accent, 90), 1.2));
                painter.setBrush(withAlpha(accent, 16 + layer * 10));
                painter.drawRoundedRect(layerRect, 6, 6);
            }

            painter.setFont(pixelFont(38, QFont::Black));
            painter.setPen(withAlpha(accent, 225));
            painter.drawText(QRectF(cell.left(), cell.top() + 62, cell.width(), 52),
                             Qt::AlignCenter,
                             QStringLiteral("+%1").arg(itemCount - drawnItems));
            painter.setFont(pixelFont(19));
            painter.setPen(kInkMuted);
            painter.drawText(QRectF(cell.left(), cell.top() + 112, cell.width(), 28),
                             Qt::AlignCenter, QStringLiteral("还有这些勋章"));
        }
    }

    // ── 下一枚目标：勋章不多时把下半张图填起来 ──
    if (hasNextTarget)
    {
        const QRectF strip(kMargin, gridBottom + kNextStripGap, kContentWidth, kNextStripHeight);
        painter.setPen(QPen(withAlpha(accent, 72), 1));
        painter.setBrush(QColor(255, 255, 255, 12));
        painter.drawRoundedRect(strip, 22, 22);

        const QPixmap nextIcon = data.next.game
                                     ? MedalCatalog::gamePixmap(data.next.iconId, 52)
                                     : MedalCatalog::pixmapFor(data.next.iconId, 52);
        if (!nextIcon.isNull())
            painter.drawPixmap(QPointF(strip.left() + 22, strip.top() + 22), nextIcon);

        painter.setFont(pixelFont(18, QFont::Bold));
        painter.setPen(withAlpha(accent, 220));
        painter.drawText(QRectF(strip.left() + 94, strip.top() + 20, 420, 26),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("下一枚目标"));

        painter.setFont(pixelFont(26, QFont::Bold));
        painter.setPen(kInk);
        painter.drawText(QRectF(strip.left() + 94, strip.top() + 46, strip.width() - 440, 36),
                         Qt::AlignLeft | Qt::AlignVCenter, data.next.name);

        if (!data.nextProgress.trimmed().isEmpty())
        {
            painter.setFont(pixelFont(19));
            painter.setPen(kInkMuted);
            painter.drawText(QRectF(strip.right() - 360, strip.top() + 30, 338, 40),
                             Qt::AlignRight | Qt::AlignVCenter,
                             data.nextProgress.trimmed());
        }
    }

    // ── 底部：品牌 + 论坛地址 ──
    const qreal footerLineY = height - 150;
    painter.fillRect(QRectF(kMargin, footerLineY, kContentWidth, 1), QColor(255, 255, 255, 22));

    const qreal footerLockupHeight = 30;
    const qreal footerLockupTop = footerLineY + 30;
    if (!lockup.isNull())
    {
        const qreal lockupWidth =
            footerLockupHeight * lockup.width() / qMax(1, lockup.height());
        painter.drawPixmap(QRectF(kMargin, footerLockupTop, lockupWidth, footerLockupHeight),
                           lockup, QRectF(lockup.rect()));
    }
    else if (!logo.isNull())
    {
        const qreal logoWidth =
            footerLockupHeight * logo.width() / qMax(1, logo.height());
        painter.drawPixmap(QRectF(kMargin, footerLockupTop, logoWidth, footerLockupHeight), logo,
                           QRectF(logo.rect()));
    }
    else
    {
        painter.setFont(pixelFont(24, QFont::Bold));
        painter.setPen(kInk);
        painter.drawText(QPointF(kMargin, footerLockupTop + 24),
                         QStringLiteral("方块盒子 BlockBox"));
    }

    painter.setFont(pixelFont(19));
    painter.setPen(kInkMuted);
    painter.drawText(QPointF(kMargin, footerLockupTop + footerLockupHeight + 26),
                     QStringLiteral("一个盒子装下整个方块世界"));

    if (!data.siteUrl.trimmed().isEmpty())
    {
        painter.setFont(pixelFont(21));
        painter.setPen(kInkSoft);
        painter.drawText(QRectF(kWidth - kMargin - 520, footerLockupTop - 2, 520, 30),
                         Qt::AlignRight | Qt::AlignVCenter, data.siteUrl.trimmed());
    }
    painter.setFont(pixelFont(19));
    painter.setPen(kInkMuted);
    painter.drawText(QRectF(kWidth - kMargin - 520, footerLockupTop + 32, 520, 30),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("在启动器「账户 → 勋章」查看完整勋章墙"));

    painter.end();
    return image;
}

QString suggestedFileName()
{
    return QStringLiteral("方块盒子-勋章墙-%1.png")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
}

} // namespace MedalShareCard
