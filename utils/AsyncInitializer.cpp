/**
 * @file   AsyncInitializer.cpp
 * @brief  应用启动异步初始化管理器实现
 * @author BlockBox Team
 * @date   2026-08-11
 */

#include "AsyncInitializer.h"

#include <QDebug>

#include "PerformanceMonitor.h"
#include "ResourceManager.h"
#include "StartupProgressWidget.h"

AsyncInitializer::AsyncInitializer(QObject* parent)
  : QObject(parent)
  , m_resourceFutureWatcher(new QFutureWatcher<void>(this))
{
  // 连接资源异步加载完成信号
  connect(m_resourceFutureWatcher, &QFutureWatcher<void>::finished,
          this, &AsyncInitializer::onResourceLoadFinished);
}

void AsyncInitializer::startAsyncInitialization()
{
  qDebug() << "[App]" << "Starting async initialization...";

  // 更新进度：开始异步初始化
  StartupProgressWidget::instance()->updateProgress(40, tr("正在异步加载资源..."));

  // 启动异步资源加载（语言已在启动时同步加载完成）
  PerformanceMonitor::instance()->startMeasurement("Async Resource Loading");
  QFuture<void> resourceFuture = ResourceManager::instance()->preloadResourcesAsync();
  m_resourceFutureWatcher->setFuture(resourceFuture);
}

void AsyncInitializer::onResourceLoadFinished()
{
  PerformanceMonitor::instance()->endMeasurement("Async Resource Loading");
  qDebug() << "[App]" << "Resource loading completed";
  qDebug() << "[App]" << "All async initialization tasks completed";
  StartupProgressWidget::instance()->updateProgress(65, tr("异步初始化完成"));
  emit allInitializationFinished();
}
