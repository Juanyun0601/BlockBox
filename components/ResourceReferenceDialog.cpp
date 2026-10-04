/**
 * @file   ResourceReferenceDialog.cpp
 * @brief  AI 助手资源引用选择对话框实现
 * @author BlockBox Team
 * @date   2026-07-17
 */

#include "ResourceReferenceDialog.h"

#include <QComboBox>
#include <QColor>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "utils/SettingsManager.h"
#include "utils/mod/ModrinthAPI.h"

namespace
{
    /// 数据角色：标记可勾选的引用条目，并保存其元信息
    constexpr int RoleIsReference = Qt::UserRole;      ///< bool：是否为引用条目
    constexpr int RoleCategory = Qt::UserRole + 1;     ///< QString：类别
    constexpr int RolePath = Qt::UserRole + 2;         ///< QString：绝对路径或 URL
    constexpr int RoleSize = Qt::UserRole + 3;         ///< qlonglong：文件大小
    constexpr int RoleExtra = Qt::UserRole + 4;        ///< QString：附加说明

    /// 根据 ModInfo 推断 Modrinth URL 路径段
    QString projectTypeForUrl(const ModInfo &mod)
    {
        QString t = mod.modType.toLower();
        if (t == QStringLiteral("mod")) return QStringLiteral("mod");
        if (t == QStringLiteral("resourcepack")) return QStringLiteral("resourcepack");
        if (t == QStringLiteral("shader")) return QStringLiteral("shader");
        if (t == QStringLiteral("datapack")) return QStringLiteral("datapack");
        return QStringLiteral("mod");
    }
}

ResourceReferenceDialog::ResourceReferenceDialog(const QString &instancePath,
                                                 const QString &instanceVersion,
                                                 const QString &instanceLoader,
                                                 QWidget *parent)
    : AppDialogBase(parent)
    , m_instancePath(instancePath)
    , m_instanceRoot(resolveInstanceRoot(instancePath))
    , m_instanceVersion(instanceVersion)
    , m_instanceLoader(instanceLoader)
    , m_tabs(nullptr)
    , m_addFileBtn(nullptr)
    , m_instanceTree(nullptr)
    , m_instanceItem(nullptr)
    , m_fileCategoryItem(nullptr)
    , m_localInstanceList(nullptr)
    , m_localTree(nullptr)
    , m_netSearchEdit(nullptr)
    , m_netTypeCombo(nullptr)
    , m_netSearchBtn(nullptr)
    , m_netResultList(nullptr)
    , m_netStatusLabel(nullptr)
    , m_modrinthApi(nullptr)
    , m_netPendingCount(0)
    , m_webUrlEdit(nullptr)
    , m_webTitleEdit(nullptr)
    , m_webFetchBtn(nullptr)
    , m_webAddBtn(nullptr)
    , m_webList(nullptr)
    , m_webNetwork(nullptr)
    , m_webReply(nullptr)
{
    setWindowTitle(tr("引用资源"));
    initUI();
    loadInstanceResources();
}

ResourceReferenceDialog::~ResourceReferenceDialog()
{
    if (m_webReply)
    {
        m_webReply->abort();
    }
}

void ResourceReferenceDialog::initUI()
{
    // 居中卡片容器（objectName 以 Card 结尾，供 AppDialogBase 定位与点遮罩关闭）
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("referenceDialogCard"));
    card->setMinimumSize(680, 520);
    card->setMaximumSize(860, 700);
    outer->addWidget(card, 0, Qt::AlignCenter);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    // 标题栏（与其它内置弹窗标题栏一致）
    auto *titleBar = new QWidget(card);
    titleBar->setObjectName(QStringLiteral("refDialogTitleBar"));
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(8);

    auto *titleLabel = new QLabel(tr("引用资源"), card);
    titleLabel->setObjectName(QStringLiteral("refDialogTitle"));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    layout->addWidget(titleBar);

    m_tabs = new QTabWidget(card);
    m_tabs->setObjectName(QStringLiteral("refTabs"));
    layout->addWidget(m_tabs, 1);

    initInstanceTab();
    initLocalTab();
    initNetworkTab();
    initWebTab();

    // 底部操作行：添加任意文件 + 确定/取消
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(8);

    m_addFileBtn = new QPushButton(tr("＋ 添加任意文件..."), card);
    m_addFileBtn->setObjectName(QStringLiteral("refAddFileBtn"));
    m_addFileBtn->setCursor(Qt::PointingHandCursor);
    m_addFileBtn->setFlat(true);
    connect(m_addFileBtn, &QPushButton::clicked, this, &ResourceReferenceDialog::onAddAnyFileClicked);
    bottomRow->addWidget(m_addFileBtn);
    bottomRow->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, card);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    buttonBox->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("refOkBtn"));
    buttonBox->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("refCancelBtn"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottomRow->addWidget(buttonBox);
    layout->addLayout(bottomRow);
}

