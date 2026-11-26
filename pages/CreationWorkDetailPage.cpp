/**
 * @file   CreationWorkDetailPage.cpp
 * @brief  创作中心的作品详情页实现（管理视角）
 * @author BlockBox Team
 * @date   2026-11-14
 */

#include "CreationWorkDetailPage.h"

#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/ForumPostBrowser.h"
#include "components/ForumTagEditor.h"
#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"
#include "utils/links/LinkRouter.h"
#include "utils/plugin/PluginManager.h"

CreationWorkDetailPage::CreationWorkDetailPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void CreationWorkDetailPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(m_scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(12);

    // ── 状态行：回列表 + 加载提示 ──
    auto *statusRow = new QHBoxLayout();
    statusRow->setSpacing(10);
    m_backButton = new QPushButton(tr("返回作品列表"), content);
    m_backButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setFixedHeight(28);
    connect(m_backButton, &QPushButton::clicked, this, &CreationWorkDetailPage::backRequested);
    statusRow->addWidget(m_backButton);

    m_statusLabel = new QLabel(content);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch(1);
    layout->addLayout(statusRow);

    // ── 作品头卡 ──
    auto *headerCard = ForumKit::makeCard(content, QStringLiteral("forumHeaderCard"));
    auto *headerLayout = new QVBoxLayout(headerCard);
    headerLayout->setContentsMargins(20, 16, 20, 16);
    headerLayout->setSpacing(8);

    m_badgeLayout = new QHBoxLayout();
    m_badgeLayout->setContentsMargins(0, 0, 0, 0);
    m_badgeLayout->setSpacing(8);
    headerLayout->addLayout(m_badgeLayout);

    m_titleLabel = new ForumKit::ElidedLabel(headerCard);
    m_titleLabel->setObjectName(QStringLiteral("forumThreadTitle"));
    headerLayout->addWidget(m_titleLabel);

    m_metaLabel = ForumKit::makeMuted(headerCard);
    headerLayout->addWidget(m_metaLabel);
    layout->addWidget(headerCard);

    // ── 数据卡 ──
    auto *dataCard = ForumKit::makeCard(content, QStringLiteral("forumCard"));
    auto *dataLayout = new QHBoxLayout(dataCard);
    dataLayout->setContentsMargins(20, 14, 20, 14);
    dataLayout->setSpacing(8);
    const auto addStat = [dataCard, dataLayout](QLabel **target, QFrame **tile,
                                                const QString &caption) {
        auto *box = new QFrame(dataCard);
        box->setObjectName(QStringLiteral("forumStatBox"));
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(14, 10, 14, 10);
        boxLayout->setSpacing(0);
        auto *value = new QLabel(QStringLiteral("0"), box);
        value->setObjectName(QStringLiteral("forumStatValue"));
        boxLayout->addWidget(value);
        auto *captionLabel = new QLabel(caption, box);
        captionLabel->setObjectName(QStringLiteral("forumRailKey"));
        boxLayout->addWidget(captionLabel);
        *target = value;
        *tile = box;
        dataLayout->addWidget(box);
    };
    QFrame *viewsTile = nullptr;
    QFrame *repliesTile = nullptr;
    addStat(&m_statViews, &viewsTile, tr("浏览"));
    addStat(&m_statReplies, &repliesTile, tr("回复"));
    addStat(&m_statDownloads, &m_downloadTile, tr("下载"));
    Q_UNUSED(viewsTile);
    Q_UNUSED(repliesTile);
    dataLayout->addStretch(1);
    layout->addWidget(dataCard);

    // ── 版本卡（插件作品才有） ──
    m_versionsCard = ForumKit::makeCard(content, QStringLiteral("forumPluginSection"));
    auto *versionsCardLayout = new QVBoxLayout(m_versionsCard);
    versionsCardLayout->setContentsMargins(14, 14, 14, 14);
    versionsCardLayout->setSpacing(8);

    auto *versionsTitle = new QLabel(tr("版本管理"), m_versionsCard);
    versionsTitle->setObjectName(QStringLiteral("forumPluginSectionTitle"));
    versionsCardLayout->addWidget(versionsTitle);

    m_versionStatus = new QLabel(m_versionsCard);
    m_versionStatus->setObjectName(QStringLiteral("forumPluginStatus"));
    m_versionStatus->setWordWrap(true);
    m_versionStatus->hide();
    versionsCardLayout->addWidget(m_versionStatus);

    auto *uploadButton = new QPushButton(tr("上传新版本"), m_versionsCard);
    uploadButton->setObjectName(QStringLiteral("forumGhostButton"));
    uploadButton->setCursor(Qt::PointingHandCursor);
    uploadButton->setFixedHeight(30);
    connect(uploadButton, &QPushButton::clicked, this,
            &CreationWorkDetailPage::uploadNewVersion);
    versionsCardLayout->addWidget(uploadButton);

    m_versionsLayout = new QVBoxLayout();
    m_versionsLayout->setContentsMargins(0, 0, 0, 0);
    m_versionsLayout->setSpacing(4);
    versionsCardLayout->addLayout(m_versionsLayout);
    layout->addWidget(m_versionsCard);

    // ── 正文卡 ──
    auto *contentCard = ForumKit::makeCard(content, QStringLiteral("forumCard"));
    auto *contentLayout = new QVBoxLayout(contentCard);
    contentLayout->setContentsMargins(20, 16, 20, 16);
    contentLayout->setSpacing(8);

    auto *contentTitle = new QLabel(tr("作品正文"), contentCard);
    contentTitle->setObjectName(QStringLiteral("forumPluginSectionTitle"));
    contentLayout->addWidget(contentTitle);

    m_contentBrowser = new ForumPostBrowser(contentCard);
    m_contentBrowser->setPostMarkdown(QString());
    contentLayout->addWidget(m_contentBrowser);
    connect(m_contentBrowser, &ForumPostBrowser::imageClicked, this,
            [](const QString &path) { ForumPostBrowser::showImagePreview(path); });
    // 作品正文里的链接：CF / Modrinth 资源页留在软件里打开，其它走浏览器
    connect(m_contentBrowser, &ForumPostBrowser::linkClicked, this, [this](const QUrl &url) {
        LinkRouter::instance()->open(url, this);
    });
    layout->addWidget(contentCard);

    // ── 操作行 ──
    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);
    auto *editButton = new QPushButton(tr("编辑作品"), content);
    editButton->setObjectName(QStringLiteral("forumGhostButton"));
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setFixedHeight(32);
    connect(editButton, &QPushButton::clicked, this, &CreationWorkDetailPage::editWork);
    actionRow->addWidget(editButton);

    auto *openButton = new QPushButton(tr("查看公开帖子"), content);
    openButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setFixedHeight(32);
    connect(openButton, &QPushButton::clicked, this, [this]() {
        if (m_thread.isValid())
            emit threadOpenRequested(m_thread.id);
    });
    actionRow->addWidget(openButton);

    auto *deleteButton = new QPushButton(tr("删除作品"), content);
    deleteButton->setObjectName(QStringLiteral("dangerBtn"));
    deleteButton->setCursor(Qt::PointingHandCursor);
    deleteButton->setFixedHeight(32);
    connect(deleteButton, &QPushButton::clicked, this, &CreationWorkDetailPage::deleteWork);
    actionRow->addWidget(deleteButton);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

    layout->addStretch(1);
    m_scroll->setWidget(content);
    outer->addWidget(m_scroll);
}

