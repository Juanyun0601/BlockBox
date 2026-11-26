/**
 * @file   ModRelationDialog.cpp
 * @brief  本地模组所属关系弹窗实现
 * @author BlockBox Team
 * @date   2026-11-14
 */

#include "ModRelationDialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPersistentModelIndex>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

// 关系图布局常量（节点卡片尺寸 / 列间距 / 页边距）
namespace
{
constexpr int kNodeWidth = 176;
constexpr int kNodeHeight = 50;
constexpr int kNodeGap = 12;
constexpr int kColumnGap = 56;
constexpr int kMargin = 20;

// 关系图配色统一走主题令牌（对齐 DownloadTaskCard::applyChrome 的做法），
// 而非控件调色板——调色板条目在部分主题下可能是无效色。
QColor safeColor(const QString &hex, const QColor &fallback)
{
  const QColor c(hex);
  return c.isValid() ? c : fallback;
}

QColor textColorToken()
{
  return safeColor(ThemeManager::instance()->currentTextColor(),
                   QColor(0x1F, 0x29, 0x37));
}

QColor accentToken()
{
  return safeColor(ThemeManager::instance()->currentThemeColor(),
                   QColor(0x25, 0x60, 0xC8));
}

QColor borderToken()
{
  return safeColor(ThemeManager::instance()->currentBorderColor(),
                   textColorToken());
}

QColor withAlpha(QColor color, int alpha)
{
  if (!color.isValid())
  {
    color = textColorToken();
  }
  color.setAlpha(alpha);
  return color;
}

// 深浅主题按当前文字色的明度判断（深色主题下文字为浅色）
bool isDarkTheme()
{
  return textColorToken().lightness() > 140;
}

// 语义状态色：深浅主题各取一档，保证小字号在卡片底上可读
QColor successToken()
{
  return isDarkTheme() ? QColor(0x45, 0xC8, 0x82) : QColor(0x1E, 0x9E, 0x56);
}

QColor dangerToken()
{
  return isDarkTheme() ? QColor(0xFF, 0x7B, 0x72) : QColor(0xD9, 0x3B, 0x36);
}

QColor warningToken()
{
  return isDarkTheme() ? QColor(0xF2, 0xAD, 0x4B) : QColor(0xB8, 0x6A, 0x00);
}

// ── 关系树的行类型 / 状态类型（委托按这些角色自绘，避免全局 QSS 的
//    ::item color 覆盖模型前景色） ──
enum TreeRowKind
{
  RowNormal = 0,   // 普通依赖条目
  RowSection = 1,  // 分组标题（「前置依赖」「被这些模组依赖」）
  RowHint = 2      // 空状态 / 提示文字
};

enum TreeStatusKind
{
  StatusInstalled = 0,          // 已安装（必需）
  StatusInstalledOptional = 1,  // 已安装（建议）
  StatusMissing = 2,            // 未安装（必需）
  StatusMissingOptional = 3,    // 未安装（建议）
  StatusNeutral = 4             // 中性说明（「依赖当前模组」）
};

constexpr int kRowKindRole = Qt::UserRole + 101;
constexpr int kStatusKindRole = Qt::UserRole + 102;
constexpr int kPillRole = Qt::UserRole + 103;

// 左侧模组列表的行角色（名称/版本/缺失与建议数量/是否禁用）
constexpr int kListNameRole = Qt::UserRole + 201;
constexpr int kListVersionRole = Qt::UserRole + 202;
constexpr int kListMissingRole = Qt::UserRole + 203;
constexpr int kListSuggestedRole = Qt::UserRole + 204;
constexpr int kListDisabledRole = Qt::UserRole + 205;

// 关系树条目委托：自绘行内文字与状态胶囊，行背景由 RelationTreeWidget 统一绘制
class RelationTreeDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
  {
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);
    const int kind = idx.data(kRowKindRole).toInt();
    const int rowH = kind == RowSection ? 34 : 28;
    if (idx.column() == 0)
    {
      return QSize(120, rowH);
    }
    // 版本 / 状态列按内容宽度算，避免 ResizeToContents 被固定宽度撑开
    const int extra = (idx.column() == 2 && idx.data(kPillRole).toBool()) ? 24 : 16;
    return QSize(o.fontMetrics.horizontalAdvance(o.text) + extra, rowH);
  }

  void paint(QPainter *p, const QStyleOptionViewItem &opt,
             const QModelIndex &idx) const override
  {
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);

    const QColor textCol = textColorToken();
    const QColor accentCol = accentToken();
    const int kind = idx.data(kRowKindRole).toInt();

    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    if (idx.column() == 0)
    {
      QFont f = o.font;
      if (kind == RowSection)
      {
        // 分组标题：「前置依赖」用主题色加粗，尾部数量做成小胶囊，和条目拉开层次
        f.setBold(true);
        f.setPointSizeF(qMax(10.0, f.pointSizeF()));
        p->setFont(f);
        const int split = o.text.indexOf(QStringLiteral("（"));
        const QString head = split > 0 ? o.text.left(split) : o.text;
        QString count = split > 0 ? o.text.mid(split + 1) : QString();
        if (count.endsWith(QStringLiteral("）")))
        {
          count.chop(1);
        }
        const QFontMetrics fm(f);
        const QRect r = o.rect.adjusted(8, 0, -8, 0);
        p->setPen(accentCol);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, head);

        if (!count.isEmpty())
        {
          QFont countFont = f;
          countFont.setWeight(QFont::Medium);
          countFont.setPointSizeF(qMax(8.0, f.pointSizeF() - 1.0));
          const QFontMetrics cfm(countFont);
          const qreal pillW = qMax<qreal>(20, cfm.horizontalAdvance(count) + 12);
          const QRectF pill(r.left() + fm.horizontalAdvance(head) + 6,
                            o.rect.center().y() - 8, pillW, 16);
          p->setPen(Qt::NoPen);
          p->setBrush(withAlpha(accentCol, isDarkTheme() ? 46 : 22));
          p->drawRoundedRect(pill, 8, 8);
          p->setFont(countFont);
          p->setPen(withAlpha(accentCol, 210));
          p->drawText(pill, Qt::AlignCenter, count);
        }
      }
      else if (kind == RowHint)
      {
        f.setItalic(true);
        p->setFont(f);
        p->setPen(withAlpha(textCol, 120));
        p->drawText(o.rect.adjusted(12, 0, -8, 0),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(f).elidedText(o.text, Qt::ElideRight,
                                               qMax(0, o.rect.width() - 20)));
      }
      else
      {
        f.setWeight(QFont::Medium);
        p->setFont(f);
        p->setPen(textCol);
        QFontMetrics fm(f);
        p->drawText(o.rect.adjusted(8, 0, -8, 0),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    fm.elidedText(o.text, Qt::ElideRight,
                                  qMax(0, o.rect.width() - 16)));
      }
    }
    else if (idx.column() == 1)
    {
      QFont f = o.font;
      f.setPointSizeF(qMax(8.5, f.pointSizeF() - 0.5));
      p->setFont(f);
      const bool anyVersion = o.text == tr("任意版本");
      p->setPen(withAlpha(textCol, anyVersion ? 105 : 150));
      QFontMetrics fm(f);
      p->drawText(o.rect.adjusted(6, 0, -6, 0),
                  Qt::AlignVCenter | Qt::AlignLeft,
                  fm.elidedText(o.text, Qt::ElideRight,
                                qMax(0, o.rect.width() - 12)));
    }
    else
    {
      // 状态列：依赖状态画成彩色胶囊，其余（「依赖当前模组」）走弱化文字
      QColor statusCol = withAlpha(textCol, 145);
      switch (idx.data(kStatusKindRole).toInt())
      {
      case StatusInstalled: statusCol = successToken(); break;
      case StatusInstalledOptional: statusCol = successToken(); break;
      case StatusMissing: statusCol = dangerToken(); break;
      case StatusMissingOptional: statusCol = warningToken(); break;
      default: break;
      }

      QFont f = o.font;
      f.setPointSizeF(qMax(8.5, f.pointSizeF() - 0.5));
      p->setFont(f);
      const QFontMetrics fm(f);
      const QString text = o.text;
      const int textW = fm.horizontalAdvance(text);

      if (idx.data(kPillRole).toBool())
      {
        const QRectF pill(o.rect.left() + 4, o.rect.center().y() - 10,
                          textW + 16, 20);
        p->setPen(Qt::NoPen);
        p->setBrush(withAlpha(statusCol, isDarkTheme() ? 44 : 24));
        p->drawRoundedRect(pill, 10, 10);
        p->setPen(statusCol);
        p->drawText(pill, Qt::AlignCenter, text);
      }
      else
      {
        p->setPen(statusCol);
        p->drawText(o.rect.adjusted(6, 0, -6, 0),
                    Qt::AlignVCenter | Qt::AlignLeft, text);
      }
    }

    p->restore();
  }
};

