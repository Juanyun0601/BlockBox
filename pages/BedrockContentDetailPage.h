#ifndef BEDROCKCONTENTDETAILPAGE_H
#define BEDROCKCONTENTDETAILPAGE_H

#include <QWidget>
#include <QList>
#include <QString>
#include <QPixmap>

#include "utils/mod/ModData.h"
#include "utils/bedrock/BedrockInstanceManager.h"
#include "utils/content/ContentData.h"

class QComboBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QScrollArea;
class QTextBrowser;
class QVBoxLayout;
class QHBoxLayout;
class QNetworkAccessManager;
class CurseForgeAPI;
class BlurLoadingOverlay;
class MultiThreadDownloader;

/**
 * @brief 基岩版社区资源详情页（CurseForge 源）
 *
 * 与 Java 版 ContentDetailPage 同风格：头部 + 左侧下载区 + 右侧截图/链接/标签/描述。
 * 支持选择版本「下载并安装」到当前激活的基岩版实例（com.mojang）。
 * 头部提供收藏按钮，可将资源收藏到基岩版独立收藏夹（favorites_bedrock.json）。
 */
class BedrockContentDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockContentDetailPage(QWidget *parent = nullptr);
    ~BedrockContentDetailPage() override;

    void setModInfo(const ModInfo &info);

    /** 设置基岩版 CurseForge 分类 classId（影响收藏时的分类存储） */
    void setBedrockClassId(int classId);
    /** 设置基岩版分类显示名（资源包/皮肤/地图/脚本/附加包） */
    void setBedrockCategory(const QString &category);

    static constexpr const char *MCIM_BASE = "https://mod.mcimirror.top";
    static constexpr const char *CF_BASE   = "https://api.curseforge.com";

public slots:
    /** 收藏状态变化后刷新星标按钮图标与提示 */
    void refreshFavoriteState();

signals:
    /** 用户在收藏菜单点击「新建收藏夹...」时触发，由 MainWindow 弹窗处理 */
    void addToFavoritesRequested(const ModInfo &modInfo, ContentType type, const QString &source,
                                 int bedrockClassId, const QString &bedrockCategory);

private slots:
    void onDetailReceived(const ModInfo &detail);
    void onDetailFailed(const QString &error);
    void showFavoriteMenu();

private:
    void initUI();
    void initAPI();
    void clearContent();
    void buildHeader();
    void buildDownloadBlock();
    void buildLinksBlock();
    void buildTagsBlock();
    void buildDescriptionBlock();
    void buildScreenshotBlock();
    void updateFavoriteButtonIcon();

    QWidget *createDownloadCard(const ModVersionFile &vf);
    QLabel *createChip(const QString &text);
    void startDownloadAndInstall(const ModVersionFile &vf);
    void doInstall(const QString &filePath);
    QString comMojangDir() const;
    static QString formatFileSize(qint64 bytes);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_contentWidget = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;

    QWidget *m_headerWidget = nullptr;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_descLabel = nullptr;
    QPushButton *m_favoriteBtn = nullptr;

    QWidget *m_leftPanel = nullptr;
    QVBoxLayout *m_leftLayout = nullptr;
    QWidget *m_downloadContainer = nullptr;
    QVBoxLayout *m_downloadListLayout = nullptr;

    QWidget *m_rightPanel = nullptr;
    QVBoxLayout *m_rightLayout = nullptr;
    QWidget *m_linksContainer = nullptr;
    QHBoxLayout *m_linksLayout = nullptr;
    QWidget *m_tagsContainer = nullptr;
    QTextBrowser *m_descriptionBrowser = nullptr;
    QWidget *m_screenshotSection = nullptr;
    QWidget *m_screenshotStrip = nullptr;

    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;
    QPushButton *m_openFolderBtn = nullptr;

    CurseForgeAPI *m_curseforgeAPI = nullptr;
    MultiThreadDownloader *m_downloader = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;

    ModInfo m_modInfo;
    QString m_comMojangDir;
    QList<ModVersionFile> m_versionFiles;
    QList<QWidget *> m_downloadCards;
    QList<QPixmap> m_screenshotPixmaps;
    QString m_activeTaskId;
    BlurLoadingOverlay *m_loadingOverlay = nullptr;

    QString m_currentSource = QStringLiteral("curseforge");
    QString m_currentItemId;
    ContentType m_contentType = ContentType::ResourcePack;
    int m_bedrockClassId = 0;
    QString m_bedrockCategory;
};

#endif // BEDROCKCONTENTDETAILPAGE_H
