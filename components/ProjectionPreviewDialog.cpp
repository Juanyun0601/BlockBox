/**
 * @file   ProjectionPreviewDialog.cpp
 * @brief  投影三维预览弹窗实现
 * @author BlockBox Team
 * @date   2026-11-21
 */

#include "ProjectionPreviewDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMetaObject>
#include <QMutex>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

#include "components/AppMessageBox.h"
#include "components/ProjectionMaterialDialog.h"
#include "components/VoxelViewWidget.h"
#include "layouts/FlowLayout.h"
#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/Schematic/InstanceBlockNames.h"
#include "utils/Schematic/InstanceEntityTextures.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/LitematicEditor.h"
#include "utils/Schematic/SchematicDocument.h"

#include <climits>

namespace {

// 居中卡片尺寸：一屏能看全工具栏 + 一块像样的三维画面；小屏由 AppDialogBase 压回屏内，
// 工具栏用 FlowLayout，宽度不够时整组换行，不会被挤出卡片外
const int kCardMinWidth = 1000;
const int kCardMinHeight = 660;
const int kCardMaxWidth = 1240;
const int kCardMaxHeight = 900;

// 参与三维预览的格数上限：再大多半是整片地形，硬塞进渲染会吃掉几个 G 内存
const qint64 kMaxPreviewCells = 48LL * 1024 * 1024;

// 解析缓存只留最近一份，且不超过这个格数（缓存本身也是内存，别为了快把人吃爆）
const qint64 kCacheMaxCells = 8LL * 1024 * 1024;

// 自动转台：30ms 一帧，标准转速每帧 0.45°，约 15°/秒（转一圈 24 秒）
const int kSpinIntervalMs = 30;
const float kDefaultSpinDegreesPerTick = 0.45f;

// 逐层下扫：每 110ms 掀掉一层，整份投影几十秒扫完
const int kScanIntervalMs = 110;

// 工具栏里所有控件统一高度：下拉框和小按钮高低不齐是这类工具条最容易显乱的地方
const int kFieldHeight = 28;

/** 文件大小的短文案（10.4 MB / 812 KB） */
QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    return QStringLiteral("%1 GB").arg(QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

/** 单份解析结果缓存：同一路径 + 同大小 + 同修改时间视为同一份 */
struct DocCacheEntry
{
    QString key;
    std::shared_ptr<SchematicDocument> doc;
    int sizeX = 0;
    int sizeY = 0;
    int sizeZ = 0;
    qint64 cells = 0;
    qint64 blocks = 0;
    int materialKinds = 0;
};

QMutex &docCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QVector<DocCacheEntry> &docCache()
{
    static QVector<DocCacheEntry> cache;
    return cache;
}

QString docCacheKey(const QString &filePath)
{
    const QFileInfo fi(filePath);
    return QStringLiteral("%1|%2|%3")
        .arg(fi.absoluteFilePath())
        .arg(fi.size())
        .arg(fi.lastModified().toMSecsSinceEpoch());
}

/** 统计非空气方块数与用到的方块种类（单次遍历，放在工作线程做完） */
void collectDocStats(const SchematicDocument &doc, qint64 *blocks, int *kinds)
{
    if (blocks)
        *blocks = 0;
    if (kinds)
        *kinds = 0;

    const int paletteSize = doc.paletteSize();
    const int volume = doc.volume();
    if (paletteSize <= 0 || volume <= 0)
        return;

    QVector<char> used(paletteSize, 0);
    qint64 count = 0;
    for (int i = 0; i < volume; ++i)
    {
        const int paletteIndex = doc.paletteIndexAt(i);
        if (paletteIndex <= 0)
            continue;   // 调色板 0 恒为空气
        ++count;
        if (paletteIndex < paletteSize)
            used[paletteIndex] = 1;
    }

    int kindsCount = 0;
    for (int i = 1; i < paletteSize; ++i)
        if (used.at(i))
            ++kindsCount;

    if (blocks)
        *blocks = count;
    if (kinds)
        *kinds = kindsCount;
}

} // namespace

void ProjectionPreviewDialog::preview(QWidget *parent, const QString &filePath,
                                      const QString &instancePath,
                                      const QString &displayName)
{
    if (filePath.isEmpty())
        return;
    ProjectionPreviewDialog dialog(parent, filePath, instancePath, displayName);
    dialog.exec();
}

ProjectionPreviewDialog::ProjectionPreviewDialog(QWidget *parent, const QString &filePath,
                                                 const QString &instancePath,
                                                 const QString &displayName)
    : AppDialogBase(parent)
    , m_filePath(filePath)
    , m_instancePath(instancePath)
    , m_displayName(displayName)
{
    // 没给实例就没法取贴图，直接按纯色块开场
    m_materialMode = m_instancePath.isEmpty() ? MaterialMode::Plain : MaterialMode::Instance;

    setObjectName(QStringLiteral("projectionPreviewDialog"));
    setWindowTitle(tr("投影预览"));
    initUI();
}

ProjectionPreviewDialog::~ProjectionPreviewDialog()
{
    // 文档先于视图释放会让视图手里留下悬空指针，这里先摘掉
    if (m_view)
        m_view->setDocument(nullptr);
    m_doc.reset();
}

QString ProjectionPreviewDialog::subjectName() const
{
    QString name = m_displayName.trimmed();
    if (name.isEmpty())
        name = QFileInfo(m_filePath).completeBaseName();
    return name;
}

void ProjectionPreviewDialog::initUI()
{
    m_outer = new QVBoxLayout(this);
    m_outer->setContentsMargins(0, 0, 0, 0);
    m_outer->setSpacing(0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("projectionPreviewCard"));
    m_card->setMinimumSize(kCardMinWidth, kCardMinHeight);
    m_card->setMaximumSize(kCardMaxWidth, kCardMaxHeight);
    m_outer->addWidget(m_card, 0, Qt::AlignCenter);

    m_cardLayout = new QVBoxLayout(m_card);
    m_cardLayout->setContentsMargins(20, 18, 20, 16);
    m_cardLayout->setSpacing(12);

    // ── 标题行：名称 + 统计 + 一次性提示 + 关闭 ──
    m_titleBar = new QWidget(m_card);
    m_titleBar->setObjectName(QStringLiteral("projectionPreviewTitleBar"));
    auto *titleRow = new QHBoxLayout(m_titleBar);
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(10);

    // 标题前的「3D」小徽标：一眼分清这是三维预览，而不是投影详情页
    m_titleBadge = new QLabel(tr("3D"), m_titleBar);
    m_titleBadge->setObjectName(QStringLiteral("projectionPreviewBadge"));
    m_titleBadge->setFixedSize(30, 22);
    m_titleBadge->setAlignment(Qt::AlignCenter);
    m_titleBadge->setToolTip(tr("三维预览"));
    titleRow->addWidget(m_titleBadge, 0, Qt::AlignVCenter);

    m_titleLabel = new QLabel(tr("投影预览：%1").arg(subjectName()), m_titleBar);
    m_titleLabel->setObjectName(QStringLiteral("projectionPreviewTitle"));
    m_titleLabel->setToolTip(subjectName());
    // 名字很长时允许压缩，别把整张卡片顶宽（手机上尤其明显）
    m_titleLabel->setMinimumWidth(0);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titleRow->addWidget(m_titleLabel, 1, Qt::AlignVCenter);

    m_statsLabel = new QLabel(m_titleBar);
    m_statsLabel->setObjectName(QStringLiteral("projectionPreviewStats"));
    // 还没解析出统计（加载中 / 解析失败）时不显示一个空的小方块
    m_statsLabel->hide();
    titleRow->addWidget(m_statsLabel, 0, Qt::AlignVCenter);

    m_statusLabel = new QLabel(m_titleBar);
    m_statusLabel->setObjectName(QStringLiteral("projectionPreviewStatus"));
    m_statusLabel->hide();
    titleRow->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    m_closeBtn = new QPushButton(tr("关闭"), m_titleBar);
    m_closeBtn->setObjectName(QStringLiteral("projectionPreviewCloseBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    m_closeBtn->setFixedHeight(kFieldHeight);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleRow->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

    m_cardLayout->addWidget(m_titleBar);

    // ── 工具栏：按用途分成四组（视角 / 材质 / 旋转 / 输出），窄窗口整组换行 ──
    m_toolBar = new QWidget(m_card);
    m_toolBar->setObjectName(QStringLiteral("projectionPreviewToolbar"));
    auto *toolFlow = new FlowLayout(m_toolBar, 0, 10, 6);
    toolFlow->setContentsMargins(10, 8, 10, 8);

    // 每组一组相关控件：换行时以整组为单位，不会把「视角」和下拉框拆到两行
    auto makeGroup = [this]() {
        auto *group = new QWidget(m_toolBar);
        group->setObjectName(QStringLiteral("projectionPreviewGroup"));
        auto *groupLayout = new QHBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(6);
        return group;
    };

    QWidget *viewGroup = makeGroup();
    auto *viewGroupLayout = qobject_cast<QHBoxLayout *>(viewGroup->layout());
    toolFlow->addWidget(viewGroup);

    auto *viewTitle = new QLabel(tr("视角"), viewGroup);
    viewTitle->setObjectName(QStringLiteral("projectionPreviewFieldLabel"));
    viewGroupLayout->addWidget(viewTitle);

    m_viewCombo = new QComboBox(viewGroup);
    m_viewCombo->setObjectName(QStringLiteral("projectionPreviewCombo"));
    m_viewCombo->setCursor(Qt::PointingHandCursor);
    m_viewCombo->setFixedHeight(kFieldHeight);
    m_viewCombo->setToolTip(tr("换个方向看整份投影（取景距离不变）；数字键 1~7 也能切"));
    m_viewCombo->addItem(tr("等距"),
                         static_cast<int>(VoxelViewWidget::ViewPreset::Iso));
    m_viewCombo->addItem(tr("正前"),
                         static_cast<int>(VoxelViewWidget::ViewPreset::Front));
    m_viewCombo->addItem(tr("正后"),
                         static_cast<int>(VoxelViewWidget::ViewPreset::Back));
    m_viewCombo->addItem(tr("左侧"),
                         static_cast<int>(VoxelViewWidget::ViewPreset::Left));
    m_viewCombo->addItem(tr("右侧"),
                         static_cast<int>(VoxelViewWidget::ViewPreset::Right));
    m_viewCombo->addItem(tr("俯视"),
                         static_cast<int>(VoxelViewWidget::ViewPreset::Top));
    m_viewCombo->addItem(tr("仰视"),
                         static_cast<int>(VoxelViewWidget::ViewPreset::Bottom));
    connect(m_viewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { applyViewPresetIndex(index); });
    viewGroupLayout->addWidget(m_viewCombo);

    auto makeChip = [this](const QString &text, const QString &tip) {
        auto *btn = new QToolButton(m_toolBar);
        btn->setObjectName(QStringLiteral("projectionPreviewChipBtn"));
        btn->setText(text);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(kFieldHeight);
        // 不抢键盘焦点：点完按钮后 WASD / 空格 仍然直接作用于模型
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };

    m_resetBtn = makeChip(tr("重置视角"), tr("回到默认等距视角，并把整份投影重新收进画面（R 键）"));
    connect(m_resetBtn, &QToolButton::clicked, this,
            &ProjectionPreviewDialog::resetViewToDefault);
    viewGroupLayout->addWidget(m_resetBtn);

    // 材质组：方块贴图来源 + 生物开关
    QWidget *materialGroup = makeGroup();
    auto *materialGroupLayout = qobject_cast<QHBoxLayout *>(materialGroup->layout());
    toolFlow->addWidget(materialGroup);

    auto *materialTitle = new QLabel(tr("材质"), materialGroup);
    materialTitle->setObjectName(QStringLiteral("projectionPreviewFieldLabel"));
    materialGroupLayout->addWidget(materialTitle);

    m_materialCombo = new QComboBox(materialGroup);
    m_materialCombo->setObjectName(QStringLiteral("projectionPreviewCombo"));
    m_materialCombo->setCursor(Qt::PointingHandCursor);
    m_materialCombo->setFixedHeight(kFieldHeight);
    m_materialCombo->setToolTip(tr("方块用实例客户端的贴图显示，还是只看颜色与形状"));
    m_materialCombo->addItem(tr("跟随实例"), static_cast<int>(MaterialMode::Instance));
    m_materialCombo->addItem(tr("纯色块"), static_cast<int>(MaterialMode::Plain));
    m_materialCombo->setCurrentIndex(static_cast<int>(m_materialMode));
    connect(m_materialCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const auto mode =
                    static_cast<MaterialMode>(m_materialCombo->itemData(index).toInt());
                if (mode == m_materialMode)
                    return;
                setMaterialMode(mode);
                if (mode != m_materialMode)
                {
                    const QSignalBlocker blocker(m_materialCombo);
                    m_materialCombo->setCurrentIndex(static_cast<int>(m_materialMode));
                }
            });
    materialGroupLayout->addWidget(m_materialCombo);

    // 生物 / 实体：默认显示，掀开层时跟着一起隐/现；纯色块模式没有贴图就自动隐藏
    m_entityBtn = makeChip(tr("生物"), tr("显示投影里保存的生物与实体（贴图取自当前实例）"));
    m_entityBtn->setCheckable(true);
    m_entityBtn->setChecked(true);
    connect(m_entityBtn, &QToolButton::toggled, this, [this](bool on) {
        m_entityVisible = on;
        if (m_view)
            m_view->setShowEntities(on);
        updateStatsLabel();
    });
    materialGroupLayout->addWidget(m_entityBtn);

    // 旋转组：自动旋转开关 + 转速
    QWidget *spinGroup = makeGroup();
    auto *spinGroupLayout = qobject_cast<QHBoxLayout *>(spinGroup->layout());
    toolFlow->addWidget(spinGroup);

    m_spinBtn = makeChip(tr("自动旋转"), tr("让模型慢慢转起来，方便整体观察；手一拖就停"));
    m_spinBtn->setCheckable(true);
    connect(m_spinBtn, &QToolButton::toggled, this,
            &ProjectionPreviewDialog::toggleAutoSpin);
    spinGroupLayout->addWidget(m_spinBtn);

    m_speedCombo = new QComboBox(spinGroup);
    m_speedCombo->setObjectName(QStringLiteral("projectionPreviewCombo"));
    m_speedCombo->setCursor(Qt::PointingHandCursor);
    m_speedCombo->setFixedHeight(kFieldHeight);
    m_speedCombo->setToolTip(tr("自动旋转的快慢：慢一点适合细看，快一点适合整体扫一眼"));
    m_speedCombo->addItem(tr("慢"), 6.0);
    m_speedCombo->addItem(tr("标准"), 15.0);
    m_speedCombo->addItem(tr("快"), 36.0);
    m_speedCombo->setCurrentIndex(1);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applySpinSpeed(); });
    spinGroupLayout->addWidget(m_speedCombo);

