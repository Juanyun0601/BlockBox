/**
 * @file   online_modpack_probe.cpp
 * @brief  在线整合包安装链路探针（无 GUI）
 *
 * 覆盖「资源>整合包 → 一键安装」真正的链路：
 *   1. 从网络下载真实整合包（CurseForge zip / Modrinth mrpack）
 *   2. ModpackDetector 认格式并解析出游戏版本 / 加载器 / 模组清单
 *   3. CurseForge 清单里只有文件 ID 时，按 ID 批量换取真实下载地址与文件名
 *      （旧实现用的 curseforge.com/api/v1/mods/x/files/y/download 已经 403）
 *
 * 用法：qmake online_modpack_probe.pro && mingw32-make && ./online_modpack_probe.exe
 */
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "utils/modpack/ModpackDetector.h"

using namespace modpack;

static QTextStream out(stdout);
static int failCount = 0;

static void check(bool cond, const QString& msg)
{
  out << (cond ? "[PASS] " : "[FAIL] ") << msg << "\n";
  out.flush();
  if (!cond)
    ++failCount;
}

/// 同步下载到本地文件（探针专用，非 GUI 线程阻塞等待）
static bool downloadTo(const QString& url, const QString& destPath, QString* error)
{
  QNetworkAccessManager nam;
  QNetworkRequest request{QUrl(url)};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(120000);
  QNetworkReply* reply = nam.get(request);

  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QTimer::singleShot(180000, &loop, &QEventLoop::quit);
  loop.exec();

  if (!reply->isFinished())
  {
    reply->abort();
    if (error)
      *error = QStringLiteral("下载超时");
    reply->deleteLater();
    return false;
  }

  const bool ok = (reply->error() == QNetworkReply::NoError);
  if (!ok && error)
    *error = reply->errorString();

  QFile file(destPath);
  if (ok && file.open(QIODevice::WriteOnly))
  {
    file.write(reply->readAll());
    file.close();
  }
  else if (!ok)
  {
    // 保持失败状态
  }
  else
  {
    if (error)
      *error = QStringLiteral("无法写入 %1").arg(destPath);
  }
  reply->deleteLater();
  return ok && file.size() > 0;
}

/// 按文件 ID 批量向 CurseForge（镜像）换取下载地址与文件名
static QJsonArray resolveCurseForgeFiles(const QStringList& fileIds, QString* error)
{
  if (fileIds.isEmpty())
    return QJsonArray();

  QJsonArray ids;
  for (const QString& id : fileIds)
  {
    bool ok = false;
    const qint64 value = id.toLongLong(&ok);
    if (ok && value > 0)
      ids.append(value);
  }
  if (ids.isEmpty())
    return QJsonArray();

  QNetworkAccessManager nam;
  QNetworkRequest request{
    QUrl(QStringLiteral("https://mod.mcimirror.top/curseforge/v1/mods/files"))};
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader("Accept", "application/json");
  request.setTransferTimeout(60000);

  QJsonObject body;
  body.insert(QStringLiteral("fileIds"), ids);

  QNetworkReply* reply =
    nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QTimer::singleShot(90000, &loop, &QEventLoop::quit);
  loop.exec();

  QJsonArray files;
  if (!reply->isFinished())
  {
    reply->abort();
    if (error)
      *error = QStringLiteral("解析请求超时");
  }
  else if (reply->error() != QNetworkReply::NoError)
  {
    if (error)
      *error = reply->errorString();
  }
  else
  {
    files = QJsonDocument::fromJson(reply->readAll())
              .object().value(QStringLiteral("data")).toArray();
  }
  reply->deleteLater();
  return files;
}

