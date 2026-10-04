#include "NewsCard.h"

#include <QMouseEvent>
#include <QVBoxLayout>

NewsCard::NewsCard(const QString &title, const QString &date, const QString &summary,
                   QWidget *parent)
    : QWidget(parent)
{
  setObjectName("newsCard");
  setCursor(Qt::PointingHandCursor);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(4);

  m_titleLabel = new QLabel(title);
  m_titleLabel->setObjectName("newsTitle");
  m_titleLabel->setWordWrap(true);

  m_dateLabel = new QLabel(date);
  m_dateLabel->setObjectName("newsDate");

  m_summaryLabel = new QLabel(summary);
  m_summaryLabel->setObjectName("newsSummary");
  m_summaryLabel->setWordWrap(true);

  layout->addWidget(m_titleLabel);
  layout->addWidget(m_dateLabel);
  layout->addWidget(m_summaryLabel);
}

void NewsCard::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
  {
    emit clicked();
  }
  QWidget::mousePressEvent(event);
}