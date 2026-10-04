#ifndef MODSCANNER_H
#define MODSCANNER_H

#include <QObject>
#include <QDateTime>
#include <QMutex>
#include <QMap>
#include <QStringList>
#include "ModData.h"

/**
 * @brief 本地模组扫描器，扫描 Minecraft 实例的 mods/ 目录并提取 JAR 元数据
 *
 * 性能优化（参考 PrismLauncher/MultiMC 做法）：
 * - 内存直取，跳过临时文件
 * - 批量提取同一JAR中的多个元数据条目（一次ZIP遍历）
 * - 多线程并行扫描
 * - 基于文件修改时间的缓存
 */
class ModScanner : public QObject
{
  Q_OBJECT

public:
  explicit ModScanner(QObject *parent = nullptr);
  ~ModScanner();

  /**
   * @brief 异步扫描模组目录
   * @param instancePath Minecraft 实例路径
   */
  void scanMods(const QString &instancePath);

  /**
   * @brief 同步扫描模组目录（直接使用）
   * @param instancePath Minecraft 实例路径
   * @param result 输出参数，扫描结果
   * @return true 成功，false 失败
   */
  bool scanModsSync(const QString &instancePath, LocalModList &result);

  /**
   * @brief 清除缓存
   */
  void clearCache();

signals:
  void scanCompleted(const LocalModList &result);
  void scanFailed(const QString &error);
  void scanProgress(int current, int total);

private:
  /**
   * @brief 扫描单个 JAR 文件，使用批量内存提取
   * @param filePath JAR 文件路径
   * @param fileName JAR 文件名
   * @return 解析出的 ModInfo
   */
  ModInfo scanSingleModFast(const QString &filePath, const QString &fileName);

  /**
   * @brief 从批量提取结果中解析元数据
   * @param extracted 批量提取结果（key=条目路径，value=内容）
   * @param info 输出参数，模组信息
   * @return true 成功解析，false 解析失败
   */
  bool parseFromExtractedData(const QMap<QString, QByteArray> &extracted, ModInfo &info);

  // 四个元数据解析器（保持不变）
  bool parseFabricModJson(const QString &content, ModInfo &info);
  bool parseModsToml(const QString &content, ModInfo &info);
  bool parseMcmodInfo(const QString &content, ModInfo &info);
  bool parseManifest(const QString &content, ModInfo &info);

  // 缓存：key=文件路径，value=上次修改时间+解析结果
  struct CachedModInfo
  {
    QDateTime lastModified;
    ModInfo modInfo;
  };
  // scanSingleModFast 由多个并发工作线程访问，需加锁保护
  mutable QMutex m_cacheMutex;
  QMap<QString, CachedModInfo> m_cache;
};

#endif // MODSCANNER_H