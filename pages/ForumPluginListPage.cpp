/**
 * @file   ForumPluginListPage.cpp
 * @brief  资源页「插件」子页实现
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "ForumPluginListPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFileDialog>
#include "components/NotificationManager.h"
#include "components/ForumKit.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/IconHelper.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"
#include "utils/plugin/PluginManager.h"
#include "utils/plugin/PluginSafetyGuard.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

/** 插件包临时下载目录（与帖子详情页共用同一处） */
QString pluginTempDir()
{
    const QString dir = QDir::tempPath() + QStringLiteral("/BlockBox/forum-plugin");
    QDir().mkpath(dir);
    return dir;
}

/** 圆角方形插件图标：远程图源按 modCardIcon 的圆角半径裁切，避免直角压住卡片 */
QPixmap roundedIconPixmap(const QImage &image, int size, qreal radius)
{
    if (image.isNull())
        return QPixmap();

    // 等比放大铺满后居中裁出正方形，再按圆角路径裁剪
    const QPixmap scaled = QPixmap::fromImage(
        image.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    const int cropX = qMax(0, (scaled.width() - size) / 2);
    const int cropY = qMax(0, (scaled.height() - size) / 2);

    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0, scaled, cropX, cropY, size, size);
    painter.end();
    return canvas;
}

} // namespace

ForumPluginListPage::ForumPluginListPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void ForumPluginListPage::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // ── 顶部：搜索条（与其他在线资源列表页同款玻璃态卡片） ──
    auto *filterCard = new QWidget(this);
    filterCard->setObjectName(QStringLiteral("filterCard"));
    auto *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(10);

    auto *searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 0, 0, 0);
    m_searchEdit = new QLineEdit(filterCard);
    m_searchEdit->setObjectName(QStringLiteral("modSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("搜索插件名"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->addAction(IconHelper::loadColoredIcon(
                                QStringLiteral(":/Images/Icons/search.svg"),
                                QColor(ThemeManager::instance()->currentThemeColor()), 18),
                            QLineEdit::LeadingPosition);
    searchLayout->addWidget(m_searchEdit);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_keyword = text.trimmed();
        loadPage(m_page);   // 关键字变化就地重渲染（列表数据已在手）
    });
    auto *searchButton = new QPushButton(tr("搜索"), filterCard);
    searchButton->setObjectName(QStringLiteral("modSearchBtn"));
    searchButton->setFixedHeight(36);
    searchButton->setFixedWidth(80);
    connect(searchButton, &QPushButton::clicked, this, [this]() { loadPage(m_page); });
    searchLayout->addWidget(searchButton);
    cardLayout->addLayout(searchLayout);

    auto *intro = new QLabel(tr("论坛里发布、装到启动器就能用的插件，点「安装」即可下载安装"), filterCard);
    intro->setObjectName(QStringLiteral("searchHintLabel"));
    intro->setWordWrap(true);
    cardLayout->addWidget(intro);
    mainLayout->addWidget(filterCard);

    // ── 中部：插件列表 ──
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(m_scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(8);

    // ── 状态行 ──
    m_statusLabel = ForumKit::makeMuted(content);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // ── 列表 ──
    m_listHost = content;
    m_listLayout = new QVBoxLayout();
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);
    layout->addLayout(m_listLayout);

    layout->addStretch(1);

    m_scroll->setWidget(content);
    mainLayout->addWidget(m_scroll, 1);

    // ── 底部：分页栏（与其他在线资源列表页同款） ──
    m_paginationBar = new QWidget(this);
    m_paginationBar->setObjectName(QStringLiteral("modPaginationBar"));
    auto *paginationLayout = new QHBoxLayout(m_paginationBar);
    paginationLayout->setContentsMargins(14, 8, 14, 8);
    paginationLayout->setSpacing(8);
    paginationLayout->addStretch(1);

    m_firstButton = new QPushButton(tr("首页"), m_paginationBar);
    m_firstButton->setObjectName(QStringLiteral("modPageBtn"));
    m_firstButton->setFixedHeight(32);
    m_firstButton->setEnabled(false);
    connect(m_firstButton, &QPushButton::clicked, this, [this]() { loadPage(1); });
    paginationLayout->addWidget(m_firstButton);

    m_prevButton = new QPushButton(tr("上一页"), m_paginationBar);
    m_prevButton->setObjectName(QStringLiteral("modPageBtn"));
    m_prevButton->setFixedHeight(32);
    m_prevButton->setEnabled(false);
    connect(m_prevButton, &QPushButton::clicked, this, [this]() { loadPage(m_page - 1); });
    paginationLayout->addWidget(m_prevButton);

    m_pageLabel = new QLabel(tr("第 1 页"), m_paginationBar);
    m_pageLabel->setObjectName(QStringLiteral("modPageLabel"));
    m_pageLabel->setFixedHeight(32);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    paginationLayout->addWidget(m_pageLabel);

    m_nextButton = new QPushButton(tr("下一页"), m_paginationBar);
    m_nextButton->setObjectName(QStringLiteral("modPageBtn"));
    m_nextButton->setFixedHeight(32);
    m_nextButton->setEnabled(false);
    connect(m_nextButton, &QPushButton::clicked, this, [this]() { loadPage(m_page + 1); });
    paginationLayout->addWidget(m_nextButton);

    m_lastButton = new QPushButton(tr("末页"), m_paginationBar);
    m_lastButton->setObjectName(QStringLiteral("modPageBtn"));
    m_lastButton->setFixedHeight(32);
    m_lastButton->setEnabled(false);
    connect(m_lastButton, &QPushButton::clicked, this, [this]() { loadPage(m_totalPages); });
    paginationLayout->addWidget(m_lastButton);

    paginationLayout->addStretch(1);
    m_paginationBar->hide();
    mainLayout->addWidget(m_paginationBar);
}

