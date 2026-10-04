#pragma once

#include <QLabel>
#include <QWidget>

class QVBoxLayout;

class NewsCard : public QWidget
{
  Q_OBJECT

public:
  explicit NewsCard(const QString &title, const QString &date, const QString &summary,
                    QWidget *parent = nullptr);

signals:
  void clicked();

protected:
  void mousePressEvent(QMouseEvent *event) override;

private:
  QLabel *m_titleLabel;
  QLabel *m_dateLabel;
  QLabel *m_summaryLabel;
};