    // 输出组：全屏 / 截图 / 更多
    QWidget *outputGroup = makeGroup();
    auto *outputGroupLayout = qobject_cast<QHBoxLayout *>(outputGroup->layout());
    toolFlow->addWidget(outputGroup);

    m_fullscreenBtn = makeChip(tr("全屏"), tr("把预览放大到整个窗口（F 键，或双击视图；Esc 退出全屏）"));
    m_fullscreenBtn->setCheckable(true);
    connect(m_fullscreenBtn, &QToolButton::toggled, this, [this](bool on) {
        setFullscreenPreview(on);
    });
    outputGroupLayout->addWidget(m_fullscreenBtn);

    m_shotBtn = makeChip(tr("截图"), tr("把当前画面存成 PNG（P 键）"));
    connect(m_shotBtn, &QToolButton::clicked, this,
            &ProjectionPreviewDialog::saveScreenshot);
    outputGroupLayout->addWidget(m_shotBtn);

    // 「更多」：材料清单 / 复制画面 / 剖切方向 / 快捷键说明，收进菜单不占宽度
    m_moreBtn = makeChip(tr("更多"), tr("材料清单、复制画面、剖切方向与快捷键说明"));
    m_moreBtn->setPopupMode(QToolButton::InstantPopup);
    m_moreBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    {
        auto *moreMenu = new QMenu(m_moreBtn);

        QAction *materialAction = moreMenu->addAction(tr("材料清单…"));
        materialAction->setToolTip(tr("列出这份投影要用哪些方块、各要多少个"));
        connect(materialAction, &QAction::triggered, this,
                &ProjectionPreviewDialog::openMaterialList);

        QAction *copyAction = moreMenu->addAction(tr("复制当前画面（Ctrl+C）"));
        connect(copyAction, &QAction::triggered, this,
                &ProjectionPreviewDialog::copyScreenshot);

        QMenu *axisMenu = moreMenu->addMenu(tr("剖切方向"));
        auto *axisGroup = new QActionGroup(axisMenu);
        axisGroup->setExclusive(true);
        const QStringList axisNames{tr("水平（按层掀开屋顶）"), tr("左右（沿 X 剖开）"),
                                    tr("前后（沿 Z 剖开）")};
        for (int i = 0; i < axisNames.size(); ++i)
        {
            QAction *action = axisMenu->addAction(axisNames.at(i));
            action->setCheckable(true);
            action->setChecked(i == 0);
            axisGroup->addAction(action);
            connect(action, &QAction::triggered, this,
                    [this, i]() { applyLayerAxis(i); });
        }

        moreMenu->addSeparator();
        QAction *helpAction = moreMenu->addAction(tr("快捷键说明（F1）"));
        connect(helpAction, &QAction::triggered, this,
                &ProjectionPreviewDialog::showShortcutHelp);

        m_moreBtn->setMenu(moreMenu);
    }
    outputGroupLayout->addWidget(m_moreBtn);