void ForumPluginListPage::openPage()
{
    loadPage(1);
}

void ForumPluginListPage::clearList()
{
    ForumKit::clearLayout(m_listLayout);
}

void ForumPluginListPage::loadPage(int page)
{
    if (page < 1)
        page = 1;

    m_statusLabel->setText(tr("正在加载插件列表…"));
    m_statusLabel->show();
    m_paginationBar->hide();

    const QString path = QStringLiteral("/api/forum-plugins?page=%1").arg(page);
    ForumAccountService::instance()->getJson(
        path,
        [this](const QJsonObject &root) {
            m_plugins.clear();
            const QJsonArray array = root.value(QStringLiteral("plugins")).toArray();
            for (const QJsonValue &value : array)
            {
                const QJsonObject object = value.toObject();
                ForumPluginInfo plugin;
                plugin.id = object.value(QStringLiteral("id")).toInt();
                plugin.uploadId = object.value(QStringLiteral("upload_id")).toInt();
                plugin.pluginId = object.value(QStringLiteral("plugin_id")).toString();
                plugin.name = object.value(QStringLiteral("name")).toString();
                plugin.version = object.value(QStringLiteral("version")).toString();
                plugin.author = object.value(QStringLiteral("author")).toString();
                plugin.description = object.value(QStringLiteral("description")).toString();
                plugin.icon = object.value(QStringLiteral("icon")).toString();
                plugin.fileSize = static_cast<qint64>(object.value(QStringLiteral("file_size")).toDouble());
                plugin.downloads = object.value(QStringLiteral("downloads")).toInt();
                // threadId 借 ForumPluginInfo 没有的字段：存到 id 派生的映射里
                const int threadId = object.value(QStringLiteral("thread_id")).toInt();
                if (!plugin.isValid() || threadId <= 0)
                    continue;
                m_plugins.append(plugin);
                m_threadIdByPlugin.insert(plugin.id, threadId);
            }

            const ForumPagination pagination =
                [root]() {
                    ForumPagination p;
                    const QJsonObject object = root.value(QStringLiteral("pagination")).toObject();
                    p.page = object.value(QStringLiteral("page")).toInt();
                    p.perPage = object.value(QStringLiteral("per_page")).toInt();
                    p.total = object.value(QStringLiteral("total")).toInt();
                    p.pages = object.value(QStringLiteral("pages")).toInt();
                    if (p.page <= 0) p.page = 1;
                    if (p.pages <= 0) p.pages = 1;
                    return p;
                }();
            m_page = pagination.page;
            m_totalPages = pagination.pages;

            renderLoaded();
        },
        [this](const QString &message, int httpStatus) {
            clearList();
            m_statusLabel->setText(message);
            m_statusLabel->show();
            m_paginationBar->hide();
            if (httpStatus == 401)
                emit loginRequested();
        });
}

