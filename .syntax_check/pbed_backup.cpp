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
#include "AppFileDialog.h"
#include "utils/Schematic/ProjectionEditorSettings.h"
#include "utils/Schematic/InstanceBlockNames.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>
#include <climits>

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
    "minecraft:oak_trapdoor[facing=north,half=bottom,open=false]",
    "minecraft:oak_trapdoor[facing=south,half=top,open=false]",
    "minecraft:oak_trapdoor[facing=east,half=bottom,open=true]",
    "minecraft:iron_trapdoor[facing=north,half=bottom,open=false]",
    "minecraft:dark_oak_trapdoor[facing=west,half=bottom,open=true]",
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
    resize(1180, 760);
    initUI();
}

ProjectionBlockEditorDialog::~ProjectionBlockEditorDialog()
{
}

void ProjectionBlockEditorDialog::initUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(12);

    // ---- 顶部工具栏 ----
    auto *toolbarCard = new QWidget(this);
    toolbarCard->setObjectName(QStringLiteral("projToolbarCard"));
    auto *toolbarWrap = new QVBoxLayout(toolbarCard);
    toolbarWrap->setContentsMargins(12, 10, 12, 10);
    toolbarWrap->setSpacing(8);
    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);

    m_toolCombo = new QComboBox(toolbarCard);
    m_toolCombo->setObjectName(QStringLiteral("projToolCombo"));
    m_toolCombo->setMinimumWidth(240);
    m_toolCombo->addItem(tr("查看（WASD 移动 / 空格升 / Shift 降 / 拖动转向）"), ToolOrbit);
    m_toolCombo->addItem(tr("放置（Ctrl+左键）"), ToolPlace);
    m_toolCombo->addItem(tr("破坏（右键）"), ToolBreak);
    m_toolCombo->addItem(tr("填充（选中的方块铺满）"), ToolFill);
    m_toolCombo->addItem(tr("选取（左键点击方块）"), ToolSelect);
    toolbar->addWidget(m_toolCombo);

    toolbar->addSpacing(6);

    m_undoBtn = new QPushButton(tr("撤销"), toolbarCard);
    m_redoBtn = new QPushButton(tr("重做"), toolbarCard);
    m_undoBtn->setObjectName(QStringLiteral("projToolBtn"));
    m_redoBtn->setObjectName(QStringLiteral("projToolBtn"));
    m_undoBtn->setToolTip(tr("撤销上一步修改（Ctrl+Z）"));
    m_redoBtn->setToolTip(tr("重做（Ctrl+Y）"));
    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);
    toolbar->addWidget(m_undoBtn);
    toolbar->addWidget(m_redoBtn);

    toolbar->addStretch();

    m_statsLabel = new QLabel(toolbarCard);
    m_statsLabel->setObjectName(QStringLiteral("projMutedLabel"));
    toolbar->addWidget(m_statsLabel);

    toolbarWrap->addLayout(toolbar);

    // ---- 选取范围（切换：只选这一个方块 / 选中全部同类型方块）----
    auto *selectRow = new QHBoxLayout();
    selectRow->setSpacing(8);
    auto *selectCaption = new QLabel(tr("选取范围"), toolbarCard);
    selectCaption->setObjectName(QStringLiteral("projMutedLabel"));
    m_selectScopeCombo = new QComboBox(toolbarCard);
    m_selectScopeCombo->setObjectName(QStringLiteral("projToolCombo"));
    m_selectScopeCombo->setMinimumWidth(170);
    m_selectScopeCombo->addItem(tr("选取这一个方块"), 0);
    m_selectScopeCombo->addItem(tr("选取所有相同方块"), 1);
    m_selectScopeCombo->setToolTip(tr("工具切到「选取」后，左键单击方块：\n"
                                      "· 选取这一个方块：只选中点到的这一格\n"
                                      "· 选取所有相同方块：选中投影中所有同种类方块（忽略朝向等状态）"));
    m_selectLabel = new QLabel(toolbarCard);
    m_selectLabel->setObjectName(QStringLiteral("projMutedLabel"));
    m_clearSelBtn = new QPushButton(tr("清除选择"), toolbarCard);
    m_clearSelBtn->setObjectName(QStringLiteral("projToolBtn"));
    m_clearSelBtn->setToolTip(tr("取消当前选中的方块（Esc 也可清除）"));
    m_clearSelBtn->setEnabled(false);
    selectRow->addWidget(selectCaption);
    selectRow->addWidget(m_selectScopeCombo);
    selectRow->addWidget(m_selectLabel);
    selectRow->addStretch();
    selectRow->addWidget(m_clearSelBtn);
    toolbarWrap->addLayout(selectRow);

    root->addWidget(toolbarCard);

    // ---- 主区域 ----
    auto *mainLayout = new QHBoxLayout();
    mainLayout->setSpacing(12);

    // 左侧：3D 视图卡片
    auto *viewCard = new QWidget(this);
    viewCard->setObjectName(QStringLiteral("projViewCard"));
    auto *viewWrap = new QVBoxLayout(viewCard);
    viewWrap->setContentsMargins(10, 10, 10, 10);
    viewWrap->setSpacing(8);
    m_view = new VoxelViewWidget(viewCard);
    m_view->setMinimumSize(560, 420);
    m_view->setObjectName(QStringLiteral("projVoxelView"));
    viewWrap->addWidget(m_view, 1);

    // 图层过滤条
    auto *layerBar = new QHBoxLayout();
    layerBar->setSpacing(8);
    auto *layerCaption = new QLabel(tr("图层 Y"), viewCard);
    layerCaption->setObjectName(QStringLiteral("projMutedLabel"));
    m_layerSlider = new QSlider(Qt::Horizontal, viewCard);
    m_layerSlider->setObjectName(QStringLiteral("projLayerSlider"));
    m_layerSlider->setRange(0, 0);
    m_layerSlider->setValue(0);
    m_layerLabel = new QLabel(tr("全部"), viewCard);
    m_layerLabel->setObjectName(QStringLiteral("projLayerLabel"));
    m_layerLabel->setMinimumWidth(56);
    m_belowToggle = new QToolButton(viewCard);
    m_belowToggle->setObjectName(QStringLiteral("projToggleBtn"));
    m_belowToggle->setCheckable(true);
    m_belowToggle->setText(tr("此层及以下"));
    m_belowToggle->setToolTip(tr("勾选后，选中某层时同时显示该层以下的方块（以下层变暗），便于查看下方结构"));
    m_belowToggle->setCursor(Qt::PointingHandCursor);
    layerBar->addWidget(layerCaption);
    layerBar->addWidget(m_layerSlider, 1);
    layerBar->addWidget(m_layerLabel);
    layerBar->addWidget(m_belowToggle);
    viewWrap->addLayout(layerBar);

    m_posLabel = new QLabel(tr("悬停：— · WASD 移动 · 空格升 · Shift 降 · 拖动转向 · 滚轮前后"), viewCard);
    m_posLabel->setObjectName(QStringLiteral("projMutedLabel"));
    viewWrap->addWidget(m_posLabel);

    mainLayout->addWidget(viewCard, 1);

    // 右侧：调色板 + 当前方块 + 批量操作（独立侧栏卡片）
    auto *sideCard = new QWidget(this);
    sideCard->setObjectName(QStringLiteral("projSideCard"));
    sideCard->setFixedWidth(310);
    auto *side = new QVBoxLayout(sideCard);
    side->setContentsMargins(14, 14, 14, 14);
    side->setSpacing(8);

    auto *currentCaption = new QLabel(tr("当前方块"), sideCard);
    currentCaption->setObjectName(QStringLiteral("projSectionTitle"));
    side->addWidget(currentCaption);

    auto *curRow = new QHBoxLayout();
    curRow->setSpacing(6);
    m_currentBlockCombo = new QComboBox(sideCard);
    m_currentBlockCombo->setObjectName(QStringLiteral("projBlockCombo"));
    m_currentBlockCombo->setEditable(true);
    // 选项以中文名显示（如「橡木活板门（北面·下半·关闭）」），内部仍保存完整方块 ID
    refreshCurrentBlockOptions();
    curRow->addWidget(m_currentBlockCombo, 1);
    m_pickBtn = new QPushButton(tr("拾取"), sideCard);
    m_pickBtn->setObjectName(QStringLiteral("projToolBtn"));
    m_pickBtn->setToolTip(tr("把鼠标指向的方块设为当前方块"));
    curRow->addWidget(m_pickBtn);
    side->addLayout(curRow);

    m_airBtn = new QToolButton(sideCard);
    m_airBtn->setObjectName(QStringLiteral("projToggleBtn"));
    m_airBtn->setCheckable(true);
    m_airBtn->setText(tr("显示空气"));
    m_airBtn->setToolTip(tr("显示/隐藏空气方块"));
    m_airBtn->setCursor(Qt::PointingHandCursor);
    side->addWidget(m_airBtn, 0, Qt::AlignLeft);

    auto *paletteCaption = new QLabel(tr("方块调色板"), sideCard);
    paletteCaption->setObjectName(QStringLiteral("projSectionTitle"));
    paletteCaption->setToolTip(tr("双击列表项即可设为当前方块"));
    side->addWidget(paletteCaption);

    m_paletteFilter = new QLineEdit(sideCard);
    m_paletteFilter->setObjectName(QStringLiteral("projSearchEdit"));
    m_paletteFilter->setPlaceholderText(tr("搜索方块名称或状态…"));
    m_paletteFilter->setClearButtonEnabled(true);
    side->addWidget(m_paletteFilter);

    m_paletteList = new QListWidget(sideCard);
    m_paletteList->setObjectName(QStringLiteral("projPaletteList"));
    m_paletteList->setMaximumWidth(300);
    m_paletteList->setMinimumWidth(250);
    m_paletteList->setUniformItemSizes(true);
    m_paletteList->setAlternatingRowColors(true);
    m_paletteList->setIconSize(QSize(20, 20));
    side->addWidget(m_paletteList, 1);

    auto *batchCaption = new QLabel(tr("批量操作"), sideCard);
    batchCaption->setObjectName(QStringLiteral("projSectionTitle"));
    side->addWidget(batchCaption);

    auto *batchRow1 = new QHBoxLayout();
    batchRow1->setSpacing(6);
    auto *replaceBtn = new QPushButton(tr("替换方块…"), sideCard);
    auto *fillBoxBtn  = new QPushButton(tr("区域填充…"), sideCard);
    replaceBtn->setObjectName(QStringLiteral("projToolBtn"));
    fillBoxBtn->setObjectName(QStringLiteral("projToolBtn"));
    batchRow1->addWidget(replaceBtn, 1);
    batchRow1->addWidget(fillBoxBtn, 1);
    side->addLayout(batchRow1);

    auto *fillAllBtn = new QPushButton(tr("全空间填充"), sideCard);
    fillAllBtn->setObjectName(QStringLiteral("projToolBtn"));
    fillAllBtn->setToolTip(tr("将整个投影包围盒填充为当前方块"));
    side->addWidget(fillAllBtn);

    mainLayout->addWidget(sideCard);

    root->addLayout(mainLayout, 1);

    // ---- 底部按钮 ----
    auto *bottom = new QHBoxLayout();
    bottom->setSpacing(8);
    auto *closeBtn = new QPushButton(tr("关闭"), this);
    m_saveAsBtn = new QPushButton(tr("另存为…"), this);
    m_saveBtn = new QPushButton(tr("保存修改"), this);
    closeBtn->setObjectName(QStringLiteral("projDialogBtn"));
    m_saveAsBtn->setObjectName(QStringLiteral("projDialogBtn"));
    m_saveBtn->setObjectName(QStringLiteral("projDialogBtnPrimary"));
    m_saveBtn->setDefault(true);
    bottom->addWidget(closeBtn);
    bottom->addStretch();
    bottom->addWidget(m_saveAsBtn);
    bottom->addWidget(m_saveBtn);
    root->addLayout(bottom);

    applyEditorChrome();

    // ---- 连接 ----
    connect(m_toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectionBlockEditorDialog::onToolChanged);
    connect(m_undoBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onUndo);
    connect(m_redoBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onRedo);
    connect(m_view, &VoxelViewWidget::placeRequested, this, &ProjectionBlockEditorDialog::onPlace);
    connect(m_view, &VoxelViewWidget::breakRequested, this, &ProjectionBlockEditorDialog::onBreak);
    connect(m_view, &VoxelViewWidget::selectRequested, this, &ProjectionBlockEditorDialog::onSelect);
    connect(m_clearSelBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onClearSelection);
    // 切换选取范围后，按新范围重新选中上次点到的方块
    connect(m_selectScopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (m_selAnchorX == INT_MIN)
            return;
        m_view->setSelectedCells(collectSelection(m_selAnchorX, m_selAnchorY, m_selAnchorZ));
        updateSelectionLabel();
    });
    connect(m_view, &VoxelViewWidget::hoverChanged, this, [this](int x, int y, int z) {
        m_hoverX = x; m_hoverY = y; m_hoverZ = z;
        if (x == INT_MIN)
            m_posLabel->setText(tr("悬停：—"));
        else
        {
            SchematicBlockState st = m_doc.block(x, y, z);
            m_posLabel->setText(tr("悬停：(%1, %2, %3)  %4").arg(x).arg(y).arg(z).arg(st.toString()));
        }
    });
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
    });
    connect(m_airBtn, &QToolButton::toggled, this, &ProjectionBlockEditorDialog::onShowAirToggled);
    connect(m_layerSlider, &QSlider::valueChanged, this, &ProjectionBlockEditorDialog::onFillLayerChanged);
    // 「此层及以下」开关变化时重算显示
    connect(m_belowToggle, &QToolButton::toggled, this, [this](bool) {
        if (m_doc.isLoaded())
            onFillLayerChanged(m_layerSlider->value());
    });
    connect(m_saveAsBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onSaveAs);
    connect(m_saveBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onSaveInPlace);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(replaceBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onReplaceAll);
    connect(fillAllBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onFillAll);
    connect(fillBoxBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onFillBox);
    connect(m_paletteFilter, &QLineEdit::textChanged, this, &ProjectionBlockEditorDialog::filterPalette);

    updateSelectionLabel();
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
    m_view->setLayerFilter(-1);
    m_view->setLayerMode(VoxelViewWidget::LayerMode::All);
    if (m_belowToggle)
        m_belowToggle->setChecked(false);

    loadBlockRegistry();
    populatePalette();
    // 材质/形状挂上时各自会 rebuild 一次；这里先不 setDocument，等设置应用完再一次性建几何
    applyTextureAndShapes();
    applyEditorSettings();
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
        if (m_texLoader)
        {
            const QIcon icon = m_texLoader->blockIcon(state.name, 20);
            if (!icon.isNull())
                item->setIcon(icon);
        }
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
        if (m_texLoader)
        {
            const QIcon icon = m_texLoader->blockIcon(st.name, 20);
            if (!icon.isNull())
                item->setIcon(icon);
        }
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
        setCurrentBlockId(keep);
    else if (m_currentBlockCombo->count() > 0)
        m_currentBlockCombo->setCurrentIndex(0);

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

