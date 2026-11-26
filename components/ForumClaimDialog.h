/**
 * @file   ForumClaimDialog.h
 * @brief  转载认领弹窗：提交认领 + 认领沟通（同意转移 / 拒绝 / 撤回）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 转载帖详情页点「认领该内容」弹出 ForumClaimSubmitDialog 填说明；
 * 提交后认领人和转载作者都在 ForumClaimChatDialog 里沟通，
 * 转载作者在弹窗里同意后作品直接转移给认领人（创作中心同款弹窗）。
 */
#pragma once

#include <functional>

#include "AppDialogBase.h"
#include "utils/ForumContentService.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/** 认领沟通弹窗：消息列表 + 输入框 + 处理按钮（转载作者可同意转移 / 拒绝，认领人可撤回） */
class ForumClaimChatDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 打开一张认领单
     * @param parent     宿主页面
     * @param claimId    认领 id
     * @param onHandled  同意 / 拒绝 / 撤回成功后回调；参数 true 表示作品已转移给认领人
     */
    static void showDialog(QWidget *parent, int claimId,
                     std::function<void(bool approved)> onHandled = nullptr);

private:
    ForumClaimChatDialog(int claimId, QWidget *parent);

    void buildUi();
    void applyStyle();
    void load();
    void renderClaim();
    void renderMessages();
    void sendMessage();
    void approveClaim();
    void rejectClaim();
    void cancelClaim();
    void setBusy(bool busy);
    void setStatus(const QString &text, bool error = false);

    int m_claimId = 0;
    ForumClaimDetail m_detail;
    bool m_loading = false;
    bool m_busy = false;
    std::function<void(bool)> m_onHandled;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusChip = nullptr;
    QLabel *m_peopleLabel = nullptr;
    QLabel *m_claimMessageLabel = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_messagesHost = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QWidget *m_actionsRow = nullptr;
    QPushButton *m_approveButton = nullptr;
    QPushButton *m_rejectButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

/** 提交认领弹窗：填一句给转载作者的说明，提交后等作者确认 */
class ForumClaimSubmitDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /** 打开提交认领弹窗（登录检查由调用方负责） */
    static void request(QWidget *parent, const ForumThreadInfo &thread,
                        std::function<void()> onSubmitted = nullptr);

private:
    ForumClaimSubmitDialog(int threadId, const QString &threadTitle,
                           const QString &authorName, QWidget *parent);

    void buildUi();
    void applyStyle();
    void submit();
    static QString defaultMessage();

    int m_threadId = 0;
    QString m_threadTitle;
    QString m_authorName;
    QPlainTextEdit *m_edit = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_submitButton = nullptr;
    bool m_busy = false;
    bool m_submitted = false;
};