void ResourceReferenceDialog::initInstanceTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *hint = new QLabel(tr("勾选当前实例中的模组、资源包、光影包、投影或当前实例本身。"), page);
    hint->setStyleSheet(QStringLiteral("color: #666666; font-size: 12px;"));
    layout->addWidget(hint);

    m_instanceTree = new QTreeWidget(page);
    m_instanceTree->setObjectName(QStringLiteral("refTree"));
    m_instanceTree->setColumnCount(3);
    m_instanceTree->setHeaderLabels(QStringList() << tr("名称") << tr("大小") << tr("路径"));
    m_instanceTree->setRootIsDecorated(true);
    m_instanceTree->setAlternatingRowColors(true);
    m_instanceTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_instanceTree->header()->setStretchLastSection(true);
    m_instanceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_instanceTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_instanceTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(m_instanceTree, 1);

    m_tabs->addTab(page, tr("实例资源"));
}

void ResourceReferenceDialog::initLocalTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *hint = new QLabel(tr("浏览本地全部实例中的模组、资源包、光影包、投影、存档与数据包，勾选后作为引用。"), page);
    hint->setStyleSheet(QStringLiteral("color: #666666; font-size: 12px;"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // 左右分栏：左侧实例列表，右侧资源类别树
    auto *splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName(QStringLiteral("refLocalSplitter"));

    m_localInstanceList = new QListWidget(splitter);
    m_localInstanceList->setObjectName(QStringLiteral("refLocalInstList"));
    m_localInstanceList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_localInstanceList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_localInstanceList->setMinimumWidth(190);
    splitter->addWidget(m_localInstanceList);

    m_localTree = new QTreeWidget(splitter);
    m_localTree->setObjectName(QStringLiteral("refTree"));
    m_localTree->setColumnCount(3);
    m_localTree->setHeaderLabels(QStringList() << tr("名称") << tr("大小") << tr("路径"));
    m_localTree->setAlternatingRowColors(true);
    m_localTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_localTree->header()->setStretchLastSection(true);
    m_localTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_localTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_localTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    splitter->addWidget(m_localTree);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({200, 460});

    layout->addWidget(splitter, 1);

    // 切换实例时重新填充右侧资源树
    connect(m_localInstanceList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *cur, QListWidgetItem *) {
        if (!cur)
        {
            return;
        }
        const QString path = cur->data(Qt::UserRole).toString();
        const bool bedrock = cur->data(Qt::UserRole + 1).toBool();
        populateLocalTree(path, bedrock);
    });

    m_tabs->addTab(page, tr("本地资源"));

    // 默认选中第一个条目（若存在）
    loadLocalInstances();
    if (m_localInstanceList->count() > 0)
    {
        m_localInstanceList->setCurrentRow(0);
    }
}

void ResourceReferenceDialog::loadLocalInstances()
{
    m_localInstanceList->clear();
    m_localInstances.clear();

    // 基岩版无需选择实例，作为伪条目置于列表顶部（仅当本机安装了基岩版）
    const QString comDir = bedrockComMojangDir();
    if (!comDir.isEmpty())
    {
        auto *bedrockItem = new QListWidgetItem(tr("基岩版本地资源"), m_localInstanceList);
        bedrockItem->setData(Qt::UserRole, comDir);
        bedrockItem->setData(Qt::UserRole + 1, true);
        bedrockItem->setToolTip(comDir);
        QFont boldFont = bedrockItem->font();
        boldFont.setBold(true);
        bedrockItem->setFont(boldFont);
    }

    // 遍历实例目录下的 versions/* 收集 Java 版实例
    QStringList roots;
    const QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    if (folders.isEmpty())
    {
        roots << QCoreApplication::applicationDirPath() + QStringLiteral("/.minecraft");
    }
    else
    {
        for (const InstanceFolderInfo &f : folders)
        {
            roots << f.path;
        }
    }

    QSet<QString> seen;
    for (const QString &root : roots)
    {
        QDir versionsDir(root + QStringLiteral("/versions"));
        if (!versionsDir.exists())
        {
            continue;
        }
        const QFileInfoList dirs = versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &dirInfo : dirs)
        {
            const QString dirPath = dirInfo.absoluteFilePath();
            if (seen.contains(dirPath))
            {
                continue;
            }
            seen.insert(dirPath);

            const QString name = dirInfo.fileName();
            QString jsonPath = dirPath + QLatin1Char('/') + name + QStringLiteral(".json");
            if (!QFile::exists(jsonPath))
            {
                jsonPath = dirPath + QStringLiteral("/version.json");
            }
            QJsonObject obj;
            {
                QFile f(jsonPath);
                if (f.open(QIODevice::ReadOnly))
                {
                    obj = QJsonDocument::fromJson(f.readAll()).object();
                }
            }
            if (obj.isEmpty())
            {
                continue;
            }

            // 依据 JSON 键名推断加载器
            QString loader;
            const QStringList keys = obj.keys();
            for (const QString &k : keys)
            {
                const QString kl = k.toLower();
                if (kl.contains(QStringLiteral("fabric")))   { loader = QStringLiteral("Fabric");   break; }
                if (kl.contains(QStringLiteral("quilt")))    { loader = QStringLiteral("Quilt");    break; }
                if (kl.contains(QStringLiteral("neoforge"))) { loader = QStringLiteral("NeoForge"); break; }
                if (kl.contains(QStringLiteral("forge")))    { loader = QStringLiteral("Forge");    break; }
            }

            QString version = obj.value(QStringLiteral("inheritsFrom")).toString();
            if (version.isEmpty())
            {
                version = obj.value(QStringLiteral("clientVersion")).toString();
            }
            if (version.isEmpty())
            {
                version = obj.value(QStringLiteral("id")).toString();
            }
            if (version.isEmpty())
            {
                version = name;
            }

            LocalInst inst;
            inst.name = name;
            inst.path = dirPath;
            inst.version = version;
            inst.loader = loader;
            m_localInstances.append(inst);
        }
    }

    for (const LocalInst &inst : m_localInstances)
    {
        QStringList parts;
        if (!inst.loader.isEmpty())
        {
            parts << inst.loader;
        }
        parts << QStringLiteral("MC ") + inst.version;
        const QString text = inst.name + QStringLiteral("  ·  ") + parts.join(QStringLiteral(" "));
        auto *item = new QListWidgetItem(text, m_localInstanceList);
        item->setData(Qt::UserRole, inst.path);
        item->setData(Qt::UserRole + 1, false);
        item->setToolTip(inst.path);
    }
}