static void probeCurseForgePack(const QString& workDir)
{
  out << "\n=== CurseForge 整合包 ===\n";
  out.flush();

  const QString url =
    QStringLiteral("https://edge.forgecdn.net/files/8697/67/1.21.1%20Turbopack%20Up.zip");
  const QString path = workDir + "/cf-pack.zip";

  QString error;
  const bool downloaded = downloadTo(url, path, &error);
  check(downloaded, QStringLiteral("下载 CurseForge 整合包 %1").arg(error));
  if (!downloaded)
    return;

  check(ModpackDetector::detect(path) == ModpackType::CurseForge,
        QStringLiteral("识别为 CurseForge 格式"));

  const ModpackInfo info = ModpackDetector::parse(path);
  check(info.name == QStringLiteral("1.21.1 Turbopack Up"),
        QStringLiteral("包名解析：%1").arg(info.name));
  check(!info.gameVersion.isEmpty(),
        QStringLiteral("游戏版本解析：%1").arg(info.gameVersion));
  check(!info.loaderType.isEmpty() && !info.loaderVersion.isEmpty(),
        QStringLiteral("加载器解析：%1 %2").arg(info.loaderType, info.loaderVersion));
  check(info.mods.size() > 0,
        QStringLiteral("清单模组数量：%1").arg(info.mods.size()));
  check(info.overridesPath == QStringLiteral("overrides"),
        QStringLiteral("覆盖目录：%1").arg(info.overridesPath));

  // 清单里只有 projectID / fileID，必须能换成真实地址
  QStringList fileIds;
  for (int i = 0; i < qMin(5, info.mods.size()); ++i)
    fileIds << info.mods.at(i).fileId;

  const QJsonArray files = resolveCurseForgeFiles(fileIds, &error);
  check(files.size() == fileIds.size(),
        QStringLiteral("按文件 ID 换取地址：请求 %1，返回 %2 %3")
          .arg(fileIds.size()).arg(files.size()).arg(error));

  int withUrl = 0;
  int withName = 0;
  for (const QJsonValue& value : files)
  {
    const QJsonObject obj = value.toObject();
    if (!obj.value(QStringLiteral("downloadUrl")).toString().isEmpty())
      ++withUrl;
    if (!obj.value(QStringLiteral("fileName")).toString().isEmpty())
      ++withName;
  }
  check(withUrl == files.size(),
        QStringLiteral("每个文件都有下载地址（%1/%2）").arg(withUrl).arg(files.size()));
  check(withName == files.size(),
        QStringLiteral("每个文件都有文件名（%1/%2）").arg(withName).arg(files.size()));

  if (!files.isEmpty())
  {
    const QJsonObject first = files.first().toObject();
    out << "    示例：" << first.value(QStringLiteral("fileName")).toString() << " -> "
        << first.value(QStringLiteral("downloadUrl")).toString() << "\n";
    out.flush();

    // 抽第一个文件真实下载一次，确认地址可用
    QString dlError;
    const QString probeFile = workDir + "/"
      + first.value(QStringLiteral("fileName")).toString();
    check(downloadTo(first.value(QStringLiteral("downloadUrl")).toString(), probeFile, &dlError),
          QStringLiteral("模组文件可真实下载 %1").arg(dlError));
  }
}

static void probeModrinthPack(const QString& workDir)
{
  out << "\n=== Modrinth 整合包 ===\n";
  out.flush();

  const QString url = QStringLiteral(
    "https://cdn.modrinth.com/data/1KVo5zza/versions/zF1GaleB/"
    "Fabulously.Optimized-v12.0.1.mrpack");
  const QString path = workDir + "/mr-pack.mrpack";

  QString error;
  const bool downloaded = downloadTo(url, path, &error);
  check(downloaded, QStringLiteral("下载 Modrinth 整合包 %1").arg(error));
  if (!downloaded)
    return;

  check(ModpackDetector::detect(path) == ModpackType::Modrinth,
        QStringLiteral("识别为 Modrinth 格式"));

  const ModpackInfo info = ModpackDetector::parse(path);
  check(!info.gameVersion.isEmpty(),
        QStringLiteral("游戏版本解析：%1").arg(info.gameVersion));
  check(!info.loaderType.isEmpty() && !info.loaderVersion.isEmpty(),
        QStringLiteral("加载器解析：%1 %2").arg(info.loaderType, info.loaderVersion));
  check(info.mods.size() > 0,
        QStringLiteral("清单文件数量：%1").arg(info.mods.size()));

  int withUrl = 0;
  int withSha1 = 0;
  int pathStyle = 0;
  for (const ModInfo& mod : info.mods)
  {
    if (!mod.downloadUrl.isEmpty())
      ++withUrl;
    if (!mod.sha1.isEmpty())
      ++withSha1;
    if (mod.name.contains(QLatin1Char('/')))
      ++pathStyle;
  }
  check(withUrl == info.mods.size(),
        QStringLiteral("每个文件都有下载地址（%1/%2）").arg(withUrl).arg(info.mods.size()));
  check(withSha1 == info.mods.size(),
        QStringLiteral("每个文件都有 sha1 校验值（%1/%2）").arg(withSha1).arg(info.mods.size()));
  check(pathStyle == info.mods.size(),
        QStringLiteral("清单路径形如 mods/xxx.jar（%1/%2）").arg(pathStyle).arg(info.mods.size()));
}

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);

  const QString workDir = QDir::tempPath() + "/blockbox_online_modpack_probe";
  QDir(workDir).removeRecursively();
  QDir().mkpath(workDir);

  out << "在线整合包安装链路探针\n工作目录：" << workDir << "\n";
  out.flush();

  probeModrinthPack(workDir);
  probeCurseForgePack(workDir);

  out << "\n结果：" << (failCount == 0 ? "全部通过" : QStringLiteral("%1 项失败").arg(failCount))
      << "\n";
  out.flush();
  return failCount == 0 ? 0 : 1;
}
