/**
 * @file   TouchLayoutEditorDialog.cpp
 * @brief  自定义触屏键位布局编辑器实现（含可视化键盘选键）
 * @author BlockBox Team
 * @date   2026-11-21
 */
#include "TouchLayoutEditorDialog.h"

#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTouchEvent>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

using namespace MobileControlSettings;

namespace {

/// 摇杆有效半径按布局半径外扩一点（与悬浮层一致）
constexpr qreal kHitSlop = 1.18;

/// 两点欧氏距离（QPointF 没有现成的 length 方法）
inline qreal pointDist(const QPointF &a, const QPointF &b)
{
    const QPointF d = a - b;
    return qSqrt(d.x() * d.x() + d.y() * d.y());
}

} // namespace

QString touchKeyDisplayName(const QString &key)
{
    if (key == QLatin1String("Space"))     return QStringLiteral("空格");
    if (key == QLatin1String("Enter"))     return QStringLiteral("回车");
    if (key == QLatin1String("Backspace")) return QStringLiteral("退格");
    if (key == QLatin1String("CapsLock"))  return QStringLiteral("Caps");
    if (key == QLatin1String("Up"))        return QStringLiteral("↑");
    if (key == QLatin1String("Down"))      return QStringLiteral("↓");
    if (key == QLatin1String("Left"))      return QStringLiteral("←");
    if (key == QLatin1String("Right"))     return QStringLiteral("→");
    return key;
}

// ── 预览画布 ──

TouchLayoutCanvas::TouchLayoutCanvas(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("touchLayoutCanvas"));
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setCursor(Qt::OpenHandCursor);
}

void TouchLayoutCanvas::setLayout(const QVector<TouchControl> &layout)
{
    m_layout = layout;
    if (m_selected >= m_layout.size())
        m_selected = m_layout.isEmpty() ? -1 : 0;
    update();
}

void TouchLayoutCanvas::setSelectedIndex(int index)
{
    m_selected = index;
    update();
}

void TouchLayoutCanvas::setMirror(bool mirror)
{
    if (m_mirror == mirror)
        return;
    m_mirror = mirror;
    update();
}

QPointF TouchLayoutCanvas::controlCenter(const TouchControl &control) const
{
    // 与游戏内悬浮层一致：左手持机镜像横坐标显示，不改保存值
    const qreal x = m_mirror ? 1.0 - control.x : control.x;
    return QPointF(x * width(), control.y * height());
}

qreal TouchLayoutCanvas::controlRadius(const TouchControl &control) const
{
    const qreal base = qBound<qreal>(22, qMin(width(), height()) / 13.0, 72);
    qreal scale = control.size / 100.0;
    switch (buttonSize()) {
    case ButtonSize::Small:  scale *= 0.85; break;
    case ButtonSize::Large:  scale *= 1.2;  break;
    case ButtonSize::Standard:
    default:                 break;
    }
    return base * (control.joystick ? 1.15 : 1.0) * scale;
}

int TouchLayoutCanvas::hitControl(const QPointF &pos) const
{
    for (int i = 0; i < m_layout.size(); ++i) {
        const TouchControl &control = m_layout.at(i);
        if (pointDist(pos, controlCenter(control)) <= controlRadius(control) * kHitSlop)
            return i;
    }
    return -1;
}

void TouchLayoutCanvas::beginDrag(int index, const QPointF &pos)
{
    m_dragIndex = index;
    m_dragOffset = pos - controlCenter(m_layout.at(index));
    setCursor(Qt::ClosedHandCursor);
}

void TouchLayoutCanvas::dragTo(const QPointF &pos)
{
    if (m_dragIndex < 0 || m_dragIndex >= m_layout.size())
        return;
    TouchControl &control = m_layout[m_dragIndex];
    const QPointF target = pos - m_dragOffset;
    qreal nx = qBound(0.03, target.x() / width(), 0.97);
    qreal ny = qBound(0.05, target.y() / height(), 0.95);
    // 镜像显示下把屏幕坐标换算回保存的布局坐标
    if (m_mirror)
        nx = 1.0 - nx;
    control.x = nx;
    control.y = ny;
    emit controlMoved(m_dragIndex, nx, ny);
    update();
}