// 关系树共用的 Fusion 底样式：Windows 11 原生样式会在选中行左缘额外画一条
// 系统强调色竖条（主题色是绿的，和系统蓝不搭）。QSS 由样式表代理继续生效，
// 样式对象挂到应用上复用，避免每次打开弹窗都新建一份。
QStyle *relationTreeBaseStyle()
{
  static QStyle *style = [] {
    QStyle *s = QStyleFactory::create(QStringLiteral("Fusion"));
    if (s)
    {
      s->setParent(QCoreApplication::instance());
    }
    return s;
  }();
  return style;
}

// 关系树：行背景 / 分组带 / 展开箭头统一在这里绘制，委托只画文字与胶囊
class RelationTreeWidget : public QTreeWidget
{
public:
  explicit RelationTreeWidget(QWidget *parent = nullptr)
      : QTreeWidget(parent)
  {
    setIndentation(18);
    setRootIsDecorated(true);
    setUniformRowHeights(false);
    setExpandsOnDoubleClick(true);
    setAllColumnsShowFocus(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setFocusPolicy(Qt::StrongFocus);
  }

protected:
  void drawRow(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
  {
    const int kind = idx.data(kRowKindRole).toInt();
    // drawRow 拿到的 option 不带逐行选中状态（由基类在列里补），这里直接问选择模型
    const QModelIndex first = idx.siblingAtColumn(0);
    const bool selected = selectionModel() && selectionModel()->isSelected(first);
    const bool hovered = m_hoverIndex.isValid() && m_hoverIndex == first;
    const QColor textCol = textColorToken();
    const QColor accentCol = accentToken();
    const bool dark = isDarkTheme();

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);

    if (kind == RowSection)
    {
      // 分组标题：主题色淡底，像一段小标题而不是普通行
      const QRectF band = QRectF(opt.rect).adjusted(3, 3, -3, -3);
      p->setBrush(withAlpha(accentCol, dark ? 34 : 18));
      p->drawRoundedRect(band, 9, 9);
    }
    else if (selected)
    {
      p->setBrush(withAlpha(accentCol, dark ? 44 : 26));
      p->drawRoundedRect(QRectF(opt.rect).adjusted(3, 1, -3, -1), 8, 8);
    }
    else if (hovered)
    {
      p->setBrush(withAlpha(textCol, dark ? 20 : 9));
      p->drawRoundedRect(QRectF(opt.rect).adjusted(3, 1, -3, -1), 8, 8);
    }

    // 展开 / 收起箭头（画在缩进区，点击判定见 mousePressEvent）
    if (model() && model()->hasChildren(idx))
    {
      const QRect cr = chevronRect(idx);
      if (cr.isValid())
      {
        const QPointF c = QRectF(cr).center();
        p->setPen(QPen(withAlpha(textCol, 150), 1.7, Qt::SolidLine,
                       Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        QPainterPath path;
        if (isExpanded(idx))
        {
          path.moveTo(c.x() - 4.0, c.y() - 2.0);
          path.lineTo(c.x(), c.y() + 2.5);
          path.lineTo(c.x() + 4.0, c.y() - 2.0);
        }
        else
        {
          path.moveTo(c.x() - 2.0, c.y() - 4.0);
          path.lineTo(c.x() + 2.5, c.y());
          path.lineTo(c.x() - 2.0, c.y() + 4.0);
        }
        p->drawPath(path);
      }
    }

    p->restore();
    QTreeWidget::drawRow(p, opt, idx);
  }

  // 分支装饰全部由 drawRow 自绘（含展开箭头），这里不再让样式画第二套
  void drawBranches(QPainter *, const QRect &, const QModelIndex &) const override
  {
  }

  void mouseMoveEvent(QMouseEvent *event) override
  {
    QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
    {
      idx = indexAt(QPoint(event->pos().x() + indentation() + 4,
                           event->pos().y()));
    }
    const QPersistentModelIndex hover(idx.isValid() ? idx.siblingAtColumn(0)
                                                    : QModelIndex());
    if (hover != m_hoverIndex)
    {
      m_hoverIndex = hover;
      viewport()->update();
    }
    QTreeWidget::mouseMoveEvent(event);
  }

  void leaveEvent(QEvent *event) override
  {
    if (m_hoverIndex.isValid())
    {
      m_hoverIndex = QPersistentModelIndex();
      viewport()->update();
    }
    QTreeWidget::leaveEvent(event);
  }

  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::LeftButton)
    {
      QModelIndex idx = indexAt(event->pos());
      if (!idx.isValid())
      {
        // 缩进区不在任何列内：向右平移半个缩进后仍能定位到所属行
        idx = indexAt(QPoint(event->pos().x() + indentation() + 4,
                             event->pos().y()));
      }
      if (idx.isValid() && model() && model()->hasChildren(idx)
          && chevronRect(idx).contains(event->pos()))
      {
        setExpanded(idx, !isExpanded(idx));
        event->accept();
        return;
      }
    }
    QTreeWidget::mousePressEvent(event);
  }

private:
  QRect chevronRect(const QModelIndex &idx) const
  {
    const QRect itemRect = visualRect(idx);
    if (!itemRect.isValid())
    {
      return QRect();
    }
    const int cx = itemRect.left() - indentation() / 2 - 1;
    const int cy = itemRect.center().y();
    if (cx < 2)
    {
      return QRect();
    }
    return QRect(cx - 6, cy - 6, 12, 12);
  }

  QPersistentModelIndex m_hoverIndex;  // 当前悬停行（自绘 hover 底色用）
};

// 左侧模组卡片委托：两行排版（名称 + 版本），右侧标注缺失 / 建议前置数量
class RelationModListDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
  {
    Q_UNUSED(opt);
    Q_UNUSED(idx);
    return QSize(100, 50);
  }

