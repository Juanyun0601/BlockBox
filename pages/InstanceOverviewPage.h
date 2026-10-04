/**
 * @file   InstanceOverviewPage.h
 * @brief  实例管理 - 概览页，展示实例基本信息、统计数据和快捷操作
 * @author BlockBox Team
 * @date   2026-06-27
 */
#ifndef INSTANCEOVERVIEWPAGE_H
#define INSTANCEOVERVIEWPAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

#include "components/OutlinedLabel.h"

class InstanceOverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceOverviewPage(QWidget *parent = nullptr);
    ~InstanceOverviewPage();

    /** 设置当前实例路径 */
    void setInstancePath(const QString &path);
    /** 设置实例名称 */
    void setInstanceName(const QString &name);
    /** 设置实例图标（自定义图片，空值恢复为首字母） */
    void setInstanceIcon(const QString &iconPath);
    /** 设置游戏版本 */
    void setGameVersion(const QString &version);
    /** 设置加载器信息 */
    void setLoaderInfo(const QString &loader);
    /** 重新统计卡片数据（启动次数等在启动后即时刷新） */
    void refreshStats();

signals:
    /** 请求启动游戏 */
    void launchGameRequested();
    /** 请求打开实例文件夹 */
    void openFolderRequested();
    /** 请求跳转到设置页 */
    void settingsRequested();
    /** 请求跳转到模组页 */
    void modsRequested();
    /** 请求导出整合包 */
    void exportRequested();
    /** 请求跳转到资源包页 */
    void resourcePacksRequested();
    /** 请求跳转到光影包页 */
    void shadersRequested();
    /** 请求跳转到存档页 */
    void savesRequested();
    /** 请求跳转到截图页 */
    void screenshotsRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void initUI();
    void updateIconPixmap();
    void refreshModCount();
    void refreshResourcePackCount();
    void refreshShaderCount();
    void refreshSaveCount();
    void refreshInstanceSize();
    void refreshLaunchCount();
    QString formatFileSize(qint64 bytes) const;

    // 头部区域
    QWidget *m_headerCard;
    QLabel *m_instanceIconLabel;
    OutlinedLabel *m_instanceNameLabel;
    QLabel *m_versionLabel;
    QLabel *m_loaderLabel;
    QLabel *m_lastPlayedLabel;
    QPushButton *m_launchBtn;

    // 统计卡片区域
    QWidget *m_statsCardsRow;
    QWidget *m_modCard;
    QLabel *m_modCountLabel;
    QWidget *m_resourcePackCard;
    QLabel *m_resourcePackCountLabel;
    QWidget *m_shaderCard;
    QLabel *m_shaderCountLabel;
    QWidget *m_savesCard;
    QLabel *m_savesCountLabel;
    QWidget *m_sizeCard;
    QLabel *m_sizeLabel;
    QWidget *m_launchCountCard;
    QLabel *m_launchCountLabel;

    // 快捷操作区域
    QWidget *m_actionsSection;
    QPushButton *m_openFolderBtn;
    QPushButton *m_settingsBtn;
    QPushButton *m_modsBtn;
    QPushButton *m_exportBtn;
    QPushButton *m_resourcePacksBtn;
    QPushButton *m_shadersBtn;
    QPushButton *m_savesBtn;
    QPushButton *m_screenshotsBtn;

    // 实例路径
    QWidget *m_pathCard;
    QLabel *m_pathLabel;

    // 数据
    QString m_instancePath;
    QString m_instanceName;
    QString m_iconPath;
    QString m_gameVersion;
    QString m_loaderInfo;
    QString m_lastPlayed;
};

#endif // INSTANCEOVERVIEWPAGE_H