void TouchLayoutCanvas::endDrag()
{
    m_dragIndex = -1;
    setCursor(Qt::OpenHandCursor);
}

bool TouchLayoutCanvas::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        auto *touch = static_cast<QTouchEvent *>(event);
        // 画布上只跟一个触点：按下选中并开始拖动，抬起结束
        for (const QEventPoint &point : touch->points()) {
            const QPointF pos = point.position();
            if (point.state() == QEventPoint::Pressed && m_touchPointId < 0) {
                m_touchPointId = point.id();
                const int index = hitControl(pos);
                if (m_selected != index) {
                    m_selected = index;
                    emit selectionChanged(index);
                }
                if (index >= 0)
                    beginDrag(index, pos);
                update();
            } else if (point.id() == m_touchPointId) {
                if (point.state() == QEventPoint::Released) {
                    endDrag();
                    m_touchPointId = -1;
                } else if (m_dragIndex >= 0) {
                    dragTo(pos);
                }
            }
        }
        event->accept();
        return true;
    }
    default:
        return QWidget::event(event);
    }
}

void TouchLayoutCanvas::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const int index = hitControl(pos);
    if (m_selected != index) {
        m_selected = index;
        emit selectionChanged(index);
    }
    if (index >= 0)
        beginDrag(index, pos);
    update();
    event->accept();
}

void TouchLayoutCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragIndex >= 0)
        dragTo(event->position());
    event->accept();
}

void TouchLayoutCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    endDrag();
}

void TouchLayoutCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 模拟游戏画面的深色底，右侧一半带淡淡的"视角区域"提示
    painter.fillRect(rect(), QColor(24, 26, 30));
    painter.setPen(QColor(255, 255, 255, 26));
    painter.drawLine(width() / 2, 0, width() / 2, height());
    painter.setPen(QColor(255, 255, 255, 40));
    painter.drawText(QRect(width() / 2, 4, width() / 2, 18),
                     Qt::AlignCenter, tr("右半屏 = 拖动视角"));
    painter.drawText(QRect(4, height() - 22, width() / 2 - 8, 18),
                     Qt::AlignLeft | Qt::AlignVCenter, tr("拖动按钮调整位置"));

    const int baseAlpha = qBound(30, buttonOpacity(), 100);
    const int fillAlpha = int(baseAlpha * 0.55);
    const int strokeAlpha = int(baseAlpha * 0.85);

    for (int i = 0; i < m_layout.size(); ++i) {
        const TouchControl &control = m_layout.at(i);
        const QPointF center = controlCenter(control);
        const qreal r = controlRadius(control);
        const QRectF circle(center.x() - r, center.y() - r, r * 2.0, r * 2.0);
        const bool selected = (i == m_selected);

        QPainterPath path;
        path.addEllipse(circle);
        painter.fillPath(path, QColor(20, 22, 26, fillAlpha));
        painter.setPen(QPen(selected ? QColor(82, 190, 140, 255)
                                     : QColor(235, 238, 245, strokeAlpha),
                            selected ? 3 : 2));
        painter.drawPath(path);

        if (control.joystick) {
            // 摇杆画中心钮示意
            const qreal knob = r * 0.42;
            QPainterPath knobPath;
            knobPath.addEllipse(QRectF(center.x() - knob, center.y() - knob,
                                       knob * 2.0, knob * 2.0));
            painter.fillPath(knobPath, QColor(235, 238, 245, strokeAlpha));
            continue;
        }

        QString text = control.label;
        if (text.isEmpty())
            text = defaultActionName(control.action);
        if (text.isEmpty())
            text = tr("按钮");

        painter.setPen(QColor(235, 238, 245, qMin(255, strokeAlpha + 20)));
        QFont font = this->font();
        const int chars = text.length();
        font.setPixelSize(int(r * (chars <= 1 ? 0.62 : 0.44)));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.drawText(circle, Qt::AlignCenter, text);
    }
}

// ── 可视化电脑键盘 ──

