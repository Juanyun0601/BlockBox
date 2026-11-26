/**
 * @file   ProjectionBlockEditorDialog.cpp
 * @brief  投影方块编辑器对话框实现（3D 立体视图）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "ProjectionBlockEditorDialog.h"

#include "VoxelViewWidget.h"
#include "AppMessageBox.h"
#include "AppInputDialog.h"
#include "ProjectionFillShapeDialog.h"
#include <QFileDialog>
#include "utils/Schematic/ProjectionEditorSettings.h"
#include "utils/Schematic/InstanceBlockNames.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/ProjectionFillShape.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <QCloseEvent>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <climits>
#include <functional>

namespace {

QString friendlyBlockName(const QString &full)
{
    QString n = full;
    const int bracket = n.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        n = n.left(bracket);
    const int slash = n.indexOf(QLatin1Char(':'));
    if (slash >= 0)
        n = n.mid(slash + 1);
    return n.isEmpty() ? full : n;
}

QString lookupChineseName(const QHash<QString, QString> &cnMap, const QString &fullId)
{
    return InstanceBlockNames::lookup(cnMap, fullId);
}

/** 与投影编辑页材料清单一致：优先「中文名 (英文ID)」，无中文时仅英文 ID */
QString materialStyleLabel(const QHash<QString, QString> &cnMap, const QString &blockId)
{
    const QString baseId = InstanceBlockNames::baseBlockId(blockId);
    const QString cn = InstanceBlockNames::lookup(cnMap, blockId);
    if (cn.isEmpty())
        return friendlyBlockName(blockId);
    return QStringLiteral("%1 (%2)").arg(cn, friendlyBlockName(baseId));
}

QString friendlyBlockStateLabel(const SchematicBlockState &state,
                                const QHash<QString, QString> &cnMap)
{
    // 列表主文案与材料清单一致；方块属性放 tooltip，避免行文字过长
    return materialStyleLabel(cnMap, state.name);
}

QString blockStateTooltip(const SchematicBlockState &state)
{
    return state.toString();
}

/** 「当前方块」下拉框的内置预设（带完整状态，放置后即可按真实形状渲染） */
const char *const kCurrentBlockPresets[] = {
    "minecraft:air",
    "minecraft:stone",
    "minecraft:cobblestone",
    "minecraft:oak_planks",
    "minecraft:glass",
    "minecraft:oak_log[axis=y]",
    "minecraft:oak_stairs[facing=north,half=bottom,shape=straight]",
    "minecraft:oak_slab[type=bottom]",
    "minecraft:oak_trapdoor[facing=north,half=bottom,open=false]",
    "minecraft:iron_trapdoor[facing=north,half=bottom,open=false]",
};

} // namespace

bool ProjectionBlockEditorDialog::editProjection(QWidget *parent, const QString &filePath,
                                                  const QString &instancePath)
{
    ProjectionBlockEditorDialog dlg(parent);
    // 先加载投影文档，再挂材质/形状：避免 setTextureSource 在无文档时白建一次几何，
    // 也让 applyEditorSettings 能看到已就绪的加载器，少走一轮重复 load。
    if (!dlg.loadProjection(filePath, instancePath))
        return false;
    dlg.exec();
    return dlg.wasSaved(); // 有保存动作即视为已修改，供调用方刷新列表
}

ProjectionBlockEditorDialog::ProjectionBlockEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ProjectionBlockEditorDialog"));
    setWindowTitle(tr("投影方块编辑器"));
    setMinimumSize(980, 640);
    resize(1220, 830);
    initUI();
}

ProjectionBlockEditorDialog::~ProjectionBlockEditorDialog()
{
}

namespace {

/** 手绘矢量小图标：不依赖任何图片资源，颜色跟随主题/选中状态 */
enum class Glyph
{
    Cube,
    Place,
    Break,
    Fill,
    Select,
    Edit,
    Undo,
    Redo,
    Focus,
    Camera,
    TriLeft,
    TriRight
};

QIcon makeGlyphIcon(Glyph glyph, const QColor &color, int size = 18)
{
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.5);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.scale(size / 18.0, size / 18.0);

    QColor soft = color;
    soft.setAlpha(70);
    QColor mid = color;
    mid.setAlpha(130);

    auto fillPoly = [&](const QPolygonF &poly, const QColor &c) {
        p.setBrush(c);
        p.drawPolygon(poly);
        p.setBrush(Qt::NoBrush);
    };

    switch (glyph)
    {
    case Glyph::Cube:
    {
        const QPolygonF top{QPointF(9, 2), QPointF(16, 6), QPointF(9, 10), QPointF(2, 6)};
        const QPolygonF left{QPointF(2, 6), QPointF(9, 10), QPointF(9, 16.5), QPointF(2, 12.5)};
        const QPolygonF right{QPointF(16, 6), QPointF(9, 10), QPointF(9, 16.5), QPointF(16, 12.5)};
        fillPoly(top, mid);
        fillPoly(left, soft);
        fillPoly(right, soft);
        p.drawPolygon(top);
        p.drawPolygon(left);
        p.drawPolygon(right);
        break;
    }
    case Glyph::Place:
    {
        p.setPen(QPen(color, 1.9));
        p.drawLine(QPointF(9, 3.2), QPointF(9, 14.8));
        p.drawLine(QPointF(3.2, 9), QPointF(14.8, 9));
        break;
    }
    case Glyph::Break:
    {
        p.setPen(QPen(color, 1.9));
        p.drawLine(QPointF(3.6, 9), QPointF(14.4, 9));
        break;
    }
    case Glyph::Fill:
    {
        p.drawPolygon(QPolygonF{QPointF(4.5, 6.5), QPointF(13.5, 6.5),
                                QPointF(12, 15.5), QPointF(6, 15.5)});
        p.drawArc(QRectF(4.5, 3.8, 9, 5.4), 0, 180 * 16);
        p.setPen(QPen(color, 1.2));
        p.drawLine(QPointF(3.6, 9.6), QPointF(14.4, 9.6));
        break;
    }
    case Glyph::Select:
    {
        QPen dash(color, 1.4);
        dash.setStyle(Qt::DashLine);
        p.setPen(dash);
        p.drawRect(QRectF(3, 3, 12, 12));
        p.setPen(pen);
        p.setBrush(color);
        for (const QPointF &c : { QPointF(3, 3), QPointF(15, 3), QPointF(3, 15), QPointF(15, 15) })
            p.drawRect(QRectF(c.x() - 1.1, c.y() - 1.1, 2.2, 2.2));
        p.setBrush(Qt::NoBrush);
        break;
    }
    case Glyph::Edit:
    {
        // 铅笔：斜向笔身 + 笔尖，一眼读作「编辑」
        const QPolygonF body{ QPointF(11.2, 2.6), QPointF(15.4, 6.8),
                              QPointF(6.6, 15.6), QPointF(2.4, 11.4) };
        p.drawPolygon(body);
        p.setPen(QPen(color, 1.1));
        p.drawLine(QPointF(4.6, 9.2), QPointF(8.8, 13.4));
        p.setPen(pen);
        const QPolygonF tip{ QPointF(6.6, 15.6), QPointF(2.4, 11.4), QPointF(1.9, 16.1) };
        fillPoly(tip, mid);
        p.drawPolygon(tip);
        break;
    }
    case Glyph::Undo:
    {
        p.drawArc(QRectF(4, 5.5, 11, 9), 20 * 16, 160 * 16);
        p.drawLine(QPointF(4.2, 5.2), QPointF(4.2, 10.4));
        p.drawLine(QPointF(4.2, 5.6), QPointF(9.2, 5.6));
        break;
    }
    case Glyph::Redo:
    {
        p.drawArc(QRectF(3, 5.5, 11, 9), 0 * 16, 160 * 16);
        p.drawLine(QPointF(13.8, 5.2), QPointF(13.8, 10.4));
        p.drawLine(QPointF(13.8, 5.6), QPointF(8.8, 5.6));
        break;
    }
    case Glyph::Focus:
    {
        p.drawEllipse(QPointF(9, 9), 3.6, 3.6);
        p.drawLine(QPointF(9, 1.6), QPointF(9, 4.2));
        p.drawLine(QPointF(9, 13.8), QPointF(9, 16.4));
        p.drawLine(QPointF(1.6, 9), QPointF(4.2, 9));
        p.drawLine(QPointF(13.8, 9), QPointF(16.4, 9));
        break;
    }
    case Glyph::Camera:
    {
        p.drawRoundedRect(QRectF(2.2, 5.6, 13.6, 9.2), 2.2, 2.2);
        p.drawEllipse(QPointF(9, 10.2), 2.7, 2.7);
        p.drawPolyline(QPolygonF{QPointF(6.2, 5.6), QPointF(7.2, 3.6),
                                  QPointF(10.8, 3.6), QPointF(11.8, 5.6)});
        break;
    }
    case Glyph::TriLeft:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(QPolygonF{QPointF(11.4, 4.4), QPointF(11.4, 13.6), QPointF(6.2, 9)});
        p.setBrush(Qt::NoBrush);
        break;
    case Glyph::TriRight:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(QPolygonF{QPointF(6.6, 4.4), QPointF(6.6, 13.6), QPointF(11.8, 9)});
        p.setBrush(Qt::NoBrush);
        break;
    }

    p.end();
    return QIcon(pm);
}

/** 无材质时的方块色块图标：带高光边的立体小方块，保证调色板始终有视觉锚点 */
QIcon makeSwatchIcon(const QColor &color, int size = 20)
{
    // 画法统一放在 InstanceTextureLoader 里，结构详情弹窗的战利品图标复用同一套
    return InstanceTextureLoader::swatchIcon(color, size, 2.0);
}

/** 读取主题令牌色（例如 "TEXT_SECONDARY"），解析失败时返回 fallback */
QColor tokenColor(const char *token, const QColor &fallback)
{
    const QString raw = StyleKit::resolve(QStringLiteral("@%1@").arg(QLatin1String(token))).trimmed();
    const QColor color(raw);
    return color.isValid() ? color : fallback;
}

} // namespace

