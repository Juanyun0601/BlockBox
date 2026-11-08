/**
 * 探针用 FabricInstaller 桩：仅声明 LoaderDetailPage.cpp 引用到的接口。
 */
#ifndef FABRICINSTALLER_H
#define FABRICINSTALLER_H

#include <QObject>
#include <QString>
#include "utils/SettingsManager.h"

class FabricInstaller : public QObject
{
    Q_OBJECT
public:
    static FabricInstaller *instance()
    {
        static FabricInstaller m;
        return &m;
    }

    void setCurrentTaskId(const QString &) {}
    void setDownloadSource(FabricDownloadSource) {}
    void downloadFabricInstaller(const QString &, const QString &, const QString &) {}

signals:
    void installCompleted(const QString &versionId);
    void installFailed(const QString &error);
};

#endif // FABRICINSTALLER_H
