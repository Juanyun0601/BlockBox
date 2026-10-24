/**
 * @file   NewsCard.cpp
 * @brief  首页「官方新闻」卡片实现
 * @author BlockBox Team
 * @date   2026-10-24
 */

#include "NewsCard.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QVBoxLayout>
#include <QtMath>

namespace {

// 卡片内边距与行间距（与构造函数里的布局保持一致）
constexpr int kPaddingLeft = 12;
constexpr int kPaddingRight = 12;
constexpr int kPaddingTop = 10;
constexpr int kPaddingBottom = 10;
constexpr int kRowSpacing = 6;
// 分类标签固定高度
constexpr int kChipHeight = 20;
// 标题最多展示的字符数，超出截断，避免个别长文把卡片撑得过高
constexpr int kMaxTitleChars = 90;

QString clampText(const QString &text, int maxChars)
{
  if (text.size() <= maxChars)
    return text;

  // 尽量在单词边界处截断，避免出现半截单词
  QString clipped = text.left(maxChars);
  const int lastSpace = clipped.lastIndexOf(QLatin1Char(' '));
  if (lastSpace > maxChars / 2)
    clipped = clipped.left(lastSpace);
  return clipped.trimmed() + QStringLiteral("…");
}

/// 按给定宽度换行后文字的实际高度（与 QLabel 的绘制方式一致，避免行距被低估）
int wrappedTextHeight(const QString &text, const QFont &font, int width)
{
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

  QTextLayout layout(text, font);
  layout.setTextOption(option);

  qreal height = 0;
  layout.beginLayout();
  for (;;)
  {
    QTextLine line = layout.createLine();
    if (!line.isValid())
      break;
    line.setLineWidth(width);
    line.setPosition(QPointF(0, height));
    height += line.height();
  }
  layout.endLayout();

  return qCeil(height) + 2;
}

} // namespace

NewsCard::NewsCard(const QString &category, const QString &title, const QString &date,
                   const QString &summary, QWidget *parent, int summaryChars)
    : QWidget(parent)
{
  setObjectName("newsCard");
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  // 自定义 QWidget 需显式开启样式表背景，否则 QWidget#newsCard 的底色/描边不会绘制
  setAttribute(Qt::WA_StyledBackground, true);

  // 字体在这里显式指定：卡片高度依赖字体度量，不能等到样式表生效后才确定
  QFont titleFont = font();
  titleFont.setPixelSize(14);
  titleFont.setWeight(QFont::DemiBold);

  QFont summaryFont = font();
  summaryFont.setPixelSize(12);

  QFont metaFont = font();
  metaFont.setPixelSize(11);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPaddingLeft, kPaddingTop, kPaddingRight, kPaddingBottom);
  layout->setSpacing(kRowSpacing);

  // ── 第一行：分类标签 + 日期 ──
  auto *metaRow = new QHBoxLayout();
  metaRow->setContentsMargins(0, 0, 0, 0);
  metaRow->setSpacing(8);

  if (!category.trimmed().isEmpty())
  {
    auto *chip = new QWidget(this);
    chip->setObjectName("newsCategoryChip");
    chip->setFixedHeight(kChipHeight);
    chip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *chipLayout = new QHBoxLayout(chip);
    chipLayout->setContentsMargins(8, 0, 8, 0);
    chipLayout->setSpacing(0);

    m_categoryLabel = new QLabel(category, chip);
    m_categoryLabel->setObjectName("newsCategoryText");
    m_categoryLabel->setTextFormat(Qt::PlainText);
    m_categoryLabel->setFont(metaFont);
    chipLayout->addWidget(m_categoryLabel);

    metaRow->addWidget(chip, 0, Qt::AlignVCenter);
  }

  m_dateLabel = new QLabel(date, this);
  m_dateLabel->setObjectName("newsDate");
  m_dateLabel->setTextFormat(Qt::PlainText);
  m_dateLabel->setFont(metaFont);
  metaRow->addWidget(m_dateLabel, 0, Qt::AlignVCenter);
  metaRow->addStretch();
  layout->addLayout(metaRow);

  // ── 标题 ──
  m_titleLabel = new QLabel(clampText(title, kMaxTitleChars), this);
  m_titleLabel->setObjectName("newsTitle");
  m_titleLabel->setTextFormat(Qt::PlainText);
  m_titleLabel->setFont(titleFont);
  m_titleLabel->setWordWrap(true);
  m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  layout->addWidget(m_titleLabel);

  // ── 摘要（官方未提供时整行隐藏） ──
  const QString summaryText = clampText(summary, qMax(40, summaryChars));
  m_summaryLabel = new QLabel(summaryText, this);
  m_summaryLabel->setObjectName("newsSummary");
  m_summaryLabel->setTextFormat(Qt::PlainText);
  m_summaryLabel->setFont(summaryFont);
  m_summaryLabel->setWordWrap(true);
  m_summaryLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_summaryLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  m_summaryLabel->setVisible(!summaryText.isEmpty());
  layout->addWidget(m_summaryLabel);
}

QSize NewsCard::contentSizeForWidth(int width)
{
  if (width <= 0)
    return QSize(0, 0);

  // 减去左右内边距与 1px 边框，得到文字真正可用的宽度
  const int contentWidth = qMax(60, width - kPaddingLeft - kPaddingRight - 2);

  const QFontMetrics metaMetrics(m_dateLabel->font());
  const int metaHeight = qMax(kChipHeight, metaMetrics.height());

  // 标题 / 摘要：先按实际宽度算出换行后的高度，再把标签固定到该尺寸。
  // 这样 QVBoxLayout 不会用「未换行」的 sizeHint 去分配高度，文字也就不会被裁掉。
  const int titleHeight = wrappedTextHeight(m_titleLabel->text(), m_titleLabel->font(), contentWidth);
  m_titleLabel->setFixedSize(contentWidth, titleHeight);

  int summaryHeight = 0;
  if (!m_summaryLabel->isHidden() && !m_summaryLabel->text().isEmpty())
  {
    summaryHeight = wrappedTextHeight(m_summaryLabel->text(), m_summaryLabel->font(), contentWidth);
    m_summaryLabel->setFixedSize(contentWidth, summaryHeight);
  }

  m_dateLabel->setFixedHeight(metaHeight);

  const int totalHeight = kPaddingTop + kPaddingBottom + metaHeight + kRowSpacing + titleHeight
                          + (summaryHeight > 0 ? kRowSpacing + summaryHeight : 0)
                          + 2;   // 1px 边框 × 2
  return QSize(width, totalHeight);
}

void NewsCard::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
  {
    emit clicked();
  }
  QWidget::mousePressEvent(event);
}
