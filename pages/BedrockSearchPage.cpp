#include "BedrockSearchPage.h"

#include "components/OutlinedLabel.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QSizePolicy>

#include "utils/ThemeManager.h"

BedrockSearchPage::BedrockSearchPage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
}

void BedrockSearchPage::setSearchFocus()
{
    m_searchInput->setFocus();
    m_searchInput->selectAll();
}

void BedrockSearchPage::clearSearch()
{
    m_searchInput->clear();
    clearResults();
}

QWidget *BedrockSearchPage::createSearchBar()
{
    auto *container = new QWidget();
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignCenter);

    m_searchInput = new QLineEdit();
    m_searchInput->setObjectName("searchPageInput");
    m_searchInput->setPlaceholderText(tr("搜索基岩版实例、资源包、行为包、皮肤包、地图..."));
    m_searchInput->setFixedWidth(900);
    m_searchInput->setFixedHeight(48);
    layout->addWidget(m_searchInput);

    connect(m_searchInput, &QLineEdit::returnPressed, this, [this]() {
        performSearch(m_searchInput->text().trimmed());
    });

    return container;
}

QWidget *BedrockSearchPage::createFilterBar()
{
    auto *container = new QWidget();
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignCenter);

    m_filterCombo = new QComboBox();
    m_filterCombo->setObjectName("searchFilterCombo");
    m_filterCombo->setFixedHeight(36);
    m_filterCombo->addItem(tr("全部"));
    m_filterCombo->addItem(tr("实例"));
    m_filterCombo->addItem(tr("内容"));
    layout->addWidget(m_filterCombo);

    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BedrockSearchPage::onFilterChanged);

    return container;
}

QWidget *BedrockSearchPage::createSubFilterBar()
{
    m_subFilterCombo = new QComboBox();
    m_subFilterCombo->setObjectName("searchSubFilterCombo");
    m_subFilterCombo->setFixedHeight(32);

    connect(m_subFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BedrockSearchPage::onSubFilterChanged);

    onFilterChanged(0);

    return m_subFilterCombo;
}

QWidget *BedrockSearchPage::createResultsArea()
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(40, 0, 40, 0);
    layout->setSpacing(8);

    m_resultsTitle = new QLabel();
    m_resultsTitle->setObjectName("searchResultsTitle");
    m_resultsTitle->setVisible(false);
    layout->addWidget(m_resultsTitle);

    m_noResultsLabel = new QLabel(tr("未找到匹配的基岩版内容"));
    m_noResultsLabel->setObjectName("searchNoResults");
    m_noResultsLabel->setAlignment(Qt::AlignCenter);
    m_noResultsLabel->setVisible(false);
    layout->addWidget(m_noResultsLabel);

    m_resultsScroll = new QScrollArea();
    m_resultsScroll->setObjectName("searchPageResultsScroll");
    m_resultsScroll->setWidgetResizable(true);
    m_resultsScroll->setFrameShape(QFrame::NoFrame);
    m_resultsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_resultsScroll->setVisible(false);

    m_resultsContainer = new QWidget();
    m_resultsContainer->setObjectName("searchResultsInnerContainer");
    m_resultsLayout = new QVBoxLayout(m_resultsContainer);
    m_resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_resultsLayout->setSpacing(6);
    m_resultsScroll->setWidget(m_resultsContainer);
    layout->addWidget(m_resultsScroll, 1);

    return container;
}

void BedrockSearchPage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *topWidget = new QWidget();
    auto *topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);

    topLayout->addSpacing(60);

    auto *titleLabel = new OutlinedLabel(tr("基岩版搜索"));
    titleLabel->setObjectName("searchPageTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    topLayout->addWidget(titleLabel);

    topLayout->addSpacing(12);
    topLayout->addWidget(createSearchBar());

    topLayout->addSpacing(12);
    topLayout->addWidget(createFilterBar());

    topLayout->addSpacing(4);
    topLayout->addWidget(createSubFilterBar());

    topLayout->addSpacing(8);

    mainLayout->addWidget(topWidget);
    mainLayout->addWidget(createResultsArea(), 1);
}

