/**
 * 探针用 OptiFabricInstaller 桩：仅声明 LoaderDetailPage.cpp 引用到的接口。
 */
#ifndef OPTIFABRICINSTALLER_H
#define OPTIFABRICINSTALLER_H

#include <QObject>
#include <QString>

class OptiFabricInstaller : public QObject
{
    Q_OBJECT
public:
    static OptiFabricInstaller *instance()
    {
        static OptiFabricInstaller m;
        return &m;
    }

    void setCurrentTaskId(const QString &) {}
    void downloadOptiFabric(const QString &, const QString &, const QString &,
                            const QString &) {}

signals:
    void installCompleted(const QString &versionId);
    void installFailed(const QString &error);
};

#endif // OPTIFABRICINSTALLER_H