void ResourceReferenceDialog::populateLocalTree(const QString &instancePath, bool bedrock)
{
    m_localTree->clear();
    if (instancePath.isEmpty())
    {
        return;
    }
    m_localTree->setUpdatesEnabled(false);

    auto addCategory = [&](const QString &category, const QString &subDir,
                           const QStringList &filters, bool dirsOnly) {
        auto *item = new QTreeWidgetItem(m_localTree);
        item->setText(0, category);
        addLocalFileItem(item, category, resolveResourceDirFor(instancePath, subDir),
                         filters, dirsOnly);
    };

    if (bedrock)
    {
        addCategory(tr("本地资源包"), QStringLiteral("resource_packs"),
                    { QStringLiteral("*.zip"), QStringLiteral("*.mcpack") }, false);
        addCategory(tr("本地行为包"), QStringLiteral("behavior_packs"),
                    { QStringLiteral("*.mcpack"), QStringLiteral("*.mcaddon"),
                      QStringLiteral("*.zip") }, false);
        addCategory(tr("本地世界"), QStringLiteral("minecraftWorlds"), {}, true);
        addCategory(tr("本地皮肤"), QStringLiteral("skins"), { QStringLiteral("*.png") }, false);
    }
    else
    {
        addCategory(tr("本地模组"), QStringLiteral("mods"),
                    { QStringLiteral("*.jar"), QStringLiteral("*.disabled") }, false);
        addCategory(tr("本地资源包"), QStringLiteral("resourcepacks"),
                    { QStringLiteral("*.zip") }, false);
        addCategory(tr("本地光影包"), QStringLiteral("shaderpacks"),
                    { QStringLiteral("*.zip") }, false);
        addCategory(tr("本地投影"), QStringLiteral("schematics"),
                    { QStringLiteral("*.litematic"), QStringLiteral("*.nbt"),
                      QStringLiteral("*.schematic"), QStringLiteral("*.schem") }, false);
        addCategory(tr("本地存档"), QStringLiteral("saves"), {}, true);
        addCategory(tr("本地数据包"), QStringLiteral("datapacks"),
                    { QStringLiteral("*.zip") }, false);
    }

    m_localTree->setUpdatesEnabled(true);
    m_localTree->expandAll();
}

void ResourceReferenceDialog::addLocalFileItem(QTreeWidgetItem *parent, const QString &category,
                                               const QString &dirPath, const QStringList &nameFilters,
                                               bool dirsOnly)
{
    QDir dir(dirPath);
    if (!dir.exists())
    {
        parent->setText(1, tr("(目录不存在)"));
        parent->setForeground(1, QColor("#999999"));
        return;
    }

    // 目录类资源（存档/世界）：列出子目录本身作为引用
    if (dirsOnly)
    {
        const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (subDirs.isEmpty())
        {
            parent->setText(1, tr("(空)"));
            parent->setForeground(1, QColor("#999999"));
            return;
        }
        for (const QFileInfo &fi : subDirs)
        {
            auto *item = new QTreeWidgetItem(parent);
            item->setText(0, fi.fileName());
            item->setText(1, tr("文件夹"));
            item->setText(2, fi.absoluteFilePath());
            item->setCheckState(0, Qt::Unchecked);
            item->setData(0, RoleIsReference, true);
            item->setData(0, RoleCategory, category);
            item->setData(0, RolePath, fi.absoluteFilePath());
            item->setData(0, RoleSize, static_cast<qlonglong>(0));
            item->setData(0, RoleExtra, QString());
        }
        return;
    }

    const QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files, QDir::Name);
    if (files.isEmpty())
    {
        parent->setText(1, tr("(空)"));
        parent->setForeground(1, QColor("#999999"));
        return;
    }

    for (const QFileInfo &fi : files)
    {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, fi.fileName());
        item->setText(1, formatSize(fi.size()));
        item->setText(2, fi.absoluteFilePath());
        item->setCheckState(0, Qt::Unchecked);
        item->setData(0, RoleIsReference, true);
        item->setData(0, RoleCategory, category);
        item->setData(0, RolePath, fi.absoluteFilePath());
        item->setData(0, RoleSize, static_cast<qlonglong>(fi.size()));
        item->setData(0, RoleExtra, QString());
    }
}

