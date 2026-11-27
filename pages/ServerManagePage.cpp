#include "ServerManagePage.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/LocalCategoryManager.h"
#include "utils/QuickLaunchManager.h"
#include "utils/ServersDatIO.h"
#include "utils/SettingsManager.h"
#include "components/LocalCategoryDialog.h"
#include "components/FavoriteFolderDialog.h"
#include "components/CardSelectionOverlay.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include "components/AppMessageBox.h"
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSpacerItem>
#include <QTimer>
#include <QUrl>

#include <algorithm>

ServerManagePage::ServerManagePage(QWidget *parent)
    : QWidget(parent)
    , m_searchEdit(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_emptyLabel(nullptr)
    , m_bottomBar(nullptr)
    , m_addServerBtn(nullptr)
    , m_deleteServerBtn(nullptr)
    , m_selectAllBtn(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryCombo(nullptr)
    , m_categoryManageBtn(nullptr)
    , m_sortCombo(nullptr)
{
    m_categoryManager = new LocalCategoryManager(this);
    m_categoryManager->setResourceType(QStringLiteral("servers"));

    m_pinger = new McServerPinger(this);
    connect(m_pinger, &McServerPinger::pingFinished, this,
            [this](const QString &key, const McServerPinger::Result &result) {
                m_pingResults.insert(key, result);
                m_pingAtMs.insert(key, QDateTime::currentMSecsSinceEpoch());
                for (QWidget *card : m_cardWidgets)
                {
                    if (card && card->property("serverKey").toString() == key)
                        applyPingResultToCard(card);
                }
            });

    initUI();
}

ServerManagePage::~ServerManagePage()
{
    // 还挂着的探测会话不继承本页生命周期，显式收掉
    if (m_pinger)
        m_pinger->cancelAll();
}

void ServerManagePage::setInstancePath(const QString &path)
{
    if (m_instancePath != path)
    {
        m_instancePath = path;
        m_lastDiskStamp = -1;   // 换了实例：下次显示时按新实例重新扫描
        m_categoryManager->setInstancePath(path);
        reloadCategoryCombo();
        if (!path.isEmpty())
        {
            loadServers();
            placeCards();
            markDiskSynced();
        }
    }
}

void ServerManagePage::applyPageFilter(const QString &query)
{
    // 同步到页内搜索框：setText 触发 textChanged → onSearchTextChanged → 既有过滤管线
    if (m_searchEdit)
        m_searchEdit->setText(query);
}

void ServerManagePage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // 顶部筛选卡：行1=搜索+分类，行2=排序+统计（与其他本地资源页两行统一；
    // 服务器为 JSON 条目，无大小/时间/禁用态数据，故不放假的状态/时间/大小筛选）
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    // ---- 行1：搜索 + 分类 ----
    QHBoxLayout *topBarLayout = new QHBoxLayout();
    topBarLayout->setSpacing(8);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索服务器名称或地址..."));
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);
    topBarLayout->addWidget(m_searchEdit, 1);

    QLabel *categoryLabel = new QLabel(tr("分类:"), this);
    categoryLabel->setObjectName("filterLabel");
    topBarLayout->addWidget(categoryLabel);

    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->setObjectName("categoryCombo");
    m_categoryCombo->setFixedHeight(32);
    m_categoryCombo->setMinimumWidth(140);
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categoryCombo->setMinimumContentsLength(5);
    reloadCategoryCombo();
    topBarLayout->addWidget(m_categoryCombo);

    m_categoryManageBtn = new QPushButton(tr("管理分类"), this);
    m_categoryManageBtn->setObjectName("categoryManageBtn");
    m_categoryManageBtn->setFixedHeight(32);
    m_categoryManageBtn->setCursor(Qt::PointingHandCursor);
    topBarLayout->addWidget(m_categoryManageBtn);

    cardLayout->addLayout(topBarLayout);

    // ---- 行2：排序 + 统计 ----
    QHBoxLayout *filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);

    QLabel *sortLabel = new QLabel(tr("排序:"), this);
    sortLabel->setObjectName("filterLabel");
    filterRow->addWidget(sortLabel);

    m_sortCombo = new QComboBox(this);
    m_sortCombo->setObjectName("searchSortCombo");
    m_sortCombo->setFixedHeight(32);
    m_sortCombo->setMinimumWidth(104);
    m_sortCombo->addItem(tr("名称 A→Z"), 0);
    m_sortCombo->addItem(tr("名称 Z→A"), 1);
    m_sortCombo->addItem(tr("地址 A→Z"), 2);
    m_sortCombo->addItem(tr("地址 Z→A"), 3);
    filterRow->addWidget(m_sortCombo);

    filterRow->addStretch();

    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("statusLabel");
    filterRow->addWidget(m_statsLabel);
    cardLayout->addLayout(filterRow);

    mainLayout->addWidget(filterCard);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
    m_cardGridLayout = new QGridLayout(m_cardContainer);
    m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
    m_cardGridLayout->setSpacing(8);
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // 空提示放滚动区外（原实现放在卡片网格内会被 clearCards 误删）
    m_emptyLabel = new QLabel(tr("暂无服务器，点击底部「添加服务器」按钮添加"));
    m_emptyLabel->setObjectName("exampleContentLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    mainLayout->addWidget(m_emptyLabel, 1);

    m_bottomBar = new QWidget();
    m_bottomBar->setObjectName("modBottomBar");
    m_bottomBar->setFixedHeight(48);
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(14, 6, 14, 6);
    bottomLayout->setSpacing(10);

    QColor themeColor = ThemeManager::instance()->currentThemeColor();

    m_addServerBtn = new QPushButton(tr("添加服务器"));
    m_addServerBtn->setObjectName("bottomActionBtn");
    m_addServerBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/install.svg", themeColor, 18));
    m_addServerBtn->setIconSize(QSize(18, 18));

    m_deleteServerBtn = new QPushButton(tr("删除服务器"));
    m_deleteServerBtn->setObjectName("bottomActionBtn");
    m_deleteServerBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    m_deleteServerBtn->setIconSize(QSize(18, 18));

    m_selectAllBtn = new QPushButton(tr("全选"));
    m_selectAllBtn->setObjectName("bottomActionBtn");
    m_selectAllBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/check.svg", themeColor, 18));
    m_selectAllBtn->setIconSize(QSize(18, 18));
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);

    bottomLayout->addWidget(m_addServerBtn);
    bottomLayout->addWidget(m_selectAllBtn);
    bottomLayout->addWidget(m_deleteServerBtn);
    bottomLayout->addStretch();
    mainLayout->addWidget(m_bottomBar);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ServerManagePage::onSearchTextChanged);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ServerManagePage::onCategoryFilterChanged);
    connect(m_categoryManageBtn, &QPushButton::clicked,
            this, &ServerManagePage::onCategoryManageClicked);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ServerManagePage::onSortChanged);
    connect(m_categoryManager, &LocalCategoryManager::categoriesChanged,
            this, &ServerManagePage::onCategoriesChanged);
    connect(m_categoryManager, &LocalCategoryManager::assignmentsChanged,
            this, &ServerManagePage::onAssignmentsChanged);
    connect(m_addServerBtn, &QPushButton::clicked,
            this, &ServerManagePage::onAddServerClicked);
    connect(m_deleteServerBtn, &QPushButton::clicked,
            this, &ServerManagePage::onDeleteServerClicked);
    connect(m_selectAllBtn, &QPushButton::clicked,
            this, &ServerManagePage::onSelectAllClicked);
}

