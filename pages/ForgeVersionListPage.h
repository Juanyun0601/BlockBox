/**
 * @file   ForgeVersionListPage.h
 * @brief  Forge版本列表页面类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef FORGEVERSIONLISTPAGE_H
#define FORGEVERSIONLISTPAGE_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

class BlurLoadingOverlay;
class ForgeInstaller;

struct ForgeVersionInfo {
    QString version;
    QString mcversion;
    QString branch;
    int build;
    QString modified;
    bool hasInstaller;
    bool isAvailable;
    QString fileName;
};

class ForgeVersionListPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForgeVersionListPage(QWidget *parent = nullptr);
    ~ForgeVersionListPage();

public slots:
    void setMinecraftVersion(const QString &mcVersion);
    void setLoaderName(const QString &loaderName);

signals:
    void backToLoaderDetailRequested();
    void forgeVersionSelected(const QString &loaderName, const QString &mcVersion, const QString &forgeVersion);

private slots:
    void onVersionItemClicked(QTreeWidgetItem *item, int column);
    void onInstallClicked();
    void onInstallProgress(int progress, const QString &status);
    void onInstallCompleted(const QString &version);
    void onInstallFailed(const QString &error);
    void onForgeInstallStarted(const QString &taskId);
    void onForgeInstallProgress(const QString &taskId, const QString &stage, int percent);
    void onForgeInstallCompleted(const QString &taskId);
    void onForgeInstallFailed(const QString &taskId, const QString &error);
    void onForgeInstallCancelled(const QString &taskId);

private:
    void initUI();
    void loadForgeVersions();
    void loadNeoForgeVersions();
    void loadOptiFineVersions();
    void loadFabricVersions();
    void loadQuiltVersions();
    void loadLegacyFabricVersions();
    void loadLiteLoaderVersions();
    void loadCleanroomVersions();
    void loadOptiFabricVersions();
    void populateVersionList();
    void populateNeoForgeVersionList(const QJsonArray &versions);
    void populateOptiFineVersionList(const QJsonArray &versions);
    void populateFabricVersionList(const QJsonArray &versions);
    void populateQuiltVersionList(const QJsonArray &versions);
    void populateLegacyFabricVersionList(const QJsonArray &versions);
    void populateLiteLoaderVersionList(const QJsonObject &versionData);
    void populateCleanroomVersionList(const QJsonArray &versions);
    void populateOptiFabricVersionList(const QJsonArray &versions);
    QString getLoaderDisplayName() const;

    QVBoxLayout *m_mainLayout;
    QLabel *m_titleLabel;
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_statusLabel;
    QTreeWidget *m_versionList;

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;

    QString m_minecraftVersion;
    QString m_loaderName;
    QList<ForgeVersionInfo> m_forgeVersions;
    ForgeVersionInfo m_selectedVersion;
    bool m_hasSelection;
    
    QPushButton *m_installButton;
    QPushButton *m_cancelButton;
    QLabel *m_statusInfoLabel;
    QProgressBar *m_installProgressBar;

    bool m_isInstalling;

    ForgeInstaller *m_forgeInstaller;
};

#endif // FORGEVERSIONLISTPAGE_H
