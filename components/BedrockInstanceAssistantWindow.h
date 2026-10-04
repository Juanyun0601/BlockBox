#ifndef BEDROCKINSTANCEASSISTANTWINDOW_H
#define BEDROCKINSTANCEASSISTANTWINDOW_H

#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

class AiChatPage;
class BedrockCommandAssistantPage;
class BedrockInstanceHomePage;
class BedrockMultiplayerPage;
class BedrockResourcesPage;
class BedrockInstanceSettingsPage;

/**
 * @brief 基岩版实例助手窗口（对齐 Java 版 InstanceAssistantWindow）
 *
 * Tab 结构（对齐 Java 版 6 个标签页，将 Java 管理替换为实例设置）：
 *   0 首页       —— BedrockInstanceHomePage（实例信息 + 快捷操作 + 存储概览）
 *   1 AI 助手    —— AiChatPage（复用 AI 聊天页面组件）
 *   2 快捷指令   —— BedrockCommandAssistantPage（中文 → 基岩版英文指令转换）
 *   3 联机       —— BedrockMultiplayerPage（GravityCone 建房/加房/玩家列表）
 *   4 资源管理   —— BedrockResourcesPage（资源包/行为包/地图/脚本入口）
 *   5 设置       —— BedrockInstanceSettingsPage（实例专属启动设置与数据统计）
 */
class BedrockInstanceAssistantWindow : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int highlightPos READ highlightPos WRITE setHighlightPos)

public:
    explicit BedrockInstanceAssistantWindow(QWidget *parent = nullptr);
    ~BedrockInstanceAssistantWindow() override;

    int highlightPos() const { return m_highlightPos; }
    void setHighlightPos(int pos);

    /** 注入当前激活的基岩版实例 ID，同步到各子页面 */
    void setInstanceId(const QString &instanceId);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void initUI();
    QPushButton *createNavButton(const QString &text, int index);
    void switchToTab(int index);
    void updateHighlightGeometry();
    void ensureTabVisible(int index);
    void applyThemeStyles();

    void buildHomePage(QWidget *page);
    void buildAiChatPage(QWidget *page);
    void buildCommandAssistantPage(QWidget *page);
    void buildMultiplayerPage(QWidget *page);
    void buildResourcesPage(QWidget *page);
    void buildSettingsPage(QWidget *page);

    QVBoxLayout *m_mainLayout;
    QWidget *m_navBar;
    QScrollArea *m_scrollArea;
    QWidget *m_navContainer;
    QHBoxLayout *m_navLayout;
    QWidget *m_selectionHighlight;
    QStackedWidget *m_stackedWidget;
    QList<QPushButton *> m_navButtons;
    QPropertyAnimation *m_highlightAnim;

    int m_currentIndex = 0;
    int m_highlightPos = 0;
    int m_prevIndex = 0;

    // ---- Tab 页面 ----
    BedrockInstanceHomePage *m_homePage = nullptr;           ///< 首页（index 0）
    AiChatPage *m_aiChatPage = nullptr;                     ///< AI 助手（index 1）
    BedrockCommandAssistantPage *m_commandPage = nullptr;    ///< 快捷指令（index 2）
    BedrockMultiplayerPage *m_multiplayerPage = nullptr;     ///< 联机（index 3）
    BedrockResourcesPage *m_resourcesPage = nullptr;         ///< 资源管理（index 4）
    BedrockInstanceSettingsPage *m_settingsPage = nullptr;   ///< 设置（index 5）

    QString m_instanceId;
};

#endif // BEDROCKINSTANCEASSISTANTWINDOW_H
