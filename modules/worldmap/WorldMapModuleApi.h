/**
 * @file   WorldMapModuleApi.h
 * @brief  存档地图模块（运行时下载）与主程序之间的最小约定
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 存档地图不再随安装包分发：桌面端首次进入地图页时，主程序从 CDN
 * 下载对应版本的模块压缩包，解压后用 QLibrary 加载。
 *
 * 这里只放「宿主怎么认出模块、怎么把参数递进去」所需的约定，
 * 不包含任何地图实现，避免主程序为了一个页面把整套地图代码带进安装包。
 *
 * 模块动态库必须导出两个 C 函数：
 *   - blockboxWorldMapModuleApiVersion()  → 返回 kApiVersion
 *   - createBlockBoxWorldMapPage(parent)  → 返回地图页 QWidget
 *
 * 页面创建后，宿主通过 Qt 元对象按名字调用：
 *   - setCurrentSave(QString saveFolderPath, QString saveName)
 *   - setInstancePath(QString instancePath)
 *   - reset()
 *   - refresh()
 * （这些槽在模块里的 WorldMapPage 上声明为 Q_INVOKABLE）
 */
#ifndef BLOCKBOX_WORLDMAP_MODULE_API_H
#define BLOCKBOX_WORLDMAP_MODULE_API_H

class QWidget;

namespace BlockBoxWorldMapModule {

/** 模块接口版本：主程序与模块不一致时拒绝加载，提示用户更新启动器 */
constexpr int kApiVersion = 1;

/** 模块导出的版本号函数名 */
inline const char *apiVersionSymbol()
{
    return "blockboxWorldMapModuleApiVersion";
}

/** 模块导出的建页函数名 */
inline const char *createPageSymbol()
{
    return "createBlockBoxWorldMapPage";
}

/** 建页函数签名：parent 由宿主提供，返回的地图页归宿主所有 */
typedef QWidget *(*CreatePageFunc)(QWidget *parent);

} // namespace BlockBoxWorldMapModule

#endif // BLOCKBOX_WORLDMAP_MODULE_API_H
