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
 *               极限模式、游戏内时间、出生点坐标、游戏规则
 *     （死亡不掉落、昼夜更替等常用规则，支持搜索过滤与恢复默认值）
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
#include <QMap>
#include <QVector>

#include "utils/Saves/LevelDatEditor.h"

class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QGridLayout;
class QVBoxLayout;

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
    /** 请求打开该存档的备份与恢复弹窗（参数为存档目录名） */
    void backupRequested(const QString &saveName);

private slots:
    void onSaveClicked();         ///< 「保存修改」按钮
    void onOpenFolderClicked();   ///< 「打开存档文件夹」按钮
    void onCopySeedClicked();     ///< 复制种子
    void onOpenChunkbaseClicked();///< 在 Chunkbase 中查看种子
    void onOpenMinecraftSearchClicked();///< 在 MinecraftSearch 中查看此种子地图
    void onHardcoreToggled();     ///< 极限模式/锁定难度复选框变化
    void onRuleSearchChanged(const QString &text); ///< 按名称/ID 过滤游戏规则
    void onRulesResetClicked();   ///< 游戏规则恢复默认值

private:
    /** 页面上的一条游戏规则控件（布尔规则用复选框，数值规则用输入框） */
    struct RuleControl
    {
        QString id;             ///< 规则 ID（如 keepInventory）
        QString zh;             ///< 中文名（显示用）
        QString group;          ///< 所属分组名
        QString defaultValue;   ///< 游戏默认值（"true"/"false" 或数字字符串）
        bool numeric = false;   ///< 是否为数值规则
        QCheckBox *check = nullptr; ///< 布尔规则复选框
        QSpinBox *spin = nullptr;   ///< 数值规则输入框
        QLineEdit *text = nullptr;  ///< 文本规则输入框（其他规则：模组/数据包添加）
        QWidget *row = nullptr;     ///< 数值/文本规则的整行容器；布尔规则即复选框本身
    };

    void initUI();
    void buildRulesCard(QWidget *contentWidget, QVBoxLayout *layout, const QFont &headerFont);
    void loadSaveInfo();
    void loadRuleValues();
    void loadOtherRules();
    void setRulesEnabled(bool enabled);
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
    QPushButton *m_backupBtn;   ///< 「备份与恢复」按钮

    // 游戏规则区
    QVector<RuleControl> m_rules;           ///< 全部可编辑规则控件
    QVector<RuleControl> m_otherRules;      ///< 其他规则控件（模组/数据包添加，随存档动态生成）
    QMap<QString, QString> m_ruleBaseline;  ///< 载入时的规则值（文件缺省项用游戏默认值）
    QMap<QString, QLabel *> m_groupHeaders; ///< 分组名 → 分组标题（过滤时隐藏空组）
    QLineEdit *m_ruleSearchEdit = nullptr;  ///< 规则搜索框
    QPushButton *m_rulesResetBtn = nullptr; ///< 「恢复默认值」按钮
    QVBoxLayout *m_otherRulesLayout = nullptr; ///< 其他规则的行容器布局
};