void ProjectionBlockEditorDialog::initUI()
{
    setMinimumSize(1120, 720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(10);

    // ============================================================ 顶部卡片
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("projToolbarCard"));
    auto *headerLay = new QVBoxLayout(header);
    headerLay->setContentsMargins(14, 12, 14, 12);
    headerLay->setSpacing(10);

    // ---- 标题行：图标 + 标题 + 文件名 + 统计徽章 ----
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    {
        auto *logo = new QLabel(header);
        logo->setPixmap(makeGlyphIcon(Glyph::Cube, QColor(96, 214, 150), 22).pixmap(22, 22));
        logo->setFixedSize(22, 22);
        titleRow->addWidget(logo, 0, Qt::AlignVCenter);

        auto *title = new QLabel(tr("投影方块编辑器"), header);
        title->setObjectName(QStringLiteral("projTitle"));
        titleRow->addWidget(title, 0, Qt::AlignVCenter);

        m_fileLabel = new QLabel(header);
        m_fileLabel->setObjectName(QStringLiteral("projSubtitle"));
        titleRow->addWidget(m_fileLabel, 0, Qt::AlignVCenter);

        titleRow->addStretch(1);

        m_chipSize = new QLabel(tr("尺寸 —"), header);
        m_chipFaces = new QLabel(tr("可见面 —"), header);
        m_chipPerf = new QLabel(tr("重建 —"), header);
        const QList<QLabel *> chips{ m_chipSize, m_chipFaces, m_chipPerf };
        for (QLabel *chip : chips)
        {
            chip->setObjectName(QStringLiteral("projChip"));
            titleRow->addWidget(chip, 0, Qt::AlignVCenter);
        }
    }
    headerLay->addLayout(titleRow);

    // ---- 工具行：工具切换 + 撤销重做 + 显示开关 ----
    auto *toolRow = new QHBoxLayout();
    toolRow->setSpacing(6);
    {
        // 隐藏的工具下拉：沿用原有「工具枚举 -> 视图模式」的数据通道
        m_toolCombo = new QComboBox(this);
        m_toolCombo->setObjectName(QStringLiteral("projToolCombo"));
        m_toolCombo->addItem(tr("选取"), int(ToolSelect));
        m_toolCombo->addItem(tr("编辑"), int(ToolEdit));
        m_toolCombo->setVisible(false);

        // 顶部只有两个模式：选取（左键选中）与编辑（左键破坏 / 右键放置）
        struct ToolSpec { Glyph glyph; QString text; QString tip; };
        const QVector<ToolSpec> tools{
            { Glyph::Select, tr("选取"), tr("左键单击选中方块；范围可切换为「同类型全部」或「框选（两点）」整块多选；右键仍为放置（快捷键 1）") },
            { Glyph::Edit,   tr("编辑"), tr("左键破坏方块、右键放置当前方块，拖动鼠标可像《我的世界》一样转动视角（快捷键 2）") },
        };
        auto *toolGroup = new QButtonGroup(this);
        toolGroup->setExclusive(true);
        m_toolButtons.clear();
        for (int i = 0; i < tools.size(); ++i)
        {
            auto *btn = new QPushButton(tools.at(i).text, header);
            btn->setObjectName(QStringLiteral("projToolSegBtn"));
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setToolTip(tools.at(i).tip);
            btn->setIconSize(QSize(16, 16));
            btn->setProperty("glyphIndex", int(tools.at(i).glyph));
            btn->setProperty("toolValue", i);
            toolGroup->addButton(btn, i);
            m_toolButtons.append(btn);
            toolRow->addWidget(btn);
            connect(btn, &QPushButton::clicked, this, [this, i]() {
                if (m_toolCombo)
                    m_toolCombo->setCurrentIndex(i);
            });
        }
        if (!m_toolButtons.isEmpty())
            m_toolButtons.first()->setChecked(true);

        auto addSeparator = [&]() {
            auto *line = new QFrame(header);
            line->setObjectName(QStringLiteral("projVLine"));
            line->setFrameShape(QFrame::VLine);
            line->setFixedWidth(1);
            toolRow->addSpacing(4);
            toolRow->addWidget(line);
            toolRow->addSpacing(4);
        };

        addSeparator();

        m_undoBtn = new QPushButton(tr("撤销"), header);
        m_undoBtn->setObjectName(QStringLiteral("projToolBtn"));
        m_undoBtn->setIconSize(QSize(15, 15));
        m_undoBtn->setToolTip(tr("撤销上一步（Ctrl+Z）"));
        m_undoBtn->setCursor(Qt::PointingHandCursor);
        toolRow->addWidget(m_undoBtn);

        m_redoBtn = new QPushButton(tr("重做"), header);
        m_redoBtn->setObjectName(QStringLiteral("projToolBtn"));
        m_redoBtn->setIconSize(QSize(15, 15));
        m_redoBtn->setToolTip(tr("重做（Ctrl+Y）"));
        m_redoBtn->setCursor(Qt::PointingHandCursor);
        toolRow->addWidget(m_redoBtn);

        toolRow->addStretch(1);

        m_placeAdjacentToggle = new QToolButton(header);
        m_placeAdjacentToggle->setObjectName(QStringLiteral("projToggleBtn"));
        m_placeAdjacentToggle->setText(tr("贴面放置"));
        m_placeAdjacentToggle->setCheckable(true);
        m_placeAdjacentToggle->setChecked(true);
        m_placeAdjacentToggle->setCursor(Qt::PointingHandCursor);
        m_placeAdjacentToggle->setToolTip(
            tr("开启后，右键会把方块放到所点方块的相邻面上；关闭则直接覆盖所点方块"));
        toolRow->addWidget(m_placeAdjacentToggle);

        m_airBtn = new QToolButton(header);
        m_airBtn->setObjectName(QStringLiteral("projToggleBtn"));
        m_airBtn->setText(tr("显示空气"));
        m_airBtn->setCheckable(true);
        m_airBtn->setCursor(Qt::PointingHandCursor);
        m_airBtn->setToolTip(tr("显示投影范围内的空气方块线框"));
        toolRow->addWidget(m_airBtn);
    }
    headerLay->addLayout(toolRow);
    root->addWidget(header);

    // ============================================================ 主体：视图 + 侧栏
    auto *mainRow = new QHBoxLayout();
    mainRow->setSpacing(10);

    auto *viewCard = new QWidget(this);
    viewCard->setObjectName(QStringLiteral("projViewCard"));
    auto *viewLay = new QVBoxLayout(viewCard);
    viewLay->setContentsMargins(8, 8, 8, 8);
    viewLay->setSpacing(6);

    m_view = new VoxelViewWidget(viewCard);
    m_view->setObjectName(QStringLiteral("projVoxelView"));
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_view->setFocusPolicy(Qt::StrongFocus);
    m_view->setMinimumSize(420, 300);
    viewLay->addWidget(m_view, 1);

    // ---- 图层条 ----
    auto *layerBar = new QHBoxLayout();
    layerBar->setContentsMargins(2, 0, 2, 0);
    layerBar->setSpacing(8);
    {
        auto *layerTitle = new QLabel(tr("图层"), viewCard);
        layerTitle->setObjectName(QStringLiteral("projLayerLabel"));
        layerBar->addWidget(layerTitle);

        m_layerPrevBtn = new QPushButton(QString(), viewCard);
        m_layerPrevBtn->setObjectName(QStringLiteral("projStepBtn"));
        m_layerPrevBtn->setFixedSize(24, 24);
        m_layerPrevBtn->setIconSize(QSize(12, 12));
        m_layerPrevBtn->setCursor(Qt::PointingHandCursor);
        m_layerPrevBtn->setToolTip(tr("降低一层（高亮该层）"));
        m_layerPrevBtn->setAccessibleName(tr("降低一层"));
        layerBar->addWidget(m_layerPrevBtn);

        m_layerSlider = new QSlider(Qt::Horizontal, viewCard);
        m_layerSlider->setObjectName(QStringLiteral("projLayerSlider"));
        m_layerSlider->setRange(0, 1);
        m_layerSlider->setValue(0);
        m_layerSlider->setMinimumWidth(160);
        layerBar->addWidget(m_layerSlider, 1);

        m_layerNextBtn = new QPushButton(QString(), viewCard);
        m_layerNextBtn->setObjectName(QStringLiteral("projStepBtn"));
        m_layerNextBtn->setFixedSize(24, 24);
        m_layerNextBtn->setIconSize(QSize(12, 12));
        m_layerNextBtn->setCursor(Qt::PointingHandCursor);
        m_layerNextBtn->setToolTip(tr("升高一层（高亮该层）"));
        m_layerNextBtn->setAccessibleName(tr("升高一层"));
        layerBar->addWidget(m_layerNextBtn);

        m_layerLabel = new QLabel(tr("全部"), viewCard);
        m_layerLabel->setObjectName(QStringLiteral("projLayerLabel"));
        m_layerLabel->setMinimumWidth(76);
        m_layerLabel->setAlignment(Qt::AlignCenter);
        layerBar->addWidget(m_layerLabel);

        m_belowToggle = new QToolButton(viewCard);
        m_belowToggle->setObjectName(QStringLiteral("projToggleBtn"));
        m_belowToggle->setText(tr("含下方"));
        m_belowToggle->setCheckable(true);
        m_belowToggle->setCursor(Qt::PointingHandCursor);
        m_belowToggle->setToolTip(tr("配合图层滑块：显示该层及以下所有层"));
        layerBar->addWidget(m_belowToggle);
    }
    viewLay->addLayout(layerBar);
    mainRow->addWidget(viewCard, 1);

    // ============================================================ 侧栏
    auto *side = new QWidget(this);
    side->setObjectName(QStringLiteral("projSideCard"));
    side->setFixedWidth(322);
    auto *sideLay = new QVBoxLayout(side);
    sideLay->setContentsMargins(12, 12, 12, 12);
    sideLay->setSpacing(7);

    auto sectionTitle = [&](const QString &text) {
        auto *label = new QLabel(text, side);
        label->setObjectName(QStringLiteral("projSectionTitle"));
        return label;
    };

    // ---- 当前方块 ----
    sideLay->addWidget(sectionTitle(tr("当前方块")));
    {
        auto *curRow = new QHBoxLayout();
        curRow->setSpacing(6);
        m_currentBlockCombo = new QComboBox(side);
        m_currentBlockCombo->setObjectName(QStringLiteral("projBlockCombo"));
        m_currentBlockCombo->setEditable(true);
        m_currentBlockCombo->setInsertPolicy(QComboBox::NoInsert);
        m_currentBlockCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_currentBlockCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_currentBlockCombo->setMinimumContentsLength(8);
        m_currentBlockCombo->lineEdit()->setPlaceholderText(tr("方块 ID 或中文名"));
        curRow->addWidget(m_currentBlockCombo, 1);

        m_pickBtn = new QPushButton(tr("拾取"), side);
        m_pickBtn->setObjectName(QStringLiteral("projToolBtn"));
        m_pickBtn->setCursor(Qt::PointingHandCursor);
        m_pickBtn->setToolTip(tr("把视图里悬停的方块设为当前方块"));
        curRow->addWidget(m_pickBtn);
        sideLay->addLayout(curRow);
    }

    // ---- 选取 ----
    {
        auto *selRow = new QHBoxLayout();
        selRow->setSpacing(6);
        auto *selTitle = new QLabel(tr("选取"), side);
        selTitle->setObjectName(QStringLiteral("projLayerLabel"));
        selRow->addWidget(selTitle);

        m_selectScopeCombo = new QComboBox(side);
        m_selectScopeCombo->setObjectName(QStringLiteral("projBlockCombo"));
        m_selectScopeCombo->addItem(tr("单方块"), 0);
        m_selectScopeCombo->addItem(tr("同类型全部"), 1);
        m_selectScopeCombo->addItem(tr("框选（两点）"), 2);
        m_selectScopeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_selectScopeCombo->setToolTip(tr("单方块：只选点到的那一个\n"
                                          "同类型全部：整份投影里同种方块一起选\n"
                                          "框选（两点）：左键点两个对角方块，框住的那一整块一起选"));
        selRow->addWidget(m_selectScopeCombo, 1);

        m_clearSelBtn = new QPushButton(tr("取消"), side);
        m_clearSelBtn->setObjectName(QStringLiteral("projToolBtn"));
        m_clearSelBtn->setCursor(Qt::PointingHandCursor);
        m_clearSelBtn->setToolTip(tr("清空当前选择"));
        m_clearSelBtn->setEnabled(false);
        selRow->addWidget(m_clearSelBtn);
        sideLay->addLayout(selRow);

        m_selectLabel = new QLabel(side);
        m_selectLabel->setObjectName(QStringLiteral("projMutedLabel"));
        m_selectLabel->setWordWrap(true);
        sideLay->addWidget(m_selectLabel);
    }

    // ---- 视图 ----
    sideLay->addWidget(sectionTitle(tr("视图")));
    {
        auto *presetGrid = new QGridLayout();
        presetGrid->setSpacing(5);
        struct PresetSpec { const char *text; VoxelViewWidget::ViewPreset preset; const char *tip; };
        const PresetSpec presets[] = {
            { QT_TR_NOOP("等轴"), VoxelViewWidget::ViewPreset::Iso,    QT_TR_NOOP("等轴视角（默认）") },
            { QT_TR_NOOP("前"),   VoxelViewWidget::ViewPreset::Front,  QT_TR_NOOP("正视图") },
            { QT_TR_NOOP("后"),   VoxelViewWidget::ViewPreset::Back,   QT_TR_NOOP("后视图") },
            { QT_TR_NOOP("左"),   VoxelViewWidget::ViewPreset::Left,   QT_TR_NOOP("左视图") },
            { QT_TR_NOOP("右"),   VoxelViewWidget::ViewPreset::Right,  QT_TR_NOOP("右视图") },
            { QT_TR_NOOP("顶"),   VoxelViewWidget::ViewPreset::Top,    QT_TR_NOOP("俯视图") },
            { QT_TR_NOOP("底"),   VoxelViewWidget::ViewPreset::Bottom, QT_TR_NOOP("仰视图") },
        };
        m_presetButtons.clear();
        int index = 0;
        for (const PresetSpec &spec : presets)
        {
            auto *btn = new QPushButton(tr(spec.text), side);
            btn->setObjectName(QStringLiteral("projMiniBtn"));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setMinimumWidth(0);
            btn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            btn->setToolTip(tr(spec.tip));
            const int presetValue = int(spec.preset);
            btn->setProperty("presetValue", presetValue);
            connect(btn, &QPushButton::clicked, this, [this, presetValue]() {
                onViewPreset(presetValue);
            });
            m_presetButtons.append(btn);
            presetGrid->addWidget(btn, index / 3, index % 3);
            ++index;
        }
        sideLay->addLayout(presetGrid);

        auto *viewRow = new QHBoxLayout();
        viewRow->setSpacing(5);
        m_focusBtn = new QPushButton(tr("聚焦"), side);
        m_focusBtn->setObjectName(QStringLiteral("projMiniBtn"));
        m_focusBtn->setIconSize(QSize(14, 14));
        m_focusBtn->setCursor(Qt::PointingHandCursor);
        m_focusBtn->setToolTip(tr("缩放到当前视角可见的方块（有选中时聚焦选中，快捷键 F）"));
        viewRow->addWidget(m_focusBtn, 1);

        m_shotBtn = new QPushButton(tr("截图"), side);
        m_shotBtn->setObjectName(QStringLiteral("projMiniBtn"));
        m_shotBtn->setIconSize(QSize(14, 14));
        m_shotBtn->setCursor(Qt::PointingHandCursor);
        m_shotBtn->setToolTip(tr("把当前视图导出为 PNG 图片"));
        viewRow->addWidget(m_shotBtn, 1);
        sideLay->addLayout(viewRow);
    }

    // ---- 调色板 ----
    {
        auto *paletteHead = new QHBoxLayout();
        paletteHead->setSpacing(6);
        paletteHead->addWidget(sectionTitle(tr("方块调色板")));
        m_paletteCountLabel = new QLabel(side);
        m_paletteCountLabel->setObjectName(QStringLiteral("projMutedLabel"));
        paletteHead->addWidget(m_paletteCountLabel, 0, Qt::AlignRight);
        sideLay->addLayout(paletteHead);
    }
    m_paletteFilter = new QLineEdit(side);
    m_paletteFilter->setObjectName(QStringLiteral("projSearchEdit"));
    m_paletteFilter->setPlaceholderText(tr("搜索方块名或 ID…"));
    m_paletteFilter->setClearButtonEnabled(true);
    sideLay->addWidget(m_paletteFilter);

    m_paletteList = new QListWidget(side);
    m_paletteList->setObjectName(QStringLiteral("projPaletteList"));
    m_paletteList->setIconSize(QSize(20, 20));
    m_paletteList->setUniformItemSizes(true);
    m_paletteList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_paletteList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_paletteList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_paletteList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_paletteList->setTextElideMode(Qt::ElideRight);
    m_paletteList->setWordWrap(false);
    m_paletteList->setMinimumWidth(0);
    m_paletteList->setMinimumHeight(170);
    m_paletteList->setToolTip(tr("双击某个方块即可设为当前方块"));
    sideLay->addWidget(m_paletteList, 1);

    // ---- 批量操作 ----
    {
        auto *batchGrid = new QGridLayout();
        batchGrid->setSpacing(5);
        auto *replaceBtn = new QPushButton(tr("替换方块"), side);
        auto *fillAllBtn = new QPushButton(tr("全部填充"), side);
        auto *fillShapeBtn = new QPushButton(tr("填充方块"), side);
        const QList<QPushButton *> batchButtons{ replaceBtn, fillAllBtn, fillShapeBtn };
        for (QPushButton *btn : batchButtons)
        {
            btn->setObjectName(QStringLiteral("projToolBtn"));
            btn->setCursor(Qt::PointingHandCursor);
        }
        replaceBtn->setToolTip(tr("有选中方块时：一键把选中的方块都换成「当前方块」\n"
                                  "没有选中时：按方块名把整份投影里的某种方块换成另一种"));
        fillAllBtn->setToolTip(tr("用当前方块填满整个投影范围"));
        fillShapeBtn->setToolTip(tr("选一个形状（实心 / 空心 / 框架 / 倾斜地板 / 方形 / 三角形 / 圆形 / 圆柱），\n"
                                    "再在视图里点两个对角方块即可生成；也可以按坐标精确填充"));
        batchGrid->addWidget(replaceBtn, 0, 0);
        batchGrid->addWidget(fillAllBtn, 0, 1);
        batchGrid->addWidget(fillShapeBtn, 1, 0, 1, 2);
        sideLay->addLayout(batchGrid);

        connect(replaceBtn, &QPushButton::clicked,
                this, &ProjectionBlockEditorDialog::onReplaceAll);
        connect(fillAllBtn, &QPushButton::clicked,
                this, &ProjectionBlockEditorDialog::onFillAll);
        connect(fillShapeBtn, &QPushButton::clicked,
                this, &ProjectionBlockEditorDialog::onFillShapeClicked);
    }
    mainRow->addWidget(side, 0);
    root->addLayout(mainRow, 1);

    // ============================================================ 状态条 + 底部按钮
    auto *statusRow = new QHBoxLayout();
    statusRow->setSpacing(12);
    m_posLabel = new QLabel(tr("悬停：—"), this);
    m_posLabel->setObjectName(QStringLiteral("projStatusLabel"));
    m_statsLabel = new QLabel(this);
    m_statsLabel->setObjectName(QStringLiteral("projStatusLabel"));
    m_statsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusRow->addWidget(m_posLabel, 1);
    statusRow->addWidget(m_statsLabel, 0);
    root->addLayout(statusRow);

    auto *bottom = new QHBoxLayout();
    bottom->setSpacing(8);
    auto *hintLabel = new QLabel(
        tr("左键破坏 · 右键放置 · 拖动转向 · 滚轮推进 · WASD 飞行 · 空格/Shift 升降 · 双击 W 或 Ctrl 疾跑"), this);
    hintLabel->setObjectName(QStringLiteral("projMutedLabel"));
    hintLabel->setToolTip(tr("视角操作与《我的世界》一致：拖动鼠标原地转动视角；WASD 沿视线飞行；"
                             "空格上升、Shift 下降；双击 W 或按住 Ctrl 疾跑；滚轮沿视线推进 / 后退。"));
    bottom->addWidget(hintLabel, 1);

    auto *closeBtn = new QPushButton(tr("关闭"), this);
    m_saveAsBtn = new QPushButton(tr("另存为"), this);
    m_saveBtn = new QPushButton(tr("保存修改"), this);
    closeBtn->setObjectName(QStringLiteral("projDialogBtn"));
    m_saveAsBtn->setObjectName(QStringLiteral("projDialogBtn"));
    m_saveBtn->setObjectName(QStringLiteral("projDialogBtnPrimary"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    m_saveAsBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setDefault(true);
    m_saveAsBtn->setToolTip(tr("保存为新的投影文件（Ctrl+Shift+S）"));
    m_saveBtn->setToolTip(tr("写回原文件（Ctrl+S）"));
    bottom->addWidget(closeBtn);
    bottom->addWidget(m_saveAsBtn);
    bottom->addWidget(m_saveBtn);
    root->addLayout(bottom);

    applyEditorChrome();
    applyEditorChromeExtras();

    // ============================================================ 连接
    connect(m_toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectionBlockEditorDialog::onToolChanged);
    connect(m_undoBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onUndo);
    connect(m_redoBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onRedo);
    connect(m_view, &VoxelViewWidget::placeRequested, this, &ProjectionBlockEditorDialog::onPlace);
    connect(m_view, &VoxelViewWidget::breakRequested, this, &ProjectionBlockEditorDialog::onBreak);
    connect(m_view, &VoxelViewWidget::selectRequested, this, &ProjectionBlockEditorDialog::onSelect);
    connect(m_view, &VoxelViewWidget::geometryRebuilt,
            this, &ProjectionBlockEditorDialog::onGeometryRebuilt);
    connect(m_clearSelBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onClearSelection);
    connect(m_focusBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onFocusView);
    connect(m_shotBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onExportScreenshot);
    connect(m_layerPrevBtn, &QPushButton::clicked, this, [this]() { onStepLayer(-1); });
    connect(m_layerNextBtn, &QPushButton::clicked, this, [this]() { onStepLayer(1); });

    // 切换选取范围后，按新范围重新选中上次点到的方块
    connect(m_selectScopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (isBoxSelectScope())
        {
            // 框选靠左键点两个角点，顺手切到「选取」模式，
            // 否则当前若在「编辑」模式，左键点角点会把方块直接破坏掉
            if (m_tool != ToolSelect && m_toolCombo)
                m_toolCombo->setCurrentIndex(0);
            // 进入框选从头开始：清掉其他范围留下的选中和上一次的框
            clearSelection();
            return;
        }
        if (m_selAnchorX == INT_MIN)
        {
            updateSelectionLabel();
            return;
        }
        m_view->setSelectedCells(collectSelection(m_selAnchorX, m_selAnchorY, m_selAnchorZ));
        updateSelectionLabel();
    });
    connect(m_view, &VoxelViewWidget::hoverChanged,
            this, &ProjectionBlockEditorDialog::updateHoverLabel);
    connect(m_paletteList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (!it)
            return;
        const int idx = it->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_blocks.size())
        {
            setCurrentBlockId(m_blocks.at(idx).toString());
            m_view->setDirty();
        }
    });
    connect(m_pickBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onPickBlock);
    // 选项文字是中文名，悬停提示里给出完整方块 ID，便于核对
    connect(m_currentBlockCombo, &QComboBox::currentTextChanged, this, [this](const QString &) {
        m_currentBlockCombo->setToolTip(currentBlockId());
        refreshToolButtonIcons();
    });
    connect(m_airBtn, &QToolButton::toggled, this, &ProjectionBlockEditorDialog::onShowAirToggled);
    connect(m_layerSlider, &QSlider::valueChanged, this, &ProjectionBlockEditorDialog::onFillLayerChanged);
    // 「此层及以下」开关变化时重算显示
    connect(m_belowToggle, &QToolButton::toggled, this, [this](bool) {
        if (m_doc.isLoaded())
            onFillLayerChanged(m_layerSlider->value());
    });
    connect(m_placeAdjacentToggle, &QToolButton::toggled,
            this, &ProjectionBlockEditorDialog::onPlaceAdjacentToggled);
    // 开关的默认勾选发生在连接信号之前，这里补一次同步，
    // 保证一打开编辑器「贴面放置」就真的生效（否则要手动点两次）
    onPlaceAdjacentToggled(m_placeAdjacentToggle->isChecked());
    connect(m_saveAsBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onSaveAs);
    connect(m_saveBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onSaveInPlace);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_paletteFilter, &QLineEdit::textChanged, this, &ProjectionBlockEditorDialog::filterPalette);

    setupShortcuts();
    refreshToolButtonIcons();
    refreshCurrentBlockOptions();
    updateUndoRedo();
    updateSelectionLabel();
    updateTitle();
}

