#include "ServerManagePage.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/LocalCategoryManager.h"
#include "components/LocalCategoryDialog.h"
#include "components/FavoriteFolderDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include "components/AppMessageBox.h"
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
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

    bottomLayout->addWidget(m_addServerBtn);
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
}

QString ServerManagePage::serversFilePath() const
{
    if (m_instancePath.isEmpty())
        return QString();
    return m_instancePath + "/servers.json";
}

void ServerManagePage::loadServers()
{
    m_servers.clear();
    QString path = serversFilePath();
    if (path.isEmpty() || !QFile::exists(path))
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

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
        m_servers.append(entry);
    }
}

void ServerManagePage::saveServers()
{
    QString path = serversFilePath();
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
}

void ServerManagePage::placeCards()
{
    clearCards();

    if (m_servers.isEmpty())
    {
        m_emptyLabel->setText(tr("暂无服务器，点击底部「添加服务器」按钮添加"));
        showEmptyHint(true);
        m_statsLabel->setText(tr("共 0 个服务器"));
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
    const bool anyFilterActive = !m_currentSearch.isEmpty()
        || !m_currentCategoryId.isEmpty();
    if (visibleCount == 0)
    {
        m_emptyLabel->setText(tr("没有符合筛选条件的服务器"));
        showEmptyHint(true);
    }
    else
    {
        showEmptyHint(false);
    }
    if (anyFilterActive)
    {
        m_statsLabel->setText(tr("显示 %1 个服务器 (共 %2 个)")
            .arg(visibleCount).arg(m_servers.size()));
    }
    else
    {
        m_statsLabel->setText(tr("共 %1 个服务器").arg(m_servers.size()));
    }
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
        m_servers[idx].name = nameEdit->text().trimmed();
        m_servers[idx].address = addrEdit->text().trimmed();
        m_servers[idx].port = static_cast<quint16>(portEdit->text().toUShort());
        m_servers[idx].description = descEdit->text().trimmed();
        saveServers();
        placeCards();
    }
}

void ServerManagePage::onDeleteServerClicked()
{
    if (m_servers.isEmpty())
        return;

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
                m_servers.removeAt(i);
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
