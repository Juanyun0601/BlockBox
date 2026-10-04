#include "EditInstancePage.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QPainter>

#include "components/NotificationManager.h"

EditInstancePage::EditInstancePage(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(nullptr)
    , m_hintLabel(nullptr)
    , m_instanceNameLabel(nullptr)
    , m_currentVersionIcon(nullptr)
    , m_currentVersionLabel(nullptr)
    , m_currentLoaderLabel(nullptr)
    , m_changeVersionBtn(nullptr)
    , m_loaderCardsContainer(nullptr)
    , m_loaderCardsLayout(nullptr)
    , m_startModifyBtn(nullptr)
{
    initUI();
}

EditInstancePage::~EditInstancePage()
{
}

void EditInstancePage::setInstancePath(const QString &instancePath)
{
    m_instancePath = instancePath;
}

void EditInstancePage::setInstanceName(const QString &name)
{
    m_instanceName = name;
    if (m_instanceNameLabel)
        m_instanceNameLabel->setText(name);
}

void EditInstancePage::setGameVersion(const QString &version)
{
    m_gameVersion = version;
    m_selectedVersion = version;
    updateVersionDisplay();
    refreshLoaderCards();
}

void EditInstancePage::setLoaderInfo(const QString &loader)
{
    m_loaderInfo = loader;
    updateVersionDisplay();
    refreshLoaderCards();
}

void EditInstancePage::initUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *contentWidget = new QWidget(scrollArea);
    QVBoxLayout *layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(20, 0, 20, 20);
    layout->setSpacing(16);

    m_titleLabel = new QLabel(tr("实例修改"), contentWidget);
    m_titleLabel->setObjectName("sectionTitle");
    layout->addWidget(m_titleLabel);

    m_hintLabel = new QLabel(
        tr("修改当前实例的游戏版本和加载器。修改后启动器将自动下载所需文件。"), contentWidget);
    m_hintLabel->setObjectName("hintLabel");
    m_hintLabel->setWordWrap(true);
    layout->addWidget(m_hintLabel);

    // 当前实例信息卡片
    QWidget *instanceInfoCard = new QWidget(contentWidget);
    instanceInfoCard->setObjectName("funcCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(instanceInfoCard);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(6);

    m_instanceNameLabel = new QLabel(m_instanceName, instanceInfoCard);
    m_instanceNameLabel->setObjectName("instanceNameLabel");
    m_instanceNameLabel->setStyleSheet("font-size: 18px; font-weight: bold;");

    QHBoxLayout *infoRow = new QHBoxLayout();
    infoRow->setSpacing(20);

    m_currentVersionIcon = new QLabel(instanceInfoCard);
    m_currentVersionIcon->setFixedSize(24, 24);
    m_currentVersionIcon->setAlignment(Qt::AlignCenter);

    m_currentVersionLabel = new QLabel(
        tr("游戏版本: %1").arg(m_gameVersion.isEmpty() ? tr("未设置") : m_gameVersion), instanceInfoCard);
    m_currentVersionLabel->setObjectName("hintLabel");
    m_currentVersionLabel->setStyleSheet("font-size: 13px; color: #4CAF50;");

    m_currentLoaderLabel = new QLabel(
        tr("加载器: %1").arg(m_loaderInfo.isEmpty() ? tr("原版") : m_loaderInfo), instanceInfoCard);
    m_currentLoaderLabel->setObjectName("hintLabel");
    m_currentLoaderLabel->setStyleSheet("font-size: 13px; color: #FF9800;");

    infoRow->addWidget(m_currentVersionLabel);
    infoRow->addWidget(m_currentLoaderLabel);
    infoRow->addStretch();

    cardLayout->addWidget(m_instanceNameLabel);
    cardLayout->addLayout(infoRow);
    layout->addWidget(instanceInfoCard);

    // 游戏版本卡片
    QWidget *versionCard = new QWidget(contentWidget);
    versionCard->setObjectName("funcCard");
    versionCard->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *versionRow = new QHBoxLayout(versionCard);
    versionRow->setContentsMargins(16, 14, 16, 14);
    versionRow->setSpacing(12);

    QLabel *versionIcon = new QLabel(versionCard);
    versionIcon->setFixedSize(40, 40);
    versionIcon->setAlignment(Qt::AlignCenter);
    QPixmap verPix(40, 40);
    verPix.fill(QColor("#4CAF50").lighter(180));
    {
        QPainter p(&verPix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor("#2E7D32"));
        p.setFont(QFont("", 18, QFont::Bold));
        p.drawText(verPix.rect(), Qt::AlignCenter, "V");
        p.end();
    }
    versionIcon->setPixmap(verPix);

    QVBoxLayout *versionInfo = new QVBoxLayout();
    versionInfo->setSpacing(2);
    QLabel *verTitle = new QLabel(tr("游戏版本"), versionCard);
    verTitle->setStyleSheet("font-size: 13px; color: #666;");
    QLabel *verValue = new QLabel(m_gameVersion.isEmpty() ? tr("未设置") : m_gameVersion, versionCard);
    verValue->setStyleSheet("font-size: 15px; font-weight: bold; color: #333;");
    verValue->setObjectName("versionValueLabel");
    versionInfo->addWidget(verTitle);
    versionInfo->addWidget(verValue);

    m_changeVersionBtn = new QPushButton(tr("修改"), versionCard);
    m_changeVersionBtn->setObjectName("actionButton");
    m_changeVersionBtn->setFixedWidth(60);
    m_changeVersionBtn->setCursor(Qt::PointingHandCursor);

    versionRow->addWidget(versionIcon);
    versionRow->addLayout(versionInfo, 1);
    versionRow->addWidget(m_changeVersionBtn);

    connect(m_changeVersionBtn, &QPushButton::clicked, this, [this]() {
        emit modifyRequested(m_instancePath, m_selectedVersion,
                             m_selectedLoaderName, m_selectedLoaderVersion);
    });

    layout->addWidget(versionCard);

    // 加载器区域标题
    QLabel *loaderSectionTitle = new QLabel(tr("加载器 / 模组 API"), contentWidget);
    loaderSectionTitle->setStyleSheet("font-size: 16px; font-weight: 600; color: #333; margin-top: 8px;");
    layout->addWidget(loaderSectionTitle);

    QLabel *loaderHint = new QLabel(
        tr("选择一个加载器。各加载器之间互不兼容，选择后将清除其他加载器。"), contentWidget);
    loaderHint->setObjectName("hintLabel");
    loaderHint->setWordWrap(true);
    layout->addWidget(loaderHint);

    // 加载器卡片容器
    m_loaderCardsContainer = new QWidget(contentWidget);
    m_loaderCardsLayout = new QVBoxLayout(m_loaderCardsContainer);
    m_loaderCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_loaderCardsLayout->setSpacing(10);
    layout->addWidget(m_loaderCardsContainer);

    layout->addStretch();

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);

    // 底部操作栏
    QWidget *bottomBar = new QWidget(this);
    bottomBar->setObjectName("unsavedChangesBar");
    bottomBar->setFixedHeight(56);
    QHBoxLayout *barLayout = new QHBoxLayout(bottomBar);
    barLayout->setContentsMargins(20, 0, 20, 0);

    QLabel *barHint = new QLabel(
        tr("修改后启动器将下载游戏文件并更新实例配置"), bottomBar);
    barHint->setObjectName("unsavedChangesLabel");

    m_startModifyBtn = new QPushButton(tr("开始修改"), bottomBar);
    m_startModifyBtn->setObjectName("saveButton");
    m_startModifyBtn->setFixedHeight(36);
    m_startModifyBtn->setCursor(Qt::PointingHandCursor);

    connect(m_startModifyBtn, &QPushButton::clicked, this, [this]() {
        if (m_selectedVersion.isEmpty()) {
            NotificationManager::showInfo(this, tr("请先选择游戏版本"));
            return;
        }
        emit modifyRequested(m_instancePath, m_selectedVersion,
                             m_selectedLoaderName, m_selectedLoaderVersion);
    });

    barLayout->addWidget(barHint);
    barLayout->addStretch();
    barLayout->addWidget(m_startModifyBtn);

    outerLayout->addWidget(bottomBar);
}

