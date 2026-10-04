/**
 * @file   SkinEditorDocument.cpp
 * @brief  皮肤制作器文档模型类实现，管理皮肤 QImage、模型类型、纹理格式、撤销/重做栈与脏标记
 * @author BlockBox Team
 * @date   2026-07-22
 */

#include "SkinEditorDocument.h"

#include <cstring>

#include <algorithm>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

namespace {

/**
 * @brief 比较两个 QImage 像素是否完全一致
 * @param a 图像 A
 * @param b 图像 B
 * @return 尺寸、格式与字节数据均相同返回 true
 */
bool imagesEqual(const QImage& a, const QImage& b)
{
  if (a.size() != b.size() || a.format() != b.format())
  {
    return false;
  }
  const int bytes = a.sizeInBytes();
  if (bytes != b.sizeInBytes())
  {
    return false;
  }
  return std::memcmp(a.constBits(), b.constBits(), static_cast<size_t>(bytes)) == 0;
}

/**
 * @brief 官方皮肤模板名称列表（用于 loadFromTemplate 校验与资源路径）
 */
const QStringList& officialTemplates()
{
  static const QStringList templates = {
    "Alex", "Ari", "Efe", "Kai", "Makena",
    "Noor", "Steve", "Sunny", "Zuri"
  };
  return templates;
}

} // namespace

SkinEditorDocument::SkinEditorDocument(QObject* parent)
  : QObject(parent)
{
}

SkinEditorDocument::~SkinEditorDocument() = default;

void SkinEditorDocument::loadFromImage(const QImage& image)
{
  if (image.isNull())
  {
    return;
  }

  bool prevIs64x64 = m_is64x64;

  QImage normalized;
  if (image.width() == 64 && image.height() == 64)
  {
    normalized = image.convertToFormat(QImage::Format_ARGB32);
    m_is64x64 = true;
  }
  else if (image.width() == 64 && image.height() == 32)
  {
    normalized = image.convertToFormat(QImage::Format_ARGB32);
    m_is64x64 = false;
  }
  else
  {
    // 非标准尺寸：缩放至 64x64
    QImage scaled = image.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    normalized = scaled.convertToFormat(QImage::Format_ARGB32);
    m_is64x64 = true;
  }

  m_skinImage = normalized;
  m_lastSavedImage = m_skinImage;
  m_isDirty = false;
  m_undoStack.clear();
  m_redoStack.clear();

  emit skinChanged(m_skinImage);
  if (m_is64x64 != prevIs64x64)
  {
    emit formatChanged(m_is64x64);
  }
  emit dirtyChanged(false);
  emit canUndoChanged(false);
  emit canRedoChanged(false);
}

bool SkinEditorDocument::loadFromFile(const QString& path)
{
  if (path.isEmpty())
  {
    return false;
  }

  QImageReader reader(path);
  reader.setAutoTransform(true);
  QImage image = reader.read();
  if (image.isNull())
  {
    return false;
  }

  m_filePath = path;
  loadFromImage(image);
  return true;
}

bool SkinEditorDocument::loadFromTemplate(const QString& templateName)
{
  QString matched;
  for (const QString& name : officialTemplates())
  {
    if (templateName.compare(name, Qt::CaseInsensitive) == 0)
    {
      matched = name;
      break;
    }
  }
  if (matched.isEmpty())
  {
    return false;
  }

  QImage skin(":/Images/Skins/" + matched + ".png");
  if (skin.isNull())
  {
    return false;
  }

  // 模板不属于文件系统路径
  m_filePath.clear();
  loadFromImage(skin);
  return true;
}

void SkinEditorDocument::loadBlank(bool is64x64)
{
  bool prevIs64x64 = m_is64x64;

  int height = is64x64 ? 64 : 32;
  QImage blank(64, height, QImage::Format_ARGB32);
  blank.fill(Qt::transparent);

  m_skinImage = blank;
  m_lastSavedImage = m_skinImage;
  m_is64x64 = is64x64;
  m_isDirty = false;
  m_undoStack.clear();
  m_redoStack.clear();

  emit skinChanged(m_skinImage);
  if (m_is64x64 != prevIs64x64)
  {
    emit formatChanged(m_is64x64);
  }
  emit dirtyChanged(false);
  emit canUndoChanged(false);
  emit canRedoChanged(false);
}

bool SkinEditorDocument::saveToFile(const QString& path)
{
  if (path.isEmpty() || m_skinImage.isNull())
  {
    return false;
  }

  // 确保目标目录存在
  QFileInfo fileInfo(path);
  QDir parentDir = fileInfo.absoluteDir();
  if (!parentDir.exists())
  {
    parentDir.mkpath(".");
  }

  if (!m_skinImage.save(path, "PNG"))
  {
    return false;
  }

  m_filePath = path;
  m_lastSavedImage = m_skinImage;
  bool wasDirty = m_isDirty;
  m_isDirty = false;

  if (wasDirty)
  {
    emit dirtyChanged(false);
  }
  return true;
}

bool SkinEditorDocument::save()
{
  if (m_filePath.isEmpty())
  {
    return false;
  }
  return saveToFile(m_filePath);
}

void SkinEditorDocument::setPixel(int x, int y, const QColor& color)
{
  if (x < 0 || y < 0 || x >= m_skinImage.width() || y >= m_skinImage.height())
  {
    return;
  }

  beginEdit();
  m_skinImage.setPixel(x, y, color.rgba());
  endEdit();
}

