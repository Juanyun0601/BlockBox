/**
 * @file   SettingsBedrockGamePage.h
 * @brief  基岩版全局游戏设置页类声明
 * @author BlockBox Team
 * @date   2026-08-16
 *
 * 基岩版（Windows UWP）没有 Java 版的内存 / Java 运行时等概念，
 * 因此提供独立的游戏设置页，包含：
 *   - 启动行为（启动后关闭启动器 / 优先启动预览版）
 *   - 安装信息（版本、包名、数据目录）
 *   - 数据与实例（当前激活实例、实例数据目录、管理实例入口）
 * 参考开源项目（HMCL / PCL / BedrockBoot 等）的「游戏启动设置」分类组织。
 */
#ifndef SETTINGSBEDROCKGAMEPAGE_H
#define SETTINGSBEDROCKGAMEPAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>

class CustomCheckBox;

class SettingsBedrockGamePage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsBedrockGamePage(QWidget *parent = nullptr);

    /** 重新读取安装信息 / 实例信息并刷新界面 */
    void refreshInfo();

signals:
    /** 请求跳转到基岩版实例选择页 */
    void manageInstancesRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void initUI();
    void refreshInstallInfo();
    void refreshInstanceInfo();
    void loadLaunchSettings();
    void saveLaunchSettings();
    void restoreDefaults();

    // 启动行为
    CustomCheckBox *m_closeLauncherCheck;
    CustomCheckBox *m_preferPreviewCheck;

    bool m_installInfoLoaded = false;

    // 安装信息
    QLabel *m_versionValue;
    QLabel *m_packageValue;
    QLabel *m_dataDirValue;
    QPushButton *m_openDataDirBtn;

    // 数据与实例
    QLabel *m_instanceValue;
    QLabel *m_instanceDirValue;
};

#endif // SETTINGSBEDROCKGAMEPAGE_H
