/**
 * @file   ProjectionFillShapeDialog.cpp
 * @brief  「填充方块」形状选择弹窗的实现（内嵌居中卡片 + QPainter 手绘剪影）
 * @author BlockBox Team
 * @date   2026-11-25
 */

#include "ProjectionFillShapeDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

#include "utils/StyleKit.h"

namespace {

/** 取 StyleKit 令牌解析出的颜色（本项目令牌一律为十六进制色值）。 */
QColor tokenColor(const char *token)
{
    return QColor(StyleKit::resolve(QString::fromLatin1(token)).trimmed());
}

/** 同色不同透明度：用来区分立体面的明暗，不引入任何新色值。 */
QColor tint(const QColor &base, int alpha)
{
    QColor c = base;
    c.setAlpha(alpha);
    return c;
}

// ── 等轴立方体的 7 个关键点（32×32 设计空间，y 向下） ──
const QPointF kCubeTop(16.0, 3.5);          // 顶面后角
const QPointF kCubeRight(27.0, 9.0);        // 右上角
const QPointF kCubeRightBottom(27.0, 22.5); // 右下角
const QPointF kCubeBottom(16.0, 28.0);      // 底面角
const QPointF kCubeLeftBottom(5.0, 22.5);   // 左下角
const QPointF kCubeLeft(5.0, 9.0);          // 左上角
const QPointF kCubeFrontTop(16.0, 14.5);    // 前上角（三面交汇）

/** 实心立方体：三个可见面用同一颜色的不同透明度，读起来就是一块实心方块。 */
void drawCubeFaces(QPainter &p, const QColor &ink)
{
    QPolygonF top;
    top << kCubeTop << kCubeRight << kCubeFrontTop << kCubeLeft;
    QPolygonF left;
    left << kCubeLeft << kCubeFrontTop << kCubeBottom << kCubeLeftBottom;
    QPolygonF right;
    right << kCubeFrontTop << kCubeRight << kCubeRightBottom << kCubeBottom;

    p.setPen(Qt::NoPen);
    p.setBrush(tint(ink, 250));
    p.drawPolygon(top);
    p.setBrush(tint(ink, 205));
    p.drawPolygon(left);
    p.setBrush(tint(ink, 150));
    p.drawPolygon(right);
}

/** 骨架立方体：只画可见的 9 条棱。 */
void drawCubeWire(QPainter &p, const QColor &ink, qreal width)
{
    QPen pen(tint(ink, 245));
    pen.setWidthF(width);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    QPolygonF outline;
    outline << kCubeTop << kCubeRight << kCubeRightBottom << kCubeBottom
            << kCubeLeftBottom << kCubeLeft << kCubeTop;
    p.drawPolyline(outline);
    p.drawLine(kCubeLeft, kCubeFrontTop);
    p.drawLine(kCubeRight, kCubeFrontTop);
    p.drawLine(kCubeBottom, kCubeFrontTop);
}

/** 圆角三角形（三角形板用）。 */
void drawRoundedTriangle(QPainter &p, const QColor &ink)
{
    QPolygonF tri;
    tri << QPointF(16.0, 5.0) << QPointF(28.5, 27.0) << QPointF(3.5, 27.0);

    QPen pen(tint(ink, 250));
    pen.setWidthF(1.6);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(tint(ink, 205));
    p.drawPolygon(tri);
}

/** 圆柱：下底椭圆 + 柱身 + 两侧棱 + 顶面椭圆。 */
void drawCylinder(QPainter &p, const QColor &ink)
{
    const QRectF topEllipse(8.0, 5.0, 16.0, 7.0);
    const QRectF bottomEllipse(8.0, 20.0, 16.0, 7.0);

    p.setPen(Qt::NoPen);
    p.setBrush(tint(ink, 150));
    p.drawEllipse(bottomEllipse);         // 下底面
    p.drawRect(QRectF(8.0, 8.5, 16.0, 15.0)); // 柱身

    QPen side(tint(ink, 240));
    side.setWidthF(1.4);
    p.setPen(side);
    p.drawLine(QPointF(8.0, 8.5), QPointF(8.0, 23.5));
    p.drawLine(QPointF(24.0, 8.5), QPointF(24.0, 23.5));

    QPen rim(tint(ink, 250));
    rim.setWidthF(1.4);
    p.setPen(rim);
    p.setBrush(tint(ink, 235));
    p.drawEllipse(topEllipse);            // 顶面
}

} // namespace

