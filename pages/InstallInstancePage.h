/**
 * @file   InstallInstancePage.h
 * @brief  安装实例页面类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef INSTALLINSTANCEPAGE_H
#define INSTALLINSTANCEPAGE_H

#include <QComboBox>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

class BlurLoadingOverlay;

class InstallInstancePage : public QWidget
{
    Q_OBJECT

public:
    explicit InstallInstancePage(QWidget *parent = nullptr);
    ~InstallInstancePage();

public slots:
    void onSourceChanged(int id);
    void onVersionTypeFilterChanged(int id);
    void onRefreshVersions();
    void onModifyExistingInstanceClicked();
    void enterModifyMode(const QString &instancePath, const QString &instanceName,
                         const QString &version, const QString &loader);
    void exitModifyMode();

signals:
    void backToMainRequested();
    void instanceInstalled(const QString &instancePath);
    void versionSelected(const QString &versionId, const QString &versionType);
    void modifyExistingInstanceRequested();

    // 修改现有实例时，携带目标实例信息继续版本选择流程
    void modifyVersionSelected(const QString &versionId, const QString &versionType,
                               const QString &instancePath, const QString &instanceName);

public:
    bool modifyMode() const { return m_modifyMode; }
    QString modifyInstancePath() const { return m_modifyInstancePath; }
    QString modifyInstanceName() const { return m_modifyInstanceName; }
    QString modifyInstanceVersion() const { return m_modifyInstanceVersion; }
    QString modifyInstanceLoader() const { return m_modifyInstanceLoader; }

protected:
    void showEvent(QShowEvent *event) override;


private:
    void initUI();
    void initNetworkManager();
    void getVersionsFromOfficial();
    void getVersionsFromBMCL();
    void parseVersionsResponse(const QJsonDocument &doc);
    void populateVersionList();
    void rebuildVersionCards();
    void createVersionListCard(const QString &id, const QString &type,
                                const QString &dateString, const QString &typeDisplay);

    // UI components
    QVBoxLayout *m_mainLayout;
    
    // 修改现有实例：模式开关 + 目标实例信息
    QWidget *m_modifyBanner;
    QHBoxLayout *m_modifyBannerLayout;
    QLabel *m_modifyBannerLabel;
    QPushButton *m_modifyInstanceBtn;
    QPushButton *m_exitModifyBtn;
    bool m_modifyMode;
    QString m_modifyInstancePath;
    QString m_modifyInstanceName;
    QString m_modifyInstanceVersion;
    QString m_modifyInstanceLoader;
    
    // Filter bar with dropdowns
    QWidget *m_filterBar;
    QHBoxLayout *m_filterBarLayout;
    QComboBox *m_sourceCombo;
    QComboBox *m_filterCombo;
    
    // Version list
    QListWidget *m_versionList;
    
    BlurLoadingOverlay *m_loadingOverlay;
    
    // Network
    QNetworkAccessManager *m_networkManager;
    
    // Data
    QJsonArray m_allVersions;
    QMap<QString, QString> m_versionTypeMap;
    QString m_currentSource;
    QString m_currentFilter;
    // 是否已自动加载过版本列表（首次 showEvent 触发，避免重复请求）
    bool m_versionsLoaded;
};

#endif // INSTALLINSTANCEPAGE_H