void ProjectionBlockEditorDialog::applyEditorChrome()
{
    // 统一使用主题令牌着色，与启动器其他页面保持一致的卡片/控件语言
    setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QDialog#ProjectionBlockEditorDialog {"
        "  background-color: @BG_BASE@;"
        "}"
        "QWidget#projToolbarCard,"
        "QWidget#projViewCard,"
        "QWidget#projSideCard {"
        "  background-color: @BG_CARD@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 12px;"
        "}"
        "QLabel#projSectionTitle {"
        "  color: @TEXT_PRIMARY@;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "  border: none;"
        "  padding-top: 4px;"
        "}"
        "QLabel#projMutedLabel {"
        "  color: @TEXT_TERTIARY@;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#projLayerLabel {"
        "  color: @TEXT_SECONDARY@;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QComboBox#projToolCombo {"
        "  min-height: 28px;"
        "  padding: 4px 28px 4px 12px;"
        "  border-radius: 8px;"
        "  font-size: 13px;"
        "}"
        "QComboBox#projBlockCombo {"
        "  min-height: 28px;"
        "  padding: 4px 28px 4px 10px;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "}"
        "QLineEdit#projSearchEdit {"
        "  padding: 7px 12px;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "}"
        "QListWidget#projPaletteList {"
        "  border-radius: 10px;"
        "  padding: 4px;"
        "  font-size: 12px;"
        "}"
        "QListWidget#projPaletteList::item {"
        "  padding: 6px 10px;"
        "  border-radius: 6px;"
        "  margin: 1px 0;"
        "}"
        "QPushButton#projToolBtn {"
        "  background-color: @BG_CONTENT@;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 8px;"
        "  padding: 6px 14px;"
        "  font-size: 12px;"
        "}"
        "QPushButton#projToolBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  border-color: @BORDER_STRONG@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#projToolBtn:pressed {"
        "  background-color: @BG_PRESSED@;"
        "}"
        "QPushButton#projToolBtn:disabled {"
        "  color: @TEXT_DISABLED@;"
        "  border-color: @BORDER_DISABLED@;"
        "  background-color: @BG_DISABLED@;"
        "}"
        "QToolButton#projToggleBtn {"
        "  background-color: transparent;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 8px;"
        "  padding: 5px 12px;"
        "  font-size: 12px;"
        "}"
        "QToolButton#projToggleBtn:hover {"
        "  border-color: @BORDER_STRONG@;"
        "  color: @TEXT_PRIMARY@;"
        "  background-color: @BG_HOVER@;"
        "}"
        "QToolButton#projToggleBtn:checked {"
        "  background-color: @PRIMARY_RGBA_20@;"
        "  border-color: @PRIMARY@;"
        "  color: @PRIMARY@;"
        "}"
        "QPushButton#projDialogBtn {"
        "  background-color: @BG_CARD@;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid @BORDER_STRONG@;"
        "  border-radius: 10px;"
        "  padding: 8px 22px;"
        "  font-size: 13px;"
        "}"
        "QPushButton#projDialogBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#projDialogBtn:pressed {"
        "  background-color: @BG_PRESSED@;"
        "}"
        "QPushButton#projDialogBtnPrimary {"
        "  background-color: @PRIMARY@;"
        "  color: @TEXT_ON_PRIMARY@;"
        "  border: none;"
        "  border-radius: 10px;"
        "  padding: 8px 24px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#projDialogBtnPrimary:hover {"
        "  background-color: @PRIMARY_HOVER@;"
        "}"
        "QPushButton#projDialogBtnPrimary:pressed {"
        "  background-color: @PRIMARY_PRESSED@;"
        "}"
        "QWidget#projVoxelView {"
        "  border-radius: 8px;"
        "}"
    )));
}

