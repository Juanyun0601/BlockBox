/**
 * @file   ForumSchematicListPage.cpp
 * @brief  资源页「投影」子页实现
 * @author BlockBox Team
 * @date   2026-11-20
 */

#include "ForumSchematicListPage.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "components/ForumKit.h"
#include "components/ResourceFilterDialog.h"
#include "../layouts/FlowLayout.h"
#include "utils/IconHelper.h"
#include "utils/Schematic/SchematicDownloadTarget.h"
#include "utils/ThemeManager.h"

namespace {

/** 投影分享板块的 slug（与论坛 schema 里的板块标识一致） */
const QString kSchematicBoardSlug = QStringLiteral("schematic-share");

/** 投影卡标题：用帖子标题（发布时玩家自己填的那个），没填标题才退回投影名 / 文件名 */
QString schematicCardTitle(const ForumThreadInfo &thread)
{
    QString name = thread.title.trimmed();
    if (name.isEmpty())
        name = thread.schematic.name.trimmed();
    if (name.isEmpty())
        name = QFileInfo(thread.schematic.fileName).completeBaseName().trimmed();
    if (name.isEmpty())
        name = QObject::tr("未命名投影");
    return name;
}

/** 卡片悬浮提示：补上完整标题与投影名 / 文件名，标题被截断或认不出是哪份投影时有个交代 */
QString schematicCardTip(const ForumThreadInfo &thread)
{
    QStringList bits;
    const QString title = thread.title.trimmed();
    if (!title.isEmpty())
        bits << QObject::tr("标题：%1").arg(title);
    const QString projectionName = thread.schematic.name.trimmed();
    if (!projectionName.isEmpty())
        bits << QObject::tr("投影名：%1").arg(projectionName);
    const QString fileName = QFileInfo(thread.schematic.fileName).fileName().trimmed();
    if (!fileName.isEmpty())
        bits << QObject::tr("文件名：%1").arg(fileName);
    return bits.join(QStringLiteral("\n"));
}

} // namespace

ForumSchematicListPage::ForumSchematicListPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void ForumSchematicListPage::buildUi()
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
    m_searchEdit->setPlaceholderText(tr("搜索投影标题"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->addAction(IconHelper::loadColoredIcon(
                                QStringLiteral(":/Images/Icons/search.svg"),
                                QColor(ThemeManager::instance()->currentThemeColor()), 18),
                            QLineEdit::LeadingPosition);
    searchLayout->addWidget(m_searchEdit);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_keyword = text.trimmed();
        // 列表已经在手：关键字变化就地重渲染，不再回头请求服务端
        if (!m_threads.isEmpty())
            renderLoaded();
    });
    auto *searchButton = new QPushButton(tr("搜索"), filterCard);
    searchButton->setObjectName(QStringLiteral("modSearchBtn"));
    searchButton->setFixedHeight(36);
    searchButton->setFixedWidth(80);
    connect(searchButton, &QPushButton::clicked, this, [this]() {
        m_keyword = m_searchEdit->text().trimmed();
        if (!m_threads.isEmpty())
            renderLoaded();
        else
            loadPage(m_page);
    });
    searchLayout->addWidget(searchButton);
    cardLayout->addLayout(searchLayout);

    // ── 排序 / 筛选行（控件与「模组」「资源包」等在线资源列表页同款） ──
    auto *filterLayout = new FlowLayout(nullptr, 0, 10, 8);

    auto *sortLabel = new QLabel(tr("排序方式:"), filterCard);
    sortLabel->setObjectName(QStringLiteral("filterLabel"));
    m_sortCombo = new QComboBox(filterCard);
    m_sortCombo->setObjectName(QStringLiteral("sortCombo"));
    m_sortCombo->setFixedHeight(32);
    m_sortCombo->setMinimumWidth(120);
    m_sortCombo->addItem(tr("最新发布"), QStringLiteral("new"));
    m_sortCombo->addItem(tr("最新回复"), QStringLiteral("latest"));
    m_sortCombo->addItem(tr("最热"), QStringLiteral("hot"));
    filterLayout->addWidget(sortLabel);
    filterLayout->addWidget(m_sortCombo);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                const QString sort = m_sortCombo->currentData().toString();
                if (sort == m_sort)
                    return;
                m_sort = sort;
                loadPage(1);
            });

    m_filterBtn = new QPushButton(tr("筛选"), filterCard);
    m_filterBtn->setObjectName(QStringLiteral("contentFilterBtn"));
    m_filterBtn->setFixedHeight(32);
    m_filterBtn->setCursor(Qt::PointingHandCursor);
    m_filterBtn->setToolTip(tr("按投影文件格式筛选"));
    updateFilterButton();
    filterLayout->addWidget(m_filterBtn);
    connect(m_filterBtn, &QPushButton::clicked, this,
            &ForumSchematicListPage::onFilterClicked);

    cardLayout->addLayout(filterLayout);
    mainLayout->addWidget(filterCard);

    // ── 中部：投影列表 ──
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

