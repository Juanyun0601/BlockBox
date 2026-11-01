/**
 * @file   InstanceRecordingsPage.cpp
 * @brief  实例管理 - 录像管理页面实现（Replay Mod / Flashback 录像列表）
 * @author BlockBox Team
 * @date   2026-10-31
 */
#include "InstanceRecordingsPage.h"

#include "components/AppMessageBox.h"
#include "utils/IconHelper.h"
#include "utils/JarUtils.h"
#include "utils/ThemeManager.h"

#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmapCache>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace
{

// 两个录像模组的固定路径与扩展名（相对实例根目录）
constexpr const char *kReplayModDir = "replay_recordings";
constexpr const char *kFlashbackDir = "flashback/replays";
constexpr const char *kReplayModExt = ".mcpr";
constexpr const char *kFlashbackExt = ".zip";

constexpr qint64 k10MB = 10LL * 1024 * 1024;
constexpr qint64 k100MB = 100LL * 1024 * 1024;

} // namespace

// ───────────────────────────── 生命周期 ─────────────────────────────

InstanceRecordingsPage::InstanceRecordingsPage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
}

InstanceRecordingsPage::~InstanceRecordingsPage() = default;

// ───────────────────────────── 对外接口 ─────────────────────────────

void InstanceRecordingsPage::setInstancePath(const QString &path)
{
    m_instancePath = path;
    m_currentSearch.clear();
    if (m_searchEdit)
    {
        m_searchEdit->blockSignals(true);
        m_searchEdit->clear();
        m_searchEdit->blockSignals(false);
    }
    scanFiles();
}

void InstanceRecordingsPage::refreshFileList()
{
    scanFiles();
}

void InstanceRecordingsPage::applyPageFilter(const QString &query)
{
    // 同步到页内搜索框：setText 触发 textChanged → onSearchTextChanged → 既有过滤管线
    if (m_searchEdit)
        m_searchEdit->setText(query);
}

// ───────────────────────────── UI 构建 ─────────────────────────────

void InstanceRecordingsPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // 顶部筛选卡：行1=搜索+来源，行2=时间/大小/排序+统计（与本地资源页两行统一）
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    // ---- 行1：搜索框 + 来源 ----
    QHBoxLayout *topBarLayout = new QHBoxLayout();
    topBarLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(filterCard);
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索录像名称..."));
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);
    topBarLayout->addWidget(m_searchEdit, 1);

    QLabel *sourceLabel = new QLabel(tr("来源:"), filterCard);
    sourceLabel->setObjectName("filterLabel");
    topBarLayout->addWidget(sourceLabel);

    m_sourceCombo = new QComboBox(filterCard);
    m_sourceCombo->setObjectName("categoryCombo");
    m_sourceCombo->setFixedHeight(28);
    m_sourceCombo->setMinimumWidth(130);
    m_sourceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sourceCombo->setMinimumContentsLength(5);
    m_sourceCombo->addItem(tr("全部来源"), 0);
    m_sourceCombo->addItem(QStringLiteral("Replay Mod"), 1);
    m_sourceCombo->addItem(QStringLiteral("Flashback"), 2);
    topBarLayout->addWidget(m_sourceCombo);

    cardLayout->addLayout(topBarLayout);

    // ---- 行2：时间 / 大小 / 排序 + 统计 ----
    QHBoxLayout *filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);

    auto makeFilterLabel = [filterCard](const QString &text) -> QLabel * {
        QLabel *label = new QLabel(text, filterCard);
        label->setObjectName("filterLabel");
        return label;
    };
    auto makeFilterCombo = [filterCard]() -> QComboBox * {
        QComboBox *combo = new QComboBox(filterCard);
        combo->setObjectName("filterCombo");
        combo->setFixedHeight(28);
        combo->setMinimumWidth(104);
        return combo;
    };

    filterRow->addWidget(makeFilterLabel(tr("时间:")));
    m_timeCombo = makeFilterCombo();
    m_timeCombo->addItem(tr("全部时间"), 0);
    m_timeCombo->addItem(tr("最近 7 天"), 1);
    m_timeCombo->addItem(tr("最近 30 天"), 2);
    m_timeCombo->addItem(tr("最近一年"), 3);
    filterRow->addWidget(m_timeCombo);

    filterRow->addWidget(makeFilterLabel(tr("大小:")));
    m_sizeCombo = makeFilterCombo();
    m_sizeCombo->addItem(tr("全部大小"), 0);
    m_sizeCombo->addItem(tr("小于 10 MB"), 1);
    m_sizeCombo->addItem(tr("10 ~ 100 MB"), 2);
    m_sizeCombo->addItem(tr("大于 100 MB"), 3);
    filterRow->addWidget(m_sizeCombo);

    filterRow->addWidget(makeFilterLabel(tr("排序:")));
    m_sortCombo = makeFilterCombo();
    m_sortCombo->setObjectName("searchSortCombo");
    m_sortCombo->addItem(tr("名称 A→Z"), 0);
    m_sortCombo->addItem(tr("名称 Z→A"), 1);
    m_sortCombo->addItem(tr("大小 大→小"), 2);
    m_sortCombo->addItem(tr("大小 小→大"), 3);
    m_sortCombo->addItem(tr("时间 新→旧"), 4);
    m_sortCombo->addItem(tr("时间 旧→新"), 5);
    m_sortCombo->setCurrentIndex(4);
    filterRow->addWidget(m_sortCombo);

    filterRow->addStretch();

    m_statsLabel = new QLabel(filterCard);
    m_statsLabel->setObjectName("statusLabel");
    filterRow->addWidget(m_statsLabel);
    cardLayout->addLayout(filterRow);

    mainLayout->addWidget(filterCard);

    // 录像列表
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
    m_cardListLayout = new QVBoxLayout(m_cardContainer);
    m_cardListLayout->setContentsMargins(14, 0, 14, 0);
    m_cardListLayout->setSpacing(8);
    m_cardListLayout->addStretch();
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // 空提示放滚动区外（与其他资源页一致，避免被 clearCards 误删）
    // 没有录像时多半连录像模组都没装，顺手给出两个「快速安装」入口
    m_emptyWidget = new QWidget(this);
    QVBoxLayout *emptyLayout = new QVBoxLayout(m_emptyWidget);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(12);
    emptyLayout->addStretch();

    m_emptyLabel = new QLabel(m_emptyWidget);
    m_emptyLabel->setObjectName("exampleContentLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    emptyLayout->addWidget(m_emptyLabel);

    QHBoxLayout *installRow = new QHBoxLayout();
    installRow->setContentsMargins(0, 0, 0, 0);
    installRow->setSpacing(10);
    installRow->addStretch();

    auto makeInstallBtn = [this](const QString &text) -> QPushButton * {
        QPushButton *btn = new QPushButton(text, m_emptyWidget);
        btn->setObjectName("quickInstallBtn");
        // 主色按钮配白色图标（主题色描边会和底色糊在一起看不清）
        btn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/download.svg", QColor("#ffffff"), 18));
        btn->setIconSize(QSize(18, 18));
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    // 点完先短暂禁用，避免连点并起两套安装流程
    auto armInstall = [this](QPushButton *btn, const QString &modKey) {
        connect(btn, &QPushButton::clicked, this, [this, btn, modKey]() {
            btn->setEnabled(false);
            QTimer::singleShot(2500, btn, [btn]() { btn->setEnabled(true); });
            emit quickInstallModRequested(modKey);
        });
    };

    m_installReplayBtn = makeInstallBtn(tr("快速安装 Replay Mod"));
    m_installReplayBtn->setToolTip(
        tr("自动安装 Replay Mod：游戏内按 F6 开始/结束录像，录像存到 replay_recordings 目录"));
    armInstall(m_installReplayBtn, QStringLiteral("replaymod"));
    installRow->addWidget(m_installReplayBtn);

    m_installFlashbackBtn = makeInstallBtn(tr("快速安装 Flashback"));
    m_installFlashbackBtn->setToolTip(
        tr("自动安装 Flashback：游戏内录制后可回放与剪辑，录像存到 flashback/replays 目录"));
    armInstall(m_installFlashbackBtn, QStringLiteral("flashback"));
    installRow->addWidget(m_installFlashbackBtn);

    installRow->addStretch();
    emptyLayout->addLayout(installRow);
    emptyLayout->addStretch();

    m_emptyWidget->hide();
    mainLayout->addWidget(m_emptyWidget, 1);

    // 底部操作栏：统计 + 打开录像文件夹
    m_bottomBar = new QWidget();
    m_bottomBar->setObjectName("modBottomBar");
    m_bottomBar->setFixedHeight(48);
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(14, 6, 14, 6);
    bottomLayout->setSpacing(10);

    m_openFolderBtn = new QPushButton(tr("打开录像文件夹"), m_bottomBar);
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/folder.svg", ThemeManager::instance()->currentThemeColor(), 18));
    m_openFolderBtn->setIconSize(QSize(18, 18));
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = currentRecordingsDir();
        if (QFileInfo::exists(dir))
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    bottomLayout->addWidget(m_openFolderBtn);
    bottomLayout->addStretch();

    mainLayout->addWidget(m_bottomBar);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &InstanceRecordingsPage::onSearchTextChanged);
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceRecordingsPage::onSourceFilterChanged);
    connect(m_timeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceRecordingsPage::onTimeFilterChanged);
    connect(m_sizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceRecordingsPage::onSizeFilterChanged);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceRecordingsPage::onSortChanged);
}

