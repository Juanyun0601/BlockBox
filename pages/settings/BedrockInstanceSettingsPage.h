/**
 * @file   BedrockInstanceSettingsPage.h
 * @brief  基岩版单个实例设置页类声明
 * @author BlockBox Team
 * @date   2026-08-27
 *
 * 页面结构对齐 Java 版 InstanceGameSettingsPage（顶部工具栏 + 可折叠
 * 设置分区卡片 + QScrollArea），按基岩版（Windows UWP）特性组织：
 *   - 实例信息：名称 / 游戏版本 / 上次游玩 / 游戏数据目录
 *   - 启动设置：当前激活状态、启动后自动关闭启动器、优先启动预览版
 *   - 数据与存储：com.mojang 目录、世界 / 资源包 / 行为包 / 皮肤包统计
 *
 * 设置项以实例级覆盖持久化（bedrock/instance/<id>/...），未单独设置时
 * 回退到全局设置。组织方式参考开源启动器（HMCL / PCL / BedrockBoot 等）
 * 的实例级启动设置。
 */
#ifndef BEDROCKINSTANCESETTINGSPAGE_H
#define BEDROCKINSTANCESETTINGSPAGE_H

#include <QString>
#include <QWidget>

class CustomCheckBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

class BedrockInstanceSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockInstanceSettingsPage(QWidget *parent = nullptr);
    ~BedrockInstanceSettingsPage() override;

    /** 设置当前基岩版实例 ID，加载该实例专属设置并刷新信息 */
    void setInstanceId(const QString &instanceId);

    /** 读取实例级「启动后自动关闭启动器」，未单独设置时回退到全局设置 */
    static bool closeLauncherSetting(const QString &instanceId);
    /** 读取实例级「优先启动预览版」，未单独设置时回退到全局设置 */
    static bool preferPreviewSetting(const QString &instanceId);

signals:
    /** 设置项发生变化 */
    void settingChanged();
    /** 请求将指定实例激活为当前实例 */
    void activateInstanceRequested(const QString &instanceId);

private:
    void initUI();
    void loadSettings();
    void saveSettings();
    void restoreDefaults();
    void refreshInstanceInfo();
    void refreshStorageStats();

    QVBoxLayout *createSettingsCard(QVBoxLayout *parentLayout, const QString &title);
    QHBoxLayout *appendSettingRow(QVBoxLayout *cardLayout, const QString &title, const QString &desc,
                                  const QString &help = QString(), bool isLast = false);

    QString m_instanceId;
    bool m_loading = false;

    // 实例信息
    QLabel *m_nameValue = nullptr;
    QLabel *m_versionValue = nullptr;
    QLabel *m_lastPlayedValue = nullptr;
    QLabel *m_dataDirValue = nullptr;
    QPushButton *m_openDataDirBtn = nullptr;

    // 启动设置
    QLabel *m_activeStatusValue = nullptr;
    QPushButton *m_activateBtn = nullptr;
    CustomCheckBox *m_closeLauncherCheck = nullptr;
    CustomCheckBox *m_preferPreviewCheck = nullptr;

    // 数据与存储
    QLabel *m_mojangDirValue = nullptr;
    QPushButton *m_openMojangBtn = nullptr;
    QLabel *m_worldsCountValue = nullptr;
    QLabel *m_resourcePacksCountValue = nullptr;
    QLabel *m_behaviorPacksCountValue = nullptr;
    QLabel *m_skinPacksCountValue = nullptr;
    QPushButton *m_rescanBtn = nullptr;
};

#endif // BEDROCKINSTANCESETTINGSPAGE_H