/**
 * 探针用 OptiFineInstaller 桩：仅声明 LoaderDetailPage.cpp 引用到的接口。
 * 枚举取自真实 SettingsManager.h（探针编译真实 SettingsManager）。
 */
#ifndef OPTIFINEINSTALLER_H
#define OPTIFINEINSTALLER_H

#include <QObject>
#include <QString>
#include "utils/SettingsManager.h"

class OptiFineInstaller : public QObject
{
    Q_OBJECT
public:
    static OptiFineInstaller *instance()
    {
        static OptiFineInstaller m;
        return &m;
    }

    void setDownloadSource(OptiFineDownloadSource) {}
    void setCurrentTaskId(const QString &) {}
    void downloadOptiFineInstaller(const QString &, const QString &, const QString &,
                                   const QString &) {}

signals:
    void installProgressUpdated(int progress, const QString &status);
    void installCompleted(const QString &versionId);
    void installFailed(const QString &error);
    void installCancelled();
};

#endif // OPTIFINEINSTALLER_H