void ResourceReferenceDialog::initNetworkTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *hint = new QLabel(tr("搜索 Modrinth 在线模组、资源包、光影包、数据包，勾选后作为引用（不下载）。"), page);
    hint->setStyleSheet(QStringLiteral("color: #666666; font-size: 12px;"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // 搜索行
    auto *searchRow = new QHBoxLayout();
    searchRow->setSpacing(8);
    m_netTypeCombo = new QComboBox(page);
    m_netTypeCombo->setObjectName(QStringLiteral("refNetTypeCombo"));
    m_netTypeCombo->addItem(tr("模组"), 0);
    m_netTypeCombo->addItem(tr("资源包"), 1);
    m_netTypeCombo->addItem(tr("光影包"), 2);
    m_netTypeCombo->addItem(tr("数据包"), 3);
    searchRow->addWidget(m_netTypeCombo);

    m_netSearchEdit = new QLineEdit(page);
    m_netSearchEdit->setObjectName(QStringLiteral("refNetSearchEdit"));
    m_netSearchEdit->setPlaceholderText(tr("输入关键词后回车搜索..."));
    searchRow->addWidget(m_netSearchEdit, 1);

    m_netSearchBtn = new QPushButton(tr("搜索"), page);
    m_netSearchBtn->setObjectName(QStringLiteral("refNetSearchBtn"));
    m_netSearchBtn->setCursor(Qt::PointingHandCursor);
    searchRow->addWidget(m_netSearchBtn);
    layout->addLayout(searchRow);

    m_netStatusLabel = new QLabel(page);
    m_netStatusLabel->setStyleSheet(QStringLiteral("color: #999999; font-size: 12px;"));
    layout->addWidget(m_netStatusLabel);

    m_netResultList = new QListWidget(page);
    m_netResultList->setObjectName(QStringLiteral("refNetResultList"));
    m_netResultList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_netResultList, 1);

    // Modrinth API 初始化（异步搜索，结果通过信号回填）
    m_modrinthApi = new ModrinthAPI(this);
    QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    if (useMcim)
    {
        m_modrinthApi->setBaseUrl(QStringLiteral("https://mod.mcimirror.top/modrinth"));
    }
    connect(m_modrinthApi, &ModrinthAPI::searchCompleted,
            this, &ResourceReferenceDialog::onNetworkSearchCompleted);
    connect(m_modrinthApi, &ModrinthAPI::searchFailed,
            this, &ResourceReferenceDialog::onNetworkSearchFailed);
    connect(m_netSearchBtn, &QPushButton::clicked, this, &ResourceReferenceDialog::onNetworkSearchClicked);
    connect(m_netSearchEdit, &QLineEdit::returnPressed, this, &ResourceReferenceDialog::onNetworkSearchClicked);

    m_tabs->addTab(page, tr("网络资源"));
}

void ResourceReferenceDialog::initWebTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *hint = new QLabel(tr("输入网页 URL，可选自动抓取标题，添加为引用。"), page);
    hint->setStyleSheet(QStringLiteral("color: #666666; font-size: 12px;"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // URL 输入行
    auto *urlRow = new QHBoxLayout();
    urlRow->setSpacing(8);
    auto *urlLabel = new QLabel(tr("URL:"), page);
    urlRow->addWidget(urlLabel);
    m_webUrlEdit = new QLineEdit(page);
    m_webUrlEdit->setObjectName(QStringLiteral("refWebUrlEdit"));
    m_webUrlEdit->setPlaceholderText(tr("https://example.com/page"));
    urlRow->addWidget(m_webUrlEdit, 1);
    m_webFetchBtn = new QPushButton(tr("抓取标题"), page);
    m_webFetchBtn->setObjectName(QStringLiteral("refWebFetchBtn"));
    m_webFetchBtn->setCursor(Qt::PointingHandCursor);
    urlRow->addWidget(m_webFetchBtn);
    layout->addLayout(urlRow);

    // 标题行
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    auto *titleLabel = new QLabel(tr("标题:"), page);
    titleRow->addWidget(titleLabel);
    m_webTitleEdit = new QLineEdit(page);
    m_webTitleEdit->setObjectName(QStringLiteral("refWebTitleEdit"));
    m_webTitleEdit->setPlaceholderText(tr("可手动填写或点击「抓取标题」自动获取"));
    titleRow->addWidget(m_webTitleEdit, 1);
    m_webAddBtn = new QPushButton(tr("添加引用"), page);
    m_webAddBtn->setObjectName(QStringLiteral("refWebAddBtn"));
    m_webAddBtn->setCursor(Qt::PointingHandCursor);
    titleRow->addWidget(m_webAddBtn);
    layout->addLayout(titleRow);

    m_webList = new QListWidget(page);
    m_webList->setObjectName(QStringLiteral("refWebList"));
    m_webList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_webList, 1);

    m_webNetwork = new QNetworkAccessManager(this);
    connect(m_webFetchBtn, &QPushButton::clicked, this, &ResourceReferenceDialog::onFetchWebTitleClicked);
    connect(m_webAddBtn, &QPushButton::clicked, this, &ResourceReferenceDialog::onAddWebPageClicked);

    m_tabs->addTab(page, tr("网页"));
}