TouchKeyKeyboard::TouchKeyKeyboard(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("touchKeyKeyboard"));
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    buildKeyRows();
    setMinimumHeight(160);
}

void TouchKeyKeyboard::buildKeyRows()
{
    // 按真实电脑键盘排布；units 是相对宽度（普通键 = 1），
    // 每行按单位占比分宽度，视觉上接近实体键盘的键帽宽窄
    const auto push = [](QVector<KeyCap> &row, const QString &key,
                         const QString &display, qreal units) {
        KeyCap cap;
        cap.key = key;
        cap.display = display.isEmpty() ? touchKeyDisplayName(key) : display;
        cap.units = units;
        row.append(cap);
    };

    QVector<KeyCap> row;
    // 功能键行
    push(row, QStringLiteral("Esc"), QString(), 1.3);
    for (int f = 1; f <= 12; ++f)
        push(row, QStringLiteral("F%1").arg(f), QString(), 1.0);
    m_rows.append(row);

    row.clear();
    // 数字行
    push(row, QStringLiteral("`"), QStringLiteral("`"), 1.0);
    for (const QString &d : {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                             QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
                             QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"),
                             QStringLiteral("0")})
        push(row, d, d, 1.0);
    push(row, QStringLiteral("-"), QStringLiteral("-"), 1.0);
    push(row, QStringLiteral("="), QStringLiteral("="), 1.0);
    push(row, QStringLiteral("Backspace"), QString(), 1.9);
    m_rows.append(row);

    row.clear();
    // QWERTY 行
    push(row, QStringLiteral("Tab"), QString(), 1.6);
    for (const QString &k : {QStringLiteral("Q"), QStringLiteral("W"), QStringLiteral("E"),
                             QStringLiteral("R"), QStringLiteral("T"), QStringLiteral("Y"),
                             QStringLiteral("U"), QStringLiteral("I"), QStringLiteral("O"),
                             QStringLiteral("P")})
        push(row, k, k, 1.0);
    push(row, QStringLiteral("["), QStringLiteral("["), 1.0);
    push(row, QStringLiteral("]"), QStringLiteral("]"), 1.0);
    push(row, QStringLiteral("\\"), QStringLiteral("\\"), 1.0);
    m_rows.append(row);

    row.clear();
    // ASDF 行
    push(row, QStringLiteral("CapsLock"), QString(), 1.8);
    for (const QString &k : {QStringLiteral("A"), QStringLiteral("S"), QStringLiteral("D"),
                             QStringLiteral("F"), QStringLiteral("G"), QStringLiteral("H"),
                             QStringLiteral("J"), QStringLiteral("K"), QStringLiteral("L")})
        push(row, k, k, 1.0);
    push(row, QStringLiteral(";"), QStringLiteral(";"), 1.0);
    push(row, QStringLiteral("'"), QStringLiteral("'"), 1.0);
    push(row, QStringLiteral("Enter"), QString(), 1.8);
    m_rows.append(row);

    row.clear();
    // ZXCV 行（左右 Shift 都可选）
    push(row, QStringLiteral("Shift"), QStringLiteral("Shift"), 2.2);
    for (const QString &k : {QStringLiteral("Z"), QStringLiteral("X"), QStringLiteral("C"),
                             QStringLiteral("V"), QStringLiteral("B"), QStringLiteral("N"),
                             QStringLiteral("M")})
        push(row, k, k, 1.0);
    push(row, QStringLiteral(","), QStringLiteral(","), 1.0);
    push(row, QStringLiteral("."), QStringLiteral("."), 1.0);
    push(row, QStringLiteral("/"), QStringLiteral("/"), 1.0);
    push(row, QStringLiteral("Shift"), QStringLiteral("Shift"), 2.2);
    m_rows.append(row);

    row.clear();
    // 底部：修饰键 + 空格 + 方向键
    push(row, QStringLiteral("Ctrl"), QStringLiteral("Ctrl"), 1.9);
    push(row, QStringLiteral("Alt"), QStringLiteral("Alt"), 1.9);
    push(row, QStringLiteral("Space"), QString(), 6.0);
    push(row, QStringLiteral("Left"), QString(), 1.3);
    push(row, QStringLiteral("Up"), QString(), 1.3);
    push(row, QStringLiteral("Down"), QString(), 1.3);
    push(row, QStringLiteral("Right"), QString(), 1.3);
    m_rows.append(row);
}

