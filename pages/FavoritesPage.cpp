/**
 * @file   FavoritesPage.cpp
 * @brief  收藏夹内容展示页面实现
 * @author BlockBox Team
 * @date   2026-07-18
 */
#include "FavoritesPage.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPixmap>
#include <QPixmapCache>
#include <QScrollArea>
#include <QSpacerItem>
#include <QStandardPaths>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include "../components/FavoriteFolderDialog.h"
#include "../components/MasonryContentCard.h"
#include "../layouts/FlowLayout.h"
#include "../layouts/MasonryLayout.h"
#include "../components/NotificationManager.h"
#include "../utils/McimHelper.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

static QNetworkAccessManager *s_favIconNAM = nullptr;
static QNetworkDiskCache *s_favIconCache = nullptr;

static QNetworkAccessManager *sharedFavIconNAM()
{
    if (!s_favIconNAM)
    {
        s_favIconNAM = new QNetworkAccessManager();
        s_favIconCache = new QNetworkDiskCache();
        QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/fav_icons";
        QDir().mkpath(cachePath);
        s_favIconCache->setCacheDirectory(cachePath);
        s_favIconCache->setMaximumCacheSize(50 * 1024 * 1024);
        s_favIconNAM->setCache(s_favIconCache);
    }
    return s_favIconNAM;
}

