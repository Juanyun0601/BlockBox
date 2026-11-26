/**
 * @file   MultiplayerLobbyDetailPage.h
 * @brief  联机大厅房间详情页：封面、介绍、模组 / 前置整合包、邀请码与一键加入
 * @author BlockBox Team
 * @date   2026-11-13
 *
 * 大厅列表里单击房间卡进入这里。页面沿用模组详情页的版式：
 *   顶部返回按钮 → 封面横幅（标题 / 房主 / 状态）→ 左右两栏
 * 左栏：房间介绍（Markdown）、房间评论、房主使用的模组、前置整合包；
 * 右栏：邀请码 / 连接地址与一键加入 / 分享、正在游玩的人、房间信息、本机实例匹配情况。
 * 窄窗口（手机 / 窄窗）自动改成单栏，加入卡和「正在游玩」排到介绍前面。
 *
 * 所有颜色走 @TOKEN@ 令牌，由 StyleKit::resolve 按当前主题解析。
 */

#pragma once

#include "utils/ForumLobbyService.h"

#include <QWidget>

class QBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QTimer;
class QVBoxLayout;
class ForumPostBrowser;
class LobbyHeroBanner;
class PlayerCountChart;
class QWidget;

class MultiplayerLobbyDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit MultiplayerLobbyDetailPage(QWidget *parent = nullptr);
    ~MultiplayerLobbyDetailPage() override;

    /** 填充 / 更新房间信息（内容没变时不重建，避免定时刷新打断阅读） */
    void setRoom(const ForumLobbyRoom &room);
    const ForumLobbyRoom &room() const { return m_room; }

    /** 加入 / 复制等操作的结果提示（写在大厅详情的加入卡里） */
    void setActionHint(const QString &text, bool error = false);

    /** 回到页面顶部（从列表换一个房间进详情时用） */
    void scrollToTop();