void InstanceRecordingsPage::onSearchTextChanged(const QString &text)
{
    m_currentSearch = text.trimmed();
    rebuildCards();
}

void InstanceRecordingsPage::onSourceFilterChanged(int index)
{
    // 0=全部 1=Replay Mod 2=Flashback：筛选状态要落到成员上，rebuildCards/updateStats 都读它
    m_sourceFilter = index;
    rebuildCards();
}

void InstanceRecordingsPage::onTimeFilterChanged(int index)
{
    m_timeFilter = index;
    rebuildCards();
}

void InstanceRecordingsPage::onSizeFilterChanged(int index)
{
    m_sizeFilter = index;
    rebuildCards();
}

void InstanceRecordingsPage::onSortChanged(int index)
{
    m_sortMode = index;
    rebuildCards();
}

// ───────────────────────────── 数据扫描 ─────────────────────────────

QString InstanceRecordingsPage::recordingsDir(int source) const
{
    if (m_instancePath.isEmpty())
        return {};
    if (source == ReplayMod)
        return m_instancePath + QStringLiteral("/") + QLatin1String(kReplayModDir);
    return m_instancePath + QStringLiteral("/") + QLatin1String(kFlashbackDir);
}

QString InstanceRecordingsPage::currentRecordingsDir() const
{
    if (m_sourceFilter == 0)
        return m_instancePath; // 全部来源：打开实例根目录，两个录像文件夹都在里面
    return recordingsDir(m_sourceFilter == 1 ? ReplayMod : Flashback);
}

bool InstanceRecordingsPage::anyFilterActive() const
{
    return m_sourceFilter != 0 || m_timeFilter != 0
        || m_sizeFilter != 0 || !m_currentSearch.isEmpty();
}

void InstanceRecordingsPage::scanFiles()
{
    m_entries.clear();

    // 两个来源一起扫，来源筛选只在展示层做，切换来源无需重读文件
    const std::pair<int, const char *> sources[] = {
        {ReplayMod, kReplayModExt}, {Flashback, kFlashbackExt}};
    for (const auto &[source, ext] : sources)
    {
        const QString dir = recordingsDir(source);
        if (dir.isEmpty() || !QDir(dir).exists())
            continue;
        const QFileInfoList infos = QDir(dir).entryInfoList(
            QStringList() << QStringLiteral("*") + QLatin1String(ext),
            QDir::Files, QDir::Time);
        for (const QFileInfo &fi : infos)
        {
            RecordingEntry entry;
            entry.filePath = fi.absoluteFilePath();
            entry.fileName = fi.fileName();
            entry.fileSize = fi.size();
            entry.lastModified = fi.lastModified();
            entry.source = source;
            m_entries.append(entry);
        }
    }

    // 逐条读取录像内记录的名字 / 服务器 / 时长 / 预览图
    for (RecordingEntry &entry : m_entries)
        loadRecordingMeta(entry);

    // 基础顺序按录制时间新→旧（展示层排序会按当前排序模式重排）
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [](const RecordingEntry &a, const RecordingEntry &b) {
                         return a.lastModified > b.lastModified;
                     });

    rebuildCards();
}