FavoritesPage::FavoritesPage(QWidget *parent)
    : QWidget(parent)
    , m_headerWidget(nullptr)
    , m_titleLabel(nullptr)
    , m_countLabel(nullptr)
    , m_refreshBtn(nullptr)
    , m_manageBtn(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_cardFlowLayout(nullptr)
    , m_viewSwitch(nullptr)
    , m_viewMode(ContentViewSwitch::Masonry)
    , m_emptyLabel(nullptr)
    , m_iconNAM(nullptr)
    , m_iconCache(nullptr)
{
    initUI();
}

FavoritesPage::~FavoritesPage()
{
}

void FavoritesPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header
    m_headerWidget = new QWidget(this);
    m_headerWidget->setObjectName("favoritesHeader");
    m_headerWidget->setFixedHeight(64);

    QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(24, 12, 24, 12);
    headerLayout->setSpacing(12);

    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(m_headerWidget);
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: 600; color: #202124;");
    titleLayout->addWidget(m_titleLabel);

    m_countLabel = new QLabel(m_headerWidget);
    m_countLabel->setStyleSheet("font-size: 12px; color: #888888;");
    titleLayout->addWidget(m_countLabel);

    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    m_manageBtn = new QPushButton(m_headerWidget);
    m_manageBtn->setObjectName("favManageBtn");
    m_manageBtn->setText(tr("管理收藏夹"));
    m_manageBtn->setCursor(Qt::PointingHandCursor);
    m_manageBtn->setToolTip(tr("新建或重命名收藏夹"));
    connect(m_manageBtn, &QPushButton::clicked, this, &FavoritesPage::manageFoldersRequested);
    headerLayout->addWidget(m_manageBtn);

    m_refreshBtn = new QPushButton(m_headerWidget);
    m_refreshBtn->setObjectName("favRefreshBtn");
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setToolTip(tr("刷新"));
    m_refreshBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/refresh.svg", themeColor, 16));
    m_refreshBtn->setIconSize(QSize(16, 16));
    m_refreshBtn->setFixedSize(34, 34);
    connect(m_refreshBtn, &QPushButton::clicked, this, &FavoritesPage::onRefreshClicked);
    headerLayout->addWidget(m_refreshBtn);

    // 视图切换（列表式 / 瀑布流）
    m_viewSwitch = new ContentViewSwitch(m_headerWidget);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted("favorites", ContentViewSwitch::Masonry));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &FavoritesPage::onViewModeChanged);
    headerLayout->addWidget(m_viewSwitch);

    mainLayout->addWidget(m_headerWidget);

    // Scroll area with cards
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_cardContainer = new QWidget(m_scrollArea);
    m_cardContainer->setObjectName("favoritesContainer");
    if (m_viewMode == ContentViewSwitch::List) {
        m_cardGridLayout = new QGridLayout(m_cardContainer);
        m_cardGridLayout->setContentsMargins(24, 16, 24, 24);
        m_cardGridLayout->setSpacing(10);
        m_cardGridLayout->setColumnStretch(0, 1);
    } else {
        m_cardFlowLayout = new MasonryLayout(m_cardContainer, 0, 12, 12);
        m_cardFlowLayout->setContentsMargins(24, 16, 24, 24);
    }

    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // Empty hint
    m_emptyLabel = new QLabel(m_scrollArea);
    m_emptyLabel->setText(tr("这个收藏夹还是空的\n\n在资源详情页点击星标按钮即可添加资源"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(
        "color: #999999;"
        "font-size: 14px;"
        "background: transparent;"
        );
    m_emptyLabel->hide();
}

void FavoritesPage::setFolder(const QString &folderId)
{
    m_folderId = folderId;
    FavoriteFolder f = FavoritesManager::instance()->folder(folderId);
    m_folderName = f.name;
    m_items = f.items;

    rebuildHeader();

    clearCards();
    for (const FavoriteItem &item : m_items)
    {
        addCard(item);
    }
    placeCards();

    showEmptyHint(m_items.isEmpty());
}

void FavoritesPage::rebuildHeader()
{
    m_titleLabel->setText(m_folderName.isEmpty() ? tr("收藏夹") : m_folderName);
    m_countLabel->setText(tr("共 %1 个资源").arg(m_items.size()));
}

void FavoritesPage::clearCards()
{
    for (QWidget *w : m_cardWidgets)
    {
        if (m_cardGridLayout)
            m_cardGridLayout->removeWidget(w);
        if (m_cardFlowLayout)
            m_cardFlowLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
    m_cardItems.clear();
}

void FavoritesPage::addCard(const FavoriteItem &item)
{
    m_cardItems.append(item);
    if (m_viewMode == ContentViewSwitch::List)
        addListCard(item);
    else
        addMasonryCard(item);
}

void FavoritesPage::addListCard(const FavoriteItem &item)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    // 水平方向拉伸占满列宽，垂直方向保持固定高度（由 setFixedHeight 控制）
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(card, &QWidget::customContextMenuRequested, this, &FavoritesPage::onCardContextMenu);

    QHBoxLayout *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap placeholder(48, 48);
    placeholder.fill(ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme
                         ? QColor("#2d2d2d") : QColor("#e8e8e8"));
    iconLabel->setPixmap(placeholder);
    mainLayout->addWidget(iconLabel);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QString displayName = item.displayName();
    QLabel *nameLabel = new QLabel(displayName);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    auto addChip = [&](const QString &text) {
        QLabel *chip = new QLabel(text);
        chip->setObjectName("modChip");
        chip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(chip);
    };

    addChip(item.displayType());
    if (!item.source.isEmpty())
    {
        addChip(item.source == "curseforge" ? QStringLiteral("CurseForge") : QStringLiteral("Modrinth"));
    }
    if (item.downloadCount > 0)
    {
        QString countStr;
        if (item.downloadCount >= 1000000)
        {
            countStr = QString::number(item.downloadCount / 1000000.0, 'f', 1) + "M";
        }
        else if (item.downloadCount >= 1000)
        {
            countStr = QString::number(item.downloadCount / 1000.0, 'f', 1) + "K";
        }
        else
        {
            countStr = QString::number(item.downloadCount);
        }
        addChip(QStringLiteral("↓ ") + countStr);
    }
    if (item.addedAt > 0)
    {
        QDateTime addedTime = QDateTime::fromMSecsSinceEpoch(item.addedAt);
        addChip(addedTime.toString("yyyy-MM-dd"));
    }

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);

    mainLayout->addLayout(infoLayout, 1);

    // 操作按钮组：与 ContentDownloadPage 保持一致
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton* {
        QPushButton *btn = new QPushButton();
        btn->setObjectName("modCardActionBtn");
        btn->setProperty("cardRole", "actionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
        btn->setIconSize(QSize(18, 18));
        return btn;
    };



    // 收藏按钮（弹出收藏夹菜单，与其他页面一致）
    QPushButton *favBtn = createBtn(":/Images/Icons/star.svg", tr("收藏"));
    favBtn->setProperty("cardRole", "favBtn");
    connect(favBtn, &QPushButton::clicked, this, [this, item, favBtn]() {
        if (item.id.isEmpty())
        {
            NotificationManager::showInfo(this, tr("当前资源信息不完整，无法收藏"));
            return;
        }

        QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
        QList<QString> containingIds = FavoritesManager::instance()->foldersContaining(item.id);

        QMenu menu(this);
        menu.setWindowTitle(tr("收藏到分组"));

        QList<QAction *> folderActions;
        for (const FavoriteFolder &f : folders)
        {
            bool contains = containingIds.contains(f.id);
            QString label = f.name + (contains ? QStringLiteral("  ✓") : QString());
            QAction *act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(contains);
            act->setData(f.id);
            folderActions.append(act);
        }

        menu.addSeparator();
        QAction *newFolderAct = menu.addAction(tr("新建收藏夹..."));

        QAction *chosen = menu.exec(favBtn->mapToGlobal(QPoint(0, favBtn->height() + 4)));
        if (!chosen)
        {
            return;
        }

        if (chosen == newFolderAct)
        {
            FavoriteFolderDialog dlg(FavoriteFolderDialog::CreateMode, this);
            if (dlg.exec() == QDialog::Accepted)
            {
                QString newId = FavoritesManager::instance()->createFolder(dlg.folderName());
                if (!newId.isEmpty())
                {
                    ModInfo info = item.toModInfo();
                    FavoritesManager::instance()->addFavorite(newId, info,
                                                              static_cast<ContentType>(item.contentType),
                                                              item.source, item.bedrockClassId,
                                                              item.bedrockCategory);
                    NotificationManager::showSuccess(this, tr("已添加到新收藏夹“%1”").arg(dlg.folderName()));
                }
            }
            return;
        }

        QString folderId = chosen->data().toString();
        if (folderId.isEmpty())
        {
            return;
        }

        bool wasFav = containingIds.contains(folderId);
        if (wasFav)
        {
            FavoritesManager::instance()->removeFavorite(folderId, item.id);
            NotificationManager::showSuccess(this, tr("已从收藏夹移除"));
        }
        else
        {
            ModInfo info = item.toModInfo();
            FavoritesManager::instance()->addFavorite(folderId, info,
                                                      static_cast<ContentType>(item.contentType),
                                                      item.source, item.bedrockClassId,
                                                      item.bedrockCategory);
            NotificationManager::showSuccess(this, tr("已添加到收藏夹"));
        }
    });
    btnLayout->addWidget(favBtn);

    // 从当前收藏夹移除按钮（收藏夹页面专属，使用红色 delete 图标）
    QPushButton *removeBtn = new QPushButton();
    removeBtn->setObjectName("modCardActionBtn");
    removeBtn->setProperty("cardRole", "actionBtn");
    removeBtn->setFixedSize(32, 32);
    removeBtn->setToolTip(tr("从收藏夹移除"));
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#e53935"), 18));
    removeBtn->setIconSize(QSize(18, 18));
    connect(removeBtn, &QPushButton::clicked, this, [this, item]() {
        if (FavoritesManager::instance()->removeFavorite(m_folderId, item.id))
        {
            NotificationManager::showSuccess(this, tr("已从收藏夹移除“%1”").arg(item.displayName()));
            setFolder(m_folderId);
        }
    });
    btnLayout->addWidget(removeBtn);

    mainLayout->addLayout(btnLayout);

    card->installEventFilter(this);
    m_cardWidgets.append(card);
    m_cardItems.append(item);

    if (!item.iconUrl.isEmpty())
    {
        loadCardIcon(iconLabel, item.iconUrl);
    }
}
/* 瀑布流卡片操作配置（小卡片展开态与瀑布流共用） */
QList<MasonryContentCard::ActionSpec> FavoritesPage::buildMasonryActions(const FavoriteItem &item)
{
    QList<MasonryContentCard::ActionSpec> actions;

    // 收藏分组菜单
    actions.append({ QStringLiteral(":/Images/Icons/star.svg"), tr("收藏"), QColor(),
                     [this, item]() {
                         if (item.id.isEmpty())
                         {
                             NotificationManager::showInfo(this, tr("当前资源信息不完整，无法收藏"));
                             return;
                         }
                         QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
                         QList<QString> containingIds = FavoritesManager::instance()->foldersContaining(item.id);
                         QMenu menu(this);
                         menu.setWindowTitle(tr("收藏到分组"));
                         for (const FavoriteFolder &f : folders)
                         {
                             bool contains = containingIds.contains(f.id);
                             QString label = f.name + (contains ? QStringLiteral("  ✓") : QString());
                             QAction *act = menu.addAction(label);
                             act->setCheckable(true);
                             act->setChecked(contains);
                             act->setData(f.id);
                         }
                         menu.addSeparator();
                         QAction *newFolderAct = menu.addAction(tr("新建收藏夹..."));
                         QAction *chosen = menu.exec(QCursor::pos());
                         if (!chosen)
                             return;
                         if (chosen == newFolderAct)
                         {
                             FavoriteFolderDialog dlg(FavoriteFolderDialog::CreateMode, this);
                             if (dlg.exec() == QDialog::Accepted)
                             {
                                 QString newId = FavoritesManager::instance()->createFolder(dlg.folderName());
                                 if (!newId.isEmpty())
                                 {
                                     ModInfo info = item.toModInfo();
                                     FavoritesManager::instance()->addFavorite(newId, info,
                                                                               static_cast<ContentType>(item.contentType),
                                                                               item.source, item.bedrockClassId,
                                                                               item.bedrockCategory);
                                     NotificationManager::showSuccess(this, tr("已添加到新收藏夹“%1”").arg(dlg.folderName()));
                                 }
                             }
                             return;
                         }
                         QString folderId = chosen->data().toString();
                         if (folderId.isEmpty())
                             return;
                         if (containingIds.contains(folderId))
                         {
                             FavoritesManager::instance()->removeFavorite(folderId, item.id);
                             NotificationManager::showSuccess(this, tr("已从收藏夹移除"));
                         }
                         else
                         {
                             ModInfo info = item.toModInfo();
                             FavoritesManager::instance()->addFavorite(folderId, info,
                                                                       static_cast<ContentType>(item.contentType),
                                                                       item.source, item.bedrockClassId,
                                                                       item.bedrockCategory);
                             NotificationManager::showSuccess(this, tr("已添加到收藏夹"));
                         }
                     } });

    // 从收藏夹移除（红色）
    actions.append({ QStringLiteral(":/Images/Icons/delete.svg"), tr("从收藏夹移除"), QColor("#e53935"),
                     [this, item]() {
                         if (FavoritesManager::instance()->removeFavorite(m_folderId, item.id))
                         {
                             NotificationManager::showSuccess(this, tr("已从收藏夹移除“%1”").arg(item.displayName()));
                             setFolder(m_folderId);
                         }
                     } });

    return actions;
}

/* 瀑布流卡片（复用 MasonryContentCard 组件） */
void FavoritesPage::addMasonryCard(const FavoriteItem &item)
{
    ModInfo info = item.toModInfo();
    QWidget *card = MasonryContentCard::build(info, m_cardContainer, buildMasonryActions(item));
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, &FavoritesPage::onCardContextMenu);

    card->installEventFilter(this);
    m_cardWidgets.append(card);

    if (!item.iconUrl.isEmpty())
    {
        QLabel *nameLabel = card->findChild<QLabel *>("modCardName");
        Q_UNUSED(nameLabel);
        // 瀑布流 logo 为字母块，不加载网络图标；如后续需要可在此替换 logo pixmap
    }
}