    m_cardLayout->addWidget(m_toolBar);

    // ── 层数条：只看其中一层，或该层及以下整段（掀开屋顶看内部用） ──
    m_layerBar = new QWidget(m_card);
    m_layerBar->setObjectName(QStringLiteral("projectionPreviewLayerBar"));
    auto *layerRow = new QHBoxLayout(m_layerBar);
    layerRow->setContentsMargins(10, 6, 10, 6);
    layerRow->setSpacing(6);

    auto *layerTitle = new QLabel(tr("显示层"), m_layerBar);
    layerTitle->setObjectName(QStringLiteral("projectionPreviewFieldLabel"));
    layerTitle->setToolTip(tr("只看投影中的某一层，或该层及以下整段"));
    layerRow->addWidget(layerTitle);

    // 箭头用「◂ ▸」（小三角，无彩色 emoji 变体），并用 QToolButton 避开
    // 全局「QDialog QPushButton{min-width:80px}」把按钮拉成长条
    m_layerDown = new QToolButton(m_layerBar);
    m_layerDown->setObjectName(QStringLiteral("projectionPreviewStepBtn"));
    m_layerDown->setText(QStringLiteral(u"\u25C2"));
    m_layerDown->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_layerDown->setFixedSize(kFieldHeight, kFieldHeight);
    m_layerDown->setCursor(Qt::PointingHandCursor);
    m_layerDown->setToolTip(tr("往下一层"));
    m_layerDown->setAccessibleName(tr("往下一层"));
    m_layerDown->setFocusPolicy(Qt::NoFocus);
    layerRow->addWidget(m_layerDown);

    m_layerSlider = new QSlider(Qt::Horizontal, m_layerBar);
    m_layerSlider->setObjectName(QStringLiteral("projectionPreviewLayerSlider"));
    m_layerSlider->setRange(0, 0);
    m_layerSlider->setMinimumWidth(180);
    m_layerSlider->setToolTip(tr("最左为「全部」；往右依次是自下往上的每一层"));
    m_layerSlider->setFocusPolicy(Qt::NoFocus);
    layerRow->addWidget(m_layerSlider, 1);

    m_layerUp = new QToolButton(m_layerBar);
    m_layerUp->setObjectName(QStringLiteral("projectionPreviewStepBtn"));
    m_layerUp->setText(QStringLiteral(u"\u25B8"));
    m_layerUp->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_layerUp->setFixedSize(kFieldHeight, kFieldHeight);
    m_layerUp->setCursor(Qt::PointingHandCursor);
    m_layerUp->setToolTip(tr("往上一层"));
    m_layerUp->setAccessibleName(tr("往上一层"));
    m_layerUp->setFocusPolicy(Qt::NoFocus);
    layerRow->addWidget(m_layerUp);

    m_layerValue = new QLabel(m_layerBar);
    m_layerValue->setObjectName(QStringLiteral("projectionPreviewLayerValue"));
    m_layerValue->setMinimumWidth(90);
    m_layerValue->setAlignment(Qt::AlignCenter);
    layerRow->addWidget(m_layerValue);

    m_belowToggle = new QToolButton(m_layerBar);
    m_belowToggle->setObjectName(QStringLiteral("projectionPreviewBelowToggle"));
    m_belowToggle->setText(tr("此层以下"));
    m_belowToggle->setCheckable(true);
    m_belowToggle->setCursor(Qt::PointingHandCursor);
    m_belowToggle->setToolTip(
        tr("勾选后显示该层及以下的全部层（选中层正常、下方调暗），不勾选则只看这一层"));
    m_belowToggle->setFocusPolicy(Qt::NoFocus);
    m_belowToggle->setFixedHeight(kFieldHeight);
    layerRow->addWidget(m_belowToggle);

    // 逐层下扫：从最顶层开始自动往下掀，一路看完内部结构
    m_scanBtn = new QToolButton(m_layerBar);
    m_scanBtn->setObjectName(QStringLiteral("projectionPreviewChipBtn"));
    m_scanBtn->setText(tr("扫描"));
    m_scanBtn->setCheckable(true);
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    m_scanBtn->setToolTip(
        tr("从最上层自动往下逐层掀开，一次看完建筑内部；再点一下停止"));
    m_scanBtn->setAccessibleName(tr("逐层扫描"));
    m_scanBtn->setFocusPolicy(Qt::NoFocus);
    m_scanBtn->setFixedHeight(kFieldHeight);
    layerRow->addWidget(m_scanBtn);
    connect(m_scanBtn, &QToolButton::toggled, this,
            &ProjectionPreviewDialog::toggleLayerScan);

