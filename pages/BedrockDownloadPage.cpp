/**
 * @file   BedrockDownloadPage.cpp
 * @brief  基岩版版本列表页实现
 * @author BlockBox Team
 */

#include "BedrockDownloadPage.h"

#include <QFont>
#include <QHBoxLayout>
#include <QPainter>
#include <QShowEvent>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "components/OutlinedLabel.h"
#include "utils/IconHelper.h"
#include "utils/SystemInfo.h"
#include "utils/ThemeManager.h"

BedrockDownloadPage::BedrockDownloadPage(QWidget *parent)
    : QWidget(parent)
    , m_loaded(false)
{
    initUI();

    // 根据系统名自动选择合适的安装类型（源）：
    // Windows → Windows 版（mcappx 直链）；其它 → Android 版（mcapks 网盘）
    const QString autoSource = SystemInfo::isWindows() ? QStringLiteral("mcappx")
                                                       : QStringLiteral("mcapks");
    const int autoIndex = m_sourceCombo->findData(autoSource);
    if (autoIndex >= 0)
        m_sourceCombo->setCurrentIndex(autoIndex);

    BedrockVersionService *service = BedrockVersionService::instance();
    connect(service, &BedrockVersionService::versionListFetched,
            this, &BedrockDownloadPage::onVersionListFetched);
    connect(service, &BedrockVersionService::fetchError,
            this, &BedrockDownloadPage::onFetchError);
}

BedrockDownloadPage::~BedrockDownloadPage()
{
}

void BedrockDownloadPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 首次显示时加载版本列表；再次进入不重新拉取（避免反复请求）
    if (!m_loaded)
        refresh();
}

void BedrockDownloadPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 24, 32, 24);
    mainLayout->setSpacing(16);

    // ── 标题区 ──
    m_titleLabel = new OutlinedLabel(tr("基岩版下载"), this);
    m_titleLabel->setObjectName("sectionTitle");

    m_subtitleLabel = new QLabel(tr("从 mcappx.com（Windows 版）与 mcapks.net、bbk.endyun.ltd（Android 版）获取基岩版新版本。"
                                     "点击版本进入详情页进行下载安装。"), this);
    m_subtitleLabel->setObjectName("sectionSubtitle");
    m_subtitleLabel->setWordWrap(true);

    // ── 筛选栏（与 Java 版安装实例页一致：微透明玻璃态卡片）──
    QWidget *filterBar = new QWidget(this);
    filterBar->setObjectName("filterCard");
    QHBoxLayout *filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(14, 10, 14, 10);
    filterLayout->setSpacing(10);

    QLabel *sourceLabel = new QLabel(tr("来源"), filterBar);
    sourceLabel->setObjectName("loaderSectionLabel");
    m_sourceCombo = new QComboBox(filterBar);
    m_sourceCombo->addItem(tr("全部来源"), QString());
    m_sourceCombo->addItem(tr("Windows 版（mcappx）"), QStringLiteral("mcappx"));
    m_sourceCombo->addItem(tr("Android 版（mcapks）"), QStringLiteral("mcapks"));
    m_sourceCombo->addItem(tr("Android 版（bbk）"), QStringLiteral("bbk"));
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedrockDownloadPage::onSourceFilterChanged);

    QLabel *typeLabel = new QLabel(tr("类型"), filterBar);
    typeLabel->setObjectName("loaderSectionLabel");
    m_typeCombo = new QComboBox(filterBar);
    m_typeCombo->addItem(tr("全部类型"), -1);
    m_typeCombo->addItem(tr("正式版"), static_cast<int>(BedrockVersionType::Release));
    m_typeCombo->addItem(tr("测试版"), static_cast<int>(BedrockVersionType::Beta));
    m_typeCombo->addItem(tr("预览版"), static_cast<int>(BedrockVersionType::Preview));
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedrockDownloadPage::onTypeFilterChanged);

    m_refreshBtn = new QPushButton(tr("刷新"), filterBar);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &BedrockDownloadPage::onRefreshClicked);

    m_statusHintLabel = new QLabel(filterBar);
    m_statusHintLabel->setObjectName("hintLabel");

    filterLayout->addWidget(sourceLabel);
    filterLayout->addWidget(m_sourceCombo);
    filterLayout->addWidget(typeLabel);
    filterLayout->addWidget(m_typeCombo);
    filterLayout->addWidget(m_refreshBtn);
    filterLayout->addWidget(m_statusHintLabel);
    filterLayout->addStretch();

    // ── 版本列表（Java 版卡片样式）──
    m_versionList = new QListWidget(this);
    m_versionList->setObjectName("versionListWidget");
    m_versionList->setFrameShape(QFrame::NoFrame);
    m_versionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_versionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_versionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_versionList->setSpacing(8);
    m_versionList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_versionList->setMinimumHeight(320);
    connect(m_versionList, &QListWidget::currentRowChanged,
            this, [this]() { syncCardSelection(); });
    connect(m_versionList, &QListWidget::itemClicked,
            this, &BedrockDownloadPage::onCardItemClicked);

    // ── 组装 ──
    mainLayout->addWidget(m_titleLabel);
    mainLayout->addWidget(m_subtitleLabel);
    mainLayout->addWidget(filterBar);
    mainLayout->addWidget(m_versionList, 1);

    updateUIState();
}

