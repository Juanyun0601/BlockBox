/**
 * @file   ModRelationDialog.cpp
 * @brief  本地模组所属关系弹窗实现
 * @author BlockBox Team
 * @date   2026-11-14
 */

#include "ModRelationDialog.h"

#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

// 关系图布局常量（节点卡片尺寸 / 列间距 / 页边距）
namespace
{
constexpr int kNodeWidth = 170;
constexpr int kNodeHeight = 46;
constexpr int kNodeGap = 10;
constexpr int kColumnGap = 64;
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

// 关系树的自定义数据角色：全局 QSS 的 ::item color 会覆盖模型前景色，
// 状态/版本等列的颜色经由这些角色交给 RelationTreeDelegate 自绘
constexpr int kTitleColorRole = Qt::UserRole + 101;
constexpr int kVersionColorRole = Qt::UserRole + 102;
constexpr int kStatusColorRole = Qt::UserRole + 103;

// 关系树条目委托：自绘行背景与文字颜色，保留全局的缩进/分支样式
class RelationTreeDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *p, const QStyleOptionViewItem &opt,
             const QModelIndex &idx) const override
  {
    const QColor textCol = textColorToken();
    const QColor accentCol = accentToken();

    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);

    // 行背景：选中用主色淡底、悬停用文字色淡底，观感对齐全局选中样式
    if (o.state & QStyle::State_Selected || o.state & QStyle::State_MouseOver)
    {
      p->save();
      p->setRenderHint(QPainter::Antialiasing);
      p->setPen(Qt::NoPen);
      p->setBrush(o.state & QStyle::State_Selected ? withAlpha(accentCol, 30)
                                                   : withAlpha(textCol, 12));
      p->drawRoundedRect(o.rect.adjusted(2, 1, -2, -1), 8, 8);
      p->restore();
    }

    QColor c;
    switch (idx.column())
    {
    case 1: c = idx.data(kVersionColorRole).value<QColor>(); break;
    case 2: c = idx.data(kStatusColorRole).value<QColor>(); break;
    default: c = idx.data(kTitleColorRole).value<QColor>(); break;
    }
    if (!c.isValid())
    {
      c = idx.column() == 0 ? textCol : withAlpha(textCol, 145);
    }

    p->save();
    QFontMetrics fm(o.font);
    constexpr int kPad = 8;
    const QString text =
        fm.elidedText(o.text, Qt::ElideRight, qMax(0, o.rect.width() - kPad * 2));
    p->setFont(o.font);
    p->setPen(c);
    p->drawText(o.rect.adjusted(kPad, 0, -kPad, 0),
                Qt::AlignVCenter | Qt::AlignLeft, text);
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
  const int contentH = qMax(qMax(depsH, usedH), kNodeHeight);
  const int availH = qMax(contentH, height());
  const int availW = qMax(width(), minimumSizeHint().width());

  const int contentW = kNodeWidth * 3 + kColumnGap * 2;
  const int leftX = qMax(kMargin, (availW - contentW) / 2);
  const int midY = availH / 2;

  int y = qMax(kMargin, midY - depsH / 2);
  for (int i = 0; i < m_deps.size(); ++i)
  {
    m_deps[i].rect = QRectF(leftX, y, kNodeWidth, kNodeHeight);
    y += kNodeHeight + kNodeGap;
  }

  const int centerX = leftX + kNodeWidth + kColumnGap;
  m_center.rect = QRectF(centerX, qMax(kMargin, midY - kNodeHeight / 2),
                         kNodeWidth, kNodeHeight);

  const int rightX = centerX + kNodeWidth + kColumnGap;
  y = qMax(kMargin, midY - usedH / 2);
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
  const int contentH = qMax(qMax(depsH, usedH), kNodeHeight) + kMargin * 2;
  const int contentW = kNodeWidth * 3 + kColumnGap * 2 + kMargin * 2;
  return QSize(contentW, qMax(contentH, 320));
}