void ResourceReferenceDialog::loadInstanceResources()
{
    m_instanceTree->clear();

    // 「当前实例」可勾选项（仅当实例路径非空时提供）
    if (!m_instancePath.isEmpty())
    {
        m_instanceItem = new QTreeWidgetItem(m_instanceTree);
        m_instanceItem->setText(0, tr("当前实例"));
        m_instanceItem->setText(2, m_instancePath);
        m_instanceItem->setCheckState(0, Qt::Unchecked);
        QFont boldFont = m_instanceItem->font(0);
        boldFont.setBold(true);
        m_instanceItem->setFont(0, boldFont);
        m_instanceItem->setData(0, RoleIsReference, true);
        m_instanceItem->setData(0, RoleCategory, QStringLiteral("实例"));
        m_instanceItem->setData(0, RolePath, m_instancePath);
        m_instanceItem->setData(0, RoleSize, static_cast<qlonglong>(0));
        QStringList extraParts;
        if (!m_instanceVersion.isEmpty())
        {
            extraParts << tr("版本: %1").arg(m_instanceVersion);
        }
        if (!m_instanceLoader.isEmpty())
        {
            extraParts << tr("加载器: %1").arg(m_instanceLoader);
        }
        m_instanceItem->setData(0, RoleExtra, extraParts.join(QStringLiteral("，")));
    }

    // 各资源类别分组：通过 resolveResourceDir 兼容版本隔离/非隔离两种布局
    // 模组
    auto *modItem = new QTreeWidgetItem(m_instanceTree);
    modItem->setText(0, tr("模组"));
    addFileItem(modItem, QStringLiteral("模组"),
                resolveResourceDir(QStringLiteral("mods")),
                QStringList{QStringLiteral("*.jar"), QStringLiteral("*.disabled")});

    // 资源包
    auto *rpItem = new QTreeWidgetItem(m_instanceTree);
    rpItem->setText(0, tr("资源包"));
    addFileItem(rpItem, QStringLiteral("资源包"),
                resolveResourceDir(QStringLiteral("resourcepacks")),
                QStringList{QStringLiteral("*.zip")});

    // 光影包
    auto *spItem = new QTreeWidgetItem(m_instanceTree);
    spItem->setText(0, tr("光影包"));
    addFileItem(spItem, QStringLiteral("光影包"),
                resolveResourceDir(QStringLiteral("shaderpacks")),
                QStringList{QStringLiteral("*.zip")});

    // 投影
    auto *projItem = new QTreeWidgetItem(m_instanceTree);
    projItem->setText(0, tr("投影"));
    addFileItem(projItem, QStringLiteral("投影"),
                resolveResourceDir(QStringLiteral("schematics")),
                QStringList{QStringLiteral("*.litematic"),
                             QStringLiteral("*.nbt"),
                             QStringLiteral("*.schematic"),
                             QStringLiteral("*.schem")});

    // 存档（世界目录，以文件夹形式引用）
    auto *saveItem = new QTreeWidgetItem(m_instanceTree);
    saveItem->setText(0, tr("存档"));
    addLocalFileItem(saveItem, QStringLiteral("存档"),
                     resolveResourceDir(QStringLiteral("saves")),
                     QStringList(), true);

    // 「文件」分组（任意文件挂在下面，初始为空）
    m_fileCategoryItem = new QTreeWidgetItem(m_instanceTree);
    m_fileCategoryItem->setText(0, tr("文件"));
    m_fileCategoryItem->setExpanded(true);

    m_instanceTree->expandAll();
}

QString ResourceReferenceDialog::resolveResourceDir(const QString &subDir) const
{
    return resolveResourceDirFor(m_instancePath, subDir);
}

QString ResourceReferenceDialog::resolveResourceDirFor(const QString &instancePath, const QString &subDir)
{
    if (instancePath.isEmpty() || subDir.isEmpty())
    {
        return QString();
    }
    // 1. 版本隔离布局：资源在 versions/{ver}/{subDir}
    const QString isolated = instancePath + QStringLiteral("/") + subDir;
    if (QDir(isolated).exists())
    {
        return isolated;
    }
    // 2. 非隔离布局：资源在 .minecraft/{subDir}（即 versions/{ver}/../{subDir}）
    QDir parentDir(instancePath);
    parentDir.cdUp();
    const QString shared = parentDir.absoluteFilePath(subDir);
    if (QDir(shared).exists())
    {
        return shared;
    }
    // 3. 都不存在，返回隔离路径（addFileItem 会显示"目录不存在"）
    return isolated;
}