bool ProjectionBlockEditorDialog::loadProjection(const QString &filePath, const QString &instancePath)
{
    if (!m_doc.load(filePath))
    {
        AppMessageBox::warning(this, tr("打开失败"),
                               tr("无法解析投影文件：\n%1\n\n仅支持 .litematic 格式。").arg(filePath));
        return false;
    }
    m_filePath = filePath;
    if (!instancePath.isEmpty())
        m_instancePath = instancePath;
    setWindowTitle(tr("投影方块编辑器 - %1").arg(displayName()));
    m_dirty = false;
    m_undoStack.clear();
    m_redoStack.clear();
    updateUndoRedo();

    m_layerSlider->setRange(0, m_doc.sizeY());
    m_layerSlider->setValue(0);
    m_layerLabel->setText(tr("全部"));
    // 层与模式一起设，只重建一次几何
    m_view->setLayerView(-1, VoxelViewWidget::LayerMode::All);
    if (m_belowToggle)
        m_belowToggle->setChecked(false);

    loadBlockRegistry();
    populatePalette();
    // 材质/形状挂上时各自会 rebuild 一次；这里先不 setDocument，等设置应用完再一次性建几何
    applyTextureAndShapes();
    applyEditorSettings();
    // 材质就绪后重建一次调色板与「当前方块」下拉框：方块图标统一取自当前实例的贴图，
    // 与投影编辑页材料清单的图标来源保持一致（设置里关闭材质时才回退纯色块）。
    populatePalette();
    refreshCurrentBlockOptions();
    // 应用设置可能改过材质/形状；文档挂上后只建一次几何
    m_view->setDocument(&m_doc);
    clearSelection();
    m_view->resetView();
    m_view->setFocus(Qt::OtherFocusReason);
    updateTitle();
    onRefreshStats();
    return true;
}

void ProjectionBlockEditorDialog::applyTextureAndShapes()
{
    m_texLoader = nullptr;
    m_shapeLoader = nullptr;
    m_view->setTextureLoader(nullptr);
    m_view->setShapeLoader(nullptr);

    if (m_instancePath.isEmpty())
        return;

    m_texLoader = InstanceTextureLoader::sharedForInstance(m_instancePath);
    if (m_texLoader)
        m_view->setTextureLoader(m_texLoader);

    m_shapeLoader = BlockShapeLoader::sharedForInstance(m_instancePath);
    if (m_shapeLoader)
    {
        m_view->setShapeLoader(m_shapeLoader);
        // 预热调色板里出现过的形状，避免首帧大量随机读 jar
        QStringList states;
        states.reserve(m_blocks.size());
        for (const SchematicBlockState &st : m_blocks)
            states.append(st.toString());
        m_shapeLoader->prewarm(states);
    }
}

void ProjectionBlockEditorDialog::setTextureSource(const QString &instancePath)
{
    m_instancePath = instancePath;
    if (instancePath.isEmpty())
    {
        m_texLoader = nullptr;
        m_shapeLoader = nullptr;
        m_blockCnMap.clear();
        refreshCurrentBlockOptions();
        m_view->setTextureLoader(nullptr);
        m_view->setShapeLoader(nullptr);
        if (m_doc.isLoaded())
            populatePalette();
        rebuildView();
        return;
    }
    applyTextureAndShapes();
    loadBlockRegistry();
    if (m_doc.isLoaded())
        populatePalette();
    rebuildView();
}

QString ProjectionBlockEditorDialog::displayName() const
{
    return m_doc.name().isEmpty() ? QFileInfo(m_filePath).completeBaseName() : m_doc.name();
}

void ProjectionBlockEditorDialog::populatePalette()
{
    QSet<QString> seen;
    m_blocks.clear();
    m_blocks.append(SchematicBlockState{ QStringLiteral("minecraft:air"), {} }); // 0 空气
    m_paletteList->clear();

    // 已出现方块（优先）+ 常用方块补全
    for (const auto &state : m_doc.palette(false))
    {
        const QString key = state.toString();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        m_blocks.append(state);
        const int idx = m_blocks.size() - 1;
        auto *item = new QListWidgetItem(friendlyBlockStateLabel(state, m_blockCnMap));
        item->setData(Qt::UserRole, idx);
        item->setData(Qt::UserRole + 1, state.name);
        const QString cn = lookupChineseName(m_blockCnMap, state.name);
        if (!cn.isEmpty())
            item->setData(Qt::UserRole + 2, cn);
        item->setToolTip(blockStateTooltip(state));
        item->setIcon(paletteItemIcon(state));
        m_paletteList->addItem(item);
    }

    // 补全常用方块（含多种典型状态）
    static const char *kCommon[] = {
        "minecraft:stone",
        "minecraft:glass",
        "minecraft:sand",
        "minecraft:dirt",
        "minecraft:grass_block",
        "minecraft:oak_planks",
        "minecraft:oak_log",
        "minecraft:oak_log[axis=x]",
        "minecraft:water",
        "minecraft:lava",
        "minecraft:bricks",
        "minecraft:iron_block",
        "minecraft:gold_block",
        "minecraft:diamond_block",
        "minecraft:redstone_block",
        "minecraft:sea_lantern",
        "minecraft:white_wool",
        "minecraft:black_wool",
        "minecraft:oak_stairs",
        "minecraft:oak_stairs[facing=north,half=bottom,shape=straight]",
        "minecraft:oak_stairs[facing=south,half=top,shape=inner_left]",
        "minecraft:stone_slab",
        "minecraft:stone_slab[type=double]",
        "minecraft:oak_slab",
        "minecraft:oak_slab[type=top]",
        "minecraft:oak_fence",
        "minecraft:oak_fence[east=true,north=false,south=false,west=false]",
        "minecraft:oak_door[half=lower,hinge=left,open=false,facing=north]",
        "minecraft:oak_door[half=upper,hinge=left,open=false,facing=north]",
        "minecraft:oak_trapdoor[facing=north,half=bottom,open=false]",
        "minecraft:oak_trapdoor[facing=north,half=bottom,open=true]",
        "minecraft:oak_trapdoor[facing=south,half=top,open=false]",
        "minecraft:iron_trapdoor[facing=north,half=bottom,open=false]",
        "minecraft:dark_oak_trapdoor[facing=west,half=bottom,open=true]",
        "minecraft:torch",
        "minecraft:wall_torch[facing=north]",
        "minecraft:lever[face=wall,facing=north,powered=false]",
        "minecraft:redstone_wire[east=none,north=side,south=none,west=none]",
        "minecraft:chest[facing=north,type=single,waterlogged=false]",
        "minecraft:sign",
        "minecraft:oak_wall_sign[facing=north]",
    };
    for (const char *n : kCommon)
    {
        const QString key = QString::fromLatin1(n);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        SchematicBlockState st = SchematicBlockState::fromString(key);
        m_blocks.append(st);
        const int idx = m_blocks.size() - 1;
        auto *item = new QListWidgetItem(friendlyBlockStateLabel(st, m_blockCnMap));
        item->setData(Qt::UserRole, idx);
        item->setData(Qt::UserRole + 1, st.name);
        const QString cn = lookupChineseName(m_blockCnMap, st.name);
        if (!cn.isEmpty())
            item->setData(Qt::UserRole + 2, cn);
        item->setToolTip(blockStateTooltip(st));
        item->setIcon(paletteItemIcon(st));
        m_paletteList->addItem(item);
    }
    if (m_paletteFilter)
        m_paletteFilter->clear();
    onRefreshStats();
}

QString ProjectionBlockEditorDialog::currentBlockId() const
{
    if (!m_currentBlockCombo)
        return QString();
    const QString text = m_currentBlockCombo->currentText().trimmed();
    if (text.isEmpty())
        return QString();

    // 未编辑时直接用项里存的完整方块 ID（显示名是中文，无法反向解析）
    const int idx = m_currentBlockCombo->currentIndex();
    if (idx >= 0 && m_currentBlockCombo->itemText(idx) == m_currentBlockCombo->currentText())
    {
        const QString data = m_currentBlockCombo->itemData(idx).toString();
        if (!data.isEmpty())
            return data;
    }
    // 手动输入/粘贴：同时兼容中文名与英文 ID
    return InstanceBlockNames::resolveId(m_blockCnMap, text);
}

void ProjectionBlockEditorDialog::refreshCurrentBlockOptions()
{
    if (!m_currentBlockCombo)
        return;

    const QString keep = currentBlockId();

    QSignalBlocker blocker(m_currentBlockCombo);
    m_currentBlockCombo->clear();
    for (const char *id : kCurrentBlockPresets)
        addCurrentBlockItem(QString::fromLatin1(id));

    if (!keep.isEmpty())
    {
        setCurrentBlockId(keep);
    }
    else if (m_currentBlockCombo->count() > 0)
    {
        // 默认选中石头而不是空气，避免「放置」工具点下去看不出变化
        setCurrentBlockId(QStringLiteral("minecraft:stone"));
        if (m_currentBlockCombo->currentIndex() < 0)
            m_currentBlockCombo->setCurrentIndex(0);
    }

    m_currentBlockCombo->setToolTip(currentBlockId());
}

void ProjectionBlockEditorDialog::addCurrentBlockItem(const QString &blockId)
{
    if (!m_currentBlockCombo)
        return;
    const QString id = blockId.trimmed();
    if (id.isEmpty())
        return;

    m_currentBlockCombo->addItem(InstanceBlockNames::displayName(m_blockCnMap, id), id);
    const int idx = m_currentBlockCombo->count() - 1;
    m_currentBlockCombo->setItemData(idx, id, Qt::ToolTipRole);
    m_currentBlockCombo->setItemIcon(idx, paletteItemIcon(SchematicBlockState::fromString(id)));
}

QIcon ProjectionBlockEditorDialog::paletteItemIcon(const SchematicBlockState &state) const
{
    if (state.isAir())
    {
        // 空气用虚线空心方块表示，避免和「没有图标」混淆
        QPixmap pm(40, 40);
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(tokenColor("TEXT_TERTIARY", QColor(0x88, 0x88, 0x88)), 1.4);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(5, 5, 10, 10), 2.4, 2.4);
        p.end();
        return QIcon(pm);
    }

    if (m_texLoader)
    {
        const QIcon icon = m_texLoader->blockIcon(state.name, 20);
        if (!icon.isNull())
            return icon;
    }
    return makeSwatchIcon(VoxelViewWidget::blockColorOf(state.name), 20);
}

void ProjectionBlockEditorDialog::setCurrentBlockId(const QString &blockId)
{
    if (!m_currentBlockCombo)
        return;
    const QString id = blockId.trimmed();
    if (id.isEmpty())
        return;

    // 已有相同方块（忽略属性书写顺序）的选项就直接选中
    const SchematicBlockState target = SchematicBlockState::fromString(id);
    for (int i = 0; i < m_currentBlockCombo->count(); ++i)
    {
        const QString data = m_currentBlockCombo->itemData(i).toString();
        if (data.isEmpty())
            continue;
        if (data == id || SchematicBlockState::fromString(data) == target)
        {
            m_currentBlockCombo->setCurrentIndex(i);
            m_currentBlockCombo->setToolTip(data);
            return;
        }
    }

    // 否则追加一条（显示中文名，内部仍是完整方块 ID）
    addCurrentBlockItem(id);
    m_currentBlockCombo->setCurrentIndex(m_currentBlockCombo->count() - 1);
    m_currentBlockCombo->setToolTip(id);
}

void ProjectionBlockEditorDialog::applyToolToView()
{
    if (!m_view)
        return;
    // 左键随模式切换：选取模式左键选取、编辑模式左键破坏；
    // 右键在两种模式下都是放置当前方块，这样「左键破坏 / 右键放置」始终可用
    if (m_tool == ToolSelect)
        m_view->setLeftAction(VoxelViewWidget::LeftAction::Select);
    else
        m_view->setLeftAction(VoxelViewWidget::LeftAction::Break);
}

void ProjectionBlockEditorDialog::onToolChanged(int index)
{
    const int tool = m_toolCombo->itemData(index).toInt();
    m_tool = tool;
    // 左键动作随模式切换（选取 / 编辑），右键始终放置
    applyToolToView();
    if (tool != ToolSelect)
        m_view->setCursor(Qt::ArrowCursor);
    // 切走「选取」模式时，还没点完的填充角点自动作废，免得之后误点生成
    if (tool != ToolSelect && m_fillActive)
        cancelFillPick();
    updateSelectionLabel();
    m_view->setDirty();

    // 同步工具栏分段按钮的勾选态
    for (QPushButton *btn : m_toolButtons)
    {
        if (!btn)
            continue;
        btn->setChecked(btn->property("toolValue").toInt() == tool);
    }
    refreshToolButtonIcons();
}