void TouchKeyKeyboard::setSelectedKeys(const QStringList &keys)
{
    if (m_selectedKeys == keys)
        return;
    m_selectedKeys = keys;
    update();
}

void TouchKeyKeyboard::setKeysEnabled(bool enabled)
{
    if (m_keysEnabled == enabled)
        return;
    m_keysEnabled = enabled;
    update();
}

void TouchKeyKeyboard::relayoutKeys()
{
    const qreal gap = 3.0;
    const qreal keyH = qBound<qreal>(20.0, width() / 15.0, 34.0);
    qreal y = 0;
    for (QVector<KeyCap> &row : m_rows) {
        qreal totalUnits = 0;
        for (const KeyCap &cap : row)
            totalUnits += cap.units;
        const qreal usable = width() - gap * (row.size() - 1);
        qreal x = 0;
        for (KeyCap &cap : row) {
            const qreal w = usable * cap.units / totalUnits;
            cap.rect = QRectF(x, y, w, keyH);
            x += w + gap;
        }
        y += keyH + gap;
    }
    // 按布局实际高度占位，避免被压缩或留白一大截；值没变不动，
    // 防止绘制期反复触发布局失效
    const int minHeight = int(y - gap + 0.5);
    if (minimumHeight() != minHeight)
        setMinimumHeight(minHeight);
}

int TouchKeyKeyboard::hitKey(const QPointF &pos) const
{
    for (int r = 0; r < m_rows.size(); ++r) {
        const QVector<KeyCap> &row = m_rows.at(r);
        for (int i = 0; i < row.size(); ++i) {
            if (row.at(i).rect.contains(pos))
                return r * 1000 + i; // 行 × 1000 + 列，展开成扁平下标
        }
    }
    return -1;
}

void TouchKeyKeyboard::toggleKey(int index)
{
    const int rowIdx = index / 1000;
    const int keyIdx = index % 1000;
    if (rowIdx < 0 || rowIdx >= m_rows.size())
        return;
    const QVector<KeyCap> &row = m_rows.at(rowIdx);
    if (keyIdx < 0 || keyIdx >= row.size())
        return;

    const QString key = row.at(keyIdx).key;
    // 点一下选中、再点一下取消；点选顺序就是组合键里的顺序
    if (m_selectedKeys.contains(key))
        m_selectedKeys.removeAll(key);
    else
        m_selectedKeys.append(key);
    emit selectionChanged(m_selectedKeys);
    update();
}

bool TouchKeyKeyboard::event(QEvent *event)
{
    if (!m_keysEnabled)
        return QWidget::event(event);

    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        auto *touch = static_cast<QTouchEvent *>(event);
        for (const QEventPoint &point : touch->points()) {
            if (point.state() == QEventPoint::Pressed) {
                const int index = hitKey(point.position());
                if (index >= 0)
                    toggleKey(index);
            }
        }
        event->accept();
        return true;
    }
    default:
        return QWidget::event(event);
    }
}

void TouchKeyKeyboard::mousePressEvent(QMouseEvent *event)
{
    if (!m_keysEnabled)
        return;
    const int index = hitKey(event->position());
    if (index >= 0)
        toggleKey(index);
    event->accept();
}

