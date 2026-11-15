/**
 * @file   ChatForwardDialog.h
 * @brief  转发消息弹窗：选一位好友或一个群聊，把消息内容转过去
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 聊天页消息右键「转发…」共用这一个入口。列表直接用页面上已经拿到的
 * 好友与群聊快照（不再重复请求接口），支持搜索过滤；选好后由页面负责
 * 真正发送（文字直接发，图片先取回原图再重新上传）。
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

/** 转发目标：好友（group=false）或群聊（group=true） */
struct ChatForwardTarget
{
    bool group = false;
    int id = 0;
    QString name;

    bool isValid() const { return id > 0; }
};

class ChatForwardDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 一站式转发入口：弹窗里挑一个会话
     * @param friends 可转发的好友（页面当前快照）
     * @param groups  可转发的群聊
     * @param preview 被转发消息的一行预览（弹窗顶部展示）
     * @param target  选中的会话
     * @return 是否选中并确认（点转发 / 双击会话）
     */
    static bool pick(QWidget *parent, const QVector<ForumFriendInfo> &friends,
                     const QVector<ForumGroupInfo> &groups, const QString &preview,
                     ChatForwardTarget &target);

    explicit ChatForwardDialog(const QVector<ForumFriendInfo> &friends,
                               const QVector<ForumGroupInfo> &groups,
                               const QString &preview, QWidget *parent = nullptr);

private:
    void initUI();
    void initStyle();
    void rebuildRows();
    /** 当前过滤词下这一行要不要显示 */
    bool matchesFilter(const QString &name, const QString &subtitle) const;
    /** 选中一个会话（列表行点击时） */
    void selectTarget(bool group, int id);

    QVector<ForumFriendInfo> m_friends;
    QVector<ForumGroupInfo> m_groups;
    QString m_preview;
    ChatForwardTarget m_selected;

    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_forwardButton = nullptr;
};