void ProjectionBlockEditorDialog::onToolChanged(int index)
{
    const int tool = m_toolCombo->itemData(index).toInt();
    m_tool = tool;
    // 「查看」和「选取」工具下都可以用左键单击选方块：查看工具是默认工具，
    // 这样顶部的「选取范围」不改工具也能立刻生效，不必先切工具
    m_view->setSelectTool(tool == ToolSelect || tool == ToolOrbit);
    if (tool != ToolSelect && tool != ToolOrbit)
        m_view->setCursor(Qt::ArrowCursor);
    updateSelectionLabel();
    m_view->setDirty();
}

void ProjectionBlockEditorDialog::onPlace(int x, int y, int z)
{
    pushUndo();
    SchematicBlockState st = SchematicBlockState::fromString(currentBlockId());
    QString err;
    m_doc.setBlock(x, y, z, st, &err);
    m_dirty = true;
    updateTitle();
    rebuildView();
    onRefreshStats();
}

void ProjectionBlockEditorDialog::onBreak(int x, int y, int z)
{
    pushUndo();
    QString err;
    m_doc.setBlock(x, y, z, SchematicBlockState{ QStringLiteral("minecraft:air"), {} }, &err);
    m_dirty = true;
    updateTitle();
    rebuildView();
    onRefreshStats();
}