  void paint(QPainter *p, const QStyleOptionViewItem &opt,
             const QModelIndex &idx) const override
  {
    const QColor textCol = textColorToken();
    const QColor accentCol = accentToken();
    const bool dark = isDarkTheme();
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = opt.state & QStyle::State_MouseOver;

    const QRectF row = QRectF(opt.rect).adjusted(1, 2, -1, -2);
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    if (selected || hovered)
    {
      p->setPen(Qt::NoPen);
      p->setBrush(selected ? withAlpha(accentCol, dark ? 46 : 26)
                           : withAlpha(textCol, dark ? 22 : 10));
      p->drawRoundedRect(row, 9, 9);
    }
    if (selected)
    {
      p->setPen(Qt::NoPen);
      p->setBrush(accentCol);
      p->drawRoundedRect(QRectF(row.left() + 1, row.top() + 10, 3,
                                row.height() - 20),
                         1.5, 1.5);
    }

    const QString name = idx.data(kListNameRole).toString();
    const QString version = idx.data(kListVersionRole).toString();
    const int missing = idx.data(kListMissingRole).toInt();
    const int suggested = idx.data(kListSuggestedRole).toInt();
    const bool disabled = idx.data(kListDisabledRole).toBool();

    // 状态圆点：缺失（红）/ 建议缺失（橙）/ 禁用（灰），其余不画
    QColor dot;
    if (disabled)
    {
      dot = withAlpha(textCol, 90);
    }
    else if (missing > 0)
    {
      dot = dangerToken();
    }
    else if (suggested > 0)
    {
      dot = warningToken();
    }
    if (dot.isValid())
    {
      p->setPen(Qt::NoPen);
      p->setBrush(dot);
      p->drawEllipse(QPointF(row.left() + 16, row.center().y() + 1), 3.2, 3.2);
    }

    // 右侧状态徽标
    QString badge;
    QColor badgeCol;
    if (missing > 0)
    {
      badge = tr("缺 %1").arg(missing);
      badgeCol = dangerToken();
    }
    else if (suggested > 0)
    {
      badge = tr("建议 %1").arg(suggested);
      badgeCol = warningToken();
    }
    else if (disabled)
    {
      badge = tr("已禁用");
      badgeCol = withAlpha(textCol, 130);
    }

    QFont badgeFont = opt.font;
    badgeFont.setPointSizeF(qMax(8.0, badgeFont.pointSizeF() - 2.0));
    const QFontMetrics bfm(badgeFont);
    qreal titleRight = row.right() - 12;
    if (!badge.isEmpty())
    {
      const qreal bw = bfm.horizontalAdvance(badge) + 12;
      const QRectF br(row.right() - 8 - bw, row.center().y() - 9, bw, 18);
      p->setPen(Qt::NoPen);
      p->setBrush(withAlpha(badgeCol, dark ? 46 : 22));
      p->drawRoundedRect(br, 9, 9);
      p->setFont(badgeFont);
      p->setPen(badgeCol);
      p->drawText(br, Qt::AlignCenter, badge);
      titleRight = br.left() - 6;
    }

    QFont titleFont = opt.font;
    titleFont.setWeight(QFont::DemiBold);
    p->setFont(titleFont);
    p->setPen(disabled ? withAlpha(textCol, 140) : textCol);
    const QFontMetrics tfm(titleFont);
    const qreal titleLeft = row.left() + (dot.isValid() ? 26 : 14);
    p->drawText(QRectF(titleLeft, row.top() + 6, qMax<qreal>(10, titleRight - titleLeft), 18),
                Qt::AlignVCenter | Qt::AlignLeft,
                tfm.elidedText(name, Qt::ElideRight,
                               qMax(0, int(titleRight - titleLeft))));

    QFont subFont = opt.font;
    subFont.setPointSizeF(qMax(8.0, subFont.pointSizeF() - 1.5));
    p->setFont(subFont);
    p->setPen(withAlpha(textCol, disabled ? 110 : 140));
    const QFontMetrics sfm(subFont);
    const QString sub = version.isEmpty() ? tr("版本未知") : version;
    p->drawText(QRectF(titleLeft, row.top() + 25, qMax<qreal>(10, titleRight - titleLeft), 16),
                Qt::AlignVCenter | Qt::AlignLeft,
                sfm.elidedText(sub, Qt::ElideRight,
                               qMax(0, int(titleRight - titleLeft))));

    p->restore();
  }
};
} // namespace

// ============================================================
// ModRelationGraphWidget — 三列关系图自绘控件
// ============================================================

ModRelationGraphWidget::ModRelationGraphWidget(QWidget *parent)
    : QWidget(parent)
{
  setMouseTracking(true);
  setAttribute(Qt::WA_StyledBackground, false);
}

void ModRelationGraphWidget::setGraph(const QList<GraphNode> &deps,
                                      const GraphNode &center,
                                      const QList<GraphNode> &dependents)
{
  m_deps = deps;
  m_center = center;
  m_dependents = dependents;
  m_hasCenter = !center.title.isEmpty();
  m_hoverCol = -1;
  m_hoverIdx = -1;
  relayout();
  update();
}

void ModRelationGraphWidget::relayout()
{
  // 各列节点总高度（列内竖直堆叠，整体在中轴线上居中）
  const int depsH = m_deps.size() * kNodeHeight + qMax(0, m_deps.size() - 1) * kNodeGap;
  const int usedH = m_dependents.size() * kNodeHeight + qMax(0, m_dependents.size() - 1) * kNodeGap;
  const int nodesH = qMax(qMax(depsH, usedH), kNodeHeight);

  // 列标题 + 节点区作为一个整体居中，标题始终紧贴自己那一列的节点，
  // 避免标题固定在画布顶端、和节点之间空出一大片留白
  constexpr int kHeaderH = 24;
  constexpr int kHeaderGap = 12;
  const int blockH = kHeaderH + kHeaderGap + nodesH;
  const int availH = qMax(blockH + kMargin * 2, height());
  const int availW = qMax(width(), minimumSizeHint().width());

  const int contentW = kNodeWidth * 3 + kColumnGap * 2;
  const int leftX = qMax(kMargin, (availW - contentW) / 2);
  const int blockTop = qMax(kMargin, (availH - blockH) / 2);
  const int nodesTop = blockTop + kHeaderH + kHeaderGap;
  const int nodesMid = nodesTop + nodesH / 2;

  m_headerTop = blockTop;

  int y = nodesTop + (nodesH - depsH) / 2;
  for (int i = 0; i < m_deps.size(); ++i)
  {
    m_deps[i].rect = QRectF(leftX, y, kNodeWidth, kNodeHeight);
    y += kNodeHeight + kNodeGap;
  }

  const int centerX = leftX + kNodeWidth + kColumnGap;
  m_center.rect = QRectF(centerX, nodesMid - kNodeHeight / 2,
                         kNodeWidth, kNodeHeight);

  const int rightX = centerX + kNodeWidth + kColumnGap;
  y = nodesTop + (nodesH - usedH) / 2;
  for (int i = 0; i < m_dependents.size(); ++i)
  {
    m_dependents[i].rect = QRectF(rightX, y, kNodeWidth, kNodeHeight);
    y += kNodeHeight + kNodeGap;
  }
}

QSize ModRelationGraphWidget::minimumSizeHint() const
{
  const int depsH = m_deps.size() * kNodeHeight + qMax(0, m_deps.size() - 1) * kNodeGap;
  const int usedH = m_dependents.size() * kNodeHeight + qMax(0, m_dependents.size() - 1) * kNodeGap;
  const int contentH = qMax(qMax(depsH, usedH), kNodeHeight) + 36 + kMargin * 2;
  const int contentW = kNodeWidth * 3 + kColumnGap * 2 + kMargin * 2;
  return QSize(contentW, qMax(contentH, 300));
}

QRectF ModRelationGraphWidget::nodeRect(int column, int index) const
{
  if (column == 0 && index >= 0 && index < m_deps.size())
  {
    return m_deps[index].rect;
  }
  if (column == 1 && m_hasCenter)
  {
    return m_center.rect;
  }
  if (column == 2 && index >= 0 && index < m_dependents.size())
  {
    return m_dependents[index].rect;
  }
  return QRectF();
}

