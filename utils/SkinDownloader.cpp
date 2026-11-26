/**
 * @file   SkinDownloader.cpp
 * @brief  皮肤下载器类实现，支持多来源皮肤加载（用户名 URL、UUID Crafatar API、Qt 资源官方皮肤）
 * @author BlockBox Team
 * @date   2026-07-01
 */

#include "SkinDownloader.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QStringList>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include "../platform.h"

SkinDownloader* SkinDownloader::m_instance = nullptr;
QMutex SkinDownloader::m_instanceMutex;

SkinDownloader::SkinDownloader(QObject* parent)
  : QObject(parent)
{
  m_networkManager = new QNetworkAccessManager(this);
  connect(m_networkManager, &QNetworkAccessManager::finished,
          this, &SkinDownloader::onReplyFinished);
}

SkinDownloader::~SkinDownloader()
{
  // 取消所有进行中的下载
  for (auto& download : m_pendingDownloads)
  {
    download.timer->stop();
    download.reply->abort();
    // abort() 后 reply 不会被自动释放，需 deleteLater() 避免网络对象泄漏
    download.reply->deleteLater();
    delete download.timer;
  }
  m_pendingDownloads.clear();

  m_instance = nullptr;
}

SkinDownloader* SkinDownloader::instance()
{
  if (!m_instance)
  {
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance)
    {
      m_instance = new SkinDownloader();
    }
  }
  return m_instance;
}

QString SkinDownloader::cacheDir()
{
  return Platform::getDataDirectory() + "/cache/skins";
}

QString SkinDownloader::getCachePath(const QString& name)
{
  return cacheDir() + "/" + name + ".png";
}

QString SkinDownloader::offlineSkinsDir()
{
  // 相对应用目录的路径（不使用绝对路径常量），与可执行文件同级
  return QApplication::applicationDirPath() + "/Images/Skins";
}

QString SkinDownloader::getOfflineSkinPath(const QString& username)
{
  return offlineSkinsDir() + "/" + username + ".png";
}

bool SkinDownloader::loadOfflineSkin(const QString& username, QImage& image)
{
  if (username.isEmpty()) return false;
  const QString path = getOfflineSkinPath(username);
  if (!QFile::exists(path)) return false;
  QImage loaded(path);
  if (loaded.isNull()) return false;
  image = loaded;
  return true;
}

QString SkinDownloader::offlineCapesDir()
{
  // 与离线皮肤同层放在应用目录下，方便用户自己往里面放图
  return QApplication::applicationDirPath() + "/Images/Capes";
}

QString SkinDownloader::getOfflineCapePath(const QString& username)
{
  return offlineCapesDir() + "/" + username + ".png";
}

bool SkinDownloader::loadOfflineCape(const QString& username, QImage& image)
{
  if (username.isEmpty()) return false;
  const QString path = getOfflineCapePath(username);
  if (!QFile::exists(path)) return false;
  QImage loaded(path);
  if (loaded.isNull()) return false;
  image = loaded;
  return true;
}

