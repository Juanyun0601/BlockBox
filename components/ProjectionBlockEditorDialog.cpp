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
#include "utils/plugin/PluginManager.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSet>
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
    QString baseId = fullId;
    const int bracket = baseId.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        baseId = baseId.left(bracket);
    return cnMap.value(baseId.toLower());
}

QString friendlyBlockStateLabel(const SchematicBlockState &state,
                                const QHash<QString, QString> &cnMap)
{
    const QString cnName = lookupChineseName(cnMap, state.name);
    const QString engName = friendlyBlockName(state.name);
    if (state.properties.isEmpty())
    {
        if (cnName.isEmpty())
            return engName;
        return QStringLiteral("%1 (%2)").arg(cnName, engName);
    }
    QStringList parts;
    static const QMap<QString, QString> kKeyLabels = {
        {QStringLiteral("facing"),    QObject::tr("朝向")},
        {QStringLiteral("axis"),      QObject::tr("轴")},
        {QStringLiteral("half"),      QObject::tr("半砖")},
        {QStringLiteral("open"),      QObject::tr("开启")},
        {QStringLiteral("type"),      QObject::tr("类型")},
        {QStringLiteral("shape"),     QObject::tr("形状")},
        {QStringLiteral("waterlogged"), QObject::tr("含水")},
        {QStringLiteral("hinge"),     QObject::tr("铰链")},
        {QStringLiteral("part"),      QObject::tr("部件")},
        {QStringLiteral("delay"),     QObject::tr("延迟")},
        {QStringLiteral("lit"),       QObject::tr("点亮")},
        {QStringLiteral("powered"),   QObject::tr("通电")},
        {QStringLiteral("mode"),      QObject::tr("模式")},
        {QStringLiteral("age"),       QObject::tr("生长")},
        {QStringLiteral("level"),     QObject::tr("等级")},
        {QStringLiteral("layers"),    QObject::tr("层数")},
        {QStringLiteral("snowy"),     QObject::tr("积雪")},
        {QStringLiteral("down"),      QObject::tr("向下")},
        {QStringLiteral("up"),        QObject::tr("向上")},
        {QStringLiteral("north"),     QObject::tr("北")},
        {QStringLiteral("south"),     QObject::tr("南")},
        {QStringLiteral("east"),      QObject::tr("东")},
        {QStringLiteral("west"),      QObject::tr("西")},
        {QStringLiteral("rotation"),  QObject::tr("旋转")},
        {QStringLiteral("conditional"), QObject::tr("条件")},
    };
    for (auto it = state.properties.constBegin(); it != state.properties.constEnd(); ++it)
    {
        const QString label = kKeyLabels.value(it.key(), it.key());
        parts.append(QStringLiteral("%1=%2").arg(label, it.value()));
    }
    const QString propStr = QStringLiteral("[%1]").arg(parts.join(QLatin1String(", ")));
    if (cnName.isEmpty())
        return QStringLiteral("%1 %2").arg(engName, propStr);
    return QStringLiteral("%1 %2 (%3)").arg(cnName, propStr, engName);
}

QString blockStateTooltip(const SchematicBlockState &state)
{
    return state.toString();
}

} // namespace

bool ProjectionBlockEditorDialog::editProjection(QWidget *parent, const QString &filePath,
                                                  const QString &instancePath)
{
    ProjectionBlockEditorDialog dlg(parent);
    if (!instancePath.isEmpty())
        dlg.setTextureSource(instancePath);
    if (!dlg.loadProjection(filePath))
        return false;
    dlg.exec();
    return dlg.wasSaved(); // 有保存动作即视为已修改，供调用方刷新列表
}