QString ResourceReferenceDialog::bedrockComMojangDir()
{
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (local.isEmpty())
    {
        return QString();
    }
    const QString pkgRoot = local + QStringLiteral("/Packages");
    QDir pkg(pkgRoot);
    if (!pkg.exists())
    {
        return QString();
    }
    const QFileInfoList pkgs = pkg.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : pkgs)
    {
        const QString fn = fi.fileName();
        if (fn.startsWith(QStringLiteral("Microsoft.Minecraft")))
        {
            const QString com = fi.absoluteFilePath()
                + QStringLiteral("/LocalState/games/com.mojang");
            if (QDir(com).exists())
            {
                return com;
            }
        }
    }
    return QString();
}

void ResourceReferenceDialog::addFileItem(QTreeWidgetItem *parent, const QString &category,
                                          const QString &dirPath, const QStringList &nameFilters)
{
    QDir dir(dirPath);
    if (!dir.exists())
    {
        parent->setText(1, tr("(目录不存在)"));
        parent->setForeground(1, QColor("#999999"));
        return;
    }

    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files, QDir::Name);
    if (files.isEmpty())
    {
        parent->setText(1, tr("(空)"));
        parent->setForeground(1, QColor("#999999"));
        return;
    }

    for (const QFileInfo &fi : files)
    {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, fi.fileName());
        item->setText(1, formatSize(fi.size()));
        item->setText(2, fi.absoluteFilePath());
        item->setCheckState(0, Qt::Unchecked);
        item->setData(0, RoleIsReference, true);
        item->setData(0, RoleCategory, category);
        item->setData(0, RolePath, fi.absoluteFilePath());
        item->setData(0, RoleSize, static_cast<qlonglong>(fi.size()));
        item->setData(0, RoleExtra, QString());
    }
}

void ResourceReferenceDialog::addCustomFileItem(const QString &filePath)
{
    if (!m_fileCategoryItem)
    {
        return;
    }
    QFileInfo fi(filePath);
    if (!fi.exists())
    {
        return;
    }

    // 避免重复添加
    for (int i = 0; i < m_fileCategoryItem->childCount(); ++i)
    {
        QTreeWidgetItem *child = m_fileCategoryItem->child(i);
        if (child->data(0, RolePath).toString() == fi.absoluteFilePath())
        {
            return;
        }
    }

    auto *item = new QTreeWidgetItem(m_fileCategoryItem);
    item->setText(0, fi.fileName());
    item->setText(1, formatSize(fi.size()));
    item->setText(2, fi.absoluteFilePath());
    item->setCheckState(0, Qt::Checked); // 新添加的任意文件默认勾选
    item->setData(0, RoleIsReference, true);
    item->setData(0, RoleCategory, QStringLiteral("文件"));
    item->setData(0, RolePath, fi.absoluteFilePath());
    item->setData(0, RoleSize, static_cast<qlonglong>(fi.size()));
    item->setData(0, RoleExtra, QString());
    m_fileCategoryItem->setExpanded(true);
    // 切到实例资源标签页让用户看到新增项
    m_tabs->setCurrentIndex(0);
}

void ResourceReferenceDialog::onAddAnyFileClicked()
{
    const QStringList files = AppFileDialog::getOpenFileNames(
        this, tr("选择要引用的文件"), QString(), tr("所有文件 (*.*)"));
    for (const QString &f : files)
    {
        addCustomFileItem(f);
    }
}

QString ResourceReferenceDialog::networkTypeToProjectType(int typeIndex) const
{
    switch (typeIndex)
    {
    case 0: return QStringLiteral("mod");
    case 1: return QStringLiteral("resourcepack");
    case 2: return QStringLiteral("shader");
    case 3: return QStringLiteral("datapack");
    default: return QStringLiteral("mod");
    }
}

QString ResourceReferenceDialog::networkTypeToCategory(int typeIndex) const
{
    switch (typeIndex)
    {
    case 0: return tr("网络模组");
    case 1: return tr("网络资源包");
    case 2: return tr("网络光影包");
    case 3: return tr("网络数据包");
    default: return tr("网络资源");
    }
}

void ResourceReferenceDialog::onNetworkSearchClicked()
{
    const QString query = m_netSearchEdit->text().trimmed();
    if (query.isEmpty())
    {
        m_netStatusLabel->setText(tr("请输入搜索关键词。"));
        return;
    }

    const int typeIdx = m_netTypeCombo->currentIndex();
    const QString projectType = networkTypeToProjectType(typeIdx);
    m_modrinthApi->setProjectType(projectType);

    // 状态提示
    m_netStatusLabel->setText(tr("正在搜索 Modrinth..."));
    m_netSearchBtn->setEnabled(false);
    ++m_netPendingCount;

    // 实例版本作为过滤（可为空）
    m_modrinthApi->searchMods(query, m_instanceVersion, QString(), 0, 30,
                              QStringLiteral("relevance"), QStringLiteral("desc"));
}