void BedrockDownloadPage::refresh()
{
    m_statusHintLabel->setText(tr("正在获取版本列表..."));
    m_versionList->clear();
    m_allVersions.clear();
    m_filteredVersions.clear();
    BedrockVersionService::instance()->fetchVersionList();
}

// ────────────────────────────────────────────────────────────
// 信号处理
// ────────────────────────────────────────────────────────────

void BedrockDownloadPage::onVersionListFetched(const QVector<BedrockVersionEntry> &versions)
{
    m_loaded = true;
    m_allVersions = versions;
    m_statusHintLabel->setText(tr("共 %1 个版本").arg(versions.size()));
    populateVersionList();
}

void BedrockDownloadPage::onFetchError(const QString &sourceName, const QString &error)
{
    NotificationManager::showError(this, tr("%1 版本列表获取失败: %2").arg(sourceName, error));
}

// ────────────────────────────────────────────────────────────
// 过滤与列表
// ────────────────────────────────────────────────────────────

void BedrockDownloadPage::onSourceFilterChanged()
{
    populateVersionList();
}

void BedrockDownloadPage::onTypeFilterChanged()
{
    populateVersionList();
}

void BedrockDownloadPage::onRefreshClicked()
{
    refresh();
}

void BedrockDownloadPage::populateVersionList()
{
    const QString sourceFilter = m_sourceCombo->currentData().toString();
    const int typeFilter = m_typeCombo->currentData().toInt();

    m_filteredVersions.clear();
    for (const BedrockVersionEntry &e : m_allVersions) {
        if (!sourceFilter.isEmpty() && e.source != sourceFilter)
            continue;
        if (typeFilter >= 0 && static_cast<int>(e.type) != typeFilter)
            continue;
        m_filteredVersions.append(e);
    }

    m_versionList->clear();

    for (int i = 0; i < m_filteredVersions.size(); ++i) {
        const BedrockVersionEntry &e = m_filteredVersions[i];

        QListWidgetItem *item = new QListWidgetItem(m_versionList);
        item->setData(Qt::UserRole, e.version);

        QWidget *card = createVersionCard(e, i);
        m_versionList->addItem(item);
        m_versionList->setItemWidget(item, card);
        item->setSizeHint(card->sizeHint().expandedTo(QSize(0, 76)));
    }

    if (m_filteredVersions.isEmpty()) {
        m_statusHintLabel->setText(tr("没有匹配的版本"));
    } else {
        m_versionList->setCurrentRow(0);
        syncCardSelection();
    }
    updateUIState();
}

void BedrockDownloadPage::onCardItemClicked(QListWidgetItem *item)
{
    const int row = m_versionList->row(item);
    if (row < 0 || row >= m_filteredVersions.size())
        return;
    m_versionList->setCurrentRow(row);
    syncCardSelection();
    emit versionDetailRequested(m_filteredVersions[row]);
}