void TouchKeyKeyboard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    relayoutKeys();

    if (!m_keysEnabled)
        painter.setOpacity(0.35); // 不可选时整块键盘置灰

    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QColor capFill = isLight ? QColor(236, 238, 242, 235) : QColor(58, 60, 66, 225);
    const QColor capBorder = isLight ? QColor(205, 208, 214, 255) : QColor(86, 88, 95, 255);
    const QColor capText = isLight ? QColor(51, 51, 51) : QColor(232, 232, 232);
    const QColor selectedFill(tm->currentThemeColor());

    QFont font = this->font();
    font.setPixelSize(11);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);

    for (const QVector<KeyCap> &row : m_rows) {
        for (const KeyCap &cap : row) {
            const bool selected = m_selectedKeys.contains(cap.key);
            QPainterPath path;
            path.addRoundedRect(cap.rect, 5, 5);
            painter.fillPath(path, selected ? selectedFill : capFill);
            painter.setPen(QPen(selected ? selectedFill.darker(130) : capBorder, 1));
            painter.drawPath(path);

            painter.setPen(selected ? QColor(255, 255, 255) : capText);
            painter.drawText(cap.rect, Qt::AlignCenter, cap.display);
        }
    }
}

// ── 编辑器弹窗 ──

TouchLayoutEditorDialog::TouchLayoutEditorDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("touchLayoutEditorDialog"));
    setWindowTitle(tr("自定义键位布局"));

    m_layout = touchLayout();

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("touchLayoutEditorCard"));
    card->setMaximumWidth(940);
    card->setMaximumHeight(700);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 18, 22, 18);
    cardLayout->setSpacing(10);

    QLabel *title = new QLabel(windowTitle(), card);
    title->setObjectName(QStringLiteral("appDialogTitleLabel"));
    cardLayout->addWidget(title);

    // 小屏（手机横屏）容不下整块内容：内容装进无框滚动区，滚动浏览
    QScrollArea *scroll = new QScrollArea(card);
    scroll->setObjectName(QStringLiteral("touchLayoutEditorScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *bodyWidget = new QWidget(scroll);
    bodyWidget->setObjectName(QStringLiteral("touchLayoutEditorBody"));
    QVBoxLayout *bodyLayout = new QVBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(10);

    QHBoxLayout *body = new QHBoxLayout();
    body->setSpacing(14);

    m_canvas = new TouchLayoutCanvas(bodyWidget);
    m_canvas->setMinimumSize(340, 180);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_canvas->setMirror(handedness() == Handedness::Left);
    body->addWidget(m_canvas, 3);

    QWidget *panel = new QWidget(bodyWidget);
    panel->setObjectName(QStringLiteral("touchLayoutEditPanel"));
    panel->setFixedWidth(250);
    QVBoxLayout *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(8);

    m_controlTitle = new QLabel(tr("未选中控件"), panel);
    m_controlTitle->setObjectName(QStringLiteral("touchLayoutControlTitle"));
    panelLayout->addWidget(m_controlTitle);

    panelLayout->addWidget(new QLabel(tr("按钮文字"), panel), 0, Qt::AlignBottom);
    m_labelEdit = new QLineEdit(panel);
    panelLayout->addWidget(m_labelEdit);

    QHBoxLayout *sizeRow = new QHBoxLayout();
    m_sizeSlider = new QSlider(Qt::Horizontal, panel);
    m_sizeSlider->setRange(60, 160);
    m_sizeValue = new QLabel(panel);
    m_sizeValue->setMinimumWidth(40);
    m_sizeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sizeRow->addWidget(new QLabel(tr("大小"), panel));
    sizeRow->addWidget(m_sizeSlider, 1);
    sizeRow->addWidget(m_sizeValue);
    panelLayout->addLayout(sizeRow);

    QHBoxLayout *opacityRow = new QHBoxLayout();
    m_opacitySlider = new QSlider(Qt::Horizontal, panel);
    m_opacitySlider->setRange(30, 100);
    m_opacityValue = new QLabel(panel);
    m_opacityValue->setMinimumWidth(40);
    m_opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opacityRow->addWidget(new QLabel(tr("浓度"), panel));
    opacityRow->addWidget(m_opacitySlider, 1);
    opacityRow->addWidget(m_opacityValue);
    panelLayout->addLayout(opacityRow);

    m_defaultHint = new QLabel(panel);
    m_defaultHint->setObjectName(QStringLiteral("touchLayoutHint"));
    m_defaultHint->setWordWrap(true);
    panelLayout->addWidget(m_defaultHint);

    panelLayout->addWidget(new QLabel(tr("键值绑定（在下方键盘点选，多选即组合键）"), panel), 0, Qt::AlignBottom);
    QHBoxLayout *keyBtnRow = new QHBoxLayout();
    m_clearKeysBtn = new QPushButton(tr("清空键值"), panel);
    keyBtnRow->addWidget(m_clearKeysBtn, 1);
    panelLayout->addLayout(keyBtnRow);

    QHBoxLayout *ctrlBtnRow = new QHBoxLayout();
    m_addBtn = new QPushButton(tr("添加按钮"), panel);
    m_deleteBtn = new QPushButton(tr("删除按钮"), panel);
    ctrlBtnRow->addWidget(m_addBtn, 1);
    ctrlBtnRow->addWidget(m_deleteBtn, 1);
    panelLayout->addLayout(ctrlBtnRow);

    panelLayout->addStretch();
    body->addWidget(panel, 0);
    bodyLayout->addLayout(body, 1);

    // ── 可视化电脑键盘：点键选中高亮，多选即组合键 ──
    m_keyKeyboard = new TouchKeyKeyboard(bodyWidget);
    bodyLayout->addWidget(m_keyKeyboard, 0);

    scroll->setWidget(bodyWidget);
    // 滚动区视口别自绘底色，否则卡片圆角内会露出一块异色矩形
    scroll->viewport()->setAutoFillBackground(false);
    cardLayout->addWidget(scroll, 1);

    QHBoxLayout *bottomRow = new QHBoxLayout();
    m_resetBtn = new QPushButton(tr("恢复默认布局"), card);
    m_saveBtn = new QPushButton(tr("保存"), card);
    m_saveBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    QPushButton *cancelBtn = new QPushButton(tr("取消"), card);
    cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    bottomRow->addWidget(m_resetBtn);
    bottomRow->addStretch();
    bottomRow->addWidget(m_saveBtn);
    bottomRow->addWidget(cancelBtn);
    cardLayout->addLayout(bottomRow);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(card, 0, 0, Qt::AlignCenter);

    // ── 画布 ↔ 属性面板 ──
    connect(m_canvas, &TouchLayoutCanvas::selectionChanged,
            this, &TouchLayoutEditorDialog::onSelectionChanged);
    connect(m_canvas, &TouchLayoutCanvas::controlMoved, this,
            [this](int index, qreal x, qreal y) {
                if (index >= 0 && index < m_layout.size()) {
                    m_layout[index].x = x;
                    m_layout[index].y = y;
                }
            });

    // ── 属性面板改动实时写回布局 ──
    connect(m_labelEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_updatingPanel || m_selected < 0 || m_selected >= m_layout.size())
            return;
        if (!m_layout[m_selected].joystick) {
            m_layout[m_selected].label = text;
            updateCanvas();
        }
    });
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sizeValue->setText(tr("%1%").arg(value));
        if (m_updatingPanel || m_selected < 0 || m_selected >= m_layout.size())
            return;
        m_layout[m_selected].size = value;
        updateCanvas();
    });
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityValue->setText(tr("%1%").arg(value));
        if (m_updatingPanel || m_selected < 0 || m_selected >= m_layout.size())
            return;
        m_layout[m_selected].opacity = value;
        updateCanvas();
    });
    connect(m_keyKeyboard, &TouchKeyKeyboard::selectionChanged,
            this, &TouchLayoutEditorDialog::onKeyboardSelectionChanged);
    connect(m_clearKeysBtn, &QPushButton::clicked,
            this, &TouchLayoutEditorDialog::onClearKeys);
    connect(m_addBtn, &QPushButton::clicked,
            this, &TouchLayoutEditorDialog::onAddButton);
    connect(m_deleteBtn, &QPushButton::clicked,
            this, &TouchLayoutEditorDialog::onDeleteButton);
    connect(m_resetBtn, &QPushButton::clicked,
            this, &TouchLayoutEditorDialog::onResetLayout);
    connect(m_saveBtn, &QPushButton::clicked,
            this, &TouchLayoutEditorDialog::onSave);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_canvas->setLayout(m_layout);
    refreshPropertyPanel();
    initStyle();
}