void ModRelationGraphWidget::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QColor textCol = textColorToken();
  const QColor mutedCol = withAlpha(textCol, 110);

  relayout();

  // 连线：前置 → 当前模组 → 被依赖（悬停节点相关的连线高亮）
  for (int i = 0; i < m_deps.size(); ++i)
  {
    const GraphNode &dep = m_deps[i];
    const bool hot = (m_hoverCol == 0 && m_hoverIdx == i) || (m_hoverCol == 1);
    drawEdge(p, QPointF(dep.rect.right(), dep.rect.center().y()),
             QPointF(m_center.rect.left(), m_center.rect.center().y()),
             hot, !dep.installed);
  }
  for (int i = 0; i < m_dependents.size(); ++i)
  {
    const bool hot = (m_hoverCol == 2 && m_hoverIdx == i) || (m_hoverCol == 1);
    drawEdge(p, QPointF(m_center.rect.right(), m_center.rect.center().y()),
             QPointF(m_dependents[i].rect.left(), m_dependents[i].rect.center().y()),
             hot, false);
  }

  for (int i = 0; i < m_deps.size(); ++i)
  {
    drawNode(p, m_deps[i], m_hoverCol == 0 && m_hoverIdx == i);
  }
  if (m_hasCenter)
  {
    drawNode(p, m_center, m_hoverCol == 1);
  }
  for (int i = 0; i < m_dependents.size(); ++i)
  {
    drawNode(p, m_dependents[i], m_hoverCol == 2 && m_hoverIdx == i);
  }

  if (!m_hasCenter)
  {
    p.setPen(mutedCol);
    p.drawText(rect(), Qt::AlignCenter, tr("请先在左侧选择一个模组"));
    return;
  }

  // 列标题（三列常驻并带数量，空列画灰字占位，避免版面偏向一侧）
  const QRectF leftColRect(m_center.rect.left() - kColumnGap - kNodeWidth,
                           m_center.rect.top(), kNodeWidth, kNodeHeight);
  const QRectF rightColRect(m_center.rect.right() + kColumnGap,
                            m_center.rect.top(), kNodeWidth, kNodeHeight);
  QFont colFont = font();
  colFont.setPointSizeF(qMax(9.0, colFont.pointSizeF() - 1.0));
  p.setFont(colFont);
  p.setPen(mutedCol);
  const int headY = qMax(kMargin - 18, 4);
  auto drawColumnTitle = [this, &p, headY](const QRectF &colRect, const QString &text) {
    const qreal cx = colRect.center().x();
    p.drawText(QRectF(cx - kNodeWidth / 2, headY, kNodeWidth, 14),
               Qt::AlignHCenter | Qt::AlignVCenter, text);
  };
  drawColumnTitle(leftColRect, tr("前置依赖 · %1").arg(m_deps.size()));
  drawColumnTitle(m_center.rect, tr("当前模组"));
  drawColumnTitle(rightColRect, tr("被这些模组依赖 · %1").arg(m_dependents.size()));

  QFont ghostFont = colFont;
  ghostFont.setItalic(true);
  p.setFont(ghostFont);
  p.setPen(withAlpha(textCol, 80));
  if (m_deps.isEmpty())
  {
    p.drawText(QRectF(leftColRect.left(), height() / 2 - 9, kNodeWidth, 18),
               Qt::AlignCenter, tr("没有前置依赖"));
  }
  if (m_dependents.isEmpty())
  {
    p.drawText(QRectF(rightColRect.left(), height() / 2 - 9, kNodeWidth, 18),
               Qt::AlignCenter, tr("没有被其他模组依赖"));
  }
}