void EditInstancePage::refreshLoaderCards()
{
    // 清除旧卡片
    QLayoutItem *item;
    while ((item = m_loaderCardsLayout->takeAt(0)) != nullptr) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_loaders.clear();

    bool hasLoader = !m_loaderInfo.isEmpty();
    QString currentLoader = m_loaderInfo.toLower();

    struct LoaderDef {
        QString key;
        QString displayName;
        QString iconColor;
        QString mcMin;
        QString mcMax;
    };

    QList<LoaderDef> loaderDefs = {
        {"forge",        tr("Forge"),        "#d35400",  "",  ""},
        {"fabric",       tr("Fabric"),       "#8e44ad",  "",  ""},
        {"neoforge",     tr("NeoForge"),     "#2ecc71",  "",  ""},
        {"quilt",        tr("Quilt"),        "#3498db",  "",  ""},
        {"liteloader",   tr("LiteLoader"),   "#1abc9c",  "",  ""},
        {"optifine",     tr("OptiFine"),     "#e67e22",  "",  ""},
        {"fabric-api",   tr("Fabric API"),   "#9b59b6",  "",  ""},
    };

    for (const LoaderDef &def : loaderDefs) {
        bool installed = hasLoader && currentLoader.contains(def.key);
        QString installedVer = installed ? (m_loaderInfo + " " + tr("已安装")) : QString();

        QWidget *card = createLoaderCard(def.key, def.displayName, def.iconColor,
                                         installed, installed ? m_loaderInfo : QString());
        m_loaderCardsLayout->addWidget(card);
    }
}

