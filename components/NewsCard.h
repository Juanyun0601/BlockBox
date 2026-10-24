#pragma once

#include <QLabel>
#include <QSize>
#include <QWidget>

class QVBoxLayout;

/**
 * @brief 「官方新闻」卡片
 *
 * 展示一条 Minecraft 官方新闻：分类标签、日期、标题、摘要；点击查看详情。
 * 卡片宽度由页面统一分配，高度通过 contentSizeForWidth() 预先算好，
 * 这样并排的卡片能保持整齐、文字不被裁切。
 */
class NewsCard : public QWidget
{
  Q_OBJECT

public:
  /// 摘要默认展示字符数（首页横排卡片用较短的摘要）
  static constexpr int kDefaultSummaryChars = 120;

  explicit NewsCard(const QString &category, const QString &title, const QString &date,
                    const QString &summary, QWidget *parent = nullptr,
                    int summaryChars = kDefaultSummaryChars);

  /// 按指定宽度排好内部文字，并返回卡片应占的实际尺寸
  QSize contentSizeForWidth(int width);

signals:
  void clicked();

protected:
  void mousePressEvent(QMouseEvent *event) override;

private:
  QLabel *m_categoryLabel = nullptr;
  QLabel *m_titleLabel = nullptr;
  QLabel *m_dateLabel = nullptr;
  QLabel *m_summaryLabel = nullptr;
};
