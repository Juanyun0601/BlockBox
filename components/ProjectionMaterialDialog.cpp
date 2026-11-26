/**
 * @file   ProjectionMaterialDialog.cpp
 * @brief  投影材料清单弹窗实现
 * @author BlockBox Team
 * @date   2026-11-21
 */

#include "ProjectionMaterialDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

#include "utils/CommandAssistant/LitematicReader.h"
#include "utils/Schematic/InstanceBlockNames.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/LitematicEditor.h"

namespace {

// 居中卡片尺寸：与「投影预览」同一套观感，小屏由 AppDialogBase 压回屏内
const int kCardMinWidth = 560;
const int kCardMinHeight = 440;
const int kCardMaxWidth = 760;
const int kCardMaxHeight = 660;

// 清单行里方块图标的边长
const int kBlockIconSize = 18;

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

/** 数量的千分位文案（12345 -> 12,345） */
QString formatCount(int count)
{
    return QLocale().toString(count);
}

/**
 * @brief 实例的方块中文名映射（按实例路径缓存一次）
 *
 * 读中文名要翻 assets 索引 / 客户端 jar，同一个实例反复开清单没必要再读一遍。
 */
const QHash<QString, QString> &cachedBlockChineseMap(const QString &instancePath)
{
    static QHash<QString, QHash<QString, QString>> cache;
    auto it = cache.find(instancePath);
    if (it == cache.end())
        it = cache.insert(instancePath, InstanceBlockNames::loadChineseMap(instancePath));
    return it.value();
}

} // namespace

void ProjectionMaterialDialog::open(QWidget *parent, const QString &filePath,
                                    const QString &instancePath,
                                    const QString &displayName)
{
    if (filePath.isEmpty())
        return;
    ProjectionMaterialDialog dialog(parent, filePath, instancePath, displayName);
    dialog.exec();
}

ProjectionMaterialDialog::ProjectionMaterialDialog(QWidget *parent, const QString &filePath,
                                                   const QString &instancePath,
                                                   const QString &displayName)
    : AppDialogBase(parent)
    , m_filePath(filePath)
    , m_instancePath(instancePath)
    , m_displayName(displayName)
{
    setObjectName(QStringLiteral("projectionMaterialDialog"));
    setWindowTitle(tr("材料清单"));
    initUI();
}

ProjectionMaterialDialog::~ProjectionMaterialDialog() = default;

