#include "ProjectionBuildGuideDialog.h"

#include "LayerBuildPlanWidget.h"
#include "components/VoxelViewWidget.h"
#include "utils/IconHelper.h"
#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/Schematic/InstanceBlockNames.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/LitematicEditor.h"
#include "utils/Schematic/SchematicDocument.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QComboBox>
#include <QCoreApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QSplitter>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVBoxLayout>
#include <QVector>

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {

/** 方块 ID 去掉命名空间：minecraft:oak_planks -> oak_planks */
QString shortBlockId(const QString &blockName)
{
    QString id = blockName;
    const int colon = id.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        id = id.mid(colon + 1);
    return id;
}

bool isAirState(const SchematicBlockState &state)
{
    return state.isAir()
           || state.name == QLatin1String("minecraft:cave_air")
           || state.name == QLatin1String("minecraft:void_air");
}

/**
 * @brief 材料小图标：有实例材质时用真实贴图，否则回退到方块代表色
 *
 * 两种都裁成圆角小方块，步骤横幅和搭建顺序列表共用同一套观感。
 */
QPixmap materialSwatch(const QString &blockName, int size, InstanceTextureLoader *loader)
{
    const int dpr = 2;
    QPixmap pm(size * dpr, size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box(1, 1, size - 2, size - 2);
    QPainterPath clip;
    clip.addRoundedRect(box, 4, 4);

    bool drewTexture = false;
    if (loader && loader->isValid())
    {
        const QIcon icon = loader->blockIcon(blockName, size * dpr);
        if (!icon.isNull())
        {
            const QPixmap texture = icon.pixmap(size * dpr, size * dpr);
            if (!texture.isNull())
            {
                p.save();
                p.setClipPath(clip);
                p.drawPixmap(QRectF(1, 1, size - 2, size - 2), texture,
                             QRectF(texture.rect()));
                p.restore();
                drewTexture = true;
            }
        }
    }

    if (!drewTexture)
    {
        QColor color = VoxelViewWidget::blockColorOf(blockName);
        if (!color.isValid())
            color = QColor(150, 150, 150);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(clip);
    }

    p.setPen(QPen(QColor(255, 255, 255, 95), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box, 4, 4);
    p.end();
    return pm;
}

} // namespace

void ProjectionBuildGuideDialog::open(QWidget *parent, const QString &filePath,
                                      const QString &instancePath,
                                      const QString &displayName)
{
    auto *dialog = new ProjectionBuildGuideDialog(parent ? parent->window() : nullptr,
                                                  filePath, instancePath, displayName);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

ProjectionBuildGuideDialog::ProjectionBuildGuideDialog(QWidget *parent,
                                                       const QString &filePath,
                                                       const QString &instancePath,
                                                       const QString &displayName)
    : QDialog(parent)
    , m_filePath(filePath)
    , m_instancePath(instancePath)
    , m_displayName(displayName)
{
    setObjectName(QStringLiteral("ProjectionBuildGuideDialog"));
    setWindowTitle(tr("学习搭建 - %1").arg(
        m_displayName.trimmed().isEmpty() ? QFileInfo(filePath).completeBaseName() : m_displayName));
    setMinimumSize(1080, 700);
    resize(1240, 820);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setModal(false);

    initUI();
    applyChrome();

    if (ThemeManager *tm = ThemeManager::instance())
    {
        connect(tm, &ThemeManager::themeChanged, this, [this]() {
            applyChrome();
            if (m_docReady)
                rebuildStepTree();
        });
        connect(tm, &ThemeManager::themeColorChanged, this, [this]() {
            applyChrome();
            if (m_docReady)
                rebuildStepTree();
        });
    }
}

ProjectionBuildGuideDialog::~ProjectionBuildGuideDialog()
{
    // 视图只持有文档的裸指针，析构时先摘掉，避免控件销毁阶段碰到已释放的文档
    if (m_view)
        m_view->setDocument(nullptr);
    if (m_plan)
        m_plan->setDocument(nullptr);
    m_doc.reset();
}

void ProjectionBuildGuideDialog::initUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // ── 顶部：标题 + 文件 + 尺寸徽章 ──────────────────────────────────
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("buildGuideCard"));
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(16, 12, 16, 12);
    headerLay->setSpacing(10);

    {
        auto *logo = new QLabel(header);
        const QColor themeColor(ThemeManager::instance()->currentThemeColor());
        logo->setPixmap(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/grid.svg"), themeColor, 22).pixmap(22, 22));
        logo->setFixedSize(22, 22);
        headerLay->addWidget(logo, 0, Qt::AlignVCenter);

        auto *title = new QLabel(tr("学习搭建"), header);
        title->setObjectName(QStringLiteral("buildGuideTitle"));
        headerLay->addWidget(title, 0, Qt::AlignVCenter);

        m_fileLabel = new QLabel(header);
        m_fileLabel->setObjectName(QStringLiteral("buildGuideSubtitle"));
        m_fileLabel->setToolTip(m_filePath);
        m_fileLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        headerLay->addWidget(m_fileLabel, 1, Qt::AlignVCenter);

        m_sizeChip = new QLabel(tr("尺寸 —"), header);
        m_layerChip = new QLabel(tr("层数 —"), header);
        m_blockChip = new QLabel(tr("方块 —"), header);
        const QList<QLabel *> chips{ m_sizeChip, m_layerChip, m_blockChip };
        for (QLabel *chip : chips)
        {
            chip->setObjectName(QStringLiteral("buildGuideChip"));
            headerLay->addWidget(chip, 0, Qt::AlignVCenter);
        }
    }
    root->addWidget(header);

    // ── 当前步骤横幅 ─────────────────────────────────────────────────
    m_stepCard = new QWidget(this);
    m_stepCard->setObjectName(QStringLiteral("buildGuideStepCard"));
    auto *stepLay = new QHBoxLayout(m_stepCard);
    stepLay->setContentsMargins(16, 12, 16, 12);
    stepLay->setSpacing(16);

    auto *stepLeft = new QVBoxLayout();
    stepLeft->setSpacing(5);

    m_stepLabel = new QLabel(tr("正在准备…"), m_stepCard);
    m_stepLabel->setObjectName(QStringLiteral("buildGuideStepLabel"));
    stepLeft->addWidget(m_stepLabel);

    auto *materialRow = new QHBoxLayout();
    materialRow->setSpacing(8);
    m_materialSwatch = new QLabel(m_stepCard);
    m_materialSwatch->setFixedSize(18, 18);
    materialRow->addWidget(m_materialSwatch, 0, Qt::AlignVCenter);

    m_materialLabel = new QLabel(tr("—"), m_stepCard);
    m_materialLabel->setObjectName(QStringLiteral("buildGuideMaterial"));
    m_materialLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    materialRow->addWidget(m_materialLabel, 0, Qt::AlignVCenter);

    m_materialIdLabel = new QLabel(m_stepCard);
    m_materialIdLabel->setObjectName(QStringLiteral("buildGuideMuted"));
    m_materialIdLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    materialRow->addWidget(m_materialIdLabel, 1, Qt::AlignVCenter);
    stepLeft->addLayout(materialRow);

    m_stepHint = new QLabel(m_stepCard);
    m_stepHint->setObjectName(QStringLiteral("buildGuideMuted"));
    m_stepHint->setWordWrap(true);
    stepLeft->addWidget(m_stepHint);

    stepLay->addLayout(stepLeft, 1);

    auto *stepRight = new QVBoxLayout();
    stepRight->setSpacing(6);
    stepRight->addStretch(1);
    m_progressLabel = new QLabel(tr("已完成 0 / 0 步"), m_stepCard);
    m_progressLabel->setObjectName(QStringLiteral("buildGuideMuted"));
    m_progressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stepRight->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(m_stepCard);
    m_progressBar->setObjectName(QStringLiteral("buildGuideProgress"));
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedSize(230, 8);
    stepRight->addWidget(m_progressBar, 0, Qt::AlignRight);
    stepRight->addStretch(1);
    stepLay->addLayout(stepRight, 0);

    root->addWidget(m_stepCard);

    // ── 主体：平面图 + 右侧立体图 / 搭建顺序 ─────────────────────────
    auto *mainSplit = new QSplitter(Qt::Horizontal, this);
    mainSplit->setChildrenCollapsible(false);

    auto *planCard = new QWidget(mainSplit);
    planCard->setObjectName(QStringLiteral("buildGuideCard"));
    auto *planLay = new QVBoxLayout(planCard);
    planLay->setContentsMargins(12, 10, 12, 12);
    planLay->setSpacing(8);

    auto *planTitleRow = new QHBoxLayout();
    planTitleRow->setSpacing(8);
    auto *planTitle = new QLabel(tr("本层平面图"), planCard);
    planTitle->setObjectName(QStringLiteral("buildGuideSectionTitle"));
    planTitleRow->addWidget(planTitle, 0, Qt::AlignVCenter);
    auto *planLegend = new QLabel(tr("琥珀色格 = 这一步要放的方块"), planCard);
    planLegend->setObjectName(QStringLiteral("buildGuideMuted"));
    planTitleRow->addWidget(planLegend, 0, Qt::AlignVCenter);
    planTitleRow->addStretch(1);

    auto *fitBtn = new QPushButton(tr("适应"), planCard);
    fitBtn->setObjectName(QStringLiteral("buildGuideMiniBtn"));
    fitBtn->setToolTip(tr("把整层缩放到刚好放进窗口（也可以双击平面图或按 0）"));
    connect(fitBtn, &QPushButton::clicked, this, [this]() { m_plan->fitToView(); });
    planTitleRow->addWidget(fitBtn, 0, Qt::AlignVCenter);

    auto *zoomOutBtn = new QPushButton(QStringLiteral("−"), planCard);
    zoomOutBtn->setObjectName(QStringLiteral("buildGuideMiniBtn"));
    zoomOutBtn->setToolTip(tr("缩小"));
    connect(zoomOutBtn, &QPushButton::clicked, this, [this]() {
        m_plan->setFocus(Qt::OtherFocusReason);
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Minus, Qt::NoModifier);
        QApplication::sendEvent(m_plan, &event);
    });
    planTitleRow->addWidget(zoomOutBtn, 0, Qt::AlignVCenter);

    auto *zoomInBtn = new QPushButton(QStringLiteral("+"), planCard);
    zoomInBtn->setObjectName(QStringLiteral("buildGuideMiniBtn"));
    zoomInBtn->setToolTip(tr("放大"));
    connect(zoomInBtn, &QPushButton::clicked, this, [this]() {
        m_plan->setFocus(Qt::OtherFocusReason);
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Plus, Qt::NoModifier);
        QApplication::sendEvent(m_plan, &event);
    });
    planTitleRow->addWidget(zoomInBtn, 0, Qt::AlignVCenter);
    planLay->addLayout(planTitleRow);

    m_plan = new LayerBuildPlanWidget(planCard);
    connect(m_plan, &LayerBuildPlanWidget::cellHovered,
            this, &ProjectionBuildGuideDialog::updateHoverHint);
    planLay->addWidget(m_plan, 1);
    mainSplit->addWidget(planCard);

    auto *rightColumn = new QWidget(mainSplit);
    auto *rightLay = new QVBoxLayout(rightColumn);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(10);

    auto *viewCard = new QWidget(rightColumn);
    viewCard->setObjectName(QStringLiteral("buildGuideCard"));
    auto *viewLay = new QVBoxLayout(viewCard);
    viewLay->setContentsMargins(12, 10, 12, 12);
    viewLay->setSpacing(8);

    auto *viewTitleRow = new QHBoxLayout();
    viewTitleRow->setSpacing(8);
    auto *viewTitle = new QLabel(tr("本层立体图"), viewCard);
    viewTitle->setObjectName(QStringLiteral("buildGuideSectionTitle"));
    viewTitleRow->addWidget(viewTitle, 0, Qt::AlignVCenter);
    auto *viewHint = new QLabel(tr("只看这一层"), viewCard);
    viewHint->setObjectName(QStringLiteral("buildGuideMuted"));
    viewTitleRow->addWidget(viewHint, 0, Qt::AlignVCenter);
    viewTitleRow->addStretch(1);

    m_viewCombo = new QComboBox(viewCard);
    m_viewCombo->setObjectName(QStringLiteral("buildGuideCombo"));
    m_viewCombo->addItem(tr("等距"), int(VoxelViewWidget::ViewPreset::Iso));
    m_viewCombo->addItem(tr("俯视"), int(VoxelViewWidget::ViewPreset::Top));
    m_viewCombo->addItem(tr("正前"), int(VoxelViewWidget::ViewPreset::Front));
    m_viewCombo->addItem(tr("正后"), int(VoxelViewWidget::ViewPreset::Back));
    m_viewCombo->addItem(tr("左侧"), int(VoxelViewWidget::ViewPreset::Left));
    m_viewCombo->addItem(tr("右侧"), int(VoxelViewWidget::ViewPreset::Right));
    connect(m_viewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (!m_view || index < 0)
            return;
        const int preset = m_viewCombo->itemData(index).toInt();
        m_view->applyViewPreset(static_cast<VoxelViewWidget::ViewPreset>(preset));
    });
    viewTitleRow->addWidget(m_viewCombo, 0, Qt::AlignVCenter);

    auto *viewFitBtn = new QPushButton(tr("适应本层"), viewCard);
    viewFitBtn->setObjectName(QStringLiteral("buildGuideMiniBtn"));
    viewFitBtn->setToolTip(tr("把这一层重新收进立体图"));
    connect(viewFitBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentStep >= 0 && m_currentStep < m_steps.size())
        {
            const QSet<int> cells = layerCells(m_steps.at(m_currentStep).layerY);
            if (!cells.isEmpty())
                m_view->focusOnCells(cells);
        }
    });
    viewTitleRow->addWidget(viewFitBtn, 0, Qt::AlignVCenter);
    viewLay->addLayout(viewTitleRow);

    m_view = new VoxelViewWidget(viewCard);
    m_view->setObjectName(QStringLiteral("buildGuideVoxelView"));
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_view->setMinimumHeight(210);
    m_view->setLeftAction(VoxelViewWidget::LeftAction::Select);
    m_view->setShowAir(false);
    // 这个窗口只读：用户点到方块只会发信号，这里有意不接任何编辑槽
    viewLay->addWidget(m_view, 1);
    rightLay->addWidget(viewCard, 1);

    auto *stepsCard = new QWidget(rightColumn);
    stepsCard->setObjectName(QStringLiteral("buildGuideCard"));
    auto *stepsLay = new QVBoxLayout(stepsCard);
    stepsLay->setContentsMargins(12, 10, 12, 12);
    stepsLay->setSpacing(8);

    auto *stepsTitleRow = new QHBoxLayout();
    stepsTitleRow->setSpacing(8);
    auto *stepsTitle = new QLabel(tr("搭建顺序"), stepsCard);
    stepsTitle->setObjectName(QStringLiteral("buildGuideSectionTitle"));
    stepsTitleRow->addWidget(stepsTitle, 0, Qt::AlignVCenter);
    auto *stepsHint = new QLabel(tr("每层从数量最多的材料开始"), stepsCard);
    stepsHint->setObjectName(QStringLiteral("buildGuideMuted"));
    stepsTitleRow->addWidget(stepsHint, 0, Qt::AlignVCenter);
    stepsTitleRow->addStretch(1);
    stepsLay->addLayout(stepsTitleRow);

    m_tree = new QTreeWidget(stepsCard);
    m_tree->setObjectName(QStringLiteral("buildGuideTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({ tr("材料"), tr("数量") });
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setMinimumHeight(170);
    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int column) {
        Q_UNUSED(column);
        if (!item)
            return;
        const int stepIndex = item->data(0, Qt::UserRole).toInt();
        if (stepIndex >= 0 && stepIndex < m_steps.size())
            goToStep(stepIndex);
    });
    stepsLay->addWidget(m_tree, 1);
    rightLay->addWidget(stepsCard, 1);

    mainSplit->addWidget(rightColumn);
    mainSplit->setStretchFactor(0, 3);
    mainSplit->setStretchFactor(1, 2);
    mainSplit->setSizes({ 760, 430 });
    root->addWidget(mainSplit, 1);

    // ── 底部：提示 + 步骤按钮 ────────────────────────────────────────
    auto *footer = new QHBoxLayout();
    footer->setSpacing(8);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("buildGuideMuted"));
    m_hintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_hintLabel->setText(tr("平面图滚轮缩放、按住拖动平移、双击或按 0 回到适应窗口；"
                            "立体图可拖动转向、滚轮前后移动。"));
    footer->addWidget(m_hintLabel, 1);

    m_restartBtn = new QPushButton(tr("从头开始"), this);
    m_restartBtn->setObjectName(QStringLiteral("buildGuideGhostBtn"));
    m_restartBtn->setCursor(Qt::PointingHandCursor);
    m_restartBtn->setToolTip(tr("回到第一步；已搭到哪一步只记在当前窗口，不会动投影文件"));
    connect(m_restartBtn, &QPushButton::clicked, this, &ProjectionBuildGuideDialog::restart);
    footer->addWidget(m_restartBtn);

    m_prevBtn = new QPushButton(tr("上一步"), this);
    m_prevBtn->setObjectName(QStringLiteral("buildGuideBtn"));
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentStep > 0)
            goToStep(m_currentStep - 1);
    });
    footer->addWidget(m_prevBtn);

    m_skipBtn = new QPushButton(tr("跳过"), this);
    m_skipBtn->setObjectName(QStringLiteral("buildGuideGhostBtn"));
    m_skipBtn->setCursor(Qt::PointingHandCursor);
    m_skipBtn->setToolTip(tr("这一种材料暂时不搭，先看下一种"));
    connect(m_skipBtn, &QPushButton::clicked, this, &ProjectionBuildGuideDialog::advanceStep);
    footer->addWidget(m_skipBtn);

    m_nextBtn = new QPushButton(tr("这一步搭完了，下一步"), this);
    m_nextBtn->setObjectName(QStringLiteral("buildGuidePrimaryBtn"));
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    m_nextBtn->setDefault(true);
    connect(m_nextBtn, &QPushButton::clicked, this, &ProjectionBuildGuideDialog::advanceStep);
    footer->addWidget(m_nextBtn);

    m_closeBtn = new QPushButton(tr("关闭"), this);
    m_closeBtn->setObjectName(QStringLiteral("buildGuideBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    footer->addWidget(m_closeBtn);

    root->addLayout(footer);

    setControlsEnabled(false);

    m_fileLabel->setText(m_displayName.trimmed().isEmpty()
                             ? QFileInfo(m_filePath).completeBaseName()
                             : m_displayName);
}

void ProjectionBuildGuideDialog::applyChrome()
{
    setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QDialog#ProjectionBuildGuideDialog {"
        "  background-color: @BG_BASE@;"
        "}"
        "QWidget#buildGuideCard,"
        "QWidget#buildGuideStepCard {"
        "  background-color: @BG_CARD@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 12px;"
        "}"
        "QWidget#buildGuideStepCard {"
        "  border-color: @BORDER_STRONG@;"
        "}"
        "QLabel#buildGuideTitle {"
        "  color: @TEXT_PRIMARY@;"
        "  font-size: 16px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#buildGuideSubtitle {"
        "  color: @TEXT_TERTIARY@;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#buildGuideSectionTitle {"
        "  color: @TEXT_PRIMARY@;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#buildGuideStepLabel {"
        "  color: @TEXT_SECONDARY@;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#buildGuideMaterial {"
        "  color: @TEXT_PRIMARY@;"
        "  font-size: 20px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#buildGuideMuted {"
        "  color: @TEXT_TERTIARY@;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#buildGuideChip {"
        "  background-color: @BG_CONTENT@;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 9px;"
        "  padding: 3px 9px;"
        "  font-size: 11px;"
        "}"
        "QPushButton#buildGuidePrimaryBtn {"
        "  background-color: @PRIMARY@;"
        "  color: @TEXT_ON_PRIMARY@;"
        "  border: none;"
        "  border-radius: 9px;"
        "  padding: 8px 18px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#buildGuidePrimaryBtn:hover {"
        "  background-color: @PRIMARY_HOVER@;"
        "}"
        "QPushButton#buildGuidePrimaryBtn:pressed {"
        "  background-color: @PRIMARY_PRESSED@;"
        "}"
        "QPushButton#buildGuidePrimaryBtn:disabled {"
        "  background-color: @BG_DISABLED@;"
        "  color: @TEXT_DISABLED@;"
        "}"
        "QPushButton#buildGuideBtn {"
        "  background-color: @BG_CONTENT@;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 9px;"
        "  padding: 8px 15px;"
        "  font-size: 13px;"
        "}"
        "QPushButton#buildGuideBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  border-color: @BORDER_STRONG@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#buildGuideBtn:pressed {"
        "  background-color: @BG_PRESSED@;"
        "}"
        "QPushButton#buildGuideBtn:disabled {"
        "  color: @TEXT_DISABLED@;"
        "  border-color: @BORDER_DISABLED@;"
        "  background-color: @BG_DISABLED@;"
        "}"
        "QPushButton#buildGuideGhostBtn {"
        "  background: transparent;"
        "  color: @TEXT_TERTIARY@;"
        "  border: none;"
        "  border-radius: 8px;"
        "  padding: 7px 10px;"
        "  font-size: 12px;"
        "}"
        "QPushButton#buildGuideGhostBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QPushButton#buildGuideMiniBtn {"
        "  background-color: @BG_CONTENT@;"
        "  color: @TEXT_SECONDARY@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 7px;"
        "  padding: 4px 9px;"
        "  font-size: 11px;"
        "}"
        "QPushButton#buildGuideMiniBtn:hover {"
        "  background-color: @BG_HOVER@;"
        "  border-color: @BORDER_STRONG@;"
        "  color: @TEXT_PRIMARY@;"
        "}"
        "QComboBox#buildGuideCombo {"
        "  min-height: 24px;"
        "  padding: 2px 22px 2px 9px;"
        "  border-radius: 7px;"
        "  font-size: 11px;"
        "}"
        "QTreeWidget#buildGuideTree {"
        "  background: transparent;"
        "  border: none;"
        "  font-size: 12px;"
        "}"
        "QTreeWidget#buildGuideTree::item {"
        "  padding: 4px 2px;"
        "  border-radius: 6px;"
        "}"
        "QTreeWidget#buildGuideTree::item:hover {"
        "  background-color: @BG_HOVER@;"
        "}"
        "QProgressBar#buildGuideProgress {"
        "  background-color: @BG_CONTENT@;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QProgressBar#buildGuideProgress::chunk {"
        "  background-color: @PRIMARY@;"
        "  border-radius: 4px;"
        "}"
        "QWidget#buildGuideVoxelView {"
        "  border-radius: 8px;"
        "}"
        "QSplitter::handle {"
        "  background: transparent;"
        "}"
    )));
}

void ProjectionBuildGuideDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!m_loadStarted)
        startLoad();
}

void ProjectionBuildGuideDialog::startLoad()
{
    if (m_loadStarted || m_loading)
        return;

    m_loadStarted = true;
    m_loading = true;

    m_plan->setPlaceholder(tr("正在解析投影文件…"));
    m_stepLabel->setText(tr("正在解析投影…"));
    m_materialLabel->setText(tr("准备中…"));
    m_materialIdLabel->clear();
    m_stepHint->setText(tr("解析完成后会先显示最下面一层，并从这一层数量最多的材料开始。"));
    m_progressLabel->setText(tr("已完成 0 / 0 步"));
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_view->hide();
    setControlsEnabled(false);

    const QString filePath = m_filePath;
    QPointer<ProjectionBuildGuideDialog> guard(this);
    (void)QtConcurrent::run([guard, filePath]() {
        const LoadResult result = loadProjection(filePath);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, result]() {
                if (!guard)
                    return;
                guard->applyLoad(result);
            },
            Qt::QueuedConnection);
    });
}

ProjectionBuildGuideDialog::LoadResult
ProjectionBuildGuideDialog::loadProjection(const QString &filePath)
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
        result.error = tr("这种格式暂时不能学习搭建，"
                          "只有 .litematic / .schematic / .schem 能打开。");
        return result;
    }

    QString parsePath = filePath;
    bool tempFile = false;
    if (!isLitematic)
    {
        const QString tmpDir =
            QDir::tempPath() + QStringLiteral("/blockbox_projection_build_guide");
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
            result.error = tr("这份投影转换不出方块数据，可能文件已损坏：\n%1")
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

    const int sizeY = doc->sizeY();
    const int layerSize = doc->layerSize();
    if (sizeY <= 0 || layerSize <= 0)
    {
        result.error = tr("这份投影没有可搭建的空间。");
        return result;
    }

    // 调色板先翻成方块名，逐格扫描时只按下标查表
    QVector<QString> paletteNames(doc->paletteSize());
    QVector<bool> paletteIsAir(doc->paletteSize(), true);
    for (int i = 0; i < doc->paletteSize(); ++i)
    {
        const SchematicBlockState &state = doc->paletteEntry(i);
        paletteNames[i] = state.name;
        paletteIsAir[i] = isAirState(state);
    }

    QVector<QHash<QString, QVector<int>>> layerMaps(sizeY);
    QVector<int> layerCounts(sizeY, 0);
    for (int y = 0; y < sizeY; ++y)
    {
        const int base = y * layerSize;
        for (int i = 0; i < layerSize; ++i)
        {
            const int index = base + i;
            const int paletteIndex = doc->paletteIndexAt(index);
            if (paletteIndex <= 0 || paletteIndex >= paletteIsAir.size()
                || paletteIsAir.at(paletteIndex))
                continue;

            layerMaps[y][paletteNames.at(paletteIndex)].append(index);
            ++layerCounts[y];
        }
    }

    QVector<ProjectionBuildStep> steps;
    QVector<ProjectionBuildLayer> layers;
    steps.reserve(64);
    layers.reserve(sizeY);
    int totalBlocks = 0;

    for (int y = 0; y < sizeY; ++y)
    {
        if (layerCounts.at(y) <= 0)
            continue;

        QHash<QString, QVector<int>> &map = layerMaps[y];

        // 本层按材料数量从多到少排；数量一样时按方块 ID 排，顺序稳定
        QVector<QPair<int, QString>> order;
        order.reserve(map.size());
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            order.append(qMakePair(it.value().size(), it.key()));
        std::sort(order.begin(), order.end(),
                  [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                      if (a.first != b.first)
                          return a.first > b.first;
                      return a.second < b.second;
                  });

        ProjectionBuildLayer layer;
        layer.y = y;
        layer.ordinal = layers.size();
        layer.blockCount = layerCounts.at(y);

        for (int i = 0; i < order.size(); ++i)
        {
            ProjectionBuildStep step;
            step.layerY = y;
            step.layerOrdinal = layer.ordinal;
            step.blockName = order.at(i).second;
            step.cells = map.take(step.blockName);
            step.count = step.cells.size();
            step.indexInLayer = i;
            step.stepCountInLayer = order.size();
            layer.stepIndices.append(steps.size());
            steps.append(step);
        }

        totalBlocks += layer.blockCount;
        layers.append(layer);
    }

    if (steps.isEmpty())
    {
        result.error = tr("这份投影里没有可搭建的方块。");
        return result;
    }

    result.doc = doc;
    result.steps = steps;
    result.layers = layers;
    result.totalBlocks = totalBlocks;
    return result;
}