// ───────────────────────────── 构造 ─────────────────────────────

ProjectionFillShapeDialog::ProjectionFillShapeDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("projectionFillShapeDialog"));
    setWindowTitle(tr("填充方块"));
    initUI();
}

ProjectionFillShape ProjectionFillShapeDialog::selectedShape() const
{
    if (m_shapeGroup) {
        const int id = m_shapeGroup->checkedId();
        if (id >= 0 && id < static_cast<int>(ProjectionFillShape::Count))
            return static_cast<ProjectionFillShape>(id);
    }
    return ProjectionFillShape::Solid;
}

bool ProjectionFillShapeDialog::airOnly() const
{
    return m_airBox && m_airBox->isChecked();
}

// ───────────────────────────── 界面 ─────────────────────────────

void ProjectionFillShapeDialog::initUI()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 居中内容卡片：objectName 必须以 Card 结尾，AppDialogBase 靠它定位/压屏
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("projectionFillShapeCard"));
    card->setMinimumWidth(640);
    card->setMaximumWidth(720);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(26, 22, 26, 22);
    cardLayout->setSpacing(12);

    // ---- 顶部：图标 + 标题 + 副标题 ----
    QHBoxLayout *headRow = new QHBoxLayout();
    headRow->setContentsMargins(0, 0, 0, 0);
    headRow->setSpacing(14);

    QLabel *iconLabel = new QLabel(card);
    iconLabel->setFixedSize(40, 40);
    iconLabel->setPixmap(makeHeaderPixmap());
    headRow->addWidget(iconLabel, 0, Qt::AlignTop);

    QVBoxLayout *titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(3);

    QLabel *titleLabel = new QLabel(tr("填充方块"), card);
    titleLabel->setObjectName(QStringLiteral("pfsTitle"));
    titleCol->addWidget(titleLabel);

    QLabel *subtitleLabel =
        new QLabel(tr("选一个形状，然后在视图里点两个对角方块生成"), card);
    subtitleLabel->setObjectName(QStringLiteral("pfsSubtitle"));
    subtitleLabel->setWordWrap(true);
    titleCol->addWidget(subtitleLabel);

    headRow->addLayout(titleCol, 1);
    cardLayout->addLayout(headRow);

    // ---- 形状网格：两行，每行 4 个，互斥 ----
    m_shapeGroup = new QButtonGroup(this);
    m_shapeGroup->setExclusive(true);

    for (int group = 0; group < 2; ++group) {
        QVBoxLayout *groupCol = new QVBoxLayout();
        groupCol->setContentsMargins(0, 0, 0, 0);
        groupCol->setSpacing(6);

        QLabel *groupTitle = new QLabel(card);
        groupTitle->setObjectName(QStringLiteral("pfsGroupTitle"));
        groupTitle->setText(group == 0 ? tr("方块填充") : tr("平面与立体形状"));
        groupCol->addWidget(groupTitle);

        QGridLayout *grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(8);

        int col = 0;
        for (int i = 0; i < static_cast<int>(ProjectionFillShape::Count); ++i) {
            const ProjectionFillShape shape = static_cast<ProjectionFillShape>(i);
            if (ProjectionFillShapes::groupOf(shape) != group)
                continue;
            QToolButton *btn = makeShapeButton(shape, card);
            m_shapeButtons[i] = btn;
            // 固定每行 4 个：形状再多也只会向下换行，不会把一行挤满
            grid->addWidget(btn, col / 4, col % 4);
            grid->setColumnStretch(col % 4, 1);
            ++col;
        }

        groupCol->addLayout(grid);
        cardLayout->addLayout(groupCol);
    }

    // ---- 动态说明：随选中形状变化 ----
    m_descLabel = new QLabel(card);
    m_descLabel->setObjectName(QStringLiteral("pfsDesc"));
    m_descLabel->setWordWrap(true);
    m_descLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // 说明长短不一，留出两行高度，避免切换形状时弹窗高度跳动
    m_descLabel->setMinimumHeight(34);
    cardLayout->addWidget(m_descLabel);

    // ---- 只填充空气 ----
    m_airBox = new QCheckBox(tr("只填充空气（不覆盖已有方块）"), card);
    m_airBox->setObjectName(QStringLiteral("pfsAirBox"));
    m_airBox->setChecked(false);
    m_airBox->setCursor(Qt::PointingHandCursor);
    m_airBox->setToolTip(tr("勾选后只往空位里填，范围内已有的方块保持原样；\n"
                            "不勾选则直接用当前方块覆盖整个形状。"));
    cardLayout->addWidget(m_airBox);

    // ---- 底部按钮行（右对齐） ----
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(10);
    btnRow->addStretch(1);

    m_cancelBtn = new QPushButton(tr("取消"), card);
    m_cancelBtn->setObjectName(QStringLiteral("pfsBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setAutoDefault(false);
    m_cancelBtn->setToolTip(tr("关闭弹窗，不做任何填充（Esc）"));
    btnRow->addWidget(m_cancelBtn);

    m_coordBtn = new QPushButton(tr("按坐标填充…"), card);
    m_coordBtn->setObjectName(QStringLiteral("pfsBtn"));
    m_coordBtn->setCursor(Qt::PointingHandCursor);
    m_coordBtn->setAutoDefault(false);
    m_coordBtn->setToolTip(tr("手工输入起止坐标来生成形状，不用在视图里点方块"));
    btnRow->addWidget(m_coordBtn);

    m_startBtn = new QPushButton(tr("开始选点"), card);
    m_startBtn->setObjectName(QStringLiteral("pfsPrimaryBtn"));
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setDefault(true);
    m_startBtn->setAutoDefault(true);
    m_startBtn->setToolTip(tr("回到 3D 视图，依次点两个对角方块来圈定范围（Enter）"));
    btnRow->addWidget(m_startBtn);

    cardLayout->addLayout(btnRow);

    // ---- 信号 ----
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_coordBtn, &QPushButton::clicked, this, [this]() {
        commit(Mode::Coordinates);
    });
    connect(m_startBtn, &QPushButton::clicked, this, [this]() {
        commit(Mode::PickTwoPoints);
    });
    // 任何形式的 reject（取消按钮 / 点遮罩 / Esc）都把出口归零
    connect(this, &QDialog::rejected, this, [this]() { m_mode = Mode::Cancel; });

    // 默认选中「实心」
    QToolButton *solidBtn = m_shapeButtons[static_cast<int>(ProjectionFillShape::Solid)];
    if (solidBtn)
        solidBtn->setChecked(true);
    updateDescription();

    initStyle(card);
    rootLayout->addWidget(card, 0, Qt::AlignCenter);

    // 卡片高度必须按内容算出来：基类在 showEvent 里会把卡片布局包进滚动区
    // （小屏兜底），若卡片没给最小高度，就会被滚动区的默认 sizeHint 压扁，
    // 内容只能拖动滚动条看。这里用布局的 heightForWidth 取 640 宽下的真实高度，
    // 再留 2px 给卡片上下描边；屏幕更小时基类仍会把它压回屏内并改为滚动。
    card->ensurePolished();
    int contentHeight = card->heightForWidth(card->minimumWidth());
    if (contentHeight <= 0)
        contentHeight = cardLayout->sizeHint().height();
    if (contentHeight > 0)
        card->setMinimumHeight(contentHeight + 2);
}