void ModRelationGraphWidget::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QColor textCol = textColorToken();

  relayout();

  if (!m_hasCenter)
  {
    p.setPen(withAlpha(textCol, 110));
    p.drawText(rect(), Qt::AlignCenter, tr("请先在左侧选择一个模组"));
    return;
  }

  // 三列标题（数量做成小胶囊，空列也常驻，版面不会偏向一侧）
  const QRectF leftHead(m_center.rect.left() - kColumnGap - kNodeWidth,
                        m_headerTop, kNodeWidth, 24);
  const QRectF centerHead(m_center.rect.left(), m_headerTop, kNodeWidth, 24);
  const QRectF rightHead(m_center.rect.right() + kColumnGap,
                         m_headerTop, kNodeWidth, 24);
  drawColumnHeader(p, leftHead, tr("前置依赖"), m_deps.size(), true);
  drawColumnHeader(p, centerHead, tr("当前模组"), 0, false);
  drawColumnHeader(p, rightHead, tr("被这些模组依赖"), m_dependents.size(), true);

  // 连线：前置 → 当前模组 → 被依赖（悬停节点相关的连线高亮）
  const QColor mutedEdge = withAlpha(textCol, 95);
  for (int i = 0; i < m_deps.size(); ++i)
  {
    const GraphNode &dep = m_deps[i];
    const bool hot = (m_hoverCol == 0 && m_hoverIdx == i) || (m_hoverCol == 1);
    const QColor edgeCol = dep.installed
                               ? mutedEdge
                               : withAlpha(dep.required ? dangerToken() : warningToken(), 165);
    drawEdge(p, QPointF(dep.rect.right(), dep.rect.center().y()),
             QPointF(m_center.rect.left(), m_center.rect.center().y()),
             hot, !dep.installed, edgeCol);
  }
  for (int i = 0; i < m_dependents.size(); ++i)
  {
    const bool hot = (m_hoverCol == 2 && m_hoverIdx == i) || (m_hoverCol == 1);
    drawEdge(p, QPointF(m_center.rect.right(), m_center.rect.center().y()),
             QPointF(m_dependents[i].rect.left(), m_dependents[i].rect.center().y()),
             hot, false, mutedEdge);
  }

  // 空列占位：和节点同尺寸的虚线框，贴在自己那一列的标题下方
  if (m_deps.isEmpty())
  {
    drawEmptyColumn(p, QRectF(m_center.rect.left() - kColumnGap - kNodeWidth,
                              m_headerTop + 36, kNodeWidth, kNodeHeight),
                    tr("没有前置依赖"));
  }
  if (m_dependents.isEmpty())
  {
    drawEmptyColumn(p, QRectF(m_center.rect.right() + kColumnGap,
                              m_headerTop + 36, kNodeWidth, kNodeHeight),
                    tr("没有模组依赖它"));
  }

  for (int i = 0; i < m_deps.size(); ++i)
  {
    drawNode(p, m_deps[i], m_hoverCol == 0 && m_hoverIdx == i);
  }
  drawNode(p, m_center, m_hoverCol == 1);
  for (int i = 0; i < m_dependents.size(); ++i)
  {
    drawNode(p, m_dependents[i], m_hoverCol == 2 && m_hoverIdx == i);
  }

  // 图例放在节点区下方，说明虚实线与缺失色
  qreal nodesBottom = m_center.rect.bottom();
  if (!m_deps.isEmpty())
  {
    nodesBottom = qMax(nodesBottom, m_deps.last().rect.bottom());
  }
  if (!m_dependents.isEmpty())
  {
    nodesBottom = qMax(nodesBottom, m_dependents.last().rect.bottom());
  }
  drawLegend(p, nodesBottom + 26);
}

void ModRelationGraphWidget::drawColumnHeader(QPainter &p, const QRectF &columnRect,
                                              const QString &text, int count,
                                              bool showCount) const
{
  const QColor textCol = textColorToken();
  QFont f = font();
  f.setPointSizeF(qMax(9.0, f.pointSizeF() - 0.6));
  f.setWeight(QFont::DemiBold);
  p.setFont(f);
  const QFontMetrics fm(f);

  QFont countFont = f;
  countFont.setPointSizeF(qMax(8.0, f.pointSizeF() - 1.0));
  countFont.setWeight(QFont::Medium);
  const QFontMetrics cfm(countFont);
  const QString countText = QString::number(count);
  const qreal pillW = qMax<qreal>(20, cfm.horizontalAdvance(countText) + 12);

  const qreal textW = fm.horizontalAdvance(text);
  const qreal totalW = showCount ? textW + 6 + pillW : textW;
  const qreal x0 = columnRect.center().x() - totalW / 2;

  p.setPen(withAlpha(textCol, 135));
  p.drawText(QRectF(x0, columnRect.top(), textW, columnRect.height()),
             Qt::AlignVCenter | Qt::AlignLeft, text);

  if (showCount)
  {
    const QRectF pill(x0 + textW + 6, columnRect.center().y() - 8, pillW, 16);
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(textCol, isDarkTheme() ? 30 : 12));
    p.drawRoundedRect(pill, 8, 8);
    p.setFont(countFont);
    p.setPen(withAlpha(textCol, 150));
    p.drawText(pill, Qt::AlignCenter, countText);
  }
}

void ModRelationGraphWidget::drawEmptyColumn(QPainter &p, const QRectF &slot,
                                             const QString &text) const
{
  const QColor textCol = textColorToken();
  QPen pen(withAlpha(textCol, 45), 1.2, Qt::DashLine);
  pen.setCapStyle(Qt::RoundCap);
  p.setPen(pen);
  p.setBrush(withAlpha(textCol, 6));
  p.drawRoundedRect(slot, 12, 12);

  QFont f = font();
  f.setPointSizeF(qMax(8.5, f.pointSizeF() - 1.2));
  f.setItalic(true);
  p.setFont(f);
  p.setPen(withAlpha(textCol, 95));
  p.drawText(slot, Qt::AlignCenter, text);
}

void ModRelationGraphWidget::drawLegend(QPainter &p, qreal bottomY) const
{
  const QColor textCol = textColorToken();
  QFont f = font();
  f.setPointSizeF(qMax(8.5, f.pointSizeF() - 1.4));
  p.setFont(f);
  const QFontMetrics fm(f);

  const qreal y = qMin<qreal>(height() - 14, bottomY);
  const int contentW = kNodeWidth * 3 + kColumnGap * 2;
  qreal x = qMax<qreal>(kMargin, (width() - contentW) / 2.0);

  auto drawItem = [&](const QColor &color, bool dashed, const QString &label) {
    QPen pen(color, 1.6);
    if (dashed)
    {
      pen.setStyle(Qt::DashLine);
    }
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(x, y), QPointF(x + 18, y));
    p.setPen(withAlpha(textCol, 120));
    p.drawText(QRectF(x + 24, y - 9, fm.horizontalAdvance(label) + 4, 18),
               Qt::AlignVCenter | Qt::AlignLeft, label);
    x += 24 + fm.horizontalAdvance(label) + 16;
  };

  drawItem(withAlpha(textCol, 100), false, tr("已安装"));
  drawItem(dangerToken(), true, tr("必需前置缺失"));
  drawItem(warningToken(), true, tr("建议前置缺失"));

  // 右侧操作提示：只有存在可点击节点时才显示
  if (!m_deps.isEmpty() || !m_dependents.isEmpty())
  {
    const QString hint = tr("点击节点可切换查看");
    const qreal hintW = fm.horizontalAdvance(hint);
    p.setPen(withAlpha(textCol, 95));
    p.drawText(QRectF(width() - hintW - kMargin, y - 9, hintW, 18),
               Qt::AlignVCenter | Qt::AlignRight, hint);
  }
}

