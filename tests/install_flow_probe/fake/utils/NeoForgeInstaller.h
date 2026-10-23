/**
 * 探针用 NeoForgeInstaller 桩：仅声明 LoaderDetailPage.cpp 引用到的接口。
 * 枚举取自真实 SettingsManager.h（探针编译真实 SettingsManager）。
 */
#ifndef NEOFORGEINSTALLER_H
#define NEOFORGEINSTALLER_H

#include <QObject>
#include <QString>
#include "utils/SettingsManager.h"

class NeoForgeInstaller : public QObject
{
    Q_OBJECT
public:
    static NeoForgeInstaller *instance()
    {
        static NeoForgeInstaller m;
        return &m;
    }

    void setDownloadSource(NeoForgeDownloadSource) {}
    void setCurrentTaskId(const QString &) {}
    void downloadNeoForgeInstaller(const QString &, const QString &, const QString &,
                                   const QString & = QString()) {}

signals:
    void installCompleted(const QString &versionId);
    void installFailed(const QString &error);
};

#endif // NEOFORGEINSTALLER_H
