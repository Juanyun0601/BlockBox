/**
 * @file   ForgeProfile.h
 * @brief  Forge安装配置数据结构定义
 * @author BlockBox Team
 * @date   2026-06-01
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QVector>

namespace forge {

/**
 * @brief Maven坐标工件描述
 */
struct ForgeArtifact
{
    QString group;
    QString artifact;
    QString version;
    QString classifier;
    QString extension;

    QString name;

    static ForgeArtifact fromName(const QString &name);
    QString toPath() const;
    QString toFileName() const;
};

/**
 * @brief 库文件描述
 */
struct ForgeLibrary
{
    QString name;
    QString url;
    QStringList candidateUrls;
    QString path;
    QString sha1;
    qint64 size = 0;
    bool isNative = false;

    static ForgeLibrary fromJson(const QJsonObject &obj);
};

/**
 * @brief 处理器输出文件描述
 */
struct ForgeProcessorOutput
{
    QString filePath;
    QString sha1;
};

/**
 * @brief 处理器描述
 */
struct ForgeProcessor
{
    QString jar;
    QStringList classpath;
    QJsonArray args;
    QMap<QString, QString> outputs;
    QStringList sides;
};

/**
 * @brief 安装配置文件
 */
struct ForgeInstallProfile
{
    int spec = 0;
    QString minecraft;
    QString json;
    QString path;
    QString version;
    QJsonArray libraries;
    QVector<ForgeProcessor> processors;
    QMap<QString, QJsonValue> data;

    static ForgeInstallProfile fromJson(const QJsonObject &obj);
};

} // namespace forge