QString ServerManagePage::serversFilePath() const
{
    if (m_instancePath.isEmpty())
        return QString();
    return m_instancePath + "/servers.json";
}

QStringList ServerManagePage::serversDatCandidates() const
{
    // 版本目录 + 共享 .minecraft 目录都要扫（开关版本隔离会改变游戏写在哪）
    return ServersDatIO::candidatePaths(m_instancePath);
}

QString ServerManagePage::serversDatPath() const
{
    const QStringList candidates = serversDatCandidates();
    if (candidates.isEmpty())
        return QString();

    // 写回游戏实际使用的那个文件：多个位置都有时取最近改动的
    const QString existing = ServersDatIO::existingPath(candidates);
    if (!existing.isEmpty())
        return existing;

    // 都还不存在：按版本隔离开关推断游戏将来会写在哪里
    return SettingsManager::instance()->isVersionIsolationEnabled()
               ? candidates.first()
               : candidates.last();
}

qint64 ServerManagePage::diskStamp() const
{
    // 合并「是否存在 + 大小 + 修改时间」为一个指纹；任一处变化都会让显示重新扫描
    qint64 stamp = 0;
    QStringList files;
    const QString jsonPath = serversFilePath();
    if (!jsonPath.isEmpty())
        files << jsonPath;
    files += serversDatCandidates();

    for (const QString &path : files)
    {
        if (path.isEmpty())
            continue;
        const QFileInfo info(path);
        if (!info.exists())
            continue;
        stamp ^= (info.size() + 1) * 1000003LL
                 + info.lastModified().toMSecsSinceEpoch();
    }
    return stamp;
}

void ServerManagePage::markDiskSynced()
{
    m_lastDiskStamp = diskStamp();
}

void ServerManagePage::loadServers()
{
    m_servers.clear();
    QHash<QString, int> indexByKey;

    // 1) 启动器自有列表（含描述等扩展字段）
    const QString jsonPath = serversFilePath();
    if (!jsonPath.isEmpty() && QFile::exists(jsonPath))
    {
        QFile file(jsonPath);
        if (file.open(QIODevice::ReadOnly))
        {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();

            QJsonArray arr = doc.array();
            for (const QJsonValue &val : arr)
            {
                QJsonObject obj = val.toObject();
                ServerEntry entry;
                entry.name = obj["name"].toString();
                entry.address = obj["address"].toString();
                entry.port = static_cast<quint16>(obj["port"].toInt(25565));
                entry.description = obj["description"].toString();
                if (entry.address.trimmed().isEmpty())
                    continue;
                indexByKey.insert(ServersDatIO::addressKey(entry.address, entry.port),
                                  m_servers.size());
                m_servers.append(entry);
            }
        }
    }

    // 2) 游戏内多人列表 servers.dat（合并进来，避免「游戏里加的服务器页上看不见」）
    //    游戏目录随版本隔离开关变化（版本目录 / 共享 .minecraft），两处都扫
    for (const QString &datPath : serversDatCandidates())
    {
        const QList<ServersDatIO::Entry> gameServers = ServersDatIO::read(datPath);
        for (const ServersDatIO::Entry &game : gameServers)
        {
            const QString key = ServersDatIO::addressKey(game.host, game.port);
            if (indexByKey.contains(key))
            {
                // 已有同地址条目：以游戏内名称为准（游戏是显示名的权威来源）
                ServerEntry &entry = m_servers[indexByKey.value(key)];
                if (!game.name.trimmed().isEmpty())
                    entry.name = game.name;
                continue;
            }
            ServerEntry entry;
            entry.name = game.name;
            entry.address = game.host;
            entry.port = game.port;
            indexByKey.insert(key, m_servers.size());
            m_servers.append(entry);
        }
    }
}

void ServerManagePage::reloadServers()
{
    if (m_instancePath.isEmpty())
        return;
    loadServers();
    placeCards();
    markDiskSynced();
}

void ServerManagePage::refresh()
{
    if (m_instancePath.isEmpty())
        return;
    loadServers();
    // 手动刷新：丢掉探测缓存，卡片就位后会重新探测
    m_pingResults.clear();
    m_pingAtMs.clear();
    placeCards();
    markDiskSynced();
}

void ServerManagePage::saveServers()
{
    // 仅写启动器 JSON（描述等扩展字段）；游戏列表由 servers.dat 同步
    const QString path = serversFilePath();
    if (path.isEmpty())
        return;

    QJsonArray arr;
    for (const ServerEntry &entry : m_servers)
    {
        QJsonObject obj;
        obj["name"] = entry.name;
        obj["address"] = entry.address;
        obj["port"] = entry.port;
        obj["description"] = entry.description;
        arr.append(obj);
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(arr).toJson());
        file.close();
    }
    markDiskSynced();
}

void ServerManagePage::upsertGameServer(const ServerEntry &entry)
{
    const QString datPath = serversDatPath();
    if (datPath.isEmpty() || entry.address.trimmed().isEmpty())
        return;
    ServersDatIO::Entry game;
    game.name = entry.name;
    game.host = entry.address;
    game.port = entry.port;
    ServersDatIO::upsert(datPath, game);
    markDiskSynced();
}

