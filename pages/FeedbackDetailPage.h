/**
 * @file   FeedbackDetailPage.h
 * @brief  反馈详情页：一条反馈的正文、管理员回复和下面的评论区
 * @author BlockBox Team
 * @date   2026-11-21
 *
 * 从反馈列表页点反馈卡片进入（整页，侧边栏隐藏，顶栏返回回反馈列表）。
 * 正文与管理员的公开回复直接展示，评论区支持刷新、发表和删除；
 * 发表需要登录论坛账号并通过入站答题，和网页论坛一致。
 */
#pragma once

#include <QVector>
#include <QWidget>

#include "utils/FeedbackService.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

class FeedbackDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit FeedbackDetailPage(QWidget *parent = nullptr);

    /** 用列表里的一条反馈填充整页（同时清掉上一条反馈的评论与输入状态） */
    void open(const PublicFeedbackItem &item);

    /** 当前展示的反馈 id（0 表示还没打开任何反馈） */
    int feedbackId() const { return m_item.id; }

signals:
    /** 评论区点了「登录 / 注册」：请求跳到账户页弹登录表单 */
    void loginRequested();

private:
    void buildUi();
    void renderFeedback();

    // ---- 评论区 ----
    void requestComments();
    void renderComments();
    void rebuildCommentComposer();
    void updateCommentCount();
    void submitComment();
    void deleteComment(const FeedbackComment &comment);
    void setCommentStatus(const QString &text, bool error = false);
    void openQuizDialog();

    PublicFeedbackItem m_item;

    QLabel *m_statePill = nullptr;   ///< 待处理 / 已处理 / 已忽略
    QLabel *m_titleLabel = nullptr;
    QLabel *m_typeBadge = nullptr;   ///< 问题反馈 / 功能建议
    QLabel *m_metaLabel = nullptr;   ///< #编号 · 提交人 · 时间
    QLabel *m_contentLabel = nullptr;
    QLabel *m_replyLabel = nullptr;  ///< 管理员回复（没有时隐藏）

    QLabel *m_commentHint = nullptr; ///< 共 N 条评论 / 正在加载 / 加载失败
    QPushButton *m_commentRefresh = nullptr;
    QWidget *m_commentRowsHost = nullptr;
    QVBoxLayout *m_commentRowsLayout = nullptr;
    QWidget *m_composerHost = nullptr;
    QVBoxLayout *m_composerLayout = nullptr;
    QLabel *m_commentStatus = nullptr;
    QPlainTextEdit *m_edit = nullptr;
    QLabel *m_count = nullptr;
    QPushButton *m_send = nullptr;

    QVector<FeedbackComment> m_comments;
    int m_commentTotal = 0;
    bool m_commentLoading = false;
    bool m_commentQueued = false;    ///< 拉取中又要求刷新（刚发完 / 刚删完）
    bool m_commentFailed = false;
    bool m_commentSubmitting = false;
    QString m_composerState;         ///< 输入区当前对应的登录 / 答题状态
    QString m_rowsSignature;         ///< 评论行渲染签名（内容没变不重画）
    QString m_draft;                 ///< 重建输入区前收起来的半截评论
};