ProjectionBlockEditorDialog::ProjectionBlockEditorDialog(QWidget *parent)
    : QDialog(parent)
{
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
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // ---- 顶部工具栏 ----
    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);

    m_toolCombo = new QComboBox(this);
    m_toolCombo->addItem(tr("查看（拖动旋转 / 滚轮缩放）"), ToolOrbit);
    m_toolCombo->addItem(tr("放置（Ctrl+左键）"), ToolPlace);
    m_toolCombo->addItem(tr("破坏（右键）"), ToolBreak);
    m_toolCombo->addItem(tr("填充（选中的方块铺满）"), ToolFill);
    toolbar->addWidget(m_toolCombo);

    toolbar->addSpacing(8);

    m_undoBtn = new QPushButton(tr("撤销"), this);
    m_redoBtn = new QPushButton(tr("重做"), this);
    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);
    toolbar->addWidget(m_undoBtn);
    toolbar->addWidget(m_redoBtn);

    toolbar->addStretch();

    m_statsLabel = new QLabel(this);
    m_statsLabel->setStyleSheet(QStringLiteral("color:#888;"));
    toolbar->addWidget(m_statsLabel);

    root->addLayout(toolbar);

    // ---- 主区域 ----
    auto *mainLayout = new QHBoxLayout();
    mainLayout->setSpacing(10);

    // 左侧：3D 视图
    auto *viewWrap = new QVBoxLayout();
    m_view = new VoxelViewWidget(this);
    m_view->setMinimumSize(560, 420);
    viewWrap->addWidget(m_view, 1);

    // 图层过滤条
    auto *layerBar = new QHBoxLayout();
    auto *layerCaption = new QLabel(tr("图层 Y:"), this);
    m_layerSlider = new QSlider(Qt::Horizontal, this);
    m_layerSlider->setRange(0, 0);
    m_layerSlider->setValue(0);
    m_layerLabel = new QLabel(tr("全部"), this);
    m_layerLabel->setMinimumWidth(48);
    m_belowToggle = new QToolButton(this);
    m_belowToggle->setCheckable(true);
    m_belowToggle->setText(tr("此层及以下"));
    m_belowToggle->setToolTip(tr("勾选后，选中某层时同时显示该层以下的方块（以下层变暗），便于查看下方结构"));
    m_belowToggle->setCursor(Qt::PointingHandCursor);
    layerBar->addWidget(layerCaption);
    layerBar->addWidget(m_layerSlider, 1);
    layerBar->addWidget(m_layerLabel);
    layerBar->addWidget(m_belowToggle);
    viewWrap->addLayout(layerBar);

    m_posLabel = new QLabel(tr("悬停: —"), this);
    m_posLabel->setStyleSheet(QStringLiteral("color:#888;"));
    viewWrap->addWidget(m_posLabel);

    mainLayout->addLayout(viewWrap, 1);

    // 右侧：调色板 + 当前方块 + 批量操作
    auto *side = new QVBoxLayout();
    side->setSpacing(8);

    auto *currentCaption = new QLabel(tr("当前方块"), this);
    currentCaption->setStyleSheet(QStringLiteral("font-weight:bold;"));
    side->addWidget(currentCaption);

    auto *curRow = new QHBoxLayout();
    m_currentBlockCombo = new QComboBox(this);
    m_currentBlockCombo->setEditable(true);
    m_currentBlockCombo->addItem(tr("minecraft:air"));
    // 常用活板门预设（带完整状态，放置后即可按真实形状渲染）
    m_currentBlockCombo->addItem(tr("minecraft:oak_trapdoor[facing=north,half=bottom,open=false]"));
    m_currentBlockCombo->addItem(tr("minecraft:oak_trapdoor[facing=south,half=top,open=false]"));
    m_currentBlockCombo->addItem(tr("minecraft:oak_trapdoor[facing=east,half=bottom,open=true]"));
    m_currentBlockCombo->addItem(tr("minecraft:iron_trapdoor[facing=north,half=bottom,open=false]"));
    m_currentBlockCombo->addItem(tr("minecraft:dark_oak_trapdoor[facing=west,half=bottom,open=true]"));
    curRow->addWidget(m_currentBlockCombo, 1);
    m_pickBtn = new QPushButton(tr("拾取"), this);
    m_pickBtn->setToolTip(tr("把鼠标指向的方块设为当前方块"));
    curRow->addWidget(m_pickBtn);
    side->addLayout(curRow);

    m_airBtn = new QToolButton(this);
    m_airBtn->setCheckable(true);
    m_airBtn->setText(tr("显示空气"));
    side->addWidget(m_airBtn);

    auto *paletteCaption = new QLabel(tr("方块调色板（双击设为当前）"), this);
    paletteCaption->setStyleSheet(QStringLiteral("font-weight:bold;"));
    side->addWidget(paletteCaption);

    m_paletteFilter = new QLineEdit(this);
    m_paletteFilter->setPlaceholderText(tr("搜索方块…"));
    m_paletteFilter->setClearButtonEnabled(true);
    side->addWidget(m_paletteFilter);

    m_paletteList = new QListWidget(this);
    m_paletteList->setMaximumWidth(300);
    m_paletteList->setMinimumWidth(250);
    side->addWidget(m_paletteList, 1);

    auto *batchCaption = new QLabel(tr("批量操作"), this);
    batchCaption->setStyleSheet(QStringLiteral("font-weight:bold;"));
    side->addWidget(batchCaption);

    auto *replaceBtn = new QPushButton(tr("替换方块…"), this);
    auto *fillAllBtn  = new QPushButton(tr("全空间填充"), this);
    auto *fillBoxBtn  = new QPushButton(tr("区域填充…"), this);
    side->addWidget(replaceBtn);
    side->addWidget(fillAllBtn);
    side->addWidget(fillBoxBtn);

    mainLayout->addLayout(side);

    root->addLayout(mainLayout, 1);

    // ---- 底部按钮 ----
    auto *bottom = new QHBoxLayout();
    m_saveAsBtn = new QPushButton(tr("另存为…"), this);
    m_saveBtn = new QPushButton(tr("保存修改"), this);
    m_saveBtn->setStyleSheet(QStringLiteral("font-weight:bold;"));
    auto *closeBtn = new QPushButton(tr("关闭"), this);
    bottom->addWidget(m_saveAsBtn);
    bottom->addWidget(m_saveBtn);
    bottom->addStretch();
    bottom->addWidget(closeBtn);
    root->addLayout(bottom);

    // ---- 连接 ----
    connect(m_toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectionBlockEditorDialog::onToolChanged);
    connect(m_undoBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onUndo);
    connect(m_redoBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onRedo);
    connect(m_view, &VoxelViewWidget::placeRequested, this, &ProjectionBlockEditorDialog::onPlace);
    connect(m_view, &VoxelViewWidget::breakRequested, this, &ProjectionBlockEditorDialog::onBreak);
    connect(m_view, &VoxelViewWidget::hoverChanged, this, [this](int x, int y, int z) {
        m_hoverX = x; m_hoverY = y; m_hoverZ = z;
        if (x == INT_MIN)
            m_posLabel->setText(tr("悬停: —"));
        else
        {
            SchematicBlockState st = m_doc.block(x, y, z);
            m_posLabel->setText(tr("悬停: (%1,%2,%3) %4").arg(x).arg(y).arg(z).arg(st.toString()));
        }
    });
    connect(m_paletteList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (!it)
            return;
        const int idx = it->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_blocks.size())
        {
            m_currentBlockCombo->setCurrentText(m_blocks.at(idx).toString());
            m_view->setDirty();
        }
    });
    connect(m_pickBtn, &QPushButton::clicked, this, &ProjectionBlockEditorDialog::onPickBlock);
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
}