void ForumSchematicListPage::setInstancePath(const QString &path)
{
    m_instancePath = path;
}

void ForumSchematicListPage::applyPageFilter(const QString &query)
{
    const QString keyword = query.trimmed();
    if (m_searchEdit)
    {
        m_searchEdit->blockSignals(true);
        m_searchEdit->setText(keyword);
        m_searchEdit->blockSignals(false);
    }
    m_keyword = keyword;
    if (!m_threads.isEmpty())
        renderLoaded();
}

void ForumSchematicListPage::openPage()
{
    loadPage(1);
}

void ForumSchematicListPage::clearList()
{
    ForumKit::clearLayout(m_listLayout);
}

void ForumSchematicListPage::loadPage(int page)
{
    if (page < 1)
        page = 1;

    m_statusLabel->setText(tr("正在加载投影列表…"));
    m_statusLabel->show();
    m_paginationBar->hide();

    ForumContentService::instance()->fetchBoardThreads(
        kSchematicBoardSlug, m_sort, page,
        [this](const ForumBoard &, const QVector<ForumThreadInfo> &threads,
               const ForumPagination &pagination) {
            m_threads.clear();
            // 只留挂了投影文件的帖子，投影分享板块里的普通讨论帖不进列表
            for (const ForumThreadInfo &thread : threads)
            {
                if (!thread.isValid() || !thread.schematic.isValid())
                    continue;
                m_threads.append(thread);
            }

            m_page = qMax(1, pagination.page);
            m_totalPages = qMax(1, pagination.pages);
            renderLoaded();
        },
        [this](const QString &message, int httpStatus) {
            m_threads.clear();
            clearList();
            m_statusLabel->setText(tr("投影列表加载失败：%1").arg(message));
            m_statusLabel->show();
            m_paginationBar->hide();
            if (httpStatus == 401)
                emit loginRequested();
        });
}

