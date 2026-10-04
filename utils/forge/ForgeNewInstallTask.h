/**
 * @file   ForgeNewInstallTask.h
 * @brief  Forge新版本安装任务
 * @author BlockBox Team
 * @date   2026-06-01
 */

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>
#include <atomic>

#include "ForgeProfile.h"
#include "../JarUtils.h"

namespace forge {

struct ForgeProcessor;

class ForgeNewInstallTask : public QObject
{
    Q_OBJECT
public:
    ForgeNewInstallTask(const QString &installerJarPath, const QString &instancePath,
                        QObject *parent = nullptr);
    ~ForgeNewInstallTask();

    void execute();
    void cancel();
    void setTaskId(const QString &taskId);

signals:
    void progressUpdated(const QString &stage, int percent);
    void completed(bool success, const QString &error);

private:
    void preExecute();
    void onPreExecuteDone();
    void onPreExtractDone();
    void doExecute();
    void onExecuteDone();
    void postExecute();
    void onPostExecuteDone();

    void parseInstallProfile();
    void extractVersionJson();
    void downloadProcessorDependencies();
    void copyEmbeddedLibraries();
    void buildVariables();
    void runProcessors();
    void runProcessorsAsync();
    void onRunProcessorsDone(bool success, const QString &error);
    void generateVersionJson();
    void downloadRemainingLibraries();
    void cleanupTempFiles();

    QString getLibraryPath(const QString &libraryName);
    QString getJavaPath();
    QString readManifestMainClass(const QString &jarPath);
    QString replaceVariables(const QString &str, const QMap<QString, QString> &vars);
    QString parseLiteral(const QString &literal, const QMap<QString, QString> &vars);
    QString calculateSha1(const QString &filePath);

    QString m_installerJarPath;
    QString m_instancePath;
    QString m_tempDir;
    ForgeInstallProfile m_profile;
    QJsonObject m_baseVersionJson;
    QMap<QString, QString> m_variables;
    std::atomic<bool> m_cancelled;
    int m_processorTotal;
    int m_processorDone;
    QString m_taskId;
};

} // namespace forge