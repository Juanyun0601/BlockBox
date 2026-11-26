/**
 * @file   LibraryPage.cpp
 * @brief  方块图书馆 · 启动器内置整页（不依赖浏览器）
 * @author BlockBox Team
 * @date   2026-11-25
 */

#include "LibraryPage.h"

#include <QApplication>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QJsonValue>
#include <QSignalBlocker>
#include <QStyle>

#include <algorithm>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "utils/ResourceManager.h"
#include "utils/ThemeManager.h"

namespace {

struct Palette
{
    QString bg;
    QString card;
    QString card2;
    QString line;
    QString ink;
    QString ink2;
    QString ink3;
    QString brand;
    QString brandHover;
    QString soft;
    QString codeBg;
    QString danger;
    QString tipBg;
    QString warnBg;
    QString warnInk;
    QString chipBg;
    QString chipInk;
    QString tableHead;
};

Palette paletteFor(bool dark)
{
    if (dark)
    {
        return {QStringLiteral("#0f1512"), QStringLiteral("#161f19"), QStringLiteral("#131b16"),
                QStringLiteral("#26332b"), QStringLiteral("#e7f0e9"), QStringLiteral("#a7baac"),
                QStringLiteral("#7d9184"), QStringLiteral("#66bb6a"), QStringLiteral("#81c784"),
                QStringLiteral("#1b2b21"), QStringLiteral("#0b120e"), QStringLiteral("#ef7c74"),
                QStringLiteral("#17281b"), QStringLiteral("#241d0f"), QStringLiteral("#e8c680"),
                QStringLiteral("#1b2b21"), QStringLiteral("#9be3a5"), QStringLiteral("#1b2b20")};
    }
    return {QStringLiteral("#f4f8f3"), QStringLiteral("#ffffff"), QStringLiteral("#fbfdfb"),
            QStringLiteral("#e3ebe2"), QStringLiteral("#17251a"), QStringLiteral("#52665a"),
            QStringLiteral("#87998b"), QStringLiteral("#2e7d32"), QStringLiteral("#1b5e20"),
            QStringLiteral("#e8f5e9"), QStringLiteral("#f1f6f2"), QStringLiteral("#c62828"),
            QStringLiteral("#eef7ea"), QStringLiteral("#fff8e6"), QStringLiteral("#7a5a12"),
            QStringLiteral("#e8f5e9"), QStringLiteral("#1b5e20"), QStringLiteral("#eef7ea")};
}

QString librarySiteRoot()
{
    return QStringLiteral("https://mcblockbox.top/");
}


} // namespace

LibraryPage::LibraryPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("libraryPage"));
    buildUi();
    applyTheme();
    showPlaceholder(tr("<h2>方块图书馆</h2><p>正在同步方块盒子的四卷文档…</p>"));

    m_network = new QNetworkAccessManager(this);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](ThemeManager::ThemeType) { applyTheme(); });
}

LibraryPage::~LibraryPage() = default;

QString LibraryPage::contentUrl()
{
    QString url = qEnvironmentVariable("BLOCKBOX_LIBRARY_URL").trimmed();
    if (url.isEmpty())
        url = QStringLiteral("https://mcblockbox.top/library/assets/library-content.json");
    return url;
}

