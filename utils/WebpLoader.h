#ifndef WEBPLOADER_H
#define WEBPLOADER_H

#include <QByteArray>
#include <QImage>
#include <QLibrary>
#include <QString>

class WebpLoader
{
public:
    static QImage decode(const QByteArray &data);

private:
    static bool resolveLib();
    static QLibrary *s_lib;
    static QFunctionPointer s_webpDecodeRGBA;
    static QFunctionPointer s_webpGetInfo;
    static bool s_resolved;
};

#endif // WEBPLOADER_H