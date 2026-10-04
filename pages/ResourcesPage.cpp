#include "ResourcesPage.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpacerItem>
#include <QLabel>
#include <QEvent>
#include <QMouseEvent>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "layouts/FlowLayout.h"
#include "components/OutlinedLabel.h"
#include "utils/IconHelper.h"
#include "utils/JarUtils.h"
#include "utils/ThemeManager.h"
#include "utils/FavoritesManager.h"
#include "utils/ThumbnailProvider.h"

// ── Static icon extraction helpers ────────────────────────────────────────

/**
 * @brief 归一化 zip 内部 entry 路径
 *        处理 mod 元数据中可能出现的 /prefix、./prefix、反斜杠等情况
 */
static QString normalizeZipEntryPath(const QString &raw)
{
    QString path = raw;
    path.replace('\\', '/');
    while (path.startsWith('/'))
        path.remove(0, 1);
    while (path.startsWith("./"))
        path.remove(0, 2);
    return path;
}

QPixmap ResourcesPage::extractIconFromZip(const QString &zipPath, const QString &entryName)
{
    QString normalized = normalizeZipEntryPath(entryName);
    QByteArray data;
    if (JarUtils::extractFromJarToMemory(zipPath, normalized, data)) {
        QPixmap pix;
        if (pix.loadFromData(data))
            return pix.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return QPixmap();
}

QPixmap ResourcesPage::extractIconFromDir(const QString &dirPath, const QString &iconName)
{
    QString iconFile = dirPath + "/" + iconName;
    QFileInfo fi(iconFile);
    if (fi.exists() && fi.isFile()) {
        QPixmap pix(iconFile);
        if (!pix.isNull())
            return pix.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return QPixmap();
}

QPixmap ResourcesPage::loadImageThumbnail(const QString &imagePath)
{
    return ThumbnailProvider::getThumbnail(imagePath, 40);
}

// ── Mod icon extraction (try metadata first, then generic fallbacks) ──────

QPixmap ResourcesPage::extractModIcon(const QString &jarPath)
{
    // 1. Quick try: pack.png (most common resource-pack style icon)
    QPixmap icon = extractIconFromZip(jarPath, "pack.png");
    if (!icon.isNull()) return icon;

    // 2. fabric.mod.json → "icon" field
    QByteArray data;
    if (JarUtils::extractFromJarToMemory(jarPath, "fabric.mod.json", data)) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QString iconPath = doc.object()["icon"].toString();
            if (!iconPath.isEmpty()) {
                icon = extractIconFromZip(jarPath, iconPath);
                if (!icon.isNull()) return icon;
            }
        }
    }

    // 3. quilt.mod.json → quilt_loader.metadata.icon
    if (JarUtils::extractFromJarToMemory(jarPath, "quilt.mod.json", data)) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QJsonObject loader = doc.object()["quilt_loader"].toObject();
            QJsonObject meta = loader["metadata"].toObject();
            QString iconPath = meta["icon"].toString();
            if (!iconPath.isEmpty()) {
                icon = extractIconFromZip(jarPath, iconPath);
                if (!icon.isNull()) return icon;
            }
        }
    }

    // 4. META-INF/mods.toml → logoFile (Forge 1.13+)
    // 5. META-INF/neoforge.mods.toml → logoFile (NeoForge)
    QStringList tomlPaths = {"META-INF/mods.toml", "META-INF/neoforge.mods.toml"};
    for (const auto &tomlPath : tomlPaths) {
        if (JarUtils::extractFromJarToMemory(jarPath, tomlPath, data)) {
            QString content = QString::fromUtf8(data);
            QRegularExpression re("logoFile\\s*=\\s*\"([^\"]+)\"");
            auto match = re.match(content);
            if (match.hasMatch()) {
                icon = extractIconFromZip(jarPath, match.captured(1));
                if (!icon.isNull()) return icon;
            }
        }
    }

    // 6. mcmod.info → [0].logoFile (Forge ≤1.12)
    if (JarUtils::extractFromJarToMemory(jarPath, "mcmod.info", data)) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull()) {
            QJsonArray arr;
            if (doc.isArray())
                arr = doc.array();
            else if (doc.isObject() && doc.object().contains("modList"))
                arr = doc.object()["modList"].toArray();
            if (!arr.isEmpty()) {
                QString iconPath = arr[0].toObject()["logoFile"].toString();
                if (!iconPath.isEmpty()) {
                    icon = extractIconFromZip(jarPath, iconPath);
                    if (!icon.isNull()) return icon;
                }
            }
        }
    }

    // 7. Generic fallback paths (HMCL-inspired)
    QStringList fallbacks = {"icon.png", "logo.png", "mod_logo.png", "logoFile.png"};
    for (const auto &entry : fallbacks) {
        icon = extractIconFromZip(jarPath, entry);
        if (!icon.isNull()) return icon;
    }

    return QPixmap();
}