void ProjectionBuildGuideDialog::applyLoad(const LoadResult &result)
{
    m_loading = false;

    if (!result.doc)
    {
        m_docReady = false;
        m_view->hide();
        m_plan->setPlaceholder(result.error.isEmpty() ? tr("无法解析这份投影。") : result.error);
        m_stepLabel->setText(tr("打不开这份投影"));
        m_materialLabel->setText(tr("—"));
        m_materialIdLabel->clear();
        m_stepHint->setText(result.error);
        m_progressLabel->setText(tr("已完成 0 / 0 步"));
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(0);
        setControlsEnabled(false);
        return;
    }

    m_doc = result.doc;
    m_steps = result.steps;
    m_layers = result.layers;
    m_totalBlocks = result.totalBlocks;
    m_docReady = true;
    m_completed = false;
    m_currentStep = 0;
    m_lastShownLayer = -1;
    m_layerCellCache.clear();

    // 先挂材质/形状，再挂文档，避免整份几何重建两次
    InstanceTextureLoader *textureLoader = nullptr;
    BlockShapeLoader *shapeLoader = nullptr;
    if (!m_instancePath.isEmpty())
    {
        textureLoader = InstanceTextureLoader::sharedForInstance(m_instancePath);
        shapeLoader = BlockShapeLoader::sharedForInstance(m_instancePath);
        if (textureLoader && m_view->textureLoader() != textureLoader)
            m_view->setTextureLoader(textureLoader);
        if (shapeLoader && m_view->shapeLoader() != shapeLoader)
            m_view->setShapeLoader(shapeLoader);
    }

    // 平面图也铺同一份实例材质；取不到时控件内部自动回退到方块代表色
    m_textureLoader = textureLoader;
    m_plan->setTextureLoader(textureLoader);

    // 先只建最下面一层，再挂文档：大投影不会在打开时白建整份几何
    m_view->setLayerView(m_steps.first().layerY, VoxelViewWidget::LayerMode::Single);
    m_view->setDocument(m_doc.get());
    m_plan->setDocument(m_doc.get());
    m_sizeChip->setText(tr("尺寸 %1 × %2 × %3")
                            .arg(m_doc->sizeX()).arg(m_doc->sizeY()).arg(m_doc->sizeZ()));
    m_layerChip->setText(tr("%1 层").arg(m_layers.size()));
    m_blockChip->setText(tr("%1 个方块").arg(m_totalBlocks));
    setWindowTitle(tr("学习搭建 - %1").arg(subjectName()));

    m_view->show();
    m_progressBar->setRange(0, m_steps.size());
    m_progressBar->setValue(0);

    rebuildStepTree();
    setControlsEnabled(true);
    goToStep(0);

    startBlockNamesLoad();
}