void ServerManagePage::removeGameServer(const ServerEntry &entry)
{
    const QString datPath = serversDatPath();
    if (datPath.isEmpty() || entry.address.trimmed().isEmpty())
        return;
    ServersDatIO::remove(datPath, entry.address, entry.port);
    markDiskSynced();
}

void ServerManagePage::clearCards()
{
    QLayoutItem *item;
    while ((item = m_cardGridLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
        {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }

    m_cardWidgets.clear();
    m_selectedCardIndices.clear();
    m_allSelected = false;
    m_dragSelectActive = false;
    m_dragMoved = false;
    m_dragAnchorIdx = -1;
    m_dragLastHoverIdx = -1;
    // 遮罩是卡片子控件，随卡片销毁
    m_selectionOverlays.clear();
}

void ServerManagePage::placeCards()
{
    clearCards();

    if (m_servers.isEmpty())
    {
        m_emptyLabel->setText(tr("暂无服务器，点击底部「添加服务器」按钮添加"));
        showEmptyHint(true);
        updateSelectionBar();
        return;
    }
    showEmptyHint(false);

    // 按当前排序模式重排副本（不动持久化顺序）
    QList<ServerEntry> sorted = m_servers;
    sortServers(sorted);

    QColor themeColor = ThemeManager::instance()->currentThemeColor();
    int col = 0;
    int row = 0;
    int visibleCount = 0;

    for (int i = 0; i < sorted.size(); ++i)
    {
        const ServerEntry &entry = sorted[i];
        if (!m_currentSearch.isEmpty())
        {
            bool match = entry.name.contains(m_currentSearch, Qt::CaseInsensitive)
                      || entry.address.contains(m_currentSearch, Qt::CaseInsensitive)
                      || entry.description.contains(m_currentSearch, Qt::CaseInsensitive);
            if (!match)
                continue;
        }

        // 分类筛选（键=服务器地址）
        if (!m_currentCategoryId.isEmpty())
        {
            const QString cat = m_categoryManager->categoryOf(entry.address);
            if (m_currentCategoryId == QStringLiteral("__uncategorized__"))
            {
                if (!cat.isEmpty())
                    continue;
            }
            else if (cat != m_currentCategoryId)
            {
                continue;
            }
        }

        QWidget *card = new QWidget(m_cardContainer);
        card->setObjectName("resourceCard");
        card->setFixedHeight(88);
        card->setCursor(Qt::PointingHandCursor);
        card->setProperty("serverKey",
            ServersDatIO::addressKey(entry.address, entry.port));
        card->setProperty("serverCardDescription", entry.description);
        card->installEventFilter(this);
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        const QString catKey = entry.address;
        const QString serverKey = card->property("serverKey").toString();
        connect(card, &QWidget::customContextMenuRequested, this,
            [this, catKey, serverKey, entry](const QPoint &pos) {
                QMenu menu(this);
                appendCategoryMenu(&menu, catKey);
                menu.addSeparator();
                QAction *refreshAction = menu.addAction(tr("刷新服务器信息"));
                connect(refreshAction, &QAction::triggered, this,
                    [this, serverKey, entry]() {
                        m_pingAtMs.remove(serverKey);
                        m_pingResults.remove(serverKey);
                        for (QWidget *c : m_cardWidgets)
                        {
                            if (c && c->property("serverKey").toString() == serverKey)
                                applyPingResultToCard(c);
                        }
                        pingOne(serverKey, entry.address, entry.port);
                    });
                // 桌面 .blockbox 快捷启动：双击直接启动游戏并连这台服
                QAction *desktopAction = menu.addAction(tr("发送快捷启动到桌面"));
                connect(desktopAction, &QAction::triggered, this, [this, entry]() {
                    QString message;
                    if (QuickLaunchManager::createServerShortcut(
                            m_instancePath, entry.name, entry.address, entry.port, &message))
                    {
                        AppMessageBox::information(
                            this, tr("已发送到桌面"),
                            tr("桌面已生成「%1」快捷启动文件，双击它就会启动游戏并自动连接这台服务器。")
                                .arg(entry.name));
                    }
                    else
                    {
                        AppMessageBox::warning(this, tr("发送失败"), message);
                    }
                });
                menu.exec(pos);
            });

        QHBoxLayout *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(10);

        QLabel *iconLabel = new QLabel(card);
        iconLabel->setObjectName("serverCardIcon");
        iconLabel->setFixedSize(48, 48);
        // 默认图标画在 @TEXT_ON_PRIMARY@ 的主色渐变底上，用白色描边才看得清
        QIcon srvIcon = IconHelper::loadColoredIcon(
            ":/Images/Icons/server.svg", QColor(Qt::white), 48);
        // 记住默认图标：离线或没有 favicon 时回退到它
        iconLabel->setProperty("defaultIcon", srvIcon.pixmap(48, 48));
        iconLabel->setPixmap(srvIcon.pixmap(48, 48));
        iconLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(iconLabel);

        QVBoxLayout *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(2);

        // 名称行：名称 + 分类芯片（右）
        const QString catName = m_categoryManager->categoryName(entry.address);
        QHBoxLayout *nameRow = new QHBoxLayout();
        nameRow->setContentsMargins(0, 0, 0, 0);
        nameRow->setSpacing(6);
        QLabel *nameLabel = new QLabel(entry.name, card);
        nameLabel->setObjectName("modNameLabel");
        nameLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
        // 名称过长时按卡片宽度省略，避免把右侧的状态角标挤出可视区
        nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        nameLabel->setMinimumWidth(0);
        nameLabel->setProperty("fullText", entry.name);
        nameRow->addWidget(nameLabel, 1);
        if (!catName.isEmpty())
        {
            QLabel *catChip = new QLabel(catName, card);
            catChip->setObjectName("modChip");
            catChip->setProperty("cardRole", "chip");
            nameRow->addWidget(catChip);
        }
        QLabel *statusLabel = new QLabel(tr("● 检测中…"), card);
        statusLabel->setObjectName("serverCardStatus");
        statusLabel->setStyleSheet("font-size: 11px; color: #888;");
        nameRow->addWidget(statusLabel, 0, Qt::AlignVCenter);
        infoLayout->addLayout(nameRow);

        // 地址行：地址（左）+ 在线人数 / 版本 / 延迟（右）
        QHBoxLayout *addrRow = new QHBoxLayout();
        addrRow->setContentsMargins(0, 0, 0, 0);
        addrRow->setSpacing(8);
        QString addrText = entry.address;
        if (entry.port != 25565)
            addrText += QString(":%1").arg(entry.port);
        QLabel *addrLabel = new QLabel(addrText, card);
        addrLabel->setObjectName("modDescLabel");
        addrLabel->setStyleSheet("font-size: 12px; color: #888;");
        addrRow->addWidget(addrLabel, 0);
        addrRow->addStretch();
        QLabel *infoLabel = new QLabel(QString(), card);
        infoLabel->setObjectName("serverCardInfo");
        infoLabel->setStyleSheet("font-size: 11px; color: #888;");
        infoLabel->setTextFormat(Qt::PlainText);
        addrRow->addWidget(infoLabel, 0, Qt::AlignVCenter);
        infoLayout->addLayout(addrRow);

        // MOTD 行：探测到就在线文案，否则退回用户自己写的描述；都没有则整行隐藏
        QLabel *motdLabel = new QLabel(card);
        motdLabel->setObjectName("serverCardMotd");
        motdLabel->setStyleSheet("font-size: 11px; color: #aaa;");
        motdLabel->setTextFormat(Qt::PlainText);
        motdLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        motdLabel->setMinimumWidth(0);
        motdLabel->setProperty("fullText", entry.description);
        infoLayout->addWidget(motdLabel);
        cardLayout->addLayout(infoLayout, 1);

        QPushButton *connectBtn = new QPushButton(card);
        connectBtn->setFixedSize(32, 32);
        connectBtn->setObjectName("iconOnlyButton");
        connectBtn->setToolTip(tr("连接服务器"));
        connectBtn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/play.svg", themeColor, 18));
        connectBtn->setIconSize(QSize(18, 18));
        connect(connectBtn, &QPushButton::clicked, this, [this, entry]() {
            QString addr = entry.address;
            if (entry.port != 25565)
                addr += QString(":%1").arg(entry.port);
            emit connectToServerRequested(addr);
        });

        QPushButton *quickLaunchBtn = new QPushButton(card);
        quickLaunchBtn->setFixedSize(32, 32);
        quickLaunchBtn->setObjectName("iconOnlyButton");
        quickLaunchBtn->setToolTip(tr("快捷启动并连接"));
        quickLaunchBtn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/quick_launch.svg", themeColor, 18));
        quickLaunchBtn->setIconSize(QSize(18, 18));
        connect(quickLaunchBtn, &QPushButton::clicked, this, [this, entry]() {
            emit quickLaunchServerRequested(entry.address, entry.port);
        });

        QPushButton *copyBtn = new QPushButton(card);
        copyBtn->setFixedSize(32, 32);
        copyBtn->setObjectName("iconOnlyButton");
        copyBtn->setToolTip(tr("复制地址"));
        copyBtn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/copy.svg", themeColor, 18));
        copyBtn->setIconSize(QSize(18, 18));
        connect(copyBtn, &QPushButton::clicked, this, [entry]() {
            QString addr = entry.address;
            if (entry.port != 25565)
                addr += QString(":%1").arg(entry.port);
            QApplication::clipboard()->setText(addr);
        });

        QPushButton *editBtn = new QPushButton(card);
        editBtn->setFixedSize(32, 32);
        editBtn->setObjectName("iconOnlyButton");
        editBtn->setToolTip(tr("编辑"));
        editBtn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/modify.svg", themeColor, 18));
        editBtn->setIconSize(QSize(18, 18));
        connect(editBtn, &QPushButton::clicked, this, [this, entry]() {
            onEditServerClicked(entry.name);
        });

        cardLayout->addWidget(connectBtn);
        cardLayout->addWidget(quickLaunchBtn);
        cardLayout->addWidget(copyBtn);
        cardLayout->addWidget(editBtn);

        m_cardGridLayout->addWidget(card, row, col, 1, 1);
        m_cardWidgets.append(card);
        createSelectionOverlay(card);
        applyPingResultToCard(card);   // 先用缓存/占位状态填充
        col++;
        if (col >= 1)
        {
            col = 0;
            row++;
        }
        visibleCount++;
    }

    QSpacerItem *spacer = new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding);
    m_cardGridLayout->addItem(spacer, row + 1, 0);

    // 统计：筛选/搜索激活时显示「显示 N (共 M)」；筛空给提示（不引导下载）
    if (visibleCount == 0)
    {
        m_emptyLabel->setText(tr("没有符合筛选条件的服务器"));
        showEmptyHint(true);
    }
    else
    {
        showEmptyHint(false);
    }
    updateSelectionBar();

    // 卡片就位后异步探测在线状态（离开页面一段时间再回来时顺带刷新）
    pingServers(false);

    // 等布局铺开后再按实际宽度省略 MOTD
    QTimer::singleShot(0, this, [this]() {
        for (QWidget *card : m_cardWidgets)
            elideCardTexts(card);
    });
}

void ServerManagePage::showEmptyHint(bool show)
{
    m_emptyLabel->setVisible(show);
    // 空提示占据列表区域时隐藏滚动区（原实现提示在网格内会被 clearCards 删除）
    m_scrollArea->setVisible(!show);
}

void ServerManagePage::onSearchTextChanged(const QString &text)
{
    m_currentSearch = text;
    placeCards();
}

void ServerManagePage::onAddServerClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("添加服务器"));
    dialog.setFixedSize(400, 250);

    QFormLayout *form = new QFormLayout(&dialog);
    QLineEdit *nameEdit = new QLineEdit(&dialog);
    nameEdit->setObjectName("dialogLineEdit");
    nameEdit->setPlaceholderText(tr("服务器名称"));
    QLineEdit *addrEdit = new QLineEdit(&dialog);
    addrEdit->setObjectName("dialogLineEdit");
    addrEdit->setPlaceholderText(tr("IP地址"));
    QLineEdit *portEdit = new QLineEdit(&dialog);
    portEdit->setObjectName("dialogLineEdit");
    portEdit->setPlaceholderText(tr("端口 (默认 25565)"));
    portEdit->setText("25565");
    QLineEdit *descEdit = new QLineEdit(&dialog);
    descEdit->setObjectName("dialogLineEdit");
    descEdit->setPlaceholderText(tr("描述（可选）"));

    form->addRow(tr("名称:"), nameEdit);
    form->addRow(tr("地址:"), addrEdit);
    form->addRow(tr("端口:"), portEdit);
    form->addRow(tr("描述:"), descEdit);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *cancelBtn = new QPushButton(tr("取消"), &dialog);
    cancelBtn->setObjectName("dialogButton");
    QPushButton *okBtn = new QPushButton(tr("确定"), &dialog);
    okBtn->setObjectName("dialogButton");
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    form->addRow(btnLayout);

    connect(okBtn, &QPushButton::clicked, &dialog, [&dialog, nameEdit, addrEdit, portEdit]() {
        if (nameEdit->text().trimmed().isEmpty() || addrEdit->text().trimmed().isEmpty())
        {
            AppMessageBox::warning(&dialog, tr("提示"), tr("名称和地址不能为空"));
            return;
        }
        dialog.accept();
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
    {
        ServerEntry entry;
        entry.name = nameEdit->text().trimmed();
        entry.address = addrEdit->text().trimmed();
        entry.port = static_cast<quint16>(portEdit->text().toUShort());
        entry.description = descEdit->text().trimmed();
        m_servers.append(entry);
        saveServers();
        upsertGameServer(entry);
        placeCards();
    }
}

void ServerManagePage::onEditServerClicked(const QString &name)
{
    int idx = -1;
    for (int i = 0; i < m_servers.size(); ++i)
    {
        if (m_servers[i].name == name)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0) return;

    const ServerEntry &old = m_servers[idx];

    QDialog dialog(this);
    dialog.setWindowTitle(tr("编辑服务器"));
    dialog.setFixedSize(400, 250);

    QFormLayout *form = new QFormLayout(&dialog);
    QLineEdit *nameEdit = new QLineEdit(old.name, &dialog);
    nameEdit->setObjectName("dialogLineEdit");
    QLineEdit *addrEdit = new QLineEdit(old.address, &dialog);
    addrEdit->setObjectName("dialogLineEdit");
    QLineEdit *portEdit = new QLineEdit(QString::number(old.port), &dialog);
    portEdit->setObjectName("dialogLineEdit");
    QLineEdit *descEdit = new QLineEdit(old.description, &dialog);
    descEdit->setObjectName("dialogLineEdit");

    form->addRow(tr("名称:"), nameEdit);
    form->addRow(tr("地址:"), addrEdit);
    form->addRow(tr("端口:"), portEdit);
    form->addRow(tr("描述:"), descEdit);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *cancelBtn = new QPushButton(tr("取消"), &dialog);
    cancelBtn->setObjectName("dialogButton");
    QPushButton *okBtn = new QPushButton(tr("确定"), &dialog);
    okBtn->setObjectName("dialogButton");
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    form->addRow(btnLayout);

    connect(okBtn, &QPushButton::clicked, &dialog, [&dialog, nameEdit, addrEdit]() {
        if (nameEdit->text().trimmed().isEmpty() || addrEdit->text().trimmed().isEmpty())
        {
            AppMessageBox::warning(&dialog, tr("提示"), tr("名称和地址不能为空"));
            return;
        }
        dialog.accept();
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
    {
        const QString oldKey = ServersDatIO::addressKey(old.address, old.port);
        m_servers[idx].name = nameEdit->text().trimmed();
        m_servers[idx].address = addrEdit->text().trimmed();
        m_servers[idx].port = static_cast<quint16>(portEdit->text().toUShort());
        m_servers[idx].description = descEdit->text().trimmed();
        saveServers();
        const QString newKey = ServersDatIO::addressKey(m_servers[idx].address, m_servers[idx].port);
        if (oldKey != newKey)
            removeGameServer(old);
        upsertGameServer(m_servers[idx]);
        placeCards();
    }
}

void ServerManagePage::onDeleteServerClicked()
{
    if (m_servers.isEmpty())
        return;

    // 已在列表上勾选了卡片：批量删除选中的服务器
    if (!m_selectedCardIndices.isEmpty())
    {
        QList<ServerEntry> toRemove;
        for (int idx : m_selectedCardIndices)
        {
            if (idx >= 0 && idx < m_cardWidgets.size())
            {
                const QString key =
                    m_cardWidgets[idx]->property("serverKey").toString();
                for (const ServerEntry &e : m_servers)
                {
                    if (ServersDatIO::addressKey(e.address, e.port) == key)
                    {
                        toRemove.append(e);
                        break;
                    }
                }
            }
        }
        if (toRemove.isEmpty())
            return;

        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除服务器"),
            tr("确定要删除选中的 %1 个服务器吗？此操作不可撤销。").arg(toRemove.size()),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);
        if (reply != AppMessageBox::Yes)
            return;

        for (const ServerEntry &e : toRemove)
        {
            removeGameServer(e);
            const QString key = ServersDatIO::addressKey(e.address, e.port);
            for (int i = m_servers.size() - 1; i >= 0; --i)
            {
                if (ServersDatIO::addressKey(m_servers[i].address,
                                             m_servers[i].port) == key)
                    m_servers.removeAt(i);
            }
        }
        saveServers();
        placeCards();
        return;
    }

    // 未勾选卡片：弹出勾选列表逐个选择删除（原交互保留）
    QDialog dialog(this);
    dialog.setWindowTitle(tr("删除服务器"));
    dialog.setFixedSize(350, 300);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *label = new QLabel(tr("选择要删除的服务器:"), &dialog);
    label->setObjectName("dialogLabel");
    layout->addWidget(label);

    QList<QCheckBox *> checks;
    for (const ServerEntry &entry : m_servers)
    {
        QCheckBox *cb = new QCheckBox(
            QString("%1 (%2:%3)").arg(entry.name, entry.address).arg(entry.port),
            &dialog);
        cb->setObjectName("dialogCheckBox");
        checks.append(cb);
        layout->addWidget(cb);
    }

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *cancelBtn = new QPushButton(tr("取消"), &dialog);
    cancelBtn->setObjectName("dialogButton");
    QPushButton *okBtn = new QPushButton(tr("删除"), &dialog);
    okBtn->setObjectName("dialogButton");
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    layout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, &dialog, [&dialog, &checks, this]() {
        bool any = false;
        for (QCheckBox *cb : checks)
        {
            if (cb->isChecked()) { any = true; break; }
        }
        if (!any)
        {
            AppMessageBox::warning(&dialog, tr("提示"), tr("请至少选择一个服务器"));
            return;
        }
        dialog.accept();
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
    {
        for (int i = m_servers.size() - 1; i >= 0; --i)
        {
            if (checks[i]->isChecked())
            {
                removeGameServer(m_servers[i]);
                m_servers.removeAt(i);
            }
        }
        saveServers();
        placeCards();
    }
}

// ─────────────── 分类 / 排序筛选 ───────────────

void ServerManagePage::reloadCategoryCombo()
{
    if (!m_categoryCombo)
        return;

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(tr("全部"), QString());
    // 服务器为 JSON 条目，无文件大小/修改时间数据，不加智能筛选项
    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        m_categoryCombo->addItem(c.name, c.id);
    }
    m_categoryCombo->addItem(tr("未分类"), QStringLiteral("__uncategorized__"));

    // 恢复之前选中的分类（已被删除则回退「全部」）
    int restore = 0;
    if (!m_currentCategoryId.isEmpty())
    {
        for (int i = 0; i < m_categoryCombo->count(); ++i)
        {
            if (m_categoryCombo->itemData(i).toString() == m_currentCategoryId)
            {
                restore = i;
                break;
            }
        }
    }
    m_categoryCombo->setCurrentIndex(restore);
    m_currentCategoryId = m_categoryCombo->itemData(restore).toString();
    m_categoryCombo->blockSignals(false);
}

void ServerManagePage::onCategoryFilterChanged(int index)
{
    if (!m_categoryCombo)
        return;
    m_currentCategoryId = m_categoryCombo->itemData(index).toString();
    placeCards();
}

void ServerManagePage::onCategoryManageClicked()
{
    LocalCategoryDialog dialog(m_categoryManager, this);
    dialog.exec();
}

void ServerManagePage::onCategoriesChanged()
{
    reloadCategoryCombo();
    placeCards();
}

void ServerManagePage::onAssignmentsChanged()
{
    placeCards();
}

void ServerManagePage::onSortChanged(int index)
{
    if (!m_sortCombo)
        return;
    m_sortMode = m_sortCombo->itemData(index).toInt();
    placeCards();
}

void ServerManagePage::sortServers(QList<ServerEntry> &list) const
{
    const int mode = m_sortMode;
    std::sort(list.begin(), list.end(), [mode](const ServerEntry &a, const ServerEntry &b) {
        switch (mode)
        {
        case 1:  // 名称 Z→A
            return QString::compare(a.name, b.name, Qt::CaseInsensitive) > 0;
        case 2:  // 地址 A→Z
            return QString::compare(a.address, b.address, Qt::CaseInsensitive) < 0;
        case 3:  // 地址 Z→A
            return QString::compare(a.address, b.address, Qt::CaseInsensitive) > 0;
        default: // 名称 A→Z
            return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
        }
    });
}

void ServerManagePage::appendCategoryMenu(QMenu *menu, const QString &key)
{
    // ── 设置分类子菜单（键 = 服务器地址）──
    QMenu *categoryMenu = menu->addMenu(tr("设置分类"));
    const QString currentCat = m_categoryManager->categoryOf(key);
    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        QAction *action = categoryMenu->addAction(c.name);
        action->setCheckable(true);
        action->setChecked(currentCat == c.id);
        connect(action, &QAction::triggered, this,
            [this, key, categoryId = c.id]() {
                m_categoryManager->assignCategory(key, categoryId);
            });
    }
    if (cats.isEmpty())
    {
        QAction *emptyAction = categoryMenu->addAction(tr("暂无分类"));
        emptyAction->setEnabled(false);
    }
    QAction *clearAction = categoryMenu->addAction(tr("清除分类"));
    clearAction->setEnabled(!currentCat.isEmpty());
    connect(clearAction, &QAction::triggered, this,
        [this, key]() {
            m_categoryManager->assignCategory(key, QString());
        });
    categoryMenu->addSeparator();
    QAction *newCatAction = categoryMenu->addAction(tr("新建分类…"));
    connect(newCatAction, &QAction::triggered, this,
        [this, key]() {
            FavoriteFolderDialog dialog(FavoriteFolderDialog::CreateMode, this);
            dialog.setWindowTitle(tr("新建分类"));
            dialog.setPresets(LocalCategoryManager::suggestedNames(m_categoryManager->resourceType()));
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QString name = dialog.folderName();
            if (name.isEmpty())
                return;
            const QString newId = m_categoryManager->addCategory(name);
            m_categoryManager->assignCategory(key, newId);
        });
}

// ─────────────── 卡片多选 ───────────────

void ServerManagePage::onSelectAllClicked()
{
    m_allSelected = !m_allSelected;

    if (m_allSelected)
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            m_selectedCardIndices.insert(i);
            animateCardHighlight(i, true);
        }
    }
    else
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
            animateCardHighlight(i, false);
        m_selectedCardIndices.clear();
    }

    updateSelectionBar();
}

