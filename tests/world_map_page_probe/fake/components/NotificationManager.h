/**
 * 探针用 NotificationManager 桩：WorldMapPage 只用到 showSuccess（复制坐标提示），
 * 真实实现会连带 NotificationCard / ThemeManager 一整条依赖链。
 */
#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include <QString>

class QWidget;

class NotificationManager
{
public:
    static void showSuccess(QWidget *parent, const QString &text, int duration = 5000)
    {
        Q_UNUSED(parent)
        Q_UNUSED(text)
        Q_UNUSED(duration)
    }
};

#endif // NOTIFICATIONMANAGER_H
