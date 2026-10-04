/**
 * @file   SkinEditorDocument.h
 * @brief  皮肤制作器文档模型类声明，管理皮肤 QImage、模型类型、纹理格式、撤销/重做栈与脏标记
 * @author BlockBox Team
 * @date   2026-07-22
 */

#pragma once

#include <vector>

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QVector>

/**
 * @brief 皮肤编辑文档模型
 *
 * 持有当前皮肤图像（QImage::Format_ARGB32）、Classic/Slim 模型标志、
 * 64x64/64x32 纹理格式标志、文件路径、脏标记以及撤销/重做快照栈。
 * 所有绘制操作（setPixel/setPixels/fillRegion/clearAll）在执行前会保存快照，
 * 最多保留 MAX_UNDO_STEPS 步历史。
 */
class SkinEditorDocument : public QObject
{
  Q_OBJECT

public:
  explicit SkinEditorDocument(QObject* parent = nullptr);
  ~SkinEditorDocument() override;

  // 加载接口

  /**
   * @brief 从 QImage 加载皮肤（自动检测 64x64/64x32 格式，非标准尺寸缩放至 64x64）
   * @param image 皮肤图像
   *
   * 加载后重置撤销/重做栈，重置脏标记。
   */
  void loadFromImage(const QImage& image);

  /**
   * @brief 从 PNG 文件加载皮肤
   * @param path PNG 文件路径
   * @return 加载成功返回 true，失败返回 false
   */
  bool loadFromFile(const QString& path);

  /**
   * @brief 从官方皮肤模板加载（Steve/Alex/Ari/Efe/Kai/Makena/Noor/Sunny/Zuri）
   * @param templateName 模板名称（大小写不敏感）
   * @return 加载成功返回 true；模板名非法或资源缺失返回 false
   *
   * 加载后清空 m_filePath（模板不属于文件系统路径）。
   */
  bool loadFromTemplate(const QString& templateName);

  /**
   * @brief 加载空白透明皮肤
   * @param is64x64 为 true 创建 64x64 空白，否则 64x32
   */
  void loadBlank(bool is64x64 = true);

  // 保存接口

  /**
   * @brief 保存到 PNG 文件
   * @param path 目标文件路径
   * @return 保存成功返回 true
   *
   * 成功后更新 m_filePath、重置脏标记、记录最后保存快照。
   */
  bool saveToFile(const QString& path);

  /**
   * @brief 保存到原路径（m_filePath）
   * @return 无原路径或保存失败返回 false
   */
  bool save();

  // 编辑接口（每次调用压入撤销栈）

  /**
   * @brief 设置单像素颜色
   * @param x 像素横坐标
   * @param y 像素纵坐标
   * @param color 目标颜色
   *
   * 坐标越界时为空操作。
   */
  void setPixel(int x, int y, const QColor& color);

  /**
   * @brief 批量设置像素（一笔操作，共享一个撤销快照）
   * @param points 像素坐标列表
   * @param color 目标颜色
   *
   * 列表为空或全部越界时为空操作。
   */
  void setPixels(const QVector<QPoint>& points, const QColor& color);

  /**
   * @brief 洪水填充：填充与起点同色的连通区域
   * @param x 起点横坐标
   * @param y 起点纵坐标
   * @param color 填充颜色
   */
  void fillRegion(int x, int y, const QColor& color);

  /**
   * @brief 清空整个皮肤为透明
   */
  void clearAll();

  // 模型与格式

  /**
   * @brief 切换 Classic/Slim 模型（仅改变标志，不修改图像）
   * @param slim 为 true 切换到 Slim 模型
   */
  void setSlim(bool slim);

  /**
   * @brief 切换 64x64/64x32 纹理格式（裁剪或扩展画布）
   * @param is64x64 目标格式标志
   *
   * 64x64→64x32 裁剪到 64x32（丢弃 y>=32 的内容）；
   * 64x32→64x64 扩展画布到 64x64，新区域透明。
   */
  void setFormat64x64(bool is64x64);

  // 撤销/重做

  bool canUndo() const;
  bool canRedo() const;
  void undo();
  void redo();

  // 状态查询

  const QImage& skinImage() const;
  bool isSlim() const;
  bool is64x64() const;
  bool isDirty() const;
  QString filePath() const;
  void setFilePath(const QString& path);

  /**
   * @brief 导出当前皮肤副本（用于应用到账户）
   * @return 当前皮肤 QImage 的独立副本
   */
  QImage exportImage() const;

signals:
  void skinChanged(const QImage& image);  ///< 皮肤内容变更（绘制、加载、撤销/重做、格式切换）
  void modelChanged(bool slim);           ///< 模型切换
  void formatChanged(bool is64x64);       ///< 格式切换
  void dirtyChanged(bool dirty);          ///< 脏标记变更
  void canUndoChanged(bool can);          ///< 撤销栈可用性变更
  void canRedoChanged(bool can);          ///< 重做栈可用性变更

private:
  /**
   * @brief 编辑操作前调用：清空重做栈，将当前图像快照压入撤销栈
   *
   * 超过 MAX_UNDO_STEPS 时移除撤销栈最早元素。
   */
  void beginEdit();

  /**
   * @brief 编辑操作后调用：发出 skinChanged 信号并更新脏标记与撤销/重做可用性
   */
  void endEdit();

  /**
   * @brief 撤销/重做后调用：发出 skinChanged 信号并同步脏标记与可用性信号
   */
  void emitAfterHistoryChange();

  /**
   * @brief 比较当前图像与最后保存快照，更新脏标记并按需发出 dirtyChanged
   */
  void updateDirtyFlag();

  QImage m_skinImage;          ///< 当前皮肤图像（QImage::Format_ARGB32）
  QImage m_lastSavedImage;     ///< 最后一次保存时的图像快照，用于计算脏标记
  bool m_isSlim = false;       ///< Slim 模型标志
  bool m_is64x64 = true;       ///< 64x64 格式标志（false 表示 64x32）
  bool m_isDirty = false;      ///< 脏标记
  QString m_filePath;          ///< 当前文件路径

  std::vector<QImage> m_undoStack;  ///< 撤销栈（操作前快照）
  std::vector<QImage> m_redoStack;  ///< 重做栈（撤销前的当前图像）

  static constexpr int MAX_UNDO_STEPS = 50;  ///< 最大撤销步数
};
