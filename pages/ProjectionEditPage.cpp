/**
 * @file   ProjectionEditPage.cpp
 * @brief  投影编辑页面实现
 * @author BlockBox Team
 * @date   2026-08-02
 */

#include "ProjectionEditPage.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include "components/AppMessageBox.h"
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "utils/Schematic/LitematicEditor.h"
#include "components/CollapsibleSectionCard.h"

ProjectionEditPage::ProjectionEditPage(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(nullptr)
    , m_nameLabel(nullptr)
    , m_pathLabel(nullptr)
    , m_sizeLabel(nullptr)
    , m_modifiedLabel(nullptr)
    , m_versionLabel(nullptr)
    , m_dataVersionLabel(nullptr)
    , m_statsLabel(nullptr)
    , m_regionTree(nullptr)
    , m_materialTree(nullptr)
    , m_materialSummary(nullptr)
    , m_nameEdit(nullptr)
    , m_authorEdit(nullptr)
    , m_descEdit(nullptr)
    , m_saveBtn(nullptr)
    , m_openFolderBtn(nullptr)
    , m_exportSchematicBtn(nullptr)
    , m_exportSpongeBtn(nullptr)
    , m_currentVersionLabel(nullptr)
    , m_targetVersionCombo(nullptr)
    , m_targetVersionHint(nullptr)
    , m_convertVersionBtn(nullptr)
{
    initUI();
}

ProjectionEditPage::~ProjectionEditPage()
{
}

void ProjectionEditPage::initUI()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *contentWidget = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // 投影名称
    m_titleLabel = new QLabel(tr("投影编辑"), contentWidget);
    m_titleLabel->setObjectName("listTitle");
    layout->addWidget(m_titleLabel);

    // ── 文件信息卡片 ──
    QVBoxLayout *infoCardLayout = createSettingsCard(layout, tr("文件信息"));

    m_nameLabel = new QLabel();
    m_nameLabel->setObjectName("settingValueLabel");
    m_nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QHBoxLayout *nameRow = appendSettingRow(infoCardLayout, tr("名称"));
    nameRow->addWidget(m_nameLabel);

    m_pathLabel = new QLabel();
    m_pathLabel->setObjectName("settingValueLabel");
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathLabel->setWordWrap(true);
    QHBoxLayout *pathRow = appendSettingRow(infoCardLayout, tr("文件路径"));
    pathRow->addWidget(m_pathLabel, 1);

    m_sizeLabel = new QLabel();
    m_sizeLabel->setObjectName("settingValueLabel");
    QHBoxLayout *sizeRow = appendSettingRow(infoCardLayout, tr("文件大小"));
    sizeRow->addWidget(m_sizeLabel);

    m_modifiedLabel = new QLabel();
    m_modifiedLabel->setObjectName("settingValueLabel");
    QHBoxLayout *modifiedRow = appendSettingRow(infoCardLayout, tr("修改时间"));
    modifiedRow->addWidget(m_modifiedLabel);

    m_versionLabel = new QLabel();
    m_versionLabel->setObjectName("settingValueLabel");
    QHBoxLayout *versionRow = appendSettingRow(infoCardLayout, tr("Litematica 版本"));
    versionRow->addWidget(m_versionLabel);

    m_dataVersionLabel = new QLabel();
    m_dataVersionLabel->setObjectName("settingValueLabel");
    QHBoxLayout *dataVersionRow = appendSettingRow(infoCardLayout, tr("MC 数据版本"));
    dataVersionRow->addWidget(m_dataVersionLabel);

    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("settingValueLabel");
    QHBoxLayout *statsRow = appendSettingRow(infoCardLayout, tr("统计"), QString(), true);
    statsRow->addWidget(m_statsLabel);

    // ── 区域信息卡片 ──
    QVBoxLayout *regionCardLayout = createSettingsCard(layout, tr("区域信息"));

    m_regionTree = new QTreeWidget();
    m_regionTree->setColumnCount(5);
    m_regionTree->setHeaderLabels({ tr("区域名"), tr("位置"), tr("尺寸"), tr("体积"), tr("方块数") });
    m_regionTree->setRootIsDecorated(false);
    m_regionTree->setAlternatingRowColors(true);
    m_regionTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 5; ++i)
        m_regionTree->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    m_regionTree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QFrame *regionTreeWrap = new QFrame();
    regionTreeWrap->setObjectName("settingRow");
    regionTreeWrap->setAttribute(Qt::WA_StyledBackground, true);
    regionTreeWrap->setProperty("lastRow", true);
    QVBoxLayout *regionTreeWrapLayout = new QVBoxLayout(regionTreeWrap);
    regionTreeWrapLayout->setContentsMargins(24, 8, 24, 16);
    regionTreeWrapLayout->setSpacing(0);
    regionTreeWrapLayout->addWidget(m_regionTree);
    regionCardLayout->addWidget(regionTreeWrap);

    // ── 材料清单卡片 ──
    QVBoxLayout *materialCardLayout = createSettingsCard(layout, tr("材料清单"));

    m_materialTree = new QTreeWidget();
    m_materialTree->setColumnCount(2);
    m_materialTree->setHeaderLabels({ tr("方块"), tr("数量") });
    m_materialTree->setRootIsDecorated(false);
    m_materialTree->setAlternatingRowColors(true);
    m_materialTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_materialTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_materialTree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_materialSummary = new QLabel();
    m_materialSummary->setObjectName("hintLabel");

    QFrame *materialTreeWrap = new QFrame();
    materialTreeWrap->setObjectName("settingRow");
    materialTreeWrap->setAttribute(Qt::WA_StyledBackground, true);
    materialTreeWrap->setProperty("lastRow", true);
    QVBoxLayout *materialTreeWrapLayout = new QVBoxLayout(materialTreeWrap);
    materialTreeWrapLayout->setContentsMargins(24, 8, 24, 16);
    materialTreeWrapLayout->setSpacing(8);
    materialTreeWrapLayout->addWidget(m_materialTree);
    materialTreeWrapLayout->addWidget(m_materialSummary);
    materialCardLayout->addWidget(materialTreeWrap);

    // ── 元数据编辑卡片 ──
    QVBoxLayout *editCardLayout = createSettingsCard(layout, tr("元数据编辑"));

    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText(tr("投影显示名称"));
    QHBoxLayout *nameEditRow = appendSettingRow(editCardLayout, tr("名称"));
    nameEditRow->addWidget(m_nameEdit, 2);

    m_authorEdit = new QLineEdit();
    m_authorEdit->setPlaceholderText(tr("投影作者"));
    QHBoxLayout *authorEditRow = appendSettingRow(editCardLayout, tr("作者"));
    authorEditRow->addWidget(m_authorEdit, 2);

    m_descEdit = new QTextEdit();
    m_descEdit->setPlaceholderText(tr("投影描述"));
    m_descEdit->setFixedHeight(72);
    m_descEdit->setAcceptRichText(false);
    QHBoxLayout *descEditRow = appendSettingRow(editCardLayout, tr("描述"), QString(), true);
    descEditRow->addWidget(m_descEdit, 2);

    // ── 版本转换卡片 ──
    QVBoxLayout *versionCardLayout = createSettingsCard(layout, tr("版本转换"));

    m_currentVersionLabel = new QLabel(tr("当前版本：未知"));
    m_currentVersionLabel->setObjectName("settingValueLabel");
    QHBoxLayout *currentVersionRow = appendSettingRow(versionCardLayout, tr("当前版本"));
    currentVersionRow->addWidget(m_currentVersionLabel);

    // 目标版本
    m_targetVersionCombo = new QComboBox();
    // 数据: 高 16 位版本号 + 低 32 位 Minecraft 数据版本（1.17 起约 2730，1.12~1.20 约 2531/2578/3955）
    m_targetVersionCombo->addItem(tr("V4（Minecraft 1.12 ~ 1.15）"), (static_cast<quint64>(4) << 32) | 2531u);
    m_targetVersionCombo->addItem(tr("V5（Minecraft 1.16）"), (static_cast<quint64>(5) << 32) | 2578u);
    m_targetVersionCombo->addItem(tr("V6（Minecraft 1.17 ~ 1.20.4）"), (static_cast<quint64>(6) << 32) | 3955u);
    m_targetVersionCombo->addItem(tr("V7（Minecraft 1.21+）"), (static_cast<quint64>(7) << 32) | 3955u);
    connect(m_targetVersionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectionEditPage::onVersionComboChanged);

    QHBoxLayout *targetVersionRow = appendSettingRow(versionCardLayout, tr("目标版本"));
    targetVersionRow->addWidget(m_targetVersionCombo, 2);

    m_targetVersionHint = new QLabel();
    m_targetVersionHint->setObjectName("hintLabel");
    m_targetVersionHint->setWordWrap(true);
    QFrame *hintRow = new QFrame();
    hintRow->setObjectName("settingRow");
    hintRow->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *hintRowLayout = new QVBoxLayout(hintRow);
    hintRowLayout->setContentsMargins(24, 4, 24, 8);
    hintRowLayout->setSpacing(0);
    hintRowLayout->addWidget(m_targetVersionHint);
    versionCardLayout->addWidget(hintRow);

    m_convertVersionBtn = new QPushButton(tr("转换为所选版本"));
    m_convertVersionBtn->setObjectName("bottomActionBtn");
    m_convertVersionBtn->setCursor(Qt::PointingHandCursor);
    m_convertVersionBtn->setToolTip(tr("生成新文件，不改动当前文件"));
    connect(m_convertVersionBtn, &QPushButton::clicked, this, &ProjectionEditPage::onConvertVersionClicked);

    QFrame *convertRow = new QFrame();
    convertRow->setObjectName("settingRow");
    convertRow->setAttribute(Qt::WA_StyledBackground, true);
    convertRow->setProperty("lastRow", true);
    QHBoxLayout *convertRowLayout = new QHBoxLayout(convertRow);
    convertRowLayout->setContentsMargins(24, 8, 24, 16);
    convertRowLayout->setSpacing(0);
    convertRowLayout->addStretch();
    convertRowLayout->addWidget(m_convertVersionBtn);
    versionCardLayout->addWidget(convertRow);

    // ── 底部按钮 ──
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(10);
    m_openFolderBtn = new QPushButton(tr("打开所在文件夹"), contentWidget);
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &ProjectionEditPage::onOpenFolderClicked);
    bottomRow->addWidget(m_openFolderBtn);

    m_exportSchematicBtn = new QPushButton(tr("导出 .schematic"), contentWidget);
    m_exportSchematicBtn->setObjectName("bottomActionBtn");
    m_exportSchematicBtn->setCursor(Qt::PointingHandCursor);
    m_exportSchematicBtn->setToolTip(tr("转换为 Schematica 格式 (.schematic)"));
    connect(m_exportSchematicBtn, &QPushButton::clicked, this, &ProjectionEditPage::onExportSchematicClicked);
    bottomRow->addWidget(m_exportSchematicBtn);

    m_exportSpongeBtn = new QPushButton(tr("导出 .schem"), contentWidget);
    m_exportSpongeBtn->setObjectName("bottomActionBtn");
    m_exportSpongeBtn->setCursor(Qt::PointingHandCursor);
    m_exportSpongeBtn->setToolTip(tr("转换为 Sponge 格式 (.schem)"));
    connect(m_exportSpongeBtn, &QPushButton::clicked, this, &ProjectionEditPage::onExportSpongeClicked);
    bottomRow->addWidget(m_exportSpongeBtn);

    bottomRow->addStretch();

    m_saveBtn = new QPushButton(tr("保存修改"), contentWidget);
    m_saveBtn->setObjectName("addFirstAccountBtn");
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, &ProjectionEditPage::onSaveMetadataClicked);
    bottomRow->addWidget(m_saveBtn);
    layout->addLayout(bottomRow);

    layout->addStretch();

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);

    // 初始占位
    reset();
}