void InstanceRecordingsPage::loadRecordingMeta(RecordingEntry &entry) const
{
    // 默认标题：去掉扩展名的文件名
    entry.title = QFileInfo(entry.fileName).completeBaseName();

    const QStringList metaEntries = entry.source == ReplayMod
        ? QStringList{QStringLiteral("replay.meta.txt"), QStringLiteral("metaData.json")}
        : QStringList{QStringLiteral("metadata.json")};

    QMap<QString, QByteArray> results;
    JarUtils::extractMultipleFromJarToMemory(entry.filePath, metaEntries, results);

    if (entry.source == ReplayMod)
    {
        parseReplayModMeta(entry,
                           results.value(QStringLiteral("replay.meta.txt")),
                           results.value(QStringLiteral("metaData.json")));
    }
    else
    {
        parseFlashbackMeta(entry, results.value(QStringLiteral("metadata.json")));
    }

    entry.icon = extractPreviewIcon(entry.filePath);
}

void InstanceRecordingsPage::parseReplayModMeta(RecordingEntry &entry,
                                                const QByteArray &metaTxt,
                                                const QByteArray &metaJson) const
{
    // replay.meta.txt 是 Java properties 格式（key=value，每行一条）
    QHash<QString, QString> props;
    if (!metaTxt.isEmpty())
    {
        const QStringList lines =
            QString::fromUtf8(metaTxt).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
        {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            props.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
    }

    // 新版录像可能只有 metaData.json（字段与 replay.meta.txt 一致）
    const QJsonObject json =
        metaJson.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(metaJson).object();

    // properties 里全是字符串；metaData.json 里数字字段是 JSON number，两种都要兼容
    const auto readKey = [&props, &json](const char *key) -> QString {
        const QString fromProps = props.value(QLatin1String(key));
        if (!fromProps.isEmpty())
            return fromProps;
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isString())
            return v.toString();
        if (v.isDouble())
            return QString::number(v.toDouble(), 'f', 0);
        return {};
    };

    const bool singleplayer = readKey("singleplayer") == QStringLiteral("true");
    const QString serverName = readKey("serverName");
    const qint64 dateMs = readKey("date").toLongLong();
    const qint64 mpdMs = readKey("mpd").toLongLong();

    if (singleplayer)
        entry.worldLine = tr("单人游戏");
    else if (!serverName.isEmpty())
        entry.worldLine = serverName;

    if (mpdMs > 0)
        entry.durationMs = mpdMs;
    if (dateMs > 0)
        entry.lastModified = QDateTime::fromMSecsSinceEpoch(dateMs);
}

void InstanceRecordingsPage::parseFlashbackMeta(RecordingEntry &entry,
                                                const QByteArray &metaJson) const
{
    const QJsonObject json = QJsonDocument::fromJson(metaJson).object();
    if (json.isEmpty())
        return;

    const QString name = json.value(QStringLiteral("name")).toString();
    if (!name.isEmpty() && name != QStringLiteral("Unnamed"))
        entry.title = name;

    const QString worldName = json.value(QStringLiteral("world_name")).toString();
    if (!worldName.isEmpty())
        entry.worldLine = worldName;

    const qint64 totalTicks = static_cast<qint64>(
        json.value(QStringLiteral("total_ticks")).toDouble(-1));
    if (totalTicks > 0)
        entry.durationMs = totalTicks * 1000 / 20; // 20 tick = 1 秒
}