QString libraryCachePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/library/library-content.json");
}void LibraryPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 16);
    root->setSpacing(12);

    // ---- 顶部一行：搜索 + 分卷切换 + 状态 + 同步 / 翻页 ----
    auto *header = new QHBoxLayout();
    header->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("libSearch"));
    m_searchEdit->setPlaceholderText(tr("搜索文档…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMaximumWidth(300);
    m_searchEdit->setMinimumWidth(150);
    m_searchEdit->addAction(ResourceManager::instance()->loadThemedIcon(QStringLiteral(":/Images/Icons/search.svg")),
                           QLineEdit::LeadingPosition);
    header->addWidget(m_searchEdit);

    // 分卷切换：首页 / 使用教程 / 插件开发 / 开放平台 API / MCP 接入
    m_tabHost = new QWidget(this);
    m_tabLayout = new QHBoxLayout(m_tabHost);
    m_tabLayout->setContentsMargins(0, 0, 0, 0);
    m_tabLayout->setSpacing(2);
    header->addWidget(m_tabHost);

    header->addStretch(1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("libStatus"));
    m_statusLabel->setText(tr("正在准备…"));
    m_statusLabel->setMaximumWidth(190);
    m_statusLabel->setMinimumWidth(0);
    m_statusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    header->addWidget(m_statusLabel);

    auto *syncButton = new QPushButton(tr("同步"), this);
    syncButton->setObjectName(QStringLiteral("libNavBtn"));
    syncButton->setCursor(Qt::PointingHandCursor);
    syncButton->setToolTip(tr("从 mcblockbox.top 重新同步文档（断网时继续显示本地缓存）"));
    connect(syncButton, &QPushButton::clicked, this, &LibraryPage::fetchContent);
    header->addWidget(syncButton);

    m_prevButton = new QPushButton(tr("‹"), this);
    m_prevButton->setObjectName(QStringLiteral("libNavBtn"));
    m_prevButton->setCursor(Qt::PointingHandCursor);
    m_prevButton->setToolTip(tr("上一章"));
    m_prevButton->setProperty("compact", true);
    m_prevButton->setFixedWidth(32);

    m_nextButton = new QPushButton(tr("›"), this);
    m_nextButton->setObjectName(QStringLiteral("libNavBtn"));
    m_nextButton->setCursor(Qt::PointingHandCursor);
    m_nextButton->setToolTip(tr("下一章"));
    m_nextButton->setProperty("compact", true);
    m_nextButton->setFixedWidth(32);

    header->addWidget(m_prevButton);
    header->addWidget(m_nextButton);
    root->addLayout(header);

    connect(m_prevButton, &QPushButton::clicked, this, [this]() {
        int v = m_volumeIndex;
        int c = m_chapterIndex - 1;
        if (c < 0)
        {
            v -= 1;
            if (v < 0)
                return;
            c = m_volumes[v].chapters.size() - 1;
        }
        showChapter(v, c);
    });
    connect(m_nextButton, &QPushButton::clicked, this, [this]() {
        int v = m_volumeIndex;
        int c = m_chapterIndex + 1;
        if (v < 0)
        {
            v = 0;
            c = 0;
        }
        else if (c >= m_volumes[v].chapters.size())
        {
            v += 1;
            c = 0;
            if (v >= m_volumes.size())
                return;
        }
        showChapter(v, c);
    });

    // ---- 主体：左侧目录 / 右侧正文 ----
    auto *body = new QHBoxLayout();
    body->setSpacing(14);

    m_leftPanel = new QFrame(this);
    m_leftPanel->setObjectName(QStringLiteral("libPanel"));
    auto *leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->setContentsMargins(10, 12, 10, 12);
    leftLayout->setSpacing(8);

    auto *leftTitle = new QLabel(tr("文档目录"), m_leftPanel);
    leftTitle->setObjectName(QStringLiteral("libPanelTitle"));
    m_panelTitle = leftTitle;
    leftLayout->addWidget(leftTitle);

    m_leftStack = new QStackedWidget(m_leftPanel);

    m_tree = new QTreeWidget(m_leftStack);
    m_tree->setObjectName(QStringLiteral("libTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_leftStack->addWidget(m_tree);

    m_results = new QListWidget(m_leftStack);
    m_results->setObjectName(QStringLiteral("libResults"));
    m_results->setWordWrap(true);
    m_results->setSpacing(2);
    m_results->setFocusPolicy(Qt::NoFocus);
    m_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_leftStack->addWidget(m_results);

    leftLayout->addWidget(m_leftStack, 1);
    m_leftPanel->setFixedWidth(276);
    body->addWidget(m_leftPanel);

    auto *reading = new QFrame(this);
    reading->setObjectName(QStringLiteral("libReading"));
    auto *readingLayout = new QVBoxLayout(reading);
    readingLayout->setContentsMargins(22, 14, 22, 14);
    readingLayout->setSpacing(8);

    auto *crumbRow = new QHBoxLayout();
    crumbRow->setSpacing(8);
    m_volumeChip = new QLabel(reading);
    m_volumeChip->setObjectName(QStringLiteral("libVolumeChip"));
    crumbRow->addWidget(m_volumeChip);
    m_crumbLabel = new QLabel(reading);
    m_crumbLabel->setObjectName(QStringLiteral("libCrumb"));
    m_crumbLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    crumbRow->addWidget(m_crumbLabel, 1);
    m_timeLabel = new QLabel(reading);
    m_timeLabel->setObjectName(QStringLiteral("libTimeChip"));
    crumbRow->addWidget(m_timeLabel);
    readingLayout->addLayout(crumbRow);

    m_browser = new QTextBrowser(reading);
    m_browser->setObjectName(QStringLiteral("libBrowser"));
    m_browser->setOpenLinks(false);
    m_browser->setOpenExternalLinks(false);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setReadOnly(true);
    m_browser->document()->setDocumentMargin(6);
    readingLayout->addWidget(m_browser, 1);
    body->addWidget(reading, 1);

    root->addLayout(body, 1);

    // ---- 交互 ----
    connect(m_searchEdit, &QLineEdit::textChanged, this, &LibraryPage::runSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_leftStack->currentWidget() == m_results && m_results->count() > 0)
            activateSearchRow(0);
    });
    connect(m_results, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item)
            activateSearchRow(m_results->row(item));
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (!item)
            return;
        bool ok = false;
        const int chapter = item->data(0, Qt::UserRole + 1).toInt(&ok);
        if (!ok)
            return; // 分卷标题本身不跳转
        const int volume = item->data(0, Qt::UserRole).toInt();
        showChapter(volume, chapter);
    });
    connect(m_browser, &QTextBrowser::anchorClicked, this, &LibraryPage::handleAnchor);


}