QString ProjectionBuildGuideDialog::subjectName() const
{
    if (!m_displayName.trimmed().isEmpty())
        return m_displayName.trimmed();
    if (m_doc && !m_doc->name().trimmed().isEmpty())
        return m_doc->name().trimmed();
    return QFileInfo(m_filePath).completeBaseName();
}

QString ProjectionBuildGuideDialog::materialPrimary(const QString &blockName) const
{
    const QString cn = InstanceBlockNames::lookup(m_cnMap, blockName);
    if (!cn.isEmpty())
        return cn;
    return shortBlockId(blockName);
}

QString ProjectionBuildGuideDialog::materialSecondary(const QString &blockName) const
{
    const QString shortId = shortBlockId(blockName);
    const QString cn = InstanceBlockNames::lookup(m_cnMap, blockName);
    if (cn.isEmpty())
        return QString();
    return shortId;
}

QSet<int> ProjectionBuildGuideDialog::targetCellSet() const
{
    QSet<int> set;
    if (m_currentStep < 0 || m_currentStep >= m_steps.size())
        return set;
    const QVector<int> &cells = m_steps.at(m_currentStep).cells;
    set.reserve(cells.size());
    for (int index : cells)
        set.insert(index);
    return set;
}

QSet<int> ProjectionBuildGuideDialog::layerCells(int layerY)
{
    const auto cached = m_layerCellCache.constFind(layerY);
    if (cached != m_layerCellCache.constEnd())
        return cached.value();

    QSet<int> cells;
    if (!m_doc || !m_doc->isLoaded() || layerY < 0 || layerY >= m_doc->sizeY())
        return cells;

    const int layerSize = m_doc->layerSize();
    const int base = layerY * layerSize;
    for (int i = 0; i < layerSize; ++i)
    {
        const int index = base + i;
        if (m_doc->paletteIndexAt(index) > 0)
            cells.insert(index);
    }
    m_layerCellCache.insert(layerY, cells);
    return cells;
}