    m_layerBar->hide();
    m_cardLayout->addWidget(m_layerBar);

    connect(m_layerSlider, &QSlider::valueChanged, this,
            &ProjectionPreviewDialog::applyLayer);
    // 用户自己拖滑块就停下自动扫描，免得和手的操作打架
    connect(m_layerSlider, &QSlider::sliderPressed, this,
            &ProjectionPreviewDialog::stopLayerScan);
    connect(m_layerDown, &QToolButton::clicked, this, [this]() {
        stopLayerScan();
        if (m_layerSlider)
            m_layerSlider->setValue(m_layerSlider->value() - 1);
    });
    connect(m_layerUp, &QToolButton::clicked, this, [this]() {
        stopLayerScan();
        if (m_layerSlider)
            m_layerSlider->setValue(m_layerSlider->value() + 1);
    });
    connect(m_belowToggle, &QToolButton::toggled, this, [this](bool checked) {
        if (m_scanning)
            stopLayerScan();
        if (!m_layerSlider)
            return;
        int value = m_layerSlider->value();
        // 还在「全部」时勾上「此层以下」：自动落到最上面一层，方便直接往下拖
        if (checked && value <= 0)
        {
            const QSignalBlocker blocker(m_layerSlider);
            m_layerSlider->setValue(m_layerSlider->maximum());
            value = m_layerSlider->value();
        }
        applyLayer(value);
    });

    // ── 三维画面外框：一块独立的「取景框」，四周一圈细边，全屏时去掉圆角描边 ──
    m_stage = new QWidget(m_card);
    m_stage->setObjectName(QStringLiteral("projectionPreviewStage"));
    auto *stageLayout = new QVBoxLayout(m_stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);

    // ── 三维视图（解析成功后出现） ──
    m_view = new VoxelViewWidget(m_stage);
    m_view->setObjectName(QStringLiteral("projectionPreviewView"));
    m_view->setMinimumHeight(320);
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_view->installEventFilter(this);
    m_view->hide();
    stageLayout->addWidget(m_view, 1);
    m_cardLayout->addWidget(m_stage, 1);

    // ── 提示行（加载中 / 失败 / 操作说明）+ 悬停方块信息 + 超大投影的「仍然加载」 ──
    // 整块放进一个容器：模型没就绪时它撑满空出来的画面区，提示居中显示，
    // 就绪后回到卡片底部当一行说明文字
    m_infoBar = new QWidget(m_card);
    m_infoBar->setObjectName(QStringLiteral("projectionPreviewInfoBar"));
    auto *infoColumn = new QVBoxLayout(m_infoBar);
    infoColumn->setContentsMargins(0, 0, 0, 0);
    infoColumn->setSpacing(10);

    auto *infoRow = new QHBoxLayout();
    infoRow->setSpacing(10);

    m_hint = new QLabel(m_infoBar);
    m_hint->setObjectName(QStringLiteral("projectionPreviewHint"));
    m_hint->setWordWrap(true);
    m_hint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    infoRow->addWidget(m_hint, 1);

    m_hoverLabel = new QLabel(m_infoBar);
    m_hoverLabel->setObjectName(QStringLiteral("projectionPreviewHover"));
    m_hoverLabel->setMinimumWidth(0);
    m_hoverLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_hoverLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_hoverLabel->hide();
    infoRow->addWidget(m_hoverLabel, 0);
    infoColumn->addLayout(infoRow);

    m_forceBtn = new QPushButton(tr("仍然加载"), m_infoBar);
    m_forceBtn->setObjectName(QStringLiteral("projectionPreviewForceBtn"));
    m_forceBtn->setCursor(Qt::PointingHandCursor);
    m_forceBtn->setToolTip(tr("知道会占内存，还是要看这份投影"));
    m_forceBtn->setFocusPolicy(Qt::NoFocus);
    m_forceBtn->hide();
    connect(m_forceBtn, &QPushButton::clicked, this,
            &ProjectionPreviewDialog::forceLoad);
    infoColumn->addWidget(m_forceBtn, 0, Qt::AlignHCenter);

    m_cardLayout->addWidget(m_infoBar);

    // 鼠标指着某个方块：显示它的名字和坐标（中文名后台读好后才补上）
    connect(m_view, &VoxelViewWidget::hoverChanged, this,
            [this](int x, int y, int z) {
                if (!m_hoverLabel)
                    return;
                if (x == INT_MIN || !m_doc || !m_doc->isLoaded())
                {
                    m_hoverLabel->clear();
                    m_hoverLabel->hide();
                    return;
                }
                const SchematicBlockState state = m_doc->block(x, y, z);
                if (state.isAir())
                {
                    m_hoverLabel->clear();
                    m_hoverLabel->hide();
                    return;
                }
                const QString name =
                    InstanceBlockNames::displayName(m_cnMap, state.toString());
                const QString text =
                    tr("%1  @ %2, %3, %4").arg(name).arg(x).arg(y).arg(z);
                m_hoverLabel->setText(text);
                m_hoverLabel->setToolTip(text);
                m_hoverLabel->show();
            });

    // 自动转台定时器：一边转一边不需要重建几何，只是改朝向
    m_spinTimer = new QTimer(this);
    m_spinTimer->setInterval(kSpinIntervalMs);
    connect(m_spinTimer, &QTimer::timeout, this, [this]() {
        if (m_view && m_docReady)
            m_view->orbitYaw(m_spinDegreesPerTick);
    });
    applySpinSpeed();

    // 逐层扫描定时器：每走一步就是一次层过滤重建
    m_scanTimer = new QTimer(this);
    m_scanTimer->setInterval(kScanIntervalMs);
    connect(m_scanTimer, &QTimer::timeout, this, [this]() {
        if (!m_layerSlider)
        {
            stopLayerScan();
            return;
        }
        const int next = m_layerSlider->value() - 1;
        if (next <= 0)
        {
            if (m_layerSlider->value() > 1)
                m_layerSlider->setValue(1);
            stopLayerScan();
            showTransient(tr("逐层扫描结束，拖动滑块可以回看任意一层"));
            return;
        }
        m_layerSlider->setValue(next);
    });

    // 一次性提示的自动消失计时器
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        if (m_statusLabel)
        {
            m_statusLabel->clear();
            m_statusLabel->hide();
        }
    });

    setControlsEnabled(false);
    setPlaceholderState(true);
    setHint(tr("正在准备预览…"));
}

void ProjectionPreviewDialog::setHint(const QString &text)
{
    if (!m_hint)
        return;
    m_hint->setText(text);
    m_hint->show();
}

void ProjectionPreviewDialog::setPlaceholderState(bool on)
{
    // 没有画面可看时，别在卡片里留一个空荡荡的边框盒子：
    // 收起画面框，让提示文字撑满这块留白并居中显示
    if (m_stage)
        m_stage->setVisible(!on);
    if (m_infoBar && m_cardLayout)
    {
        const int index = m_cardLayout->indexOf(m_infoBar);
        if (index >= 0)
            m_cardLayout->setStretch(index, on ? 1 : 0);
    }
    if (m_hint)
        m_hint->setAlignment((on ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter);
    if (on && m_hoverLabel)
        m_hoverLabel->hide();
}

void ProjectionPreviewDialog::showTransient(const QString &text)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setToolTip(text);
    m_statusLabel->show();
    if (m_statusTimer)
        m_statusTimer->start(3500);
}