void ModRelationGraphWidget::drawNode(QPainter &p, const GraphNode &node,
                                      bool highlighted) const
{
  const QColor textCol = textColorToken();
  const QColor mutedCol = withAlpha(textCol, 130);
  const QColor accentCol = accentToken();
  const QColor missingCol(0xE1, 0x50, 0x4A); // 缺失前置的红（深浅主题下都可读）

  const bool isCenter = (node.rect == m_center.rect);

  QColor fill = withAlpha(textCol, 14);              // 常规节点：文字色的极淡底
  QColor border = withAlpha(borderToken(), 210);
  QColor titleCol = textCol;
  int borderW = 1;

  if (isCenter)
  {
    fill = withAlpha(accentCol, 32);
    border = accentCol;
    titleCol = accentCol;
    borderW = 2;
  }
  else if (!node.installed)
  {
    fill = withAlpha(textCol, 8);                    // 未安装：近乎镂空
    titleCol = mutedCol;
    border = node.required ? withAlpha(missingCol, 170)
                           : withAlpha(borderToken(), 120);
  }
  if (highlighted)
  {
    border = isCenter ? accentCol : textCol;
    borderW = 2;
  }

  p.setBrush(fill);
  QPen pen(border, borderW);
  if (!node.installed)
  {
    pen.setStyle(Qt::DashLine);
  }
  p.setPen(pen);
  p.drawRoundedRect(node.rect, 10, 10);

  // 标题（超出宽度省略）
  QFont titleFont = font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(qMax(9.5, titleFont.pointSizeF()));
  p.setFont(titleFont);
  p.setPen(titleCol);
  QFontMetrics fm(titleFont);
  const QString title = fm.elidedText(node.title, Qt::ElideRight, kNodeWidth - 20);
  p.drawText(QRectF(node.rect.left() + 10, node.rect.top() + 6, kNodeWidth - 20, 18),
             Qt::AlignVCenter | Qt::AlignLeft, title);

  // 副标题（版本 / 状态）
  QFont subFont = font();
  subFont.setPointSizeF(qMax(8.5, subFont.pointSizeF() - 1.0));
  p.setFont(subFont);
  p.setPen(node.installed || !node.required ? mutedCol : missingCol);
  QFontMetrics sfm(subFont);
  const QString sub = sfm.elidedText(node.subtitle, Qt::ElideRight, kNodeWidth - 20);
  p.drawText(QRectF(node.rect.left() + 10, node.rect.top() + kNodeHeight - 22,
                    kNodeWidth - 20, 16),
             Qt::AlignVCenter | Qt::AlignLeft, sub);
}

void ModRelationGraphWidget::drawEdge(QPainter &p, const QPointF &from,
                                      const QPointF &to, bool highlighted,
                                      bool dashed) const
{
  const QColor textCol = textColorToken();
  const QColor mutedCol = withAlpha(textCol, 90);
  const QColor accentCol = accentToken();

  QPainterPath path;
  path.moveTo(from);
  const qreal bend = qMin<qreal>(kColumnGap * 0.5, 44);
  path.cubicTo(QPointF(from.x() + bend, from.y()),
               QPointF(to.x() - bend, to.y()), to);

  QPen pen(highlighted ? accentCol : mutedCol, highlighted ? 2 : 1.4);
  if (dashed)
  {
    pen.setStyle(Qt::DashLine);
  }
  pen.setCapStyle(Qt::RoundCap);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);

  // 箭头（所有连线均自左向右，指向被依赖一侧）
  const QPointF ux(1, 0);
  const QPointF tip = to;
  const QPointF back = tip - ux * 8;
  QPolygonF arrow;
  arrow << tip
        << back + QPointF(0, 3.5)
        << back - QPointF(0, 3.5);
  p.setPen(Qt::NoPen);
  p.setBrush(highlighted ? accentCol : mutedCol);
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

  // ── 标题行：标题 + 模式切换 ──
  QHBoxLayout *titleRow = new QHBoxLayout();
  titleRow->setSpacing(8);

  QVBoxLayout *titleBox = new QVBoxLayout();
  titleBox->setSpacing(2);
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
    btn->setFixedSize(84, 26);
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
  titleRow->addWidget(modeSwitch);
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
  m_modListWidget->setFixedWidth(230);
  m_modListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  leftBox->addWidget(m_modListWidget, 1);
  contentRow->addLayout(leftBox);

  m_modeStack = new QStackedWidget(card);
  m_treeWidget = new QTreeWidget(m_modeStack);
  m_treeWidget->setObjectName(QStringLiteral("relationTree"));
  m_treeWidget->setColumnCount(3);
  m_treeWidget->setHeaderLabels(QStringList() << tr("模组") << tr("版本要求") << tr("状态"));
  m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_treeWidget->setUniformRowHeights(true);
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

  // ── 底部汇总（细分隔线 + 统计行） ──
  QFrame *divider = new QFrame(card);
  divider->setObjectName(QStringLiteral("relationDivider"));
  divider->setFrameShape(QFrame::NoFrame);
  divider->setFixedHeight(1);
  cardLayout->addWidget(divider);

  m_summaryLabel = new QLabel(card);
  m_summaryLabel->setObjectName(QStringLiteral("relationSummaryLabel"));
  cardLayout->addWidget(m_summaryLabel);

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
            rebuildTree();
            rebuildGraph();
          });

  rebuildModList();
}