void ProjectionBlockEditorDialog::onSelect(int x, int y, int z)
{
    if (!m_doc.isLoaded())
        return;
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

void ProjectionBlockEditorDialog::updateSelectionLabel()
{
    if (!m_selectLabel)
        return;
    const int n = m_view ? m_view->selectedCells().size() : 0;
    if (n > 0)
        m_selectLabel->setText(tr("已选中 %1 个方块（绿色边框）").arg(n));
    else if (m_tool == ToolSelect || m_tool == ToolOrbit)
        m_selectLabel->setText(tr("左键单击方块即可选中"));
    else
        m_selectLabel->setText(tr("切到「查看」或「选取」工具后左键单击方块即可选中"));
    if (m_clearSelBtn)
        m_clearSelBtn->setEnabled(n > 0);
}

void ProjectionBlockEditorDialog::clearSelection()
{
    m_selAnchorX = INT_MIN;
    m_selAnchorY = INT_MIN;
    m_selAnchorZ = INT_MIN;
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
        m_view->setLayerFilter(-1);
        m_view->setLayerMode(VoxelViewWidget::LayerMode::All);
    }
    else
    {
        m_layerLabel->setText(tr("Y层 %1").arg(value));
        m_view->setLayerFilter(value - 1);
        // 勾选「此层及以下」→ 显示当前层及以下（下方变暗）；否则仅显示当前层
        m_view->setLayerMode(m_belowToggle && m_belowToggle->isChecked()
            ? VoxelViewWidget::LayerMode::Below
            : VoxelViewWidget::LayerMode::Single);
    }
    m_view->setDirty();
}