QPixmap InstanceRecordingsPage::extractPreviewIcon(const QString &filePath) const
{
    QByteArray data;
    // 常见预览图条目名优先；都找不到再遍历取第一张根目录下的 PNG
    const QStringList candidates = {QStringLiteral("thumb.png"),
                                    QStringLiteral("thumbnail.png"),
                                    QStringLiteral("icon.png")};
    for (const QString &name : candidates)
    {
        if (JarUtils::extractFromJarToMemory(filePath, name, data) && !data.isEmpty())
            break;
        data.clear();
    }
    if (data.isEmpty())
    {
        const QStringList entries = JarUtils::listEntriesInJar(filePath);
        for (const QString &e : entries)
        {
            if (!e.contains(QLatin1Char('/'))
                && e.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
                && JarUtils::extractFromJarToMemory(filePath, e, data) && !data.isEmpty())
            {
                break;
            }
            data.clear();
        }
    }

    QPixmap pm;
    if (!data.isEmpty() && pm.loadFromData(data, "PNG"))
        return pm.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return {};
}

// ───────────────────────────── 列表构建 ─────────────────────────────

void InstanceRecordingsPage::clearCards()
{
    // 跳过末尾的 stretch
    while (m_cardListLayout->count() > 1)
    {
        QLayoutItem *item = m_cardListLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void InstanceRecordingsPage::rebuildCards()
{
    clearCards();

    // 筛选出要展示的条目
    const QString needle = m_currentSearch.toLower();
    const QDateTime now = QDateTime::currentDateTime();
    QList<const RecordingEntry *> visible;
    for (const RecordingEntry &entry : std::as_const(m_entries))
    {
        // 来源
        if (m_sourceFilter != 0
            && entry.source != (m_sourceFilter == 1 ? ReplayMod : Flashback))
        {
            continue;
        }
        // 搜索：匹配标题 / 文件名 / 服务器或世界
        if (!needle.isEmpty()
            && !entry.title.toLower().contains(needle)
            && !entry.fileName.toLower().contains(needle)
            && !entry.worldLine.toLower().contains(needle))
        {
            continue;
        }
        // 时间
        if (m_timeFilter == 1 && entry.lastModified < now.addDays(-7))
            continue;
        if (m_timeFilter == 2 && entry.lastModified < now.addDays(-30))
            continue;
        if (m_timeFilter == 3 && entry.lastModified < now.addDays(-365))
            continue;
        // 大小
        if (m_sizeFilter == 1 && entry.fileSize >= k10MB)
            continue;
        if (m_sizeFilter == 2 && (entry.fileSize < k10MB || entry.fileSize >= k100MB))
            continue;
        if (m_sizeFilter == 3 && entry.fileSize < k100MB)
            continue;
        visible.append(&entry);
    }

    // 排序（0=名称↑ 1=名称↓ 2=大小↓ 3=大小↑ 4=新→旧 5=旧→新）
    const auto byNameAsc = [](const RecordingEntry *a, const RecordingEntry *b) {
        return a->title.compare(b->title, Qt::CaseInsensitive) < 0;
    };
    switch (m_sortMode)
    {
    case 0: std::stable_sort(visible.begin(), visible.end(), byNameAsc); break;
    case 1: std::stable_sort(visible.begin(), visible.end(),
                             [](const RecordingEntry *a, const RecordingEntry *b) {
                                 return b->title.compare(a->title, Qt::CaseInsensitive) < 0;
                             });
            break;
    case 2: std::stable_sort(visible.begin(), visible.end(),
                             [](const RecordingEntry *a, const RecordingEntry *b) {
                                 return a->fileSize > b->fileSize;
                             });
            break;
    case 3: std::stable_sort(visible.begin(), visible.end(),
                             [](const RecordingEntry *a, const RecordingEntry *b) {
                                 return a->fileSize < b->fileSize;
                             });
            break;
    case 4: std::stable_sort(visible.begin(), visible.end(),
                             [](const RecordingEntry *a, const RecordingEntry *b) {
                                 return a->lastModified > b->lastModified;
                             });
            break;
    case 5: std::stable_sort(visible.begin(), visible.end(),
                             [](const RecordingEntry *a, const RecordingEntry *b) {
                                 return a->lastModified < b->lastModified;
                             });
            break;
    default: break;
    }

    for (const RecordingEntry *entry : visible)
        m_cardListLayout->insertWidget(m_cardListLayout->count() - 1,
                                       createRecordingCard(*entry));

    m_visibleCount = visible.size();
    updateStats();
}

QWidget *InstanceRecordingsPage::createRecordingCard(const RecordingEntry &entry)
{
    // 卡片结构与其他本地资源页的列表项完全一致（modCardListItem 体系）
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("filePath", entry.filePath);
    card->setProperty("fileName", entry.fileName);
    card->installEventFilter(this);

    QHBoxLayout *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    // 图标 - 优先用录像内置截图，失败则回退到录像 SVG 图标
    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        const QString cacheKey = QStringLiteral("rec_icon:%1").arg(entry.filePath);
        QPixmap icon;
        if (!QPixmapCache::find(cacheKey, &icon))
        {
            icon = entry.icon;
            if (!icon.isNull())
                QPixmapCache::insert(cacheKey, icon);
        }
        if (!icon.isNull())
        {
            iconLabel->setPixmap(
                icon.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        else
        {
            iconLabel->setPixmap(IconHelper::loadColoredIcon(
                                     ":/Images/Icons/nav_recording.svg",
                                     QColor(ThemeManager::instance()->currentThemeColor()), 36)
                                     .pixmap(36, 36));
        }
    }
    mainLayout->addWidget(iconLabel);

    // 信息区域：文件名 + 标签行（来源 / 时长 / 服务器或世界 / 大小 / 日期）
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(entry.title);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(10);
    nameLabel->setFont(nameFont);
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    auto addChip = [chipsLayout](const QString &text) {
        QLabel *chip = new QLabel(text);
        chip->setObjectName("modChip");
        chip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(chip);
    };

    addChip(entry.source == ReplayMod ? QStringLiteral("Replay Mod")
                                      : QStringLiteral("Flashback"));
    if (entry.durationMs > 0)
        addChip(formatDuration(entry.durationMs));
    if (!entry.worldLine.isEmpty())
        addChip(entry.worldLine);
    addChip(formatFileSize(entry.fileSize));
    addChip(entry.lastModified.toString(QStringLiteral("yyyy-MM-dd")));

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
    mainLayout->addLayout(infoLayout, 1);

    // 操作按钮（样式与其他资源页的 modCardActionBtn 一致）
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    auto createBtn = [&card](const QString &iconPath, const QString &tip) -> QPushButton * {
        QPushButton *btn = new QPushButton(card);
        btn->setObjectName("modCardActionBtn");
        btn->setProperty("cardRole", "actionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(
            iconPath, QColor(ThemeManager::instance()->currentThemeColor()), 18));
        btn->setIconSize(QSize(18, 18));
        return btn;
    };

    // 详情按钮：弹窗展示录像完整信息
    QPushButton *detailBtn = createBtn(":/Images/Icons/list.svg", tr("查看详情"));
    connect(detailBtn, &QPushButton::clicked, this, [this, entry]() {
        QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
                           .arg(entry.title, entry.filePath, formatFileSize(entry.fileSize),
                                entry.lastModified.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        if (!entry.worldLine.isEmpty())
            info += QStringLiteral("\n") + tr("服务器/世界: %1").arg(entry.worldLine);
        if (entry.durationMs > 0)
            info += QStringLiteral("\n") + tr("时长: %1").arg(formatDuration(entry.durationMs));
        AppMessageBox::information(this, tr("录像详情"), info);
    });
    btnLayout->addWidget(detailBtn);

    // 打开文件位置按钮
    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开文件位置"));
    connect(folderBtn, &QPushButton::clicked, this, [entry]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(entry.filePath).absolutePath()));
    });
    btnLayout->addWidget(folderBtn);

    // 删除按钮
    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, this, [this, entry]() {
        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this, tr("删除录像"),
            tr("确定要删除 \"%1\" 吗？此操作不可撤销。").arg(entry.title),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);
        if (reply != AppMessageBox::Yes)
            return;
        if (QFile::remove(entry.filePath))
        {
            QPixmapCache::remove(QStringLiteral("rec_icon:%1").arg(entry.filePath));
            scanFiles();
        }
        else
        {
            AppMessageBox::warning(this, tr("删除录像"),
                                   tr("删除失败，请检查文件是否被占用。"));
        }
    });
    btnLayout->addWidget(deleteBtn);

    mainLayout->addLayout(btnLayout);

    return card;
}

