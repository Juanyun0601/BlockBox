/**
 * @file   ChatUiStore.h
 * @brief  聊天页的本地整理项：会话置顶 / 消息免打扰 / 未发送草稿（按论坛账号分开保存）
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 这些都只是本机上的个人使用习惯，论坛接口没有对应字段，所以单独存一份
 * JSON（在软件数据目录下按账号分文件），换账号登录时自动切换到那份数据。
 *
 * 会话用统一的小写 key 标识：
 *   f<好友 id>   —— 私聊（friend）
 *   g<群 id>     —— 群聊（group）
 * 这样置顶、免打扰、草稿共用同一套键，页面里只需要判断一次类型。
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class ChatUiStore : public QObject
{
    Q_OBJECT

public:
    /** 全局单例（首次使用时创建） */
    static ChatUiStore *instance();

    /** 好友 / 群聊的会话 key：f12、g3 */
    static QString conversationKey(bool group, int id);
    /** key 是否是群聊 */
    static bool isGroupKey(const QString &key);

    /**
     * @brief 切换当前账号（0 = 未登录，清空内存里的数据）
     *
     * 同一个账号重复调用不会重新读盘；账号变了才载入对应的 JSON。
     */
    void setAccount(int userId);
    int accountId() const { return m_accountId; }

    bool isPinned(const QString &key) const { return m_pinned.contains(key); }
    bool isMuted(const QString &key) const { return m_muted.contains(key); }
    /** 草稿（没有草稿时是空串） */
    QString draft(const QString &key) const { return m_drafts.value(key); }
    /** 会话备注：只在本机显示的名字（没有备注时是空串） */
    QString remark(const QString &key) const { return m_remarks.value(key); }
    /** 会话列表里是否不显示最后一条消息的内容 */
    bool hidesPreview(const QString &key) const { return m_hidePreview.contains(key); }

    /** 置顶 / 取消置顶（官方会话也能置顶，只是它本来就在最前） */
    void setPinned(const QString &key, bool pinned);
    /** 免打扰 / 取消免打扰 */
    void setMuted(const QString &key, bool muted);
    /** 保存草稿；空串（或只有空白）表示删除这条草稿 */
    void setDraft(const QString &key, const QString &text);
    /** 设置会话备注；空串（或只有空白）表示恢复显示原来的昵称 */
    void setRemark(const QString &key, const QString &remark);
    /** 隐藏 / 显示会话列表里的消息预览 */
    void setHidesPreview(const QString &key, bool hide);

    /** 是否还有任何草稿（页面切换时用来决定要不要提示） */
    bool hasDraft(const QString &key) const { return !m_drafts.value(key).isEmpty(); }

signals:
    /** 置顶 / 免打扰 / 草稿有变化（页面据此重建左栏） */
    void changed();

private:
    explicit ChatUiStore(QObject *parent = nullptr);

    QString filePath() const;
    void load();
    void save();

    int m_accountId = 0;
    QSet<QString> m_pinned;
    QSet<QString> m_muted;
    QHash<QString, QString> m_drafts;
    QHash<QString, QString> m_remarks;
    QSet<QString> m_hidePreview;
};