void TouchLayoutEditorDialog::onSelectionChanged(int index)
{
    m_selected = index;
    refreshPropertyPanel();
}

void TouchLayoutEditorDialog::onAddButton()
{
    TouchControl control;
    control.id = nextCustomControlId(m_layout);
    control.label = tr("按钮%1").arg(control.id.mid(7).toInt());
    control.action = QStringLiteral("custom");
    control.x = 0.5;
    control.y = 0.45;
    control.size = 100;
    control.opacity = 85;
    m_layout.append(control);

    m_selected = m_layout.size() - 1;
    updateCanvas();
    m_canvas->setSelectedIndex(m_selected);
    refreshPropertyPanel();
}

void TouchLayoutEditorDialog::onDeleteButton()
{
    if (m_selected < 0 || m_selected >= m_layout.size())
        return;
    if (m_layout[m_selected].joystick)
        return; // 摇杆是移动核心，不允许删
    m_layout.remove(m_selected);
    m_selected = -1;
    updateCanvas();
    m_canvas->setSelectedIndex(-1);
    refreshPropertyPanel();
}

void TouchLayoutEditorDialog::onResetLayout()
{
    m_layout = defaultTouchLayout();
    m_selected = -1;
    m_canvas->setMirror(handedness() == Handedness::Left);
    updateCanvas();
    m_canvas->setSelectedIndex(-1);
    refreshPropertyPanel();
}

