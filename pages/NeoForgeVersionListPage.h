/**
 * @file   NeoForgeVersionListPage.h
 * @brief  NeoForge版本列表页面类声明
 * @author BlockBox Team
 * @date   2026-05-29
 */
#pragma once

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

#include "utils/NeoForgeInstaller.h"

class BlurLoadingOverlay;

struct NeoForgeVersionInfo
{
    QString version;
    QString mcversion;
};

class NeoForgeVersionListPage : public QWidget
{
    Q_OBJECT

public:
    explicit NeoForgeVersionListPage(QWidget *parent = nullptr);
    ~NeoForgeVersionListPage();

public slots:
    void setMinecraftVersion(const QString &mcVersion);

signals:
    void backToLoaderDetailRequested();
    void neoForgeVersionSelected(const QString &mcVersion, const QString &neoForgeVersion);

private slots:
    void onVersionItemClicked(QTreeWidgetItem *item, int column);
    void onInstallClicked();
    void onInstallProgress(int progress, const QString &status);
    void onInstallCompleted(const QString &version);
    void onInstallFailed(const QString &error);

private:
    void initUI();
    void loadNeoForgeVersions();

    QVBoxLayout *m_mainLayout;
    QLabel *m_titleLabel;
    BlurLoadingOverlay *m_loadingOverlay;
    QLabel *m_statusLabel;
    QTreeWidget *m_versionList;

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;

    QString m_minecraftVersion;
    QList<NeoForgeVersionInfo> m_versions;
    NeoForgeVersionInfo m_selectedVersion;
    bool m_hasSelection;

    QPushButton *m_installButton;
    QPushButton *m_cancelButton;
    QLabel *m_statusInfoLabel;
    QProgressBar *m_installProgressBar;

    bool m_isInstalling;
};