void ModRelationGraphWidget::drawNode(QPainter &p, const GraphNode &node,
                                      bool highlighted) const
{
  const QColor textCol = textColorToken();
  const QColor accentCol = accentToken();
  const bool dark = isDarkTheme();
  const bool isCenter = node.center;

  const QColor statusCol =
      node.installed ? (isCenter ? accentCol : successToken())
                     : (node.required ? dangerToken() : warningToken());

  QColor fill;
  QColor border;
  QColor titleCol = textCol;
  qreal borderW = 1.0;
  Qt::PenStyle borderStyle = Qt::SolidLine;

  if (isCenter)
  {
    fill = withAlpha(accentCol, dark ? 44 : 24);
    border = accentCol;
    titleCol = accentCol;
    borderW = 1.8;
  }
  else if (!node.installed)
  {
    fill = withAlpha(statusCol, dark ? 30 : 14);
    border = withAlpha(statusCol, 190);
    titleCol = withAlpha(textCol, 175);
    borderStyle = Qt::DashLine;
  }
  else
  {
    fill = withAlpha(textCol, dark ? 24 : 12);
    border = withAlpha(borderToken(), 190);
  }
  if (highlighted)
  {
    border = isCenter ? accentCol : withAlpha(textCol, 200);
    borderW = 2;
  }

  // 当前模组 / 悬停节点的柔和外圈，突出焦点又不用额外阴影
  if (isCenter || highlighted)
  {
    p.setPen(QPen(withAlpha(isCenter ? accentCol : statusCol, 50), 3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(node.rect.adjusted(-2, -2, 2, 2), 14, 14);
  }

  p.setBrush(fill);
  QPen pen(border, borderW);
  pen.setStyle(borderStyle);
  p.setPen(pen);
  p.drawRoundedRect(node.rect, 12, 12);

  // 状态圆点：已安装绿、必需缺失红、建议缺失橙，当前节点用主题色
  const qreal dotX = node.rect.left() + 14;
  const qreal dotY = node.rect.top() + 17;
  p.setPen(Qt::NoPen);
  p.setBrush(statusCol);
  p.drawEllipse(QPointF(dotX, dotY), 3.2, 3.2);

  const qreal textLeft = dotX + 9;
  const qreal textW = qMax<qreal>(20, node.rect.right() - 12 - textLeft);

  // 标题（超出宽度省略）
  QFont titleFont = font();
  titleFont.setWeight(QFont::DemiBold);
  titleFont.setPointSizeF(qMax(9.5, titleFont.pointSizeF()));
  p.setFont(titleFont);
  p.setPen(titleCol);
  const QFontMetrics fm(titleFont);
  p.drawText(QRectF(textLeft, node.rect.top() + 8, textW, 18),
             Qt::AlignVCenter | Qt::AlignLeft,
             fm.elidedText(node.title, Qt::ElideRight, int(textW)));

  // 副标题（版本 / 未安装等状态）
  QFont subFont = font();
  subFont.setPointSizeF(qMax(8.5, subFont.pointSizeF() - 1.2));
  p.setFont(subFont);
  p.setPen(node.installed ? withAlpha(textCol, 135) : statusCol);
  const QFontMetrics sfm(subFont);
  p.drawText(QRectF(textLeft, node.rect.bottom() - 21, textW, 16),
             Qt::AlignVCenter | Qt::AlignLeft,
             sfm.elidedText(node.subtitle, Qt::ElideRight, int(textW)));
}

void ModRelationGraphWidget::drawEdge(QPainter &p, const QPointF &from,
                                      const QPointF &to, bool highlighted,
                                      bool dashed, const QColor &color) const
{
  const QColor accentCol = accentToken();

  QPainterPath path;
  path.moveTo(from);
  const qreal bend = qMin<qreal>(kColumnGap * 0.5, 40);
  path.cubicTo(QPointF(from.x() + bend, from.y()),
               QPointF(to.x() - bend, to.y()), to);

  const QColor lineCol = highlighted ? accentCol : color;
  QPen pen(lineCol, highlighted ? 2 : 1.4);
  if (dashed)
  {
    pen.setStyle(Qt::DashLine);
  }
  pen.setCapStyle(Qt::RoundCap);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);

  // 箭头（所有连线均自左向右，指向被依赖一侧）
  const QPointF tip = to;
  const QPointF back = tip - QPointF(9, 0);
  QPolygonF arrow;
  arrow << tip
        << back + QPointF(0, 3.8)
        << back - QPointF(0, 3.8);
  p.setPen(Qt::NoPen);
  p.setBrush(lineCol);
  p.drawPolygon(arrow);
}

int ModRelationGraphWidget::hitNode(const QPointF &pos, int *indexInColumn) const
{
  if (indexInColumn)
  {
    *indexInColumn = -1;
  }
  for (int i = 0; i < m_deps.size(); ++i)
  {
    if (m_deps[i].rect.contains(pos))
    {
      if (indexInColumn) { *indexInColumn = i; }
      return 0;
    }
  }
  for (int i = 0; i < m_dependents.size(); ++i)
  {
    if (m_dependents[i].rect.contains(pos))
    {
      if (indexInColumn) { *indexInColumn = i; }
      return 2;
    }
  }
  if (m_hasCenter && m_center.rect.contains(pos))
  {
    return 1;
  }
  return -1;
}

void ModRelationGraphWidget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
  {
    m_pressedCol = hitNode(event->position(), &m_pressedIdx);
  }
  QWidget::mousePressEvent(event);
}

void ModRelationGraphWidget::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton && m_pressedCol >= 0)
  {
    int releaseIdx = -1;
    const int col = hitNode(event->position(), &releaseIdx);
    if (col == m_pressedCol && releaseIdx == m_pressedIdx)
    {
      int modIndex = -1;
      if (col == 0 && releaseIdx >= 0 && releaseIdx < m_deps.size())
      {
        modIndex = m_deps[releaseIdx].modIndex;
      }
      else if (col == 2 && releaseIdx >= 0 && releaseIdx < m_dependents.size())
      {
        modIndex = m_dependents[releaseIdx].modIndex;
      }
      if (modIndex >= 0)
      {
        emit nodeClicked(modIndex);
      }
    }
  }
  m_pressedCol = -1;
  m_pressedIdx = -1;
  QWidget::mouseReleaseEvent(event);
}

void ModRelationGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
  const QPointF pos = event->position();
  int idx = -1;
  const int col = hitNode(pos, &idx);
  if (col != m_hoverCol || idx != m_hoverIdx)
  {
    m_hoverCol = col;
    m_hoverIdx = idx;
    update();
  }

  // 悬停提示
  const GraphNode *node = nullptr;
  if (col == 0 && idx >= 0) { node = &m_deps[idx]; }
  else if (col == 1 && m_hasCenter) { node = &m_center; }
  else if (col == 2 && idx >= 0) { node = &m_dependents[idx]; }
  if (node)
  {
    const QString tip = node->title
                        + (node->subtitle.isEmpty()
                               ? QString()
                               : QStringLiteral("\n") + node->subtitle);
    QToolTip::showText(event->globalPosition().toPoint(), tip, this);
    setCursor(Qt::PointingHandCursor);
  }
  else
  {
    setCursor(Qt::ArrowCursor);
    QToolTip::hideText();
  }
  QWidget::mouseMoveEvent(event);
}

void ModRelationGraphWidget::leaveEvent(QEvent *event)
{
  m_hoverCol = -1;
  m_hoverIdx = -1;
  QToolTip::hideText();
  setCursor(Qt::ArrowCursor);
  update();
  QWidget::leaveEvent(event);
}

// ============================================================
// ModRelationDialog — 居中弹窗
// ============================================================

ModRelationDialog::ModRelationDialog(const QList<ModInfo> &mods, QWidget *parent)
    : AppDialogBase(parent)
    , m_mods(mods)
{
  setObjectName(QStringLiteral("modRelationDialog"));
  setWindowTitle(tr("模组所属关系"));

  // modId / 文件名（去 .jar）索引：判断前置是否已安装
  for (int i = 0; i < m_mods.size(); ++i)
  {
    const ModInfo &info = m_mods[i];
    if (!info.id.isEmpty())
    {
      m_indexByKey.insert(info.id.toLower(), i);
    }
    if (info.fileName.endsWith(".jar", Qt::CaseInsensitive))
    {
      m_indexByKey.insert(info.fileName.left(info.fileName.size() - 4).toLower(), i);
    }
  }

  initUI();

  // 默认选中第一个模组（触发 onModSelected 刷新右侧内容）
  if (!m_mods.isEmpty())
  {
    m_modListWidget->setCurrentRow(0);
  }
  updateSummary();
}