void ProjectionBlockEditorDialog::onPlace(int x, int y, int z)
{
    const int cell = cellIndexAt(x, y, z);
    const int beforeIndex = (cell >= 0) ? m_doc.paletteIndexAt(cell) : -1;

    SchematicBlockState st = SchematicBlockState::fromString(currentBlockId());
    QString err;
    m_doc.setBlock(x, y, z, st, &err);
    if (cell >= 0)
        recordCellEdit(cell, beforeIndex, m_doc.paletteIndexAt(cell), tr("放置方块"));
    m_dirty = true;
    updateTitle();
    rebuildView();
    onRefreshStats();
}

void ProjectionBlockEditorDialog::onBreak(int x, int y, int z)
{
    const int cell = cellIndexAt(x, y, z);
    const int beforeIndex = (cell >= 0) ? m_doc.paletteIndexAt(cell) : -1;

    QString err;
    m_doc.setBlock(x, y, z, SchematicBlockState{ QStringLiteral("minecraft:air"), {} }, &err);
    if (cell >= 0)
        recordCellEdit(cell, beforeIndex, m_doc.paletteIndexAt(cell), tr("破坏方块"));
    m_dirty = true;
    updateTitle();
    rebuildView();
    onRefreshStats();
}

void ProjectionBlockEditorDialog::onSelect(int x, int y, int z)
{
    if (!m_doc.isLoaded())
        return;

    // ── 填充方块：先把两个对角点收齐，再按所选形状生成 ──
    // 两步都用左键点击视图里的方块（和「框选（两点）」一致），
    // 第一个角点用金色描边标出来，点到空白处则放弃这次填充。
    if (m_fillActive)
    {
        if (x == INT_MIN || y == INT_MIN || z == INT_MIN)
        {
            cancelFillPick();
            if (m_selectLabel)
                m_selectLabel->setText(tr("已取消填充（点到了空白处）；需要时重新点「填充方块」选形状"));
            return;
        }

        if (m_fillAX == INT_MIN)
        {
            m_fillAX = x;
            m_fillAY = y;
            m_fillAZ = z;
            if (m_view)
                m_view->clearSelection();
            syncFillMarker();
            updateSelectionLabel();
            return;
        }

        const int ax = m_fillAX, ay = m_fillAY, az = m_fillAZ;
        const ProjectionFillShape shape = m_fillShape;
        const bool airOnly = m_fillAirOnly;
        m_fillActive = false;
        m_fillAX = m_fillAY = m_fillAZ = INT_MIN;
        syncFillMarker();

        const QVector<int> written = applyFillShape(shape, airOnly, ax, ay, az, x, y, z);
        if (written.isEmpty())
        {
            if (m_view)
                m_view->clearSelection();
            updateSelectionLabel();
            AppMessageBox::information(this, tr("填充方块"),
                tr("这个范围里没有可填充的格子：可能已经都是当前方块，"
                   "或勾选了「只填充空气」而位置都被占用了。"));
            return;
        }

        if (m_view)
        {
            // 生成结果太多时不再整片标绿：选中线框是按格画的，几万格就能明显拖慢渲染
            if (written.size() <= 60000)
            {
                QSet<int> cells;
                cells.reserve(written.size());
                for (int cell : written)
                    cells.insert(cell);
                m_view->setSelectedCells(cells);
            }
            else
            {
                m_view->clearSelection();
            }
        }
        updateSelectionLabel();
        if (m_selectLabel)
            m_selectLabel->setText(tr("已用「%1」生成 %2 个方块%3；可继续点两个角点，或重新选形状")
                .arg(ProjectionFillShapes::displayName(shape))
                .arg(QLocale().toString(written.size()))
                .arg(written.size() <= 60000 ? tr("（绿色边框）") : QString()));
        return;
    }

    // 点到空白/天空（视图给的坐标是 INT_MIN）：先收起待定角点，再清掉选中
    if (x == INT_MIN || y == INT_MIN || z == INT_MIN)
    {
        if (boxSelectPending())
        {
            clearBoxPivot();
            updateSelectionLabel();
        }
        else
        {
            clearSelection();
        }
        return;
    }

    // ── 两点框选：第一次左键记下第一个角点，第二次左键完成整块框选 ──
    if (isBoxSelectScope())
    {
        if (m_doc.block(x, y, z).isAir())
        {
            // 点到空气格：先收起待定角点，再考虑清空选中
            if (boxSelectPending())
            {
                clearBoxPivot();
                updateSelectionLabel();
            }
            else
            {
                clearSelection();
            }
            return;
        }

        if (!boxSelectPending())
        {
            // 第一个角点：清掉上一次的选中，先用金色描边标出起点
            m_boxPivotX = x;
            m_boxPivotY = y;
            m_boxPivotZ = z;
            m_boxAX = m_boxAY = m_boxAZ = INT_MIN;
            m_boxBX = m_boxBY = m_boxBZ = INT_MIN;
            if (m_view)
                m_view->clearSelection();
            syncBoxPivotMarker();
            updateSelectionLabel();
            return;
        }

        // 第二个角点：两点围出的立方体里所有方块一起选中
        if (m_view)
            m_view->setSelectedCells(
                collectBoxSelection(m_boxPivotX, m_boxPivotY, m_boxPivotZ, x, y, z));
        m_boxAX = m_boxPivotX;
        m_boxAY = m_boxPivotY;
        m_boxAZ = m_boxPivotZ;
        m_boxBX = x;
        m_boxBY = y;
        m_boxBZ = z;
        clearBoxPivot();
        // 之后若切回单方块 / 同类型，以第二个角点为准重新选中
        m_selAnchorX = x;
        m_selAnchorY = y;
        m_selAnchorZ = z;
        updateSelectionLabel();
        return;
    }

    const SchematicBlockState st = m_doc.block(x, y, z);
    if (st.isAir())
    {
        // 点到空白处 = 取消选择
        clearSelection();
        return;
    }
    m_selAnchorX = x;
    m_selAnchorY = y;
    m_selAnchorZ = z;
    m_view->setSelectedCells(collectSelection(x, y, z));
    updateSelectionLabel();
}

void ProjectionBlockEditorDialog::onClearSelection()
{
    clearSelection();
}

int ProjectionBlockEditorDialog::selectScope() const
{
    return m_selectScopeCombo ? m_selectScopeCombo->currentData().toInt() : 0;
}

QSet<int> ProjectionBlockEditorDialog::collectSelection(int x, int y, int z) const
{
    QSet<int> cells;
    if (!m_doc.isLoaded())
        return cells;
    const int sx = m_doc.sizeX();
    const int sy = m_doc.sizeY();
    const int sz = m_doc.sizeZ();
    const int lx = x - m_doc.originX();
    const int ly = y - m_doc.originY();
    const int lz = z - m_doc.originZ();
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sx || ly >= sy || lz >= sz)
        return cells;
    const SchematicBlockState target = m_doc.block(x, y, z);
    if (target.isAir())
        return cells;

    if (selectScope() == 0)
    {
        cells.insert(ly * m_doc.layerSize() + lz * sx + lx);
        return cells;
    }

    // 全部同类型：按方块 ID 匹配（忽略朝向等状态属性），跳过空气
    const int vol = m_doc.volume();
    for (int i = 0; i < vol; ++i)
    {
        const SchematicBlockState s = m_doc.blockAt(i);
        if (!s.isAir() && s.name == target.name)
            cells.insert(i);
    }
    return cells;
}

QSet<int> ProjectionBlockEditorDialog::collectBoxSelection(int x1, int y1, int z1,
                                                           int x2, int y2, int z2) const
{
    QSet<int> cells;
    if (!m_doc.isLoaded())
        return cells;

    // 世界坐标 → 本地坐标；框可以超出投影边界，超出的部分直接忽略
    const int sx = m_doc.sizeX();
    const int sy = m_doc.sizeY();
    const int sz = m_doc.sizeZ();
    const int lx0 = qMin(x1, x2) - m_doc.originX();
    const int lx1 = qMax(x1, x2) - m_doc.originX();
    const int ly0 = qMin(y1, y2) - m_doc.originY();
    const int ly1 = qMax(y1, y2) - m_doc.originY();
    const int lz0 = qMin(z1, z2) - m_doc.originZ();
    const int lz1 = qMax(z1, z2) - m_doc.originZ();
    if (lx1 < 0 || ly1 < 0 || lz1 < 0 || lx0 >= sx || ly0 >= sy || lz0 >= sz)
        return cells;   // 整个框都在投影外面

    const int xa = qMax(0, lx0), xb = qMin(sx - 1, lx1);
    const int ya = qMax(0, ly0), yb = qMin(sy - 1, ly1);
    const int za = qMax(0, lz0), zb = qMin(sz - 1, lz1);

    // 逐格只查调色板下标是否空气，不构造方块状态字符串（大框也够快）
    const int layer = m_doc.layerSize();
    for (int y = ya; y <= yb; ++y)
    {
        const int planeBase = y * layer;
        for (int z = za; z <= zb; ++z)
        {
            const int rowBase = planeBase + z * sx;
            for (int x = xa; x <= xb; ++x)
            {
                const int cell = rowBase + x;
                if (!m_doc.paletteEntry(m_doc.paletteIndexAt(cell)).isAir())
                    cells.insert(cell);
            }
        }
    }
    return cells;
}

void ProjectionBlockEditorDialog::clearBoxPivot()
{
    m_boxPivotX = INT_MIN;
    m_boxPivotY = INT_MIN;
    m_boxPivotZ = INT_MIN;
    syncBoxPivotMarker();
}

void ProjectionBlockEditorDialog::syncBoxPivotMarker()
{
    if (!m_view)
        return;
    // 第一个角点用视图自带的高亮通道（金色描边 + 淡金填充）标出来，
    // 第二个角点还没点时，用户始终能看见框选的起点在哪
    QSet<int> marker;
    if (boxSelectPending())
    {
        const int cell = cellIndexAt(m_boxPivotX, m_boxPivotY, m_boxPivotZ);
        if (cell >= 0)
            marker.insert(cell);
    }
    m_view->setHighlightCells(marker);
}

void ProjectionBlockEditorDialog::updateSelectionLabel()
{
    if (!m_selectLabel)
        return;
    const int n = m_view ? m_view->selectedCells().size() : 0;

    // 「填充方块」正在等两个角点：优先提示这一步该点哪里
    if (m_fillActive)
    {
        const QString name = ProjectionFillShapes::displayName(m_fillShape);
        const QString airOnlyHint = m_fillAirOnly
            ? tr("；只填空格，已有方块不会被覆盖") : QString();
        if (m_fillAX == INT_MIN)
        {
            m_selectLabel->setText(tr("填充·%1：左键点第一个对角方块（会标成金色框），"
                                      "再点对角方块生成%2").arg(name).arg(airOnlyHint));
        }
        else
        {
            m_selectLabel->setText(tr("填充·%1：已标记第一个角点 (%2, %3, %4)，"
                                      "左键点对角方块生成%5")
                .arg(name).arg(m_fillAX).arg(m_fillAY).arg(m_fillAZ).arg(airOnlyHint));
        }
        if (m_clearSelBtn)
            m_clearSelBtn->setEnabled(true);
        return;
    }

    if (isBoxSelectScope())
    {
        if (boxSelectPending())
        {
            m_selectLabel->setText(tr("已标记第一个角点 (%1, %2, %3)（金色框），左键点对角方块完成框选")
                .arg(m_boxPivotX).arg(m_boxPivotY).arg(m_boxPivotZ));
        }
        else if (n > 0 && hasDoneBox())
        {
            const int w = qAbs(m_boxBX - m_boxAX) + 1;
            const int h = qAbs(m_boxBY - m_boxAY) + 1;
            const int d = qAbs(m_boxBZ - m_boxAZ) + 1;
            m_selectLabel->setText(tr("已框选 %1×%2×%3 区域，共 %4 个方块（绿色边框）；点「替换方块」可一键换成当前方块")
                .arg(w).arg(h).arg(d).arg(n));
        }
        else if (hasDoneBox())
        {
            m_selectLabel->setText(tr("这个框选范围里没有方块，重新点两个角点试试"));
        }
        else if (m_tool == ToolSelect)
        {
            m_selectLabel->setText(tr("左键先后点两个对角方块，框住的那一整块会一起选中"));
        }
        else
        {
            m_selectLabel->setText(tr("切到「选取」模式（快捷键 1）后左键点两个对角方块即可框选"));
        }
    }
    else if (n > 0)
    {
        m_selectLabel->setText(tr("已选中 %1 个方块（绿色边框），点「替换方块」可一键换成当前方块").arg(n));
    }
    else if (m_tool == ToolSelect)
    {
        m_selectLabel->setText(tr("左键单击方块即可选中"));
    }
    else
    {
        m_selectLabel->setText(tr("切到「选取」模式（快捷键 1）后左键单击方块即可选中"));
    }

    if (m_clearSelBtn)
        m_clearSelBtn->setEnabled(n > 0 || boxSelectPending());
}