void SkinEditorDocument::setPixels(const QVector<QPoint>& points, const QColor& color)
{
  if (points.isEmpty())
  {
    return;
  }

  const int w = m_skinImage.width();
  const int h = m_skinImage.height();
  const QRgb rgb = color.rgba();

  // 校验至少有一个有效点
  bool hasValid = false;
  for (const QPoint& p : points)
  {
    if (p.x() >= 0 && p.y() >= 0 && p.x() < w && p.y() < h)
    {
      hasValid = true;
      break;
    }
  }
  if (!hasValid)
  {
    return;
  }

  beginEdit();
  for (const QPoint& p : points)
  {
    if (p.x() >= 0 && p.y() >= 0 && p.x() < w && p.y() < h)
    {
      m_skinImage.setPixel(p.x(), p.y(), rgb);
    }
  }
  endEdit();
}

void SkinEditorDocument::fillRegion(int x, int y, const QColor& color)
{
  const int w = m_skinImage.width();
  const int h = m_skinImage.height();
  if (x < 0 || y < 0 || x >= w || y >= h)
  {
    return;
  }

  const QRgb fillColor = color.rgba();
  const QRgb targetColor = m_skinImage.pixel(x, y);
  if (fillColor == targetColor)
  {
    // 起点已是目标色，无需填充（避免空操作进入撤销栈）
    return;
  }

  beginEdit();

  // 基于显式栈的 4 连通洪水填充
  std::vector<QPoint> stack;
  stack.reserve(64);
  stack.push_back(QPoint(x, y));

  while (!stack.empty())
  {
    QPoint p = stack.back();
    stack.pop_back();

    const int px = p.x();
    const int py = p.y();
    if (px < 0 || py < 0 || px >= w || py >= h)
    {
      continue;
    }
    if (m_skinImage.pixel(px, py) != targetColor)
    {
      continue;
    }
    m_skinImage.setPixel(px, py, fillColor);

    stack.push_back(QPoint(px + 1, py));
    stack.push_back(QPoint(px - 1, py));
    stack.push_back(QPoint(px, py + 1));
    stack.push_back(QPoint(px, py - 1));
  }

  endEdit();
}

void SkinEditorDocument::clearAll()
{
  beginEdit();
  m_skinImage.fill(Qt::transparent);
  endEdit();
}

void SkinEditorDocument::setSlim(bool slim)
{
  if (m_isSlim == slim)
  {
    return;
  }
  m_isSlim = slim;
  emit modelChanged(slim);
}

void SkinEditorDocument::setFormat64x64(bool is64x64)
{
  if (m_is64x64 == is64x64)
  {
    return;
  }

  QImage newImage;
  if (is64x64)
  {
    // 64x32 → 64x64：扩展画布，新区域透明
    newImage = QImage(64, 64, QImage::Format_ARGB32);
    newImage.fill(Qt::transparent);
    QPainter painter(&newImage);
    painter.drawImage(0, 0, m_skinImage, 0, 0, 64, 32);
  }
  else
  {
    // 64x64 → 64x32：裁剪，丢弃 y>=32 的内容
    newImage = QImage(64, 32, QImage::Format_ARGB32);
    newImage.fill(Qt::transparent);
    QPainter painter(&newImage);
    painter.drawImage(0, 0, m_skinImage, 0, 0, 64, 32);
  }

  m_skinImage = newImage;
  m_is64x64 = is64x64;

  emit skinChanged(m_skinImage);
  emit formatChanged(is64x64);
  updateDirtyFlag();
}

bool SkinEditorDocument::canUndo() const
{
  return !m_undoStack.empty();
}

bool SkinEditorDocument::canRedo() const
{
  return !m_redoStack.empty();
}

void SkinEditorDocument::undo()
{
  if (m_undoStack.empty())
  {
    return;
  }

  // 把当前图像压入重做栈，恢复撤销栈顶
  m_redoStack.push_back(m_skinImage);
  m_skinImage = m_undoStack.back();
  m_undoStack.pop_back();

  emitAfterHistoryChange();
}

void SkinEditorDocument::redo()
{
  if (m_redoStack.empty())
  {
    return;
  }

  // 把当前图像压入撤销栈，恢复重做栈顶
  m_undoStack.push_back(m_skinImage);
  m_skinImage = m_redoStack.back();
  m_redoStack.pop_back();

  emitAfterHistoryChange();
}

const QImage& SkinEditorDocument::skinImage() const
{
  return m_skinImage;
}

bool SkinEditorDocument::isSlim() const
{
  return m_isSlim;
}

bool SkinEditorDocument::is64x64() const
{
  return m_is64x64;
}

bool SkinEditorDocument::isDirty() const
{
  return m_isDirty;
}

QString SkinEditorDocument::filePath() const
{
  return m_filePath;
}

void SkinEditorDocument::setFilePath(const QString& path)
{
  m_filePath = path;
}

QImage SkinEditorDocument::exportImage() const
{
  return m_skinImage.copy();
}

void SkinEditorDocument::beginEdit()
{
  // 新操作：清空重做栈，操作前快照压入撤销栈
  m_redoStack.clear();
  m_undoStack.push_back(m_skinImage);
  if (static_cast<int>(m_undoStack.size()) > MAX_UNDO_STEPS)
  {
    m_undoStack.erase(m_undoStack.begin());
  }
}

void SkinEditorDocument::endEdit()
{
  emit skinChanged(m_skinImage);
  updateDirtyFlag();
  emit canUndoChanged(canUndo());
  emit canRedoChanged(canRedo());
}

void SkinEditorDocument::emitAfterHistoryChange()
{
  emit skinChanged(m_skinImage);
  updateDirtyFlag();
  emit canUndoChanged(canUndo());
  emit canRedoChanged(canRedo());
}

void SkinEditorDocument::updateDirtyFlag()
{
  bool newDirty = !imagesEqual(m_skinImage, m_lastSavedImage);
  if (newDirty != m_isDirty)
  {
    m_isDirty = newDirty;
    emit dirtyChanged(newDirty);
  }
}