QWidget *EditInstancePage::createLoaderCard(const QString &name, const QString &displayName,
                                             const QString &iconColor, bool installed,
                                             const QString &installedVersion)
{
    QWidget *card = new QWidget(m_loaderCardsContainer);
    card->setObjectName("funcCard");
    card->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *row = new QHBoxLayout(card);
    row->setContentsMargins(16, 14, 16, 14);
    row->setSpacing(12);

    QLabel *icon = new QLabel(card);
    icon->setFixedSize(40, 40);
    icon->setAlignment(Qt::AlignCenter);
    QPixmap pix(40, 40);
    pix.fill(QColor(iconColor).lighter(180));
    {
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(iconColor));
        QFont f = font();
        f.setPixelSize(16);
        f.setBold(true);
        p.setFont(f);
        p.drawText(pix.rect(), Qt::AlignCenter, displayName.left(2));
        p.end();
    }
    icon->setPixmap(pix);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(2);
    QLabel *nameLabel = new QLabel(displayName, card);
    nameLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #333;");

    QLabel *statusLabel = new QLabel(
        installed ? tr("已安装") : tr("点击选择"), card);
    statusLabel->setStyleSheet(
        installed ? "font-size: 12px; color: #4CAF50;" : "font-size: 12px; color: #999;");
    statusLabel->setObjectName("loaderStatusLabel");

    infoLayout->addWidget(nameLabel);
    infoLayout->addWidget(statusLabel);

    QHBoxLayout *actionRow = new QHBoxLayout();
    actionRow->setSpacing(6);

    QPushButton *actionBtn = new QPushButton(
        installed ? tr("更换版本") : tr("安装"), card);
    actionBtn->setObjectName("actionButton");
    actionBtn->setFixedWidth(80);
    actionBtn->setCursor(Qt::PointingHandCursor);

    QPushButton *removeBtn = nullptr;
    if (installed) {
        removeBtn = new QPushButton(tr("移除"), card);
        removeBtn->setObjectName("cancelButton");
        removeBtn->setFixedWidth(60);
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setStyleSheet("color: #e74c3c; border-color: #e74c3c;");

        connect(removeBtn, &QPushButton::clicked, this, [this, name]() {
            m_selectedLoaderName.clear();
            m_selectedLoaderVersion.clear();
            refreshLoaderCards();
            NotificationManager::showInfo(this, tr("已移除 %1").arg(name));
        });
    }

    connect(actionBtn, &QPushButton::clicked, this, [this, name, displayName]() {
        m_selectedLoaderName = name;
        m_selectedLoaderVersion.clear();
        emit modifyRequested(m_instancePath, m_selectedVersion,
                             m_selectedLoaderName, m_selectedLoaderVersion);
    });

    actionRow->addWidget(actionBtn);
    if (removeBtn)
        actionRow->addWidget(removeBtn);

    row->addWidget(icon);
    row->addLayout(infoLayout, 1);
    row->addLayout(actionRow);

    LoaderEntry entry;
    entry.displayName = displayName;
    entry.iconColor = iconColor;
    entry.installed = installed;
    entry.installedVersion = installedVersion;
    entry.card = card;
    entry.statusLabel = statusLabel;
    entry.actionBtn = actionBtn;
    entry.removeBtn = removeBtn;
    m_loaders[name] = entry;

    return card;
}

void EditInstancePage::updateVersionDisplay()
{
    if (m_currentVersionLabel)
        m_currentVersionLabel->setText(
            tr("游戏版本: %1").arg(m_gameVersion.isEmpty() ? tr("未设置") : m_gameVersion));
    if (m_currentLoaderLabel)
        m_currentLoaderLabel->setText(
            tr("加载器: %1").arg(m_loaderInfo.isEmpty() ? tr("原版") : m_loaderInfo));
}