void ModRelationDialog::initUI()
{
  QWidget *card = new QWidget(this);
  card->setObjectName(QStringLiteral("appDialogCard"));
  card->setFixedSize(UiMetrics::fitToScreen(this, QSize(980, 620)));

  QVBoxLayout *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(20, 16, 20, 14);
  cardLayout->setSpacing(10);

  // ── 标题行：图标 + 标题 + 模式切换 ──
  QHBoxLayout *titleRow = new QHBoxLayout();
  titleRow->setSpacing(12);

  m_headerIcon = new QLabel(card);
  m_headerIcon->setObjectName(QStringLiteral("relationHeaderIcon"));
  m_headerIcon->setFixedSize(38, 38);
  m_headerIcon->setAlignment(Qt::AlignCenter);
  titleRow->addWidget(m_headerIcon, 0, Qt::AlignTop);

  QVBoxLayout *titleBox = new QVBoxLayout();
  titleBox->setSpacing(3);
  QLabel *titleLabel = new QLabel(tr("模组所属关系"), card);
  titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
  titleBox->addWidget(titleLabel);
  QLabel *promptLabel = new QLabel(tr("选择左侧模组，查看它声明的前置依赖，以及哪些模组依赖它"), card);
  promptLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
  titleBox->addWidget(promptLabel);
  titleRow->addLayout(titleBox);
  titleRow->addStretch();

  // 模式切换（依赖树 / 关系图），复用分段控件样式
  QWidget *modeSwitch = new QWidget(card);
  modeSwitch->setObjectName(QStringLiteral("contentViewSwitch"));
  modeSwitch->setAttribute(Qt::WA_StyledBackground);
  QHBoxLayout *modeLayout = new QHBoxLayout(modeSwitch);
  modeLayout->setContentsMargins(3, 3, 3, 3);
  modeLayout->setSpacing(2);

  auto makeModeBtn = [modeSwitch](const QString &text) {
    QPushButton *btn = new QPushButton(text, modeSwitch);
    btn->setObjectName(QStringLiteral("contentViewSwitchBtn"));
    btn->setFixedSize(88, 28);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setCheckable(true);
    // 分段按钮不抢焦点：避免点击后残留的焦点框看起来像「两个都选中」
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
  };
  m_treeModeBtn = makeModeBtn(tr("依赖树"));
  m_graphModeBtn = makeModeBtn(tr("关系图"));
  modeLayout->addWidget(m_treeModeBtn);
  modeLayout->addWidget(m_graphModeBtn);
  titleRow->addWidget(modeSwitch, 0, Qt::AlignTop);
  cardLayout->addLayout(titleRow);

  // ── 内容区：左侧模组列表 + 右侧堆叠视图 ──
  QHBoxLayout *contentRow = new QHBoxLayout();
  contentRow->setSpacing(12);

  QVBoxLayout *leftBox = new QVBoxLayout();
  leftBox->setSpacing(8);
  m_searchEdit = new QLineEdit(card);
  m_searchEdit->setPlaceholderText(tr("搜索模组"));
  m_searchEdit->setObjectName(QStringLiteral("appDialogLineEdit"));
  m_searchEdit->setClearButtonEnabled(true);
  leftBox->addWidget(m_searchEdit);

  m_modListWidget = new QListWidget(card);
  m_modListWidget->setObjectName(QStringLiteral("relationModList"));
  m_modListWidget->setFixedWidth(240);
  m_modListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_modListWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_modListWidget->setItemDelegate(new RelationModListDelegate(m_modListWidget));
  leftBox->addWidget(m_modListWidget, 1);
  contentRow->addLayout(leftBox);

  m_modeStack = new QStackedWidget(card);
  m_treeWidget = new RelationTreeWidget(m_modeStack);
  // 行背景 / 选中态都自绘，用 Fusion 作底样式，避免系统样式的选中装饰串色
  if (QStyle *treeStyle = relationTreeBaseStyle())
  {
    m_treeWidget->setStyle(treeStyle);
  }
  // 分支区的选中底色也交给自绘，这里的原生高亮设为透明，避免叠出一块色块
  {
    QPalette pal = m_treeWidget->palette();
    pal.setColor(QPalette::Highlight, QColor(0, 0, 0, 0));
    pal.setColor(QPalette::HighlightedText, pal.color(QPalette::Text));
    m_treeWidget->setPalette(pal);
  }
  m_treeWidget->setObjectName(QStringLiteral("relationTree"));
  m_treeWidget->setColumnCount(3);
  m_treeWidget->setHeaderLabels(QStringList() << tr("模组") << tr("版本要求") << tr("状态"));
  m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  // QTreeView 默认让最后一列吃掉剩余宽度，会把「状态」列撑得特别宽
  m_treeWidget->header()->setStretchLastSection(false);
  m_treeWidget->header()->setFixedHeight(36);
  m_treeWidget->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_treeWidget->setAlternatingRowColors(false);
  m_treeWidget->setMouseTracking(true);
  m_treeWidget->setItemDelegate(new RelationTreeDelegate(m_treeWidget));
  m_modeStack->addWidget(m_treeWidget);

  QScrollArea *graphScroll = new QScrollArea(m_modeStack);
  graphScroll->setWidgetResizable(true);
  graphScroll->setFrameShape(QFrame::NoFrame);
  graphScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  graphScroll->setStyleSheet(QStringLiteral(
      "QScrollArea{background:transparent;}"
      "QScrollArea>QWidget>QWidget{background:transparent;}"));
  m_graphWidget = new ModRelationGraphWidget(graphScroll);
  m_graphWidget->setObjectName(QStringLiteral("relationGraph"));
  graphScroll->setWidget(m_graphWidget);
  m_modeStack->addWidget(graphScroll);

  contentRow->addWidget(m_modeStack, 1);
  cardLayout->addLayout(contentRow, 1);

  // ── 底部统计：左侧概览 + 右侧数量胶囊 ──
  QFrame *divider = new QFrame(card);
  divider->setObjectName(QStringLiteral("relationDivider"));
  divider->setFrameShape(QFrame::NoFrame);
  divider->setFixedHeight(1);
  cardLayout->addWidget(divider);

  QWidget *summaryRow = new QWidget(card);
  QHBoxLayout *summaryLayout = new QHBoxLayout(summaryRow);
  summaryLayout->setContentsMargins(2, 0, 2, 0);
  summaryLayout->setSpacing(6);

  m_summaryLabel = new QLabel(summaryRow);
  m_summaryLabel->setObjectName(QStringLiteral("relationSummaryLabel"));
  summaryLayout->addWidget(m_summaryLabel, 0, Qt::AlignVCenter);

  m_currentLabel = new QLabel(summaryRow);
  m_currentLabel->setObjectName(QStringLiteral("relationCurrentLabel"));
  m_currentLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  summaryLayout->addWidget(m_currentLabel, 0, Qt::AlignVCenter);
  summaryLayout->addStretch();

  auto makeChip = [summaryRow](const QString &objectName) {
    QLabel *chip = new QLabel(summaryRow);
    chip->setObjectName(objectName);
    chip->setAlignment(Qt::AlignCenter);
    return chip;
  };
  m_statPrimLabel = makeChip(QStringLiteral("relationStatChip"));
  m_statMissingLabel = makeChip(QStringLiteral("relationStatChipDanger"));
  m_statSuggestedLabel = makeChip(QStringLiteral("relationStatChipWarn"));
  m_statUsedLabel = makeChip(QStringLiteral("relationStatChipAccent"));
  for (QLabel *chip : {m_statPrimLabel, m_statMissingLabel,
                       m_statSuggestedLabel, m_statUsedLabel})
  {
    summaryLayout->addWidget(chip, 0, Qt::AlignVCenter);
  }
  cardLayout->addWidget(summaryRow);

  updateStaticIcons();

  // ── 交互连接 ──
  auto switchMode = [this](int pageIndex) {
    m_modeStack->setCurrentIndex(pageIndex);
    m_treeModeBtn->setProperty("active", pageIndex == 0);
    m_graphModeBtn->setProperty("active", pageIndex == 1);
    for (QPushButton *btn : {m_treeModeBtn, m_graphModeBtn})
    {
      btn->style()->unpolish(btn);
      btn->style()->polish(btn);
    }
  };
  connect(m_treeModeBtn, &QPushButton::clicked, this, [switchMode]() { switchMode(0); });
  connect(m_graphModeBtn, &QPushButton::clicked, this, [switchMode]() { switchMode(1); });
  switchMode(0);

  connect(m_searchEdit, &QLineEdit::textChanged,
          this, &ModRelationDialog::onSearchChanged);
  connect(m_modListWidget, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *cur, QListWidgetItem *) {
            if (!cur)
            {
              return;
            }
            onModSelected(cur->data(Qt::UserRole).toInt());
          });
  connect(m_graphWidget, &ModRelationGraphWidget::nodeClicked,
          this, &ModRelationDialog::onGraphNodeClicked);

  // 树的颜色在构建时按主题令牌计算，主题切换后重建两种模式
  connect(ThemeManager::instance(), &ThemeManager::themeChanged,
          this, [this](ThemeManager::ThemeType) {
            updateStaticIcons();
            rebuildTree();
            rebuildGraph();
          });

  rebuildModList();
}

