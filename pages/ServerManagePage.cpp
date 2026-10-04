#include "ServerManagePage.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

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
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSpacerItem>
#include <QUrl>

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
{
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

    // 顶部搜索/统计条：微透明玻璃态卡片
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索服务器名称或地址..."));
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);
    cardLayout->addWidget(m_searchEdit);

    QHBoxLayout *statsBar = new QHBoxLayout();
    statsBar->setSpacing(8);
    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("statusLabel");
    statsBar->addWidget(m_statsLabel);
    statsBar->addStretch();
    cardLayout->addLayout(statsBar);
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

    m_emptyLabel = new QLabel(tr("暂无服务器，点击底部「添加服务器」按钮添加"));
    m_emptyLabel->setObjectName("exampleContentLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    m_cardGridLayout->addWidget(m_emptyLabel, 0, 0, 1, 1, Qt::AlignCenter);

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
        showEmptyHint(true);
        m_statsLabel->setText(tr("共 0 个服务器"));
        return;
    }
    showEmptyHint(false);

    QColor themeColor = ThemeManager::instance()->currentThemeColor();
    int col = 0;
    int row = 0;
    int visibleCount = 0;

    for (int i = 0; i < m_servers.size(); ++i)
    {
        const ServerEntry &entry = m_servers[i];
        if (!m_currentSearch.isEmpty())
        {
            bool match = entry.name.contains(m_currentSearch, Qt::CaseInsensitive)
                      || entry.address.contains(m_currentSearch, Qt::CaseInsensitive)
                      || entry.description.contains(m_currentSearch, Qt::CaseInsensitive);
            if (!match)
                continue;
        }

        QWidget *card = new QWidget(m_cardContainer);
        card->setObjectName("resourceCard");
        card->setFixedHeight(80);
        card->setCursor(Qt::PointingHandCursor);

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
        QLabel *nameLabel = new QLabel(entry.name, card);
        nameLabel->setObjectName("modNameLabel");
        nameLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
        infoLayout->addWidget(nameLabel);

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

    m_statsLabel->setText(tr("共 %1 个服务器").arg(visibleCount));
}

void ServerManagePage::showEmptyHint(bool show)
{
    m_emptyLabel->setVisible(show);
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
