/**
 * @file   InstanceGameSettingsPage.h
 * @brief  单个实例游戏设置页类声明
 * @author BlockBox Team
 * @date   2026-06-19
 */
#ifndef INSTANCEGAMESETTINGSPAGE_H
#define INSTANCEGAMESETTINGSPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

class CustomCheckBox;

class InstanceGameSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceGameSettingsPage(QWidget *parent = nullptr);
    ~InstanceGameSettingsPage();

    /** 设置当前实例路径，加载该实例的专属设置 */
    void setInstancePath(const QString &instancePath);
    /** 设置实例名称 */
    void setInstanceName(const QString &name);
    /** 设置游戏版本 */
    void setGameVersion(const QString &version);
    /** 设置加载器信息 */
    void setLoaderInfo(const QString &loader);
    /** 设置实例图标（外部联动，如保存后刷新） */
    void setInstanceIcon(const QString &iconPath);

    /** 获取当前配置的 Java 路径 */
    QString javaPath() const;
    /** 获取当前配置的最大内存 (MB) */
    int maxMemory() const;
    /** 获取当前配置的最小内存 (MB) */
    int minMemory() const;
    /** 获取 JVM 参数 */
    QString jvmArgs() const;
    /** 获取窗口宽度 */
    QString windowWidth() const;
    /** 获取窗口高度 */
    QString windowHeight() const;
    /** 是否全屏 */
    bool fullscreen() const;
    /** 升级策略: 0=保守升级(仅正式版), 1=抢先升级(所有版本) */
    int upgradeMode() const;

signals:
    void settingChanged();
    /** 实例名称或图标被修改（displayName 为空表示使用文件夹名） */
    void instanceInfoChanged(const QString &displayName, const QString &iconPath);

private:
    void initUI();
    void loadSettings();
    void saveSettings();
    /** 选择实例图标文件并预览 */
    void chooseInstanceIcon();
    /** 恢复默认图标（清除自定义图标） */
    void resetInstanceIcon();
    void updateIconPreview();

    QVBoxLayout *createSettingsCard(QVBoxLayout *parentLayout,
                                    const QString &title = QString(),
                                    bool startCollapsed = false);
    QHBoxLayout *appendSettingRow(QVBoxLayout *cardLayout,
                                  const QString &title,
                                  const QString &desc,
                                  const QString &help = QString(),
                                  bool isLast = false);

    QString m_instancePath;
    QString m_instanceName;
    QString m_gameVersion;
    QString m_loaderInfo;

    // 实例基本信息
    QLineEdit *m_instanceNameEdit;
    QLabel *m_gameVersionLabel;
    QLabel *m_loaderLabel;
    QLabel *m_iconPreviewLabel;
    QString m_iconPath;

    // Java 设置
    QComboBox *m_javaPathCombo;
    QLabel *m_javaVersionLabel;

    // 内存设置
    QSlider *m_maxMemorySlider;
    QLabel *m_maxMemoryLabel;
    QLineEdit *m_minMemoryEdit;

    // JVM 参数
    QLineEdit *m_jvmArgsEdit;

    // 窗口设置
    QLineEdit *m_windowWidthEdit;
    QLineEdit *m_windowHeightEdit;

    // 全屏
    CustomCheckBox *m_fullscreenCheck;

    // 升级策略
    CustomCheckBox *m_priorityUpgradeCheck;
    CustomCheckBox *m_cautiousUpgradeCheck;
};

#endif // INSTANCEGAMESETTINGSPAGE_H