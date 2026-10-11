/**
 * @file   AccountManagePage.h
 * @brief  账户管理页面类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef ACCOUNTMANAGEPAGE_H
#define ACCOUNTMANAGEPAGE_H

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QImage>
#include <QLineEdit>
#include <QList>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QHideEvent>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "components/Skin3DWidget.h"

class SkinDownloader;

class AccountManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit AccountManagePage(QWidget *parent = nullptr);
    ~AccountManagePage();

    void initUI();
    void loadAccounts();

    /**
     * @brief 获取当前选中的账户名
     * @return 当前账户名，若无选中返回空字符串
     */
    QString currentAccountName() const;

    /**
     * @brief 获取当前选中的账户类型
     * @return 当前账户类型字符串（如 "离线"、"微软正版"、"第三方"）
     */
    QString currentAccountType() const { return m_currentAccountType; }

    /**
     * @brief 刷新指定账户的皮肤显示
     * @param username 账户名
     */
    void refreshSkinForAccount(const QString &username);

    /**
     * @brief 强制刷新 3D 皮肤控件（同步重绘）
     *
     * 用于页面切换动画结束后触发 Skin3DWidget 重绘，
     * 解决 QGraphicsEffect 移除后 QOpenGLWidget 不自动重绘的问题。
     */
    void refreshSkin3DWidget();

signals:
    void backToMainRequested();
    void addAccountPageOpened();
    void accountManagePageOpened();
    void accountSelected(const QString &accountName);
    void skinEditorRequested();
    /**
     * @brief 当前账户皮肤已更新，发出截取的头部正面头像
     * @param avatar 由皮肤头部正面裁切生成的头像（正方形 QPixmap）
     */
    void accountSkinUpdated(const QPixmap &avatar);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

public slots:
    void onAddAccountClicked();
    void onSwitchAccountClicked();
    void onLogoutClicked();
    void onSetDefaultClicked();
    void onLoginMethodChanged(int index);
    void onBackToAccountList();
    void onThemeColorChanged(const QString &color);

    /**
     * @brief 执行右侧面板"动作选择"下拉框中当前选中的动作
     *
     * 支持多个动作：复制用户名 / 复制 UUID / 刷新皮肤 /
     * 打开皮肤编辑器 / 删除账户（参考 HMCL 账户操作集）。
     */
    void onExecuteActionClicked();

    /**
     * @brief 点击"模型动作"图标按钮，直接切换皮肤模型动作
     *
     * 驱动皮肤模型肢体动画（参考 skinview3d walking/wave/riding）：
     * 待机 / 挥手 / 走路 / 跑步 / 骑马坐姿。
     */
    void onModelPoseButtonClicked();

private:
    void initLeftAccountList();
    void initCenterSkinDisplay();
    void initRightAccountPanel();
    /** 初始化模型动作控件（统一放入右侧账户面板区域） */
    void initModelActions();
    /** 切换皮肤模型动作：更新勾选态/图标颜色，tip 非空时弹出提示 */
    void setModelPose(SkinPose pose, const QString &tip = QString());
    /** 主题/主题色变化时按勾选态重新着色模型动作按钮图标 */
    void refreshModelPoseIcons();
    /** 模型动作按钮图标颜色：选中用主题色，未选中用中性灰 */
    QColor modelPoseIconColor(bool checked) const;
    void initLoginPage();
    /** 按当前登录方式的实际高度收缩添加账户卡片（QStackedWidget 默认取所有页的最大值，会让最短表单留下大片空白） */
    void updateLoginColumnHeight();
    void addAccountItem(const QString &accountName, const QString &accountType, const QString &serverUrl = "", bool isDefault = false, const QString &skinUrl = "", const QString &uuid = "");
    void updateCurrentAccountInfo(const QString &accountName, const QString &accountType, bool isDefault = false);
    void clearAccountButtons();
    void onAccountItemClicked(QPushButton *btn);
    /** 从皮肤纹理截取头部正面并发出 accountSkinUpdated 头像信号 */
    void applySkinAvatar(const QImage &texture);

    /** 应用"跟随主题"背景：设置页启用了自定义应用背景时清屏全透明透出，否则用主题色 */
    void applyThemePresetBackground();

    // 左侧账户列表：统一选中逻辑（含图标颜色切换 + 滑动高亮 + 入场动画）
    // 与 InstanceSelectPage / SubNavPanel 的实现完全一致
    void setSelectedAccount(QPushButton *target);
    void slideHighlightTo(QPushButton *target);
    void animateAccountEntrance();
    QIcon loadColoredIcon(const QString &path, const QColor &color) const;

    // 左侧账户列表悬浮气泡：重新计算几何（页面尺寸变化时调用）
    void updateLeftBubbleGeometry();

    // 左侧导航悬浮气泡参数
    static constexpr int kBubbleWidth = 200;   // 气泡固定宽度
    static constexpr int kBubbleMargin = 10;   // 气泡与页面边缘间距

    // UI components
    QHBoxLayout *m_mainLayout;

    // Left account list
    QVBoxLayout *m_leftLayout;
    QWidget *m_leftWidget;
    QWidget *m_accountHighlight;
    QLabel *m_accountCountLabel;   // 左侧栏账户数量徽章
    QScrollArea *m_accountScrollArea;
    QWidget *m_accountScrollContent;
    QVBoxLayout *m_accountContentLayout;
    QList<QPushButton *> m_accountButtons;
    QColor m_iconNormalColor;
    QColor m_iconSelectedColor;
    QPushButton *m_addAccountBtn;
    QPushButton *m_skinEditorBtn;

    // Center skin display
    QVBoxLayout *m_centerLayout;
    Skin3DWidget *m_skin3DWidget;
    QWidget *m_emptyStateWidget;        // 空态容器（提示 + 按钮，居中于预览区）
    QLabel *m_noAccountHint;
    QPushButton *m_addFirstAccountBtn;

    // 模型动作选择（肢体动画）—— 参考 skinview3d walking/wave/riding
    // 图标 + 文字按钮网格，点击直接切换动作
    QWidget *m_modelActionWidget;
    QList<QPushButton *> m_modelPoseButtons;
    SkinPose m_modelPose;            // 当前模型动作（待机/挥手/走路/跑步/骑马）

    // Right account panel
    QWidget *m_rightWidget;
    QVBoxLayout *m_rightLayout;
    QLabel *m_accountNameLabel;
    QLabel *m_accountTypeDot;     // 账户类型彩色圆点
    QLabel *m_accountTypeLabel;
    QLabel *m_defaultAccountLabel;
    QPushButton *m_switchAccountBtn;
    QPushButton *m_logoutBtn;
    QPushButton *m_setDefaultBtn;

    // 动作选择（更多操作）—— 支持多个动作的下拉选择器
    QComboBox *m_actionCombo;
    QPushButton *m_executeActionBtn;

    // Login page
    QStackedWidget *m_stackedWidget;
    QWidget *m_accountManageWidget;
    QWidget *m_loginWidget;
    QTabWidget *m_loginTabWidget;
    QWidget *m_microsoftLoginTab;
    QWidget *m_offlineLoginTab;
    QWidget *m_thirdPartyLoginTab;

    // Data
    QString m_currentAccountName;
    QString m_currentAccountType;
    QString m_currentUuid;      // 当前选中账户的 UUID（供复制 UUID 动作使用）
    QString m_currentSkinUrl;   // 当前选中账户的皮肤 URL（供刷新皮肤动作使用）
    bool m_isDefaultAccount;

    // Skin
    SkinDownloader *m_skinDownloader;
};

#endif // ACCOUNTMANAGEPAGE_H
