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
#include <QVector>
#include "../components/BlurLoadingOverlay.h"
#include "../utils/mod/ModData.h"

class QParallelAnimationGroup;

class ModDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit ModDetailPage(QWidget *parent = nullptr);
    ~ModDetailPage();

    void setModInfo(const ModInfo &info);
    void setInstanceFilter(const QString &gameVersion, const QString &loaderType);

public slots:
    void receiveDetail(const ModInfo &detail);
    void setMcmodUrl(const QString &url);
    void hideLoading();

signals:
    void backToListRequested();
    void downloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    void clearContent();
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
};

#endif
