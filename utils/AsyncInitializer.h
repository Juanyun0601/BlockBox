/**
 * @file   AsyncInitializer.h
 * @brief  应用启动异步初始化管理器（从 main.cpp 拆出，解决 qmake 并行构建
 *         时 main.moc 依赖缺失导致的干净构建失败问题）
 * @author BlockBox Team
 * @date   2026-08-11
 */
#pragma once

#include <QFutureWatcher>
#include <QObject>

/**
 * @brief 负责异步资源预加载（语言、资源等）并汇报启动进度
 *
 * 原定义于 main.cpp（含 Q_OBJECT 与 #include "main.moc"）。qmake 6.11.1
 * win32-g++ 下未将 main.moc 加入 main.o 依赖，并行构建（make -jN）时
 * main.o 先编译而 main.moc 尚不存在 → 干净构建必然失败。拆分为独立
 * 文件后 main.cpp 不再需要 moc，问题根治。
 */
class AsyncInitializer : public QObject
{
  Q_OBJECT

public:
  explicit AsyncInitializer(QObject* parent = nullptr);

  void startAsyncInitialization();

signals:
  void allInitializationFinished();

private slots:
  void onResourceLoadFinished();

private:
  QFutureWatcher<void>* m_resourceFutureWatcher = nullptr;
};