void TouchLayoutEditorDialog::onSave()
{
    setTouchLayout(m_layout);
    accept();
}

void TouchLayoutEditorDialog::onKeyboardSelectionChanged(const QStringList &keys)
{
    if (m_updatingPanel || m_selected < 0 || m_selected >= m_layout.size())
        return;
    TouchControl &control = m_layout[m_selected];
    if (control.joystick)
        return;
    control.comboKeys = keys;
    // 只刷新提示文字和清空按钮，键盘本身就是来源，不回写键盘
    m_updatingPanel = true;
    refreshPropertyPanel();
    m_updatingPanel = false;
}

void TouchLayoutEditorDialog::onClearKeys()
{
    if (m_selected < 0 || m_selected >= m_layout.size())
        return;
    m_layout[m_selected].comboKeys.clear();
    refreshPropertyPanel();
    updateCanvas();
}

void TouchLayoutEditorDialog::refreshKeyboardSelection()
{
    const bool valid = m_selected >= 0 && m_selected < m_layout.size();
    const TouchControl control = valid ? m_layout.at(m_selected) : TouchControl();
    const bool keysEditable = valid && !control.joystick;
    m_keyKeyboard->setKeysEnabled(keysEditable);
    m_keyKeyboard->setSelectedKeys(keysEditable ? control.comboKeys : QStringList());
}

void TouchLayoutEditorDialog::refreshPropertyPanel()
{
    m_updatingPanel = true;

    const bool valid = m_selected >= 0 && m_selected < m_layout.size();
    const TouchControl control = valid ? m_layout.at(m_selected) : TouchControl();

    if (!valid) {
        m_controlTitle->setText(tr("未选中控件"));
        m_defaultHint->setText(tr("在左侧点一下按钮选中它；拖动可以调整位置。"));
    } else if (control.joystick) {
        m_controlTitle->setText(tr("移动摇杆"));
        m_defaultHint->setText(tr("摇杆负责移动走位，位置可以拖动调整，不支持改键值。"));
    } else {
        const QString name = control.label.isEmpty()
            ? defaultActionName(control.action) : control.label;
        m_controlTitle->setText(name.isEmpty() ? tr("自定义按钮") : name);

        if (!control.comboKeys.isEmpty()) {
            QStringList display;
            for (const QString &key : control.comboKeys)
                display << touchKeyDisplayName(key);
            m_defaultHint->setText(tr("按下时发送：%1").arg(display.join(QStringLiteral(" + "))));
        } else if (control.action == QLatin1String("custom")) {
            m_defaultHint->setText(tr("还没绑定键值，按下不会发送按键；在下方键盘点选。"));
        } else {
            m_defaultHint->setText(tr("默认键值：%1（%2）")
                .arg(defaultActionName(control.action),
                     defaultActionKeys(control.action)));
        }
    }

    m_labelEdit->setEnabled(valid && !control.joystick);
    m_labelEdit->setText(valid && !control.joystick ? control.label : QString());
    m_labelEdit->setPlaceholderText(valid && !control.joystick
        ? tr("按钮上显示的文字") : QString());
    m_sizeSlider->setEnabled(valid);
    m_opacitySlider->setEnabled(valid);
    if (valid) {
        m_sizeSlider->setValue(control.size);
        m_opacitySlider->setValue(control.opacity);
    }
    m_sizeValue->setText(tr("%1%").arg(m_sizeSlider->value()));
    m_opacityValue->setText(tr("%1%").arg(m_opacitySlider->value()));

    m_clearKeysBtn->setEnabled(valid && !control.joystick && !control.comboKeys.isEmpty());
    m_deleteBtn->setEnabled(valid && !control.joystick);
    refreshKeyboardSelection();

    m_updatingPanel = false;
}

