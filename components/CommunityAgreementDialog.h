#ifndef COMMUNITYAGREEMENTDIALOG_H
#define COMMUNITYAGREEMENTDIALOG_H

#include "AppDialogBase.h"

class QCheckBox;
class QLabel;
class QPushButton;

/**
 * @brief 首次发布前的「社区规范」确认弹窗：居中卡片上列出核心规则与处罚，
 *        勾选「已阅读并同意」后才能继续发布，可点链接打开规范全文。
 *
 * 同意状态记在本机设置里，同意一次永久生效（换弹窗入口也共用这一份记录）。
 */
class CommunityAgreementDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /// 已经同意过直接返回 true；否则弹出确认弹窗，用户勾选同意后返回 true 并记住
    static bool ensureAccepted(QWidget *parent = nullptr);

private:
    explicit CommunityAgreementDialog(QWidget *parent = nullptr);

    void initUI();
    void initStyle();

private:
    QCheckBox *m_agreeCheck = nullptr;
    QPushButton *m_confirmButton = nullptr;
};

/**
 * @brief 「社区规范」全文查看弹窗：居中卡片内滚动阅读完整规则与处罚
 */
class CommunityGuidelinesDialog : public AppDialogBase
{
    Q_OBJECT

public:
    static void show(QWidget *parent = nullptr);

private:
    explicit CommunityGuidelinesDialog(QWidget *parent = nullptr);

    void initUI();
    void initStyle();

    QLabel *m_bodyLabel = nullptr;
};

#endif // COMMUNITYAGREEMENTDIALOG_H