void ProjectionPreviewDialog::setControlsEnabled(bool enabled)
{
    if (m_viewCombo)
        m_viewCombo->setEnabled(enabled);
    if (m_resetBtn)
        m_resetBtn->setEnabled(enabled);
    if (m_materialCombo)
        m_materialCombo->setEnabled(enabled);
    if (m_speedCombo)
        m_speedCombo->setEnabled(enabled);
    if (m_fullscreenBtn)
        m_fullscreenBtn->setEnabled(enabled);
    if (m_shotBtn)
        m_shotBtn->setEnabled(enabled);
    if (m_moreBtn)
        m_moreBtn->setEnabled(enabled);
    if (m_entityBtn)
        m_entityBtn->setEnabled(enabled);

    if (!enabled)
        stopLayerScan();

    if (m_spinBtn)
    {
        if (!enabled)
        {
            const QSignalBlocker blocker(m_spinBtn);
            m_spinBtn->setChecked(false);
            if (m_spinTimer)
                m_spinTimer->stop();
        }
        m_spinBtn->setEnabled(enabled);
    }
}

void ProjectionPreviewDialog::showEvent(QShowEvent *event)
{
    AppDialogBase::showEvent(event);
    if (!m_loadStarted)
        startLoad();
}

void ProjectionPreviewDialog::startLoad()
{
    if (m_loading || m_docReady)
        return;

    m_loadStarted = true;
    m_loading = true;
    setPlaceholderState(true);
    setHint(tr("正在解析投影文件…（%1）").arg(humanSize(QFileInfo(m_filePath).size())));

    const QString filePath = m_filePath;
    QPointer<ProjectionPreviewDialog> guard(this);
    (void)QtConcurrent::run([guard, filePath]() {
        const LoadResult result = loadDocument(filePath);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, result]() {
                if (!guard)
                    return;   // 弹窗已关闭，结果作废
                guard->applyDocument(result);
            },
            Qt::QueuedConnection);
    });
}

void ProjectionPreviewDialog::forceLoad()
{
    if (m_forced || m_loading)
        return;
    m_forced = true;
    if (m_forceBtn)
        m_forceBtn->hide();
    startLoad();
}

ProjectionPreviewDialog::LoadResult
ProjectionPreviewDialog::loadDocument(const QString &filePath)
{
    LoadResult result;

    if (!QFileInfo::exists(filePath))
    {
        result.error = tr("找不到投影文件：\n%1").arg(filePath);
        return result;
    }

    const QString lowered = filePath.toLower();
    const bool isLitematic = lowered.endsWith(QLatin1String(".litematic"));
    const bool isSchematica = lowered.endsWith(QLatin1String(".schematic"));
    const bool isSponge = lowered.endsWith(QLatin1String(".schem"));
    if (!isLitematic && !isSchematica && !isSponge)
    {
        result.error = tr("这种格式暂时看不了三维预览，"
                          "只有 .litematic / .schematic / .schem 能直接打开。");
        return result;
    }

    // 同一份文件（路径 + 大小 + 修改时间都没变）直接用上次的解析结果，关掉再开秒开
    const QString cacheKey = docCacheKey(filePath);
    {
        QMutexLocker locker(&docCacheMutex());
        for (const DocCacheEntry &entry : docCache())
        {
            if (entry.key != cacheKey || !entry.doc)
                continue;
            result.doc = entry.doc;
            result.sizeX = entry.sizeX;
            result.sizeY = entry.sizeY;
            result.sizeZ = entry.sizeZ;
            result.cells = entry.cells;
            result.blocks = entry.blocks;
            result.materialKinds = entry.materialKinds;
            result.fromCache = true;
            return result;
        }
    }

    // .schematic / .schem 先转成临时 .litematic 再读，读完立刻删掉
    QString parsePath = filePath;
    bool tempFile = false;
    if (!isLitematic)
    {
        const QString tmpDir =
            QDir::tempPath() + QStringLiteral("/blockbox_projection_preview");
        QDir().mkpath(tmpDir);
        const QString tmpPath =
            QStringLiteral("%1/%2_%3.litematic")
                .arg(tmpDir, QFileInfo(filePath).completeBaseName(),
                     QUuid::createUuid().toString(QUuid::WithoutBraces));

        QString convertError;
        const bool converted =
            isSchematica
                ? LitematicEditor::convertFromSchematica(filePath, tmpPath, &convertError)
                : LitematicEditor::convertFromSponge(filePath, tmpPath, &convertError);
        if (!converted)
        {
            QFile::remove(tmpPath);
            result.error = tr("这份投影转换不出三维数据，可能文件已损坏：\n%1")
                               .arg(convertError.isEmpty() ? filePath : convertError);
            return result;
        }
        parsePath = tmpPath;
        tempFile = true;
    }

    auto doc = std::make_shared<SchematicDocument>();
    QString loadError;
    const bool loaded = doc->load(parsePath, &loadError);
    if (tempFile)
        QFile::remove(parsePath);

    if (!loaded || !doc->isLoaded())
    {
        result.error = loadError.isEmpty()
                           ? tr("无法解析这份投影，文件可能已损坏。")
                           : loadError;
        return result;
    }

    result.doc = doc;
    result.sizeX = doc->sizeX();
    result.sizeY = doc->sizeY();
    result.sizeZ = doc->sizeZ();
    result.cells = qint64(result.sizeX) * qint64(result.sizeY) * qint64(result.sizeZ);
    collectDocStats(*doc, &result.blocks, &result.materialKinds);

    // 只缓存「不算大」的投影：缓存占的就是内存，大文件宁可每次重解析
    if (result.cells <= kCacheMaxCells)
    {
        QMutexLocker locker(&docCacheMutex());
        DocCacheEntry entry;
        entry.key = cacheKey;
        entry.doc = doc;
        entry.sizeX = result.sizeX;
        entry.sizeY = result.sizeY;
        entry.sizeZ = result.sizeZ;
        entry.cells = result.cells;
        entry.blocks = result.blocks;
        entry.materialKinds = result.materialKinds;
        docCache().clear();   // 只留最近一份
        docCache().append(entry);
    }

    return result;
}

