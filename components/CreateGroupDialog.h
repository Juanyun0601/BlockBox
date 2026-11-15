/**
 * @file   CreateGroupDialog.h
 * @brief  发起群聊 / 邀请好友进群弹窗：从自己的好友里勾几个人，一键建群或拉人进群
 * @author BlockBox Team
 * @date   2026-11-08
 *
 * 聊天页左上角的「＋」按钮和群聊会话里的「邀请」按钮共用这一个弹窗。
 * 打开时拉好友列表（ForumChatService::fetchFriends），支持搜索过滤；点行即勾选 / 取消；
 * 建群模式没填群名时会按勾中的人自动起名。确认后分别调 createGroup / inviteGroupMembers，
 * 成功后通过回调把结果交给页面（建群打开新会话，邀请刷新成员与人数）。
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

namespace ForumKit
{
class ClickableCard;
}

class CreateGroupDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 一站式建群入口：弹窗起名 + 挑好友 → 调接口建群
     * @param preselectFriendId > 0 时预先勾中这位好友（私聊页「拉群」一键带上对方）
     * @return 是否真的建成了（未登录、没选人、接口失败都会给提示并返回 false）
     */
    static bool create(QWidget *parent, const std::function<void(const ForumGroupInfo &)> &onCreated,
                       int preselectFriendId = 0);

    /**
     * @brief 邀请好友进已有的群（群里任何成员都能拉人）：挑人 → 调接口邀请
     * @param existingMemberIds 已在群里的成员 id，列表里会排除这些人
     * @param onInvited 邀请成功后的回调（页面用来刷新成员与人数）
     */
    static bool invite(QWidget *parent, const ForumGroupInfo &group,
                       const QVector<int> &existingMemberIds,
                       const std::function<void()> &onInvited);

    explicit CreateGroupDialog(QWidget *parent = nullptr);
    /** 邀请模式：不填群名、排除已在群成员，可勾人数按群剩余名额算 */
    CreateGroupDialog(QWidget *parent, const ForumGroupInfo &group,
                      const QVector<int> &existingMemberIds);

private:
    void initUI();
    void initStyle();
    void rebuildFriendRows();
    /** 好友列表拉到后把预选的好友勾上（私聊一键拉群用） */
    void applyPreselect();

    /** 当前过滤词下要不要显示这位好友 */
    bool matchesFilter(const ForumFriendInfo &friendInfo) const;
    /** 群名和已选人数都合法时才能点确认按钮 */
    void refreshConfirmState();
    /** 建群模式：群名还空着时按已选好友自动起一个（用户自己输过就不碰） */
    void suggestGroupName();
    /** 调接口建群 / 邀请 */
    void submit();
    /** 打开群图标选择器（内置表情 / 上传图片 / 恢复首字） */
    void openIconPicker();
    /** 按当前选择刷新图标预览与状态说明 */
    void refreshIconPreview();

    bool m_inviteMode = false;     ///< true = 邀请进已有群，false = 建新群
    ForumGroupInfo m_group;        ///< 邀请模式下的目标群
    QVector<int> m_existingIds;    ///< 邀请模式下已在群里的成员 id（列表排除用）
    int m_capacity = 0;            ///< 一次最多能勾多少人（建群 49，邀请按群剩余名额）
    int m_preselectId = 0;         ///< 好友列表拉到后要预选的好友（0 = 没有）
    bool m_nameTouched = false;    ///< 用户自己动过群名输入框（动了就不再自动起名）

    QVector<ForumFriendInfo> m_friends;
    QSet<int> m_selectedIds;     ///< 勾中的好友 id（不含自己）
    ForumGroupInfo m_lastCreated; ///< 建群成功时记住新群，accept 后交给 onCreated
    bool m_creating = false;

    QString m_icon;              ///< 群图标：内置表情或已上传的站内图片地址（空 = 群名首字）
    QString m_iconPath;          ///< 本地选好、点「创建群聊」时才上传的图片

    QLabel *m_statusLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_friendList = nullptr;
    QPushButton *m_confirmBtn = nullptr;
    ForumKit::ClickableCard *m_iconSlot = nullptr; ///< 图标预览（点它也能打开选择器）
    QLabel *m_iconHint = nullptr;    ///< 当前图标状态说明
};
