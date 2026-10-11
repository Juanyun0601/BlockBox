#include "MarkdownOutlinedLabel.h"
#include "utils/ThemeManager.h"

#include <QPainter>
#include <QPaintEvent>

MarkdownOutlinedLabel::MarkdownOutlinedLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent),
      m_outlineWidth(1),
      m_customColor(false)
{
    // 跟随主题文字边框颜色变化实时重绘
    connect(ThemeManager::instance(), &ThemeManager::textBorderColorChanged,
            this, [this](const QString &) {
        if (!m_customColor)
            update();
    });
}

int MarkdownOutlinedLabel::outlineWidth() const
{
    return m_outlineWidth;
}

void MarkdownOutlinedLabel::setOutlineWidth(int width)
{
    m_outlineWidth = qMax(1, width);
    update();
}

QColor MarkdownOutlinedLabel::outlineColor() const
{
    if (m_customColor)
        return m_outlineColor;
    return QColor(ThemeManager::instance()->currentTextBorderColor());
}

void MarkdownOutlinedLabel::setOutlineColor(const QColor &color)
{
    m_customColor = true;
    m_outlineColor = color;
    update();
}

bool MarkdownOutlinedLabel::outlineDirty() const
{
    TextOutlineLayer::CacheKey key;
    key.size = rect().size();
    key.outlineWidth = m_outlineWidth;
    key.color = outlineColor();
    key.dpr = devicePixelRatioF();
    key.text = text();
    key.font = font();
    key.textFormat = int(textFormat());
    return m_outlineLayer.isNull() || key != m_cacheKey;
}

void MarkdownOutlinedLabel::paintEvent(QPaintEvent *event)
{
    // 文本/尺寸/描边参数变化后先重建描边层
    if (outlineDirty())
        renderOutlineLayer();

    // 描边层绘制在正文之下
    if (!m_outlineLayer.isNull())
    {
        QPainter p(this);
        p.drawPixmap(0, 0, m_outlineLayer);
        p.end();
    }

    // 正文（含选中态/链接等）仍由 QLabel 默认绘制，保证样式与交互一致
    QLabel::paintEvent(event);
}

void MarkdownOutlinedLabel::renderOutlineLayer()
{
    m_cacheKey.size = rect().size();
    m_cacheKey.outlineWidth = m_outlineWidth;
    m_cacheKey.color = outlineColor();
    m_cacheKey.dpr = devicePixelRatioF();
    m_cacheKey.text = text();
    m_cacheKey.font = font();
    m_cacheKey.textFormat = int(textFormat());

    m_outlineLayer = QPixmap();

    const QImage textImg = TextOutlineLayer::renderLabelText(this, rect());
    if (textImg.isNull())
        return;

    const QImage ring = TextOutlineLayer::buildOutlineRing(textImg, m_outlineWidth);
    m_outlineLayer = TextOutlineLayer::colorizeRing(ring, m_cacheKey.color, m_cacheKey.dpr);
}
