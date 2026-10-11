#include "OutlinedTextEdit.h"
#include "utils/ThemeManager.h"

#include <QPainter>
#include <QPaintEvent>

OutlinedTextEdit::OutlinedTextEdit(QWidget *parent)
    : QTextEdit(parent),
      m_outlineWidth(1),
      m_customColor(false),
      m_outlineEnabled(true)
{
    // 跟随主题文字边框颜色变化实时重绘
    connect(ThemeManager::instance(), &ThemeManager::textBorderColorChanged,
            this, [this](const QString &) {
        if (!m_customColor)
            update();
    });
}

int OutlinedTextEdit::outlineWidth() const
{
    return m_outlineWidth;
}

void OutlinedTextEdit::setOutlineWidth(int width)
{
    m_outlineWidth = qMax(1, width);
    update();
}

QColor OutlinedTextEdit::outlineColor() const
{
    if (m_customColor)
        return m_outlineColor;
    return QColor(ThemeManager::instance()->currentTextBorderColor());
}

void OutlinedTextEdit::setOutlineColor(const QColor &color)
{
    m_customColor = true;
    m_outlineColor = color;
    update();
}

bool OutlinedTextEdit::outlineEnabled() const
{
    return m_outlineEnabled;
}

void OutlinedTextEdit::setOutlineEnabled(bool enabled)
{
    m_outlineEnabled = enabled;
    if (!enabled)
        m_outlineLayer = QPixmap();
    update();
}

bool OutlinedTextEdit::outlineDirty() const
{
    TextOutlineLayer::CacheKey key;
    key.size = viewport()->size();
    key.outlineWidth = m_outlineWidth;
    key.color = outlineColor();
    key.dpr = devicePixelRatioF();
    key.docRevision = quint64(document()->revision());
    key.font = font();
    return m_outlineLayer.isNull() || key != m_cacheKey;
}

void OutlinedTextEdit::paintEvent(QPaintEvent *event)
{
    if (m_outlineEnabled)
    {
        // 文本/视口/描边参数变化后先重建描边层
        if (outlineDirty())
            renderOutlineLayer();

        // 描边层绘制在 viewport 正文之下（QTextEdit::paintEvent 亦画在 viewport 上）
        if (!m_outlineLayer.isNull())
        {
            QPainter p(viewport());
            p.drawPixmap(0, 0, m_outlineLayer);
            p.end();
        }
    }

    QTextEdit::paintEvent(event);
}

void OutlinedTextEdit::renderOutlineLayer()
{
    m_cacheKey.size = viewport()->size();
    m_cacheKey.outlineWidth = m_outlineWidth;
    m_cacheKey.color = outlineColor();
    m_cacheKey.dpr = devicePixelRatioF();
    m_cacheKey.docRevision = quint64(document()->revision());
    m_cacheKey.font = font();

    m_outlineLayer = QPixmap();

    const QImage textImg = TextOutlineLayer::renderTextEdit(this, viewport()->size());
    if (textImg.isNull())
        return;

    const QImage ring = TextOutlineLayer::buildOutlineRing(textImg, m_outlineWidth);
    m_outlineLayer = TextOutlineLayer::colorizeRing(ring, m_cacheKey.color, m_cacheKey.dpr);
}