void ModRelationDialog::updateStaticIcons()
{
  if (m_headerIcon && QFile::exists(QStringLiteral(":/Images/Icons/link.svg")))
  {
    m_headerIcon->setPixmap(IconHelper::loadColoredIcon(
                                QStringLiteral(":/Images/Icons/link.svg"),
                                accentToken(), 19)
                                .pixmap(19, 19));
  }

  if (!m_searchEdit
      || !QFile::exists(QStringLiteral(":/Images/Icons/search.svg")))
  {
    return;
  }
  const QIcon searchIcon = IconHelper::loadColoredIcon(
      QStringLiteral(":/Images/Icons/search.svg"),
      withAlpha(textColorToken(), 145), 15);
  if (!m_searchAction)
  {
    m_searchAction = m_searchEdit->addAction(searchIcon, QLineEdit::LeadingPosition);
  }
  else
  {
    m_searchAction->setIcon(searchIcon);
  }
}

void ModRelationDialog::rebuildModList()
{
  m_modListWidget->blockSignals(true);
  m_modListWidget->clear();

  for (int i = 0; i < m_mods.size(); ++i)
  {
    const ModInfo &info = m_mods[i];

    // 每行先算好缺失 / 建议前置数量，卡片右侧直接用徽标提示，
    // 不点开也能看出哪些模组装得不完整
    int missing = 0;
    int suggested = 0;
    for (const ModDependency &dep : info.dependencies)
    {
      if (findLocalIndex(dep.name) >= 0)
      {
        continue;
      }
      if (dep.isRequired)
      {
        ++missing;
      }
      else
      {
        ++suggested;
      }
    }

    QString text = info.name;
    if (!info.latestVersion.isEmpty())
    {
      text += QStringLiteral("  ·  ") + info.latestVersion;
    }
    if (!info.enabled)
    {
      text += tr("（已禁用）");
    }

    QListWidgetItem *item = new QListWidgetItem(text, m_modListWidget);
    item->setData(Qt::UserRole, i);
    item->setData(kListNameRole, info.name);
    item->setData(kListVersionRole, info.latestVersion);
    item->setData(kListMissingRole, missing);
    item->setData(kListSuggestedRole, suggested);
    item->setData(kListDisabledRole, !info.enabled);

    QString tip = info.fileName;
    if (missing > 0)
    {
      tip += QStringLiteral("\n") + tr("缺少 %1 个必需前置").arg(missing);
    }
    else if (suggested > 0)
    {
      tip += QStringLiteral("\n") + tr("有 %1 个建议前置未安装").arg(suggested);
    }
    if (!info.enabled)
    {
      tip += QStringLiteral("\n") + tr("当前已被禁用");
    }
    item->setToolTip(tip);
  }
  m_modListWidget->blockSignals(false);

  m_searchEdit->setPlaceholderText(
      m_mods.isEmpty() ? tr("搜索模组") : tr("搜索 %1 个模组").arg(m_mods.size()));

  // 恢复搜索过滤与选中
  onSearchChanged(m_searchEdit->text());
  if (m_currentIndex >= 0 && m_currentIndex < m_mods.size())
  {
    for (int r = 0; r < m_modListWidget->count(); ++r)
    {
      if (m_modListWidget->item(r)->data(Qt::UserRole).toInt() == m_currentIndex)
      {
        m_modListWidget->blockSignals(true);
        m_modListWidget->setCurrentRow(r);
        m_modListWidget->blockSignals(false);
        break;
      }
    }
  }
}

void ModRelationDialog::onSearchChanged(const QString &text)
{
  const QString key = text.trimmed().toLower();
  for (int r = 0; r < m_modListWidget->count(); ++r)
  {
    QListWidgetItem *item = m_modListWidget->item(r);
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_mods.size())
    {
      item->setHidden(!key.isEmpty());
      continue;
    }
    const ModInfo &info = m_mods[idx];
    const bool match = key.isEmpty()
                       || info.name.toLower().contains(key)
                       || info.id.toLower().contains(key)
                       || info.fileName.toLower().contains(key);
    item->setHidden(!match);
  }
}

void ModRelationDialog::onModSelected(int index)
{
  if (index < 0 || index >= m_mods.size() || index == m_currentIndex)
  {
    return;
  }
  m_currentIndex = index;
  rebuildTree();
  rebuildGraph();
  updateSummary();
}

QString ModRelationDialog::modDisplayKey(const ModInfo &info) const
{
  return info.id.isEmpty() ? info.fileName : info.id;
}

int ModRelationDialog::findLocalIndex(const QString &depName) const
{
  const QString key = depName.trimmed().toLower();
  if (key.isEmpty())
  {
    return -1;
  }
  auto it = m_indexByKey.constFind(key);
  return it == m_indexByKey.constEnd() ? -1 : it.value();
}

void ModRelationDialog::rebuildTree()
{
  m_treeWidget->clear();

  if (m_currentIndex < 0 || m_currentIndex >= m_mods.size())
  {
    QTreeWidgetItem *empty = new QTreeWidgetItem(
        m_treeWidget, QStringList() << tr("左侧没有可选的模组") << QString() << QString());
    empty->setFlags(Qt::NoItemFlags);
    empty->setData(0, kRowKindRole, int(RowHint));
    return;
  }

  const ModInfo &mod = m_mods[m_currentIndex];

  // ── 根 1：前置依赖（已安装的前置可逐级展开其前置） ──
  const int depCount = mod.dependencies.size();
  QTreeWidgetItem *depRoot = new QTreeWidgetItem(
      m_treeWidget, QStringList() << tr("前置依赖（%1）").arg(depCount) << QString() << QString());
  depRoot->setData(0, kRowKindRole, int(RowSection));

  m_treeVisited.clear();
  m_treeVisited.insert(modDisplayKey(mod).toLower());
  addDepTreeItems(depRoot, mod, 0);
  depRoot->setExpanded(true);

  // ── 根 2：被这些模组依赖 ──
  int dependentCount = 0;
  QTreeWidgetItem *usedRoot = new QTreeWidgetItem(
      m_treeWidget, QStringList() << tr("被这些模组依赖") << QString() << QString());
  usedRoot->setData(0, kRowKindRole, int(RowSection));
  for (const ModInfo &other : m_mods)
  {
    for (const ModDependency &dep : other.dependencies)
    {
      if (dep.name.compare(modDisplayKey(mod), Qt::CaseInsensitive) == 0
          || (!mod.id.isEmpty() && dep.name.compare(mod.id, Qt::CaseInsensitive) == 0))
      {
        const QString ver = dep.version.isEmpty() || dep.version == QLatin1String("*")
                                ? tr("任意版本")
                                : dep.version;
        QTreeWidgetItem *item = new QTreeWidgetItem(
            usedRoot, QStringList() << other.name << ver << tr("依赖当前模组"));
        item->setData(2, kStatusKindRole, int(StatusNeutral));
        item->setData(2, kPillRole, false);
        item->setToolTip(0, other.fileName);
        ++dependentCount;
        break;
      }
    }
  }
  usedRoot->setText(0, tr("被这些模组依赖（%1）").arg(dependentCount));
  usedRoot->setExpanded(true);

  if (depCount == 0 && dependentCount == 0)
  {
    QTreeWidgetItem *empty = new QTreeWidgetItem(
        depRoot, QStringList()
                     << tr("该模组没有声明前置依赖，也没有被其他模组依赖")
                     << QString() << QString());
    empty->setFlags(Qt::NoItemFlags);
    empty->setData(0, kRowKindRole, int(RowHint));
  }
}