/* ---------------- 主题 ---------------- */
void LibraryPage::applyTheme()
{
    const bool dark = ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    const Palette p = paletteFor(dark);
    setProperty("dark", dark);

    QString qss = QStringLiteral(R"QSS(
        QWidget#libraryPage { background: transparent; }
        QFrame#libPanel, QFrame#libReading {
            background: %CARD%;
            border: 1px solid %LINE%;
            border-radius: 16px;
        }
        QLabel#libPanelTitle {
            color: %INK3%; font-size: 11.5px; font-weight: 600;
            padding: 2px 6px 4px 6px;
        }
        QLabel#libVolumeChip {
            color: %CHIP_INK%; background: %CHIP_BG%;
            border-radius: 9px; padding: 3px 10px;
            font-size: 11.5px; font-weight: 600;
        }
        QLabel#libCrumb { color: %INK%; font-size: 14px; font-weight: 700; }
        QLabel#libTimeChip { color: %INK3%; font-size: 11px; padding: 2px 4px; }
        QLabel#libStatus {
            color: %INK3%; font-size: 11.5px;
            padding: 3px 9px; border-radius: 8px; background: %CARD2%;
        }
        QLabel#libStatus[warning="true"] { color: %DANGER%; background: %WARN_BG%; }
        QLineEdit#libSearch {
            background: %CARD2%; color: %INK%;
            border: 1px solid %LINE%; border-radius: 11px;
            padding: 8px 10px; font-size: 13px;
            selection-background-color: %SOFT%; selection-color: %INK%;
        }
        QLineEdit#libSearch:focus { border-color: %BRAND%; background: %CARD%; }
        QPushButton#libNavBtn {
            background: %CARD%; color: %INK2%;
            border: 1px solid %LINE%; border-radius: 9px;
            padding: 7px 13px; font-size: 12.5px;
        }
        QPushButton#libNavBtn:hover { color: %BRAND%; border-color: %BRAND%; background: %CARD2%; }
        QPushButton#libNavBtn:pressed { background: %SOFT%; }
        QPushButton#libNavBtn:disabled { color: %INK3%; border-color: %LINE%; background: %CARD%; }
        QPushButton#libNavBtn[compact="true"] { padding: 6px 0; font-size: 15px; }
        QPushButton#libTab {
            background: transparent; color: %INK2%;
            border: 1px solid transparent; border-radius: 9px;
            padding: 5px 10px; font-size: 12px;
        }
        QPushButton#libTab:hover { background: %SOFT%; color: %BRAND%; }
        QPushButton#libTab:checked {
            background: %CHIP_BG%; color: %CHIP_INK%; font-weight: 600;
        }
        QTreeWidget#libTree, QListWidget#libResults {
            background: transparent; border: none; color: %INK%;
            font-size: 13px; outline: 0;
        }
        QTreeWidget#libTree::item, QListWidget#libResults::item {
            padding: 6px 8px; border-radius: 8px;
        }
        QTreeWidget#libTree::item:hover, QListWidget#libResults::item:hover { background: %SOFT%; }
        QTreeWidget#libTree::item:selected, QListWidget#libResults::item:selected {
            background: %SOFT%; color: %BRAND%;
        }
        QTextBrowser#libBrowser { background: transparent; border: none; }
        QTextBrowser#libBrowser QScrollBar:vertical,
        QTreeWidget#libTree QScrollBar:vertical,
        QListWidget#libResults QScrollBar:vertical {
            background: transparent; width: 10px; margin: 2px;
        }
        QTextBrowser#libBrowser QScrollBar::handle:vertical,
        QTreeWidget#libTree QScrollBar::handle:vertical,
        QListWidget#libResults QScrollBar::handle:vertical {
            background: %LINE%; border-radius: 5px; min-height: 32px;
        }
        QTextBrowser#libBrowser QScrollBar::add-line:vertical,
        QTextBrowser#libBrowser QScrollBar::sub-line:vertical,
        QTreeWidget#libTree QScrollBar::add-line:vertical,
        QTreeWidget#libTree QScrollBar::sub-line:vertical,
        QListWidget#libResults QScrollBar::add-line:vertical,
        QListWidget#libResults QScrollBar::sub-line:vertical { height: 0; }
        QTextBrowser#libBrowser QScrollBar::add-page:vertical,
        QTextBrowser#libBrowser QScrollBar::sub-page:vertical,
        QTreeWidget#libTree QScrollBar::add-page:vertical,
        QTreeWidget#libTree QScrollBar::sub-page:vertical,
        QListWidget#libResults QScrollBar::add-page:vertical,
        QListWidget#libResults QScrollBar::sub-page:vertical { background: transparent; }
    )QSS");
    qss.replace(QStringLiteral("%CARD2%"), p.card2)
       .replace(QStringLiteral("%CARD%"), p.card)
       .replace(QStringLiteral("%LINE%"), p.line)
       .replace(QStringLiteral("%INK3%"), p.ink3)
       .replace(QStringLiteral("%INK2%"), p.ink2)
       .replace(QStringLiteral("%INK%"), p.ink)
       .replace(QStringLiteral("%BRAND%"), p.brand)
       .replace(QStringLiteral("%DANGER%"), p.danger)
       .replace(QStringLiteral("%WARN_BG%"), p.warnBg)
       .replace(QStringLiteral("%CHIP_BG%"), p.chipBg)
       .replace(QStringLiteral("%CHIP_INK%"), p.chipInk)
       .replace(QStringLiteral("%SOFT%"), p.soft);
    setStyleSheet(qss);

    // 正文排版：Qt 富文本只认默认样式表这一层，这里给出字号、颜色和表格线
    if (m_browser && m_browser->document())
    {
        QString docCss = QStringLiteral(R"CSS(
            body, p, li, td, th, blockquote { color: %INK%; font-size: 14px; }
            h2 { color: %INK%; font-size: 23px; font-weight: 700; margin-top: 22px; margin-bottom: 8px; }
            h3 { color: %INK%; font-size: 17px; font-weight: 700; margin-top: 18px; margin-bottom: 5px; }
            h4 { color: %INK2%; font-size: 15px; font-weight: 700; margin-top: 14px; margin-bottom: 4px; }
            a { color: %BRAND%; text-decoration: underline; }
            code { color: %CHIP_INK%; background-color: %SOFT%; }
            pre { color: %INK%; background-color: %CODE_BG%; margin-top: 6px; margin-bottom: 12px; }
            blockquote { color: %INK2%; background-color: %TIP_BG%; }
            blockquote.tip { background-color: %TIP_BG%; }
            blockquote.warn { color: %WARN_INK%; background-color: %WARN_BG%; }
            table { border: 1px solid %LINE%; }
            td, th { border: 1px solid %LINE%; padding: 7px 10px; }
            th { color: %CHIP_INK%; background-color: %TABLE_HEAD%; font-weight: 700; text-align: left; }
            ul, ol { margin-top: 4px; margin-bottom: 8px; }
        )CSS");
        docCss.replace(QStringLiteral("%CODE_BG%"), p.codeBg)
              .replace(QStringLiteral("%TABLE_HEAD%"), p.tableHead)
              .replace(QStringLiteral("%CHIP_INK%"), p.chipInk)
              .replace(QStringLiteral("%TIP_BG%"), p.tipBg)
              .replace(QStringLiteral("%WARN_BG%"), p.warnBg)
              .replace(QStringLiteral("%WARN_INK%"), p.warnInk)
              .replace(QStringLiteral("%LINE%"), p.line)
              .replace(QStringLiteral("%INK2%"), p.ink2)
              .replace(QStringLiteral("%INK%"), p.ink)
              .replace(QStringLiteral("%BRAND%"), p.brand)
              .replace(QStringLiteral("%SOFT%"), p.soft);
        m_browser->document()->setDefaultStyleSheet(docCss);
    }

    // 链接颜色跟主题色走（QTextBrowser 的链接色取自调色板）
    if (m_browser)
    {
        QPalette browserPalette = m_browser->palette();
        browserPalette.setColor(QPalette::Link, QColor(p.brand));
        browserPalette.setColor(QPalette::LinkVisited, QColor(p.brandHover));
        m_browser->setPalette(browserPalette);
    }

    // 主题切换后重画当前内容，让文字颜色跟上（滚动位置保持）
    if (m_browser && m_loaded)
    {
        const int scroll = m_browser->verticalScrollBar()->value();
        if (m_homeMode)
        {
            m_browser->setHtml(homeHtml());
            applyReadingTypography();
        }
        else
        {
            renderChapter();
        }
        m_browser->verticalScrollBar()->setValue(scroll);
    }
}
/* ---------------- 数据 ---------------- */
void LibraryPage::open()
{
    if (!m_loaded)
        loadCache();

    if (!m_loaded)
        showStatus(tr("正在同步文档…"));

    if (!m_loaded || m_volumes.isEmpty())
    {
        // 没有缓存时给一段占位说明，同步成功后会被正文替换
        if (m_browser && m_browser->toPlainText().trimmed().isEmpty())
            showPlaceholder(tr("<h2>方块图书馆</h2><p>正在从 mcblockbox.top 同步使用教程、插件开发、开放平台 API 与 MCP 接入四卷文档…</p>"));
    }

    // 文档不常变：缓存命中后 6 小时内不再重复拉取；顶栏刷新 / 「同步」按钮可强制同步
    const bool stale = !m_lastFetch.isValid()
        || m_lastFetch.secsTo(QDateTime::currentDateTime()) > 6 * 3600;
    if (!m_loaded || stale)
        fetchContent();

    if (m_loaded && !m_pendingTarget.volume.isEmpty())
        applyPendingTarget();
    else if (m_loaded && m_volumeIndex < 0)
        restorePosition();

    if (m_searchEdit)
        m_searchEdit->setFocus();
}