void ForumSchematicListPage::renderLoaded()
{
    clearList();

    const QString keyword = m_keyword;
    QVector<ForumThreadInfo> visible;
    for (const ForumThreadInfo &thread : m_threads)
    {
        // 格式筛选：只看当前选中的文件后缀（.litematic / .schem 等）
        if (!m_format.isEmpty()
            && QFileInfo(thread.schematic.fileName).suffix().compare(m_format, Qt::CaseInsensitive) != 0)
        {
            continue;
        }
        if (keyword.isEmpty()
            || thread.title.contains(keyword, Qt::CaseInsensitive)
            || thread.schematic.name.contains(keyword, Qt::CaseInsensitive)
            || thread.schematic.fileName.contains(keyword, Qt::CaseInsensitive)
            || thread.authorName.contains(keyword, Qt::CaseInsensitive))
        {
            visible.append(thread);
        }
    }

    if (visible.isEmpty())
    {
        if (!m_threads.isEmpty())
        {
            // 本页被筛空：分页留着，方便直接翻下一页继续找
            m_statusLabel->setText(keyword.isEmpty()
                                       ? tr("本页没有这种格式的投影，翻页或换个筛选看看")
                                       : tr("本页没有匹配「%1」的投影，翻页或换个筛选看看").arg(keyword));
            m_statusLabel->show();
            updatePager();
        }
        else
        {
            m_statusLabel->setText(keyword.isEmpty()
                                       ? tr("投影分享里还没有投影帖，去「发布」页发一个试试")
                                       : tr("没有匹配「%1」的投影").arg(keyword));
            m_statusLabel->show();
            m_paginationBar->hide();
        }
        return;
    }
    m_statusLabel->hide();

    const QColor themeColor(ThemeManager::instance()->currentThemeColor());

    for (const ForumThreadInfo &thread : visible)
    {
        const QString displayName = schematicCardTitle(thread);
        const QString fileTip = schematicCardTip(thread);

        // 卡片外观与「插件」「下载模组」等列表页的列表卡片同款
        auto *card = new QWidget(m_listHost);
        card->setObjectName(QStringLiteral("modCardListItem"));
        card->setProperty("cardRole", "container");
        card->setFixedHeight(76);
        if (!fileTip.isEmpty())
            card->setToolTip(fileTip);

        auto *rowLayout = new QHBoxLayout(card);
        rowLayout->setContentsMargins(14, 12, 14, 12);
        rowLayout->setSpacing(14);

        // 图标：投影没有封面图，用标题首字占位
        auto *icon = new QLabel(card);
        icon->setObjectName(QStringLiteral("modCardIcon"));
        icon->setProperty("cardRole", "icon");
        icon->setFixedSize(48, 48);
        icon->setAlignment(Qt::AlignCenter);
        icon->setText(displayName.left(1).toUpper());
        rowLayout->addWidget(icon);

        // 名称 + 芯片行（大小 / 下载次数 / 作者 / 评论）
        auto *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(6);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        // 长标题按卡片宽度省略，完整标题和投影名 / 文件名留在悬浮提示里
        auto *name = ForumKit::makeElided(card, displayName);
        name->setObjectName(QStringLiteral("modCardName"));
        name->setProperty("cardRole", "name");
        if (!fileTip.isEmpty())
            name->setToolTip(fileTip);
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
        const QString suffix = QFileInfo(thread.schematic.fileName).suffix().toUpper();
        if (!suffix.isEmpty())
            addChip(suffix);
        // 一帖挂了好几份（主世界 / 下界…）：先说清有几个版本，免得以为只有这一份
        if (thread.schematic.versionCount() > 1)
            addChip(tr("%1 个版本").arg(thread.schematic.versionCount()));
        if (thread.schematic.fileSize > 0)
            addChip(ForumKit::humanFileSize(thread.schematic.fileSize));
        addChip(tr("%1 次下载").arg(thread.schematic.downloads));
        addChip(thread.authorName.isEmpty() ? tr("作者未知") : thread.authorName);
        addChip(tr("%1 评论").arg(thread.comments));
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

        const int threadId = thread.id;
        const QString fileName = thread.schematic.fileName;
        QPushButton *downloadButton = createBtn(QStringLiteral(":/Images/Icons/download.svg"),
                                                tr("下载到实例"));
        connect(downloadButton, &QPushButton::clicked, this,
                [this, threadId, displayName, fileName]() {
            downloadSchematic(threadId, displayName, fileName);
        });
        btnLayout->addWidget(downloadButton);

        QPushButton *openThreadButton = createBtn(
            QStringLiteral(":/Images/Icons/external_link.svg"), tr("看原帖"));
        connect(openThreadButton, &QPushButton::clicked, this,
                [this, threadId]() { emit threadOpenRequested(threadId); });
        btnLayout->addWidget(openThreadButton);
        rowLayout->addLayout(btnLayout);

        m_listLayout->addWidget(card);
    }

    updatePager();
}

void ForumSchematicListPage::updatePager()
{
    m_pageLabel->setText(tr("第 %1/%2 页").arg(m_page).arg(m_totalPages));
    // 与其他在线资源列表页一致：只有一页时不占底部位置
    m_paginationBar->setVisible(m_totalPages > 1);
    m_firstButton->setEnabled(m_page > 1);
    m_prevButton->setEnabled(m_page > 1);
    m_nextButton->setEnabled(m_page < m_totalPages);
    m_lastButton->setEnabled(m_page < m_totalPages);
}

void ForumSchematicListPage::updateFilterButton()
{
    if (!m_filterBtn)
        return;
    const bool active = !m_format.isEmpty();
    m_filterBtn->setText(active ? tr("筛选 · 1") : tr("筛选"));
    m_filterBtn->setProperty("active", active);
    m_filterBtn->style()->unpolish(m_filterBtn);
    m_filterBtn->style()->polish(m_filterBtn);
}