void ServerManagePage::toggleCardSelection(int cardIdx)
{
    if (cardIdx < 0 || cardIdx >= m_cardWidgets.size())
        return;

    if (m_selectedCardIndices.contains(cardIdx))
    {
        m_selectedCardIndices.remove(cardIdx);
        animateCardHighlight(cardIdx, false);
    }
    else
    {
        m_selectedCardIndices.insert(cardIdx);
        animateCardHighlight(cardIdx, true);
    }

    updateSelectionBar();
}

void ServerManagePage::deselectAllCards()
{
    for (int idx : m_selectedCardIndices)
        animateCardHighlight(idx, false);
    m_selectedCardIndices.clear();
    updateSelectionBar();
}

int ServerManagePage::cardIndexAtGlobal(const QPoint &globalPos) const
{
    QWidget *w = QApplication::widgetAt(globalPos);
    while (w)
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            if (m_cardWidgets[i] == w)
                return i;
        }
        w = w->parentWidget();
    }
    return -1;
}

void ServerManagePage::selectCardDuringDrag(int cardIdx)
{
    if (cardIdx < 0 || cardIdx >= m_cardWidgets.size())
        return;
    if (m_selectedCardIndices.contains(cardIdx))
        return;

    m_selectedCardIndices.insert(cardIdx);
    animateCardHighlight(cardIdx, true);
    updateSelectionBar();
}