void ProjectionBlockEditorDialog::onShowAirToggled(bool checked)
{
    m_view->setShowAir(checked);
}

void ProjectionBlockEditorDialog::pushUndo()
{
    if (m_redoStack.size() > 64)
        m_redoStack.pop_front();

    // 保存到临时文件作为快照
    QTemporaryDir dir;
    if (dir.isValid())
    {
        const QString snap = dir.filePath(QStringLiteral("snap.litematic"));
        m_doc.saveCopy(snap);
        m_undoStack.append(snap);
        if (m_undoStack.size() > 64)
            m_undoStack.pop_front();
        m_redoStack.clear();
    }
    updateUndoRedo();
}

void ProjectionBlockEditorDialog::onUndo()
{
    if (m_undoStack.isEmpty())
        return;
    // 当前状态存入 redo
    QTemporaryDir dir;
    if (dir.isValid())
    {
        const QString cur = dir.filePath("cur.litematic");
        m_doc.saveCopy(cur);
        m_redoStack.append(cur);
        if (m_redoStack.size() > 64)
            m_redoStack.pop_front();
    }
    const QString snap = m_undoStack.takeLast();
    m_doc.load(snap);
    m_dirty = true;
    updateTitle();
    rebuildView();
    clearSelection();
    onRefreshStats();
    updateUndoRedo();
}