QImage SkinDownloader::normalizeSkinTexture(const QImage& image)
{
  if (image.isNull()) return QImage();

  // 皮肤图至少得有 64 宽、32 高，否则不可能是标准皮肤
  if (image.width() < 64 || image.height() < 32) return QImage();

  QImage tex = image;
  if (tex.width() == 64 && (tex.height() == 32 || tex.height() == 64))
    return tex;

  // 整数倍放大的皮肤（128×64、128×128、256×128……）按倍数缩回标准尺寸
  if (tex.width() % 64 == 0)
  {
    const int factor = tex.width() / 64;
    const int height = tex.height() / factor;
    if ((height == 32 || height == 64) && tex.height() % factor == 0)
    {
      tex = tex.scaled(64, height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
      if (tex.width() == 64 && (tex.height() == 32 || tex.height() == 64))
        return tex;
    }
  }

  // 其他尺寸：按宽度等比缩到 64，缩完高度不是 32 / 64 就不认
  tex = tex.scaledToWidth(64, Qt::FastTransformation);
  if (tex.height() == 32 || tex.height() == 64)
    return tex;

  return QImage();
}

QImage SkinDownloader::normalizeCapeTexture(const QImage& image)
{
  if (image.isNull()) return QImage();

  // 披风纹理是 64×32（也有 128×64 这类整数倍版本）
  if (image.width() < 64 || image.height() < 32) return QImage();

  QImage tex = image;
  if (tex.width() % 64 == 0)
  {
    const int factor = tex.width() / 64;
    const int height = tex.height() / factor;
    if (height >= 32 && tex.height() % factor == 0)
      tex = tex.scaled(64, height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
  }
  if (tex.width() != 64)
    tex = tex.scaledToWidth(64, Qt::FastTransformation);
  if (tex.height() < 32) return QImage();
  if (tex.height() > 32)
    tex = tex.copy(0, 0, 64, 32);

  // 标准披风的 Box UV 只用到左上角 22×17 的一块，
  // 这块全是透明的话说明选错了图（比如传了张皮肤），直接拒绝
  const QImage argb = tex.convertToFormat(QImage::Format_ARGB32);
  bool hasContent = false;
  for (int y = 0; y < 17 && !hasContent; ++y)
  {
    for (int x = 0; x < 22; ++x)
    {
      if (qAlpha(argb.pixel(x, y)) > 8)
      {
        hasContent = true;
        break;
      }
    }
  }
  if (!hasContent) return QImage();

  return tex;
}

bool SkinDownloader::saveOfflineSkin(const QString& username, const QImage& image)
{
  if (username.isEmpty() || image.isNull()) return false;
  QDir().mkpath(offlineSkinsDir());
  return image.save(getOfflineSkinPath(username), "PNG");
}

bool SkinDownloader::removeOfflineSkin(const QString& username)
{
  if (username.isEmpty()) return false;
  const QString path = getOfflineSkinPath(username);
  if (!QFile::exists(path)) return true;
  return QFile::remove(path);
}

bool SkinDownloader::saveOfflineCape(const QString& username, const QImage& image)
{
  if (username.isEmpty() || image.isNull()) return false;
  QDir().mkpath(offlineCapesDir());
  return image.save(getOfflineCapePath(username), "PNG");
}

bool SkinDownloader::removeOfflineCape(const QString& username)
{
  if (username.isEmpty()) return false;
  const QString path = getOfflineCapePath(username);
  if (!QFile::exists(path)) return true;
  return QFile::remove(path);
}

QString SkinDownloader::normalizeUuid(const QString& uuid)
{
  QString result = uuid;
  result.remove('-');
  return result;
}

bool SkinDownloader::loadFromCache(const QString& name)
{
  QString path = getCachePath(name);
  QFileInfo fileInfo(path);

  if (!fileInfo.exists())
  {
    return false;
  }

  QImageReader reader(path);
  reader.setAutoTransform(true);
  QImage image = reader.read();

  if (image.isNull())
  {
    // 缓存文件无效，删除并视为未缓存
    QFile::remove(path);
    return false;
  }

  emit skinLoaded(image);
  return true;
}

void SkinDownloader::downloadSkin(const QString& username, const QString& skinUrl)
{
  // 先检查缓存
  if (loadFromCache(username))
  {
    return;
  }

  // 检查是否已有相同用户名的下载在进行中
  if (m_pendingDownloads.contains(username))
  {
    return;
  }

  startDownload(username, skinUrl);
}

void SkinDownloader::downloadSkinByUuid(const QString& uuid)
{
  // 归一化 UUID（移除横线）
  QString normalizedUuid = normalizeUuid(uuid);

  // 先检查缓存
  if (loadFromCache(normalizedUuid))
  {
    return;
  }

  // 检查是否已有相同 UUID 的下载在进行中
  if (m_pendingDownloads.contains(normalizedUuid))
  {
    return;
  }

  // 构造 Crafatar 皮肤 URL
  QString url = "https://crafatar.com/skins/" + normalizedUuid;
  startDownload(normalizedUuid, url);
}

void SkinDownloader::startDownload(const QString& key, const QString& url)
{
  QNetworkRequest request{QUrl(url)};
  request.setTransferTimeout(TIMEOUT_MS);

  QNetworkReply* reply = m_networkManager->get(request);

  QTimer* timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, &SkinDownloader::onTimeout);

  PendingDownload download;
  download.reply = reply;
  download.timer = timer;
  download.key = key;
  download.url = url;

  m_pendingDownloads.insert(key, download);
  timer->start(TIMEOUT_MS);
}

void SkinDownloader::onReplyFinished(QNetworkReply* reply)
{
  // 查找对应的下载信息
  QString key;
  for (auto it = m_pendingDownloads.begin(); it != m_pendingDownloads.end(); ++it)
  {
    if (it.value().reply == reply)
    {
      key = it.key();
      break;
    }
  }

  if (key.isEmpty())
  {
    reply->deleteLater();
    return;
  }

  PendingDownload download = m_pendingDownloads.take(key);
  download.timer->stop();
  delete download.timer;

  if (reply->error() != QNetworkReply::NoError)
  {
    reply->deleteLater();
    emit skinLoadFailed();
    return;
  }

  QByteArray data = reply->readAll();
  reply->deleteLater();

  QImage image;
  if (!image.loadFromData(data))
  {
    emit skinLoadFailed();
    return;
  }

  // 保存到缓存
  QString dir = cacheDir();
  QDir().mkpath(dir);

  QString filePath = getCachePath(key);
  QFile file(filePath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    file.write(data);
    file.close();
  }

  emit skinLoaded(image);
}

void SkinDownloader::onTimeout()
{
  QTimer* timer = qobject_cast<QTimer*>(sender());
  if (!timer)
  {
    return;
  }

  // 找到对应的下载
  for (auto it = m_pendingDownloads.begin(); it != m_pendingDownloads.end(); ++it)
  {
    if (it.value().timer == timer)
    {
      it.value().reply->abort();
      delete it.value().timer;
      m_pendingDownloads.erase(it);
      emit skinLoadFailed();
      return;
    }
  }
}

QImage SkinDownloader::defaultSkin()
{
  QImage skin(":/Images/Skins/Steve.png");
  if (skin.isNull())
  {
    // 资源加载失败返回空 QImage
    return QImage();
  }
  return skin;
}

QImage SkinDownloader::getDefaultSkinForUser(const QString& username)
{
  // 官方皮肤名列表
  static const QStringList officialSkins = {
    "Alex", "Ari", "Efe", "Kai", "Makena",
    "Noor", "Steve", "Sunny", "Zuri"
  };

  for (const QString& skinName : officialSkins)
  {
    if (username.compare(skinName, Qt::CaseInsensitive) == 0)
    {
      QImage skin(":/Images/Skins/" + skinName + ".png");
      if (!skin.isNull())
      {
        return skin;
      }
      // 资源缺失则回退到默认皮肤
      break;
    }
  }

  return defaultSkin();
}

QImage SkinDownloader::cropHeadPortrait(const QImage& skin, int targetSize)
{
  if (skin.isNull() || skin.width() < 64 || skin.height() < 32 || targetSize <= 0)
  {
    return QImage();
  }

  // 头部正面（head front）：纹理坐标 (8,8) 尺寸 8x8
  const QImage headFace = skin.copy(8, 8, 8, 8);
  // 帽子/头发正面（hat front）：纹理坐标 (40,8) 尺寸 8x8，叠加在头部上方
  const QImage hatFront = skin.copy(40, 8, 8, 8);

  QImage portrait(targetSize, targetSize, QImage::Format_ARGB32_Premultiplied);
  portrait.fill(Qt::transparent);

  // 最近邻放大，保留像素画质感（不进行平滑插值）
  QPainter painter(&portrait);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  const QRect dst(0, 0, targetSize, targetSize);
  painter.drawImage(dst, headFace);
  painter.drawImage(dst, hatFront); // 帽子层透明区域透出头部
  painter.end();

  return portrait;
}

QImage SkinDownloader::loadSkinForAccount(const QString& username, const QString& uuid)
{
  QImage skin;
  // 优先级 1：离线皮肤目录（{applicationDirPath}/Images/Skins/{username}.png）
  if (loadOfflineSkin(username, skin) && !skin.isNull())
  {
    return skin;
  }
  // 优先级 2：UUID 皮肤缓存（Crafatar 下载缓存）
  if (!uuid.isEmpty())
  {
    skin = QImage(getCachePath(normalizeUuid(uuid)));
    if (!skin.isNull())
    {
      return skin;
    }
  }
  // 优先级 3：用户名皮肤缓存
  skin = QImage(getCachePath(username));
  if (!skin.isNull())
  {
    return skin;
  }
  // 优先级 4：回退到默认皮肤（根据用户名匹配官方皮肤）
  return getDefaultSkinForUser(username);
}

void SkinDownloader::loadDefaultSkin()
{
  emit skinLoaded(defaultSkin());
}