void ServerManagePage::deselectCardDuringDrag(int cardIdx)
{
    if (cardIdx < 0 || cardIdx >= m_cardWidgets.size())
        return;
    if (!m_selectedCardIndices.contains(cardIdx))
        return;

    m_selectedCardIndices.remove(cardIdx);
    animateCardHighlight(cardIdx, false);
    updateSelectionBar();
}

QWidget *ServerManagePage::createSelectionOverlay(QWidget *card)
{
    // 自绘遮罩：不再用 QGraphicsOpacityEffect（效果控件在滚动区内
    // 滚一次就不再重绘，选中高亮会整个消失），详见 CardSelectionOverlay
    CardSelectionOverlay *overlay = new CardSelectionOverlay(card);
    overlay->raise();

    m_selectionOverlays.append(overlay);
    return overlay;
}

void ServerManagePage::animateCardHighlight(int idx, bool selected)
{
    if (idx < 0 || idx >= m_selectionOverlays.size())
        return;

    m_selectionOverlays[idx]->setSelected(selected);
}

void ServerManagePage::updateSelectionBar()
{
    const int total = m_cardWidgets.size();
    const int selected = m_selectedCardIndices.size();
    const int all = m_servers.size();
    const bool anyFilterActive = !m_currentSearch.isEmpty()
        || !m_currentCategoryId.isEmpty();

    if (selected == 0)
    {
        if (anyFilterActive)
            m_statsLabel->setText(tr("显示 %1 个服务器 (共 %2 个)").arg(total).arg(all));
        else
            m_statsLabel->setText(tr("共 %1 个服务器").arg(all));
    }
    else
    {
        if (anyFilterActive)
            m_statsLabel->setText(tr("显示 %1 个服务器 (共 %2 个，已选 %3 个)")
                .arg(total).arg(all).arg(selected));
        else
            m_statsLabel->setText(tr("共 %1 个服务器（已选 %2 个）").arg(all).arg(selected));
    }

    // 全选按钮文案：所有可见卡片都被选中时显示「取消全选」
    m_allSelected = !m_cardWidgets.isEmpty() && selected == total;
    if (m_selectAllBtn)
        m_selectAllBtn->setText(m_allSelected ? tr("取消全选") : tr("全选"));

    // 删除按钮文案：有选中时提示可批量删除
    if (m_deleteServerBtn)
    {
        if (selected == 0)
            m_deleteServerBtn->setText(tr("删除服务器"));
        else
            m_deleteServerBtn->setText(tr("删除选中 (%1)").arg(selected));
    }
}