void ProjectionEditPage::setCurrentProjection(const QString &filePath)
{
    m_filePath = filePath;
    m_loaded = false;
    loadProjection();
}

void ProjectionEditPage::reset()
{
    m_filePath.clear();
    m_loaded = false;
    m_titleLabel->setText(tr("投影编辑"));
    m_nameLabel->setText(tr("—"));
    m_pathLabel->setText(tr("—"));
    m_sizeLabel->setText(tr("—"));
    m_modifiedLabel->setText(tr("—"));
    m_versionLabel->setText(tr("—"));
    m_dataVersionLabel->setText(tr("—"));
    m_statsLabel->setText(tr("—"));
    m_regionTree->clear();
    m_materialTree->clear();
    m_materialSummary->clear();
    m_nameEdit->clear();
    m_authorEdit->clear();
    m_descEdit->clear();
    m_nameEdit->setEnabled(false);
    m_authorEdit->setEnabled(false);
    m_descEdit->setEnabled(false);
    m_saveBtn->setEnabled(false);
    m_exportSchematicBtn->setEnabled(false);
    m_exportSpongeBtn->setEnabled(false);
    m_currentVersionLabel->setText(tr("当前版本：未知"));
    m_targetVersionCombo->setEnabled(false);
    m_targetVersionHint->clear();
    m_convertVersionBtn->setEnabled(false);
}

