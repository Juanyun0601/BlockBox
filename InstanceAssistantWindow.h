#ifndef INSTANCEASSISTANTWINDOW_H
#define INSTANCEASSISTANTWINDOW_H

#include <QComboBox>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
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

class AiChatPage;
class InstanceHomePage;
class InstanceJavaDownloadPage;
class InstanceJavaPage;
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

signals:
    /** 请求快捷启动游戏并进入指定存档（由资源管理页存档卡片触发） */
    void quickLaunchSaveRequested(const QString &saveName);
    /** 请求打开资源详情页进行替换（由资源管理页模组卡片触发） */
    void resourceDetailForReplaceRequested(const ModInfo &info, const QString &instancePath, const ModInfo &localMod);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void initUI();
    QPushButton* createNavButton(const QString &text, int index);
    void switchToTab(int index);
    void updateHighlightGeometry();
    void animatePageFade(int from, int to);
    void ensureTabVisible(int index);
    void applyThemeStyles();
    /** @brief 计算并应用侧栏模式的贴右几何（右侧四分之一屏宽、全屏高） */
    void applyOverlayGeometry();

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
    /** @brief 防抖刷新：输入停顿后统一执行补全 + 翻译（合并高频击键） */
    void flushDebouncedInput();
    /** @brief 复制按钮点击 */
    void onCopyClicked();
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

    QVBoxLayout *m_mainLayout;
    QWidget *m_navBar;
    QScrollArea *m_scrollArea;
    QWidget *m_navContainer;
    QHBoxLayout *m_navLayout;
    QWidget *m_selectionHighlight;
    QStackedWidget *m_stackedWidget;
    QList<QPushButton *> m_navButtons;
    QPropertyAnimation *m_highlightAnim;

    int m_currentIndex;
    int m_highlightPos;
    int m_prevIndex;

    bool m_overlayMode = false; ///< 侧栏模式：无边框、贴右四分之一屏宽、半透明（安卓游戏内）

    // ---- 输入防抖 ----
    QTimer *m_inputDebounceTimer;  ///< 输入防抖定时器（singleShot，合并高频击键）
    QString m_lastCompletionInput; ///< 上次已处理的输入文本（无变化时跳过列表重建）

    // ---- 快捷指令页面控件 ----
    QLabel *m_contextLabel;          ///< 上下文状态标签
    QPushButton *m_serverToggleBtn;  ///< 切换服务端按钮
    QComboBox *m_opLevelCombo;       ///< OP 等级下拉框
    QPushButton *m_redetectBtn;      ///< 重新检测按钮
    QLineEdit *m_commandInput;       ///< 中文输入框
    QListWidget *m_completionList;   ///< 候选列表
    QLineEdit *m_englishOutput;      ///< 英文输出框（只读）
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

    // ---- AI 助手页面 ----
    AiChatPage *m_aiChatPage;        ///< 复用的 AI 聊天页面组件

    // ---- 首页 ----
    InstanceHomePage *m_homePage;    ///< 复用的首页组件

    // ---- 联机页面 ----
    MultiplayerPage *m_multiplayerPage; ///< 复用的联机页面组件

    // ---- 资源管理页面 ----
    InstanceResourcesPage *m_resourcesPage; ///< 复用的资源管理页面组件

    // ---- Java 管理页面 ----
    QStackedWidget *m_javaStack;             ///< Java 页面内部子栈：0=管理页 / 1=下载页
    InstanceJavaPage *m_javaPage;           ///< Java 管理子页
    InstanceJavaDownloadPage *m_javaDownloadPage; ///< Java 下载子页
};

#endif // INSTANCEASSISTANTWINDOW_H