void ProjectionBuildGuideDialog::goToStep(int index)
{
    if (!m_docReady || m_steps.isEmpty())
        return;

    if (index >= m_steps.size())
    {
        m_currentStep = m_steps.size();
        m_completed = true;
        showCompleted();
        return;
    }

    m_completed = false;
    m_currentStep = qBound(0, index, m_steps.size() - 1);
    refreshStepUi();
}

void ProjectionBuildGuideDialog::refreshStepUi()
{
    if (!m_docReady || m_steps.isEmpty() || m_completed
        || m_currentStep < 0 || m_currentStep >= m_steps.size())
        return;

    const ProjectionBuildStep &step = m_steps.at(m_currentStep);
    const QSet<int> targetCells = targetCellSet();
    const bool layerChanged = (step.layerY != m_lastShownLayer);

    m_plan->setOverlayMessage(QString());
    m_plan->setStep(step.layerY, targetCells);

    if (layerChanged)
    {
        m_view->setLayerView(step.layerY, VoxelViewWidget::LayerMode::Single);
        m_view->setHighlightCells(targetCells);
        const QSet<int> allCells = layerCells(step.layerY);
        m_view->focusOnCells(allCells.isEmpty() ? targetCells : allCells);
        m_lastShownLayer = step.layerY;
    }
    else
    {
        m_view->setHighlightCells(targetCells);
    }

    const QString primary = materialPrimary(step.blockName);
    const QString secondary = materialSecondary(step.blockName);

    m_stepLabel->setText(tr("第 %1 层（Y = %2）· 第 %3 / %4 步")
                             .arg(step.layerOrdinal + 1)
                             .arg(step.layerY)
                             .arg(step.indexInLayer + 1)
                             .arg(step.stepCountInLayer));
    m_materialLabel->setText(tr("%1 × %2").arg(primary).arg(step.count));
    m_materialSwatch->setPixmap(materialSwatch(step.blockName, 18, m_textureLoader));
    m_materialIdLabel->setText(
        secondary.isEmpty()
            ? tr("方块 ID：%1").arg(step.blockName)
            : tr("%1 · 本层这种材料共 %2 个").arg(secondary).arg(step.count));
    m_materialIdLabel->setToolTip(step.blockName);

    if (step.indexInLayer == 0)
    {
        m_stepHint->setText(tr("这一层数量最多的材料，先把它搭完；搭完再搭本层下一种材料。"));
    }
    else
    {
        const int remaining = step.stepCountInLayer - step.indexInLayer - 1;
        m_stepHint->setText(
            remaining > 0
                ? tr("本层第 %1 种材料，搭完后还剩 %2 种，然后进入上一层。")
                      .arg(step.indexInLayer + 1).arg(remaining)
                : tr("本层最后一种材料，搭完就进入上一层。"));
    }

    m_progressLabel->setText(tr("已完成 %1 / %2 步").arg(m_currentStep).arg(m_steps.size()));
    m_progressBar->setValue(m_currentStep);

    m_prevBtn->setEnabled(m_currentStep > 0);
    m_skipBtn->setEnabled(true);
    m_nextBtn->setEnabled(true);
    m_nextBtn->setText(m_currentStep == m_steps.size() - 1
                           ? tr("全部搭完")
                           : tr("这一步搭完了，下一步"));

    m_hintLabel->setText(tr("平面图滚轮缩放、按住拖动平移、双击或按 0 回到适应窗口；"
                            "四边刻度是格子序号，旁边写着整层长度。"));
    m_hintLabel->setToolTip(m_hintLabel->text());

    updateStepTreeState();
}