void LibraryPage::onRefreshClicked()
{
    fetchContent();
}

void LibraryPage::loadCache()
{
    const QString cachePath = libraryCachePath();
    QFile file(cachePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;
    const QByteArray payload = file.readAll();
    file.close();
    parsePayload(payload, true);
}

void LibraryPage::fetchContent()
{
    if (m_fetching)
        return;
    if (m_network == nullptr)
        return;

    if (m_reply)
    {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    m_fetching = true;
    m_lastFetch = QDateTime::currentDateTime();
    showStatus(tr("正在同步文档…"));

    QNetworkRequest request{QUrl(contentUrl())};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BlockBox-Launcher/LibraryPage"));
    request.setTransferTimeout(15000);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_fetching = false;
        if (!reply)
            return;
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && (status == 0 || status == 200);
        if (!ok)
        {
            const QString reason = reply->errorString();
            if (m_loaded)
                showStatus(tr("同步失败（%1），当前显示本地缓存").arg(reason), true);
            else
            {
                showStatus(tr("同步失败：%1").arg(reason), true);
                showPlaceholder(tr("<h2>暂时取不到文档</h2>"
                                   "<p>请检查网络后点上方「同步」重试；离线时也可以先看软件里上次同步的内容（如果之前打开过）。</p>"));
            }
            return;
        }

        const QByteArray payload = reply->readAll();
        const bool hadContent = m_loaded;
        if (!parsePayload(payload, false))
        {
            showStatus(tr("文档格式无法解析，当前显示本地缓存"), true);
            if (!hadContent)
                showPlaceholder(tr("<h2>文档数据异常</h2><p>稍后再点「同步」重试。</p>"));
            return;
        }

        // 落盘缓存（写临时文件再替换，避免半截文件）
        const QString path = libraryCachePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(payload);
            if (!file.commit())
                qWarning() << "[Library] 写入文档缓存失败:" << path << file.errorString();
        }
        else
        {
            qWarning() << "[Library] 打开文档缓存失败:" << path << file.errorString();
        }

        showStatus(tr("已同步 · %1").arg(m_lastSynced));
        if (!hadContent && m_volumeIndex < 0)
            restorePosition();
    });
}

bool LibraryPage::parsePayload(const QByteArray &payload, bool fromCache)
{
    Q_UNUSED(fromCache);
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    const QJsonArray volumes = root.value(QStringLiteral("volumes")).toArray();
    if (volumes.isEmpty())
        return false;

    QVector<Volume> parsed;
    QVector<SearchHit> parsedSearch;

    for (const QJsonValue &value : volumes)
    {
        const QJsonObject object = value.toObject();
        Volume volume;
        volume.id = object.value(QStringLiteral("id")).toString();
        volume.file = object.value(QStringLiteral("file")).toString();
        volume.name = object.value(QStringLiteral("name")).toString();
        volume.shortName = object.value(QStringLiteral("short")).toString();
        volume.title = object.value(QStringLiteral("title")).toString();
        volume.lead = object.value(QStringLiteral("lead")).toString();
        for (const QJsonValue &chapterValue : object.value(QStringLiteral("chapters")).toArray())
        {
            const QJsonObject chapterObject = chapterValue.toObject();
            Chapter chapter;
            chapter.id = chapterObject.value(QStringLiteral("id")).toString();
            chapter.title = chapterObject.value(QStringLiteral("title")).toString();
            chapter.html = chapterObject.value(QStringLiteral("html")).toString();
            if (!chapter.id.isEmpty() && !chapter.html.isEmpty())
                volume.chapters.append(chapter);
        }
        if (!volume.id.isEmpty() && !volume.chapters.isEmpty())
            parsed.append(volume);
    }

    if (parsed.isEmpty())
        return false;

    for (const QJsonValue &value : root.value(QStringLiteral("search")).toArray())
    {
        const QJsonObject object = value.toObject();
        SearchHit hit;
        hit.volume = object.value(QStringLiteral("v")).toString();
        hit.chapterId = object.value(QStringLiteral("ci")).toString();
        hit.anchor = object.value(QStringLiteral("i")).toString();
        hit.heading = object.value(QStringLiteral("h")).toString();
        hit.chapter = object.value(QStringLiteral("c")).toString();
        hit.text = object.value(QStringLiteral("x")).toString();
        if (!hit.volume.isEmpty() && !hit.heading.isEmpty())
            parsedSearch.append(hit);
    }

    const QString generated = root.value(QStringLiteral("generated")).toString();
    m_volumes = parsed;
    m_search = parsedSearch;
    m_loaded = true;
    m_lastSynced = generated.isEmpty() ? tr("刚刚") : generated;

    // 刷新后尽量停在原来那一章
    const QString keepVolume = m_volumeIndex >= 0 && m_volumeIndex < m_volumes.size()
        ? m_volumes[m_volumeIndex].id : QString();
    const QString keepChapter = m_chapterIndex >= 0 && m_volumeIndex >= 0
        && m_volumeIndex < m_volumes.size() && m_chapterIndex < m_volumes[m_volumeIndex].chapters.size()
        ? m_volumes[m_volumeIndex].chapters[m_chapterIndex].id : QString();

    populateTree();
    populateSearch();

    // 从其它页面点了「开发文档 / 接入文档」：优先跳到指定章节
    if (!m_pendingTarget.volume.isEmpty())
    {
        applyPendingTarget();
        return true;
    }

    if (!keepVolume.isEmpty())
    {
        const int v = findVolumeById(keepVolume);
        if (v >= 0)
        {
            int c = 0;
            for (int i = 0; i < m_volumes[v].chapters.size(); ++i)
                if (m_volumes[v].chapters[i].id == keepChapter)
                    c = i;
            // 拉到的正文可能已经更新：把「当前章」清掉再定位，强制重画一次
            m_volumeIndex = -1;
            m_chapterIndex = -1;
            showChapter(v, c);
            return true;
        }
    }

    if (m_volumeIndex < 0)
        restorePosition();

    return true;
}