bool InstanceRecordingsPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick)
    {
        // 双击卡片：打开录像所在文件夹（录像文件本身无法直接播放）
        if (QWidget *card = qobject_cast<QWidget *>(watched))
        {
            const QString filePath = card->property("filePath").toString();
            if (!filePath.isEmpty())
            {
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void InstanceRecordingsPage::updateStats()
{
    // 当前来源筛选下的条目数（不含搜索/时间/大小筛选）
    int sourceCount = 0;
    qint64 sourceBytes = 0;
    for (const RecordingEntry &entry : std::as_const(m_entries))
    {
        if (m_sourceFilter != 0
            && entry.source != (m_sourceFilter == 1 ? ReplayMod : Flashback))
        {
            continue;
        }
        ++sourceCount;
        sourceBytes += entry.fileSize;
    }

    if (sourceCount == 0)
    {
        // 该来源一个录像都没有：说明对应文件夹还是空的，给路径提示 + 快速安装录像模组
        QString hintText;
        if (m_sourceFilter == 2)
            hintText = tr("暂无 Flashback 录像：录像保存在实例目录的 flashback/replays 文件夹内");
        else if (m_sourceFilter == 1)
            hintText = tr("暂无 Replay Mod 录像：录像保存在实例目录的 replay_recordings 文件夹内");
        else
            hintText = tr("暂无录像：Replay Mod 录像保存在实例目录的 replay_recordings 文件夹，"
                          "Flashback 录像保存在 flashback/replays 文件夹");
        showEmptyState(hintText, true);
        m_statsLabel->setText(tr("共 0 个录像"));
        return;
    }

    if (m_visibleCount == 0)
    {
        // 有录像但被搜索/时间/大小筛掉了：换成"无匹配"提示，不误导去翻文件夹
        showEmptyState(tr("没有符合筛选条件的录像"), false);
        m_statsLabel->setText(tr("显示 0 个录像 (共 %1 个)").arg(sourceCount));
        return;
    }

    m_emptyWidget->hide();
    m_scrollArea->show();

    if (anyFilterActive())
    {
        // 有筛选：仿资源页显示「显示 N 个 (共 M 个)」
        m_statsLabel->setText(tr("显示 %1 个录像 (共 %2 个)")
                                  .arg(m_visibleCount).arg(sourceCount));
    }
    else
    {
        m_statsLabel->setText(tr("共 %1 个录像 · 占用 %2")
                                  .arg(sourceCount)
                                  .arg(formatFileSize(sourceBytes)));
    }
}

// ───────────────────────────── 工具函数 ─────────────────────────────

void InstanceRecordingsPage::showEmptyState(const QString &text, bool allowInstall)
{
    m_emptyLabel->setText(text);
    m_emptyWidget->show();
    // 只露出当前来源对应的安装入口：「全部」时两个都给
    m_installReplayBtn->setVisible(allowInstall && m_sourceFilter != 2);
    m_installFlashbackBtn->setVisible(allowInstall && m_sourceFilter != 1);
    m_scrollArea->hide();
}

QString InstanceRecordingsPage::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024), 'f', 1));
    return QStringLiteral("%1 GB").arg(QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2));
}

QString InstanceRecordingsPage::formatDuration(qint64 ms) const
{
    const qint64 totalSec = (ms + 500) / 1000;
    const qint64 h = totalSec / 3600;
    const qint64 m = (totalSec % 3600) / 60;
    const qint64 s = totalSec % 60;
    if (h > 0)
        return tr("%1时%2分%3秒").arg(h).arg(m).arg(s);
    if (m > 0)
        return tr("%1分%2秒").arg(m).arg(s);
    return tr("%1秒").arg(s);
}
