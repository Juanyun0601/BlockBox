#ifndef CONTENTDETAILPAGE_H
#define CONTENTDETAILPAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include "../utils/IconHelper.h"
#include "../utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QTextBrowser>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QEvent>
#include <QHash>
#include <QVector>
#include "../components/BlurLoadingOverlay.h"
#include "../components/TranslateSwitchCard.h"
#include "../utils/content/ContentData.h"
#include "../utils/translation/TranslationService.h"

class QParallelAnimationGroup;
class SourceSwitchBar;
class CurseForgeAPI;
class ModrinthAPI;

class ContentDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit ContentDetailPage(ContentType contentType, QWidget *parent = nullptr);
    ~ContentDetailPage();

    void setModInfo(const ModInfo &info);
    void setInstanceFilter(const QString &gameVersion, const QString &loaderType);
    /** 当前左侧筛选对应的实例版本/加载器（从列表页带入，跳转其他资源详情时沿用） */
    QString instanceGameVersion() const { return m_instanceGameVersion; }
    QString instanceLoaderType() const { return m_instanceLoaderType; }
    void setCurrentSource(const QString &source);
    void setReturnFolderId(const QString &folderId);

public slots:
    void receiveDetail(const ModInfo &detail);
    void setMcmodUrl(const QString &url);
    void hideLoading();

signals:
    void backToListRequested();
    void downloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile);
    void contentClicked(const ModInfo &info);
    void addToFavoritesRequested(const ModInfo &modInfo, ContentType type, const QString &source);
    void backToFavoriteFolderRequested(const QString &folderId);
    /** 点击「问问AI」：请求跳转 AI 助手并引用当前网络资源 */
    void askAiRequested(const ResourceReference &ref);

