/**
 * @file   ChatUiStore.cpp
 * @brief  聊天页本地整理项（置顶 / 免打扰 / 草稿）的读写实现
 * @author BlockBox Team
 * @date   2026-11-14
 */
#include "ChatUiStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "../platform.h"

ChatUiStore *ChatUiStore::instance()
{
    static ChatUiStore *s_instance = new ChatUiStore();
    return s_instance;
}

ChatUiStore::ChatUiStore(QObject *parent) : QObject(parent) {}

QString ChatUiStore::conversationKey(bool group, int id)
{
    return (group ? QStringLiteral("g") : QStringLiteral("f")) + QString::number(id);
}

bool ChatUiStore::isGroupKey(const QString &key)
{
    return key.startsWith(QLatin1Char('g'));
}

QString ChatUiStore::filePath() const
{
    // 一个账号一份，换账号不会串数据；未登录时没有文件
    return Platform::getDataDirectory() + QStringLiteral("/chat_ui_%1.json").arg(m_accountId);
}

void ChatUiStore::setAccount(int userId)
{
    if (m_accountId == userId)
        return;
    m_accountId = userId > 0 ? userId : 0;
    m_pinned.clear();
    m_muted.clear();
    m_drafts.clear();
    m_remarks.clear();
    m_hidePreview.clear();
    if (m_accountId > 0)
        load();
    emit changed();
}

void ChatUiStore::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &value : root.value(QStringLiteral("pinned")).toArray())
    {
        const QString key = value.toString();
        if (!key.isEmpty())
            m_pinned.insert(key);
    }
    for (const QJsonValue &value : root.value(QStringLiteral("muted")).toArray())
    {
        const QString key = value.toString();
        if (!key.isEmpty())
            m_muted.insert(key);
    }
    const QJsonObject drafts = root.value(QStringLiteral("drafts")).toObject();
    for (auto it = drafts.constBegin(); it != drafts.constEnd(); ++it)
    {
        if (!it.key().isEmpty() && !it.value().toString().isEmpty())
            m_drafts.insert(it.key(), it.value().toString());
    }
    const QJsonObject remarks = root.value(QStringLiteral("remarks")).toObject();
    for (auto it = remarks.constBegin(); it != remarks.constEnd(); ++it)
    {
        if (!it.key().isEmpty() && !it.value().toString().isEmpty())
            m_remarks.insert(it.key(), it.value().toString());
    }
    for (const QJsonValue &value : root.value(QStringLiteral("hidePreview")).toArray())
    {
        const QString key = value.toString();
        if (!key.isEmpty())
            m_hidePreview.insert(key);
    }
}

void ChatUiStore::save()
{
    if (m_accountId <= 0)
        return;

    QJsonObject root;
    QJsonArray pinned;
    for (const QString &key : m_pinned)
        pinned.append(key);
    QJsonArray muted;
    for (const QString &key : m_muted)
        muted.append(key);
    QJsonObject drafts;
    for (auto it = m_drafts.constBegin(); it != m_drafts.constEnd(); ++it)
    {
        if (!it.value().isEmpty())
            drafts.insert(it.key(), it.value());
    }
    QJsonObject remarks;
    for (auto it = m_remarks.constBegin(); it != m_remarks.constEnd(); ++it)
    {
        if (!it.value().isEmpty())
            remarks.insert(it.key(), it.value());
    }
    QJsonArray hidePreview;
    for (const QString &key : m_hidePreview)
        hidePreview.append(key);
    root.insert(QStringLiteral("pinned"), pinned);
    root.insert(QStringLiteral("muted"), muted);
    root.insert(QStringLiteral("drafts"), drafts);
    root.insert(QStringLiteral("remarks"), remarks);
    root.insert(QStringLiteral("hidePreview"), hidePreview);

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    // 写失败时 QSaveFile 不会覆盖原文件，聊天记录仍在
    file.commit();
}

void ChatUiStore::setPinned(const QString &key, bool pinned)
{
    if (key.isEmpty() || m_pinned.contains(key) == pinned)
        return;
    if (pinned)
        m_pinned.insert(key);
    else
        m_pinned.remove(key);
    save();
    emit changed();
}

void ChatUiStore::setMuted(const QString &key, bool muted)
{
    if (key.isEmpty() || m_muted.contains(key) == muted)
        return;
    if (muted)
        m_muted.insert(key);
    else
        m_muted.remove(key);
    save();
    emit changed();
}

void ChatUiStore::setDraft(const QString &key, const QString &text)
{
    if (key.isEmpty())
        return;
    const bool blank = text.trimmed().isEmpty();
    const QString previous = m_drafts.value(key);
    if (blank)
    {
        if (previous.isEmpty())
            return;
        m_drafts.remove(key);
    }
    else
    {
        if (previous == text)
            return;
        m_drafts.insert(key, text);
    }
    save();
    emit changed();
}

void ChatUiStore::setRemark(const QString &key, const QString &remark)
{
    if (key.isEmpty())
        return;
    const QString trimmed = remark.trimmed();
    if (m_remarks.value(key) == trimmed)
        return;
    if (trimmed.isEmpty())
        m_remarks.remove(key);
    else
        m_remarks.insert(key, trimmed);
    save();
    emit changed();
}

void ChatUiStore::setHidesPreview(const QString &key, bool hide)
{
    if (key.isEmpty() || m_hidePreview.contains(key) == hide)
        return;
    if (hide)
        m_hidePreview.insert(key);
    else
        m_hidePreview.remove(key);
    save();
    emit changed();
}