void ProjectionBuildGuideDialog::advanceStep()
{
    if (!m_docReady || m_steps.isEmpty())
        return;
    if (m_completed)
        return;

    if (m_currentStep + 1 >= m_steps.size())
        goToStep(m_steps.size());
    else
        goToStep(m_currentStep + 1);
}

void ProjectionBuildGuideDialog::restart()
{
    if (!m_docReady || m_steps.isEmpty())
        return;

    m_completed = false;
    m_lastShownLayer = -1;
    m_currentStep = 0;
    m_plan->setOverlayMessage(QString());
    setControlsEnabled(true);
    goToStep(0);
}

void ProjectionBuildGuideDialog::showCompleted()
{
    if (!m_docReady || m_steps.isEmpty())
        return;

    m_completed = true;
    m_currentStep = m_steps.size();

    const ProjectionBuildStep &lastStep = m_steps.last();
    m_plan->setStep(lastStep.layerY, QSet<int>());
    m_plan->setOverlayMessage(tr("✓ 全部搭建完成！\n这份投影的每一层、每一种材料都走完了。"));
    m_plan->setPlaceholder(QString());

    m_view->setLayerView(lastStep.layerY, VoxelViewWidget::LayerMode::Single);
    m_view->clearHighlight();
    m_lastShownLayer = lastStep.layerY;

    m_stepLabel->setText(tr("全部完成"));
    m_materialLabel->setText(tr("✓ 全部搭建完成"));
    m_materialSwatch->clear();
    m_materialIdLabel->setText(tr("共 %1 层、%2 步、%3 个方块")
                                   .arg(m_layers.size()).arg(m_steps.size()).arg(m_totalBlocks));
    m_stepHint->setText(tr("可以回到游戏里验收了；想再看某一步，点右侧搭建顺序里的材料就行。"));
    m_progressLabel->setText(tr("已完成 %1 / %2 步").arg(m_steps.size()).arg(m_steps.size()));
    m_progressBar->setValue(m_steps.size());

    m_prevBtn->setEnabled(true);
    m_skipBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    m_nextBtn->setText(tr("已全部完成"));
    m_hintLabel->setText(tr("想重新走一遍就点「从头开始」；这个窗口不会改动投影文件。"));
    m_hintLabel->setToolTip(m_hintLabel->text());

    updateStepTreeState();

    if (m_currentStep > 0)
    {
        m_prevBtn->setEnabled(true);
    }
}