void ResourceReferenceDialog::onNetworkSearchCompleted(const ModSearchResult &result)
{
    // 仅处理最新一次请求的结果（忽略过期回调）
    if (m_netPendingCount > 0)
    {
        --m_netPendingCount;
    }
    if (m_netPendingCount > 0)
    {
        return;
    }

    m_netSearchBtn->setEnabled(true);
    m_netResultList->clear();

    if (result.mods.isEmpty())
    {
        m_netStatusLabel->setText(tr("无搜索结果。"));
        return;
    }

    m_netStatusLabel->setText(tr("找到 %1 项结果（勾选需要的条目）。").arg(result.totalHits));

    for (const ModInfo &mod : result.mods)
    {
        auto *item = new QListWidgetItem(m_netResultList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);

        // 显示文本：名称 + 下载量 + 简述
        QString display = mod.name;
        if (!mod.englishName.isEmpty() && mod.englishName != mod.name)
        {
            display += QStringLiteral(" (%1)").arg(mod.englishName);
        }
        if (mod.downloadCount > 0)
        {
            display += QStringLiteral("  ·  ↓%1").arg(mod.downloadCount);
        }
        if (!mod.description.isEmpty())
        {
            display += QStringLiteral("\n") + mod.description.left(120);
        }
        item->setText(display);
        item->setToolTip(mod.pageUrl.isEmpty() ? mod.id : mod.pageUrl);

        // 保存引用元信息
        QString category = networkTypeToCategory(m_netTypeCombo->currentIndex());
        QString url = mod.pageUrl.isEmpty()
                          ? QStringLiteral("https://modrinth.com/%1/%2").arg(projectTypeForUrl(mod), mod.id)
                          : mod.pageUrl;
        QStringList extraParts;
        extraParts << QStringLiteral("来源: Modrinth");
        if (!mod.author.isEmpty())
        {
            extraParts << tr("作者: %1").arg(mod.author);
        }
        if (!mod.latestVersion.isEmpty())
        {
            extraParts << tr("版本: %1").arg(mod.latestVersion);
        }
        item->setData(RoleIsReference, true);
        item->setData(RoleCategory, category);
        item->setData(RolePath, url);
        item->setData(RoleSize, static_cast<qlonglong>(mod.downloadCount));
        item->setData(RoleExtra, extraParts.join(QStringLiteral("，")));
    }
}

void ResourceReferenceDialog::onNetworkSearchFailed(const QString &error)
{
    if (m_netPendingCount > 0)
    {
        --m_netPendingCount;
    }
    if (m_netPendingCount > 0)
    {
        return;
    }
    m_netSearchBtn->setEnabled(true);
    m_netStatusLabel->setText(tr("搜索失败：%1").arg(error));
}

void ResourceReferenceDialog::onFetchWebTitleClicked()
{
    const QString url = m_webUrlEdit->text().trimmed();
    if (url.isEmpty())
    {
        m_webTitleEdit->setPlaceholderText(tr("请先输入 URL"));
        return;
    }

    // 简单校验 URL
    QUrl qurl(url);
    if (!qurl.isValid() || (qurl.scheme() != QStringLiteral("http") && qurl.scheme() != QStringLiteral("https")))
    {
        m_webTitleEdit->setText(QString());
        m_webTitleEdit->setPlaceholderText(tr("URL 无效，请输入 http/https 链接"));
        return;
    }

    m_webFetchBtn->setEnabled(false);
    m_webTitleEdit->setText(tr("正在抓取..."));

    if (m_webReply)
    {
        m_webReply->abort();
        m_webReply->deleteLater();
    }

    QNetworkRequest request(qurl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) BlockBox/1.0"));
    request.setRawHeader("Accept", "text/html");
    m_webReply = m_webNetwork->get(request);
    connect(m_webReply, &QNetworkReply::finished, this, &ResourceReferenceDialog::onWebReplyFinished);
}

void ResourceReferenceDialog::onWebReplyFinished()
{
    if (!m_webReply)
    {
        return;
    }
    m_webFetchBtn->setEnabled(true);

    QNetworkReply::NetworkError err = m_webReply->error();
    QByteArray data = m_webReply->readAll();
    QString url = m_webReply->request().url().toString();
    m_webReply->deleteLater();
    m_webReply = nullptr;

    if (err != QNetworkReply::NoError || data.isEmpty())
    {
        m_webTitleEdit->setText(QString());
        m_webTitleEdit->setPlaceholderText(tr("抓取失败，请手动输入标题"));
        return;
    }

    // 从 HTML 中提取 <title> 内容
    QString html = QString::fromUtf8(data);
    QRegularExpression re(QStringLiteral("<title[^>]*>(.*?)</title>"),
                          QRegularExpression::CaseInsensitiveOption
                              | QRegularExpression::DotMatchesEverythingOption);
    auto match = re.match(html);
    if (match.hasMatch())
    {
        QString title = match.captured(1).trimmed();
        title.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
        title.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        title.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        title.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        title.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        title.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
        title.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
        m_webTitleEdit->setText(title);
    }
    else
    {
        m_webTitleEdit->setText(QString());
        m_webTitleEdit->setPlaceholderText(tr("未找到标题，请手动输入"));
    }
}

