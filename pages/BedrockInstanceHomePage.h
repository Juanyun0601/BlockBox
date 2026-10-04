/**
 * @file   BedrockInstanceHomePage.h
 * @brief  基岩版实例助手 - 首页：实例信息 + 快捷操作 + 存储概览
 * @author BlockBox Team
 * @date   2026-08-25
 *
 * 用于 BedrockInstanceAssistantWindow 的「首页」标签页。窗口宽度较窄（450px），
 * 采用单列垂直卡片列表，分三个区块：
 *  - 实例信息：版本、上次游玩、存档数量
 *  - 快捷操作：启动游戏、打开数据目录
 *  - 存储概览：世界、资源包、行为包、皮肤包的数量统计
 */
#ifndef BEDROCKINSTANCEHOMEPAGE_H
#define BEDROCKINSTANCEHOMEPAGE_H

#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

class QVBoxLayout;

class BedrockInstanceHomePage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockInstanceHomePage(QWidget *parent = nullptr);
    ~BedrockInstanceHomePage() override;

    /** 设置当前基岩版实例 ID，刷新实例信息与存储统计 */
    void setInstanceId(const QString &instanceId);

signals:
    /** 请求启动基岩版游戏 */
    void launchGameRequested();
    /** 请求打开实例数据目录 */
    void openDataDirRequested();
    /** 请求打开 com.mojang 目录 */
    void openMojangDirRequested();

private slots:
    void onSecondTick();

private:
    void initUI();
    void applyThemeStyles();
    void refreshInstanceInfo();
    void refreshStorageStats();

    /** 创建信息单元格（标题 + 数值标签） */
    QWidget *createInfoCell(const QString &title, QLabel *&valueLabel);

    // ---- 实例信息控件 ----
    QLabel *m_versionValue;       ///< 版本号数值
    QLabel *m_lastPlayedValue;    ///< 上次游玩时间
    QLabel *m_worldsCountValue;   ///< 世界数量

    // ---- 快捷操作按钮 ----
    QPushButton *m_launchBtn;     ///< 启动游戏按钮
    QPushButton *m_openDataBtn;   ///< 打开数据目录按钮
    QPushButton *m_openMojangBtn; ///< 打开 com.mojang 目录按钮

    // ---- 存储概览控件 ----
    QLabel *m_resourcePacksCountValue;  ///< 资源包数量
    QLabel *m_behaviorPacksCountValue;  ///< 行为包数量
    QLabel *m_skinPacksCountValue;      ///< 皮肤包数量

    // ---- 定时器与状态 ----
    QTimer *m_tickTimer;          ///< 1 秒刷新定时器
    QString m_instanceId;         ///< 当前实例 ID
};

#endif // BEDROCKINSTANCEHOMEPAGE_H