void LibraryPage::openAt(const QString &volumeId, const QString &chapterId, const QString &anchor)
{
    m_pendingTarget.volume = volumeId;
    m_pendingTarget.chapter = chapterId;
    m_pendingTarget.anchor = anchor;

    if (!m_loaded)
    {
        loadCache();
        if (!m_loaded)
            fetchContent();
    }

    if (m_loaded)
        applyPendingTarget();
}

void LibraryPage::applyPendingTarget()
{
    if (m_pendingTarget.volume.isEmpty() || !m_loaded || m_volumes.isEmpty())
        return;

    const PendingTarget target = m_pendingTarget;
    m_pendingTarget = {};

    int volumeIndex = findVolumeById(target.volume);
    if (volumeIndex < 0)
        volumeIndex = 0;
    int chapterIndex = 0;
    if (!target.chapter.isEmpty())
    {
        for (int c = 0; c < m_volumes[volumeIndex].chapters.size(); ++c)
        {
            if (m_volumes[volumeIndex].chapters[c].id == target.chapter)
            {
                chapterIndex = c;
                break;
            }
        }
    }
    showChapter(volumeIndex, chapterIndex, target.anchor);
}


void LibraryPage::populateTree()
{
    if (!m_tree)
        return;
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    int totalChapters = 0;
    for (const Volume &volume : m_volumes)
        totalChapters += volume.chapters.size();
    if (m_panelTitle)
        m_panelTitle->setText(tr("文档目录 · %1 篇").arg(totalChapters));
    for (int v = 0; v < m_volumes.size(); ++v)
    {
        const Volume &volume = m_volumes[v];
        auto *volumeItem = new QTreeWidgetItem(m_tree);
        volumeItem->setText(0, volume.name);
        QFont font = volumeItem->font(0);
        font.setBold(true);
        volumeItem->setFont(0, font);
        volumeItem->setData(0, Qt::UserRole, v);
        volumeItem->setFlags(volumeItem->flags() & ~Qt::ItemIsSelectable);

        for (int c = 0; c < volume.chapters.size(); ++c)
        {
            auto *chapterItem = new QTreeWidgetItem(volumeItem);
            chapterItem->setText(0, volume.chapters[c].title);
            chapterItem->setData(0, Qt::UserRole, v);
            chapterItem->setData(0, Qt::UserRole + 1, c);
        }
    }
    m_tree->expandAll();
    rebuildTabs();
}

void LibraryPage::populateSearch()
{
    if (!m_results)
        return;
    m_results->clear();
    if (m_searchEdit && !m_searchEdit->text().trimmed().isEmpty())
        runSearch(m_searchEdit->text());
}

/* ---------------- 阅读 ---------------- */
void LibraryPage::showChapter(int volumeIndex, int chapterIndex, const QString &anchor)
{
    if (volumeIndex < 0 || volumeIndex >= m_volumes.size())
        return;
    if (chapterIndex < 0 || chapterIndex >= m_volumes[volumeIndex].chapters.size())
        return;

    const bool sameChapter = (volumeIndex == m_volumeIndex && chapterIndex == m_chapterIndex);
    m_homeMode = false;
    m_volumeIndex = volumeIndex;
    m_chapterIndex = chapterIndex;
    m_pendingAnchor = anchor;
    m_lastChapterInVolume[m_volumes[volumeIndex].id] =
        m_volumes[volumeIndex].chapters[chapterIndex].id;

    if (!sameChapter)
        renderChapter();
    else if (!anchor.isEmpty())
        m_browser->scrollToAnchor(anchor);

    updateBreadcrumb();
    updateNavigation();
    persistPosition();
    syncTreeSelection();
    syncTabs();

    if (!anchor.isEmpty())
        m_browser->scrollToAnchor(anchor);
    else if (!sameChapter)
        m_browser->verticalScrollBar()->setValue(0);
}

void LibraryPage::renderChapter()
{
    if (!m_browser || m_volumeIndex < 0 || m_volumeIndex >= m_volumes.size())
        return;
    const Volume &volume = m_volumes[m_volumeIndex];
    if (m_chapterIndex < 0 || m_chapterIndex >= volume.chapters.size())
        return;

    const bool dark = ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    const Palette p = paletteFor(dark);

    QString html = volume.chapters[m_chapterIndex].html;
    // Qt 富文本没有伪元素：章标题前补一根主题色竖条
    html.replace(QRegularExpression(QStringLiteral("<h2([^>]*)>")),
                 QStringLiteral("<h2\\1><span style=\"color:%1\">▍</span> ").arg(p.brand));
    // 提示块：Qt 对 class 选择器支持有限，底色直接内联，明暗主题都能跟上
    html.replace(QStringLiteral("<blockquote class=\"tip\">"),
                 QStringLiteral("<blockquote style=\"background-color:%1;\">").arg(p.tipBg));
    html.replace(QStringLiteral("<blockquote class=\"warn\">"),
                 QStringLiteral("<blockquote style=\"background-color:%1;\">").arg(p.warnBg));

    m_browser->setHtml(html);
    applyReadingTypography();
    if (!m_pendingAnchor.isEmpty())
        m_browser->scrollToAnchor(m_pendingAnchor);
}