void ResourceReferenceDialog::onAddWebPageClicked()
{
    const QString url = m_webUrlEdit->text().trimmed();
    if (url.isEmpty())
    {
        return;
    }
    QUrl qurl(url);
    if (!qurl.isValid())
    {
        return;
    }

    // 去重
    for (int i = 0; i < m_webList->count(); ++i)
    {
        if (m_webList->item(i)->data(RolePath).toString() == url)
        {
            return;
        }
    }

    const QString title = m_webTitleEdit->text().trimmed();
    const QString display = title.isEmpty() ? url : QStringLiteral("%1 — %2").arg(title, url);

    auto *item = new QListWidgetItem(m_webList);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked); // 新添加的网页默认勾选
    item->setText(display);
    item->setToolTip(url);
    item->setData(RoleIsReference, true);
    item->setData(RoleCategory, QStringLiteral("网页"));
    item->setData(RolePath, url);
    item->setData(RoleSize, static_cast<qlonglong>(0));
    item->setData(RoleExtra, title.isEmpty() ? QString() : tr("标题: %1").arg(title));

    // 清空输入框准备下一条
    m_webUrlEdit->clear();
    m_webTitleEdit->clear();
    m_webTitleEdit->setPlaceholderText(tr("可手动填写或点击「抓取标题」自动获取"));
}

QList<ResourceReference> ResourceReferenceDialog::selectedReferences() const
{
    QList<ResourceReference> result;

    // 实例资源树
    std::function<void(QTreeWidgetItem *)> walkTree;
    walkTree = [&](QTreeWidgetItem *item)
    {
        if (item->data(0, RoleIsReference).toBool() && item->checkState(0) == Qt::Checked)
        {
            ResourceReference ref;
            ref.category = item->data(0, RoleCategory).toString();
            ref.path = item->data(0, RolePath).toString();
            ref.size = item->data(0, RoleSize).toLongLong();
            ref.extra = item->data(0, RoleExtra).toString();
            ref.name = item->text(0);
            result.append(ref);
        }
        for (int i = 0; i < item->childCount(); ++i)
        {
            walkTree(item->child(i));
        }
    };
    for (int i = 0; i < m_instanceTree->topLevelItemCount(); ++i)
    {
        walkTree(m_instanceTree->topLevelItem(i));
    }

    // 本地资源树
    for (int i = 0; i < m_localTree->topLevelItemCount(); ++i)
    {
        walkTree(m_localTree->topLevelItem(i));
    }

    // 网络资源列表
    for (int i = 0; i < m_netResultList->count(); ++i)
    {
        QListWidgetItem *item = m_netResultList->item(i);
        if (item->checkState() == Qt::Checked)
        {
            ResourceReference ref;
            ref.category = item->data(RoleCategory).toString();
            ref.path = item->data(RolePath).toString();
            ref.size = item->data(RoleSize).toLongLong();
            ref.extra = item->data(RoleExtra).toString();
            ref.name = item->text().section(QStringLiteral("\n"), 0, 0);
            result.append(ref);
        }
    }

    // 网页列表
    for (int i = 0; i < m_webList->count(); ++i)
    {
        QListWidgetItem *item = m_webList->item(i);
        if (item->checkState() == Qt::Checked)
        {
            ResourceReference ref;
            ref.category = item->data(RoleCategory).toString();
            ref.path = item->data(RolePath).toString();
            ref.size = item->data(RoleSize).toLongLong();
            ref.extra = item->data(RoleExtra).toString();
            ref.name = item->text().section(QStringLiteral(" — "), 0, 0);
            if (ref.name == item->data(RolePath).toString())
            {
                ref.name = ref.path; // 无标题时用 URL 作名称
            }
            result.append(ref);
        }
    }

    return result;
}

QString ResourceReferenceDialog::resolveInstanceRoot(const QString &path)
{
    // BlockBox 实例路径即为资源根目录（版本隔离时为 versions/{ver}，
    // 非隔离时为 .minecraft）。资源子目录的实际定位由 resolveResourceDir
    // 依次尝试隔离与非隔离两种布局完成，此处不再做回退推断。
    return path;
}

QString ResourceReferenceDialog::formatSize(qint64 bytes) const
{
    if (bytes < 1024)
    {
        return QString::number(bytes) + QStringLiteral(" B");
    }
    if (bytes < 1024 * 1024)
    {
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    if (bytes < 1024 * 1024 * 1024)
    {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + QStringLiteral(" GB");
}
