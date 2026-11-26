/**
 * @file   LibraryPage.h
 * @brief  方块图书馆 · 启动器内置整页
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 不依赖外部浏览器：四卷文档（使用教程 / 插件开发 / 开放平台 API / MCP 接入）
 * 直接渲染在软件里。
 *
 *   - 文档包来自 https://mcblockbox.top/library/assets/library-content.json，
 *     由官网构建脚本（tools/build-library.mjs）从四卷正文生成，按「章」切好；
 *   - 拉到的包缓存到本地数据目录，断网时继续显示上次同步的内容；
 *   - 左侧按分卷列出章节，顶部搜索框跨卷检索（命中到小节，点击直接跳到那一段）；
 *   - 正文用 QTextBrowser 渲染，明暗主题跟随启动器，站内链接在页内跳转，
 *     论坛 / 官网等站外链接才交给系统浏览器。
 */
#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QFrame;
class QButtonGroup;
class QHBoxLayout;
class QNetworkAccessManager;
class QNetworkReply;
class QResizeEvent;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QTreeWidget;
class QUrl;

class LibraryPage : public QWidget
{
    Q_OBJECT

public:
    explicit LibraryPage(QWidget *parent = nullptr);
    ~LibraryPage() override;

    /** 进入本页：首次进入加载文档包（先本地缓存、再联网同步） */
    void open();

    /** 顶栏「刷新」：强制从线上重新同步一次 */
    Q_INVOKABLE void onRefreshClicked();

    /**
     * 从其它页面直接跳到某一卷 / 某一章（可选小节锚点）。
     * 文档还没加载时先记下来，等缓存或同步完成后自动跳过去。
     */
    void openAt(const QString &volumeId, const QString &chapterId = QString(),
                const QString &anchor = QString());

    /** 文档包地址：默认线上地址，可用环境变量 BLOCKBOX_LIBRARY_URL 覆盖（本地联调用） */
    static QString contentUrl();

protected:
    /** 窄窗口时收窄左侧目录，给正文留出空间 */
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Chapter
    {
        QString id;
        QString title;
        QString html;
    };

    struct Volume
    {
        QString id;
        QString file;
        QString name;
        QString shortName;
        QString title;
        QString lead;
        QVector<Chapter> chapters;
    };

    struct SearchHit
    {
        QString volume;
        QString chapterId;
        QString anchor;
        QString heading;
        QString chapter;
        QString text;
    };

    void buildUi();
    void applyTheme();
    void loadCache();
    void fetchContent();
    bool parsePayload(const QByteArray &payload, bool fromCache);
    void populateTree();
    void populateSearch();
    void showChapter(int volumeIndex, int chapterIndex, const QString &anchor = QString());
    void renderChapter();
    void updateNavigation();
    void syncTreeSelection();
    void applyPendingTarget();
    void updateBreadcrumb();
    /** 顶栏分卷切换：首页 + 四卷 */
    void rebuildTabs();
    void syncTabs();
    void showHome();
    void activateVolumeTab(const QString &volumeId);
    QString homeHtml() const;
    void applyReadingTypography();
    void runSearch(const QString &query);
    void activateSearchRow(int row);
    void handleAnchor(const QUrl &url);
    void openLibraryPath(const QString &raw);
    void openExternal(const QUrl &url);
    void showStatus(const QString &text, bool warning = false);
    void showPlaceholder(const QString &html);
    int findChapterByAnchor(int volumeIndex, const QString &anchor) const;
    int findVolumeById(const QString &id) const;
    QString currentChapterHtml() const;
    void persistPosition();
    void restorePosition();

    QVector<Volume> m_volumes;
    QVector<SearchHit> m_search;
    int m_volumeIndex = -1;
    int m_chapterIndex = -1;
    QString m_pendingAnchor;
    struct PendingTarget
    {
        QString volume;
        QString chapter;
        QString anchor;
    };
    PendingTarget m_pendingTarget;

    QDateTime m_lastFetch;
    bool m_loaded = false;
    bool m_fetching = false;
    QString m_lastSynced;

    QWidget *m_tabHost = nullptr;
    QHBoxLayout *m_tabLayout = nullptr;
    QButtonGroup *m_tabGroup = nullptr;
    bool m_homeMode = false;
    QHash<QString, QString> m_lastChapterInVolume;

    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QFrame *m_leftPanel = nullptr;
    QStackedWidget *m_leftStack = nullptr;
    QTreeWidget *m_tree = nullptr;
    QListWidget *m_results = nullptr;
    QLabel *m_volumeChip = nullptr;
    QLabel *m_crumbLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_panelTitle = nullptr;
    QTextBrowser *m_browser = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
};
