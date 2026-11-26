/**
 * @file   CommunitySkinPage.cpp
 * @brief  资源页「皮肤」子页实现：方块皮肤站皮肤库
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "CommunitySkinPage.h"

#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "components/ForumKit.h"
#include "../layouts/FlowLayout.h"
#include "utils/ForumAccountService.h"
#include "utils/IconHelper.h"
#include "utils/SkinDownloader.h"
#include "utils/ThemeManager.h"

namespace {

/** 每页作品数（与皮肤站网页版一致） */
constexpr int kPageSize = 24;

/** 皮肤卡尺寸：头像 + 名字 + 一行小字 */
constexpr int kCardWidth = 128;
constexpr int kCardHeight = 186;
constexpr int kHeadSize = 88;

/** 名字太长时截断，避免卡片被撑开 */
QString displayName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QObject::tr("未命名皮肤");
    if (trimmed.length() > 16)
        return trimmed.left(15) + QStringLiteral("…");
    return trimmed;
}

} // namespace

CommunitySkinPage::CommunitySkinPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void CommunitySkinPage::buildUi()
{
    m_net = new QNetworkAccessManager(this);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // ── 顶部：搜索 / 排序 / 模型筛选（与其他在线资源列表页同款玻璃态卡片） ──
    auto *filterCard = new QWidget(this);
    filterCard->setObjectName(QStringLiteral("filterCard"));
    auto *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(10);

    auto *searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 0, 0, 0);
    m_searchEdit = new QLineEdit(filterCard);
    m_searchEdit->setObjectName(QStringLiteral("modSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("搜索皮肤站作品"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->addAction(IconHelper::loadColoredIcon(
                                QStringLiteral(":/Images/Icons/search.svg"),
                                QColor(ThemeManager::instance()->currentThemeColor()), 18),
                            QLineEdit::LeadingPosition);
    searchLayout->addWidget(m_searchEdit);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        m_query = m_searchEdit->text().trimmed();
        loadPage(1);
    });

    auto *searchButton = new QPushButton(tr("搜索"), filterCard);
    searchButton->setObjectName(QStringLiteral("modSearchBtn"));
    searchButton->setFixedHeight(36);
    searchButton->setFixedWidth(80);
    connect(searchButton, &QPushButton::clicked, this, [this]() {
        m_query = m_searchEdit->text().trimmed();
        loadPage(1);
    });
    searchLayout->addWidget(searchButton);
    cardLayout->addLayout(searchLayout);

    // 排序 / 模型筛选行：与「模组」「投影」等在线资源列表页同一个流式布局，
    // 窗口变窄时整行自动换行，不会把右边的筛选框挤到界外
    auto *filterLayout = new FlowLayout(nullptr, 0, 10, 8);

    auto *sortLabel = new QLabel(tr("排序方式:"), filterCard);
    sortLabel->setObjectName(QStringLiteral("filterLabel"));
    m_sortCombo = new QComboBox(filterCard);
    m_sortCombo->setObjectName(QStringLiteral("sortCombo"));
    m_sortCombo->setFixedHeight(32);
    m_sortCombo->setMinimumWidth(120);
    m_sortCombo->addItem(tr("最热"), QStringLiteral("hot"));
    m_sortCombo->addItem(tr("最新"), QStringLiteral("new"));
    filterLayout->addWidget(sortLabel);
    filterLayout->addWidget(m_sortCombo);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_sort = m_sortCombo->currentData().toString();
        loadPage(1);
    });

    auto *modelLabel = new QLabel(tr("模型:"), filterCard);
    modelLabel->setObjectName(QStringLiteral("filterLabel"));
    m_modelCombo = new QComboBox(filterCard);
    m_modelCombo->setObjectName(QStringLiteral("sortCombo"));
    m_modelCombo->setFixedHeight(32);
    m_modelCombo->setMinimumWidth(110);
    m_modelCombo->addItem(tr("全部模型"), QString());
    m_modelCombo->addItem(tr("Classic（经典）"), QStringLiteral("classic"));
    m_modelCombo->addItem(tr("Slim（纤细）"), QStringLiteral("slim"));
    filterLayout->addWidget(modelLabel);
    filterLayout->addWidget(m_modelCombo);
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_model = m_modelCombo->currentData().toString();
        loadPage(1);
    });

    cardLayout->addLayout(filterLayout);
    mainLayout->addWidget(filterCard);

    // ── 中部：皮肤卡片网格 ──
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(m_scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(14, 0, 14, 0);
    contentLayout->setSpacing(8);

    m_statusLabel = ForumKit::makeMuted(content);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    contentLayout->addWidget(m_statusLabel);

    m_gridHost = content;
    m_gridLayout = new QGridLayout();
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setSpacing(10);
    m_gridLayout->setAlignment(Qt::AlignTop);
    contentLayout->addLayout(m_gridLayout);

    contentLayout->addStretch(1);

    m_scroll->setWidget(content);
    mainLayout->addWidget(m_scroll, 1);

    // ── 底部：分页栏（与其他在线资源列表页同款：首页 / 上一页 / 页码 / 下一页 / 末页） ──
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

void CommunitySkinPage::openPage()
{
    loadPage(1);
}

void CommunitySkinPage::applyPageFilter(const QString &query)
{
    m_searchEdit->setText(query);
    m_query = query.trimmed();
    loadPage(1);
}

bool CommunitySkinPage::eventFilter(QObject *watched, QEvent *event)
{
    // 点卡片打开整页详情（与 ResourcesPage 卡片的点击处理同款）
    if (event->type() == QEvent::MouseButtonRelease)
    {
        if (auto *card = qobject_cast<QWidget *>(watched))
        {
            const QVariant id = card->property("skinId");
            if (id.isValid())
            {
                for (const SkinLibraryItem &item : m_skins)
                {
                    if (item.id == id.toInt())
                    {
                        emit skinDetailRequested(item);
                        break;
                    }
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CommunitySkinPage::showStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    if (error)
        m_statusLabel->setStyleSheet(
            QStringLiteral("color:#c62828;background:#fdecea;border:1px solid #f3c5c2;"
                           "border-radius:9px;padding:6px 10px;"));
    else
        m_statusLabel->setStyleSheet(QString());
}

void CommunitySkinPage::loadPage(int page)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("sort"), m_sort);
    query.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
    query.addQueryItem(QStringLiteral("page_size"), QString::number(kPageSize));
    if (!m_query.isEmpty())
        query.addQueryItem(QStringLiteral("q"), m_query);
    if (!m_model.isEmpty())
        query.addQueryItem(QStringLiteral("model"), m_model);

    showStatus(tr("正在加载皮肤库…"));
    getJson(QStringLiteral("/api/library?") + query.toString(QUrl::FullyEncoded),
            [this](bool ok, int httpStatus, const QJsonObject &root) {
                if (!ok)
                {
                    if (httpStatus == 401 || httpStatus == 403)
                    {
                        showStatus(tr("皮肤站需要登录后才能浏览，请先登录论坛账号。"), true);
                        emit loginRequested();
                    }
                    else
                    {
                        showStatus(tr("连不上皮肤站，检查网络后再试。"), true);
                    }
                    return;
                }

                m_skins.clear();
                const int myId = ForumAccountService::instance()->user().id;
                for (const QJsonValue &value : root.value(QStringLiteral("items")).toArray())
                {
                    const QJsonObject object = value.toObject();
                    SkinLibraryItem item;
                    item.id = object.value(QStringLiteral("id")).toInt();
                    item.kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("skin"));
                    item.name = object.value(QStringLiteral("name")).toString();
                    item.model = object.value(QStringLiteral("model")).toString(QStringLiteral("classic"));
                    item.url = object.value(QStringLiteral("url")).toString();
                    item.likes = object.value(QStringLiteral("likes")).toInt();
                    item.downloads = object.value(QStringLiteral("downloads")).toInt();
                    const QJsonObject owner = object.value(QStringLiteral("owner")).toObject();
                    item.ownerName = owner.value(QStringLiteral("player_name")).toString();
                    if (item.ownerName.isEmpty())
                        item.ownerName = owner.value(QStringLiteral("username")).toString();
                    item.mine = myId > 0 && owner.value(QStringLiteral("id")).toInt() == myId;
                    // 本页只做皮肤；皮肤库若混入披风就跳过
                    if (item.id <= 0 || item.kind != QLatin1String("skin"))
                        continue;
                    m_skins.append(item);
                }
                m_page = qMax(1, root.value(QStringLiteral("page")).toInt());
                m_totalPages = qMax(1, root.value(QStringLiteral("pages")).toInt());
                m_total = root.value(QStringLiteral("total")).toInt();
                renderLoaded();
            });
}

void CommunitySkinPage::renderLoaded()
{
    // 清空旧卡片
    QLayoutItem *taken = nullptr;
    while ((taken = m_gridLayout->takeAt(0)) != nullptr)
    {
        if (QWidget *widget = taken->widget())
            widget->deleteLater();
        delete taken;
    }

    if (m_skins.isEmpty())
    {
        showStatus(m_query.isEmpty() ? tr("皮肤库还是空的，换个条件再试试。")
                                     : tr("没有找到相关作品，换个关键词或筛选条件再试试。"), true);
        updatePager();
        return;
    }

    // 按滚动区宽度估算列数（4~10 列之间自适应）
    const int columns = qBound(4, qMax(1, (m_scroll->viewport()->width() - 28) / (kCardWidth + 10)), 10);
    int index = 0;
    for (const SkinLibraryItem &source : m_skins)
    {
        // 复制一份 item，卡片点击时按 id 找回
        const SkinLibraryItem item = source;
        auto *card = new QWidget(m_gridHost);
        card->setObjectName(QStringLiteral("modCardListItem"));
        card->setFixedSize(kCardWidth, kCardHeight);
        card->setCursor(Qt::PointingHandCursor);
        card->setProperty("skinId", item.id);
        card->installEventFilter(this);

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 8, 8, 8);
        cardLayout->setSpacing(4);

        auto *icon = new QLabel(card);
        icon->setAlignment(Qt::AlignCenter);
        icon->setFixedSize(kHeadSize, kHeadSize);
        icon->setText(tr("加载中…"));
        cardLayout->addWidget(icon, 0, Qt::AlignHCenter);

        auto *name = new QLabel(displayName(item.name), card);
        name->setObjectName(QStringLiteral("modCardName"));
        name->setAlignment(Qt::AlignHCenter);
        cardLayout->addWidget(name);

        auto *meta = new QLabel(item.ownerName.isEmpty()
                                    ? tr("♥ %1").arg(item.likes)
                                    : tr("%1 · ♥ %2").arg(item.ownerName).arg(item.likes),
                                card);
        meta->setObjectName(QStringLiteral("forumMuted"));
        meta->setAlignment(Qt::AlignHCenter);
        meta->setWordWrap(false);
        cardLayout->addWidget(meta, 0, Qt::AlignHCenter);

        // 头像异步加载（带缓存）
        const int id = item.id;
        const QString url = item.url;
        fetchPng(url, [this, id, icon](bool ok, const QByteArray &data) {
            if (!ok)
            {
                icon->setText(tr("加载失败"));
                return;
            }
            QImage image;
            image.loadFromData(data);
            QImage head = SkinDownloader::cropHeadPortrait(image, kHeadSize);
            if (!head.isNull())
                icon->setPixmap(QPixmap::fromImage(head));
        });

        m_gridLayout->addWidget(card, index / columns, index % columns);
        ++index;
    }

    showStatus(tr("皮肤库共 %1 件作品").arg(m_total));
    updatePager();
}

void CommunitySkinPage::updatePager()
{
    // 页码文案与「模组」「投影」等在线资源列表页一致
    m_pageLabel->setText(tr("第 %1/%2 页").arg(m_page).arg(m_totalPages));
    m_firstButton->setEnabled(m_page > 1);
    m_prevButton->setEnabled(m_page > 1);
    m_nextButton->setEnabled(m_page < m_totalPages);
    m_lastButton->setEnabled(m_page < m_totalPages);
    // 只有一页时不占底部位置，和其他在线资源列表页一样
    m_paginationBar->setVisible(m_totalPages > 1);
}

// ── 网络小工具（与皮肤站整页 / 作品详情页同款逻辑） ────────────────────

QString CommunitySkinPage::skinSiteBaseUrl() const
{
    const QString fromEnv = qEnvironmentVariable("BLOCKBOX_SKIN_SITE_URL");
    if (!fromEnv.trimmed().isEmpty())
        return fromEnv.trimmed().remove(QLatin1Char('/'));
    return QStringLiteral("https://skin.mcblockbox.top");
}

void CommunitySkinPage::sendRequest(const QString &path, const QString &method,
                                    const QByteArray &contentType, const QByteArray &body, JsonCb done)
{
    const QString base = skinSiteBaseUrl();
    QUrl url(path.startsWith(QStringLiteral("http")) ? path : base + path);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (ForumAccountService::instance()->hasSession())
        request.setRawHeader("Authorization",
                             "Bearer " + ForumAccountService::instance()->token().toUtf8());

    QNetworkReply *reply = nullptr;
    if (method == QLatin1String("POST"))
        reply = m_net->post(request, body);
    else
        reply = m_net->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        reply->deleteLater();

        if (status <= 0)
        {
            if (done) done(false, status, QJsonObject());
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (status >= 400)
        {
            QJsonObject errorRoot;
            if (doc.isObject())
                errorRoot = doc.object();
            if (done) done(false, status, errorRoot);
            return;
        }
        // PNG 等非 JSON 回包：当成成功但内容为空
        QJsonObject root;
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
            root = doc.object();
        if (done) done(true, status, root);
    });
}

void CommunitySkinPage::getJson(const QString &path, JsonCb done)
{
    sendRequest(path, QStringLiteral("GET"), QByteArray(), QByteArray(), std::move(done));
}

void CommunitySkinPage::fetchPng(const QString &url, const std::function<void(bool, const QByteArray &)> &done)
{
    if (url.isEmpty())
    {
        if (done) done(false, QByteArray());
        return;
    }
    if (m_pngCache.contains(url))
    {
        if (done) done(true, m_pngCache.value(url));
        return;
    }

    // 站内相对地址（/textures/xxx.png）拼皮肤站域名，完整地址直接用
    const QString base = skinSiteBaseUrl();
    QNetworkRequest request(QUrl(url.startsWith(QStringLiteral("http")) ? url : base + url));
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, done]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        reply->deleteLater();
        const bool ok = status >= 200 && status < 300 && !raw.isEmpty();
        if (ok)
            m_pngCache.insert(url, raw);
        if (done) done(ok, raw);
    });
}
