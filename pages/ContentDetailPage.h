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
#include <QMap>
#include <QVector>
#include "../components/BlurLoadingOverlay.h"
#include "../components/TranslateSwitchCard.h"
#include "../utils/content/ContentData.h"
#include "../utils/translation/TranslationService.h"
#include "../utils/translation/ContentTranslationService.h"

class QParallelAnimationGroup;
class SourceSwitchBar;
class CurseForgeAPI;
class ModrinthAPI;
class ModScanner;

class ContentDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit ContentDetailPage(ContentType contentType, QWidget *parent = nullptr);
    ~ContentDetailPage();

    void setModInfo(const ModInfo &info);
    void setInstanceFilter(const QString &gameVersion, const QString &loaderType);
    /** 设置当前筛选的实例路径；模组/资源包/光影包会据此识别安装状态（已安装 / 可转换） */
    void setInstancePath(const QString &instancePath);
    /** 当前左侧筛选对应的实例版本/加载器（从列表页带入，跳转其他资源详情时沿用） */
    QString instanceGameVersion() const { return m_instanceGameVersion; }
    QString instanceLoaderType() const { return m_instanceLoaderType; }
    /** 翻译已在编辑页提交成功：按钮换成「完善翻译」（本会话内不再等服务器确认） */
    void notifyTranslationSubmitted();
    void setCurrentSource(const QString &source);
    void setReturnFolderId(const QString &folderId);

public slots:
    void receiveDetail(const ModInfo &detail);
    void setMcmodUrl(const QString &url);
    void hideLoading();

signals:
    void backToListRequested();
    void downloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile);
    /** 点击版本卡「转换」：把实例里已装的旧文件替换成该版本，oldFilePaths 为待删除的旧文件 */
    void convertRequested(const ModInfo &modInfo, const ModVersionFile &versionFile,
                          const QStringList &oldFilePaths);
    /** 点击左侧「一键安装」：安装最新正式版并自动补齐其必备前置 */
    void installLatestRequested(const ModInfo &modInfo);
    void contentClicked(const ModInfo &info);
    void addToFavoritesRequested(const ModInfo &modInfo, ContentType type, const QString &source);
    void backToFavoriteFolderRequested(const QString &folderId);
    /** 点击「问问AI」：请求跳转 AI 助手并引用当前网络资源 */
    void askAiRequested(const ResourceReference &ref);
    /** 点击「创建翻译 / 完善翻译」：请求进入翻译编辑页 */
    void translationComposeRequested(const ModInfo &modInfo);

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
    void buildInstallBlock(QVBoxLayout *parentLayout);
    void buildFilterBlock(QVBoxLayout *parentLayout);
    void buildDependenciesBlock(QVBoxLayout *parentLayout);
    void buildDownloadListBlock(QVBoxLayout *parentLayout);
    void buildLinksBlock(QVBoxLayout *parentLayout);
    /** 重建「相关链接」整行按钮（详情回包带来作者设置的链接后也用它刷新） */
    void rebuildLinkButtons();
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

    struct DownloadCardEntry {
        QStringList gameVersions;
        QStringList loaders;
        QString releaseType;
        QWidget *widget;
        ModVersionFile vf;                 // 卡片对应的版本文件
        QPushButton *actionBtn = nullptr;  // 右侧动作按钮（下载/转换/已安装）
        int action = ActionDownload;
        QStringList oldFilePaths;          // Convert 态：实例中待替换的旧文件
    };

    // ── 安装状态识别：下载列表每个版本按钮的三态（下载 / 转换 / 已安装）──
    enum CardAction {
        ActionDownload = 0,   // 未安装：普通下载按钮
        ActionConvert = 1,    // 已装该项目但非此版本：转换为该版本
        ActionInstalled = 2   // 该版本文件已安装：对号，不可点击
    };
    /** 该内容类型是否支持按文件识别安装状态（模组/资源包/光影包；整合包等安装形态特殊不做） */
    bool supportsInstallStateCheck() const;
    /** 实例下用于比对的目录（mods/resourcepacks/shaderpacks） */
    QString installCheckFolder() const;
    QStringList installCheckNameFilters() const;
    void startInstallStateCheck();
    void onInstallScanCompleted(const LocalModList &list);
    /** 用已扫描到的本地文件按当前来源批量匹配在线项目（Modrinth 哈希 / CurseForge 指纹） */
    void issueProjectMatch();
    void applyInstallStates();
    void setCardAction(DownloadCardEntry &entry, CardAction action,
                       const QStringList &oldFilePaths = QStringList());
    /** 当前资源的规范项目 ID（详情回包带来的 id，兼容从分享链接 slug 进入） */
    QString currentProjectId() const;

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

    // ── 社区翻译（方块盒子服务器上其他用户的翻译，审核通过后自动覆盖显示）──
    void fetchCommunityTranslation();
    void applyCommunityTranslation();
    QString translationComposeButtonLabel() const;
    void updateTranslationComposeButtonLabel();

    enum TranslateChunkKind {
        TranslateSummary = 0,          // 只翻译顶部简介
        TranslateDetail = 1,           // 只翻译详细信息
        TranslateSummaryAndDetail = 2  // 简介与详细信息同源，一次翻译两处共用
    };

    // 分页相关
    static const int kDownloadPageSize = 30;
    int m_downloadCurrentPage;
    QWidget *m_downloadPaginationBar;
    QPushButton *m_downloadPrevBtn;
    QPushButton *m_downloadNextBtn;
    QLabel *m_downloadPageLabel;
    QLabel *m_downloadEmptyHint;   // 筛选后没有可下载版本时的提示
    /** 该类资源是否存在"加载器"概念（资源包/数据包/光影包/世界都没有） */
    bool supportsLoaderFilter() const;
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

    // ── 安装状态识别（按实例目录扫描 + 哈希/指纹批量匹配）──
    QString m_instancePath;                  // 当前筛选的实例（空 = 未筛选，不识别）
    QString m_canonicalProjectId;            // 详情回包的规范项目 id
    ModScanner *m_installScanner = nullptr;
    LocalModList m_installedMods;            // 实例目录扫描结果
    QString m_checkInstancePath;             // 已装数据对应的实例
    QString m_matchedSource;                 // 匹配数据对应的来源（换源后需重发匹配）
    bool m_scanRequested = false;            // 是否已有扫描在途
    int m_checkGeneration = 0;               // 世代计数，防止旧扫描/匹配结果串台
    int m_pendingScanGeneration = 0;
    QMap<QString, ModVersionFile> m_hashMatches;   // sha1 → 匹配到的在线版本（Modrinth）
    QMap<quint32, QString> m_cfFingerprintModIds;  // 文件指纹 → 项目 ID（CurseForge）

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
    QPushButton *m_installBtn = nullptr;   // 「一键安装」按钮（在筛选选项之上）
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

    QPushButton *m_translationComposeBtn = nullptr; ///< 「创建翻译 / 完善翻译」动作按钮
    bool m_hasCommunityTranslation = false;
    bool m_hasMyTranslation = false;
    QString m_communitySummary;
    QString m_communityDetailHtml;
    QString m_translationFetchContext; ///< 发起拉取时的资源标识，回包时比对防串页
};

#endif