void CreationWorkDetailPage::openWork(const ForumThreadInfo &thread)
{
    if (!thread.isValid())
        return;
    m_thread = thread;
    m_firstPostId = 0;
    m_firstPostContent.clear();
    m_versionStatus->hide();
    setStatus(QString());
    renderWork();
    loadContent();
    if (thread.plugin.isValid())
        loadVersions();
}

void CreationWorkDetailPage::renderWork()
{
    // 标题 + 徽标
    m_titleLabel->setFullText(m_thread.title);
    ForumKit::clearLayout(m_badgeLayout);
    if (m_thread.plugin.isValid())
        m_badgeLayout->addWidget(ForumKit::makeBadge(m_badgeLayout->parentWidget(), tr("插件")));
    if (m_thread.schematic.isValid())
        m_badgeLayout->addWidget(ForumKit::makeBadge(m_badgeLayout->parentWidget(), tr("投影")));
    if (m_thread.source.isRepost())
        m_badgeLayout->addWidget(ForumKit::makeBadge(m_badgeLayout->parentWidget(), tr("转载")));
    if (m_thread.pinned)
        m_badgeLayout->addWidget(ForumKit::makeBadge(m_badgeLayout->parentWidget(), tr("置顶")));
    if (m_thread.locked)
        m_badgeLayout->addWidget(ForumKit::makeBadge(m_badgeLayout->parentWidget(), tr("锁定"), true));

    // 数据行
    QStringList metaParts;
    if (!m_thread.boardName.isEmpty())
        metaParts << m_thread.boardName;
    if (m_thread.createdAt > 0)
        metaParts << tr("发布于 %1").arg(ForumKit::relativeTime(m_thread.createdAt));
    if (m_thread.lastPostAt > 0)
        metaParts << tr("最后回复 %1").arg(ForumKit::relativeTime(m_thread.lastPostAt));
    m_metaLabel->setText(metaParts.join(QStringLiteral(" · ")));

    m_statViews->setText(QString::number(m_thread.views));
    m_statReplies->setText(QString::number(m_thread.replies));
    // 下载量：插件 / 投影作品显示真实下载数，普通帖没有可下载的包
    const bool hasPackage = m_thread.plugin.isValid() || m_thread.schematic.isValid();
    m_statDownloads->setText(hasPackage
                                 ? QString::number(m_thread.plugin.isValid()
                                                       ? m_thread.plugin.downloads
                                                       : m_thread.schematic.downloads)
                                 : QStringLiteral("—"));
    m_downloadTile->setVisible(hasPackage);

    // 版本卡只有插件作品才有
    m_versionsCard->setVisible(m_thread.plugin.isValid());
}