void ForumPluginListPage::renderLoaded()
{
    clearList();

    const QString keyword = m_keyword;
    QVector<ForumPluginInfo> visible;
    for (const ForumPluginInfo &plugin : m_plugins)
    {
        if (keyword.isEmpty()
            || plugin.name.contains(keyword, Qt::CaseInsensitive)
            || plugin.pluginId.contains(keyword, Qt::CaseInsensitive)
            || plugin.author.contains(keyword, Qt::CaseInsensitive)
            || plugin.description.contains(keyword, Qt::CaseInsensitive))
        {
            visible.append(plugin);
        }
    }

    if (visible.isEmpty())
    {
        m_statusLabel->setText(keyword.isEmpty()
                                   ? tr("还没有插件，在论坛发一个插件帖就会出现在这里")
                                   : tr("没有匹配「%1」的插件").arg(keyword));
        m_statusLabel->show();
        m_paginationBar->hide();
        return;
    }
    m_statusLabel->hide();

    const QColor themeColor(ThemeManager::instance()->currentThemeColor());

    for (const ForumPluginInfo &plugin : visible)
    {
        const int threadId = m_threadIdByPlugin.value(plugin.id, 0);

        // 卡片外观与「下载模组」「插件管理」等页的列表卡片同款（modCardListItem）
        auto *card = new QWidget(m_listHost);
        card->setObjectName(QStringLiteral("modCardListItem"));
        card->setProperty("cardRole", "container");
        card->setFixedHeight(76);
        card->setCursor(Qt::PointingHandCursor);

        auto *rowLayout = new QHBoxLayout(card);
        rowLayout->setContentsMargins(14, 12, 14, 12);
        rowLayout->setSpacing(14);

        // 图标：有封面用封面（圆角裁切），没有就用名称首字母占位
        auto *icon = new QLabel(card);
        icon->setObjectName(QStringLiteral("modCardIcon"));
        icon->setProperty("cardRole", "icon");
        icon->setFixedSize(48, 48);
        icon->setAlignment(Qt::AlignCenter);
        const QString trimmedName = plugin.name.trimmed();
        icon->setText(trimmedName.isEmpty() ? QStringLiteral("?")
                                            : trimmedName.left(1).toUpper());
        if (!plugin.icon.isEmpty())
        {
            ForumImageStore::instance()->requestImage(
                ForumAccountService::instance()->absoluteUrl(plugin.icon),
                [icon](const QImage &image) {
                    const QPixmap rounded = roundedIconPixmap(image, 44, 12);
                    if (!rounded.isNull())
                        icon->setPixmap(rounded);
                });
        }
        rowLayout->addWidget(icon);

        // 名称 + 芯片行（版本 / 作者 / 大小 / 下载次数）
        auto *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(6);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        auto *name = new QLabel(plugin.name, card);
        name->setObjectName(QStringLiteral("modCardName"));
        name->setProperty("cardRole", "name");
        if (!plugin.description.isEmpty())
            name->setToolTip(plugin.description);
        infoLayout->addWidget(name);

        auto *chipsWidget = new QWidget(card);
        auto *chipsLayout = new QHBoxLayout(chipsWidget);
        chipsLayout->setContentsMargins(0, 0, 0, 0);
        chipsLayout->setSpacing(6);
        auto addChip = [&](const QString &text) {
            if (text.isEmpty())
                return;
            auto *chip = new QLabel(text, chipsWidget);
            chip->setObjectName(QStringLiteral("modChip"));
            chip->setProperty("cardRole", "chip");
            chipsLayout->addWidget(chip);
        };
        if (!plugin.version.isEmpty())
            addChip(tr("v%1").arg(plugin.version));
        addChip(plugin.author.isEmpty() ? tr("作者未知") : plugin.author);
        addChip(ForumKit::humanFileSize(plugin.fileSize));
        addChip(tr("%1 次下载").arg(plugin.downloads));
        chipsLayout->addStretch();
        infoLayout->addWidget(chipsWidget);
        rowLayout->addLayout(infoLayout, 1);

        // 右侧操作区：图标按钮，与其它列表页一致
        auto *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(4);
        btnLayout->setContentsMargins(0, 0, 0, 0);

        auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton * {
            auto *btn = new QPushButton(card);
            btn->setObjectName(QStringLiteral("modCardActionBtn"));
            btn->setProperty("cardRole", "actionBtn");
            btn->setFixedSize(32, 32);
            btn->setToolTip(tip);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
            btn->setIconSize(QSize(18, 18));
            return btn;
        };

        QPushButton *installButton = createBtn(QStringLiteral(":/Images/Icons/install.svg"),
                                               tr("安装"));
        connect(installButton, &QPushButton::clicked, this,
                [this, plugin]() { installPlugin(plugin); });
        btnLayout->addWidget(installButton);

        if (threadId > 0)
        {
            QPushButton *openThreadButton = createBtn(
                QStringLiteral(":/Images/Icons/external_link.svg"), tr("看原帖"));
            connect(openThreadButton, &QPushButton::clicked, this,
                    [this, threadId]() { emit threadOpenRequested(threadId); });
            btnLayout->addWidget(openThreadButton);
        }
        rowLayout->addLayout(btnLayout);

        m_listLayout->addWidget(card);
    }

    m_pageLabel->setText(tr("第 %1/%2 页").arg(m_page).arg(m_totalPages));
    m_paginationBar->show();
    m_firstButton->setEnabled(m_page > 1);
    m_prevButton->setEnabled(m_page > 1);
    m_nextButton->setEnabled(m_page < m_totalPages);
    m_lastButton->setEnabled(m_page < m_totalPages);
}

