/**
 * 探针用 ForgeInstaller 桩：仅声明 LoaderDetailPage.cpp / ForgeVersionListPage.cpp
 * 引用到的接口，避免拉入真实安装器依赖链。
 */
#ifndef FORGEINSTALLER_H
#define FORGEINSTALLER_H

#include <QObject>
#include <QString>

class ForgeInstaller : public QObject
{
    Q_OBJECT
public:
    static ForgeInstaller *instance()
    {
        static ForgeInstaller m;
        return &m;
    }

    explicit ForgeInstaller(QObject *parent = nullptr) : QObject(parent) {}

    void setCurrentTaskId(const QString &) {}
    void setVersionIsolationEnabled(bool) {}
    void downloadForgeInstaller(const QString &, const QString &, const QString &,
                                const QString & = QString()) {}

    QString currentTaskId() const { return QString(); }
    void setInstancePath(const QString &) {}
    void setMcVersion(const QString &) {}
    void setForgeVersion(const QString &) {}
    void startInstall() {}
    void cancelInstall() {}

signals:
    void installStarted(const QString &taskId);
    void installProgress(const QString &taskId, const QString &stage, int percent);
    void installCompleted(const QString &taskId);
    void installFailed(const QString &taskId, const QString &error);
    void installCancelled(const QString &taskId);
};

#endif // FORGEINSTALLER_H