void ProjectionFillShapeDialog::initStyle(QWidget *card)
{
    card->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QWidget#projectionFillShapeCard {"
        "    background-color: @BG_CARD@;"
        "    border: 1px solid @BORDER@;"
        "    border-radius: 16px;"
        "}"
        "QLabel#pfsTitle {"
        "    color: @TEXT_PRIMARY@; font-size: 16px; font-weight: 700;"
        "    background: transparent;"
        "}"
        "QLabel#pfsSubtitle {"
        "    color: @TEXT_TERTIARY@; font-size: 12px; background: transparent;"
        "}"
        "QLabel#pfsGroupTitle {"
        "    color: @TEXT_SECONDARY@; font-size: 12px; font-weight: 600;"
        "    background: transparent;"
        "}"
        "QLabel#pfsDesc {"
        "    color: @TEXT_TERTIARY@; font-size: 12px; background: transparent;"
        "}"
        "QToolButton#pfsShapeBtn {"
        "    background-color: @BG_CONTENT@;"
        "    color: @TEXT_SECONDARY@;"
        "    border: 1px solid @BORDER@;"
        "    border-radius: 10px;"
        "    padding: 6px 4px;"
        "    font-size: 12px;"
        "}"
        "QToolButton#pfsShapeBtn:hover {"
        "    background-color: @BG_HOVER@;"
        "    border-color: @BORDER_STRONG@;"
        "    color: @TEXT_PRIMARY@;"
        "}"
        "QToolButton#pfsShapeBtn:checked {"
        "    background-color: @PRIMARY_RGBA_20@;"
        "    border: 1px solid @PRIMARY@;"
        "    color: @PRIMARY@;"
        "}"
        "QToolButton#pfsShapeBtn:checked:hover {"
        "    background-color: @PRIMARY_RGBA_26@;"
        "}"
        "QCheckBox#pfsAirBox {"
        "    color: @TEXT_SECONDARY@; font-size: 13px; spacing: 8px;"
        "    background: transparent;"
        "}"
        "QCheckBox#pfsAirBox::indicator {"
        "    width: 16px; height: 16px; border-radius: 4px;"
        "    border: 1px solid @BORDER_STRONG@; background-color: @BG_CONTENT@;"
        "}"
        "QCheckBox#pfsAirBox::indicator:hover {"
        "    border-color: @PRIMARY@;"
        "}"
        "QCheckBox#pfsAirBox::indicator:checked {"
        "    background-color: @PRIMARY@; border-color: @PRIMARY@;"
        "}"
        "QPushButton#pfsBtn {"
        "    background-color: @BG_CONTENT@;"
        "    color: @TEXT_SECONDARY@;"
        "    border: 1px solid @BORDER_STRONG@;"
        "    border-radius: 10px;"
        "    padding: 8px 22px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#pfsBtn:hover {"
        "    background-color: @BG_HOVER@; color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#pfsBtn:pressed {"
        "    background-color: @BG_PRESSED@;"
        "}"
        "QPushButton#pfsPrimaryBtn {"
        "    background-color: @PRIMARY@;"
        "    color: @TEXT_ON_PRIMARY@;"
        "    border: none;"
        "    border-radius: 10px;"
        "    padding: 8px 26px;"
        "    font-size: 13px;"
        "    font-weight: 600;"
        "}"
        "QPushButton#pfsPrimaryBtn:hover {"
        "    background-color: @PRIMARY_HOVER@;"
        "}"
        "QPushButton#pfsPrimaryBtn:pressed {"
        "    background-color: @PRIMARY_PRESSED@;"
        "}"
    )));
}

