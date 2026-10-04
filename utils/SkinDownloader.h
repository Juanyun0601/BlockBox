/**
 * @file   SkinDownloader.h
 * @brief  皮肤下载器类声明，支持多来源皮肤加载（用户名 URL、UUID Crafatar API、Qt 资源官方皮肤）
 * @author BlockBox Team
 * @date   2026-07-01
 */

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

class SkinDownloader : public QObject
{
  Q_OBJECT

public:
  explicit SkinDownloader(QObject* parent = nullptr);
  ~SkinDownloader();

  static SkinDownloader* instance();

  /**
   * @brief 获取指定名称的皮肤缓存路径
   * @param name 缓存标识（用户名或归一化 UUID）
   * @return 缓存文件路径，格式为 {AppData}/BlockBox/cache/skins/{name}.png
   */
  static QString getCachePath(const QString& name);

  /**
   * @brief 获取离线皮肤目录（相对应用目录，非绝对路径）
   * @return 离线皮肤目录路径，格式为 {applicationDirPath}/Images/Skins
   *
   * 用于存放离线账户皮肤及官方模板，与 BlockBoxWindows.exe 同级。
   * 用户可在此目录放置 {username}.png 为离线账户自定义皮肤。
   */
  static QString offlineSkinsDir();

  /**
   * @brief 获取指定用户名的离线皮肤文件路径
   * @param username 用户名
   * @return 离线皮肤文件路径，格式为 {offlineSkinsDir}/{username}.png
   */
  static QString getOfflineSkinPath(const QString& username);

  /**
   * @brief 尝试从离线皮肤目录加载指定用户的皮肤
   * @param username 用户名
   * @param[out] image 输出加载的 QImage（成功时填充）
   * @return 加载成功返回 true，文件不存在或加载失败返回 false
   */
  static bool loadOfflineSkin(const QString& username, QImage& image);

  /**
   * @brief 下载指定用户的皮肤纹理（基于用户名 URL）
   * @param username 用户名，用于缓存标识
   * @param skinUrl  皮肤图片的远程 URL
   *
   * 优先检查本地缓存，若已缓存则直接加载；否则发起异步网络下载，
   * 超时时间为 10 秒。
   */
  void downloadSkin(const QString& username, const QString& skinUrl);

  /**
   * @brief 基于 UUID 通过 Crafatar API 下载皮肤纹理
   * @param uuid 玩家 UUID（可含或不含横线）
   *
   * 内部会先归一化 UUID（移除横线），再检查缓存，最后发起 Crafatar 请求。
   */
  void downloadSkinByUuid(const QString& uuid);

  /**
   * @brief 加载默认 Steve 皮肤（从 Qt 资源加载）
   * @return 资源中的 Steve 皮肤 QImage；若加载失败返回空 QImage
   */
  static QImage defaultSkin();

  /**
   * @brief 根据用户名返回对应的官方皮肤，未知用户名返回 Steve
   * @param username 用户名（大小写不敏感匹配官方皮肤名）
   * @return 对应官方皮肤 QImage；若不匹配官方皮肤列表则返回 defaultSkin()
   *
   * 官方皮肤列表：Alex, Ari, Efe, Kai, Makena, Noor, Steve, Sunny, Zuri
   */
  static QImage getDefaultSkinForUser(const QString& username);

  /**
   * @brief 从皮肤纹理中截取头部正面（含帽子/头发层）作为头像
   * @param skin       皮肤纹理 QImage（支持 64x32 / 64x64 新旧格式）
   * @param targetSize 目标头像边长（正方形），默认 64；使用最近邻缩放保留像素风
   * @return 裁切叠加后的头像 QImage；皮肤无效或尺寸不足返回空 QImage
   *
   * 纹理布局（头正面位于 (8,8) 尺寸 8x8，帽子/头发正面位于 (40,8) 尺寸 8x8）：
   * 先将头部正面按目标尺寸最近邻放大，再将帽子层叠加其上，得到带头发/帽子的大头照。
   */
  static QImage cropHeadPortrait(const QImage& skin, int targetSize = 64);

  /**
   * @brief 同步加载指定账户的皮肤（离线目录 > 缓存 > 默认皮肤），用于无需联网的场景
   * @param username 账户名
   * @param uuid     账户 UUID（可为空，用于读取 Crafatar 皮肤缓存）
   * @return 加载到的皮肤 QImage；均不可用时回退到默认皮肤
   */
  static QImage loadSkinForAccount(const QString& username, const QString& uuid = QString());

public slots:
  /**
   * @brief 加载默认皮肤，发出 skinLoaded 信号
   */
  void loadDefaultSkin();

signals:
  /**
   * @brief 皮肤纹理加载成功
   * @param texture 已加载的 QImage 纹理
   */
  void skinLoaded(const QImage& texture);

  /**
   * @brief 皮肤纹理加载失败（网络错误、超时或数据无效）
   */
  void skinLoadFailed();

private slots:
  void onReplyFinished(QNetworkReply* reply);
  void onTimeout();

private:
  /**
   * @brief 通用下载启动函数
   * @param key 缓存标识（用户名或归一化 UUID）
   * @param url  远程皮肤图片 URL
   */
  void startDownload(const QString& key, const QString& url);

  /**
   * @brief 从本地缓存加载皮肤
   * @param name 缓存标识（用户名或归一化 UUID）
   * @return 加载成功返回 true，并发出 skinLoaded 信号；否则返回 false
   */
  bool loadFromCache(const QString& name);

  /**
   * @brief 获取缓存目录路径
   * @return 缓存目录绝对路径
   */
  static QString cacheDir();

  /**
   * @brief 归一化 UUID，移除其中所有横线
   * @param uuid 原始 UUID 字符串
   * @return 仅包含十六进制字符的 UUID 字符串
   */
  static QString normalizeUuid(const QString& uuid);

  QNetworkAccessManager* m_networkManager;
  static SkinDownloader* m_instance;
  static QMutex m_instanceMutex;

  static constexpr int TIMEOUT_MS = 10000;

  // 正在进行的下载信息
  struct PendingDownload
  {
    QNetworkReply* reply;
    QTimer* timer;
    QString key;
    QString url;
  };

  // 缓存标识 -> 当前下载信息（防止重复下载）
  QHash<QString, PendingDownload> m_pendingDownloads;
};
