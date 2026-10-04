/**
 * @file   SaveSettingsPage.h
 * @brief  存档设置页面
 * @author BlockBox Team
 * @date   2026-08-01
 *
 * 展示单个 Minecraft 单机存档的基本信息，并允许修改 level.dat 中的
 * 部分设置。功能参考开源启动器 PCL-CE 的存档设置页（PageInstanceSavesInfo）：
 *   - 信息展示：版本、种子、最后游玩时间、出生点、游戏模式、游玩时长
 *   - 可编辑项：世界名称、游戏模式、允许作弊、难度、锁定难度、
 *               极限模式、游戏内时间、出生点坐标
 *
 * 本页嵌入实例管理页（InstanceManagePage）的内容栈中，通过
 * setCurrentSave() 指定要查看的存档。修改通过 LevelDatEditor 原子写入 level.dat。
 *
 * 本页不含自己的返回按钮与页面标题，返回与当前页名称复用主界面
 * TopBar 的返回按钮与标题（由 MainWindow 在进入本页时设置）。
 */

#pragma once

#include <QWidget>
#include <QString>

#include "utils/Saves/LevelDatEditor.h"

class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QGridLayout;

/**
 * @brief 存档设置页面
 *
 * 顶部为存档名称；中部为存档信息卡片与可编辑设置卡片；
 * 底部为「打开存档文件夹」「保存修改」按钮。
 */
class SaveSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SaveSettingsPage(QWidget *parent = nullptr);
    ~SaveSettingsPage() override;

    /**
     * @brief 指定要展示/编辑的存档并重新加载信息
     * @param saveFolderPath 存档文件夹绝对路径（内含 level.dat）
     * @param saveName       存档目录名
     */
    void setCurrentSave(const QString &saveFolderPath, const QString &saveName);

    /** 重置为未加载状态（实例切换时调用，避免显示旧存档信息） */
    void reset();

signals:
    /** 世界重命名完成（旧名，新名） */
    void saveRenamed(const QString &oldName, const QString &newName);

private slots:
    void onSaveClicked();         ///< 「保存修改」按钮
    void onOpenFolderClicked();   ///< 「打开存档文件夹」按钮
    void onCopySeedClicked();     ///< 复制种子
    void onOpenChunkbaseClicked();///< 在 Chunkbase 中查看种子
    void onOpenMinecraftSearchClicked();///< 在 MinecraftSearch 中查看此种子地图
    void onHardcoreToggled();     ///< 极限模式/锁定难度复选框变化

private:
    void initUI();
    void loadSaveInfo();
    void addInfoRow(const QString &head, QWidget *content);
    void refreshSpawnSpinEnable();
    void refreshHardcoreState();
    void setDayTimePreset(qint64 ticks);
    QString formatPlayTime() const;
    QString formatLastPlayed() const;
    QString chunkbaseUrl() const;
    QString versionForChunkbase() const;
    QString versionForMinecraftSearch() const;
    QString minecraftSearchUrl() const;
    qint64 currentDayTimeTicks() const;

    QString m_saveFolderPath;   ///< 存档文件夹绝对路径
    QString m_saveName;         ///< 存档目录名
    SaveInfo m_info;            ///< 已读取的存档信息
    bool m_loaded = false;      ///< level.dat 是否成功解析

    // 顶部
    QLabel *m_saveNameLabel;

    // 信息区
    QGridLayout *m_infoGrid;
    QLabel *m_versionLabel;
    QLabel *m_seedLabel;
    QLabel *m_lastPlayedLabel;
    QLabel *m_spawnLabel;
    QLabel *m_gameModeLabel;
    QLabel *m_playTimeLabel;
    QLabel *m_dayTimeLabel;

    // 设置区
    QLineEdit *m_worldNameEdit;
    QComboBox *m_gameTypeCombo;
    QComboBox *m_allowCommandsCombo;
    QComboBox *m_difficultyCombo;
    QCheckBox *m_lockDifficultyCheck;
    QCheckBox *m_hardcoreCheck;
    QComboBox *m_dayTimeCombo;
    QSpinBox *m_spawnXSpin;
    QSpinBox *m_spawnYSpin;
    QSpinBox *m_spawnZSpin;
    QLabel *m_hardcoreHint;

    QPushButton *m_copySeedBtn;
    QPushButton *m_chunkbaseBtn;
    QPushButton *m_minecraftSearchBtn;
    QPushButton *m_saveBtn;
    QPushButton *m_openFolderBtn;
};