QWidget *BedrockDownloadPage::createVersionCard(const BedrockVersionEntry &entry, int index)
{
    // 与 Java 版安装实例页版本卡片一致的样式（modCardListItem）
    QWidget *card = new QWidget(m_versionList);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setProperty("selected", false);
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);

    QHBoxLayout *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    // ── 图标：按版本类型着色 + 白色字母（与 Java 版一致）──
    QLabel *iconLabel = new QLabel(card);
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        QColor iconColor = (entry.type == BedrockVersionType::Release) ? QColor("#4CAF50")
                          : (entry.type == BedrockVersionType::Beta) ? QColor("#FF9800")
                                                                     : QColor("#9E9E9E");
        QPixmap placeholderIcon(48, 48);
        placeholderIcon.fill(iconColor.lighter(180));
        QPainter painter(&placeholderIcon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        QFont font;
        font.setBold(true);
        font.setPixelSize(24);
        painter.setFont(font);
        QString letter = entry.version.trimmed().isEmpty() ? QStringLiteral("?")
                         : entry.version.trimmed().left(1).toUpper();
        painter.drawText(placeholderIcon.rect(), Qt::AlignCenter, letter);
        painter.end();
        iconLabel->setPixmap(placeholderIcon);
    }
    mainLayout->addWidget(iconLabel);

    // ── 名称 + chips（日期 / 类型 / 来源）──
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(entry.version, card);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget(card);
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    auto addChip = [&](const QString &text) {
        QLabel *chip = new QLabel(text, chipsWidget);
        chip->setObjectName("modChip");
        chip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(chip);
    };

    if (!entry.date.isEmpty())
        addChip(entry.date);
    addChip(versionTypeName(entry.type));
    addChip(entry.source == QLatin1String("mcappx") ? tr("Windows") : tr("Android"));
    if (entry.source == QLatin1String("mcappx") && entry.hasDirectPackage)
        addChip(tr("可下载"));
    chipsLayout->addStretch();

    infoLayout->addWidget(chipsWidget);
    mainLayout->addLayout(infoLayout, 1);

    // ── 下载操作按钮（点击进入详情页）──
    QPushButton *dlBtn = new QPushButton(card);
    dlBtn->setObjectName("modCardActionBtn");
    dlBtn->setProperty("cardRole", "actionBtn");
    dlBtn->setFixedSize(32, 32);
    dlBtn->setToolTip(tr("下载"));
    dlBtn->setCursor(Qt::PointingHandCursor);
    dlBtn->setIcon(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/download.svg"),
                                               QColor(ThemeManager::instance()->currentThemeColor()), 18));
    dlBtn->setIconSize(QSize(18, 18));
    dlBtn->setEnabled(entry.source != QLatin1String("mcappx") || entry.hasDirectPackage);
    connect(dlBtn, &QPushButton::clicked, this, [this, index]() {
        if (index < 0 || index >= m_filteredVersions.size())
            return;
        m_versionList->setCurrentRow(index);
        syncCardSelection();
        emit versionDetailRequested(m_filteredVersions[index]);
    });
    mainLayout->addWidget(dlBtn);

    return card;
}

void BedrockDownloadPage::syncCardSelection()
{
    const int current = m_versionList->currentRow();
    for (int i = 0; i < m_versionList->count(); ++i) {
        QWidget *card = m_versionList->itemWidget(m_versionList->item(i));
        if (!card)
            continue;
        const bool sel = (i == current);
        if (card->property("selected").toBool() != sel) {
            card->setProperty("selected", sel);
            card->style()->unpolish(card);
            card->style()->polish(card);
        }
    }
}

// ────────────────────────────────────────────────────────────
// 辅助
// ────────────────────────────────────────────────────────────

void BedrockDownloadPage::updateUIState()
{
    const bool busy = m_versionList->count() == 0 && !m_statusHintLabel->text().isEmpty();
    Q_UNUSED(busy);
}

QString BedrockDownloadPage::versionTypeName(BedrockVersionType type) const
{
    switch (type) {
    case BedrockVersionType::Release: return tr("正式版");
    case BedrockVersionType::Beta:    return tr("测试版");
    case BedrockVersionType::Preview: return tr("预览版");
    }
    return tr("未知");
}