bool ServerManagePage::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *card = qobject_cast<QWidget *>(watched);
    if (!card || !card->property("serverKey").isValid())
        return QWidget::eventFilter(watched, event);

    const int idx = m_cardWidgets.indexOf(card);
    if (idx < 0)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            // 按下左键开始滑动多选：抓取鼠标，拖拽经过的卡片将被快速勾选
            m_dragSelectActive = true;
            m_dragMoved = false;
            m_dragPressPos = mouseEvent->globalPos();
            m_dragAnchorIdx = idx;
            m_dragLastHoverIdx = idx;
            card->grabMouse();
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_dragSelectActive && (mouseEvent->buttons() & Qt::LeftButton))
        {
            // 超过拖拽阈值才视为滑动多选，避免与单击冲突
            if (!m_dragMoved &&
                (mouseEvent->globalPos() - m_dragPressPos).manhattanLength()
                    >= QApplication::startDragDistance())
            {
                m_dragMoved = true;
                selectCardDuringDrag(m_dragAnchorIdx);
            }

            if (m_dragMoved)
            {
                int hoverIdx = cardIndexAtGlobal(mouseEvent->globalPos());
                if (hoverIdx >= 0 && hoverIdx != m_dragLastHoverIdx)
                {
                    if (hoverIdx > m_dragLastHoverIdx)
                    {
                        // 正向滑动：勾选滑过的可见卡片
                        for (int i = m_dragLastHoverIdx + 1; i <= hoverIdx; ++i)
                            selectCardDuringDrag(i);
                    }
                    else
                    {
                        // 反向滑动：取消勾选滑过的可见卡片
                        for (int i = m_dragLastHoverIdx - 1; i >= hoverIdx; --i)
                            deselectCardDuringDrag(i);
                    }
                    m_dragLastHoverIdx = hoverIdx;
                }
            }
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_dragSelectActive && mouseEvent->button() == Qt::LeftButton)
        {
            m_dragSelectActive = false;
            card->releaseMouse();
            if (!m_dragMoved)
            {
                // 未发生拖拽：视为单击，切换该卡片的选中状态
                toggleCardSelection(m_dragAnchorIdx);
            }
            m_dragMoved = false;
            m_dragAnchorIdx = -1;
            m_dragLastHoverIdx = -1;
            return true;
        }
    }

    if (event->type() == QEvent::Resize)
    {
        if (idx < m_selectionOverlays.size())
            m_selectionOverlays[idx]->setGeometry(card->rect());
        elideCardTexts(card);
    }

    return QWidget::eventFilter(watched, event);
}

void ServerManagePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 回到本页时重新扫描磁盘：玩家在游戏里新加的服务器要能马上看到，
    // 不必重启启动器（离开页面期间游戏可能已经改写了 servers.dat）
    if (!m_instancePath.isEmpty() && diskStamp() != m_lastDiskStamp)
        reloadServers();
    // 页面重新可见时（切回来 / 从别的页返回）刷新一次，避免长时间显示旧状态
    else if (m_pinger && QDateTime::currentMSecsSinceEpoch() - m_lastPingSweepMs > 30000)
        pingServers(false);
}

void ServerManagePage::pingServers(bool force)
{
    if (!m_pinger || m_cardWidgets.isEmpty())
        return;
    // 页面还在后台时不发请求，等真正切到本页再由 showEvent 触发
    if (!isVisible())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastPingSweepMs = now;
    const qint64 freshMs = 60000;   // 一分钟内的结果视为新鲜，不重复探测

    for (QWidget *card : m_cardWidgets)
    {
        if (!card)
            continue;
        const QString key = card->property("serverKey").toString();
        if (key.isEmpty())
            continue;
        quint16 port = 25565;
        QString host = key;
        for (const ServerEntry &entry : m_servers)
        {
            if (ServersDatIO::addressKey(entry.address, entry.port) != key)
                continue;
            host = entry.address;
            port = entry.port;
            break;
        }
        if (!force && m_pingAtMs.contains(key) && now - m_pingAtMs.value(key) < freshMs)
        {
            applyPingResultToCard(card);
            continue;
        }
        applyPingResultToCard(card);
        pingOne(key, host, port);
    }
}

