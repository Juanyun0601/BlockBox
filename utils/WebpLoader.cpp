#include "WebpLoader.h"
#include <QApplication>
#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QProcessEnvironment>
#include <QStandardPaths>

// Typedefs for libwebp functions
typedef int (*WebPGetInfoFunc)(const uint8_t*, size_t, int*, int*);
typedef uint8_t* (*WebPDecodeRGBAFunc)(const uint8_t*, size_t, int*, int*);
typedef void (*WebPFreeFunc)(void*);

QLibrary *WebpLoader::s_lib = nullptr;
QFunctionPointer WebpLoader::s_webpDecodeRGBA = nullptr;
QFunctionPointer WebpLoader::s_webpGetInfo = nullptr;
bool WebpLoader::s_resolved = false;

bool WebpLoader::resolveLib()
{
    if (s_resolved) return s_lib && s_lib->isLoaded();
    s_resolved = true;

    // Try to load libwebp with platform-specific names
    QStringList libNames;
#ifdef Q_OS_WIN
    libNames << "libwebp-7" << "libwebp" << "libwebp.dll";
#elif defined(Q_OS_MACOS)
    libNames << "libwebp.dylib" << "libwebp.0.dylib";
#else
    libNames << "libwebp.so" << "libwebp.so.0" << "libwebp.so.0.6.0";
#endif

    // Search paths: app dir, system PATH, common locations
    QStringList searchPaths;
    searchPaths << "." << QApplication::applicationDirPath();
#ifdef Q_OS_WIN
    // Windows: include System32/SysWOW64 for system-installed libraries
    QString sysRoot = qgetenv("SystemRoot");
    if (sysRoot.isEmpty()) sysRoot = "C:\\Windows";
    searchPaths << sysRoot + "\\System32" << sysRoot + "\\SysWOW64";
#endif

    // Add PATH entries
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains("PATH")) {
#ifdef Q_OS_WIN
        searchPaths << env.value("PATH").split(';', Qt::SkipEmptyParts);
#else
        searchPaths << env.value("PATH").split(':', Qt::SkipEmptyParts);
#endif
    }

    for (const QString &name : libNames) {
        for (const QString &path : searchPaths) {
            QString fullPath = path + "/" + name;
#ifdef Q_OS_WIN
            if (!fullPath.endsWith(".dll", Qt::CaseInsensitive))
                fullPath += ".dll";
#endif
            if (QFile::exists(fullPath)) {
                s_lib = new QLibrary(fullPath);
                if (s_lib->load()) {
                    qDebug() << "Loaded libwebp from:" << fullPath;
                    s_webpDecodeRGBA = s_lib->resolve("WebPDecodeRGBA");
                    s_webpGetInfo = s_lib->resolve("WebPGetInfo");
                    if (s_webpDecodeRGBA && s_webpGetInfo) {
                        qDebug() << "libwebp functions resolved successfully";
                        return true;
                    }
                    qDebug() << "libwebp found but missing required functions";
                    s_lib->unload();
                }
                delete s_lib;
                s_lib = nullptr;
            }
        }
    }

    qDebug() << "libwebp DLL not found - WebP decoding unavailable";
    return false;
}

QImage WebpLoader::decode(const QByteArray &data)
{
    if (data.isEmpty()) return QImage();

    // Try Qt's built-in support first (in case plugin is available)
    {
        QImageReader rdr;
        rdr.setFormat("webp");
        QBuffer buf;
        buf.setData(data);
        buf.open(QIODevice::ReadOnly);
        rdr.setDevice(&buf);
        QImage img = rdr.read();
        if (!img.isNull()) {
            return img;
        }
    }

    // Try libwebp via dynamic loading
    if (!resolveLib()) {
        return QImage();
    }

    auto fnGetInfo = reinterpret_cast<WebPGetInfoFunc>(s_webpGetInfo);
    auto fnDecode = reinterpret_cast<WebPDecodeRGBAFunc>(s_webpDecodeRGBA);

    int width = 0, height = 0;
    if (!fnGetInfo(reinterpret_cast<const uint8_t*>(data.constData()),
                   static_cast<size_t>(data.size()), &width, &height)) {
        qDebug() << "WebPGetInfo failed - not a valid WebP image";
        return QImage();
    }

    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        qDebug() << "Invalid WebP dimensions:" << width << "x" << height;
        return QImage();
    }

    uint8_t *rgba = fnDecode(reinterpret_cast<const uint8_t*>(data.constData()),
                             static_cast<size_t>(data.size()), &width, &height);
    if (!rgba) {
        qDebug() << "WebPDecodeRGBA failed";
        return QImage();
    }

    // Copy decoded RGBA data into a QImage
    QImage result(width, height, QImage::Format_RGBA8888);
    if (result.isNull()) {
        // Try to free via QLibrary
        static QFunctionPointer fnFree = s_lib ? s_lib->resolve("WebPFree") : nullptr;
        if (fnFree) reinterpret_cast<WebPFreeFunc>(fnFree)(rgba);
        else free(rgba);
        return QImage();
    }
    memcpy(result.bits(), rgba, static_cast<size_t>(width * height * 4));

    // Free the libwebp buffer
    static QFunctionPointer fnFree = s_lib ? s_lib->resolve("WebPFree") : nullptr;
    if (fnFree) reinterpret_cast<WebPFreeFunc>(fnFree)(rgba);
    else free(rgba);

    return result;
}