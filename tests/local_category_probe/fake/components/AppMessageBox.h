/**
 * @file   components/AppMessageBox.h
 * @brief  探针桩：仅满足 LocalCategoryDialog 删除确认的链接需求，不做真实 UI
 */
#ifndef APPMESSAGEBOX_H
#define APPMESSAGEBOX_H

#include <QString>
#include <QWidget>

class AppMessageBox
{
public:
    enum StandardButton {
        NoButton = 0x00000000,
        Yes = 0x00004000,
        No = 0x00010000
    };
    enum StandardButtons {
        None = 0x00000000
    };

    static StandardButton question(QWidget *, const QString &, const QString &,
                                   int = 0, StandardButton = No)
    {
        return No;
    }
};

// 允许 AppMessageBox::Yes | AppMessageBox::No 写法
inline int operator|(AppMessageBox::StandardButton a, AppMessageBox::StandardButton b)
{
    return int(a) | int(b);
}

#endif // APPMESSAGEBOX_H