void ProjectionBuildGuideDialog::rebuildStepTree()
{
    if (!m_tree)
        return;

    m_tree->clear();

    for (const ProjectionBuildLayer &layer : m_layers)
    {
        auto *layerItem = new QTreeWidgetItem(m_tree);
        layerItem->setText(0, tr("第 %1 层 · Y = %2").arg(layer.ordinal + 1).arg(layer.y));
        layerItem->setText(1, tr("%1 个").arg(layer.blockCount));
        layerItem->setData(0, Qt::UserRole, -1);
        layerItem->setData(0, Qt::UserRole + 1, layer.y);
        layerItem->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);

        QFont layerFont = m_tree->font();
        layerFont.setBold(true);
        layerItem->setFont(0, layerFont);
        layerItem->setFont(1, layerFont);

        const QColor secondary = QColor(ThemeManager::instance()->currentTextColor());
        layerItem->setForeground(0, QBrush(secondary));
        layerItem->setForeground(1, QBrush(secondary));

        for (int stepIndex : layer.stepIndices)
        {
            if (stepIndex < 0 || stepIndex >= m_steps.size())
                continue;
            const ProjectionBuildStep &step = m_steps.at(stepIndex);

            auto *item = new QTreeWidgetItem(layerItem);
            const QString primary = materialPrimary(step.blockName);
            item->setIcon(0, QIcon(materialSwatch(step.blockName, 16, m_textureLoader)));
            item->setText(0, primary);
            item->setText(1, QString::number(step.count));
            item->setData(0, Qt::UserRole, stepIndex);
            item->setData(0, Qt::UserRole + 2, primary);
            item->setToolTip(0, step.blockName);
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }

        if (!m_steps.isEmpty() && m_currentStep < m_steps.size()
            && m_steps.at(m_currentStep).layerY == layer.y)
            layerItem->setExpanded(true);
    }

    updateStepTreeState();
}