void ModRelationDialog::addDepTreeItems(QTreeWidgetItem *parent, const ModInfo &owner,
                                        int depth)
{
  // 状态列区分「必需 / 建议 × 已装 / 未装」，委托按状态色画成胶囊
  for (const ModDependency &dep : owner.dependencies)
  {
    const int localIdx = findLocalIndex(dep.name);
    const bool installed = localIdx >= 0;
    const QString title = installed ? m_mods[localIdx].name : dep.name;

    QString status;
    TreeStatusKind statusKind = StatusNeutral;
    if (installed && dep.isRequired)
    {
      status = tr("● 已安装");
      statusKind = StatusInstalled;
    }
    else if (installed)
    {
      status = tr("● 已安装 · 建议");
      statusKind = StatusInstalledOptional;
    }
    else if (dep.isRequired)
    {
      status = tr("● 未安装");
      statusKind = StatusMissing;
    }
    else
    {
      status = tr("○ 建议 · 未安装");
      statusKind = StatusMissingOptional;
    }

    const QString version = dep.version.isEmpty() || dep.version == QLatin1String("*")
                                ? tr("任意版本")
                                : dep.version;
    QTreeWidgetItem *item = new QTreeWidgetItem(
        parent, QStringList() << title << version << status);
    item->setData(2, kStatusKindRole, int(statusKind));
    item->setData(2, kPillRole, true);
    item->setToolTip(0, installed ? m_mods[localIdx].fileName
                                  : tr("未在当前实例的 mods 目录找到 %1").arg(dep.name));

    // 已安装的前置继续展开下一级（限深 + 防环）
    const QString depKey = dep.name.toLower();
    if (installed && depth < 6 && !m_treeVisited.contains(depKey))
    {
      m_treeVisited.insert(depKey);
      addDepTreeItems(item, m_mods[localIdx], depth + 1);
    }
  }
}

void ModRelationDialog::rebuildGraph()
{
  ModRelationGraphWidget::GraphNode center;
  QList<ModRelationGraphWidget::GraphNode> deps;
  QList<ModRelationGraphWidget::GraphNode> dependents;

  if (m_currentIndex < 0 || m_currentIndex >= m_mods.size())
  {
    m_graphWidget->setGraph(deps, center, dependents);
    return;
  }

  const ModInfo &mod = m_mods[m_currentIndex];

  center.title = mod.name;
  center.subtitle = mod.latestVersion;
  center.installed = true;
  center.required = true;
  center.center = true;
  center.modIndex = m_currentIndex;

  // 左列：前置依赖
  for (const ModDependency &dep : mod.dependencies)
  {
    ModRelationGraphWidget::GraphNode node;
    const int localIdx = findLocalIndex(dep.name);
    node.installed = localIdx >= 0;
    node.required = dep.isRequired;
    node.modIndex = localIdx;
    node.title = node.installed ? m_mods[localIdx].name : dep.name;
    if (node.installed)
    {
      node.subtitle = m_mods[localIdx].latestVersion;
      if (!dep.isRequired)
      {
        node.subtitle += tr(" · 建议");
      }
    }
    else
    {
      node.subtitle = dep.isRequired ? tr("未安装") : tr("建议 · 未安装");
    }
    deps.append(node);
  }

  // 右列：被依赖模组
  for (int i = 0; i < m_mods.size(); ++i)
  {
    const ModInfo &other = m_mods[i];
    if (i == m_currentIndex)
    {
      continue;
    }
    for (const ModDependency &dep : other.dependencies)
    {
      if (dep.name.compare(modDisplayKey(mod), Qt::CaseInsensitive) == 0
          || (!mod.id.isEmpty() && dep.name.compare(mod.id, Qt::CaseInsensitive) == 0))
      {
        ModRelationGraphWidget::GraphNode node;
        node.title = other.name;
        // 副标题与前置节点保持一致展示自身版本；箭头方向已表达「依赖当前模组」
        node.subtitle = other.latestVersion;
        node.installed = true;
        node.required = true;
        node.modIndex = i;
        dependents.append(node);
        break;
      }
    }
  }

  m_graphWidget->setGraph(deps, center, dependents);
}

void ModRelationDialog::updateSummary()
{
  if (m_currentIndex < 0 || m_currentIndex >= m_mods.size())
  {
    m_summaryLabel->setText(m_mods.isEmpty()
                                ? tr("当前实例没有模组")
                                : tr("共 %1 个模组").arg(m_mods.size()));
    m_currentLabel->clear();
    m_currentLabel->setVisible(false);
    m_statPrimLabel->setVisible(false);
    m_statMissingLabel->setVisible(false);
    m_statSuggestedLabel->setVisible(false);
    m_statUsedLabel->setVisible(false);
    return;
  }

  const ModInfo &mod = m_mods[m_currentIndex];
  int missing = 0, suggested = 0;
  for (const ModDependency &dep : mod.dependencies)
  {
    if (findLocalIndex(dep.name) < 0)
    {
      if (dep.isRequired) { ++missing; }
      else { ++suggested; }
    }
  }
  int usedBy = 0;
  for (const ModInfo &other : m_mods)
  {
    for (const ModDependency &dep : other.dependencies)
    {
      if (dep.name.compare(modDisplayKey(mod), Qt::CaseInsensitive) == 0
          || (!mod.id.isEmpty() && dep.name.compare(mod.id, Qt::CaseInsensitive) == 0))
      {
        ++usedBy;
        break;
      }
    }
  }

  m_summaryLabel->setText(tr("共 %1 个模组").arg(m_mods.size()));

  // 当前模组单独成一枚标签：名字过长时只省略名字，不影响右侧统计
  const QString currentText = tr("当前：%1").arg(mod.name);
  const QFontMetrics fm(m_currentLabel->font());
  m_currentLabel->setText(fm.elidedText(currentText, Qt::ElideRight, 260));
  m_currentLabel->setToolTip(mod.name);
  m_currentLabel->setVisible(true);

  m_statPrimLabel->setText(tr("前置 %1 个").arg(mod.dependencies.size()));
  m_statPrimLabel->setVisible(true);

  m_statMissingLabel->setText(tr("缺失 %1 个").arg(missing));
  m_statMissingLabel->setVisible(missing > 0);

  m_statSuggestedLabel->setText(tr("建议 %1 个").arg(suggested));
  m_statSuggestedLabel->setVisible(suggested > 0);

  m_statUsedLabel->setText(tr("被 %1 个模组依赖").arg(usedBy));
  m_statUsedLabel->setVisible(true);
}

void ModRelationDialog::onGraphNodeClicked(int modIndex)
{
  if (modIndex < 0 || modIndex >= m_mods.size() || modIndex == m_currentIndex)
  {
    return;
  }
  // 同步左侧列表选中（currentItemChanged → onModSelected 刷新两种模式）
  for (int r = 0; r < m_modListWidget->count(); ++r)
  {
    if (m_modListWidget->item(r)->data(Qt::UserRole).toInt() == modIndex)
    {
      m_modListWidget->setCurrentRow(r);
      break;
    }
  }
}
