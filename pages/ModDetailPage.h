#ifndef MODDETAILPAGE_H
#define MODDETAILPAGE_H

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
#include "../utils/mod/ModData.h"
#include "../utils/content/ContentData.h"
#include "../utils/translation/TranslationService.h"

class QParallelAnimationGroup;
class SourceSwitchBar;
class CurseForgeAPI;
class ModrinthAPI;

class ModDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit ModDetailPage(QWidget *parent = nullptr);
    ~ModDetailPage();

    void setModInfo(const ModInfo &info);
    void setInstanceFilter(const QString &gameVersion, const QString &loaderType);
    ModInfo currentModInfo() const { return m_modInfo; }
    QString instanceGameVersion() const { return m_instanceGameVersion; }
    QString instanceLoaderType() const { return m_instanceLoaderType; }

public slots:
    void receiveDetail(const ModInfo &detail);
    void setMcmodUrl(const QString &url);
    void hideLoading();

signals:
    void backToListRequested();
    void downloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile);
    /** 用户点击顶部来源切换，请求拉取另一来源的详情（source: "modrinth"/"curseforge"） */
    void sourceSwitchRequested(const QString &source);
    /** 点击「问问AI」：请求跳转 AI 助手并引用当前网络模组 */
    void askAiRequested(const ResourceReference &ref);
    /** 点击左侧「前置模组」里的条目：请求跳转到该前置资源自己的详情页 */
    void contentClicked(const ModInfo &modInfo);

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

    // ── 前置模组：详情返回后重建，并按 id 批量取名称与图标 ──
    void initAPI();
    void applyMCIMSetting();
    void refreshDependencies();
    void resolveDependencyInfos();
    void onDependencyInfosResolved(const QList<ModInfo> &infos);
    void loadDepIcon(QLabel *iconLabel, const QString &iconUrl);
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
    /** 在版本下拉里选中指定版本（不存在时补入），用于列表页带入的实例筛选 */
    void selectVersionFilter(const QString &gameVersion);

    QWidget *createChip(const QString &text, const QColor &bgColor = QColor("#e8e8e8"));
    void addSectionShadow(QWidget *section);

    struct DownloadCardEntry {
        QStringList gameVersions;
        QStringList loaders;
        QString releaseType;
        QWidget *widget;
    };

    ModInfo m_modInfo;
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

    // Left sidebar
    QWidget *m_leftPanel;
    QComboBox *m_versionCombo;
    QComboBox *m_loaderCombo;
    QComboBox *m_versionTypeCombo;
    QWidget *m_dependenciesContainer;
    QVBoxLayout *m_dependenciesLayout;
    QWidget *m_dependenciesSection;          // 「前置模组」整块（无前置时隐藏）
    CurseForgeAPI *m_curseforgeAPI;          // 批量查询前置资源用
    ModrinthAPI *m_modrinthAPI;
    QHash<QString, ModInfo> m_depResolved;   // 前置资源 id → 基础信息
    QStringList m_depResolvingIds;           // 正在批量查询的前置 id
    QWidget *m_downloadContainer;
    QVBoxLayout *m_downloadListLayout;
    QList<DownloadCardEntry> m_downloadCards;

    // 分页相关
    static const int kDownloadPageSize = 30;
    int m_downloadCurrentPage;
    QWidget *m_downloadPaginationBar;
    QPushButton *m_downloadPrevBtn;
    QPushButton *m_downloadNextBtn;
    QLabel *m_downloadPageLabel;
    void updateDownloadPagination();
    void onDownloadPrevPage();
    void onDownloadNextPage();

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

    // ── 右下角悬浮翻译卡片（EN 原文 / 中 AI 翻译当前页）──
    void onTranslateModeChanged(bool chinese);
    void startPageTranslation();
    void translateNextChunk();
    void onTranslateChunkFinished(const QString &text);
    void applyTranslatedTexts();
    void restoreOriginalTexts();
    void setTranslateBusy(bool busy);
    void resetTranslation();

    /// 翻译任务种类：简介与详细描述可能只翻其中一处
    enum TranslateChunkKind {
        TranslateSummary = 0,          // 只翻译顶部简介
        TranslateDetail = 1,           // 只翻译详细信息
        TranslateSummaryAndDetail = 2  // 简介与详细信息同源，一次翻译两处共用
    };

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
