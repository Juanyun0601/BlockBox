/**
 * @file   LoaderDetailPage.h
 * @brief  加载器详情页面类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef LOADERDETAILPAGE_H
#define LOADERDETAILPAGE_H

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "components/CustomCheckBox.h"

class QNetworkAccessManager;
struct SelectedLoader {
    QString name;
    QString version;
    QString fileName;
};

class LoaderDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit LoaderDetailPage(QWidget *parent = nullptr);
    ~LoaderDetailPage();

public slots:
    void setVersionInfo(const QString &versionId);
    void setVersionType(const QString &versionType);
    void addSelectedLoader(const QString &loaderName, const QString &loaderVersion, const QString &fileName = QString());
    void removeSelectedLoader(const QString &loaderName);
    void showMessage(const QString &message, bool isError = false);
    void enterModifyMode(const QString &instancePath, const QString &instanceName,
                         const QString &version, const QString &loader);
    void exitModifyMode();

    bool modifyMode() const { return m_modifyMode; }

signals:
    void backToInstallPageRequested();
    void viewAllVersionsRequested(const QString &loaderName, const QString &minecraftVersion);
    void loaderCardClicked(const QString &loaderName, const QString &minecraftVersion);
    void installRequested(const QString &instanceName, const QString &mcVersion, const QMap<QString, SelectedLoader> &loaders);
    void instanceModified(const QString &instancePath);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    void initLoaderCards();
    void createLoaderListCard(const QString &name, const QString &logoPath, const QString &description);
    void rebuildLoaderCards();
    void updateSelectedLoadersDisplay();
    void updateInstanceName();
    void loadInstanceFolders();
    QWidget* createSelectedLoaderCard(const QString &loaderName, const QString &loaderVersion);
    bool checkLoaderCompatibility(const QString &loaderName);

private:
    void tryAddLoaderWithCompatibility(const QString &loaderName, const QString &loaderVersion, const QString &fileName = QString());
    QStringList getInstalledLoaderNames() const;

    // UI components
    QVBoxLayout *m_mainLayout;
    QWidget *m_topBarWidget;
    QHBoxLayout *m_topBarLayout;
    QLineEdit *m_instanceNameEdit;
    QPushButton *m_installButton;

    QWidget *m_pathWidget;
    QHBoxLayout *m_pathLayout;
    QComboBox *m_pathComboBox;
    QPushButton *m_addFolderButton;
    QLabel *m_sourceLabel;
    QComboBox *m_sourceComboBox;
    QWidget *m_selectedLoadersWidget;
    QHBoxLayout *m_selectedLoadersLayout;
    QLabel *m_selectedLabel;
    QWidget *m_selectedCardsContainer;
    QHBoxLayout *m_selectedCardsLayout;
    QListWidget *m_loaderListWidget;
    
    // Message bar
    QWidget *m_messageBar;
    QHBoxLayout *m_messageBarLayout;
    QLabel *m_messageLabel;
    QTimer *m_messageTimer;

    // Version info
    QString m_currentVersion;
    QString m_versionType;
    QString m_instancePath;

    // 修改现有实例模式
    bool m_modifyMode;
    QString m_modifyInstancePath;
    QString m_modifyInstanceName;
    QString m_modifyInstanceVersion;
    QString m_modifyInstanceLoader;

    // 修改模式底部提示条
    QWidget *m_modifyInfoWidget;
    QHBoxLayout *m_modifyInfoLayout;
    QLabel *m_modifyInfoLabel;
    
    // Selected loaders
    QMap<QString, SelectedLoader> m_selectedLoaders;
    
    bool m_isInstalling;
};

#endif // LOADERDETAILPAGE_H