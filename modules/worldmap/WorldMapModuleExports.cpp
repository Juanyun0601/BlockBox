/**
 * @file   WorldMapModuleExports.cpp
 * @brief  存档地图模块动态库的导出入口
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 这里只做两件事：报告接口版本、按宿主给的父窗口创建地图页。
 * 页面本身仍是原来的 WorldMapPage，模块只是把它从安装包里搬到 CDN。
 */

#include "WorldMapModuleApi.h"

#include "pages/WorldMapPage.h"

#include <QWidget>

#if defined(Q_OS_WIN)
#  define BB_WORLDMAP_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#  define BB_WORLDMAP_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

BB_WORLDMAP_MODULE_EXPORT int blockboxWorldMapModuleApiVersion()
{
    return BlockBoxWorldMapModule::kApiVersion;
}

BB_WORLDMAP_MODULE_EXPORT QWidget *createBlockBoxWorldMapPage(QWidget *parent)
{
    return new WorldMapPage(parent);
}