// ── Resource pack icon (pack.png at root) ─────────────────────────────────

QPixmap ResourcesPage::extractResourcePackIcon(const QString &zipPath)
{
    return extractIconFromZip(zipPath, "pack.png");
}

// ── Save icon (icon.png in save directory) ────────────────────────────────

QPixmap ResourcesPage::extractSaveIcon(const QString &saveDirPath)
{
    QPixmap icon = extractIconFromDir(saveDirPath, "icon.png");
    if (!icon.isNull()) return icon;
    return extractIconFromDir(saveDirPath, "server-icon.png");
}

ResourcesPage::ResourcesPage(QWidget *parent)
    : QWidget(parent)
    , m_scrollContent(nullptr)
    , m_favoritesContainer(nullptr)
    , m_favoritesLayout(nullptr)
{
    initUI();

    // 监听收藏夹变化，动态刷新收藏夹卡片
    connect(FavoritesManager::instance(), &FavoritesManager::foldersChanged,
            this, &ResourcesPage::rebuildFavoriteCards);
}

ResourcesPage::~ResourcesPage()
{
}

// ── 事件过滤器：处理卡片点击 ───────────────────────────────────────────────

bool ResourcesPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        QWidget *card = qobject_cast<QWidget*>(watched);
        if (card) {
            bool ok = false;
            int childIndex = card->property("childIndex").toInt(&ok);
            if (ok) {
                emit navItemClicked(childIndex);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ── UI ────────────────────────────────────────────────────────────────────

void ResourcesPage::initUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_scrollContent = new QWidget(scrollArea);
    QVBoxLayout *mainLayout = new QVBoxLayout(m_scrollContent);
    mainLayout->setContentsMargins(28, 24, 28, 28);
    mainLayout->setSpacing(16);

    // ── Page Hero（对齐原型 page-hero） ──
    OutlinedLabel *pageTitle = new OutlinedLabel(tr("资源中心"), m_scrollContent);
    pageTitle->setObjectName("resourcesPageTitle");
    mainLayout->addWidget(pageTitle);

    QLabel *pageSubtitle = new QLabel(
        tr("从云端下载或本地导入你需要的全部游戏资源"),
        m_scrollContent);
    pageSubtitle->setObjectName("resourcesPageSubtitle");
    mainLayout->addWidget(pageSubtitle);

    mainLayout->addSpacing(12);

    // ── 新游戏分组 ──
    mainLayout->addWidget(createSectionHeader(tr("新游戏")));
    {
        QWidget *row = new QWidget(m_scrollContent);
        FlowLayout *flow = new FlowLayout(row, 0, 12, 12);

        flow->addWidget(createResourceCard(
            tr("安装新实例"), tr("选择 Minecraft 版本与加载器，安装全新游戏"),
            QColor("#10B981"), QColor("#047857"),
            ":/Images/Icons/nav_install.svg", 0));
        flow->addWidget(createResourceCard(
            tr("下载整合包"), tr("浏览 CurseForge / Modrinth 上的整合包"),
            QColor("#6366F1"), QColor("#4338CA"),
            ":/Images/Icons/nav_download.svg", 1));
        flow->addWidget(createResourceCard(
            tr("导入整合包"), tr("从本地 zip 文件导入整合包"),
            QColor("#F59E0B"), QColor("#D97706"),
            ":/Images/Icons/nav_import.svg", 2));

        mainLayout->addWidget(row);
    }

    // ── 社区资源分组 ──
    mainLayout->addWidget(createSectionHeader(tr("社区资源")));
    {
        QWidget *row = new QWidget(m_scrollContent);
        FlowLayout *flow = new FlowLayout(row, 0, 12, 12);

        flow->addWidget(createResourceCard(
            tr("模组"), tr("扩展游戏玩法的修改文件"),
            QColor("#EC4899"), QColor("#BE185D"),
            ":/Images/Icons/nav_mods.svg", 3));
        flow->addWidget(createResourceCard(
            tr("数据包"), tr("自定义游戏数据和机制"),
            QColor("#8B5CF6"), QColor("#6D28D9"),
            ":/Images/Icons/nav_datapacks.svg", 4));
        flow->addWidget(createResourceCard(
            tr("资源包"), tr("更改游戏材质和音效"),
            QColor("#06B6D4"), QColor("#0E7490"),
            ":/Images/Icons/nav_resourcepacks.svg", 5));
        flow->addWidget(createResourceCard(
            tr("光影包"), tr("提升画面光影与渲染效果"),
            QColor("#F59E0B"), QColor("#D97706"),
            ":/Images/Icons/nav_shaders.svg", 6));
        flow->addWidget(createResourceCard(
            tr("世界"), tr("导入或备份你的存档"),
            QColor("#84CC16"), QColor("#4D7C0F"),
            ":/Images/Icons/nav_worlds.svg", 7));

        mainLayout->addWidget(row);
    }

    // ── 收藏夹分组（标题右侧挂“新建收藏夹”按钮，对齐原型 ghost-btn） ──
    QPushButton *newFavBtn = new QPushButton(m_scrollContent);
    newFavBtn->setObjectName("resourcesNewFavBtn");
    newFavBtn->setText(tr("＋ 新建收藏夹"));
    newFavBtn->setCursor(Qt::PointingHandCursor);
    connect(newFavBtn, &QPushButton::clicked, this, [this]() {
        emit navItemClicked(8);   // 与左侧导航“新建收藏夹”行为一致
    });

    mainLayout->addWidget(createSectionHeader(tr("收藏夹"), newFavBtn));

    m_favoritesContainer = new QWidget(m_scrollContent);
    m_favoritesLayout = new QVBoxLayout(m_favoritesContainer);
    m_favoritesLayout->setContentsMargins(0, 0, 0, 0);
    m_favoritesLayout->setSpacing(12);
    mainLayout->addWidget(m_favoritesContainer);

    rebuildFavoriteCards();

    mainLayout->addStretch();

    scrollArea->setWidget(m_scrollContent);
    outerLayout->addWidget(scrollArea);
}

// ── 收藏夹卡片（动态） ────────────────────────────────────────────────────

void ResourcesPage::rebuildFavoriteCards()
{
    if (!m_favoritesLayout)
        return;

    // 清空旧卡片
    QLayoutItem *item;
    while ((item = m_favoritesLayout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();

    // 空态提示
    if (folders.isEmpty()) {
        QLabel *emptyLabel = new QLabel(
            tr("暂无收藏夹，点击右上角「新建收藏夹」创建分组"), m_favoritesContainer);
        emptyLabel->setObjectName("resourcesEmptyLabel");
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_favoritesLayout->addWidget(emptyLabel);
        return;
    }

    // 收藏夹卡片渐变色板（原型中不同收藏夹使用不同渐变）
    struct GradPair { const char *c1; const char *c2; };
    static const GradPair palette[] = {
        {"#F59E0B", "#D97706"},   // amber
        {"#6366F1", "#4338CA"},   // indigo
        {"#EC4899", "#BE185D"},   // pink
        {"#06B6D4", "#0E7490"},   // cyan
        {"#84CC16", "#4D7C0F"},   // lime
        {"#8B5CF6", "#6D28D9"},   // violet
        {"#F97316", "#C2410C"},   // orange
        {"#14B8A6", "#0F766E"},   // teal
    };
    const int paletteSize = int(sizeof(palette) / sizeof(palette[0]));

    // 收藏夹卡片（childIndex = 9 + idx），FlowLayout 自适应换行（对齐原型 auto-fill）
    QWidget *gridHolder = new QWidget(m_favoritesContainer);
    FlowLayout *flow = new FlowLayout(gridHolder, 0, 12, 12);
    for (int i = 0; i < folders.size(); ++i) {
        const FavoriteFolder &f = folders[i];
        const GradPair &g = palette[i % paletteSize];
        QString desc = tr("共 %1 项资源").arg(f.items.size());
        flow->addWidget(createResourceCard(
            f.name, desc, QColor(g.c1), QColor(g.c2),
            ":/Images/Icons/nav_folder.svg", 9 + i));
    }
    m_favoritesLayout->addWidget(gridHolder);
}

// ── 分区标题（渐变竖条 + 标题 + 可选右侧控件） ────────────────────────────

QWidget *ResourcesPage::createSectionHeader(const QString &title, QWidget *extraRight)
{
    QWidget *header = new QWidget(m_scrollContent);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QHBoxLayout *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // 渐变竖条（对齐原型 section-title::before）
    QLabel *bar = new QLabel(header);
    bar->setObjectName("resourcesSectionBar");
    bar->setFixedSize(3, 16);
    layout->addWidget(bar, 0, Qt::AlignVCenter);

    OutlinedLabel *titleLabel = new OutlinedLabel(title, header);
    titleLabel->setObjectName("resourcesSectionTitle");
    layout->addWidget(titleLabel, 0, Qt::AlignVCenter);

    layout->addStretch();

    if (extraRight)
        layout->addWidget(extraRight, 0, Qt::AlignVCenter);

    return header;
}

// ── 渐变图标块（52x52 渐变圆角 + 白色图标 + 柔和阴影） ───────────────────

QPixmap ResourcesPage::makeGradientIconPixmap(const QColor &c1, const QColor &c2,
                                               const QString &iconPath, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal radius = 12.0;

    // 底部柔和阴影（对齐原型 resource-icon box-shadow）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 30));
    p.drawRoundedRect(QRectF(0, 2.0, size, size), radius, radius);

    // 渐变背景（135° 渐变，对齐原型 linear-gradient(135deg, ...)）
    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0.0, c1);
    grad.setColorAt(1.0, c2);
    p.setBrush(grad);
    p.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

    // 白色图标居中（对齐原型白色 SVG 图标）
    QIcon icon = IconHelper::loadColoredIcon(iconPath, Qt::white, 26);
    const QPixmap iconPix = icon.pixmap(26, 26);
    if (!iconPix.isNull()) {
        int x = (size - iconPix.width()) / 2;
        int y = (size - iconPix.height()) / 2;
        p.drawPixmap(x, y, iconPix);
    }
    p.end();

    return pix;
}

// ── 横向资源卡片（对齐原型 resource-card） ───────────────────────────────

QWidget *ResourcesPage::createResourceCard(const QString &title, const QString &desc,
                                            const QColor &grad1, const QColor &grad2,
                                            const QString &iconPath, int childIndex)
{
    QWidget *card = new QWidget(m_scrollContent);
    card->setObjectName("resourcesCard");
    card->setFixedWidth(280);
    card->setFixedHeight(96);   // 与原型 grid 等高卡片一致
    card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("childIndex", childIndex);
    card->installEventFilter(this);

    QHBoxLayout *layout = new QHBoxLayout(card);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    // 渐变图标块
    QLabel *iconBlock = new QLabel(card);
    iconBlock->setObjectName("resourceIconBlock");
    iconBlock->setFixedSize(52, 52);
    iconBlock->setPixmap(makeGradientIconPixmap(grad1, grad2, iconPath, 52));
    layout->addWidget(iconBlock);

    // 中间文字区（标题 + 描述）
    QWidget *body = new QWidget(card);
    QVBoxLayout *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(4);

    QLabel *titleLabel = new QLabel(title, body);
    titleLabel->setObjectName("resourceCardTitle");
    bodyLayout->addWidget(titleLabel);

    QLabel *descLabel = new QLabel(desc, body);
    descLabel->setObjectName("resourceCardDesc");
    descLabel->setWordWrap(true);
    bodyLayout->addWidget(descLabel);

    bodyLayout->addStretch();
    layout->addWidget(body, 1);

    // 右侧箭头（hover 变主色，由 QSS 后代选择器控制）
    QLabel *arrowLabel = new QLabel(QString::fromUtf8("\u276F"), card);   // ❯
    arrowLabel->setObjectName("resourceArrow");
    layout->addWidget(arrowLabel, 0, Qt::AlignVCenter);

    return card;
}

// ── Instance path (保留接口，当前无本地资源逻辑) ──────────────────────────

void ResourcesPage::setInstancePath(const QString &path)
{
    m_instancePath = path;
}