void ProjectionBlockEditorDialog::clearSelection()
{
    m_selAnchorX = INT_MIN;
    m_selAnchorY = INT_MIN;
    m_selAnchorZ = INT_MIN;
    m_boxAX = m_boxAY = m_boxAZ = INT_MIN;
    m_boxBX = m_boxBY = m_boxBZ = INT_MIN;
    // 取消填充的待定角点（已经生成的方块保持不动）
    m_fillActive = false;
    m_fillAX = m_fillAY = m_fillAZ = INT_MIN;
    clearBoxPivot();
    if (m_view)
        m_view->clearSelection();
    updateSelectionLabel();
}

void ProjectionBlockEditorDialog::onFillLayerChanged(int value)
{
    if (!m_doc.isLoaded())
        return;
    // value: 0 = 全部；1..sizeY = 第 value 层（本地 Y = value-1）
    if (value <= 0)
    {
        m_layerLabel->setText(tr("全部"));
        m_view->setLayerView(-1, VoxelViewWidget::LayerMode::All);
    }
    else
    {
        m_layerLabel->setText(tr("Y层 %1").arg(value));
        // 勾选「此层及以下」→ 显示当前层及以下（下方变暗）；否则仅显示当前层；
        // 层与模式一次设好，只重建一次几何（大投影里两次重建会明显卡手）
        m_view->setLayerView(value - 1,
            (m_belowToggle && m_belowToggle->isChecked())
                ? VoxelViewWidget::LayerMode::Below
                : VoxelViewWidget::LayerMode::Single);
    }
    m_view->setDirty();
}

void ProjectionBlockEditorDialog::onShowAirToggled(bool checked)
{
    m_view->setShowAir(checked);
}

// ====================================================================== 撤销 / 重做
// 上一版把快照写进 QTemporaryDir 里的临时文件，目录一析构文件就没了，
// 于是撤销/重做永远失效。现在改为内存差异快照：单格编辑只记「哪一格 + 旧新下标」，
// 批量操作才整表备份，既正确又快得多。
void ProjectionBlockEditorDialog::recordCellEdit(int cell, int beforeIndex, int afterIndex,
                                                 const QString &label)
{
    if (cell < 0 || beforeIndex < 0 || afterIndex < 0 || beforeIndex == afterIndex)
        return;

    UndoStep step;
    step.label = label;
    step.cells.append(cell);
    step.beforeIndices.append(beforeIndex);
    step.afterIndices.append(afterIndex);
    m_undoStack.append(step);
    trimUndoStack();
    m_redoStack.clear();
    updateUndoRedo();
}

void ProjectionBlockEditorDialog::recordBulkEdit(const QList<int> &beforeIndices,
                                                 const QString &label)
{
    if (!m_doc.isLoaded())
        return;
    const QList<int> afterIndices = m_doc.indices();
    if (beforeIndices.size() != afterIndices.size() || beforeIndices == afterIndices)
        return; // 没有实际变化就不占撤销位

    UndoStep step;
    step.label = label;
    step.bulk = true;
    step.beforeAll = beforeIndices;
    step.afterAll = afterIndices;
    m_undoStack.append(step);
    trimUndoStack();
    m_redoStack.clear();
    updateUndoRedo();
}

void ProjectionBlockEditorDialog::trimUndoStack()
{
    constexpr int kMaxSteps = 80;   // 单格记录很轻，可以多留
    constexpr int kMaxBulk = 10;    // 整表快照较重，单独设上限

    while (m_undoStack.size() > kMaxSteps)
        m_undoStack.removeFirst();

    int bulkCount = 0;
    for (int i = m_undoStack.size() - 1; i >= 0; --i)
    {
        if (!m_undoStack.at(i).bulk)
            continue;
        if (++bulkCount > kMaxBulk)
        {
            m_undoStack.removeAt(i);
            break;
        }
    }
}

bool ProjectionBlockEditorDialog::applyUndoStep(const UndoStep &step, bool useBefore)
{
    if (!m_doc.isLoaded())
        return false;

    if (step.bulk)
        return m_doc.restoreIndices(useBefore ? step.beforeAll : step.afterAll);

    const QVector<int> &values = useBefore ? step.beforeIndices : step.afterIndices;
    bool ok = true;
    const int count = qMin(step.cells.size(), values.size());
    for (int i = 0; i < count; ++i)
    {
        if (!m_doc.setPaletteIndexAt(step.cells.at(i), values.at(i)))
            ok = false;
    }
    return ok;
}

void ProjectionBlockEditorDialog::onUndo()
{
    if (m_undoStack.isEmpty() || !m_doc.isLoaded())
        return;

    UndoStep step = m_undoStack.takeLast();
    if (!applyUndoStep(step, true))
    {
        m_undoStack.append(step);
        return;
    }
    m_redoStack.append(step);

    m_dirty = true;
    updateTitle();
    rebuildView();
    clearSelection();
    onRefreshStats();
    updateUndoRedo();
}

void ProjectionBlockEditorDialog::onRedo()
{
    if (m_redoStack.isEmpty() || !m_doc.isLoaded())
        return;

    UndoStep step = m_redoStack.takeLast();
    if (!applyUndoStep(step, false))
    {
        m_redoStack.append(step);
        return;
    }
    m_undoStack.append(step);

    m_dirty = true;
    updateTitle();
    rebuildView();
    clearSelection();
    onRefreshStats();
    updateUndoRedo();
}

void ProjectionBlockEditorDialog::updateUndoRedo()
{
    if (m_undoBtn)
    {
        const bool canUndo = !m_undoStack.isEmpty();
        m_undoBtn->setEnabled(canUndo);
        m_undoBtn->setToolTip(canUndo
            ? tr("撤销：%1（Ctrl+Z）").arg(m_undoStack.last().label)
            : tr("没有可撤销的操作"));
    }
    if (m_redoBtn)
    {
        const bool canRedo = !m_redoStack.isEmpty();
        m_redoBtn->setEnabled(canRedo);
        m_redoBtn->setToolTip(canRedo
            ? tr("重做：%1（Ctrl+Y）").arg(m_redoStack.last().label)
            : tr("没有可重做的操作"));
    }
    refreshToolButtonIcons();
}

void ProjectionBlockEditorDialog::rebuildView()
{
    m_view->setDocument(&m_doc);
    m_view->setDirty();
}

void ProjectionBlockEditorDialog::loadBlockRegistry()
{
    m_blockCnMap = InstanceBlockNames::loadChineseMap(m_instancePath);
    // 译名变化后同步刷新「当前方块」选项的中文显示
    refreshCurrentBlockOptions();
}

void ProjectionBlockEditorDialog::applyEditorSettings()
{
    // 读取内置的投影方块编辑器设置并应用到编辑器初始状态
    // （投影编辑器已内置，旧版插件设置仍会被自动兼容读取）

    // 显示空气
    const bool showAirOn =
        ProjectionEditorSettings::value(QStringLiteral("showAir"),
                                        ProjectionEditorSettings::defaultShowAir())
            == QLatin1String("true");
    if (m_airBtn)
    {
        m_airBtn->setChecked(showAirOn);
        m_view->setShowAir(showAirOn);
    }

    // 默认模式（选取 / 编辑）：按模式名映射到编辑器下拉项；
    // 旧版设置里存的「查看 / 放置 / 破坏 / 填充」一律视为「编辑」
    const QString tool = ProjectionEditorSettings::value(
        QStringLiteral("defaultTool"),
        ProjectionEditorSettings::defaultTool());
    const int toolIndex = (tool == QLatin1String("选取")) ? 0 : 1;
    m_toolCombo->setCurrentIndex(toolIndex);
    m_tool = m_toolCombo->itemData(toolIndex).toInt();
    // setCurrentIndex 在索引没变化时不会发信号，这里显式同步一次左键动作，
    // 否则默认就是第 0 项时，打开编辑器后左键单击不会按所选模式生效
    applyToolToView();

    // 材质开关与来源
    const bool texturesDisabled =
        ProjectionEditorSettings::value(QStringLiteral("useTextures"),
                                        ProjectionEditorSettings::defaultUseTextures())
            == QLatin1String("false");
    if (texturesDisabled)
    {
        // 关闭材质：清除材质/形状加载器（回退到颜色与整格立方体）
        m_texLoader = nullptr;
        m_shapeLoader = nullptr;
        m_view->setTextureLoader(nullptr);
        m_view->setShapeLoader(nullptr);
    }
    else if (m_instancePath.isEmpty() && m_texLoader == nullptr)
    {
        // 未由入口指定实例时，回退到设置的默认材质来源实例
        const QString src = ProjectionEditorSettings::value(QStringLiteral("sourceInstance")).trimmed();
        if (!src.isEmpty() && QDir(src).exists())
            setTextureSource(src);
    }

    // 真实形状渲染开关（活板门等非完整方块；关闭时整格立方体）
    const bool shapesDisabled =
        ProjectionEditorSettings::value(QStringLiteral("useShapes"),
                                        ProjectionEditorSettings::defaultUseShapes())
            == QLatin1String("false");
    if (shapesDisabled)
    {
        m_shapeLoader = nullptr;
        m_view->setShapeLoader(nullptr);
    }
    else if (m_view->shapeLoader() == nullptr && !m_instancePath.isEmpty())
    {
        // 形状加载器被关掉又重新启用，或入口未挂上时，挂回共享加载器
        m_shapeLoader = BlockShapeLoader::sharedForInstance(m_instancePath);
        if (m_shapeLoader)
            m_view->setShapeLoader(m_shapeLoader);
    }

    // 视角操作（与《我的世界》的「鼠标灵敏度 / 反转鼠标」对应）
    if (m_view)
    {
        bool ok = false;
        const int sensPercent = ProjectionEditorSettings::value(
            QStringLiteral("lookSensitivity"), QStringLiteral("50")).toInt(&ok);
        m_view->setLookSensitivity((ok ? qBound(0, sensPercent, 100) : 50) / 100.0f);
        m_view->setInvertLookY(
            ProjectionEditorSettings::value(QStringLiteral("invertLookY"),
                                            QStringLiteral("false")) == QLatin1String("true"));
    }
}

void ProjectionBlockEditorDialog::onRefreshStats()
{
    if (!m_doc.isLoaded())
    {
        if (m_statsLabel) m_statsLabel->setText(tr("未加载"));
        refreshStatsChips();
        return;
    }
    if (m_statsLabel) m_statsLabel->setText(buildStatsText());
    refreshStatsChips();
}

QString ProjectionBlockEditorDialog::buildStatsText() const
{
    if (!m_doc.isLoaded())
        return QString();

    // 先按调色板下标建「是否空气」查找表，再对下标做直方图，
    // 避免逐格构造方块状态字符串（大体积下能快一两个数量级）
    const int paletteCount = m_doc.paletteSize();
    QVector<bool> airOf(paletteCount, true);
    for (int p = 0; p < paletteCount; ++p)
        airOf[p] = m_doc.paletteEntry(p).isAir();

    qint64 nonAir = 0;
    const QList<int> &indices = m_doc.indices();
    for (int i = 0; i < indices.size(); ++i)
    {
        const int p = indices.at(i);
        if (p >= 0 && p < paletteCount && !airOf.at(p))
            ++nonAir;
    }

    int usedStates = 0;
    {
        QVector<bool> used(paletteCount, false);
        for (int i = 0; i < indices.size(); ++i)
        {
            const int p = indices.at(i);
            if (p >= 0 && p < paletteCount && !used.at(p))
            {
                used[p] = true;
                ++usedStates;
            }
        }
    }

    return tr("小计 %1 个方块 · 用到 %2 / %3 种状态")
        .arg(QLocale().toString(nonAir)).arg(usedStates).arg(paletteCount);
}

void ProjectionBlockEditorDialog::filterPalette(const QString &text)
{
    const QString lower = text.toLower().trimmed();
    for (int i = 0; i < m_paletteList->count(); ++i)
    {
        QListWidgetItem *item = m_paletteList->item(i);
        if (!item)
            continue;
        if (lower.isEmpty())
        {
            item->setHidden(false);
            continue;
        }
        const int idx = item->data(Qt::UserRole).toInt();
        const QString rawState = (idx >= 0 && idx < m_blocks.size())
            ? m_blocks.at(idx).toString().toLower()
            : item->text().toLower();
        const QString blockName = item->data(Qt::UserRole + 1).toString().toLower();
        const QString cnName = item->data(Qt::UserRole + 2).toString().toLower();
        item->setHidden(!rawState.contains(lower) && !blockName.contains(lower)
                        && !cnName.contains(lower));
    }
}

void ProjectionBlockEditorDialog::updateTitle()
{
    const QString title = tr("投影方块编辑器 - %1%2")
                              .arg(displayName(), m_dirty ? tr(" *") : QString());
    setWindowTitle(title);
    if (m_fileLabel)
    {
        // 只显示文件名 + 未保存标记，完整路径放到悬停提示里
        const QFileInfo fi(m_filePath);
        m_fileLabel->setText(m_dirty ? tr("%1 · 未保存").arg(fi.fileName())
                                     : fi.fileName());
        m_fileLabel->setToolTip(m_filePath);
    }
}