void LibraryPage::applyReadingTypography()
{
    if (!m_browser || !m_browser->document())
        return;
    QTextDocument *document = m_browser->document();
    QTextCursor edit(document);
    edit.beginEditBlock();
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
    {
        QTextBlockFormat format = block.blockFormat();
        format.setLineHeight(format.headingLevel() > 0 ? 128 : 158,
                             QTextBlockFormat::ProportionalHeight);
        QTextCursor cursor(block);
        cursor.setBlockFormat(format);
    }
    edit.endEditBlock();
}

QString LibraryPage::currentChapterHtml() const
{
    if (m_volumeIndex < 0 || m_volumeIndex >= m_volumes.size())
        return QString();
    const Volume &volume = m_volumes[m_volumeIndex];
    if (m_chapterIndex < 0 || m_chapterIndex >= volume.chapters.size())
        return QString();
    return volume.chapters[m_chapterIndex].html;
}

void LibraryPage::updateBreadcrumb()
{
    if (!m_crumbLabel)
        return;
    if (m_volumeIndex < 0 || m_volumeIndex >= m_volumes.size())
    {
        if (m_volumeChip)
            m_volumeChip->setVisible(false);
        if (m_timeLabel)
            m_timeLabel->clear();
        m_crumbLabel->setText(tr("方块图书馆"));
        return;
    }
    const Volume &volume = m_volumes[m_volumeIndex];
    if (m_volumeChip)
    {
        m_volumeChip->setText(volume.shortName.isEmpty() ? volume.name : volume.shortName);
        m_volumeChip->setVisible(true);
    }
    if (m_chapterIndex < 0 || m_chapterIndex >= volume.chapters.size())
    {
        m_crumbLabel->setText(volume.name);
        if (m_timeLabel)
            m_timeLabel->clear();
        return;
    }
    const Chapter &chapter = volume.chapters[m_chapterIndex];
    m_crumbLabel->setText(chapter.title);
    if (m_timeLabel)
    {
        const QString plain = QString(chapter.html).remove(QRegularExpression(QStringLiteral("<[^>]+>")));
        const int minutes = qMax(1, plain.size() / 380);
        m_timeLabel->setText(tr("约 %1 分钟").arg(minutes));
    }
}

void LibraryPage::updateNavigation()
{
    bool hasPrev = false;
    bool hasNext = false;
    if (m_volumeIndex >= 0 && m_volumeIndex < m_volumes.size())
    {
        hasPrev = (m_volumeIndex > 0) || (m_chapterIndex > 0);
        hasNext = (m_chapterIndex + 1 < m_volumes[m_volumeIndex].chapters.size())
            || (m_volumeIndex + 1 < m_volumes.size());
    }
    if (m_prevButton)
        m_prevButton->setEnabled(hasPrev);
    if (m_nextButton)
        m_nextButton->setEnabled(hasNext);
}

void LibraryPage::syncTreeSelection()
{
    if (!m_tree || m_volumeIndex < 0 || m_chapterIndex < 0)
        return;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *volumeItem = m_tree->topLevelItem(i);
        if (!volumeItem || volumeItem->data(0, Qt::UserRole).toInt() != m_volumeIndex)
            continue;
        if (m_chapterIndex < volumeItem->childCount())
        {
            QTreeWidgetItem *chapterItem = volumeItem->child(m_chapterIndex);
            const QSignalBlocker blocker(m_tree);
            m_tree->setCurrentItem(chapterItem);
            m_tree->scrollToItem(chapterItem, QAbstractItemView::PositionAtCenter);
        }
        return;
    }
}

void LibraryPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_leftPanel)
    {
        // 桌面 272px；窗口变窄（手机 / 小窗）时收窄，正文不至于只剩一条缝
        const int panelWidth = qBound(168, width() / 4, 272);
        m_leftPanel->setFixedWidth(panelWidth);
    }
    // 顶栏合成一行后空间紧张：窄窗口先收起同步状态（悬浮提示仍在按钮上）
    if (m_statusLabel)
        m_statusLabel->setVisible(width() >= 1000);
}

/* ---------------- 搜索 ---------------- */
void LibraryPage::runSearch(const QString &query)
{
    if (!m_results || !m_leftStack)
        return;
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
    {
        m_leftStack->setCurrentWidget(m_tree);
        syncTreeSelection();
        return;
    }

    const QStringList tokens = trimmed.toLower().split(QRegularExpression(QStringLiteral("\\s+")),
                                                       Qt::SkipEmptyParts);
    QVector<QPair<int, int>> hits; // index, score
    for (int i = 0; i < m_search.size(); ++i)
    {
        const SearchHit &hit = m_search[i];
        const QString haystack = (hit.heading + QLatin1Char(' ') + hit.chapter + QLatin1Char(' ')
                                  + hit.text).toLower();
        bool all = true;
        for (const QString &token : tokens)
        {
            if (!haystack.contains(token))
            {
                all = false;
                break;
            }
        }
        if (!all)
            continue;

        const QString headingLower = hit.heading.toLower();
        const QString chapterLower = hit.chapter.toLower();
        int score = 0;
        if (headingLower == trimmed.toLower())
            score += 120;
        else if (headingLower.startsWith(trimmed.toLower()))
            score += 80;
        else if (headingLower.contains(trimmed.toLower()))
            score += 50;
        if (chapterLower.contains(trimmed.toLower()))
            score += 16;
        for (const QString &token : tokens)
        {
            if (headingLower.contains(token))
                score += 22;
            if (chapterLower.contains(token))
                score += 5;
        }
        hits.append({i, score});
    }

    std::sort(hits.begin(), hits.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
        return a.second > b.second;
    });

    m_results->clear();
    const int limit = qMin(60, hits.size());
    for (int i = 0; i < limit; ++i)
    {
        const SearchHit &hit = m_search[hits[i].first];
        auto *item = new QListWidgetItem(m_results);
        QString volumeName = hit.volume;
        const int volumeIndex = findVolumeById(hit.volume);
        if (volumeIndex >= 0)
            volumeName = m_volumes[volumeIndex].name;
        const QString sub = hit.chapter.isEmpty() ? volumeName
                                                  : QStringLiteral("%1 · %2").arg(volumeName, hit.chapter);
        item->setText(QStringLiteral("%1\n%2").arg(hit.heading, sub));
        item->setData(Qt::UserRole, hits[i].first);
        item->setToolTip(hit.text.left(180));
    }
    if (limit == 0)
    {
        auto *empty = new QListWidgetItem(tr("没有找到「%1」\n换个关键词试试，或到论坛反馈").arg(trimmed), m_results);
        empty->setFlags(Qt::NoItemFlags);
    }
    m_leftStack->setCurrentWidget(m_results);
}