public slots:
    void refreshFavoriteState();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    void clearContent();
    QWidget *buildSourceSwitch();
    void switchToSource(const QString &source);
    void buildHeader();
    void buildLeftSidebar();
    void buildRightMain();
    void buildFilterBlock(QVBoxLayout *parentLayout);
    void buildDependenciesBlock(QVBoxLayout *parentLayout);
    void buildDownloadListBlock(QVBoxLayout *parentLayout);
    void buildLinksBlock(QVBoxLayout *parentLayout);
    void buildTagsBlock(QVBoxLayout *parentLayout);
    void buildDescriptionBlock(QVBoxLayout *parentLayout);
    QWidget *createDownloadCard(const ModVersionFile &vf);
    void loadIcon();
    void buildScreenshotBlock(QVBoxLayout *parentLayout);
    void loadScreenshots(const QStringList &urls);
    void showScreenshot(int index);
    void updateThumbnail(int index);
    void updateThumbnailHighlight();
    QPixmap scaledForDisplay(const QPixmap &px);
    void updateScreenshotNavStyle();
    void startCarousel();
    void stopCarousel();
    void onScreenshotInteraction();
    void applyDownloadFilter();

    QWidget *createChip(const QString &text, const QColor &bgColor = QColor("#e8e8e8"));
    void showFavoriteMenu();
    void updateFavoriteButtonIcon();

    // ── 详情拉取与左侧「前置资源」 ──
    void initAPI();
    void applyMCIMSetting();
    /** 按当前 source/id 拉取完整详情（版本列表、前置资源随详情返回） */
    void fetchDetail();
    /** 详情拉取失败：收起加载遮罩并提示 */
    void onDetailFailed(const QString &error);
    /** 重建左侧前置资源卡片（列表变化或详情返回后调用） */
    void refreshDependencies();
    /** 前置资源只知道 id，批量取一次名称/图标，让卡片能显示并点击跳转 */
    void resolveDependencyInfos();
    void onDependencyInfosResolved(const QList<ModInfo> &infos);
    void loadDepIcon(QLabel *iconLabel, const QString &iconUrl);

    // ── 右下角悬浮翻译切换卡片（EN / 中） ──
    void onTranslateModeChanged(bool chinese);
    void startPageTranslation();
    void translateNextChunk();
    void onTranslateChunkFinished(const QString &text);
    void applyTranslatedTexts();
    void restoreOriginalTexts();
    void setTranslateBusy(bool busy);
    void resetTranslation();

    enum TranslateChunkKind {
        TranslateSummary = 0,          // 只翻译顶部简介
        TranslateDetail = 1,           // 只翻译详细信息
        TranslateSummaryAndDetail = 2  // 简介与详细信息同源，一次翻译两处共用
    };

    struct DownloadCardEntry {
        QStringList gameVersions;
        QStringList loaders;
        QString releaseType;
        QWidget *widget;
    };

    // 分页相关
    static const int kDownloadPageSize = 30;
    int m_downloadCurrentPage;
    QWidget *m_downloadPaginationBar;
    QPushButton *m_downloadPrevBtn;
    QPushButton *m_downloadNextBtn;
    QLabel *m_downloadPageLabel;
    QLabel *m_downloadEmptyHint;   // 筛选后没有可下载版本时的提示
    void updateDownloadPagination();
    void onDownloadPrevPage();
    void onDownloadNextPage();

    ContentType m_contentType;
    ModInfo m_modInfo;
    QString m_currentSource;     // "curseforge" / "modrinth" - 当前条目来源
    QString m_currentItemId;     // 全局唯一 itemId: source:remoteId
    QString m_returnFolderId;    // 如果是从收藏夹跳转进入，记录返回的分组 ID
    QString m_instanceGameVersion;
    QString m_instanceLoaderType;

    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QVBoxLayout *m_contentLayout;
    SourceSwitchBar *m_sourceSwitchBar;

    // Header
    QWidget *m_headerWidget;
    QLabel *m_iconLabel;
    QLabel *m_chineseNameLabel;
    QLabel *m_englishNameLabel;
    QLabel *m_descriptionLabel;
    QPushButton *m_favoriteBtn;

    // Left sidebar
    QWidget *m_leftPanel;
    QComboBox *m_versionCombo;
    QComboBox *m_loaderCombo;
    QComboBox *m_versionTypeCombo;
    QWidget *m_dependenciesContainer;
    QVBoxLayout *m_dependenciesLayout;
    QWidget *m_dependenciesSection;   // 前置资源整块（无前置时整块隐藏）
    QWidget *m_downloadContainer;
    QVBoxLayout *m_downloadListLayout;
    QList<DownloadCardEntry> m_downloadCards;

    // Right main
    QWidget *m_linksContainer;
    QHBoxLayout *m_linksLayout;
    QWidget *m_tagsContainer;
    QWidget *m_descriptionContainer;
    QTextBrowser *m_descriptionBrowser;

    // Screenshots
    QWidget *m_screenshotSection;
    QWidget *m_screenshotContainer;
    QWidget *m_screenshotStack;
    QLabel *m_screenshotLabel;
    QLabel *m_screenshotOverlay;
    bool m_screenshotAnimating;
    QParallelAnimationGroup *m_screenshotAnimGroup;
    QPushButton *m_prevScreenshotBtn;
    QPushButton *m_nextScreenshotBtn;
    QWidget *m_thumbnailStrip;
    QScrollArea *m_thumbScrollArea;
    QPushButton *m_thumbScrollLeftBtn;
    QPushButton *m_thumbScrollRightBtn;
    QStringList m_screenshotUrls;
    QVector<QPixmap> m_screenshotPixmaps;
    int m_currentScreenshotIndex;
    QTimer *m_carouselTimer;
    QTimer *m_carouselResumeTimer;

    QNetworkAccessManager *m_networkManager;

    BlurLoadingOverlay *m_loadingOverlay;

    // 详情拉取（CurseForge / Modrinth）与前置资源解析
    CurseForgeAPI *m_curseforgeAPI = nullptr;
    ModrinthAPI *m_modrinthAPI = nullptr;
    QString m_pendingDetailSource;           // 正在等待详情的来源（空=没有等待中的请求）
    QString m_detailFetchedKey;              // 已经拉过详情的 "source:id"
    QHash<QString, ModInfo> m_depResolved;   // 前置资源 id → 基础信息
    QStringList m_depResolvingIds;           // 正在批量查询的前置 id

    // 右下角悬浮翻译卡片（EN 原文 / 中 AI 翻译）
    TranslateSwitchCard *m_translateSwitch = nullptr;
    TranslationService *m_translator = nullptr;
    QStringList m_translateTexts;
    QVector<int> m_translateKinds;
    int m_translateChunkIndex = 0;
    bool m_translating = false;
    bool m_translated = false;
    QString m_origSummary;
    QString m_origDetailHtml;
    QString m_zhSummary;
    QString m_zhDetailHtml;
};

#endif