/**
 * @file   FabricVersionListPage.h
 * @brief  Fabric版本列表页面类声明
 * @author BlockBox Team
 * @date   2026-05-28
 */
#ifndef FABRICVERSIONLISTPAGE_H
#define FABRICVERSIONLISTPAGE_H

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

#include "utils/fabric/FabricInstaller.h"

class BlurLoadingOverlay;

class FabricVersionListPage : public QWidget
{
    Q_OBJECT

public:
    explicit FabricVersionListPage(QWidget *parent = nullptr);
    ~FabricVersionListPage();

public slots:
    void setMinecraftVersion(const QString &mcVersion);
    void loadFabricVersions();

signals:
    void backToLoaderDetailRequested();
    void fabricVersionSelected(const QString &mcVersion, const QString &fabricVersion);

private slots:
    void onVersionItemClicked(QTreeWidgetItem *item, int column);
    void onInstallClicked();
    void onInstallProgress(int progress, const QString &status);
    void onInstallCompleted(const QString &version);
    void onInstallFailed(const QString &error);
    void onDownloadProgressUpdated(qint64 bytesReceived, qint64 bytesTotal);
    void onStatusChanged(const QString &status);
    void onVersionListFetched(const QList<FabricVersionInfo> &versions);
    void onVersionListFetchFailed(const QString &error);

private:
    void initUI();
    void populateVersionList();
    void updateInstallButtonState(bool enabled, const QString &text);

    QVBoxLayout *m_mainLayout;
    QLabel *m_titleLabel;
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_statusLabel;
    QTreeWidget *m_versionList;

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;

    QString m_minecraftVersion;
    QList<FabricVersionInfo> m_fabricVersions;
    FabricVersionInfo m_selectedVersion;
    bool m_hasSelection;

    QPushButton *m_installButton;
    QPushButton *m_cancelButton;
    QLabel *m_statusInfoLabel;
    QProgressBar *m_installProgressBar;

    bool m_isInstalling;
};

#endif // FABRICVERSIONLISTPAGE_H
