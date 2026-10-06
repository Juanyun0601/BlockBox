// 首页皮肤预览 50% 透明背景合成验证探针：
// 品红光栅背景上放置带 8 位 alpha 的 Skin3DWidget（清屏色白/alpha 128），
// 用 PrintWindow 抓真实合成结果。若 alpha 生效，GL 区域应呈 (255,128,255) 粉色，
// 且能看到模型；若退化则是不透明白色。
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "Skin3DWidget.h"

class MagentaBackground : public QWidget
{
protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.fillRect(rect(), QColor(255, 0, 255));
  }
};

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  auto *bg = new MagentaBackground();
  bg->resize(320, 720);

  // 模拟首页面板：GL 内缩 1px + 圆角遮罩
  auto *panel = new QWidget(bg);
  panel->setGeometry(50, 50, 220, 620);
  auto *lay = new QVBoxLayout(panel);
  lay->setContentsMargins(1, 1, 1, 1);

  auto *gl = new Skin3DWidget();
  QSurfaceFormat fmt = gl->format();
  fmt.setAlphaBufferSize(8);
  gl->setFormat(fmt);
  // alpha 混入窗口背景必须走 AlwaysStackOnTop 合成路径（普通路径视 GL 为不透明）
  gl->setAttribute(Qt::WA_AlwaysStackOnTop);
  gl->setBackgroundColor(QColor(255, 255, 255, 128));   // 卡片白 50% 透明
  gl->setAutoRotate(false);
  gl->resetView(6.5f);
  // 生成一张简易 64x64 测试皮肤（头部橙色、身体青色），避免依赖资源文件
  QImage skin(64, 64, QImage::Format_ARGB32);
  skin.fill(Qt::transparent);
  QPainter sp(&skin);
  sp.fillRect(8, 8, 8, 8, QColor(230, 145, 56));     // 头正面
  sp.fillRect(40, 8, 8, 8, QColor(230, 145, 56));    // 帽正面
  sp.fillRect(16, 16, 24, 16, QColor(0, 188, 212));  // 身体 UV 区
  sp.fillRect(0, 16, 16, 16, QColor(69, 90, 220));   // 右臂
  sp.fillRect(16, 48, 16, 16, QColor(69, 90, 220));  // 左腿
  sp.fillRect(32, 48, 16, 16, QColor(69, 90, 220));  // 右腿
  sp.end();
  gl->setSkin(skin);
  lay->addWidget(gl);
  bg->show();

  // 背景圆角由 GL 在帧缓冲内绘制（清屏全透明 + SDF 圆角矩形，抗锯齿）
  gl->setBackgroundCornerRadius(16.0);

  QTimer::singleShot(2500, &app, &QCoreApplication::quit);
  return app.exec();
}