void ProjectionBlockEditorDialog::onRedo()
{
    if (m_redoStack.isEmpty())
        return;
    QTemporaryDir dir;
    if (dir.isValid())
    {
        const QString cur = dir.filePath("cur.litematic");
        m_doc.saveCopy(cur);
        m_undoStack.append(cur);
        if (m_undoStack.size() > 64)
            m_undoStack.pop_front();
    }
    const QString snap = m_redoStack.takeLast();
    m_doc.load(snap);
    m_dirty = true;
    updateTitle();
    rebuildView();
    clearSelection();
    onRefreshStats();
    updateUndoRedo();
}

void ProjectionBlockEditorDialog::updateUndoRedo()
{
    if (m_undoBtn) m_undoBtn->setEnabled(!m_undoStack.isEmpty());
    if (m_redoBtn) m_redoBtn->setEnabled(!m_redoStack.isEmpty());
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

    // 默认工具（查看/放置/破坏/填充）：按工具名映射到编辑器下拉项
    const QString tool = ProjectionEditorSettings::value(
        QStringLiteral("defaultTool"),
        ProjectionEditorSettings::defaultTool());
    int toolIndex = 0;
    if (tool == QLatin1String("放置"))
        toolIndex = 1;
    else if (tool == QLatin1String("破坏"))
        toolIndex = 2;
    else if (tool == QLatin1String("填充"))
        toolIndex = 3;
    m_toolCombo->setCurrentIndex(toolIndex);
    m_tool = m_toolCombo->itemData(toolIndex).toInt();
    // setCurrentIndex 在索引没变化时不会发信号，这里显式同步一次「左键单击可选取」状态，
    // 否则默认就是「查看」工具时，打开编辑器后点方块不会有任何反应
    if (m_view)
        m_view->setSelectTool(m_tool == ToolSelect || m_tool == ToolOrbit);

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
}