QToolButton *ProjectionFillShapeDialog::makeShapeButton(ProjectionFillShape shape,
                                                        QWidget *parent)
{
    QToolButton *btn = new QToolButton(parent);
    btn->setObjectName(QStringLiteral("pfsShapeBtn"));
    btn->setText(ProjectionFillShapes::displayName(shape));
    btn->setIcon(makeShapeIcon(shape));
    btn->setIconSize(QSize(34, 34));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setCheckable(true);
    btn->setFixedHeight(76);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(ProjectionFillShapes::description(shape));
    m_shapeGroup->addButton(btn, static_cast<int>(shape));

    // 互斥组只在被选中那一下刷新说明，取消选中的那个不用管
    connect(btn, &QToolButton::toggled, this, [this](bool checked) {
        if (checked)
            updateDescription();
    });
    return btn;
}

void ProjectionFillShapeDialog::updateDescription()
{
    if (!m_descLabel)
        return;
    m_descLabel->setText(ProjectionFillShapes::description(selectedShape()));
}

void ProjectionFillShapeDialog::commit(Mode mode)
{
    m_mode = mode;
    accept();
}

void ProjectionFillShapeDialog::keyPressEvent(QKeyEvent *event)
{
    // Enter 固定走主按钮「开始选点」：焦点可能停在形状卡片或复选框上，
    // 不能让它们把这下回车吞掉。Esc 交给基类（reject → 出口为 Cancel）。
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        commit(Mode::PickTwoPoints);
        return;
    }
    AppDialogBase::keyPressEvent(event);
}

// ───────────────────────────── 手绘图标 ─────────────────────────────