void ForumPluginListPage::installPlugin(const ForumPluginInfo &plugin)
{
    m_statusLabel->setText(tr("正在下载 %1 …").arg(plugin.name));
    m_statusLabel->show();

    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/threads/%1/plugin/download").arg(m_threadIdByPlugin.value(plugin.id)),
        [this, plugin](const QJsonObject &root) {
            // 下载地址：CDN 域名或论坛自供的绝对地址
            QString url = root.value(QStringLiteral("url")).toString();
            if (url.startsWith(QLatin1String("/uploads/")))
                url = ForumAccountService::instance()->absoluteUrl(url);
            const QString filename = root.value(QStringLiteral("filename")).toString();

            auto *manager = new QNetworkAccessManager(this);
            QNetworkRequest request{QUrl(url)};
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
            request.setTransferTimeout(120000);
            auto *reply = manager->get(request);
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, manager, plugin, filename]() {
                        reply->deleteLater();
                        manager->deleteLater();
                        const QByteArray data = reply->readAll();
                        if (reply->error() != QNetworkReply::NoError || data.isEmpty())
                        {
                            m_statusLabel->setText(tr("插件包下载失败，请检查网络后再试"));
                            return;
                        }
                        QString target = pluginTempDir() + QLatin1Char('/') + filename;
                        if (!target.endsWith(QLatin1String(".BlockBox"), Qt::CaseInsensitive))
                            target += QStringLiteral(".BlockBox");
                        QFile file(target);
                        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
                        {
                            m_statusLabel->setText(tr("插件包写不进临时目录"));
                            return;
                        }
                        file.close();

                        PluginInfo info = PluginManager::instance()->inspectPluginFile(target);
                        if (!info.loaded)
                        {
                            m_statusLabel->setText(tr("下载的文件不是有效插件包"));
                            return;
                        }
                        const QString scripts = PluginManager::instance()->collectScriptsContent(target);
                        if (!PluginSafetyGuard::confirmBeforeInstall(
                                window(), info.name, info.author, info.version,
                                info.permissions, PluginSafetyGuard::RiskMedium, QString(), scripts))
                        {
                            m_statusLabel->hide();
                            return;
                        }
                        QString error;
                        if (!PluginManager::instance()->importPlugin(target, &error))
                        {
                            m_statusLabel->setText(tr("插件安装失败：%1").arg(error));
                            return;
                        }
                        PluginManager::instance()->refresh();
                        emit pluginInstalled();
                        m_statusLabel->setText(tr("%1 安装成功！到「插件」列表里查看").arg(info.name));
                        m_statusLabel->show();
                    });
        },
        [this](const QString &message, int httpStatus) {
            m_statusLabel->setText(message);
            m_statusLabel->show();
            if (httpStatus == 401)
                emit loginRequested();
        });
}
