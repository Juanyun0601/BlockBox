#ifndef INSTANCEASSISTANTWINDOW_H
#define INSTANCEASSISTANTWINDOW_H

#include <QComboBox>
#include <QEasingCurve>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRect>
#include <QScrollArea>
#include <QSet>
#include <QSize>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "utils/CommandAssistant/BlockRegistry.h"
#include "utils/CommandAssistant/CommandCompleter.h"
#include "utils/CommandAssistant/CommandDatabase.h"
#include "utils/CommandAssistant/CommandTranslator.h"
#include "utils/CommandAssistant/LevelDatReader.h"

struct ModInfo;

class QHideEvent;
class QKeyEvent;
class QMoveEvent;
class QMouseEvent;
class QPaintEvent;
class QParallelAnimationGroup;

class AiChatPage;
class GameAchievementWatcher;
class InstanceHomePage;
class InstanceJavaDownloadPage;
class InstanceJavaPage;
class InstanceServerPage;
class MultiplayerPage;
class InstanceResourcesPage;

class InstanceAssistantWindow : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int highlightPos READ highlightPos WRITE setHighlightPos)

public:
    explicit InstanceAssistantWindow(QWidget *parent = nullptr);
    ~InstanceAssistantWindow();

    int highlightPos() const { return m_highlightPos; }
    void setHighlightPos(int pos);

    /**
     * @brief 注入当前实例上下文，触发 level.dat 检测与历史加载
     * @param path    实例根目录路径
     * @param version 实例版本（如 "1.20.4"）
     * @param loader  加载器名称（如 "Vanilla"/"Forge"）
     */
    void setInstanceContext(const QString &path, const QString &version, const QString &loader);

    /**
     * @brief 安卓游戏内侧栏模式：无边框、贴屏幕右侧四分之一宽、整窗半透明
     * @param enabled true 开启侧栏模式；false 恢复桌面独立窗口形态
     *
     * 须在窗口首次 show() 之前调用（切换窗口标志）。
     */
    void setOverlayMode(bool enabled);
    bool overlayMode() const { return m_overlayMode; }

    /// 当前是否为桌面悬浮窗形态（无边框、圆角、可拖动、可置顶）
    bool floatingStyle() const { return m_floating; }