void CreationWorkDetailPage::loadContent()
{
    if (m_loading || m_thread.id <= 0)
        return;

    m_loading = true;
    setStatus(tr("正在加载作品数据…"));
    const int threadId = m_thread.id;

    ForumContentService::instance()->fetchThread(
        threadId, 1,
        [this, threadId](const ForumThreadDetail &detail) {
            m_loading = false;
            if (threadId != m_thread.id)
                return;   // 期间切到了别的作品，丢弃
            setStatus(QString());
            if (detail.info.isValid())
            {
                m_thread = detail.info;
                renderWork();
            }
            if (!detail.posts.isEmpty() && detail.posts.first().isValid())
            {
                m_firstPostId = detail.posts.first().id;
                m_firstPostContent = detail.posts.first().content;
                m_contentBrowser->setPostMarkdown(m_firstPostContent);
            }
        },
        [this, threadId](const QString &message, int) {
            m_loading = false;
            if (threadId != m_thread.id)
                return;
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void CreationWorkDetailPage::loadVersions()
{
    if (!m_thread.plugin.isValid() || m_thread.id <= 0)
        return;

    const int threadId = m_thread.id;
    ForumContentService::instance()->fetchPluginVersions(
        threadId,
        [this, threadId](const QVector<ForumPluginVersionInfo> &versions) {
            if (threadId != m_thread.id)
                return;
            renderVersions(versions);
        },
        [](const QString &, int) {
            // 版本历史是附加信息，拉不到就不显示
        });
}

void CreationWorkDetailPage::renderVersions(const QVector<ForumPluginVersionInfo> &versions)
{
    if (!m_versionsLayout)
        return;
    ForumKit::clearLayout(m_versionsLayout);
    QWidget *section = m_versionsLayout->parentWidget();

    if (versions.isEmpty())
    {
        auto *empty = ForumKit::makeMuted(section, tr("还没有历史版本，当前包是唯一的一版"));
        m_versionsLayout->addWidget(empty);
        return;
    }

    for (const ForumPluginVersionInfo &version : versions)
    {
        QStringList parts;
        parts << (version.version.isEmpty() ? tr("未标注版本") : tr("v%1").arg(version.version));
        parts << ForumKit::humanFileSize(version.fileSize);
        parts << tr("已下载 %1 次").arg(version.downloads);
        if (version.createdAt > 0)
            parts << QDateTime::fromSecsSinceEpoch(version.createdAt)
                         .toString(QStringLiteral("yyyy-MM-dd"));

        auto *row = new QHBoxLayout();
        row->setSpacing(6);
        auto *text = new QLabel(parts.join(QStringLiteral(" · ")), section);
        text->setObjectName(QStringLiteral("forumPluginHint"));
        text->setWordWrap(true);
        row->addWidget(text, 1);
        if (version.current)
            row->addWidget(ForumKit::makeBadge(section, tr("当前版")));
        m_versionsLayout->addLayout(row);
    }
}

void CreationWorkDetailPage::editWork()
{
    if (m_thread.id <= 0)
        return;
    if (m_loading)
    {
        NotificationManager::showInfo(this, tr("作品还在加载，稍候再编辑"));
        return;
    }

    QString title;
    QStringList tags;
    QString newContent;
    if (!runEditDialog(this, m_thread.title, m_thread.tags, m_firstPostContent, &title, &tags,
                       &newContent))
        return;

    const QString content = m_firstPostContent;
    const bool contentChanged = newContent != content;
    const bool metaChanged = title != m_thread.title || tags != m_thread.tags;
    if (!metaChanged && !contentChanged)
    {
        NotificationManager::showInfo(this, tr("内容没有变化"));
        return;
    }
    if (contentChanged && m_firstPostId <= 0)
    {
        NotificationManager::showInfo(this, tr("正文还没加载好，刷新后再试"));
        return;
    }

    setStatus(tr("正在保存修改…"));
    const int threadId = m_thread.id;
    const auto finish = [this](const QString &message) {
        setStatus(QString());
        if (!message.isEmpty())
        {
            NotificationManager::showError(this, message);
            return;
        }
        NotificationManager::showSuccess(this, tr("作品已更新"));
        loadContent();
        emit workChanged();
    };

    const auto saveMeta = [this, contentChanged, newContent, finish]() {
        if (!contentChanged)
        {
            finish(QString());
            return;
        }
        ForumContentService::instance()->editPost(
            m_firstPostId, newContent, [finish]() { finish(QString()); },
            [finish](const QString &message, int) { finish(message); });
    };

    if (!metaChanged)
    {
        saveMeta();
        return;
    }
    // 这里只改标题/正文：把作品上已绑的相关链接原样带回，避免被清空
    const QVector<ForumResourceLink> keepLinks =
        m_thread.plugin.isValid() ? m_thread.plugin.links : m_thread.schematic.links;
    ForumContentService::instance()->editThread(
        threadId, title, tags, keepLinks, saveMeta,
        [finish](const QString &message, int) { finish(message); });
}

void CreationWorkDetailPage::deleteWork()
{
    if (m_thread.id <= 0 || m_uploading)
        return;

    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("删除作品"),
        tr("确定删除《%1》吗？所有内容都不再显示，此操作不可撤销。").arg(m_thread.title),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    ForumContentService::instance()->deleteThread(
        m_thread.id,
        [this]() {
            NotificationManager::showSuccess(this, tr("作品已删除"));
            emit workDeleted();
        },
        [this](const QString &message, int) {
            NotificationManager::showError(this, message);
        });
}

void CreationWorkDetailPage::uploadNewVersion()
{
    if (m_uploading || !m_thread.plugin.isValid() || m_thread.id <= 0)
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择新版本插件包"), QString(),
        QStringLiteral("BlockBox 插件 (*.BlockBox *.blockbox)"));
    if (path.isEmpty())
        return;

    constexpr qint64 kMaxPluginBytes = 20 * 1024 * 1024;
    const QFileInfo info(path);
    if (info.size() > kMaxPluginBytes)
    {
        NotificationManager::showInfo(this, tr("插件包超过 20 MB，服务器不收这么大，先删减一下"));
        return;
    }

    // 本地先解析一遍：清单不合法的包传上去也会被服务端退回
    const PluginInfo localInfo = PluginManager::instance()->inspectPluginFile(path);
    if (!localInfo.loaded || localInfo.id.isEmpty() || localInfo.name.isEmpty())
    {
        NotificationManager::showInfo(this, tr("这不是有效的插件包：%1").arg(
            localInfo.loadError.isEmpty() ? tr("清单缺少 id 或 name") : localInfo.loadError));
        return;
    }
    if (localInfo.id != m_thread.plugin.pluginId)
    {
        NotificationManager::showInfo(
            this, tr("这个包的插件 ID（%1）和当前作品（%2）不是同一个插件")
                      .arg(localInfo.id, m_thread.plugin.pluginId));
        return;
    }

    m_uploading = true;
    m_versionStatus->setText(tr("正在上传新版本…"));
    m_versionStatus->show();
    const auto restore = [this]() {
        m_uploading = false;
        m_versionStatus->hide();
    };

    ForumContentService::instance()->uploadPluginVersion(
        m_thread.id, path,
        [this](int percent) {
            m_versionStatus->setText(tr("正在上传新版本 %1%").arg(percent));
            m_versionStatus->show();
        },
        [this, restore]() {
            restore();
            NotificationManager::showSuccess(this, tr("新版本已上传，旧版本留在了版本管理里"));
            loadVersions();
            loadContent();
            emit workChanged();
        },
        [this, restore](const QString &message, int) {
            restore();
            m_versionStatus->setText(message);
            m_versionStatus->show();
        });
}

void CreationWorkDetailPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}

bool CreationWorkDetailPage::runEditDialog(QWidget *parent, const QString &title,
                                           const QStringList &tags, const QString &content,
                                           QString *outTitle, QStringList *outTags,
                                           QString *outContent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("编辑作品"));
    dialog.setMinimumWidth(560);
    auto *layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(tr("标题"), &dialog));
    auto *titleEdit = new QLineEdit(title, &dialog);
    layout->addWidget(titleEdit);

    auto *tagsEditor = new ForumTagEditor(&dialog);
    tagsEditor->setTags(tags);
    layout->addWidget(tagsEditor);

    layout->addWidget(new QLabel(tr("正文（Markdown）"), &dialog));
    auto *contentEdit = new QPlainTextEdit(&dialog);
    contentEdit->setPlainText(content);
    contentEdit->setMinimumHeight(220);
    layout->addWidget(contentEdit);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), &dialog);
    auto *save = new QPushButton(tr("保存"), &dialog);
    save->setObjectName(QStringLiteral("forumPrimaryButton"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    const QString newTitle = titleEdit->text().trimmed();
    if (newTitle.isEmpty())
    {
        NotificationManager::showInfo(parent, tr("标题不能为空"));
        return false;
    }

    tagsEditor->commitPending();
    const QStringList newTags = tagsEditor->tags();

    if (outTitle)
        *outTitle = newTitle;
    if (outTags)
        *outTags = newTags;
    if (outContent)
        *outContent = contentEdit->toPlainText();
    return true;
}