void TouchLayoutEditorDialog::updateCanvas()
{
    m_canvas->setLayout(m_layout);
}

void TouchLayoutEditorDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";
    const QString hintColor  = isLight ? "#777777" : "#9a9aa2";
    const QString fieldBg    = isLight ? "#f5f5f5" : "#3b3b40";
    const QString fieldBorder = isLight ? "#d4d4d4" : "#55555a";
    const QString btnText    = isLight ? "#333333" : "#e8e8e8";

    const QString style = QString(
        "QWidget#touchLayoutEditorCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 16px;"
        "}"
        "QLabel#appDialogTitleLabel {"
        "    color: %3;"
        "    background-color: transparent;"
        "    font-size: 15px;"
        "    font-weight: bold;"
        "}"
        "QScrollArea#touchLayoutEditorScroll {"
        "    background-color: transparent;"
        "    border: none;"
        "}"
        "QWidget#touchLayoutEditorBody {"
        "    background-color: transparent;"
        "}"
        "QWidget#touchLayoutEditPanel {"
        "    background-color: transparent;"
        "}"
        "QWidget#touchLayoutEditPanel QLabel {"
        "    color: %4;"
        "    background-color: transparent;"
        "    font-size: 12px;"
        "}"
        "QLabel#touchLayoutControlTitle {"
        "    color: %3;"
        "    font-size: 14px;"
        "    font-weight: bold;"
        "}"
        "QLabel#touchLayoutHint {"
        "    color: %5;"
        "    font-size: 11px;"
        "}"
        "QWidget#touchLayoutEditPanel QLineEdit {"
        "    background-color: %6;"
        "    border: 1px solid %7;"
        "    border-radius: 8px;"
        "    color: %4;"
        "    padding: 4px 8px;"
        "    font-size: 12px;"
        "}"
        "QWidget#touchLayoutEditPanel QPushButton {"
        "    background-color: %6;"
        "    border: 1px solid %7;"
        "    border-radius: 10px;"
        "    color: %8;"
        "    padding: 6px 10px;"
        "    font-size: 12px;"
        "}"
        "QWidget#touchLayoutEditPanel QPushButton:hover {"
        "    background-color: rgba(0,0,0,28);"
        "}"
        "QPushButton#appDialogBtn {"
        "    background-color: %6;"
        "    border: 1px solid %7;"
        "    border-radius: 12px;"
        "    color: %8;"
        "    padding: 7px 22px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtn:hover {"
        "    background-color: rgba(0,0,0,28);"
        "}"
        "QPushButton#appDialogBtnPrimary {"
        "    background-color: %9;"
        "    border: 1px solid %9;"
        "    border-radius: 12px;"
        "    color: #ffffff;"
        "    padding: 7px 26px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtnPrimary:hover {"
        "    background-color: %10;"
        "    border-color: %10;"
        "}"
    ).arg(cardBg, cardBorder, titleColor, textColor, hintColor,
          fieldBg, fieldBorder, btnText, themeColor, themeHover);

    setStyleSheet(style);
}
