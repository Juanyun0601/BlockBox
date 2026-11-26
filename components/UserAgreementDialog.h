#ifndef USERAGREEMENTDIALOG_H
#define USERAGREEMENTDIALOG_H

#include "AppDialogBase.h"

class QCheckBox;
class QLabel;
class QPushButton;

/**
 * @brief 《方块盒子使用协议》与《方块盒子隐私政策》的确认弹窗
 *
 * 两个入口共用同一套条款与阅读弹窗：
 *  - 首次启动方块盒子：未同意时先弹出，同意后才能继续使用，拒绝则退出软件；
 *  - 登录 / 注册论坛账号：未同意时在提交前弹出，同意后继续登录。
 *
 * 两处分别记录自己的一份同意状态，互不影响；条款版本变化后会重新询问。
 */
class UserAgreementDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /// 弹窗出现的位置，决定标题、文案与拒绝后的行为
    enum Scope {
        FirstLaunch = 0, ///< 首次启动方块盒子
        ForumLogin = 1   ///< 登录 / 注册论坛账号
    };

    /// 可单独阅读的两份文档
    enum Document {
        TermsOfUse = 0, ///< 使用协议
        PrivacyPolicy = 1 ///< 隐私政策
    };

    /// 弹窗的结果：区分「用户明确拒绝」与「弹窗被系统/误触关掉」
    enum Decision {
        Accepted = 0, ///< 已同意（含之前同意过）
        Declined,     ///< 用户明确点了「不同意并退出」
        Dismissed     ///< 弹窗被关掉/没弹出来，不代表用户拒绝
    };

    /**
     * @brief 已同意当前版本时直接返回 Accepted；否则弹出确认窗等待用户勾选同意
     * @param scope 入口（首次启动 / 登录论坛），决定文案与记录的位置
     */
    static Decision decide(Scope scope, QWidget *parent = nullptr);

    /**
     * @brief 已同意当前版本时直接返回 true；否则弹出确认窗等待用户勾选同意
     * @param scope 入口（首次启动 / 登录论坛），决定文案与记录的位置
     * @return 用户是否已同意；拒绝或直接关闭弹窗时返回 false
     */
    static bool ensureAccepted(Scope scope, QWidget *parent = nullptr);

    /// 打开某份文档的全文阅读窗（「关于」页和弹窗内的「查看」都走这里）
    static void showDocument(Document document, QWidget *parent = nullptr);

    /// 某份文档的完整正文（HTML），全文阅读窗与关于页共用
    static QString documentText(Document document);

private:
    explicit UserAgreementDialog(Scope scope, QWidget *parent = nullptr);

    void initUI();
    void initStyle();
    void showEvent(QShowEvent *event) override;
    /// AppDialogBase 会把卡片内容包进滚动区；给内层容器补上卡片底色和圆角
    void styleScrollContent();

    static QString termsHtml();
    static QString privacyHtml();

    static QString agreementVersionKey(Scope scope);
    static int currentAgreementVersion();

private:
    Scope m_scope = FirstLaunch;
    QCheckBox *m_agreeCheck = nullptr;
    QPushButton *m_confirmButton = nullptr;
    /// 用户是否明确点了「不同意并退出」（用来把误触关闭和明确拒绝区分开）
    bool m_declined = false;
};

/**
 * @brief 协议 / 隐私政策全文阅读弹窗：居中卡片内滚动查看完整条款
 */
class UserAgreementDocumentDialog : public AppDialogBase
{
    Q_OBJECT

public:
    static void show(UserAgreementDialog::Document document, QWidget *parent = nullptr);

    explicit UserAgreementDocumentDialog(UserAgreementDialog::Document document,
                                         QWidget *parent = nullptr);

private:
    void initUI();
    void initStyle();
    void showEvent(QShowEvent *event) override;
    void styleScrollContent();

    UserAgreementDialog::Document m_document = UserAgreementDialog::TermsOfUse;
    QLabel *m_bodyLabel = nullptr;
};

#endif // USERAGREEMENTDIALOG_H
