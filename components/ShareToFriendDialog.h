/**
 * @file   ShareToFriendDialog.h
 * @brief  分享给好友弹窗：选一位好友，把资源 / 帖子的标题和链接用私信发出
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 在线资源卡片右键、资源详情页、帖子分享菜单、收藏夹右键共用这一个入口。
 * 打开时拉好友列表（ForumChatService::fetchFriends），支持搜索过滤，
 * 选中后直接把「【分享】标题 + 链接」作为一条私信发出去（sendMessage）。
 */
#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include "AppDialogBase.h"
#include "utils/ForumChatService.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class ShareToFriendDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 一站式分享入口：登录检查 → 弹窗选好友 → 发私信
     * @param parent  宿主页面（提示气泡挂它上面）
     * @param title   分享内容的标题（资源名 / 帖子标题）
     * @param url     分享链接；空字符串时只发标题
     * @param summary 一行简介（来源 / 板块），仅展示在弹窗里
     * @return 是否真的发出了（未登录、没有好友、发送失败都会给提示并返回 false）
     */
    static bool share(QWidget *parent, const QString &title, const QString &url,
                      const QString &summary = QString());

    explicit ShareToFriendDialog(const QString &title, const QString &url,
                                 const QString &summary, QWidget *parent = nullptr);

private:
    void initUI();
    void initStyle();
    void rebuildFriendRows();

    /** 当前过滤词下要不要显示这位好友 */
    bool matchesFilter(const ForumFriendInfo &friendInfo) const;
    /** 发送分享私信；成功返回 true */
    void sendToPeer(const ForumFriendInfo &peer);

    QString m_title;
    QString m_url;
    QString m_summary;

    QVector<ForumFriendInfo> m_friends;
    ForumFriendInfo m_selected;
    bool m_sent = false;

    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_friendList = nullptr;
    QPushButton *m_sendBtn = nullptr;
};
