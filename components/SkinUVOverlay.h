/**
 * @file   SkinUVOverlay.h
 * @brief  Minecraft 皮肤 UV 分区叠加层组件
 * @author BlockBox Team
 * @date   2026-07-22
 *
 * 提供 Minecraft 标准皮肤布局的 UV 区域数据与叠加绘制能力，
 * 在 2D 皮肤纹理画布上绘制各身体部件 UV 区域边界与标签，
 * 支持 64x64（含外层 overlay）与 64x32 格式，以及 Classic 与 Slim 模型。
 */
#pragma once

#include <QObject>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QVector>

/**
 * @brief UV 区域定义
 *
 * 描述皮肤纹理图中一个身体部件的 UV 矩形区域及其属性。
 */
struct UvRegion
{
  QRect rect;      // 纹理像素坐标
  QString name;    // 部件名（如"头部"、"右臂外层"）
  bool isOverlay;  // 是否为外层（overlay）
};

/**
 * @brief 皮肤 UV 分区叠加层
 *
 * 此类不直接渲染到屏幕，而是提供 UV 区域数据与 draw() 方法，
 * 供画布在绘制完皮肤纹理后调用以叠加绘制 UV 分区边界与标签。
 *
 * 支持：
 * - 64x64 格式（含外层 overlay 区域）
 * - 64x32 格式（仅内层，左臂/左腿复用右臂/右腿区域）
 * - Classic（4px 臂）与 Slim（3px 臂）模型
 */
class SkinUVOverlay : public QObject
{
  Q_OBJECT

public:
  explicit SkinUVOverlay(QObject* parent = nullptr);
  ~SkinUVOverlay() override;

  /**
   * @brief 设置叠加层是否可见
   * @param visible 是否可见
   */
  void setVisible(bool visible);

  /**
   * @brief 获取叠加层是否可见
   * @return 是否可见
   */
  bool isVisible() const;

  /**
   * @brief 设置皮肤纹理格式
   * @param is64x64 true 为 64x64 格式，false 为 64x32 格式
   */
  void setFormat(bool is64x64);

  /**
   * @brief 设置是否为 Slim 模型
   * @param slim 是否为 Slim（3px 臂）
   */
  void setSlim(bool slim);

  /**
   * @brief 在指定 QPainter 上绘制 UV 叠加层
   * @param painter 目标画布的 QPainter
   * @param scale   纹理像素到画布像素的缩放倍数（如 8.0 表示 1 纹理像素=8 画布像素）
   */
  void draw(QPainter& painter, float scale) const;

  /**
   * @brief 获取当前所有 UV 区域
   * @return UV 区域列表（供鼠标命中测试等用途）
   */
  QVector<UvRegion> regions() const;

private:
  /**
   * @brief 根据 m_is64x64 和 m_isSlim 重新计算区域列表
   */
  void rebuildRegions();

  bool m_visible;              // 是否显示叠加层
  bool m_is64x64;              // 当前格式（true=64x64, false=64x32）
  bool m_isSlim;               // 当前模型（true=Slim 3px臂）
  QVector<UvRegion> m_regions;  // 当前 UV 区域列表
};