void BedrockSearchPage::onFilterChanged(int index)
{
    m_currentFilter = index;
    m_currentSubFilter = 0;

    disconnect(m_subFilterCombo, nullptr, this, nullptr);

    m_subFilterCombo->blockSignals(true);
    m_subFilterCombo->clear();

    QStringList subFilters;
    switch (index)
    {
    case 0: // 全部
    case 1: // 实例
        m_subFilterCombo->setVisible(false);
        break;
    case 2: // 内容
        subFilters = {tr("全部"), tr("资源包"), tr("行为包"), tr("皮肤包"), tr("地图")};
        m_subFilterCombo->setVisible(true);
        break;
    }

    for (const QString &filter : subFilters)
        m_subFilterCombo->addItem(filter);

    m_subFilterCombo->blockSignals(false);

    if (!subFilters.isEmpty())
    {
        m_currentSubFilter = 0;
        connect(m_subFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BedrockSearchPage::onSubFilterChanged);
    }

    if (!m_searchInput->text().trimmed().isEmpty())
        performSearch(m_searchInput->text().trimmed());
}

void BedrockSearchPage::onSubFilterChanged(int index)
{
    if (m_currentSubFilter == index)
        return;
    m_currentSubFilter = index;
    if (!m_searchInput->text().trimmed().isEmpty())
        performSearch(m_searchInput->text().trimmed());
}

void BedrockSearchPage::performSearch(const QString &keyword)
{
    if (keyword.isEmpty())
    {
        clearResults();
        return;
    }

    QList<BedrockSearchResult> results;

    // 主分类：0=全部, 1=实例, 2=内容
    // 内容子分类：0=全部, 1=资源包, 2=行为包, 3=皮肤包, 4=地图
    if (m_currentFilter == 0 || m_currentFilter == 1)
        results.append(searchInstances(keyword));
    if (m_currentFilter == 0)
    {
        for (int kind = 0; kind <= 3; ++kind)
            results.append(searchPacks(keyword, kind));
    }
    else if (m_currentFilter == 2)
    {
        if (m_currentSubFilter == 0)
        {
            for (int kind = 0; kind <= 3; ++kind)
                results.append(searchPacks(keyword, kind));
        }
        else if (m_currentSubFilter >= 1 && m_currentSubFilter <= 4)
        {
            results.append(searchPacks(keyword, m_currentSubFilter - 1));
        }
    }

    showResults(results);
}

QList<BedrockSearchResult> BedrockSearchPage::searchInstances(const QString &keyword)
{
    QList<BedrockSearchResult> results;
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    mgr->ensureInitialized();

    const QVector<BedrockInstance> instances = mgr->instances();
    for (const auto &inst : instances)
    {
        bool nameMatch = inst.name.contains(keyword, Qt::CaseInsensitive);
        bool versionMatch = inst.version.contains(keyword, Qt::CaseInsensitive);
        bool idMatch = inst.id.contains(keyword, Qt::CaseInsensitive);
        if (!nameMatch && !versionMatch && !idMatch)
            continue;

        BedrockSearchResult item;
        item.title = inst.name;
        item.subtitle = tr("基岩版实例");
        item.description = inst.version.isEmpty()
            ? inst.dataDir
            : tr("基岩版 %1").arg(inst.version);
        item.iconText = "BE";
        item.kind = BedrockSearchResult::Instance;
        item.instanceId = inst.id;
        results.append(item);
    }

    return results;
}

QList<BedrockSearchResult> BedrockSearchPage::searchPacks(const QString &keyword, int packKind)
{
    QList<BedrockSearchResult> results;

    // 0=资源包, 1=行为包, 2=皮肤包, 3=地图
    struct PackSpec {
        QString dirName;
        QString displayName;
        QString icon;
    };
    static const PackSpec specs[] = {
        {QStringLiteral("resource_packs"),  QStringLiteral("资源包"), QStringLiteral("RP")},
        {QStringLiteral("behavior_packs"),  QStringLiteral("行为包"), QStringLiteral("BP")},
        {QStringLiteral("skin_packs"),      QStringLiteral("皮肤包"), QStringLiteral("SP")},
        {QStringLiteral("minecraftWorlds"), QStringLiteral("地图"),   QStringLiteral("WD")},
    };
    if (packKind < 0 || packKind > 3)
        return results;
    const PackSpec &spec = specs[packKind];

    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    mgr->ensureInitialized();

    const QVector<BedrockInstance> instances = mgr->instances();
    for (const auto &inst : instances)
    {
        QDir contentDir(inst.dataDir + QStringLiteral("/com.mojang/") + spec.dirName);
        if (!contentDir.exists())
            continue;

        QFileInfoList entries;
        if (packKind == 3)
        {
            // 地图：仅统计含 level.dat 的世界目录
            const QFileInfoList dirs = contentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &d : dirs)
            {
                if (QFile::exists(d.absoluteFilePath() + QStringLiteral("/level.dat")))
                    entries.append(d);
            }
        }
        else
        {
            entries = contentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        }

        for (const QFileInfo &entry : entries)
        {
            QString displayName = entry.fileName();
            if (!displayName.contains(keyword, Qt::CaseInsensitive))
                continue;

            BedrockSearchResult item;
            item.title = displayName;
            item.subtitle = tr("%1 - %2").arg(spec.displayName, inst.name);
            item.description = entry.absoluteFilePath();
            item.iconText = spec.icon;
            item.kind = BedrockSearchResult::Pack;
            item.instanceId = inst.id;
            item.packPath = entry.absoluteFilePath();
            results.append(item);
        }
    }

    return results;
}