void ProjectionMaterialDialog::initUI()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("projectionMaterialCard"));
    card->setMinimumSize(kCardMinWidth, kCardMinHeight);
    card->setMaximumSize(kCardMaxWidth, kCardMaxHeight);
    outer->addWidget(card, 0, Qt::AlignCenter);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    // ── 标题行：投影名 + 汇总 + 关闭 ──
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);

    QString title = m_displayName.trimmed();
    if (title.isEmpty())
        title = QFileInfo(m_filePath).completeBaseName();
    m_titleLabel = new QLabel(tr("材料清单：%1").arg(title), card);
    m_titleLabel->setObjectName(QStringLiteral("projectionMaterialTitle"));
    m_titleLabel->setToolTip(title);
    // 名字很长时允许压缩，别把整张卡片顶宽（手机上尤其明显）
    m_titleLabel->setMinimumWidth(0);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titleRow->addWidget(m_titleLabel, 1, Qt::AlignVCenter);

    m_summaryLabel = new QLabel(card);
    m_summaryLabel->setObjectName(QStringLiteral("projectionMaterialSummary"));
    titleRow->addWidget(m_summaryLabel, 0, Qt::AlignVCenter);

    m_closeBtn = new QPushButton(tr("关闭"), card);
    m_closeBtn->setObjectName(QStringLiteral("projectionMaterialCloseBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleRow->addWidget(m_closeBtn, 0, Qt::AlignVCenter);
    layout->addLayout(titleRow);

    // ── 筛选 + 复制 ──
    auto *toolRow = new QHBoxLayout();
    toolRow->setSpacing(8);

    m_filter = new QLineEdit(card);
    m_filter->setObjectName(QStringLiteral("projectionMaterialFilter"));
    m_filter->setPlaceholderText(tr("按方块名或英文 ID 筛选…"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setEnabled(false);
    connect(m_filter, &QLineEdit::textChanged, this,
            [this](const QString &keyword) { applyFilter(keyword); });
    toolRow->addWidget(m_filter, 1);

    m_copyBtn = new QPushButton(tr("复制清单"), card);
    m_copyBtn->setObjectName(QStringLiteral("projectionMaterialCopyBtn"));
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setToolTip(tr("把整份材料清单复制成文字，方便发给别人或自己记账"));
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QPushButton::clicked, this, &ProjectionMaterialDialog::copyList);
    toolRow->addWidget(m_copyBtn, 0);
    layout->addLayout(toolRow);

    // ── 清单本体 ──
    m_tree = new QTreeWidget(card);
    m_tree->setObjectName(QStringLiteral("projectionMaterialTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({ tr("方块"), tr("数量") });
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setIconSize(QSize(kBlockIconSize, kBlockIconSize));
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    // ── 说明 / 状态行 ──
    m_hint = new QLabel(card);
    m_hint->setObjectName(QStringLiteral("projectionMaterialHint"));
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_defaultHint = tr("数量按整份投影统计，不含空气；这份清单只用来备料，不会改动投影文件");
    m_hint->setText(m_defaultHint);
}

void ProjectionMaterialDialog::showEvent(QShowEvent *event)
{
    AppDialogBase::showEvent(event);
    if (!m_loadStarted)
        startLoad();
}

QString ProjectionMaterialDialog::displayNameFor(const QString &blockId) const
{
    // 与投影编辑页的材料清单保持一致：优先「中文名 (英文 ID)」
    const QString baseId = InstanceBlockNames::baseBlockId(blockId);
    const QString cn = InstanceBlockNames::lookup(m_blockCnMap, blockId);
    if (cn.isEmpty())
        return blockId;

    // 去掉 minecraft: 前缀，拼在中文名后面更好读
    QString shortId = baseId;
    const int slash = shortId.indexOf(QLatin1Char(':'));
    if (slash >= 0)
        shortId = shortId.mid(slash + 1);
    return QStringLiteral("%1 (%2)").arg(cn, shortId);
}

void ProjectionMaterialDialog::startLoad()
{
    if (m_loadStarted)
        return;
    m_loadStarted = true;
    m_hint->setText(tr("正在统计材料…（%1）").arg(humanSize(QFileInfo(m_filePath).size())));

    const QString filePath = m_filePath;
    QPointer<ProjectionMaterialDialog> guard(this);
    (void)QtConcurrent::run([guard, filePath]() {
        const LoadResult result = loadMaterials(filePath);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, result]() {
                if (!guard)
                    return;   // 弹窗已关闭，结果作废
                guard->applyResult(result);
            },
            Qt::QueuedConnection);
    });
}

ProjectionMaterialDialog::LoadResult
ProjectionMaterialDialog::loadMaterials(const QString &filePath)
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
        result.error = tr("这种格式统计不了材料，"
                          "只有 .litematic / .schematic / .schem 能直接打开。");
        return result;
    }

    // .schematic / .schem 先转成临时 .litematic 再读，读完立刻删掉
    QString parsePath = filePath;
    bool tempFile = false;
    if (!isLitematic)
    {
        const QString tmpDir =
            QDir::tempPath() + QStringLiteral("/blockbox_projection_material");
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
            result.error = tr("这份投影转换不出材料数据，可能文件已损坏：\n%1")
                               .arg(convertError.isEmpty() ? filePath : convertError);
            return result;
        }
        parsePath = tmpPath;
        tempFile = true;
    }

    const auto info = LitematicReader::readFile(parsePath);
    if (tempFile)
        QFile::remove(parsePath);

    if (!info.has_value())
    {
        result.error = tr("无法解析这份投影，文件可能已损坏。");
        return result;
    }

    result.entries.reserve(info->materialList.size());
    for (const LitematicBlockCount &item : info->materialList)
    {
        if (item.count <= 0)
            continue;
        ProjectionMaterialEntry entry;
        entry.blockId = item.blockId;
        entry.count = item.count;
        result.entries.append(entry);
        result.totalBlocks += item.count;
    }

    // 已经是数量降序，这里只保证文件里没排序时也对得上
    std::sort(result.entries.begin(), result.entries.end(),
              [](const ProjectionMaterialEntry &a, const ProjectionMaterialEntry &b) {
                  if (a.count != b.count)
                      return a.count > b.count;
                  return a.blockId < b.blockId;
              });
    return result;
}

