/**
 * @file   ChatBookmarkStore.cpp
 * @brief  聊天消息收藏的读写实现
 * @author BlockBox Team
 * @date   2026-11-14
 */
#include "ChatBookmarkStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include "../platform.h"

namespace {

QJsonObject bookmarkToJson(const ChatBookmark &bookmark)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), bookmark.id);
    object.insert(QStringLiteral("conversation_key"), bookmark.conversationKey);
    object.insert(QStringLiteral("conversation_name"), bookmark.conversationName);
    object.insert(QStringLiteral("message_id"), bookmark.messageId);
    object.insert(QStringLiteral("kind"), bookmark.kind);
    object.insert(QStringLiteral("body"), bookmark.body);
    object.insert(QStringLiteral("image"), bookmark.image);
    object.insert(QStringLiteral("file_name"), bookmark.fileName);
    object.insert(QStringLiteral("file_size"), static_cast<double>(bookmark.fileSize));
    object.insert(QStringLiteral("sender_name"), bookmark.senderName);
    object.insert(QStringLiteral("created_at"), static_cast<double>(bookmark.createdAt));
    object.insert(QStringLiteral("saved_at"), static_cast<double>(bookmark.savedAt));
    return object;
}

ChatBookmark bookmarkFromJson(const QJsonObject &object)
{
    ChatBookmark bookmark;
    bookmark.id = object.value(QStringLiteral("id")).toString();
    bookmark.conversationKey = object.value(QStringLiteral("conversation_key")).toString();
    bookmark.conversationName = object.value(QStringLiteral("conversation_name")).toString();
    bookmark.messageId = object.value(QStringLiteral("message_id")).toInt();
    bookmark.kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("text"));
    bookmark.body = object.value(QStringLiteral("body")).toString();
    bookmark.image = object.value(QStringLiteral("image")).toString();
    bookmark.fileName = object.value(QStringLiteral("file_name")).toString();
    bookmark.fileSize = static_cast<qint64>(object.value(QStringLiteral("file_size")).toDouble());
    bookmark.senderName = object.value(QStringLiteral("sender_name")).toString();
    bookmark.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    bookmark.savedAt = static_cast<qint64>(object.value(QStringLiteral("saved_at")).toDouble());
    return bookmark;
}

} // namespace

ChatBookmarkStore *ChatBookmarkStore::instance()
{
    static ChatBookmarkStore *s_instance = new ChatBookmarkStore();
    return s_instance;
}

ChatBookmarkStore::ChatBookmarkStore(QObject *parent) : QObject(parent) {}

QString ChatBookmarkStore::filePath() const
{
    return Platform::getDataDirectory() + QStringLiteral("/chat_bookmarks_%1.json").arg(m_accountId);
}

void ChatBookmarkStore::setAccount(int userId)
{
    if (m_accountId == userId)
        return;
    m_accountId = userId > 0 ? userId : 0;
    m_bookmarks.clear();
    if (m_accountId > 0)
        load();
    emit changed();
}

void ChatBookmarkStore::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &value : root.value(QStringLiteral("bookmarks")).toArray())
    {
        const ChatBookmark bookmark = bookmarkFromJson(value.toObject());
        if (bookmark.isValid())
            m_bookmarks.append(bookmark);
    }
}

void ChatBookmarkStore::save()
{
    if (m_accountId <= 0)
        return;

    QJsonArray array;
    for (const ChatBookmark &bookmark : m_bookmarks)
        array.append(bookmarkToJson(bookmark));
    QJsonObject root;
    root.insert(QStringLiteral("bookmarks"), array);

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

bool ChatBookmarkStore::contains(const QString &conversationKey, int messageId) const
{
    for (const ChatBookmark &bookmark : m_bookmarks)
    {
        if (bookmark.conversationKey == conversationKey && bookmark.messageId == messageId)
            return true;
    }
    return false;
}

QString ChatBookmarkStore::add(const ChatBookmark &bookmark)
{
    if (!bookmark.isValid() && bookmark.messageId <= 0)
        return QString();
    if (m_accountId <= 0)
        return QString();
    if (contains(bookmark.conversationKey, bookmark.messageId))
    {
        for (const ChatBookmark &item : m_bookmarks)
        {
            if (item.conversationKey == bookmark.conversationKey &&
                item.messageId == bookmark.messageId)
                return item.id;
        }
    }

    ChatBookmark stored = bookmark;
    if (stored.id.isEmpty())
        stored.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (stored.savedAt <= 0)
        stored.savedAt = QDateTime::currentSecsSinceEpoch();
    m_bookmarks.prepend(stored);
    save();
    emit changed();
    return stored.id;
}

void ChatBookmarkStore::remove(const QString &bookmarkId)
{
    for (int i = 0; i < m_bookmarks.size(); ++i)
    {
        if (m_bookmarks.at(i).id != bookmarkId)
            continue;
        m_bookmarks.removeAt(i);
        save();
        emit changed();
        return;
    }
}

void ChatBookmarkStore::removeByMessage(const QString &conversationKey, int messageId)
{
    for (int i = 0; i < m_bookmarks.size(); ++i)
    {
        const ChatBookmark &bookmark = m_bookmarks.at(i);
        if (bookmark.conversationKey != conversationKey || bookmark.messageId != messageId)
            continue;
        m_bookmarks.removeAt(i);
        save();
        emit changed();
        return;
    }
}
