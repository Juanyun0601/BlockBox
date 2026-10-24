#include "ServerManagePage.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/LocalCategoryManager.h"
#include "utils/ServersDatIO.h"
#include "components/LocalCategoryDialog.h"
#include "components/FavoriteFolderDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
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
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSpacerItem>
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
    initUI();
}

ServerManagePage::~ServerManagePage()
{
}

void ServerManagePage::setInstancePath(const QString &path)
{
    if (m_instancePath != path)
    {
        m_instancePath = path;
        m_categoryManager->setInstancePath(path);
        reloadCategoryCombo();
        if (!path.isEmpty())
        {
            loadServers();
            placeCards();
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

QString ServerManagePage::serversDatPath() const
{
    if (m_instancePath.isEmpty())
        return QString();
    return m_instancePath + "/servers.dat";
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
    const QString datPath = serversDatPath();
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
}

void ServerManagePage::removeGameServer(const ServerEntry &entry)
{
    const QString datPath = serversDatPath();
    if (datPath.isEmpty() || entry.address.trimmed().isEmpty())
        return;
    ServersDatIO::remove(datPath, entry.address, entry.port);
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
    // 遮罩随卡片销毁，动画由本页持有，需显式释放
    m_selectionOverlays.clear();
    m_selectionEffects.clear();
    for (QPropertyAnimation *anim : m_selectionAnims)
        anim->deleteLater();
    m_selectionAnims.clear();
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
        card->setFixedHeight(80);
        card->setCursor(Qt::PointingHandCursor);
        card->setProperty("serverKey",
            ServersDatIO::addressKey(entry.address, entry.port));
        card->installEventFilter(this);
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        const QString catKey = entry.address;
        connect(card, &QWidget::customContextMenuRequested, this,
            [this, catKey](const QPoint &pos) {
                QMenu menu(this);
                appendCategoryMenu(&menu, catKey);
                menu.exec(pos);
            });

        QHBoxLayout *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(10);

        QLabel *iconLabel = new QLabel(card);
        iconLabel->setObjectName("serverCardIcon");
        iconLabel->setFixedSize(40, 40);
        QIcon srvIcon = IconHelper::loadColoredIcon(
            ":/Images/Icons/server.svg", themeColor, 32);
        iconLabel->setPixmap(srvIcon.pixmap(32, 32));
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
        nameRow->addWidget(nameLabel);
        if (!catName.isEmpty())
        {
            QLabel *catChip = new QLabel(catName, card);
            catChip->setObjectName("modChip");
            catChip->setProperty("cardRole", "chip");
            nameRow->addWidget(catChip);
        }
        nameRow->addStretch();
        infoLayout->addLayout(nameRow);

        QString addrText = entry.address;
        if (entry.port != 25565)
            addrText += QString(":%1").arg(entry.port);
        QLabel *addrLabel = new QLabel(addrText, card);
        addrLabel->setObjectName("modDescLabel");
        addrLabel->setStyleSheet("font-size: 12px; color: #888;");
        infoLayout->addWidget(addrLabel);

        if (!entry.description.isEmpty())
        {
            QLabel *descLabel = new QLabel(entry.description, card);
            descLabel->setObjectName("modDescLabel");
            descLabel->setStyleSheet("font-size: 11px; color: #aaa;");
            descLabel->setTextFormat(Qt::PlainText);
            infoLayout->addWidget(descLabel);
        }
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
    QWidget *overlay = new QWidget(card);
    overlay->setObjectName("cardSelectionOverlay");
    overlay->setGeometry(card->rect());
    overlay->setStyleSheet(selectionOverlayStyle());
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->raise();
    overlay->hide();

    QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(overlay);
    effect->setOpacity(0.0);
    overlay->setGraphicsEffect(effect);

    QPropertyAnimation *anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(180);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // 淡出结束后隐藏遮罩（overlay 销毁时自动断开，避免悬空指针）
    connect(anim, &QPropertyAnimation::finished, overlay, [overlay, effect]() {
        if (effect->opacity() <= 0.01)
            overlay->hide();
    });

    m_selectionOverlays.append(overlay);
    m_selectionEffects.append(effect);
    m_selectionAnims.append(anim);
    return overlay;
}

void ServerManagePage::animateCardHighlight(int idx, bool selected)
{
    if (idx < 0 || idx >= m_selectionOverlays.size())
        return;

    QWidget *overlay = m_selectionOverlays[idx];
    QGraphicsOpacityEffect *effect = m_selectionEffects[idx];
    QPropertyAnimation *anim = m_selectionAnims[idx];

    anim->stop();
    anim->setStartValue(effect->opacity());
    anim->setEndValue(selected ? 1.0 : 0.0);
    if (selected)
        overlay->show();
    anim->start();
}

QString ServerManagePage::selectionOverlayStyle() const
{
    QColor c = ThemeManager::instance()->currentThemeColor();
    return QString("#cardSelectionOverlay { background-color: rgba(%1, %2, %3, 40); "
                   "border: 2px solid %4; border-radius: 6px; }")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name());
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
    }

    return QWidget::eventFilter(watched, event);
}