/* 视图切换：重建卡片（保留当前收藏数据） */
void FavoritesPage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted("favorites", mode);
    rebuildCards();
}

void FavoritesPage::rebuildCards()
{
    const QList<FavoriteItem> items = m_cardItems;
    clearCards();

    if (m_cardGridLayout) { delete m_cardGridLayout; m_cardGridLayout = nullptr; }
    if (m_cardFlowLayout) { delete m_cardFlowLayout; m_cardFlowLayout = nullptr; }
    if (m_viewMode == ContentViewSwitch::List) {
        m_cardGridLayout = new QGridLayout(m_cardContainer);
        m_cardGridLayout->setContentsMargins(24, 16, 24, 24);
        m_cardGridLayout->setSpacing(10);
        m_cardGridLayout->setColumnStretch(0, 1);
    } else {
        m_cardFlowLayout = new MasonryLayout(m_cardContainer, 0, 12, 12);
        m_cardFlowLayout->setContentsMargins(24, 16, 24, 24);
    }

    for (const FavoriteItem &item : items)
        addCard(item);
    placeCards();
}



void FavoritesPage::loadCardIcon(QLabel *iconLabel, const QString &iconUrl)
{
    QString cacheKey = QString("fav_icon:%1").arg(iconUrl);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached))
    {
        iconLabel->setPixmap(cached);
        return;
    }

    // 使用 QPointer 守护 iconLabel 生命周期：卡片可能在网络回复到达前被 clearCards() 删除
    QPointer<QLabel> labelGuard(iconLabel);
    QPointer<FavoritesPage> pageGuard(this);

    QNetworkAccessManager *nam = sharedFavIconNAM();
    QUrl url(McimHelper::rewriteImageUrl(iconUrl));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, labelGuard, pageGuard, cacheKey]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            QPixmap pixmap;
            pixmap.loadFromData(reply->readAll());
            if (!pixmap.isNull() && labelGuard && pageGuard)
            {
                QPixmap scaled = pixmap.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPixmapCache::insert(cacheKey, scaled);
                labelGuard->setPixmap(scaled);
            }
        }
        reply->deleteLater();
    });
}