bool ProjectionBlockEditorDialog::loadProjection(const QString &filePath)
{
    if (!m_doc.load(filePath))
    {
        AppMessageBox::warning(this, tr("打开失败"),
                               tr("无法解析投影文件：\n%1\n\n仅支持 .litematic 格式。").arg(filePath));
        return false;
    }
    m_filePath = filePath;
    const QFileInfo fi(filePath);
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
    m_view->setDocument(&m_doc);
    m_view->resetView();
    applyPluginSettings();
    updateTitle();
    onRefreshStats();
    rebuildView();
    return true;
}

void ProjectionBlockEditorDialog::setTextureSource(const QString &instancePath)
{
    m_instancePath = instancePath;
    if (instancePath.isEmpty())
    {
        m_texLoader = InstanceTextureLoader();
        m_shapeLoader = BlockShapeLoader();
        m_view->setTextureLoader(nullptr);
        m_view->setShapeLoader(nullptr);
        m_blockCnMap.clear();
        if (m_doc.isLoaded())
            populatePalette();
        rebuildView();
        return;
    }
    QString err;
    if (m_texLoader.load(instancePath, &err))
    {
        m_view->setTextureLoader(&m_texLoader);
    }
    else
    {
        m_texLoader = InstanceTextureLoader();
        m_view->setTextureLoader(nullptr);
    }
    // 同一实例 jar 里解析方块真实形状（活板门等非完整方块）
    QString shapeErr;
    if (m_shapeLoader.load(instancePath, &shapeErr))
        m_view->setShapeLoader(&m_shapeLoader);
    else
        m_view->setShapeLoader(nullptr);
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
        item->setForeground(VoxelViewWidget::blockColorOf(state.name));
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
        item->setForeground(VoxelViewWidget::blockColorOf(st.name));
        m_paletteList->addItem(item);
    }
    if (m_paletteFilter)
        m_paletteFilter->clear();
    onRefreshStats();
}

