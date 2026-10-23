/**
 * 探针用 CleanroomInstaller 桩：仅声明 LoaderDetailPage.cpp 引用到的接口。
 */
#ifndef CLEANROOMINSTALLER_H
#define CLEANROOMINSTALLER_H

#include <QObject>
#include <QString>

class CleanroomInstaller : public QObject
{
    Q_OBJECT
public:
    static CleanroomInstaller *instance()
    {
        static CleanroomInstaller m;
        return &m;
    }

    void setCurrentTaskId(const QString &) {}
    void downloadCleanroom(const QString &, const QString &, const QString &,
                           const QString &) {}

signals:
    void installCompleted(const QString &versionId);
    void installFailed(const QString &error);
};

#endif // CLEANROOMINSTALLER_H