void ProjectionBlockEditorDialog::onSaveAs()
{
    if (!m_doc.isLoaded())
        return;
    const QFileInfo fi(m_filePath);
    const QString out = QFileDialog::getSaveFileName(
        this, tr("另存为投影"),
        fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("_edit.litematic"),
        tr("Litematica 文件 (*.litematic)"));
    if (out.isEmpty())
        return;
    QString err;
    if (!m_doc.saveCopy(out, &err))
    {
        AppMessageBox::warning(this, tr("保存失败"), tr("无法保存投影：\n%1").arg(err));
        return;
    }
    m_lastSavedPath = out;
    m_dirty = false;
    updateTitle();
    AppMessageBox::information(this, tr("保存成功"), tr("已保存为：\n%1").arg(out));
}

void ProjectionBlockEditorDialog::onSaveInPlace()
{
    if (!m_doc.isLoaded())
        return;
    QString err;
    if (!m_doc.saveCopy(m_filePath, &err))
    {
        AppMessageBox::warning(this, tr("保存失败"), tr("无法保存投影：\n%1").arg(err));
        return;
    }
    m_lastSavedPath = m_filePath;
    m_dirty = false;
    updateTitle();
    AppMessageBox::information(this, tr("保存成功"), tr("修改已写入原文件（%1）。").arg(m_filePath));
}

void ProjectionBlockEditorDialog::onReplaceAll()
{
    if (!m_doc.isLoaded())
        return;

    // ── 有选中：一键把选中的方块换成「当前方块」 ──
    // 选中范围可以是单方块、同类型全部或两点框选，替换只动选中的那些格子
    const QSet<int> selection = m_view ? m_view->selectedCells() : QSet<int>();
    if (!selection.isEmpty())
    {
        const SchematicBlockState target = SchematicBlockState::fromString(currentBlockId());
        if (target.name.isEmpty())
        {
            AppMessageBox::warning(this, tr("替换方块"),
                tr("请先在侧栏「当前方块」里选好要换成的方块。"));
            return;
        }
        const int targetIndex = m_doc.ensurePaletteEntry(target);
        const int volume = m_doc.volume();

        QVector<int> cells;
        QVector<int> beforeIndices;
        cells.reserve(selection.size());
        beforeIndices.reserve(selection.size());
        for (int cell : selection)
        {
            if (cell < 0 || cell >= volume)
                continue;
            const int before = m_doc.paletteIndexAt(cell);
            if (before == targetIndex)
                continue;   // 已经是这个方块，不算一次改动
            cells.append(cell);
            beforeIndices.append(before);
        }

        const QString targetLabel = friendlyBlockStateLabel(target, m_blockCnMap);
        if (cells.isEmpty())
        {
            AppMessageBox::information(this, tr("替换方块"),
                tr("选中的方块已经是「%1」了，不用再替换。").arg(targetLabel));
            return;
        }

        for (int cell : cells)
            m_doc.setPaletteIndexAt(cell, targetIndex);

        // 只记被改动的格子，撤销时逐格还原即可，不用整表快照
        UndoStep step;
        step.label = tr("替换选中方块");
        step.cells = cells;
        step.beforeIndices = beforeIndices;
        step.afterIndices = QVector<int>(cells.size(), targetIndex);
        m_undoStack.append(step);
        trimUndoStack();
        m_redoStack.clear();
        updateUndoRedo();

        m_dirty = true;
        updateTitle();
        rebuildView();
        onRefreshStats();
        // 选中范围原样保留，方便接着把它们再换成别的方块
        updateSelectionLabel();
        AppMessageBox::information(this, tr("替换完成"),
            tr("已把 %1 个选中的方块换成「%2」。").arg(cells.size()).arg(targetLabel));
        return;
    }

    // ── 没有选中：保持整份投影的「整体替换」（按方块名换一种方块） ──
    bool ok1 = false;
    const QString from = AppInputDialog::getText(this, tr("替换方块"),
        tr("要替换的方块（英文 ID 或中文名，如 minecraft:stone、石头）："), QLineEdit::Normal,
        currentBlockId(), &ok1);
    if (!ok1 || from.trimmed().isEmpty())
        return;
    bool ok2b = false;
    const QString to = AppInputDialog::getText(this, tr("替换方块"),
        tr("替换为（英文 ID 或中文名，如 minecraft:glass、玻璃）："), QLineEdit::Normal,
        QStringLiteral("minecraft:glass"), &ok2b);
    if (!ok2b || to.trimmed().isEmpty())
        return;
    const QList<int> beforeAll = m_doc.indices();
    SchematicBlockState fromSt = SchematicBlockState::fromString(
        InstanceBlockNames::resolveId(m_blockCnMap, from));
    SchematicBlockState toSt = SchematicBlockState::fromString(
        InstanceBlockNames::resolveId(m_blockCnMap, to));
    const int n = m_doc.replaceAll(fromSt, toSt);
    m_dirty = true;
    updateTitle();
    rebuildView();
    clearSelection();
    onRefreshStats();
    recordBulkEdit(beforeAll, tr("批量替换"));
    AppMessageBox::information(this, tr("替换完成"), tr("已将 %1 个方块替换。").arg(n));
}

void ProjectionBlockEditorDialog::onFillAll()
{
    if (!m_doc.isLoaded())
        return;
    const QString sel = currentBlockId();
    const QList<int> beforeAll = m_doc.indices();
    SchematicBlockState st = SchematicBlockState::fromString(sel);
    const int n = m_doc.fillAll(st);
    m_dirty = true;
    updateTitle();
    rebuildView();
    clearSelection();
    onRefreshStats();
    recordBulkEdit(beforeAll, tr("全空间填充"));
    AppMessageBox::information(this, tr("填充完成"), tr("已填充 %1 个格子。").arg(n));
}

void ProjectionBlockEditorDialog::onFillShapeClicked()
{
    if (!m_doc.isLoaded())
        return;
    if (currentBlockId().trimmed().isEmpty())
    {
        AppMessageBox::warning(this, tr("填充方块"),
            tr("请先在侧栏「当前方块」里选好要用哪个方块填充。"));
        return;
    }

    // 居中的形状选择弹窗：选完形状后要么进两点拾取，要么改走坐标输入
    ProjectionFillShapeDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const ProjectionFillShape shape = dlg.selectedShape();
    const bool airOnly = dlg.airOnly();
    if (dlg.mode() == ProjectionFillShapeDialog::Mode::Coordinates)
    {
        fillShapeByCoordinates(shape, airOnly);
        return;
    }
    startFillPick(shape, airOnly);
}

void ProjectionBlockEditorDialog::startFillPick(ProjectionFillShape shape, bool airOnly)
{
    if (!m_doc.isLoaded())
        return;

    // 两点拾取靠左键点方块完成：必须切到「选取」模式，
    // 否则当前若在「编辑」模式，左键点角点会先把方块破坏掉
    if (m_tool != ToolSelect && m_toolCombo)
        m_toolCombo->setCurrentIndex(0);

    clearSelection();   // 清掉旧的选中与框选角点（也会清掉上一次的填充角点）
    m_fillShape = shape;
    m_fillAirOnly = airOnly;
    m_fillActive = true;
    m_fillAX = m_fillAY = m_fillAZ = INT_MIN;
    if (m_view)
        m_view->setDirty();
    updateSelectionLabel();
}

void ProjectionBlockEditorDialog::cancelFillPick()
{
    if (!m_fillActive && m_fillAX == INT_MIN)
        return;
    m_fillActive = false;
    m_fillAX = m_fillAY = m_fillAZ = INT_MIN;
    syncFillMarker();   // 只清金色角点描边，绿色选中保持
    updateSelectionLabel();
}

void ProjectionBlockEditorDialog::syncFillMarker()
{
    if (!m_view)
        return;
    QSet<int> marker;
    if (m_fillActive && m_fillAX != INT_MIN)
    {
        const int cell = cellIndexAt(m_fillAX, m_fillAY, m_fillAZ);
        if (cell >= 0)
            marker.insert(cell);
    }
    m_view->setHighlightCells(marker);
}

void ProjectionBlockEditorDialog::fillShapeByCoordinates(ProjectionFillShape shape, bool airOnly)
{
    if (!m_doc.isLoaded())
        return;

    const QString shapeName = ProjectionFillShapes::displayName(shape);
    bool ok = false;
    const QString text = AppInputDialog::getText(this, tr("填充方块 · %1").arg(shapeName),
        tr("请输入范围（含边界），两种写法都行：\n"
           "x0,y0,z0 → x1,y1,z1\n"
           "或直接写 6 个数字：x0 y0 z0 x1 y1 z1\n"
           "例如：0,60,0 → 10,80,10"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty())
        return;

    // 从任意写法里取出全部整数：够 6 个就认前三个 / 后三个，省得纠结分隔符
    QVector<int> numbers;
    const QRegularExpression re(QStringLiteral("-?\\d+"));
    auto it = re.globalMatch(text);
    while (it.hasNext())
        numbers.append(it.next().captured(0).toInt());
    if (numbers.size() != 6)
    {
        AppMessageBox::warning(this, tr("输入无效"),
            tr("需要正好 6 个坐标数字，例如：0,60,0 → 10,80,10"));
        return;
    }

    const QVector<int> written = applyFillShape(shape, airOnly,
        numbers.at(0), numbers.at(1), numbers.at(2),
        numbers.at(3), numbers.at(4), numbers.at(5));
    if (written.isEmpty())
    {
        AppMessageBox::information(this, tr("填充方块"),
            tr("这个范围里没有可填充的格子：可能已经都是当前方块，"
               "或勾选了「只填充空气」而位置都被占用了。"));
        return;
    }

    if (m_view)
    {
        if (written.size() <= 60000)
        {
            QSet<int> cells;
            cells.reserve(written.size());
            for (int cell : written)
                cells.insert(cell);
            m_view->setSelectedCells(cells);
        }
        else
        {
            m_view->clearSelection();
        }
    }
    updateSelectionLabel();
    if (m_selectLabel)
        m_selectLabel->setText(tr("已用「%1」填充 %2 个方块（绿色边框）")
            .arg(shapeName).arg(QLocale().toString(written.size())));
    AppMessageBox::information(this, tr("填充完成"),
        tr("已用「%1」填充 %2 个方块。").arg(shapeName).arg(QLocale().toString(written.size())));
}

QVector<int> ProjectionBlockEditorDialog::applyFillShape(ProjectionFillShape shape, bool airOnly,
                                                         int x0, int y0, int z0,
                                                         int x1, int y1, int z1)
{
    QVector<int> written;
    if (!m_doc.isLoaded())
        return written;

    const QString id = currentBlockId().trimmed();
    if (id.isEmpty())
        return written;
    const SchematicBlockState target = SchematicBlockState::fromString(id);

    // 世界坐标 → 本地坐标；范围可以超出投影，超出的部分直接忽略
    const int sx = m_doc.sizeX();
    const int sy = m_doc.sizeY();
    const int sz = m_doc.sizeZ();
    int lx0 = qMin(x0, x1) - m_doc.originX();
    int lx1 = qMax(x0, x1) - m_doc.originX();
    int ly0 = qMin(y0, y1) - m_doc.originY();
    int ly1 = qMax(y0, y1) - m_doc.originY();
    int lz0 = qMin(z0, z1) - m_doc.originZ();
    int lz1 = qMax(z0, z1) - m_doc.originZ();
    if (lx1 < 0 || ly1 < 0 || lz1 < 0 || lx0 >= sx || ly0 >= sy || lz0 >= sz)
        return written;   // 整个范围都在投影外面
    lx0 = qMax(0, lx0);
    ly0 = qMax(0, ly0);
    lz0 = qMax(0, lz0);
    lx1 = qMin(sx - 1, lx1);
    ly1 = qMin(sy - 1, ly1);
    lz1 = qMin(sz - 1, lz1);

    const int dx = lx1 - lx0 + 1;
    const int dy = ly1 - ly0 + 1;
    const int dz = lz1 - lz0 + 1;
    // 单次生成的格子数兜底：手输超大坐标时先拦下来，免得一次吃掉几百 MB 内存把视图卡死
    const qint64 boxVolume = static_cast<qint64>(dx) * dy * dz;
    if (boxVolume > 8000000LL)
    {
        AppMessageBox::warning(this, tr("填充方块"),
            tr("这个范围太大（%1 格），一次生成会占用大量内存并卡住视图。\n"
               "请把范围拆小一点，或改用「全部填充」。").arg(QLocale().toString(boxVolume)));
        return written;
    }
    const QVector<ProjectionFillShapes::Cell> offsets =
        ProjectionFillShapes::cells(shape, dx, dy, dz);
    if (offsets.isEmpty())
        return written;

    const int targetIndex = m_doc.ensurePaletteEntry(target);
    if (targetIndex < 0)
        return written;
    const int layer = m_doc.layerSize();
    const QList<int> beforeAll = m_doc.indices();
    written.reserve(offsets.size());
    for (const ProjectionFillShapes::Cell &offset : offsets)
    {
        const int cell = (ly0 + offset.y) * layer
                       + (lz0 + offset.z) * sx
                       + (lx0 + offset.x);
        if (cell < 0 || cell >= m_doc.volume())
            continue;
        const int before = m_doc.paletteIndexAt(cell);
        if (before == targetIndex)
            continue;   // 已经是这个方块，不算改动
        if (airOnly && !m_doc.paletteEntry(before).isAir())
            continue;   // 只填空格：已有方块保留
        if (!m_doc.setPaletteIndexAt(cell, targetIndex))
            continue;
        written.append(cell);
    }
    if (written.isEmpty())
        return written;

    m_dirty = true;
    updateTitle();
    rebuildView();
    onRefreshStats();
    recordBulkEdit(beforeAll, tr("填充·%1").arg(ProjectionFillShapes::displayName(shape)));
    return written;
}

void ProjectionBlockEditorDialog::onPickBlock()
{
    if (m_hoverX == INT_MIN)
    {
        AppMessageBox::information(this, tr("拾取"),
            tr("请先将鼠标悬停在 3D 视图中的某个方块上，再点击拾取。"));
        return;
    }
    SchematicBlockState st = m_doc.block(m_hoverX, m_hoverY, m_hoverZ);
    setCurrentBlockId(st.toString());
    m_view->setDirty();
}

void ProjectionBlockEditorDialog::closeEvent(QCloseEvent *e)
{
    if (m_dirty)
    {
        const auto ret = AppMessageBox::question(this, tr("未保存的修改"),
            tr("当前投影有未保存的修改，是否保存？"),
            AppMessageBox::Save | AppMessageBox::Discard | AppMessageBox::Cancel,
            AppMessageBox::Save);
        if (ret == AppMessageBox::Save)
            onSaveInPlace();
        else if (ret == AppMessageBox::Cancel)
        {
            e->ignore();
            return;
        }
    }
    QDialog::closeEvent(e);
}

void ProjectionBlockEditorDialog::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
    {
        // Esc 先放弃还没点完的填充（已生成的方块不动），再收起框选角点、取消选中
        if (m_fillActive)
        {
            cancelFillPick();
            return;
        }
        // Esc 优先收起还没点完的框选角点，再取消方块选取，都没有时才考虑关闭
        if (boxSelectPending())
        {
            clearBoxPivot();
            updateSelectionLabel();
            return;
        }
        if (m_view && !m_view->selectedCells().isEmpty())
        {
            clearSelection();
            return;
        }
        if (!m_dirty)
        {
            reject();
            return;
        }
    }
    QDialog::keyPressEvent(e);
}