void BedrockSearchPage::showResults(const QList<BedrockSearchResult> &results)
{
    clearResults();
    m_currentResults = results;

    if (results.isEmpty())
    {
        m_resultsTitle->setVisible(false);
        m_resultsScroll->setVisible(false);
        m_noResultsLabel->setVisible(true);
        return;
    }

    m_noResultsLabel->setVisible(false);
    m_resultsTitle->setText(tr("搜索结果 (%1 项)").arg(results.size()));
    m_resultsTitle->setVisible(true);

    QColor teal("#00897B");
    for (int i = 0; i < results.size(); ++i)
    {
        const auto &item = results[i];

        auto *card = new QWidget(m_resultsContainer);
        card->setObjectName("searchResultCard");
        card->setFixedHeight(68);
        card->setCursor(Qt::PointingHandCursor);
        card->setProperty("resultIndex", i);
        card->setProperty("resultType", "bedrock");

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(14, 10, 14, 10);
        cardLayout->setSpacing(12);

        auto *iconLabel = new QLabel();
        iconLabel->setObjectName("searchResultIcon");
        iconLabel->setFixedSize(44, 44);
        iconLabel->setAlignment(Qt::AlignCenter);
        {
            QPixmap pix(44, 44);
            pix.fill(Qt::transparent);
            QPainter painter(&pix);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(teal);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(0, 0, 44, 44, 10, 10);
            painter.setPen(Qt::white);
            QFont font;
            font.setBold(true);
            font.setPixelSize(18);
            painter.setFont(font);
            painter.drawText(pix.rect(), Qt::AlignCenter, item.iconText.isEmpty() ? "?" : item.iconText.left(2));
            painter.end();
            iconLabel->setPixmap(pix);
        }
        cardLayout->addWidget(iconLabel);

        auto *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(4);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        auto *titleLabel = new QLabel(item.title);
        titleLabel->setObjectName("searchResultTitle");
        infoLayout->addWidget(titleLabel);

        auto *subtitleLabel = new QLabel(item.subtitle);
        subtitleLabel->setObjectName("searchResultSubtitle");
        infoLayout->addWidget(subtitleLabel);

        cardLayout->addLayout(infoLayout, 1);

        auto *descLabel = new QLabel(item.description);
        descLabel->setObjectName("searchResultDescription");
        descLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cardLayout->addWidget(descLabel);

        m_resultsLayout->addWidget(card);
        card->installEventFilter(this);
    }

    m_resultsLayout->addStretch();
    m_resultsScroll->setVisible(true);
}

void BedrockSearchPage::clearResults()
{
    m_currentResults.clear();
    m_resultsTitle->setVisible(false);
    m_noResultsLabel->setVisible(false);
    m_resultsScroll->setVisible(false);

    while (QLayoutItem *layoutItem = m_resultsLayout->takeAt(0))
    {
        if (layoutItem->widget())
        {
            layoutItem->widget()->removeEventFilter(this);
            delete layoutItem->widget();
        }
        delete layoutItem;
    }
}

void BedrockSearchPage::onResultClicked(int index)
{
    if (index < 0 || index >= m_currentResults.size())
        return;

    const auto &item = m_currentResults[index];
    if (item.kind == BedrockSearchResult::Instance)
        emit navigateToBedrockInstance(item.instanceId);
    else
        emit openPackFolder(item.packPath);
}

bool BedrockSearchPage::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *widget = qobject_cast<QWidget *>(obj);
        if (widget)
        {
            bool ok;
            int idx = widget->property("resultIndex").toInt(&ok);
            if (ok)
            {
                onResultClicked(idx);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
