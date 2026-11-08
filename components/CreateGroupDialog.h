/**
 * @file   CreateGroupDialog.h
 * @brief  发起群聊弹窗：起个群名，从自己的好友里勾几个人，一键建群
 * @author BlockBox Team
 * @date   2026-11-08
 *
 * 聊天页左上角的「＋」按钮共用这一个入口。打开时拉好友列表（ForumChatService::fetchFriends），
 * 支持搜索过滤；点行即勾选 / 取消（最多 49 人），确认后调 createGroup，
 * 成功后通过 onCreated 把新群交给页面直接打开会话。
 */
#pragma once

#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

#include "AppDialogBase.h"
#include "utils/ForumChatService.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class CreateGroupDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 一站式建群入口：弹窗起名 + 挑好友 → 调接口建群
     * @param parent    宿主页面（提示气泡挂它上面）
     * @param onCreated 建群成功后的回调，参数是新群的信息（页面用它直接打开会话）
     * @return 是否真的建成了（未登录、没选人、接口失败都会给提示并返回 false）
     */
    static bool create(QWidget *parent, const std::function<void(const ForumGroupInfo &)> &onCreated);

    explicit CreateGroupDialog(QWidget *parent = nullptr);

private:
    void initUI();
    void initStyle();
    void rebuildFriendRows();

    /** 当前过滤词下要不要显示这位好友 */
    bool matchesFilter(const ForumFriendInfo &friendInfo) const;
    /** 群名和已选人数都合法时才能点「创建群聊」 */
    void refreshConfirmState();
    /** 调接口建群 */
    void submit();

    QVector<ForumFriendInfo> m_friends;
    QSet<int> m_selectedIds;     ///< 勾中的好友 id（不含自己）
    ForumGroupInfo m_lastCreated; ///< 建群成功时记住新群，accept 后交给 onCreated
    bool m_creating = false;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_friendList = nullptr;
    QPushButton *m_confirmBtn = nullptr;
};