// ====================================================================== 新版界面细节
void ProjectionBlockEditorDialog::applyEditorChromeExtras()
{
    // 在基础 chrome 之后追加新控件的样式，令牌统一走 StyleKit 解析
    setStyleSheet(styleSheet() + StyleKit::resolve(QStringLiteral(
        "QLabel#projTitle {"
        "  color: @TEXT_PRIMARY@;"
        "  font-size: 15px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#projSubtitle {"
        "  color: @TEXT_TERTIARY@;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#projChip {"
        "  color: @TEXT_SECONDARY@;"
        "  background-color: @BG_CONTENT@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 9px;"
        "  padding: 3px 9px;"
        "  font-size: 11px;"
        "}"
        "QLabel#projStatusLabel {"
        "  color: @TEXT_TERTIARY@;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QFrame#projVLine {"
        "  background-color: @BORDER@;"
        "  border: none;"
        "  max-width: 1px;"
        "}"
        "QPushButton#projToolSegBtn {"
        "  background-color: transparent;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid transparent;"
        "  border-radius: 8px;"
        "  padding: 5px 12px;"
        "  font-size: 12px;"
        "}"
        "QPushButton#projToolSegBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#projToolSegBtn:checked {"
        "  background-color: @PRIMARY_RGBA_20@;"
        "  border-color: @PRIMARY@;"
        "  color: @PRIMARY@;"
        "  font-weight: 600;"
        "}"
        "QPushButton#projMiniBtn {"
        "  background-color: @BG_CONTENT@;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 7px;"
        "  padding: 4px 4px;"
        "  font-size: 12px;"
        "}"
        "QPushButton#projMiniBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  border-color: @BORDER_STRONG@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#projMiniBtn:pressed {"
        "  background-color: @BG_PRESSED@;"
        "}"
        "QPushButton#projStepBtn {"
        "  background-color: transparent;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 6px;"
        "  font-size: 10px;"
        "  padding: 0;"
        "}"
        "QPushButton#projStepBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QSlider#projLayerSlider::groove:horizontal {"
        "  height: 4px;"
        "  background-color: @BG_CONTENT@;"
        "  border-radius: 2px;"
        "}"
        "QSlider#projLayerSlider::sub-page:horizontal {"
        "  background-color: @PRIMARY@;"
        "  border-radius: 2px;"
        "}"
        "QSlider#projLayerSlider::handle:horizontal {"
        "  width: 12px;"
        "  margin: -5px 0;"
        "  border-radius: 6px;"
        "  background-color: @PRIMARY@;"
        "}"
    )));
}

void ProjectionBlockEditorDialog::refreshToolButtonIcons()
{
    // 图标颜色直接取主题令牌：@TEXT_SECONDARY@ 在浅色/深色主题下都能看清，
    // @PRIMARY@ 用于"当前选中"的工具按钮，避免靠 QPalette 猜测导致图标近似隐形。
    const QColor tint = tokenColor("TEXT_SECONDARY", QColor(0x66, 0x66, 0x66));
    const QColor tintActive = tokenColor("PRIMARY", QColor(0x0d, 0x9d, 0x6a));
    const QColor tintDisabled = tokenColor("TEXT_TERTIARY", QColor(0x88, 0x88, 0x88));

    if (m_undoBtn)
        m_undoBtn->setIcon(makeGlyphIcon(Glyph::Undo, m_undoBtn->isEnabled() ? tint : tintDisabled, 15));
    if (m_redoBtn)
        m_redoBtn->setIcon(makeGlyphIcon(Glyph::Redo, m_redoBtn->isEnabled() ? tint : tintDisabled, 15));
    if (m_focusBtn)
        m_focusBtn->setIcon(makeGlyphIcon(Glyph::Focus, tint, 14));
    if (m_shotBtn)
        m_shotBtn->setIcon(makeGlyphIcon(Glyph::Camera, tint, 14));
    if (m_layerPrevBtn)
        m_layerPrevBtn->setIcon(makeGlyphIcon(Glyph::TriLeft, tint, 12));
    if (m_layerNextBtn)
        m_layerNextBtn->setIcon(makeGlyphIcon(Glyph::TriRight, tint, 12));

    for (QPushButton *btn : m_toolButtons)
    {
        if (!btn)
            continue;
        const int glyphIndex = btn->property("glyphIndex").toInt();
        const QColor c = btn->isChecked() ? tintActive : tint;
        btn->setIcon(makeGlyphIcon(static_cast<Glyph>(glyphIndex), c, 16));
    }
}

void ProjectionBlockEditorDialog::setupShortcuts()
{
    auto addShortcut = [this](const QKeySequence &sequence, const std::function<void()> &handler) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };

    addShortcut(QKeySequence(QKeySequence::Undo), [this]() { onUndo(); });
    addShortcut(QKeySequence(QKeySequence::Redo), [this]() { onRedo(); });
    addShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this]() { onRedo(); });
    addShortcut(QKeySequence(Qt::Key_1), [this]() { m_toolCombo->setCurrentIndex(0); });
    addShortcut(QKeySequence(Qt::Key_2), [this]() { m_toolCombo->setCurrentIndex(1); });
    addShortcut(QKeySequence(Qt::Key_F), [this]() { onFocusView(); });
    addShortcut(QKeySequence(Qt::Key_R), [this]() { if (m_view) m_view->resetView(); });
    addShortcut(QKeySequence(QKeySequence::Save), [this]() { onSaveInPlace(); });
    addShortcut(QKeySequence(QKeySequence::SaveAs), [this]() { onSaveAs(); });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), [this]() { onExportScreenshot(); });
}

void ProjectionBlockEditorDialog::onViewPreset(int presetValue)
{
    if (!m_view)
        return;
    m_view->applyViewPreset(static_cast<VoxelViewWidget::ViewPreset>(presetValue));
    m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionBlockEditorDialog::onFocusView()
{
    if (!m_view || !m_doc.isLoaded())
        return;

    const QSet<int> selected = m_view->selectedCells();
    if (!selected.isEmpty())
        m_view->focusOnCells(selected);
    else
        m_view->focusAll();
}

void ProjectionBlockEditorDialog::onExportScreenshot()
{
    if (!m_view)
        return;

    const QImage shot = m_view->grabFramebuffer();
    if (shot.isNull())
    {
        AppMessageBox::warning(this, tr("导出失败"),
                               tr("视图还没有渲染出内容，请稍候片刻后重试。"));
        return;
    }

    const QFileInfo info(m_filePath);
    const QString base = info.completeBaseName().isEmpty()
        ? QStringLiteral("projection") : info.completeBaseName();
    const QString suggested = QDir(info.absolutePath()).filePath(
        QStringLiteral("%1-%2.png").arg(base,
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));

    QString out = QFileDialog::getSaveFileName(this, tr("导出视图截图"), suggested,
                                                 tr("PNG 图片 (*.png)"));
    if (out.isEmpty())
        return;
    if (!out.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        out += QStringLiteral(".png");

    if (!shot.save(out, "PNG"))
    {
        AppMessageBox::warning(this, tr("导出失败"), tr("无法写入图片：\n%1").arg(out));
        return;
    }
    AppMessageBox::information(this, tr("已导出"), tr("截图已保存到：\n%1").arg(out));
}

void ProjectionBlockEditorDialog::onStepLayer(int delta)
{
    if (!m_layerSlider)
        return;
    // 0 = 全部，1..sizeY = 具体层；到头后回到「全部」，方便快速扫层
    int value = m_layerSlider->value() + delta;
    if (value < 0)
        value = m_layerSlider->maximum();
    else if (value > m_layerSlider->maximum())
        value = 0;
    m_layerSlider->setValue(value);
}

void ProjectionBlockEditorDialog::onPlaceAdjacentToggled(bool on)
{
    if (m_view)
        m_view->setPlaceAdjacent(on);
}

void ProjectionBlockEditorDialog::onGeometryRebuilt(int quads, int elapsedMs)
{
    m_lastQuadCount = quads;
    m_lastRebuildMs = elapsedMs;
    if (m_chipFaces)
        m_chipFaces->setText(tr("可见面 %1").arg(QLocale().toString(quads)));
    if (m_chipPerf)
        m_chipPerf->setText(elapsedMs >= 0 ? tr("重建 %1 ms").arg(elapsedMs)
                                          : tr("重建 —"));
}

void ProjectionBlockEditorDialog::updateHoverLabel(int x, int y, int z)
{
    m_hoverX = x;
    m_hoverY = y;
    m_hoverZ = z;
    if (!m_posLabel)
        return;

    if (x == INT_MIN)
    {
        m_posLabel->setText(tr("悬停：—"));
        return;
    }

    const SchematicBlockState state = m_doc.block(x, y, z);
    const QString cn = InstanceBlockNames::lookup(m_blockCnMap, state.name);
    const QString name = cn.isEmpty()
        ? friendlyBlockName(state.toString())
        : QStringLiteral("%1 (%2)").arg(cn, friendlyBlockName(state.name));
    m_posLabel->setText(tr("悬停：(%1, %2, %3) · %4").arg(x).arg(y).arg(z).arg(name));
}

void ProjectionBlockEditorDialog::refreshStatsChips()
{
    if (!m_doc.isLoaded())
    {
        if (m_chipSize) m_chipSize->setText(tr("尺寸 —"));
        if (m_chipFaces) m_chipFaces->setText(tr("可见面 —"));
        if (m_chipPerf) m_chipPerf->setText(tr("重建 —"));
        return;
    }
    if (m_chipSize)
        m_chipSize->setText(tr("%1 × %2 × %3")
            .arg(m_doc.sizeX()).arg(m_doc.sizeY()).arg(m_doc.sizeZ()));
}

int ProjectionBlockEditorDialog::cellIndexAt(int x, int y, int z) const
{
    if (!m_doc.isLoaded())
        return -1;
    const int lx = x - m_doc.originX();
    const int ly = y - m_doc.originY();
    const int lz = z - m_doc.originZ();
    if (lx < 0 || ly < 0 || lz < 0
        || lx >= m_doc.sizeX() || ly >= m_doc.sizeY() || lz >= m_doc.sizeZ())
        return -1;
    return ly * m_doc.layerSize() + lz * m_doc.sizeX() + lx;
}