void ProjectionPreviewDialog::applyDocument(const LoadResult &result)
{
    m_loading = false;

    if (!result.doc)
    {
        m_docReady = false;
        setControlsEnabled(false);
        if (m_forceBtn)
            m_forceBtn->hide();
        setPlaceholderState(true);
        setHint(result.error.isEmpty() ? tr("无法解析这份投影。") : result.error);
        return;
    }

    m_sizeX = result.sizeX;
    m_sizeY = result.sizeY;
    m_sizeZ = result.sizeZ;
    m_cells = result.cells;
    m_blocks = result.blocks;
    m_materialKinds = result.materialKinds;

    // 太大的投影先拦住：解析能过，但整份几何 + 贴图铺开会把内存吃满
    if (m_cells > kMaxPreviewCells && !m_forced)
    {
        m_doc.reset();
        m_docReady = false;
        setControlsEnabled(false);
        updateStatsLabel();
        setPlaceholderState(true);
        if (m_forceBtn)
            m_forceBtn->show();
        setHint(tr("这份投影有 %1 格，铺开来渲染会占用较多内存。\n"
                   "机器扛得住的话，可以点下面的「仍然加载」。")
                    .arg(QLocale().toString(m_cells)));
        return;
    }

    if (m_forceBtn)
        m_forceBtn->hide();

    m_doc = result.doc;
    m_entityCount = m_doc ? m_doc->entityCount() : 0;

    // 先挂材质 / 形状（此时还没文档，重建很轻），再挂文档就只建一次几何
    if (applyMaterialToView())
        showTransient(tr("这个实例里找不到可用客户端，已改用纯色块显示"));
    prewarmShapes();
    m_view->setDocument(m_doc.get());
    m_docReady = true;

    const int layerCount = layerSliceCount();
    {
        const QSignalBlocker blocker(m_layerSlider);
        m_layerSlider->setRange(0, qMax(0, layerCount));
        m_layerSlider->setValue(0);
    }
    if (m_belowToggle)
    {
        const QSignalBlocker blocker(m_belowToggle);
        m_belowToggle->setChecked(false);
    }
    m_layerBar->setVisible(layerCount > 0);
    refreshLayerUi();

    setControlsEnabled(true);
    updateStatsLabel();
    setPlaceholderState(false);

    refreshEntityUi();

    m_view->show();
    resetViewToDefault();

    setHint(tr("拖动转向 · WASD 飞行 · 空格上升 · Shift 下降 · 滚轮前后 · "
               "1~7 切视角 · R 重置 · F 全屏 · P 截图 · Ctrl+C 复制 · Esc 关闭"));

    // 悬停方块名要读实例语言文件，放后台慢慢补，先把模型给用户看
    ensureBlockNames();

    if (result.fromCache)
        showTransient(tr("已从上次的解析结果直接打开"));
}

/** 生物开关的可用状态：没有实体贴图（纯色块 / 老版本实例）就灰掉，别让人以为投影里没生物 */
void ProjectionPreviewDialog::refreshEntityUi()
{
    if (!m_entityBtn)
        return;
    const bool available = m_view != nullptr && m_view->entityTextures() != nullptr
                           && m_entityCount > 0;
    const QSignalBlocker blocker(m_entityBtn);
    m_entityBtn->setEnabled(available);
    m_entityBtn->setChecked(available && m_entityVisible);
    m_entityBtn->setToolTip(available
                                ? tr("显示投影里保存的生物与实体（贴图取自当前实例）")
                                : tr("这个实例取不到生物贴图，或者投影里没有生物"));
    if (m_view)
        m_view->setShowEntities(available && m_entityVisible);
}

bool ProjectionPreviewDialog::applyMaterialToView()
{    if (!m_view)
        return false;

    bool fellBack = false;

    if (m_materialMode == MaterialMode::Instance && !m_instancePath.isEmpty())
    {
        InstanceTextureLoader *texLoader =
            InstanceTextureLoader::sharedForInstance(m_instancePath);
        BlockShapeLoader *shapeLoader =
            BlockShapeLoader::sharedForInstance(m_instancePath);

        if (!texLoader && !shapeLoader)
        {
            m_materialMode = MaterialMode::Plain;
            fellBack = true;
        }
        else
        {
            // 只有真的换了加载器才调 setter，避免无谓的整份几何重建
            if (m_view->textureLoader() != texLoader)
                m_view->setTextureLoader(texLoader);
            if (m_view->shapeLoader() != shapeLoader)
                m_view->setShapeLoader(shapeLoader);

            // 生物贴图同为「跟随实例」：换实例后生物外观跟着变。
            // 只有投影里真的有生物才去解 jar 里的实体贴图（要解几百张图，别白花这个时间）
            InstanceEntityTextures *entityTextures =
                (m_entityCount > 0)
                    ? InstanceEntityTextures::sharedForInstance(m_instancePath)
                    : nullptr;
            if (m_view->entityTextures() != entityTextures)
                m_view->setEntityTextures(entityTextures);
            return false;
        }
    }
    else if (m_materialMode == MaterialMode::Instance)
    {
        m_materialMode = MaterialMode::Plain;
        fellBack = true;
    }

    // 纯色块：摘掉贴图与形状加载器
    if (m_view->textureLoader() != nullptr)
        m_view->setTextureLoader(nullptr);
    if (m_view->shapeLoader() != nullptr)
        m_view->setShapeLoader(nullptr);
    if (m_view->entityTextures() != nullptr)
        m_view->setEntityTextures(nullptr);

    if (m_materialCombo)
    {
        const QSignalBlocker blocker(m_materialCombo);
        m_materialCombo->setCurrentIndex(static_cast<int>(m_materialMode));
    }
    return fellBack;
}

void ProjectionPreviewDialog::prewarmShapes()
{
    if (!m_doc || !m_view)
        return;
    BlockShapeLoader *shapeLoader = m_view->shapeLoader();
    if (!shapeLoader)
        return;

    QStringList states;
    const int paletteSize = m_doc->paletteSize();
    states.reserve(paletteSize);
    for (int i = 0; i < paletteSize; ++i)
        states.append(m_doc->paletteEntry(i).toString());
    if (!states.isEmpty())
        shapeLoader->prewarm(states);
}

void ProjectionPreviewDialog::ensureBlockNames()
{
    if (m_cnMapLoading || !m_cnMap.isEmpty() || m_instancePath.isEmpty())
        return;
    m_cnMapLoading = true;

    const QString instancePath = m_instancePath;
    QPointer<ProjectionPreviewDialog> guard(this);
    (void)QtConcurrent::run([guard, instancePath]() {
        // 读实例语言文件可能要翻 jar / 资源包，放后台跑，别卡住已经出来的模型
        const QHash<QString, QString> map = InstanceBlockNames::loadChineseMap(instancePath);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, map]() {
                if (!guard)
                    return;
                guard->m_cnMap = map;
                guard->m_cnMapLoading = false;
            },
            Qt::QueuedConnection);
    });
}