bool FavoritesPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton)
        {
            return QWidget::eventFilter(watched, event);
        }

        QWidget *card = qobject_cast<QWidget *>(watched);
        if (card && m_cardWidgets.contains(card))
        {
            int idx = m_cardWidgets.indexOf(card);
            if (idx >= 0 && idx < m_cardItems.size())
            {
                emit favoriteClicked(m_cardItems[idx]);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FavoritesPage::onCardContextMenu(const QPoint &pos)
{
    QWidget *card = qobject_cast<QWidget *>(sender());
    if (!card || !m_cardWidgets.contains(card))
    {
        return;
    }

    int idx = m_cardWidgets.indexOf(card);
    if (idx < 0 || idx >= m_cardItems.size())
    {
        return;
    }

    const FavoriteItem &item = m_cardItems[idx];

    QMenu menu(this);

    QAction *detailAction = menu.addAction(tr("查看详情"));
    menu.addSeparator();
    QAction *copyNameAction = menu.addAction(tr("复制名称"));
    QAction *copyLinkAction = nullptr;
    if (!item.pageUrl.isEmpty())
    {
        copyLinkAction = menu.addAction(tr("复制链接"));
    }
    menu.addSeparator();
    QAction *openPageAction = nullptr;
    if (!item.pageUrl.isEmpty())
    {
        openPageAction = menu.addAction(tr("在浏览器中打开"));
    }
    QAction *removeAction = menu.addAction(tr("从收藏夹移除"));

    QAction *chosen = menu.exec(card->mapToGlobal(pos));
    if (!chosen)
    {
        return;
    }

    if (chosen == detailAction)
    {
        emit favoriteClicked(item);
    }
    else if (chosen == copyNameAction)
    {
        QApplication::clipboard()->setText(item.displayName());
    }
    else if (chosen == copyLinkAction)
    {
        QApplication::clipboard()->setText(item.pageUrl);
    }
    else if (chosen == openPageAction)
    {
        QDesktopServices::openUrl(QUrl(item.pageUrl));
    }
    else if (chosen == removeAction)
    {
        if (FavoritesManager::instance()->removeFavorite(m_folderId, item.id))
        {
            NotificationManager::showSuccess(this, tr("已从收藏夹移除“%1”").arg(item.displayName()));
            setFolder(m_folderId);
        }
    }
}

void FavoritesPage::placeCards()
{
    if (m_viewMode == ContentViewSwitch::List && m_cardGridLayout)
    {
        while (QLayoutItem *item = m_cardGridLayout->takeAt(0))
            delete item;

        int row = 0;
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            // AlignTop 确保卡片始终从顶部开始排列，不会因内容少而居中
            m_cardGridLayout->addWidget(m_cardWidgets[i], row, 0, Qt::AlignTop);
            row++;
        }

        // 在卡片下方添加一个垂直 expanding spacer：
        // 1) 卡片少时把卡片"顶"到容器顶部，避免 GridLayout 默认垂直居中
        // 2) 卡片多时 spacer 高度为 0，不影响滚动
        QSpacerItem *bottomSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        m_cardGridLayout->addItem(bottomSpacer, row, 0);
        return;
    }

    if (m_cardFlowLayout)
    {
        while (QLayoutItem *item = m_cardFlowLayout->takeAt(0))
            delete item;

        for (QWidget *w : m_cardWidgets)
            m_cardFlowLayout->addWidget(w);

        if (m_cardWidgets.isEmpty())
        {
            QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            m_cardFlowLayout->addItem(spacer);
        }
    }
}

int FavoritesPage::columnCount() const
{
    return 1;
}

void FavoritesPage::showEmptyHint(bool show)
{
    if (show)
    {
        m_emptyLabel->setParent(m_cardContainer);
        m_emptyLabel->setGeometry(0, 60, m_cardContainer->width(), 200);
        m_emptyLabel->show();
        m_emptyLabel->raise();
    }
    else
    {
        m_emptyLabel->hide();
    }
}

void FavoritesPage::onRefreshClicked()
{
    setFolder(m_folderId);
}