void ModRelationDialog::rebuildModList()
{
  m_modListWidget->blockSignals(true);
  m_modListWidget->clear();
  for (int i = 0; i < m_mods.size(); ++i)
  {
    const ModInfo &info = m_mods[i];
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
    item->setToolTip(info.fileName);
  }
  m_modListWidget->blockSignals(false);

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
    empty->setData(0, kTitleColorRole, QColor(150, 150, 150));
    return;
  }

  const ModInfo &mod = m_mods[m_currentIndex];

  // 根节点用主题色加粗，与普通条目拉开层次
  const QColor accent = accentToken();
  const QColor versionCol = withAlpha(textColorToken(), 145);
  QFont rootFont = font();
  rootFont.setBold(true);

  // ── 根 1：前置依赖（已安装的前置可逐级展开其前置） ──
  const int depCount = mod.dependencies.size();
  QTreeWidgetItem *depRoot = new QTreeWidgetItem(
      m_treeWidget, QStringList() << tr("前置依赖（%1）").arg(depCount) << QString() << QString());
  depRoot->setFont(0, rootFont);
  depRoot->setData(0, kTitleColorRole, accent);

  m_treeVisited.clear();
  m_treeVisited.insert(modDisplayKey(mod).toLower());
  addDepTreeItems(depRoot, mod, 0);
  depRoot->setExpanded(true);

  // ── 根 2：被这些模组依赖 ──
  int dependentCount = 0;
  QTreeWidgetItem *usedRoot = new QTreeWidgetItem(
      m_treeWidget, QStringList() << tr("被这些模组依赖") << QString() << QString());
  usedRoot->setFont(0, rootFont);
  usedRoot->setData(0, kTitleColorRole, accent);
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
        item->setData(2, kStatusColorRole, withAlpha(textColorToken(), 145));
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
    empty->setData(0, kTitleColorRole, QColor(150, 150, 150));
  }
}

void ModRelationDialog::addDepTreeItems(QTreeWidgetItem *parent, const ModInfo &owner,
                                        int depth)
{
  // 状态列加圆点前缀便于扫读；●必需项 ○建议项
  for (const ModDependency &dep : owner.dependencies)
  {
    const int localIdx = findLocalIndex(dep.name);
    const bool installed = localIdx >= 0;
    const QString title = installed ? m_mods[localIdx].name : dep.name;

    QString status;
    QColor statusCol;
    if (installed && dep.isRequired)
    {
      status = tr("● 已安装");
      statusCol = QColor(0x2E, 0xA0, 0x4E);
    }
    else if (installed)
    {
      status = tr("● 已安装 · 建议");
      statusCol = QColor(0x2E, 0xA0, 0x4E);
    }
    else if (dep.isRequired)
    {
      status = tr("● 未安装");
      statusCol = QColor(0xE1, 0x50, 0x4A);
    }
    else
    {
      status = tr("○ 建议 · 未安装");
      statusCol = QColor(150, 150, 150);
    }

    const QString version = dep.version.isEmpty() || dep.version == QLatin1String("*")
                                ? tr("任意版本")
                                : dep.version;
    QTreeWidgetItem *item = new QTreeWidgetItem(
        parent, QStringList() << title << version << status);
    item->setData(1, kVersionColorRole, withAlpha(textColorToken(), 145)); // 版本要求弱化
    item->setData(2, kStatusColorRole, statusCol);
    item->setToolTip(0, installed ? m_mods[localIdx].fileName
                                  : tr("未在当前实例的 mods 目录找到 %1").arg(dep.name));
    if (!installed)
    {
      item->setData(0, kTitleColorRole, QColor(150, 150, 150));
    }

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

  QStringList parts;
  parts << tr("共 %1 个模组").arg(m_mods.size());
  parts << tr("当前：%1").arg(mod.name);
  parts << tr("前置 %1 个").arg(mod.dependencies.size());
  if (missing > 0)
  {
    parts << tr("缺失 %1 个").arg(missing);
  }
  if (suggested > 0)
  {
    parts << tr("建议 %1 个").arg(suggested);
  }
  parts << tr("被 %1 个模组依赖").arg(usedBy);
  m_summaryLabel->setText(parts.join(QStringLiteral("    ")));
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
