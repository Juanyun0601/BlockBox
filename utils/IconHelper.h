#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QColor>
#include <QIcon>
#include <QString>

class IconHelper {
public:
    static QIcon loadColoredIcon(const QString &svgPath, const QColor &color, int size = 20);
    static void clearCache();
};

#endif
