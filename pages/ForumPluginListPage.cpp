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
    m_listLayout->setSpacing(10);
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

    for (const ForumPluginInfo &plugin : visible)
    {
        const int threadId = m_threadIdByPlugin.value(plugin.id, 0);
        auto *row = ForumKit::makeCard(m_listHost, QStringLiteral("forumThreadRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(14, 12, 14, 12);
        rowLayout->setSpacing(12);

        auto *icon = new QLabel(row);
        icon->setFixedSize(44, 44);
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet(QStringLiteral(
            "background:@BG_MUTED@;border:1px solid @BORDER@;border-radius:9px;"));
        if (!plugin.icon.isEmpty())
        {
            ForumImageStore::instance()->requestImage(
                ForumAccountService::instance()->absoluteUrl(plugin.icon),
                [icon](const QImage &image) {
                    if (!image.isNull())
                        icon->setPixmap(QPixmap::fromImage(
                            image.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                });
        }
        else
        {
            icon->setText(QStringLiteral("🧩"));
        }
        rowLayout->addWidget(icon, 0, Qt::AlignTop);

        auto *textLayout = new QVBoxLayout();
        textLayout->setSpacing(3);
        auto *name = new QLabel(row);
        name->setObjectName(QStringLiteral("forumRowTitle"));
        name->setText(plugin.name
                          + (plugin.version.isEmpty()
                                 ? QString()
                                 : QStringLiteral("  v%1").arg(plugin.version)));
        textLayout->addWidget(name);
        auto *meta = new QLabel(row);
        meta->setObjectName(QStringLiteral("forumMuted"));
        meta->setWordWrap(true);
        const QStringList metaParts{
            plugin.author.isEmpty() ? tr("作者未知") : tr("作者 %1").arg(plugin.author),
            ForumKit::humanFileSize(plugin.fileSize),
            tr("%1 次下载").arg(plugin.downloads),
        };
        meta->setText(metaParts.join(QStringLiteral(" · ")));
        textLayout->addWidget(meta);
        if (!plugin.description.isEmpty())
        {
            auto *description = new QLabel(row);
            description->setObjectName(QStringLiteral("forumMuted"));
            description->setWordWrap(true);
            description->setText(plugin.description);
            textLayout->addWidget(description);
        }
        rowLayout->addLayout(textLayout, 1);

        auto *actionsLayout = new QVBoxLayout();
        actionsLayout->setSpacing(6);

        auto *installButton = new QPushButton(tr("安装"), row);
        installButton->setObjectName(QStringLiteral("forumPrimaryButton"));
        installButton->setCursor(Qt::PointingHandCursor);
        installButton->setFixedHeight(30);
        installButton->setMinimumWidth(96);
        connect(installButton, &QPushButton::clicked, this,
                [this, plugin]() { installPlugin(plugin); });
        actionsLayout->addWidget(installButton, 0, Qt::AlignTop);

        if (threadId > 0)
        {
            auto *openThreadButton = new QPushButton(tr("看原帖"), row);
            openThreadButton->setObjectName(QStringLiteral("forumGhostButton"));
            openThreadButton->setCursor(Qt::PointingHandCursor);
            openThreadButton->setFixedHeight(30);
            openThreadButton->setMinimumWidth(96);
            connect(openThreadButton, &QPushButton::clicked, this,
                    [this, threadId]() { emit threadOpenRequested(threadId); });
            actionsLayout->addWidget(openThreadButton, 0, Qt::AlignTop);
        }
        rowLayout->addLayout(actionsLayout, 0);

        m_listLayout->addWidget(row);
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