QPixmap ProjectionFillShapeDialog::makeHeaderPixmap() const
{
    const int px = 40;
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(qMax(1, qRound(px * dpr)), qMax(1, qRound(px * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(px / 40.0, px / 40.0); // 40×40 设计空间

    const QColor accent = tokenColor("@PRIMARY@");
    p.setPen(Qt::NoPen);
    p.setBrush(tint(accent, 32));
    p.drawRoundedRect(QRectF(0.5, 0.5, 39.0, 39.0), 11.0, 11.0);

    p.save();
    p.translate(6.0, 6.5);
    p.scale(28.0 / 32.0, 28.0 / 32.0);
    drawCubeFaces(p, accent);
    p.restore();

    return pm;
}

QIcon ProjectionFillShapeDialog::makeShapeIcon(ProjectionFillShape shape) const
{
    const int px = 34;
    QIcon icon;
    // 未选中用次要文字色、选中用主题色：QToolButton 的 checked 状态会取 On
    icon.addPixmap(shapePixmap(shape, px, tokenColor("@TEXT_SECONDARY@")),
                   QIcon::Normal, QIcon::Off);
    icon.addPixmap(shapePixmap(shape, px, tokenColor("@PRIMARY@")),
                   QIcon::Normal, QIcon::On);
    return icon;
}

QPixmap ProjectionFillShapeDialog::shapePixmap(ProjectionFillShape shape, int px,
                                               const QColor &ink) const
{
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(qMax(1, qRound(px * dpr)), qMax(1, qRound(px * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(px / 32.0, px / 32.0); // 统一在 32×32 设计空间里画

    switch (shape)
    {
    case ProjectionFillShape::Solid:
        drawCubeFaces(p, ink);
        break;

    case ProjectionFillShape::Hollow:
    {
        // 外壳：先画实心立方体，再把内芯擦掉（组合模式 Clear），留下壳子
        drawCubeFaces(p, ink);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.setPen(Qt::NoPen);
        p.setBrush(tint(ink, 255)); // Clear 模式不取颜色，只用形状
        QPolygonF core;
        core << QPointF(16.0, 9.8) << QPointF(21.2, 12.6) << QPointF(21.2, 19.0)
             << QPointF(16.0, 21.7) << QPointF(10.8, 19.0) << QPointF(10.8, 12.6);
        p.drawPolygon(core);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        break;
    }

    case ProjectionFillShape::Frame:
        drawCubeWire(p, ink, 2.0);
        break;

    case ProjectionFillShape::SlantedFloor:
    {
        // 斜坡面：一块往右上抬起的板，加两道分层棱线体现「逐层升高」
        QPolygonF deck;
        deck << QPointF(3.0, 25.5) << QPointF(11.5, 12.0)
             << QPointF(28.5, 17.5) << QPointF(20.0, 29.0);
        p.setPen(Qt::NoPen);
        p.setBrush(tint(ink, 235));
        p.drawPolygon(deck);

        QPolygonF thickness;
        thickness << QPointF(3.0, 25.5) << QPointF(20.0, 29.0)
                  << QPointF(20.0, 31.0) << QPointF(3.0, 27.5);
        p.setBrush(tint(ink, 130));
        p.drawPolygon(thickness);

        QPen step(tint(ink, 110));
        step.setWidthF(1.0);
        p.setPen(step);
        p.drawLine(QPointF(8.8, 21.0), QPointF(26.0, 26.5));
        p.drawLine(QPointF(5.9, 25.2), QPointF(23.1, 30.2));
        break;
    }

    case ProjectionFillShape::Square:
    {
        QPen pen(tint(ink, 250));
        pen.setWidthF(1.6);
        p.setPen(pen);
        p.setBrush(tint(ink, 205));
        p.drawRoundedRect(QRectF(6.5, 6.5, 19.0, 19.0), 2.5, 2.5);
        break;
    }

    case ProjectionFillShape::Triangle:
        drawRoundedTriangle(p, ink);
        break;

    case ProjectionFillShape::Circle:
    {
        QPen pen(tint(ink, 250));
        pen.setWidthF(1.6);
        p.setPen(pen);
        p.setBrush(tint(ink, 205));
        p.drawEllipse(QRectF(5.5, 5.5, 21.0, 21.0));
        break;
    }

    case ProjectionFillShape::Cylinder:
        drawCylinder(p, ink);
        break;

    case ProjectionFillShape::Count:
    default:
        break;
    }

    return pm;
}
