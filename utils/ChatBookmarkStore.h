/**
 * @file   ChatBookmarkStore.h
 * @brief  聊天消息收藏：把重要消息记在本机，按论坛账号分开保存
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 论坛接口没有「收藏消息」，所以收藏只存在本机：消息内容存一份快照
 * （好友撤回、对方删号之后收藏里还看得到当时的内容），另外记住会话
 * key、发言人和时间，用来在收藏列表里点「打开」回到原会话。
 *
 * 数据文件：软件数据目录/chat_bookmarks_<账号 id>.json
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

/** 一条被收藏的消息（存的是收藏那一刻的内容快照） */
struct ChatBookmark
{
    QString id;                 ///< 收藏记录 id（本地生成，删除用它）
    QString conversationKey;    ///< 会话 key：f12 / g3（与 ChatUiStore 一致）
    QString conversationName;   ///< 会话名（好友昵称或群名）
    int messageId = 0;          ///< 原消息 id（同一会话里用来判断是否已收藏）
    QString kind;               ///< text / image / file
    QString body;               ///< 文字内容（图片/文件的附言）
    QString image;              ///< 图片站内路径（kind=image）
    QString fileName;           ///< 文件名（kind=file）
    qint64 fileSize = 0;        ///< 文件大小（kind=file）
    QString senderName;         ///< 发言者（「我」或对方昵称）
    qint64 createdAt = 0;       ///< 原消息时间
    qint64 savedAt = 0;         ///< 收藏时间

    bool isValid() const { return !id.isEmpty(); }
    bool isImage() const { return kind == QStringLiteral("image") && !image.isEmpty(); }
    bool isFile() const { return kind == QStringLiteral("file") && !fileName.isEmpty(); }
};

class ChatBookmarkStore : public QObject
{
    Q_OBJECT

public:
    static ChatBookmarkStore *instance();

    /** 切换当前账号（0 = 未登录，清空内存） */
    void setAccount(int userId);
    int accountId() const { return m_accountId; }

    /** 收藏列表，按收藏时间倒序（最新的在最前面） */
    QVector<ChatBookmark> bookmarks() const { return m_bookmarks; }
    bool isEmpty() const { return m_bookmarks.isEmpty(); }
    /** 这条会话里的这条消息收藏过没有 */
    bool contains(const QString &conversationKey, int messageId) const;

    /**
     * @brief 收藏一条消息；同一会话里同一条消息重复收藏时返回已有的记录 id
     * @return 收藏记录 id（失败返回空串）
     */
    QString add(const ChatBookmark &bookmark);
    void remove(const QString &bookmarkId);
    /** 取消收藏：按会话 + 原消息 id 找 */
    void removeByMessage(const QString &conversationKey, int messageId);

signals:
    /** 收藏有增删（收藏列表据此重建） */
    void changed();

private:
    explicit ChatBookmarkStore(QObject *parent = nullptr);

    QString filePath() const;
    void load();
    void save();

    int m_accountId = 0;
    QVector<ChatBookmark> m_bookmarks;   ///< 按收藏时间倒序
};
