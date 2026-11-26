/**
 * @file   ChatPrefs.cpp
 * @brief  聊天页通用偏好（发送方式 / 消息字号）的读写实现
 * @author BlockBox Team
 * @date   2026-11-20
 */
#include "ChatPrefs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "../platform.h"

ChatPrefs *ChatPrefs::instance()
{
    static ChatPrefs *s_instance = new ChatPrefs();
    return s_instance;
}

ChatPrefs::ChatPrefs(QObject *parent) : QObject(parent)
{
    load();
}

QString ChatPrefs::filePath() const
{
    return Platform::getDataDirectory() + QStringLiteral("/chat_prefs.json");
}

void ChatPrefs::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.contains(QStringLiteral("sendOnEnter")))
        m_sendOnEnter = root.value(QStringLiteral("sendOnEnter")).toBool(true);
    if (root.contains(QStringLiteral("p2pDirectSend")))
        m_p2pDirectSend = root.value(QStringLiteral("p2pDirectSend")).toBool(true);

    const QString size = root.value(QStringLiteral("fontSize")).toString();
    if (size == QStringLiteral("small"))
        m_fontSize = FontSmall;
    else if (size == QStringLiteral("large"))
        m_fontSize = FontLarge;
    else
        m_fontSize = FontNormal;
}

void ChatPrefs::save()
{
    QJsonObject root;
    root.insert(QStringLiteral("sendOnEnter"), m_sendOnEnter);
    root.insert(QStringLiteral("p2pDirectSend"), m_p2pDirectSend);
    QString size = QStringLiteral("normal");
    if (m_fontSize == FontSmall)
        size = QStringLiteral("small");
    else if (m_fontSize == FontLarge)
        size = QStringLiteral("large");
    root.insert(QStringLiteral("fontSize"), size);

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    // 写失败时 QSaveFile 不会覆盖原文件，下次启动沿用旧设置
    file.commit();
}

void ChatPrefs::setSendOnEnter(bool on)
{
    if (m_sendOnEnter == on)
        return;
    m_sendOnEnter = on;
    save();
    emit changed();
}

void ChatPrefs::setP2pDirectSend(bool on)
{
    if (m_p2pDirectSend == on)
        return;
    m_p2pDirectSend = on;
    save();
    emit changed();
}

void ChatPrefs::setFontSize(FontSize size)
{
    if (size != FontSmall && size != FontNormal && size != FontLarge)
        return;
    if (m_fontSize == size)
        return;
    m_fontSize = size;
    save();
    emit changed();
}

int ChatPrefs::fontPx(int base) const
{
    int delta = 0;
    if (m_fontSize == FontSmall)
        delta = -1;
    else if (m_fontSize == FontLarge)
        delta = 2;
    return qMax(9, base + delta);
}