void LibraryPage::activateSearchRow(int row)
{
    if (!m_results || row < 0)
        return;
    QListWidgetItem *item = m_results->item(row);
    if (!item)
        return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_search.size())
        return;
    const SearchHit &hit = m_search[index];
    const int volumeIndex = findVolumeById(hit.volume);
    if (volumeIndex < 0)
        return;
    int chapterIndex = 0;
    for (int c = 0; c < m_volumes[volumeIndex].chapters.size(); ++c)
    {
        if (m_volumes[volumeIndex].chapters[c].id == hit.chapterId)
        {
            chapterIndex = c;
            break;
        }
    }
    showChapter(volumeIndex, chapterIndex, hit.anchor);
}

/* ---------------- 链接 ---------------- */
void LibraryPage::handleAnchor(const QUrl &url)
{
    // 首页里的分卷 / 继续阅读卡片：lib://<卷>#<章>
    if (url.scheme() == QStringLiteral("lib"))
    {
        const QString volumeId = url.host();
        if (volumeId.isEmpty())
        {
            showHome();
            return;
        }
        const int volumeIndex = findVolumeById(volumeId);
        if (volumeIndex < 0)
            return;
        const QString fragment = url.fragment();
        int chapterIndex = 0;
        if (!fragment.isEmpty())
        {
            const int found = findChapterByAnchor(volumeIndex, fragment);
            if (found >= 0)
                chapterIndex = found;
        }
        showChapter(volumeIndex, chapterIndex, fragment);
        return;
    }

    if (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
    {
        const QString full = url.toString();
        const QString libraryBase = QStringLiteral("https://mcblockbox.top/library/");
        if (full.startsWith(libraryBase))
        {
            openLibraryPath(full.mid(libraryBase.size()));
            return;
        }
        openExternal(url);
        return;
    }

    const QString path = url.path();
    const QString fragment = url.fragment();
    if (path.isEmpty())
    {
        if (!fragment.isEmpty())
        {
            m_browser->scrollToAnchor(fragment);
            m_pendingAnchor = fragment;
        }
        return;
    }
    if (path.endsWith(QStringLiteral(".html")))
    {
        openLibraryPath(fragment.isEmpty() ? path : path + QLatin1Char('#') + fragment);
        return;
    }
    openExternal(QUrl(librarySiteRoot() + path));
}

void LibraryPage::openLibraryPath(const QString &raw)
{
    QString path = raw;
    QString fragment;
    const int hash = path.indexOf(QLatin1Char('#'));
    if (hash >= 0)
    {
        fragment = path.mid(hash + 1);
        path = path.left(hash);
    }
    const QString file = QFileInfo(path).fileName();
    int volumeIndex = -1;
    for (int i = 0; i < m_volumes.size(); ++i)
    {
        if (m_volumes[i].file == file)
        {
            volumeIndex = i;
            break;
        }
    }
    if (volumeIndex < 0)
    {
        // 指向官网其它页面（下载页、更新日志等）：交给系统浏览器
        QString rest = path;
        while (rest.startsWith(QStringLiteral("../")))
            rest.remove(0, 3);
        openExternal(QUrl(librarySiteRoot() + rest));
        return;
    }

    int chapterIndex = 0;
    if (!fragment.isEmpty())
    {
        const int found = findChapterByAnchor(volumeIndex, fragment);
        if (found >= 0)
            chapterIndex = found;
    }
    const QString anchor = (fragment.isEmpty() || fragment == m_volumes[volumeIndex].chapters[chapterIndex].id)
        ? (fragment.isEmpty() ? QString() : fragment)
        : fragment;
    showChapter(volumeIndex, chapterIndex, anchor);
}

void LibraryPage::openExternal(const QUrl &url)
{
    if (url.isValid())
        QDesktopServices::openUrl(url);
}

int LibraryPage::findChapterByAnchor(int volumeIndex, const QString &anchor) const
{
    if (volumeIndex < 0 || volumeIndex >= m_volumes.size())
        return -1;
    const QString needle = QStringLiteral("name=\"%1\"").arg(anchor);
    for (int c = 0; c < m_volumes[volumeIndex].chapters.size(); ++c)
    {
        if (m_volumes[volumeIndex].chapters[c].html.contains(needle))
            return c;
    }
    return -1;
}

int LibraryPage::findVolumeById(const QString &id) const
{
    for (int i = 0; i < m_volumes.size(); ++i)
        if (m_volumes[i].id == id)
            return i;
    return -1;
}

/* ---------------- 状态 / 占位 / 记忆位置 ---------------- */
void LibraryPage::showStatus(const QString &text, bool warning)
{
    if (!m_statusLabel)
        return;
    QString display = text;
    if (display.size() > 34)
        display = display.left(32) + QStringLiteral("…");
    m_statusLabel->setText(display);
    m_statusLabel->setToolTip(text);
    m_statusLabel->setProperty("warning", warning);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void LibraryPage::showPlaceholder(const QString &html)
{
    if (!m_browser)
        return;
    if (m_volumeChip)
        m_volumeChip->setVisible(false);
    if (m_timeLabel)
        m_timeLabel->clear();
    if (m_crumbLabel)
        m_crumbLabel->setText(tr("方块图书馆"));
    m_browser->setHtml(html);
    applyReadingTypography();
}

void LibraryPage::persistPosition()
{
    if (m_volumeIndex < 0 || m_volumeIndex >= m_volumes.size())
        return;
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    settings.setValue(QStringLiteral("library/lastVolume"), m_volumes[m_volumeIndex].id);
    if (m_chapterIndex >= 0 && m_chapterIndex < m_volumes[m_volumeIndex].chapters.size())
        settings.setValue(QStringLiteral("library/lastChapter"),
                          m_volumes[m_volumeIndex].chapters[m_chapterIndex].id);
}

void LibraryPage::restorePosition()
{
    if (m_volumes.isEmpty())
        return;
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    QString volumeId = settings.value(QStringLiteral("library/lastVolume")).toString();
    QString chapterId = settings.value(QStringLiteral("library/lastChapter")).toString();

    // 第一次打开（还没有阅读记录）：先给一页「首页」，四卷入口一眼看清
    int volumeIndex = findVolumeById(volumeId);
    if (volumeIndex < 0)
    {
        showHome();
        return;
    }
    int chapterIndex = 0;
    for (int c = 0; c < m_volumes[volumeIndex].chapters.size(); ++c)
    {
        if (m_volumes[volumeIndex].chapters[c].id == chapterId)
        {
            chapterIndex = c;
            break;
        }
    }
    showChapter(volumeIndex, chapterIndex);
}

void LibraryPage::rebuildTabs()
{
    if (!m_tabLayout || !m_tabHost)
        return;

    if (m_tabGroup)
        m_tabGroup->deleteLater();
    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    while (QLayoutItem *item = m_tabLayout->takeAt(0))
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto addTab = [this](const QString &text, int id) {
        auto *button = new QPushButton(text, m_tabHost);
        button->setObjectName(QStringLiteral("libTab"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        m_tabGroup->addButton(button, id);
        m_tabLayout->addWidget(button);
        return button;
    };

    addTab(tr("首页"), 0);
    for (int v = 0; v < m_volumes.size(); ++v)
        addTab(m_volumes[v].name, v + 1);
    m_tabLayout->addStretch();

    connect(m_tabGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (id <= 0)
            showHome();
        else if (id - 1 < m_volumes.size())
            activateVolumeTab(m_volumes[id - 1].id);
    });

    syncTabs();
}

void LibraryPage::syncTabs()
{
    if (!m_tabGroup)
        return;
    int id = 0;
    if (!m_homeMode && m_volumeIndex >= 0 && m_volumeIndex < m_volumes.size())
        id = m_volumeIndex + 1;
    if (QAbstractButton *button = m_tabGroup->button(id))
        button->setChecked(true);
}

void LibraryPage::activateVolumeTab(const QString &volumeId)
{
    const int volumeIndex = findVolumeById(volumeId);
    if (volumeIndex < 0)
        return;
    const QString wanted = m_lastChapterInVolume.value(volumeId);
    int chapterIndex = 0;
    for (int c = 0; c < m_volumes[volumeIndex].chapters.size(); ++c)
    {
        if (m_volumes[volumeIndex].chapters[c].id == wanted)
        {
            chapterIndex = c;
            break;
        }
    }
    showChapter(volumeIndex, chapterIndex);
}

QString LibraryPage::homeHtml() const
{
    QString html = QStringLiteral("<h2>方块图书馆</h2>"
                                  "<p>方块盒子的全部文档都在这里：四卷内容随官网一起更新，"
                                  "支持跨卷搜索、章节跳转，断网时继续显示上次同步的版本。</p>");

    if (!m_volumes.isEmpty())
    {
        html += QStringLiteral("<h3>四卷文档</h3>"
                               "<table border=\"1\" cellspacing=\"0\" cellpadding=\"8\">"
                               "<thead><tr><th>分卷</th><th>内容</th><th>篇数</th></tr></thead><tbody>");
        for (const Volume &volume : m_volumes)
        {
            QString lead = volume.lead;
            if (lead.size() > 60)
                lead = lead.left(58) + QStringLiteral("…");
            html += QStringLiteral("<tr><td><a href=\"lib://%1\">%2</a></td><td>%3</td><td>%4</td></tr>")
                        .arg(volume.id.toHtmlEscaped(), volume.name.toHtmlEscaped(),
                             lead.toHtmlEscaped())
                        .arg(volume.chapters.size());
        }
        html += QStringLiteral("</tbody></table>");
    }

    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    const QString lastVolume = settings.value(QStringLiteral("library/lastVolume")).toString();
    const QString lastChapter = settings.value(QStringLiteral("library/lastChapter")).toString();
    const int volumeIndex = findVolumeById(lastVolume);
    if (volumeIndex >= 0)
    {
        const Volume &volume = m_volumes[volumeIndex];
        QString chapterTitle;
        for (const Chapter &chapter : volume.chapters)
        {
            if (chapter.id == lastChapter)
            {
                chapterTitle = chapter.title;
                break;
            }
        }
        if (!chapterTitle.isEmpty())
        {
            html += QStringLiteral("<h3>继续阅读</h3><p><a href=\"lib://%1#%2\">%3 · %4</a></p>")
                        .arg(volume.id.toHtmlEscaped(), lastChapter.toHtmlEscaped(),
                             volume.name.toHtmlEscaped(), chapterTitle.toHtmlEscaped());
        }
    }

    html += QStringLiteral("<h3>小提示</h3><p>左边目录按卷展开，点卷名可以跳到这一卷；"
                           "顶部搜索会跨四卷找关键词，结果点一下就能落到对应小节。</p>");
    return html;
}

void LibraryPage::showHome()
{
    if (!m_loaded)
        return;
    m_homeMode = true;
    m_volumeIndex = -1;
    m_chapterIndex = -1;
    m_pendingAnchor.clear();

    m_browser->setHtml(homeHtml());
    applyReadingTypography();

    if (m_volumeChip)
        m_volumeChip->setVisible(false);
    if (m_crumbLabel)
        m_crumbLabel->setText(tr("全部文档"));
    if (m_timeLabel)
        m_timeLabel->clear();
    updateNavigation();
    syncTabs();
    m_browser->verticalScrollBar()->setValue(0);
}