signals:
    /** 请求快捷启动游戏并进入指定存档（由资源管理页存档卡片触发） */
    void quickLaunchSaveRequested(const QString &saveName);
    /** 请求打开资源详情页进行替换（由资源管理页模组卡片触发） */
    void resourceDetailForReplaceRequested(const ModInfo &info, const QString &instancePath, const ModInfo &localMod);
    /** 发布到联机大厅前需要登录论坛账号 */
    void forumLoginRequested();
    /**
     * @brief 「开服」页请求用这个窗口所属实例直接进本地服
     *
     * 由主窗口带着 --server/--port 启动该实例，进游戏就是连这台服务器。
     */
    void launchLocalServerGameRequested(const QString &address, int port);
    /**
     * @brief 「开服」页请求把服务器发布到社区「服务器导航」
     *
     * 地址已经换成隧道地址（联机页建立了隧道时），没有隧道就是本机地址。
     */
    void publishServerAdRequested(const QString &title, const QString &content,
                                  const QStringList &tags, const QString &address);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void initUI();
    QPushButton* createNavButton(const QString &text, int index);
    void switchToTab(int index);
    void updateHighlightGeometry();
    /** @brief 应用导航高亮条的样式（背景色随主题，宽度随当前标签页） */
    void updateHighlightStyle(int width);
    void animatePageFade(int from, int to);
    void ensureTabVisible(int index);
    void applyThemeStyles();
    /** @brief 计算并应用侧栏模式的贴右几何（右侧四分之一屏宽、全屏高） */
    void applyOverlayGeometry();

    // ---- 桌面悬浮窗外观 ----
    /** @brief 在导航栏上方构建悬浮窗标题栏（拖动区 + 置顶 / 收起 / 关闭） */
    void buildTitleBar();
    /** @brief 应用悬浮窗形态：无边框工具窗、透明圆角、阴影边距、可缩放尺寸 */
    void applyFloatingWindow();
    /** @brief 恢复上次保存的窗口位置与大小，没有记录时靠主窗口右侧摆放 */
    void restoreWindowGeometry();
    /** @brief 把当前位置与大小写回设置（关闭 / 收起 / 拖动结束时调用） */
    void saveWindowGeometry();
    /** @brief 没有历史位置时的默认摆放：主窗口右侧垂直居中 */
    void placeDefaultGeometry();
    /** @brief 把窗口拉回可见屏幕范围，避免换分辨率 / 拔显示器后看不见 */
    void keepOnScreen();
    /** @brief 圆角卡片区域（去掉阴影绘制边距） */
    QRect cardRect() const;
    /** @brief 判断鼠标位置落在哪几条边（用于拖动缩放），不在边缘返回空 */
    Qt::Edges resizeEdgesAt(const QPoint &pos) const;
    /** @brief 收起 / 展开：收起后只保留标题栏，像一条悬浮小条贴在游戏上 */
    void setCollapsed(bool collapsed);
    /** @brief 切换置顶状态（置顶时可悬浮在游戏和其他窗口之上） */
    void togglePinned();
    /** @brief 刷新标题栏按钮的图标、提示与选中态 */
    void updateHeaderButtons();
    /**
     * @brief 收起态下标题栏右侧显示的一行实时数据
     *
     * 数据来自首页 InstanceHomePage 的 liveStatsChanged 信号（每秒 / 每次硬件采样），
     * 收起时直接在标题栏里看帧率、CPU、GPU、内存，不用再展开窗口。
     */
    void updateLiveStats(int fps, int cpuPercent, int memoryPercent, int gpuPercent,
                         bool cpuAvailable, bool memoryAvailable, bool gpuAvailable);
    /** @brief 用缓存的数值和当前主题色重新渲染四项数据 */
    void refreshQuickStatsTexts();
    /** @brief 按标题栏可用宽度决定显示其中几项，窄窗口下自动少显示后面几项 */
    void fitQuickStatsToWidth();

    // ---- 快捷指令页面相关 ----
    /**
     * @brief 构建首页（index 0），展示性能监控与游玩信息
     * @param page 已由 initUI 创建的空白 QWidget
     *
     * 包含：当前时间、游玩时间、帧率，以及 CPU/内存/GPU 实时占用率卡片。
     */
    void buildHomePage(QWidget *page);
    /**
     * @brief 构建快捷指令页面（index 2）
     * @param page 已由 initUI 创建的空白 QWidget
     */
    void buildCommandAssistantPage(QWidget *page);
    /**
     * @brief 构建 AI 助手页面（index 1），复用 AiChatPage 组件
     * @param page 已由 initUI 创建的空白 QWidget
     */
    void buildAiChatPage(QWidget *page);
    /**
     * @brief 构建联机页面（index 3），复用 MultiplayerPage 组件
     * @param page 已由 initUI 创建的空白 QWidget
     */
    void buildMultiplayerPage(QWidget *page);
    /**
     * @brief 构建本地开服页面（index 4），复用 InstanceServerPage 组件
     * @param page 已由 initUI 创建的空白 QWidget
     *
     * 一键把当前实例开成本地服务端（自动下核心、写配置、启停、看日志），
     * 本机客户端直接连 127.0.0.1，朋友可以用联机页的隧道指过来。
     */
    void buildServerPage(QWidget *page);
    /**
     * @brief 构建资源管理页面（index 4），复用 InstanceResourcesPage 组件
     * @param page 已由 initUI 创建的空白 QWidget
     *
     * 管理当前实例的模组/资源包/光影包，支持启用/禁用、删除、粘贴、搜索等。
     */
    void buildResourcesPage(QWidget *page);
    /**
     * @brief 构建 Java 管理页面（index 5），内部使用 QStackedWidget 切换
     *        InstanceJavaPage（管理）与 InstanceJavaDownloadPage（下载）两个子页
     * @param page 已由 initUI 创建的空白 QWidget
     *
     * 管理本机 Java 安装：列表展示、扫描、添加、检测、移除、全局 Java 选择；
     * 通过「下载Java」按钮切换到下载子页，下载完成自动回切并刷新列表。
     */
    void buildJavaPage(QWidget *page);
    /** @brief 刷新候选列表 */
    void refreshCompletion();
    /** @brief 刷新英文输出框 */
    void refreshTranslation();
    /** @brief 更新上下文状态栏 */
    void updateContextDisplay();
    /** @brief 扫描 {实例路径}/saves/ 下最新 level.dat 检测上下文 */
    void detectContextFromLevelDat();
    /** @brief 检测运行中的 javaw.exe 进程，自动识别游戏版本与目录 */
    void detectRunningGame();
    /**
     * @brief 后台执行耗时上下文检测（进程检测 / JAR 解析 / level.dat 扫描）
     *
     * 在 QtConcurrent 工作线程执行，完成后自动回到主线程应用结果：
     * 覆盖 m_instancePath / m_commandContext、加载方块注册表、刷新 UI。
     * 避免在 UI 线程同步启动 PowerShell 或解析游戏 JAR 造成窗口卡顿。
     */
    void runContextDetectionInBackground();
    // ---- 游戏成就同步（生存模式 + 未开作弊） ----
    /** @brief 创建监听器、恢复未同步队列，并在登录状态变化时自动补传 */
    void setupGameAchievementSync();
    /** @brief 监听器发现一条新成就：入队、提示，并在登录后用账号同步 */
    void onGameAdvancementEarned(const QString &id, const QString &world, qint64 earnedAt);
    /** @brief 把队列里的游戏成就发给论坛，成功后弹「已获得勋章」 */
    void flushGameAchievementQueue();
    void loadPendingGameAchievements();
    void savePendingGameAchievements();
    /** @brief 成就显示名：优先用游戏语言文件里的标题 */
    QString gameAdvancementTitle(const QString &id) const;
    /** @brief 防抖刷新：输入停顿后统一执行补全 + 翻译（合并高频击键） */
    void flushDebouncedInput();
    /** @brief 复制按钮点击 */
    void onCopyClicked();
    /**
     * @brief 把候选列表里的一项按光标位置补进输入框
     * @param item 候选列表项（点击、Tab、回车共用同一套补全规则）
     */
    void insertCompletionItem(QListWidgetItem *item);
    /**
     * @brief 强制立刻重算候选与翻译，绕过输入防抖
     *
     * 回车复制 / 双击候选这类操作发生在击键后 60ms 内时，防抖还没跑，
     * 直接复制会拿到上一条指令，所以先同步刷新一次。
     */
    void flushInputImmediately();
    /** @brief 一键注入按钮点击：将英文指令注入到运行中的 Minecraft 游戏窗口 */
    void onInjectClicked();
    /** @brief 打开历史记录对话框，点击历史项可回填输入框 */
    void showHistoryDialog();
    /** @brief 弹出句子预设菜单（按分类分组），选中后一键填入输入框与输出框 */
    void showPresetsMenu();
    /** @brief 应用一条句子预设：中文填入输入框、英文写入输出框并激活操作按钮 */
    void applyPreset(const CommandPreset &preset);
    /** @brief 添加一条历史记录并持久化
     *  @param input  中文输入
     *  @param output 英文输出
     */
    void addHistory(const QString &input, const QString &output);
    /** @brief 打开指令包管理对话框 */
    void showPackManagerDialog();
    /** @brief 重新加载内置数据库 + 所有已启用指令包 */
    void reloadCommandDatabase();

    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    QVBoxLayout *m_outerLayout;   ///< 窗口外层布局（悬浮窗下用于留出阴影边距）
    QWidget *m_frame;             ///< 卡片容器（标题栏 + 导航 + 页面都放在这里）
    QVBoxLayout *m_mainLayout;
    QWidget *m_navBar;
    QScrollArea *m_scrollArea;
    QWidget *m_navContainer;
    QHBoxLayout *m_navLayout;
    QWidget *m_selectionHighlight;
    QStackedWidget *m_stackedWidget;
    QList<QPushButton *> m_navButtons;
    QPropertyAnimation *m_highlightAnim;
    QPointer<QParallelAnimationGroup> m_pageFadeGroup; ///< 当前正在跑的切页淡入淡出组

    int m_currentIndex;
    int m_highlightPos;
    int m_prevIndex;

    bool m_overlayMode = false; ///< 侧栏模式：无边框、贴右四分之一屏宽、半透明（安卓游戏内）

    // ---- 桌面悬浮窗 ----
    QWidget *m_titleBar;         ///< 悬浮窗标题栏（可拖动）
    QLabel *m_titleIcon;         ///< 标题栏左侧的实例助手图标
    QLabel *m_titleLabel;        ///< 标题栏文字
    QWidget *m_statsHost;        ///< 收起态实时数据容器（帧率 / CPU / GPU / 内存）
    QLabel *m_statFps;           ///< 帧率
    QLabel *m_statCpu;           ///< CPU 占用
    QLabel *m_statGpu;           ///< GPU 占用
    QLabel *m_statMem;           ///< 内存占用
    QPushButton *m_pinBtn;       ///< 置顶开关
    QPushButton *m_collapseBtn;  ///< 收起 / 展开
    QPushButton *m_closeBtn;     ///< 关闭（隐藏）悬浮窗
    bool m_floating = false;     ///< 桌面悬浮窗形态（移动端保持原有独立窗口 / 侧栏）
    bool m_pinned = true;        ///< 是否置顶悬浮于其他窗口之上
    bool m_collapsed = false;    ///< 是否已收起成只保留标题栏的小条
    int m_cornerRadius = 12;     ///< 圆角半径
    int m_shadowMargin = 12;     ///< 窗口四周留出的阴影边距
    QSize m_expandedSize;        ///< 收起前的窗口尺寸，展开时还原
    QRect m_launcherRect;        ///< 与主窗口脱开前记下的启动器位置（首次摆放用）

    // ---- 收起态实时数据缓存（首页每秒刷新） ----
    int m_liveFps = 0;
    int m_liveCpuPercent = 0;
    int m_liveMemoryPercent = 0;
    int m_liveGpuPercent = 0;
    bool m_liveCpuAvailable = false;
    bool m_liveMemoryAvailable = false;
    bool m_liveGpuAvailable = false;

    // ---- 标题栏拖动 / 边缘缩放 ----
    QPoint m_dragOrigin;         ///< 拖动开始时的鼠标相对窗口偏移
    bool m_dragging = false;
    QRect m_resizeStartGeometry; ///< 缩放开始时的窗口几何
    QPoint m_resizeStartGlobal;  ///< 缩放开始时的鼠标全局坐标
    Qt::Edges m_resizeEdges;     ///< 当前正在拖动的边
    bool m_resizing = false;

    // ---- 输入防抖 ----
    QTimer *m_inputDebounceTimer;  ///< 输入防抖定时器（singleShot，合并高频击键）
    QString m_lastCompletionInput; ///< 上次已处理的输入文本（无变化时跳过列表重建）

    // ---- 快捷指令页面控件 ----
    QLabel *m_contextLabel;          ///< 上下文状态标签
    QPushButton *m_serverToggleBtn;  ///< 切换服务端按钮
    QComboBox *m_opLevelCombo;       ///< OP 等级下拉框
    QPushButton *m_redetectBtn;      ///< 重新检测按钮
    QLineEdit *m_commandInput;       ///< 中文输入框
    QPushButton *m_clearInputBtn;    ///< 清空输入框的小按钮
    QListWidget *m_completionList;   ///< 候选列表
    QLineEdit *m_englishOutput;      ///< 英文输出框（只读）
    QLabel *m_outputHint;            ///< 输出框下方的警告/错误提示行
    QPushButton *m_copyBtn;          ///< 复制按钮
    QPushButton *m_injectBtn;        ///< 一键注入按钮
    QPushButton *m_historyBtn;       ///< 历史记录按钮（点击弹出对话框）
    QPushButton *m_packBtn;          ///< 指令包管理按钮
    QPushButton *m_presetBtn;        ///< 句子预设按钮（点击弹出按分类分组的预设菜单）

    // ---- 快捷指令核心模块 ----
    CommandDatabase *m_commandDb;    ///< 指令数据库
    CommandTranslator *m_translator; ///< 中文 → 英文转换器
    CommandCompleter *m_completer;   ///< 补全器
    BlockRegistry *m_blockRegistry; ///< 方块注册表（从游戏 JAR 动态加载）
    CommandContext m_commandContext; ///< 当前上下文
    QString m_instancePath;          ///< 当前实例根目录

    // ---- 游戏成就同步 ----
    GameAchievementWatcher *m_gameAchievementWatcher = nullptr;
    QHash<QString, QString> m_advancementTitles;        ///< 成就 id → 游戏内标题
    QHash<QString, QString> m_advancementDescriptions;  ///< 成就 id → 描述
    QJsonArray m_pendingGameAchievements;               ///< 等待同步到论坛的成就
    QSet<QString> m_queuedGameAchievements;             ///< 已入队的成就 id
    bool m_gameAchievementSyncing = false;

    // ---- AI 助手页面 ----
    AiChatPage *m_aiChatPage;        ///< 复用的 AI 聊天页面组件

    // ---- 首页 ----
    InstanceHomePage *m_homePage;    ///< 复用的首页组件

    // ---- 联机页面 ----
    MultiplayerPage *m_multiplayerPage; ///< 复用的联机页面组件

    // ---- 本地开服页面 ----
    InstanceServerPage *m_serverPage;   ///< 一键本地开服页面组件

    // ---- 资源管理页面 ----
    InstanceResourcesPage *m_resourcesPage; ///< 复用的资源管理页面组件

    // ---- Java 管理页面 ----
    QStackedWidget *m_javaStack;             ///< Java 页面内部子栈：0=管理页 / 1=下载页
    InstanceJavaPage *m_javaPage;           ///< Java 管理子页
    InstanceJavaDownloadPage *m_javaDownloadPage; ///< Java 下载子页
};

#endif // INSTANCEASSISTANTWINDOW_H