void ForumSchematicListPage::onFilterClicked()
{
    ResourceFilterDialog dialog(
        tr("投影筛选"),
        tr("按投影文件格式筛选：选出想看的格式后，列表只留下这一种；选「全部格式」恢复。"),
        tr("文件格式"), this);

    QVector<QPair<QString, QString>> options;
    options.append({QString(), tr("全部格式")});
    options.append({QStringLiteral("litematic"), tr("Litematica（.litematic）")});
    options.append({QStringLiteral("schem"), tr("WorldEdit（.schem）")});
    options.append({QStringLiteral("schematic"), tr("旧版 WorldEdit（.schematic）")});
    options.append({QStringLiteral("nbt"), tr("结构方块（.nbt）")});
    dialog.setSelectedId(m_format);
    dialog.setOptions(options);

    connect(&dialog, &ResourceFilterDialog::filterApplied, this,
            [this](const QString &id) {
                if (id == m_format)
                    return;
                m_format = id;
                updateFilterButton();
                renderLoaded();
            });
    dialog.exec();
}

void ForumSchematicListPage::downloadSchematic(int threadId, const QString &displayName,
                                               const QString &fileName)
{
    // 目标实例还没有 schematics 目录：顺手装上投影模组并把目录建出来，
    // 下载完 Litematica 就能直接读到
    if (!m_instancePath.isEmpty())
    {
        const QString dirPath = SchematicDownloadTarget::schematicsDir(m_instancePath);
        if (!QDir(dirPath).exists())
        {
            if (!SchematicDownloadTarget::hasProjectionMod(m_instancePath))
                emit projectionModMissing();
            QDir().mkpath(dirPath);
        }
    }

    m_statusLabel->setText(tr("正在获取 %1 的下载地址…").arg(displayName));
    m_statusLabel->show();

    ForumContentService::instance()->fetchSchematicDownload(
        threadId, 0,
        [this, displayName, fileName](const ForumSchematicDownload &download) {
            if (!download.isValid())
            {
                m_statusLabel->setText(tr("拿不到投影下载地址，稍后再试"));
                m_statusLabel->show();
                return;
            }

            QString rawName = download.filename.trimmed();
            if (rawName.isEmpty())
                rawName = fileName.trimmed();
            QString targetName = QFileInfo(rawName).fileName();
            if (targetName.isEmpty())
                targetName = QStringLiteral("projection.litematic");

            // 有实例就一键存进实例的 schematics；还没选实例时让用户自己挑位置，不让下载走空
            QString targetPath;
            if (!m_instancePath.isEmpty())
            {
                const QString dirPath = SchematicDownloadTarget::schematicsDir(m_instancePath);
                if (!QDir().mkpath(dirPath))
                {
                    m_statusLabel->setText(tr("实例的 schematics 文件夹建不出来，检查一下实例目录"));
                    m_statusLabel->show();
                    return;
                }
                targetPath = SchematicDownloadTarget::uniquePath(dirPath, targetName);
            }
            else
            {
                targetPath = QFileDialog::getSaveFileName(window(), tr("保存投影文件"), targetName);
                if (targetPath.isEmpty())
                {
                    m_statusLabel->setText(tr("已取消保存"));
                    m_statusLabel->show();
                    return;
                }
            }

            m_statusLabel->setText(tr("正在下载 %1 …").arg(displayName));
            m_statusLabel->show();

            auto *manager = new QNetworkAccessManager(this);
            QNetworkRequest request{QUrl(download.url)};
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
            request.setTransferTimeout(300000);

            QNetworkReply *reply = manager->get(request);
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, manager, targetPath, displayName]() {
                        reply->deleteLater();
                        manager->deleteLater();

                        const QByteArray data = reply->readAll();
                        if (reply->error() != QNetworkReply::NoError || data.isEmpty())
                        {
                            m_statusLabel->setText(tr("%1 下载失败，请检查网络后再试").arg(displayName));
                            m_statusLabel->show();
                            return;
                        }

                        QFile file(targetPath);
                        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
                        {
                            m_statusLabel->setText(tr("投影文件写不进目标目录，检查一下磁盘空间"));
                            m_statusLabel->show();
                            return;
                        }
                        file.close();

                        m_statusLabel->hide();
                        NotificationManager::showSuccess(
                            this, tr("「%1」已保存").arg(QFileInfo(targetPath).fileName()));
                        emit projectionSaved();
                    });
        },
        [this](const QString &message, int httpStatus) {
            m_statusLabel->setText(tr("获取下载地址失败：%1").arg(message));
            m_statusLabel->show();
            if (httpStatus == 401)
                emit loginRequested();
        });
}