QString ProjectionBlockEditorDialog::currentBlockName() const
{
    return m_currentBlockCombo->currentText().trimmed();
}

void ProjectionBlockEditorDialog::onToolChanged(int index)
{
    const int tool = m_toolCombo->itemData(index).toInt();
    m_tool = tool;
    m_view->setCursor(Qt::ArrowCursor);
    m_view->setDirty();
}

void ProjectionBlockEditorDialog::onPlace(int x, int y, int z)
{
    pushUndo();
    SchematicBlockState st = SchematicBlockState::fromString(currentBlockName());
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
    m_blockCnMap.clear();
    m_gameRegistry.clear();
    if (m_instancePath.isEmpty())
        return;
    QDir dir(m_instancePath);
    if (!dir.exists())
        return;
    const QString base = QFileInfo(m_instancePath).fileName();
    QString bestJar;
    qint64 bestSize = -1;
    const QFileInfoList jars = dir.entryInfoList(QStringList() << QStringLiteral("*.jar"),
                                                  QDir::Files, QDir::Name);
    for (const QFileInfo &fi : jars)
    {
        if (fi.completeBaseName() == base)
        {
            m_gameRegistry.extractFromJar(fi.absoluteFilePath());
            for (const BlockInfo &b : m_gameRegistry.blocks())
            {
                if (!b.chinese.isEmpty())
                    m_blockCnMap.insert(b.english.toLower(), b.chinese.first());
            }
            return;
        }
        if (fi.size() > bestSize)
        {
            bestSize = fi.size();
            bestJar = fi.absoluteFilePath();
        }
    }
    if (!bestJar.isEmpty())
    {
        m_gameRegistry.extractFromJar(bestJar);
        for (const BlockInfo &b : m_gameRegistry.blocks())
        {
            if (!b.chinese.isEmpty())
                m_blockCnMap.insert(b.english.toLower(), b.chinese.first());
        }
    }
}