QVBoxLayout *ProjectionEditPage::createSettingsCard(QVBoxLayout *parentLayout,
                                                    const QString &title,
                                                    bool startCollapsed)
{
    auto *card = new CollapsibleSectionCard(title, startCollapsed);
    parentLayout->addWidget(card);
    return card->contentLayout();
}

QHBoxLayout *ProjectionEditPage::appendSettingRow(QVBoxLayout *cardLayout,
                                                  const QString &title,
                                                  const QString &desc,
                                                  bool isLast)
{
    QFrame *row = new QFrame();
    row->setObjectName("settingRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (isLast)
        row->setProperty("lastRow", true);

    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 16, 24, 16);
    rowLayout->setSpacing(20);

    QWidget *info = new QWidget(row);
    info->setObjectName("settingInfo");
    QVBoxLayout *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    if (!title.isEmpty())
    {
        QLabel *titleLabel = new QLabel(title, info);
        titleLabel->setObjectName("settingTitle");
        infoLayout->addWidget(titleLabel);
    }

    if (!desc.isEmpty())
    {
        QLabel *descLabel = new QLabel(desc, info);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    rowLayout->addWidget(info, 1);

    cardLayout->addWidget(row);
    return rowLayout;
}

void ProjectionEditPage::loadProjection()
{
    if (m_loaded || m_filePath.isEmpty())
        return;

    auto result = LitematicReader::readFile(m_filePath);
    m_loaded = true;

    if (!result.has_value())
    {
        m_nameLabel->setText(tr("读取失败"));
        m_pathLabel->setText(m_filePath);
        m_sizeLabel->setText(tr("无法解析 .litematic 文件"));
        return;
    }

    m_info = result.value();

    QFileInfo fileInfo(m_filePath);
    m_titleLabel->setText(tr("投影编辑：%1").arg(m_info.name));
    m_nameLabel->setText(m_info.name);
    m_pathLabel->setText(m_filePath);
    m_sizeLabel->setText(formatFileSize(fileInfo.size()));
    m_modifiedLabel->setText(formatTimestamp(fileInfo.lastModified().toMSecsSinceEpoch()));
    m_versionLabel->setText(QString::number(m_info.version));
    m_dataVersionLabel->setText(m_info.minecraftDataVersion > 0
        ? QString::number(m_info.minecraftDataVersion)
        : tr("未知"));
    m_statsLabel->setText(tr("总方块 %1 · 总体积 %2 · 区域 %3 · 包围盒 %4×%5×%6")
        .arg(m_info.totalBlocks)
        .arg(m_info.totalVolume)
        .arg(m_info.regionCount)
        .arg(m_info.enclosingSizeX)
        .arg(m_info.enclosingSizeY)
        .arg(m_info.enclosingSizeZ));

    populateRegions();
    populateMaterials();

    m_nameEdit->setText(m_info.name);
    m_authorEdit->setText(m_info.author);
    m_descEdit->setPlainText(m_info.description);
    m_nameEdit->setEnabled(true);
    m_authorEdit->setEnabled(true);
    m_descEdit->setEnabled(true);
    m_saveBtn->setEnabled(true);
    m_exportSchematicBtn->setEnabled(true);
    m_exportSpongeBtn->setEnabled(true);

    m_currentVersionLabel->setText(tr("当前版本：V%1（Minecraft %2）")
        .arg(m_info.version)
        .arg(LitematicEditor::minecraftVersionRange(m_info.version)));
    m_targetVersionCombo->setEnabled(true);
    m_convertVersionBtn->setEnabled(true);
    onVersionComboChanged(m_targetVersionCombo->currentIndex());
}

void ProjectionEditPage::populateRegions()
{
    m_regionTree->clear();
    for (const LitematicRegionInfo &region : m_info.regions)
    {
        auto *item = new QTreeWidgetItem();
        item->setText(0, region.name);
        item->setText(1, tr("%1, %2, %3").arg(region.posX).arg(region.posY).arg(region.posZ));
        item->setText(2, tr("%1×%2×%3").arg(qAbs(region.sizeX)).arg(qAbs(region.sizeY)).arg(qAbs(region.sizeZ)));
        item->setText(3, QString::number(region.volume));
        item->setText(4, QString::number(region.blockCount));
        m_regionTree->addTopLevelItem(item);
    }
    if (m_regionTree->topLevelItemCount() == 0)
    {
        auto *empty = new QTreeWidgetItem();
        empty->setText(0, tr("无区域"));
        m_regionTree->addTopLevelItem(empty);
    }
    resizeTreeToContent(m_regionTree);
}

void ProjectionEditPage::resizeTreeToContent(QTreeWidget *tree)
{
    if (tree == nullptr)
        return;

    const int rowCount = tree->topLevelItemCount();
    // 表头高度 + 每行高度 × 行数
    int rowHeight = tree->header()->sizeHint().isValid()
        ? tree->header()->sizeHint().height()
        : 24;
    if (rowCount > 0)
    {
        const int firstRowHeight = tree->sizeHintForRow(0);
        if (firstRowHeight > 0)
            rowHeight = firstRowHeight;
    }
    const int headerHeight = tree->header()->height() > 0
        ? tree->header()->height()
        : tree->header()->sizeHint().height();

    const int contentHeight = headerHeight + rowCount * rowHeight + 4;
    // 上限：不超过页面可见高度，过长则内部滚动
    int availableHeight = 400;
    QWidget *parent = tree;
    while (parent != nullptr)
    {
        QScrollArea *area = qobject_cast<QScrollArea *>(parent);
        if (area != nullptr)
        {
            availableHeight = qMax(80, area->viewport()->height());
            break;
        }
        parent = parent->parentWidget();
    }
    // 上限跨多行时用树自身的计算尺寸，避免内部滚动条与页面滚动条叠加
    const int maxHeight = qMax(120, availableHeight - 40);
    const int target = qMin(contentHeight, maxHeight);

    tree->setMinimumHeight(0);
    tree->setMaximumHeight(QWIDGETSIZE_MAX);
    tree->setFixedHeight(target);
}

void ProjectionEditPage::populateMaterials()
{
    m_materialTree->clear();
    for (const LitematicBlockCount &entry : m_info.materialList)
    {
        auto *item = new QTreeWidgetItem();
        item->setText(0, entry.blockId);
        item->setText(1, QString::number(entry.count));
        m_materialTree->addTopLevelItem(item);
    }
    if (m_materialTree->topLevelItemCount() == 0)
    {
        auto *empty = new QTreeWidgetItem();
        empty->setText(0, tr("无方块数据"));
        m_materialTree->addTopLevelItem(empty);
    }
    m_materialSummary->setText(tr("共 %1 种方块").arg(m_info.materialList.size()));
    resizeTreeToContent(m_materialTree);
}

void ProjectionEditPage::onSaveMetadataClicked()
{
    if (m_filePath.isEmpty())
        return;

    const QString name = m_nameEdit->text().trimmed();
    const QString author = m_authorEdit->text().trimmed();
    const QString description = m_descEdit->toPlainText().trimmed();

    QString error;
    if (!LitematicEditor::editMetadata(m_filePath, name, author, description, &error))
    {
        AppMessageBox::warning(this, tr("保存失败"),
            tr("无法保存投影元数据：\n%1").arg(error));
        return;
    }

    // 更新内存中的信息，避免重复读取文件
    m_info.name = name;
    m_info.author = author;
    m_info.description = description;
    m_titleLabel->setText(tr("投影编辑：%1").arg(name));
    m_nameLabel->setText(name);

    emit projectionEdited(m_filePath);
    AppMessageBox::information(this, tr("保存成功"), tr("投影元数据已保存。"));
}

void ProjectionEditPage::onExportSchematicClicked()
{
    if (m_filePath.isEmpty())
        return;

    QFileInfo srcInfo(m_filePath);
    const QString baseName = srcInfo.completeBaseName();
    const QString startDir = srcInfo.absolutePath();
    const QString outputPath = AppFileDialog::getSaveFileName(
        this, tr("导出为 .schematic"),
        startDir + QLatin1Char('/') + baseName + QStringLiteral(".schematic"),
        tr("Schematica 文件 (*.schematic)"));

    if (outputPath.isEmpty())
        return;

    QString error;
    if (!LitematicEditor::convertToSchematica(m_filePath, outputPath, &error))
    {
        AppMessageBox::warning(this, tr("导出失败"),
            tr("无法导出 .schematic：\n%1").arg(error));
        return;
    }
    AppMessageBox::information(this, tr("导出成功"),
        tr("已导出：\n%1").arg(outputPath));
}

void ProjectionEditPage::onExportSpongeClicked()
{
    if (m_filePath.isEmpty())
        return;

    QFileInfo srcInfo(m_filePath);
    const QString baseName = srcInfo.completeBaseName();
    const QString startDir = srcInfo.absolutePath();
    const QString outputPath = AppFileDialog::getSaveFileName(
        this, tr("导出为 .schem"),
        startDir + QLatin1Char('/') + baseName + QStringLiteral(".schem"),
        tr("Sponge 文件 (*.schem)"));

    if (outputPath.isEmpty())
        return;

    QString error;
    if (!LitematicEditor::convertToSponge(m_filePath, outputPath, &error))
    {
        AppMessageBox::warning(this, tr("导出失败"),
            tr("无法导出 .schem：\n%1").arg(error));
        return;
    }
    AppMessageBox::information(this, tr("导出成功"),
        tr("已导出：\n%1").arg(outputPath));
}

void ProjectionEditPage::onOpenFolderClicked()
{
    if (m_filePath.isEmpty())
        return;
    QFileInfo fileInfo(m_filePath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
}

void ProjectionEditPage::onVersionComboChanged(int index)
{
    Q_UNUSED(index);
    const QVariant data = m_targetVersionCombo->currentData();
    if (!data.isValid())
    {
        m_targetVersionHint->clear();
        return;
    }
    const int target = static_cast<int>(data.toULongLong() >> 32);
    if (target == m_info.version)
    {
        m_targetVersionHint->setText(tr("目标版本与当前版本相同。"));
        m_convertVersionBtn->setEnabled(false);
    }
    else
    {
        m_targetVersionHint->setText(tr("将生成新文件，版本号 V%1 → V%2。").arg(m_info.version).arg(target));
        m_convertVersionBtn->setEnabled(true);
    }
}

void ProjectionEditPage::onConvertVersionClicked()
{
    if (m_filePath.isEmpty())
        return;

    const QVariant data = m_targetVersionCombo->currentData();
    if (!data.isValid())
        return;
    const quint64 packed = data.toULongLong();
    const int targetVersion = static_cast<int>(packed >> 32);
    const int targetDataVersion = static_cast<int>(packed & 0xFFFFFFFFu);

    QFileInfo srcInfo(m_filePath);
    const QString baseName = srcInfo.completeBaseName();
    const QString startDir = srcInfo.absolutePath();
    const QString outputPath = AppFileDialog::getSaveFileName(
        this, tr("转换为所选版本"),
        startDir + QLatin1Char('/') + baseName + QStringLiteral("_v%1.litematic").arg(targetVersion),
        tr("Litematica 文件 (*.litematic)"));

    if (outputPath.isEmpty())
        return;

    QString error;
    if (!LitematicEditor::convertVersion(m_filePath, outputPath, targetVersion, targetDataVersion, &error))
    {
        AppMessageBox::warning(this, tr("转换失败"),
            tr("无法转换版本：\n%1").arg(error));
        return;
    }
    AppMessageBox::information(this, tr("转换成功"),
        tr("已生成 V%1 版本文件：\n%2").arg(targetVersion).arg(outputPath));
}

QString ProjectionEditPage::formatFileSize(qint64 size) const
{
    if (size < 1024)
        return tr("%1 B").arg(size);
    if (size < 1024 * 1024)
        return tr("%1 KB").arg(QString::number(size / 1024.0, 'f', 1));
    if (size < 1024LL * 1024 * 1024)
        return tr("%1 MB").arg(QString::number(size / (1024.0 * 1024.0), 'f', 2));
    return tr("%1 GB").arg(QString::number(size / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

QString ProjectionEditPage::formatTimestamp(qint64 msecs) const
{
    if (msecs <= 0)
        return tr("—");
    return QDateTime::fromMSecsSinceEpoch(msecs)
        .toLocalTime()
        .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}
