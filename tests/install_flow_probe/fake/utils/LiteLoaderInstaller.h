/**
 * 探针用 LiteLoaderInstaller 桩：仅声明 LoaderDetailPage.cpp 引用到的接口。
 */
#ifndef LITELOADERINSTALLER_H
#define LITELOADERINSTALLER_H

#include <QObject>
#include <QString>

class LiteLoaderInstaller : public QObject
{
    Q_OBJECT
public:
    static LiteLoaderInstaller *instance()
    {
        static LiteLoaderInstaller m;
        return &m;
    }

    void setCurrentTaskId(const QString &) {}
    void downloadLiteLoader(const QString &, const QString &, const QString &) {}

signals:
    void installCompleted(const QString &versionId);
    void installFailed(const QString &error);
};

#endif // LITELOADERINSTALLER_H