void ProjectionBlockEditorDialog::applyPluginSettings()
{
    // 读取「投影方块编辑器」插件设置并应用到编辑器初始状态
    PluginManager *pm = PluginManager::instance();
    const PluginInfo plugin = pm->pluginById(QStringLiteral("projection_editor"));
    if (!plugin.loaded)
        return;

    // 显示空气
    const QString showAir = pm->pluginSetting(plugin, QStringLiteral("showAir"));
    const bool showAirOn = (showAir == QLatin1String("true") || showAir == QLatin1String("1"));
    if (m_airBtn)
    {
        m_airBtn->setChecked(showAirOn);
        m_view->setShowAir(showAirOn);
    }

    // 默认工具（查看/放置/破坏/填充）
    const QString tool = pm->pluginSetting(plugin, QStringLiteral("defaultTool"));
    int toolIndex = m_toolCombo->findText(tool);
    if (toolIndex < 0)
        toolIndex = 0;
    m_toolCombo->setCurrentIndex(toolIndex);
    m_tool = m_toolCombo->itemData(toolIndex).toInt();

    // 材质开关与来源
    const QString useTex = pm->pluginSetting(plugin, QStringLiteral("useTextures"),
                                             QStringLiteral("true"));
    const bool texturesOn = (useTex != QLatin1String("false"));
    if (!texturesOn)
    {
        // 关闭材质：清除材质加载器（回退到颜色）
        if (m_view->textureLoader() != nullptr || m_texLoader.isValid())
            setTextureSource(QString());
    }
    else if (m_instancePath.isEmpty() && m_view->textureLoader() == nullptr)
    {
        // 未由入口指定实例时，回退到插件设置的默认材质来源实例
        const QString src = pm->pluginSetting(plugin, QStringLiteral("sourceInstance")).trimmed();
        if (!src.isEmpty() && QDir(src).exists())
            setTextureSource(src);
    }

    // 真实形状渲染开关（活板门等非完整方块；关闭时整格立方体）
    const QString useShapes = pm->pluginSetting(plugin, QStringLiteral("useShapes"),
                                                QStringLiteral("true"));
    if (useShapes == QLatin1String("false"))
    {
        if (m_view->shapeLoader() != nullptr)
            m_view->setShapeLoader(nullptr);
    }
    else if (m_view->shapeLoader() == nullptr && !m_instancePath.isEmpty())
    {
        // 材质已加载但形状加载器被清掉/未挂上时，重新加载
        QString shapeErr;
        if (m_shapeLoader.load(m_instancePath, &shapeErr))
            m_view->setShapeLoader(&m_shapeLoader);
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
        tr("要替换的方块（如 minecraft:stone）："), QLineEdit::Normal,
        currentBlockName(), &ok1);
    if (!ok1 || from.trimmed().isEmpty())
        return;
    bool ok2b = false;
    const QString to = AppInputDialog::getText(this, tr("替换方块"),
        tr("替换为（如 minecraft:glass）："), QLineEdit::Normal,
        QStringLiteral("minecraft:glass"), &ok2b);
    if (!ok2b || to.trimmed().isEmpty())
        return;
    pushUndo();
    SchematicBlockState fromSt = SchematicBlockState::fromString(from);
    SchematicBlockState toSt = SchematicBlockState::fromString(to);
    const int n = m_doc.replaceAll(fromSt, toSt);
    m_dirty = true;
    updateTitle();
    rebuildView();
    onRefreshStats();
    AppMessageBox::information(this, tr("替换完成"), tr("已将 %1 个方块替换。").arg(n));
}

void ProjectionBlockEditorDialog::onFillAll()
{
    if (!m_doc.isLoaded())
        return;
    const QString sel = currentBlockName();
    pushUndo();
    SchematicBlockState st = SchematicBlockState::fromString(sel);
    const int n = m_doc.fillAll(st);
    m_dirty = true;
    updateTitle();
    rebuildView();
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

    const QString sel = currentBlockName();
    pushUndo();
    SchematicBlockState st = SchematicBlockState::fromString(sel);
    QString err;
    const int n = m_doc.fillBox(x0,y0,z0,x1,y1,z1, st, &err);
    m_dirty = true;
    updateTitle();
    rebuildView();
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
    m_currentBlockCombo->setCurrentText(st.toString());
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
    if (e->key() == Qt::Key_Escape && !m_dirty)
    {
        reject();
        return;
    }
    QDialog::keyPressEvent(e);
}