void ProjectionPreviewDialog::setMaterialMode(MaterialMode mode)
{
    if (m_materialMode == mode)
        return;

    if (mode == MaterialMode::Instance && m_instancePath.isEmpty())
    {
        showTransient(tr("没有选中的实例，只能用纯色块显示"));
        if (m_materialCombo)
        {
            const QSignalBlocker blocker(m_materialCombo);
            m_materialCombo->setCurrentIndex(static_cast<int>(m_materialMode));
        }
        return;
    }

    m_materialMode = mode;
    if (m_materialCombo)
    {
        const QSignalBlocker blocker(m_materialCombo);
        m_materialCombo->setCurrentIndex(static_cast<int>(m_materialMode));
    }

    if (!m_docReady)
        return;

    const bool fellBack = applyMaterialToView();
    prewarmShapes();
    refreshEntityUi();
    if (fellBack)
        showTransient(tr("这个实例里找不到可用客户端，仍用纯色块显示"));
    else
        showTransient(m_materialMode == MaterialMode::Instance ? tr("已切换为实例贴图")
                                                               : tr("已切换为纯色块"));
    if (m_view)
        m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionPreviewDialog::applyViewPresetIndex(int index)
{
    if (!m_view || !m_docReady || !m_viewCombo)
        return;
    if (index < 0 || index >= m_viewCombo->count())
        return;
    const int preset = m_viewCombo->itemData(index).toInt();
    m_view->applyViewPreset(static_cast<VoxelViewWidget::ViewPreset>(preset));
    m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionPreviewDialog::resetViewToDefault()
{
    if (!m_view || !m_docReady)
        return;

    m_view->resetView();
    if (m_viewCombo)
    {
        const QSignalBlocker blocker(m_viewCombo);
        m_viewCombo->setCurrentIndex(0);
    }

    // 等布局把视图尺寸排好再按宽高比取景，扁平的建筑才不会被推得太远
    QPointer<ProjectionPreviewDialog> guard(this);
    QTimer::singleShot(0, this, [guard]() {
        if (guard && guard->m_view && guard->m_docReady)
            guard->m_view->focusAll();
    });
    m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionPreviewDialog::toggleAutoSpin(bool on)
{
    if (!on)
    {
        if (m_spinTimer)
            m_spinTimer->stop();
        return;
    }

    if (!m_docReady)
    {
        if (m_spinBtn)
        {
            const QSignalBlocker blocker(m_spinBtn);
            m_spinBtn->setChecked(false);
        }
        return;
    }

    if (m_spinTimer)
        m_spinTimer->start();
    if (m_view)
        m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionPreviewDialog::saveScreenshot()
{
    if (!m_view || !m_docReady)
        return;

    const QImage shot = m_view->grabFramebuffer();
    if (shot.isNull())
    {
        showTransient(tr("截图没取到画面，换个视角再试"));
        return;
    }

    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();

    QString baseName = subjectName();
    baseName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),
                     QStringLiteral("_"));
    if (baseName.isEmpty())
        baseName = QStringLiteral("projection");

    const QString suggested =
        dir + QLatin1Char('/') + baseName + QStringLiteral("_预览.png");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存预览截图"), suggested, tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    if (shot.save(path, "PNG"))
        showTransient(tr("截图已保存"));
    else
        showTransient(tr("截图没保存成功，换个位置再试"));

    if (m_view)
        m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionPreviewDialog::setFullscreenPreview(bool on)
{
    // 没有可用模型就不进全屏（按钮此时也是灰的，这里再兜一层）
    if (on && !m_docReady)
        return;
    if (m_fullscreen == on)
        return;

    m_fullscreen = on;

    if (m_card)
    {
        if (on)
        {
            m_card->setMinimumSize(0, 0);
            m_card->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
        else
        {
            m_card->setMaximumSize(kCardMaxWidth, kCardMaxHeight);
            m_card->setMinimumSize(kCardMinWidth, kCardMinHeight);
        }
        // 居中 → 铺满：去掉布局项的对齐，卡片才会吃满整个窗口
        if (m_outer)
            m_outer->setAlignment(m_card, on ? Qt::Alignment() : Qt::AlignCenter);

        // 铺满时把卡片的圆角与描边去掉（QSS 用动态属性区分两种状态）
        m_card->setProperty("fs", on);
        m_card->style()->unpolish(m_card);
        m_card->style()->polish(m_card);
        m_card->update();
    }

    if (m_cardLayout)
        m_cardLayout->setContentsMargins(on ? 10 : 20, on ? 8 : 18,
                                         on ? 10 : 20, on ? 10 : 16);

    // 全屏时画面外框的圆角与描边一起去掉，模型直接铺满窗口
    if (m_stage)
    {
        m_stage->setProperty("fs", on);
        m_stage->style()->unpolish(m_stage);
        m_stage->style()->polish(m_stage);
        m_stage->update();
    }

    // 全屏时标题行与说明文字收起来，画面里只剩投影本身
    if (m_titleBar)
        m_titleBar->setVisible(!on);
    if (m_hint)
        m_hint->setVisible(!on);

    if (m_fullscreenBtn)
    {
        const QSignalBlocker blocker(m_fullscreenBtn);
        m_fullscreenBtn->setChecked(on);
        m_fullscreenBtn->setText(on ? tr("退出全屏") : tr("全屏"));
    }

    // 视口大小变了，重新取景，模型始终完整落在画面里
    QPointer<ProjectionPreviewDialog> guard(this);
    QTimer::singleShot(0, this, [guard]() {
        if (!guard || !guard->m_view || !guard->m_docReady)
            return;
        guard->m_view->focusAll();
        if (guard->m_fullscreen)
            guard->m_view->setFocus(Qt::OtherFocusReason);
    });
}

void ProjectionPreviewDialog::applyLayer(int layer)
{
    if (!m_view || !m_doc || !m_doc->isLoaded())
        return;

    const int count = layerSliceCount();
    if (layer <= 0 || count <= 0)
    {
        // 滑块最左端：显示全部
        m_view->setLayerView(-1, VoxelViewWidget::LayerMode::All);
    }
    else
    {
        const int index = qBound(0, layer - 1, count - 1);
        const bool below = m_belowToggle && m_belowToggle->isChecked();
        m_view->setLayerView(index, below ? VoxelViewWidget::LayerMode::Below
                                          : VoxelViewWidget::LayerMode::Single);
    }
    refreshLayerUi();
}

int ProjectionPreviewDialog::layerSliceCount() const
{
    if (!m_doc || !m_doc->isLoaded())
        return 0;
    switch (m_layerAxis)
    {
    case 1:
        return m_doc->sizeX();
    case 2:
        return m_doc->sizeZ();
    default:
        return m_doc->sizeY();
    }
}

void ProjectionPreviewDialog::applyLayerAxis(int axis)
{
    axis = qBound(0, axis, 2);
    if (axis == m_layerAxis)
        return;

    stopLayerScan();
    m_layerAxis = axis;

    if (m_view)
    {
        VoxelViewWidget::LayerAxis viewAxis = VoxelViewWidget::LayerAxis::Y;
        if (m_layerAxis == 1)
            viewAxis = VoxelViewWidget::LayerAxis::X;
        else if (m_layerAxis == 2)
            viewAxis = VoxelViewWidget::LayerAxis::Z;
        m_view->setLayerAxis(viewAxis);
    }

    if (m_belowToggle)
    {
        const QSignalBlocker blocker(m_belowToggle);
        m_belowToggle->setChecked(false);
        m_belowToggle->setText(m_layerAxis == 0
                                   ? tr("此层以下")
                                   : (m_layerAxis == 1 ? tr("此层以西") : tr("此层以北")));
        m_belowToggle->setToolTip(
            m_layerAxis == 0
                ? tr("勾选后显示该层及以下的全部层（选中层正常、下方调暗），不勾选则只看这一层")
                : (m_layerAxis == 1
                       ? tr("勾选后显示该层及西侧的全部层（选中层正常、西侧调暗），不勾选则只看这一层")
                       : tr("勾选后显示该层及北侧的全部层（选中层正常、北侧调暗），不勾选则只看这一层")));
    }

    if (m_layerSlider)
    {
        const QSignalBlocker blocker(m_layerSlider);
        m_layerSlider->setRange(0, qMax(0, layerSliceCount()));
        m_layerSlider->setValue(0);
        m_layerSlider->setToolTip(
            m_layerAxis == 0
                ? tr("最左为「全部」；往右依次是自下往上的每一层")
                : (m_layerAxis == 1
                       ? tr("最左为「全部」；往右依次是从西往东的每一列")
                       : tr("最左为「全部」；往右依次是从北往南的每一列")));
    }
    // 轴一变，旧的过滤层就没意义了，回到「全部」
    applyLayer(0);

    showTransient(m_layerAxis == 0   ? tr("剖切方向：水平（按层掀开）")
                  : m_layerAxis == 1 ? tr("剖切方向：左右（沿 X 剖开）")
                                     : tr("剖切方向：前后（沿 Z 剖开）"));
}

void ProjectionPreviewDialog::toggleLayerScan(bool on)
{
    if (!on)
    {
        stopLayerScan();
        return;
    }

    if (!m_docReady || !m_layerSlider || layerSliceCount() <= 0)
    {
        if (m_scanBtn)
        {
            const QSignalBlocker blocker(m_scanBtn);
            m_scanBtn->setChecked(false);
        }
        return;
    }

    // 从最顶层开始：滑块先推到最右（整份投影都还在），再逐层往回退
    {
        const QSignalBlocker blocker(m_layerSlider);
        m_layerSlider->setValue(m_layerSlider->maximum());
    }
    if (m_belowToggle)
    {
        const QSignalBlocker blocker(m_belowToggle);
        m_belowToggle->setChecked(true);
    }
    applyLayer(m_layerSlider->value());

    m_scanning = true;
    if (m_scanBtn)
    {
        m_scanBtn->setText(tr("停"));
        m_scanBtn->setToolTip(tr("正在逐层下扫，点一下停止"));
    }
    if (m_scanTimer)
        m_scanTimer->start();
}

void ProjectionPreviewDialog::stopLayerScan()
{
    if (m_scanTimer)
        m_scanTimer->stop();
    m_scanning = false;

    if (m_scanBtn)
    {
        if (m_scanBtn->isChecked())
        {
            const QSignalBlocker blocker(m_scanBtn);
            m_scanBtn->setChecked(false);
        }
        m_scanBtn->setText(tr("扫描"));
        m_scanBtn->setToolTip(
            tr("从最上层自动往下逐层掀开，一次看完建筑内部；再点一下停止"));
    }
}

void ProjectionPreviewDialog::applySpinSpeed()
{
    double degreesPerSecond =
        kDefaultSpinDegreesPerTick * 1000.0 / double(kSpinIntervalMs);
    if (m_speedCombo)
        degreesPerSecond = m_speedCombo->currentData().toDouble();
    m_spinDegreesPerTick = float(degreesPerSecond * (kSpinIntervalMs / 1000.0));
}

void ProjectionPreviewDialog::openMaterialList()
{
    if (m_filePath.isEmpty())
        return;
    ProjectionMaterialDialog::open(this, m_filePath, m_instancePath, subjectName());
    if (m_view)
        m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionPreviewDialog::copyScreenshot()
{
    if (!m_view || !m_docReady)
        return;
    const QImage shot = m_view->grabFramebuffer();
    if (shot.isNull())
    {
        showTransient(tr("画面还没准备好，稍后再试"));
        return;
    }
    QApplication::clipboard()->setImage(shot);
    showTransient(tr("当前画面已复制到剪贴板"));
    if (m_view)
        m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionPreviewDialog::showShortcutHelp()
{
    AppMessageBox::information(
        this, tr("投影预览 · 快捷键"),
        tr("视角\n"
           "  按住鼠标拖动：转向\n"
           "  WASD：前后左右飞行，空格 / Shift：上升 / 下降\n"
           "  双击 W 或按住 Ctrl：快速飞行，滚轮：沿视线前后\n"
           "  1~7：等距 / 正前 / 正后 / 左侧 / 右侧 / 俯视 / 仰视\n"
           "  R：重置视角，把整份投影重新收进画面\n"
           "  F 或双击画面：全屏 / 退出全屏（Esc 先退全屏）\n\n"
           "查看与导出\n"
           "  P 或 Ctrl+S：把当前画面存成 PNG\n"
           "  Ctrl+C：把当前画面复制到剪贴板\n"
           "  鼠标停在方块上：显示方块名与坐标\n\n"
           "剖切\n"
           "  层数条：只看某一层，或勾选「此层以下」\n"
           "  「扫描」：从最上层自动往下逐层掀开\n"
           "  「更多 → 剖切方向」：水平 / 左右 / 前后\n"));
    if (m_view)
        m_view->setFocus(Qt::OtherFocusReason);
}

void ProjectionPreviewDialog::refreshLayerUi()
{
    if (!m_doc || !m_doc->isLoaded())
        return;

    const int count = layerSliceCount();
    const int value = m_layerSlider ? m_layerSlider->value() : 0;
    if (m_layerValue)
    {
        m_layerValue->setText(value <= 0
                                  ? tr("全部（%1 层）").arg(count)
                                  : tr("第 %1 / %2 层").arg(value).arg(count));
    }
    if (m_layerDown)
        m_layerDown->setEnabled(value > 0);
    if (m_layerUp)
        m_layerUp->setEnabled(value < count);
}

void ProjectionPreviewDialog::updateStatsLabel()
{
    if (!m_statsLabel)
        return;

    QStringList parts;
    if (m_sizeX > 0 && m_sizeY > 0 && m_sizeZ > 0)
        parts << QStringLiteral("%1×%2×%3").arg(m_sizeX).arg(m_sizeY).arg(m_sizeZ);
    if (m_blocks > 0)
        parts << tr("%1 方块").arg(QLocale().toString(m_blocks));
    if (m_materialKinds > 0)
        parts << tr("%1 种材料").arg(m_materialKinds);
    if (m_entityCount > 0)
    {
        const bool shown = m_view != nullptr && m_view->showEntities()
                           && m_view->entityTextures() != nullptr;
        parts << (shown ? tr("%1 个生物").arg(QLocale().toString(m_entityCount))
                        : tr("%1 个生物（已隐藏）").arg(QLocale().toString(m_entityCount)));
    }
    if (m_docReady && m_view)
        parts << tr("%1 面").arg(QLocale().toString(m_view->quadCount()));

    m_statsLabel->setText(parts.join(QStringLiteral(" · ")));
    m_statsLabel->setVisible(!parts.isEmpty());
}

bool ProjectionPreviewDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            // 手一拖就停自动旋转，免得和自己转视角打架
            if (m_spinBtn && m_spinBtn->isChecked())
                m_spinBtn->setChecked(false);
        }
        else if (event->type() == QEvent::MouseButtonDblClick)
        {
            // 双击视图：全屏 / 退出全屏
            setFullscreenPreview(!m_fullscreen);
            return true;
        }
    }
    return AppDialogBase::eventFilter(watched, event);
}

void ProjectionPreviewDialog::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();

    // 全屏预览时 Esc 先退出全屏，而不是直接关掉弹窗
    if (key == Qt::Key_Escape || key == Qt::Key_Back)
    {
        if (m_fullscreen)
        {
            setFullscreenPreview(false);
            event->accept();
            return;
        }
        AppDialogBase::keyPressEvent(event);
        return;
    }

    // 快捷键说明：F1，或问号（Shift + /）
    if (key == Qt::Key_F1 || key == Qt::Key_Question
        || (key == Qt::Key_Slash && (event->modifiers() & Qt::ShiftModifier)))
    {
        showShortcutHelp();
        event->accept();
        return;
    }

    // 复制画面：Ctrl+C
    if (event->matches(QKeySequence::Copy))
    {
        copyScreenshot();
        event->accept();
        return;
    }

    // 截图：P（Ctrl+S 在视图里会先被 WASD 相关的按键吃掉，所以主推 P）
    if (key == Qt::Key_P || event->matches(QKeySequence::Save))
    {
        saveScreenshot();
        event->accept();
        return;
    }

    switch (key)
    {
    case Qt::Key_F:
        if (m_docReady)
            setFullscreenPreview(!m_fullscreen);
        event->accept();
        return;
    case Qt::Key_R:
        resetViewToDefault();
        event->accept();
        return;
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
        if (m_docReady && m_viewCombo)
        {
            const int index = key - Qt::Key_1;
            if (index < m_viewCombo->count())
                m_viewCombo->setCurrentIndex(index);
        }
        event->accept();
        return;
    default:
        break;
    }

    AppDialogBase::keyPressEvent(event);
}