signals:
    /** 点左上角「返回大厅」 */
    void backRequested();
    /** 点「一键加入」：交回大厅页按房间要求挑实例并启动游戏 */
    void joinRequested(const ForumLobbyRoom &room);
    /** 评论区提示需要登录论坛账号（由大厅页转给社区页打开登录） */
    void loginRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    /** 从列表重新进详情页时刷新一次评论，免得看到离开前的旧列表 */
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void applyStyle();
    void updateContent();
    void updateResponsiveLayout();
    void updateStatusChip();
    void updateHostRow();
    void updateMetaChips();
    void updateIntro();
    void updateMods();
    void updateModpack();
    void updateInfoRows();
    /** 右栏「正在游玩」：房主 + 已进游戏的玩家头像墙 */
    void updatePlayers();
    /** 换房间 / 人数变化后刷新「人数统计」卡（必要时重新拉历史曲线） */
    void updateStatsRoom(bool roomChanged);
    /** 拉取房间最近 6 小时的人数曲线（拉取中 / 刚拉过时跳过） */
    void requestStatsHistory(bool force = false);
    /** 把房间里当前人数上报到论坛（按 60 秒节流，别人也能看到这段历史） */
    void reportStatsSample(bool force);
    void updateMatchText();
    void copyTarget();
    /** 加入卡里的「分享」菜单：复制分享文案 / 只复制邀请码 / 分享给好友私信 */
    void shareRoom();
    /** 拼一段可以直接粘给朋友的房间信息（房间名、邀请码 / 地址、版本） */
    QString shareText(bool withHeader = true) const;

    // ── 房间评论 ──
    /** 房间换了 / 状态变了时同步评论卡片（房间不在大厅里就整块收起） */
    void updateComments();
    /** 拉取最近一批评论 */
    void requestComments();
    /** 重画评论列表与条数提示（不动输入区，免得打断打字） */
    void rerenderCommentRows();
    /** 按登录 / 答题状态重建输入区；状态没变时保留正在写的草稿 */
    void updateCommentComposer();
    /** 更新字数与「发表评论」按钮可用状态 */
    void updateCommentCount();
    /** 发表输入框里的评论 */
    void submitComment();
    /** 删除一条评论（先弹确认） */
    void deleteComment(const ForumLobbyComment &comment);
    void setCommentStatus(const QString &text, bool error = false);
    void openQuizDialog();

    /** 用于判断内容有没有变（定时刷新时避免整页重排） */
    QString targetText() const;
    bool isCompactWidth() const;

    ForumLobbyRoom m_room;
    QString m_contentSignature;
    QStringList m_missingMods;
    QStringList m_missingAuxMods;
    QStringList m_missingPacks;
    QStringList m_missingShaders;
    bool m_compact = false;

    QVBoxLayout *m_rootLayout = nullptr;
    QPushButton *m_backButton = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QBoxLayout *m_columns = nullptr;
    QVBoxLayout *m_leftColumn = nullptr;
    QVBoxLayout *m_rightColumn = nullptr;

    // 顶部横幅
    LobbyHeroBanner *m_heroBanner = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_mineChip = nullptr;
    QLabel *m_statusChip = nullptr;
    QWidget *m_hostHost = nullptr;
    QWidget *m_metaHost = nullptr;

    // 左栏卡片
    QWidget *m_introCard = nullptr;
    QLabel *m_descLabel = nullptr;
    ForumPostBrowser *m_introBrowser = nullptr;
    QLabel *m_introHint = nullptr;
    QWidget *m_modsCard = nullptr;
    QLabel *m_modsHint = nullptr;
    QWidget *m_modsHost = nullptr;
    QLabel *m_auxHint = nullptr;
    QWidget *m_auxHost = nullptr;
    QLabel *m_extrasHint = nullptr;
    QWidget *m_extrasHost = nullptr;
    QWidget *m_modpackCard = nullptr;
    QLabel *m_packNameLabel = nullptr;
    QLabel *m_packMetaLabel = nullptr;
    QLabel *m_packHintLabel = nullptr;
    QPushButton *m_packButton = nullptr;

    // 右栏卡片
    QWidget *m_joinCard = nullptr;
    QLabel *m_codeCaption = nullptr;
    QLabel *m_codeLabel = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_joinButton = nullptr;
    QPushButton *m_shareButton = nullptr;
    QLabel *m_matchLabel = nullptr;
    QLabel *m_actionHint = nullptr;

    // 「正在游玩」卡片（紧跟在加入卡下面，窄屏时和加入卡一起排到最前面）
    QWidget *m_playersCard = nullptr;
    QLabel *m_playersHint = nullptr;
    QWidget *m_playersHost = nullptr;

    // 「人数统计」卡片：房间里在线人数的折线图（数据来自论坛聚合采样）
    QWidget *m_statsCard = nullptr;
    QLabel *m_statsValue = nullptr;
    QLabel *m_statsHint = nullptr;
    PlayerCountChart *m_statsChart = nullptr;
    QTimer *m_statsTimer = nullptr;
    int m_statsRoomId = 0;          ///< 当前曲线属于哪个房间（换房间时清空重拉）
    qint64 m_statsLastReportMs = 0; ///< 上次上报采样时刻
    qint64 m_statsLastFetchMs = 0;  ///< 上次拉取曲线时刻
    bool m_statsFetching = false;

    QWidget *m_infoCard = nullptr;
    QLabel *m_versionValue = nullptr;
    QLabel *m_loaderValue = nullptr;
    QLabel *m_coreValue = nullptr;
    QLabel *m_statusValue = nullptr;
    QLabel *m_playersValue = nullptr;
    QLabel *m_joinCountValue = nullptr;
    QLabel *m_publishedValue = nullptr;
    QLabel *m_expireValue = nullptr;

    // 「房间评论」卡片（左栏，夹在介绍和模组之间）
    QWidget *m_commentsCard = nullptr;
    QLabel *m_commentsHint = nullptr;
    QPushButton *m_commentsRefresh = nullptr;
    QWidget *m_commentsHost = nullptr;
    QWidget *m_commentComposerHost = nullptr;
    QVBoxLayout *m_commentComposerLayout = nullptr;
    QPlainTextEdit *m_commentEdit = nullptr;
    QLabel *m_commentCount = nullptr;
    QPushButton *m_commentSend = nullptr;
    QLabel *m_commentStatus = nullptr;
    QTimer *m_commentsTimer = nullptr;
    QVector<ForumLobbyComment> m_comments;
    int m_commentsRoomId = 0;   ///< 当前列表属于哪个房间（换房间时清空重拉）
    int m_commentsTotal = 0;
    QString m_commentsRowsSignature; ///< 评论没变时不重画整列，保住选中 / 滚动位置
    bool m_commentsLoading = false;
    bool m_commentsRefreshQueued = false; ///< 拉取过程中又要求刷新，等这一轮回来再补一次
    bool m_commentsFailed = false;      ///< 上一次拉评论失败（点刷新可重试）
    bool m_commentsUnavailable = false; ///< 服务端说房间已关闭，评论区先锁住
    bool m_commentSubmitting = false;
    QString m_commentDraft;          ///< 重建输入区时保住已经写了一半的评论
    QString m_commentComposerState;  ///< 登录 / 答题 / 房间在线状态，变了才重建输入区
};
