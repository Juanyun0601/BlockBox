#ifndef TOPBAR_H
#define TOPBAR_H

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QPointer>

class TopBar : public QWidget
{
    Q_OBJECT

public:
    explicit TopBar(QWidget *parent = nullptr);
    ~TopBar();

    void setStatusText(const QString &text);
    void setInstanceName(const QString &name);
    /** 基岩版模式：设置当前激活的基岩版实例名（空则显示"基岩版"） */
    void setBedrockInstanceName(const QString &name);
    /** 根据当前版本更新游戏管理卡片（基岩版隐藏实例设置，显示实例选择 + 实例名） */
    void updateGameCardForEdition();
    void setMainTitle();
    void setSettingsTitle();
    void setResourcesTitle();
    void setInstanceSelectTitle();
    void setAccountManageTitle();
    void setAddAccountTitle();
    void setLaunchDetailsTitle();
    void setTaskListTitle();
    void setTitle(const QString &title);

    void setAccountName(const QString &name);
    void setAccountAvatar(const QPixmap &pixmap);
    void setAccountCardVisible(bool visible);
    void setWindowControlCardVisible(bool visible);
    void setInstanceSettingsSelected(bool selected);
    void setInstanceSelectSelected(bool selected);
    void setAccountSelected(bool selected);
    void setTranslateButtonVisible(bool visible);

    /** 当前游戏版本（true=Java版，false=基岩版） */
    bool isJavaEdition() const { return m_isJavaEdition; }
    void setJavaEdition(bool java);
    /** 切换 Java 版/基岩版（等同点击顶栏版本切换卡片） */
    void toggleEdition();

    const QString &instanceName() const { return m_instanceName; }
    QString accountName() const;

signals:
    void backClicked();
    void scrollToTopClicked();
    void refreshClicked();
    void instanceSelectClicked();
    void instanceSettingsClicked();
    void instanceAssistantClicked();
    void launchGameClicked();
    void accountManageClicked();
    void searchClicked();
    void translateClicked();

    /** 游戏版本切换（true=Java版，false=基岩版），参数为新版本 */
    void editionSwitched(bool javaEdition);

    void minimizeWindowRequested();
    void maximizeWindowRequested();
    void closeWindowRequested();

private:
    void initUI();
    void initWindowControls();
    void applyPlatformStyle();
    QWidget* createFuncCard();
    QWidget* createAccountCard();
    QWidget* createGameCard();
    QWidget* createWindowControlCard();
    QWidget* createEditionCard();
    void updateEditionCard();
    void animateEditionSwitch();
    void stopEditionAnimation();
    void updateTitleElision();
    void updateInstanceNameElision();
    void rotateTitleText();
    void setSearchHintForTitle(const QString &title);
    void animateTitleSwitch();
    void stopTitleAnimation();

    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_topLayout;

    // Leftmost section - app logo
    QLabel *m_logoLabel;

    // Left section - function buttons card
    QWidget *m_funcCard;
    QPushButton *m_backButton;
    QPushButton *m_scrollToTopBtn;
    QPushButton *m_refreshBtn;
    QPushButton *m_translateBtn;

    // Left section - edition switcher card (Java版/基岩版)
    QWidget *m_editionCard = nullptr;
    QLabel *m_editionGlowJava = nullptr;     // 翠绿光晕（Java 态）
    QLabel *m_editionGlowBedrock = nullptr;  // 青色光晕（基岩态）
    QLabel *m_editionJavaIcon = nullptr;     // 草方块图标
    QLabel *m_editionBedrockIcon = nullptr;  // 石块图标
    QLabel *m_editionJavaText = nullptr;     // "Java版" 文字
    QLabel *m_editionBedrockText = nullptr;  // "基岩版" 文字
    QLabel *m_editionDot = nullptr;          // 状态圆点
    bool m_isJavaEdition = true;
    bool m_editionAnimating = false;
    QPointer<QParallelAnimationGroup> m_editionAnimGroup;
    QPointer<QGraphicsOpacityEffect> m_glowJavaOpacity;
    QPointer<QGraphicsOpacityEffect> m_glowBedrockOpacity;
    QPointer<QGraphicsOpacityEffect> m_javaIconOpacity;
    QPointer<QGraphicsOpacityEffect> m_bedrockIconOpacity;
    QPointer<QGraphicsOpacityEffect> m_javaTextOpacity;
    QPointer<QGraphicsOpacityEffect> m_bedrockTextOpacity;
    QPointer<QGraphicsOpacityEffect> m_dotOpacity;

    // Middle section - page title with search icon inside
    QWidget *m_titleContainer;
    QLabel *m_searchIcon;
    QLabel *m_titleLabel;
    QString m_titleText;
    QTimer *m_titleRotateTimer;
    QString m_searchHint;
    bool m_showingHint = false;
    QGraphicsOpacityEffect *m_titleOpacityEffect;
    bool m_titleAnimating = false;
    QPointer<QSequentialAnimationGroup> m_titleAnimGroup;

    // Right section - account card
    QWidget *m_accountCard;
    QPushButton *m_accountAvatar;
    QPushButton *m_accountNameLabel;

    // Right section - game management card
    QWidget *m_gameCard;
    QLabel *m_instanceNameLabel;
    QString m_instanceName;
    QString m_bedrockInstanceName;
    QPushButton *m_instanceSelectBtn;
    QPushButton *m_instanceSettingsBtn;
    QPushButton *m_instanceAssistantBtn;
    QPushButton *m_launchGameBtn;

    // Window control buttons card
    QWidget *m_windowControlCard;
    QPushButton *m_minimizeBtn;
    QPushButton *m_maximizeBtn;
    QPushButton *m_closeBtn;
};

#endif // TOPBAR_H