void ProjectionMaterialDialog::applyResult(const LoadResult &result)
{
    m_loaded = true;

    if (!result.error.isEmpty())
    {
        m_entries.clear();
        m_totalBlocks = 0;
        m_tree->clear();
        m_filter->setEnabled(false);
        m_copyBtn->setEnabled(false);
        m_summaryLabel->clear();
        m_hint->setText(result.error);
        return;
    }

    m_entries = result.entries;
    m_totalBlocks = result.totalBlocks;

    if (m_entries.isEmpty())
    {
        m_summaryLabel->clear();
        m_hint->setText(tr("这份投影里几乎全是空气，没有可统计的材料。"));
        return;
    }

    // 中文名要翻实例资源，等统计结果回来再读，弹窗先显示进度不卡
    if (m_blockCnMap.isEmpty() && !m_instancePath.isEmpty())
        m_blockCnMap = cachedBlockChineseMap(m_instancePath);

    m_summaryLabel->setText(tr("共 %1 种方块 · 合计 %2 个")
                                .arg(formatCount(m_entries.size()),
                                     formatCount(m_totalBlocks)));
    m_filter->setEnabled(true);
    m_copyBtn->setEnabled(true);
    fillTree();
    m_hint->setText(m_defaultHint);
}

void ProjectionMaterialDialog::fillTree()
{
    m_tree->clear();

    // 图标取自当前实例的客户端贴图，拿不到就只显示文字
    InstanceTextureLoader *texLoader = m_instancePath.isEmpty()
                                           ? nullptr
                                           : InstanceTextureLoader::sharedForInstance(m_instancePath);

    for (const ProjectionMaterialEntry &entry : m_entries)
    {
        auto *item = new QTreeWidgetItem();
        item->setText(0, displayNameFor(entry.blockId));
        item->setToolTip(0, entry.blockId);
        item->setText(1, formatCount(entry.count));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        // 筛选时按「显示文案 + 原始 ID」一起匹配，中英文都搜得到
        item->setData(0, Qt::UserRole, entry.blockId);
        item->setData(0, Qt::UserRole + 1, item->text(0));
        if (texLoader)
        {
            const QIcon icon = texLoader->blockIcon(entry.blockId, kBlockIconSize);
            if (!icon.isNull())
                item->setIcon(0, icon);
        }
        m_tree->addTopLevelItem(item);
    }

    applyFilter(m_filter ? m_filter->text() : QString());
}

void ProjectionMaterialDialog::applyFilter(const QString &keyword)
{
    if (m_tree == nullptr)
        return;

    const QString needle = keyword.trimmed();
    int visible = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item == nullptr)
            continue;

        bool match = needle.isEmpty();
        if (!match)
        {
            const QString shown = item->data(0, Qt::UserRole + 1).toString();
            const QString rawId = item->data(0, Qt::UserRole).toString();
            match = shown.contains(needle, Qt::CaseInsensitive)
                 || rawId.contains(needle, Qt::CaseInsensitive);
        }
        item->setHidden(!match);
        if (match)
            ++visible;
    }

    // 筛不出东西时说一声，免得看着像清单是空的
    if (!m_loaded)
        return;
    if (needle.isEmpty())
    {
        m_hint->setText(m_defaultHint);
        return;
    }
    if (visible == 0)
        m_hint->setText(tr("没有匹配「%1」的方块").arg(needle));
    else
        m_hint->setText(tr("匹配 %1 种方块").arg(formatCount(visible)));
}

void ProjectionMaterialDialog::copyList()
{
    if (m_entries.isEmpty())
        return;

    QString title = m_displayName.trimmed();
    if (title.isEmpty())
        title = QFileInfo(m_filePath).completeBaseName();

    QStringList lines;
    lines.reserve(m_entries.size() + 2);
    lines.append(tr("材料清单：%1").arg(title));
    lines.append(tr("共 %1 种方块 · 合计 %2 个")
                     .arg(formatCount(m_entries.size()), formatCount(m_totalBlocks)));
    for (const ProjectionMaterialEntry &entry : m_entries)
        lines.append(QStringLiteral("%1 x %2")
                         .arg(displayNameFor(entry.blockId), formatCount(entry.count)));

    if (QClipboard *clipboard = QApplication::clipboard())
        clipboard->setText(lines.join(QLatin1Char('\n')));

    m_hint->setText(tr("清单已复制到剪贴板（%1 行）").arg(formatCount(m_entries.size())));
}
