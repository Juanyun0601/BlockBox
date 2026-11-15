/**
 * @file   FeedbackPage.h
 * @brief  软件内反馈页：反馈列表（仿 GitHub Issues）
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 从左侧导航栏底部的「反馈」入口进入，未登录也能看。
 * 页面只显示反馈列表：状态页签（全部 / 待处理 / 已处理 / 已忽略）+ 分页，
 * 点顶部「提交新反馈」进入提交新反馈页；管理员处理的回复在详情里展开可见。
 * 详情展开后还能看到这条反馈下的评论，登录并通过入站答题的用户可以留言讨论。
 */
#pragma once

#include <QHash>
#include <QSet>
#include <QWidget>

#include "components/ForumKit.h"
#include "utils/FeedbackService.h"

class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

/** 一条反馈详情展开区里的评论区（控件句柄 + 渲染签名，跟着反馈行一起重建） */
struct FeedbackCommentSection
{
    QWidget *detail = nullptr;         ///< 所属的详情展开区（判断这行有没有展开）
    int authorId = 0;                  ///< 反馈提交人的用户 id（决定能不能删别人的留言）
    QLabel *hint = nullptr;            ///< 顶部状态行（共 N 条评论 / 正在加载 / 还没人评论）
    QPushButton *refresh = nullptr;    ///< 手动刷新按钮
    QWidget *rowsHost = nullptr;       ///< 评论行容器
    QVBoxLayout *rowsLayout = nullptr;
    QWidget *composerHost = nullptr;   ///< 输入区容器（登录提示 / 答题提示 / 输入框三选一）
    QVBoxLayout *composerLayout = nullptr;
    QLabel *status = nullptr;          ///< 发表 / 删除的结果提示
    QPlainTextEdit *edit = nullptr;
    QLabel *count = nullptr;
    QPushButton *send = nullptr;
    QString composerState;             ///< 输入区当前对应的登录 / 答题状态
    QString rowsSignature;             ///< 评论行渲染签名（内容没变不重画）
};

class FeedbackPage : public QWidget
{
    Q_OBJECT

public:
    explicit FeedbackPage(QWidget *parent = nullptr);

    /** 进入页面时刷新反馈列表 */
    void open();

signals:
    /** 点了「提交新反馈」：请求跳到提交新反馈页 */
    void composeRequested();
    /** 评论区点了「登录 / 注册」：请求跳到账户页弹登录表单 */
    void loginRequested();

private:
    void buildUi();
    void loadPublic(int page);
    void renderPublicTabs();
    void renderPublic(const QVector<PublicFeedbackItem> &items);

    // ---- 反馈详情里的评论区 ----
    void clearCommentSections();
    void buildCommentSection(QVBoxLayout *detailLayout, const PublicFeedbackItem &item);
    void requestComments(int feedbackId);
    void renderComments(int feedbackId);
    void rebuildCommentComposer(int feedbackId);
    void updateCommentCount(int feedbackId);
    void submitComment(int feedbackId);
    void deleteComment(int feedbackId, const FeedbackComment &comment);
    void setCommentStatus(int feedbackId, const QString &text, bool error);
    void openQuizDialog();

    FeedbackCommentSection *commentSection(int feedbackId) const;

    QWidget *m_publicHost = nullptr; ///< 全部反馈（仿 GitHub Issues 列表页，未登录也可见）

    QPushButton *m_composeButton = nullptr;

    QHBoxLayout *m_publicTabsLayout = nullptr;
    QVector<QPushButton *> m_publicTabButtons;
    QVBoxLayout *m_publicListLayout = nullptr;
    ForumKit::ForumPager *m_publicPager = nullptr;
    QString m_publicStatus = QStringLiteral("all");
    FeedbackCounts m_publicCounts;
    int m_publicPage = 1;
    int m_publicTotalPages = 1;

    // ---- 评论状态（key = 反馈 id）----
    QHash<int, FeedbackCommentSection *> m_commentSections; ///< 当前列表里每条反馈的评论 UI
    QHash<int, QVector<FeedbackComment>> m_comments;        ///< 已拉到的评论数据
    QHash<int, int> m_commentTotals;                        ///< 服务端的评论总数
    QHash<int, QString> m_commentDrafts;                    ///< 重建输入区前收起来的半截评论
    QSet<int> m_commentLoading;                             ///< 正在拉取的反馈 id
    QSet<int> m_commentQueued;                              ///< 拉取中又要求刷新的反馈 id
    QSet<int> m_commentFailed;                              ///< 上一次评论拉取失败的反馈 id
    QSet<int> m_commentSubmitting;                          ///< 正在发表的反馈 id
};