void ServerManagePage::pingOne(const QString &key, const QString &host, quint16 port)
{
    if (!m_pinger || key.isEmpty())
        return;

    // 地址里带了端口就用地址里的，反之用条目端口
    QString hostPart = host.trimmed();
    quint16 portPart = port;
    if (hostPart.contains(QLatin1Char(':')) && !hostPart.startsWith(QLatin1Char('[')))
    {
        QString parsedHost;
        quint16 parsedPort = 0;
        if (ServersDatIO::parseAddress(hostPart, parsedHost, parsedPort))
        {
            hostPart = parsedHost;
            portPart = parsedPort;
        }
    }

    m_pingResults.insert(key, McServerPinger::Result());   // Pending
    m_pinger->ping(key, hostPart, portPart);
}

namespace
{
/** 把服务器图标裁成圆角方块：先按短边铺满缩放，再居中裁剪 */
QPixmap makeRoundedIcon(const QPixmap &source, int side)
{
    if (source.isNull() || side <= 0)
        return QPixmap();
    const QPixmap scaled = source.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
    QPixmap out(side, side);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, side, side), 8, 8);
    painter.setClipPath(clip);
    painter.drawPixmap((side - scaled.width()) / 2, (side - scaled.height()) / 2, scaled);
    painter.end();
    return out;
}

/** 按标签当前宽度重排 "fullText" 里的完整文案，放不下时右侧省略并挂上提示 */
void elideLabelToWidth(QLabel *label)
{
    if (!label)
        return;
    const QString full = label->property("fullText").toString();
    if (full.isEmpty())
    {
        label->clear();
        label->setToolTip(QString());
        return;
    }
    // 布局尚未铺开时先放全文，等 Resize 事件再按实际宽度省略
    if (label->width() < 20)
    {
        label->setText(full);
        label->setToolTip(QString());
        return;
    }
    const QFontMetrics metrics(label->font());
    const QString shown = metrics.elidedText(full, Qt::ElideRight, label->width() - 2);
    label->setText(shown);
    label->setToolTip(shown == full ? QString() : full);
}
}   // namespace

void ServerManagePage::applyPingResultToCard(QWidget *card)
{
    if (!card)
        return;
    QLabel *statusLabel = card->findChild<QLabel *>("serverCardStatus");
    QLabel *infoLabel = card->findChild<QLabel *>("serverCardInfo");
    QLabel *motdLabel = card->findChild<QLabel *>("serverCardMotd");
    QLabel *iconLabel = card->findChild<QLabel *>("serverCardIcon");
    if (!statusLabel || !infoLabel || !motdLabel)
        return;

    const QString key = card->property("serverKey").toString();
    const bool hasResult = m_pingResults.contains(key);
    McServerPinger::Result result;
    if (hasResult)
        result = m_pingResults.value(key);

    statusLabel->setToolTip(QString());
    switch (result.state)
    {
    case McServerPinger::Result::Online:
        if (result.latencyMs >= 0 && result.latencyMs < 200)
            statusLabel->setStyleSheet("font-size: 11px; color: #4CAF50;");
        else if (result.latencyMs < 600)
            statusLabel->setStyleSheet("font-size: 11px; color: #CDDC39;");
        else
            statusLabel->setStyleSheet("font-size: 11px; color: #FFB300;");
        statusLabel->setText(tr("● 在线"));
        break;
    case McServerPinger::Result::Offline:
        statusLabel->setStyleSheet("font-size: 11px; color: #E57373;");
        statusLabel->setText(tr("● 离线"));
        // 离线原因挂在角标上：人数那一行是空的，悬停不到
        if (!result.error.isEmpty())
            statusLabel->setToolTip(result.error);
        break;
    default:
        statusLabel->setStyleSheet("font-size: 11px; color: #888;");
        statusLabel->setText(tr("● 检测中…"));
        break;
    }

    if (result.state == McServerPinger::Result::Online)
    {
        QStringList parts;
        if (result.playersMax > 0 || result.playersOnline > 0)
            parts << tr("%1/%2 人").arg(result.playersOnline).arg(result.playersMax);
        if (!result.versionName.isEmpty())
            parts << result.versionName;
        if (result.latencyMs >= 0)
            parts << tr("%1 ms").arg(result.latencyMs);
        infoLabel->setText(parts.join(QStringLiteral(" · ")));
        infoLabel->setToolTip(result.protocolVersion > 0
                                  ? tr("协议版本 %1").arg(result.protocolVersion)
                                  : QString());
    }
    else if (result.state == McServerPinger::Result::Offline)
    {
        infoLabel->setText(QString());
        infoLabel->setToolTip(result.error);
    }
    else
    {
        infoLabel->setText(QString());
        infoLabel->setToolTip(QString());
    }

    // MOTD 优先；离线/未探测时退回用户填写的描述
    QString motd = (result.state == McServerPinger::Result::Online) ? result.motd : QString();
    if (motd.isEmpty())
        motd = card->property("serverCardDescription").toString();
    motdLabel->setProperty("fullText", motd);
    motdLabel->setVisible(!motd.trimmed().isEmpty());

    if (iconLabel)
    {
        const int side = qMax(40, iconLabel->width());
        if (result.state == McServerPinger::Result::Online && !result.icon.isNull())
        {
            iconLabel->setStyleSheet(QStringLiteral("background: transparent;"));
            iconLabel->setPixmap(makeRoundedIcon(result.icon, side));
        }
        else
        {
            iconLabel->setStyleSheet(QString());
            const QPixmap fallback = qvariant_cast<QPixmap>(
                iconLabel->property("defaultIcon"));
            if (!fallback.isNull())
                iconLabel->setPixmap(fallback.scaled(side, side, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
        }
    }

    elideCardTexts(card);
}

void ServerManagePage::elideCardTexts(QWidget *card)
{
    if (!card)
        return;
    elideLabelToWidth(card->findChild<QLabel *>("modNameLabel"));
    elideLabelToWidth(card->findChild<QLabel *>("serverCardMotd"));
}