void ProjectionBuildGuideDialog::updateStepTreeState()
{
    if (!m_tree || m_steps.isEmpty())
        return;

    const QColor accent(ThemeManager::instance()->currentThemeColor());
    const QColor primaryText(ThemeManager::instance()->currentTextColor());
    QColor muted = primaryText;
    muted.setAlpha(120);

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *layerItem = m_tree->topLevelItem(i);
        const int layerY = layerItem->data(0, Qt::UserRole + 1).toInt();
        const bool isCurrentLayer = !m_completed && m_currentStep < m_steps.size()
                                    && m_steps.at(m_currentStep).layerY == layerY;
        if (isCurrentLayer && !layerItem->isExpanded())
            layerItem->setExpanded(true);

        for (int j = 0; j < layerItem->childCount(); ++j)
        {
            QTreeWidgetItem *item = layerItem->child(j);
            const int stepIndex = item->data(0, Qt::UserRole).toInt();
            if (stepIndex < 0 || stepIndex >= m_steps.size())
                continue;

            const bool done = m_completed || stepIndex < m_currentStep;
            const bool current = !m_completed && stepIndex == m_currentStep;
            const QString baseText = item->data(0, Qt::UserRole + 2).toString();

            const QString prefix = done ? QStringLiteral("√ ")
                                        : (current ? QStringLiteral("> ") : QStringLiteral("   "));
            item->setText(0, prefix + baseText);

            QFont itemFont = m_tree->font();
            itemFont.setBold(current);
            item->setFont(0, itemFont);
            item->setFont(1, itemFont);

            QColor color = done ? muted : (current ? accent : primaryText);
            item->setForeground(0, QBrush(color));
            item->setForeground(1, QBrush(color));

            QColor bg = Qt::transparent;
            if (current)
            {
                bg = accent;
                bg.setAlpha(60);
            }
            item->setBackground(0, QBrush(bg));
            item->setBackground(1, QBrush(bg));

            if (current)
            {
                m_tree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            }
        }
    }

    if (m_completed)
        m_tree->scrollToTop();
}

void ProjectionBuildGuideDialog::setControlsEnabled(bool enabled)
{
    if (m_prevBtn)
        m_prevBtn->setEnabled(enabled && m_currentStep > 0 && !m_completed);
    if (m_skipBtn)
        m_skipBtn->setEnabled(enabled && !m_completed);
    if (m_nextBtn)
        m_nextBtn->setEnabled(enabled && !m_completed && m_currentStep < m_steps.size());
    if (m_restartBtn)
        m_restartBtn->setEnabled(enabled && !m_steps.isEmpty());
    if (m_tree)
        m_tree->setEnabled(enabled);
    if (m_viewCombo)
        m_viewCombo->setEnabled(enabled);
}

void ProjectionBuildGuideDialog::startBlockNamesLoad()
{
    if (m_instancePath.isEmpty() || m_cnMapLoading)
        return;

    m_cnMapLoading = true;
    const QString instancePath = m_instancePath;
    QPointer<ProjectionBuildGuideDialog> guard(this);

    (void)QtConcurrent::run([guard, instancePath]() {
        const QHash<QString, QString> map = InstanceBlockNames::loadChineseMap(instancePath);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, map]() {
                if (!guard)
                    return;
                guard->m_cnMap = map;
                guard->m_cnMapLoading = false;
                guard->refreshMaterialNames();
            },
            Qt::QueuedConnection);
    });
}

void ProjectionBuildGuideDialog::refreshMaterialNames()
{
    if (!m_docReady || m_steps.isEmpty())
        return;

    rebuildStepTree();

    if (m_completed)
        showCompleted();
    else
        refreshStepUi();
}

void ProjectionBuildGuideDialog::updateHoverHint(int x, int z, const QString &blockId)
{
    if (!m_hintLabel)
        return;

    if (x < 0 || z < 0 || blockId.isEmpty())
    {
        m_hintLabel->setText(tr("平面图滚轮缩放、按住拖动平移、双击或按 0 回到适应窗口；"
                                "四边刻度是格子序号，旁边写着整层长度。"));
        m_hintLabel->setToolTip(QString());
        return;
    }

    QString name = InstanceBlockNames::lookup(m_cnMap, blockId);
    if (name.isEmpty())
        name = shortBlockId(blockId);
    const QString text = tr("格 %1, %2 · %3（%4）")
                             .arg(x + 1).arg(z + 1).arg(name, blockId);
    m_hintLabel->setText(text);
    m_hintLabel->setToolTip(text);
}