void ProjectionBlockEditorDialog::onRefreshStats()
{
    if (!m_doc.isLoaded())
    {
        if (m_statsLabel) m_statsLabel->setText(tr("未加载"));
        return;
    }
    if (m_statsLabel) m_statsLabel->setText(buildStatsText());
}

QString ProjectionBlockEditorDialog::buildStatsText() const
{
    if (!m_doc.isLoaded())
        return QString();
    int nonAir = 0;
    for (int i = 0; i < m_doc.volume(); ++i)
        if (!m_doc.blockAt(i).isAir())
            ++nonAir;
    const int paletteSize = m_doc.palette(false).size();
    return tr("尺寸 %1×%2×%3 · 方块 %4 · 调色板 %5 种状态")
        .arg(m_doc.sizeX()).arg(m_doc.sizeY()).arg(m_doc.sizeZ())
        .arg(nonAir).arg(paletteSize);
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
    setWindowTitle(tr("投影方块编辑器 - %1%2")
                   .arg(displayName(), m_dirty ? tr(" *") : QString()));
}

void ProjectionBlockEditorDialog::onSaveAs()
{
    if (!m_doc.isLoaded())
        return;
    const QFileInfo fi(m_filePath);
    const QString out = AppFileDialog::getSaveFileName(
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
    pushUndo();
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
    AppMessageBox::information(this, tr("替换完成"), tr("已将 %1 个方块替换。").arg(n));
}

void ProjectionBlockEditorDialog::onFillAll()
{
    if (!m_doc.isLoaded())
        return;
    const QString sel = currentBlockId();
    pushUndo();
    SchematicBlockState st = SchematicBlockState::fromString(sel);
    const int n = m_doc.fillAll(st);
    m_dirty = true;
    updateTitle();
    rebuildView();
    clearSelection();
    onRefreshStats();
    AppMessageBox::information(this, tr("填充完成"), tr("已填充 %1 个格子。").arg(n));
}

void ProjectionBlockEditorDialog::onFillBox()
{
    if (!m_doc.isLoaded())
        return;
    bool ok2 = false;
    const QString text = AppInputDialog::getText(this, tr("区域填充"),
        tr("请输入区域范围（含边界）：\n格式 x0,y0,z0 → x1,y1,z1\n例如：0,0,0 → 10,20,30"),
        QLineEdit::Normal, QString(), &ok2);
    if (!ok2 || text.trimmed().isEmpty())
        return;

    // 解析
    const QString cleaned = text.trimmed();
    const QStringList halves = cleaned.split(QStringLiteral("→"));
    if (halves.size() != 2)
    {
        AppMessageBox::warning(this, tr("输入无效"), tr("请使用 x0,y0,z0 → x1,y1,z1 格式。"));
        return;
    }
    auto parse3 = [](const QString &s, int &a, int &b, int &c) -> bool {
        const QStringList p = s.split(QLatin1Char(','));
        if (p.size() != 3)
            return false;
        bool o1,o2,o3;
        a = p[0].trimmed().toInt(&o1);
        b = p[1].trimmed().toInt(&o2);
        c = p[2].trimmed().toInt(&o3);
        return o1 && o2 && o3;
    };
    int x0,y0,z0,x1,y1,z1;
    if (!parse3(halves[0].trimmed(), x0,y0,z0) || !parse3(halves[1].trimmed(), x1,y1,z1))
    {
        AppMessageBox::warning(this, tr("输入无效"), tr("无法解析坐标。"));
        return;
    }

    const QString sel = currentBlockId();
    pushUndo();
    SchematicBlockState st = SchematicBlockState::fromString(sel);
    QString err;
    const int n = m_doc.fillBox(x0,y0,z0,x1,y1,z1, st, &err);
    m_dirty = true;
    updateTitle();
    rebuildView();
    clearSelection();
    onRefreshStats();
    AppMessageBox::information(this, tr("填充完成"),
                               tr("已填充 %1 个格子。%2").arg(n).arg(err.isEmpty()?QString():QStringLiteral("\n")+err));
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
        // Esc 优先取消方块选取，没有选中时才考虑关闭
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
