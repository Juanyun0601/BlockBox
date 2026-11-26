/**
 * @file   WorldMapSignalBridge.h
 * @brief  存档地图模块需要监听主程序信号时的转发桥
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 存档地图在桌面端是运行时下载的动态库，模块代码不能直接对主程序里的
 * QObject 信号做「函数指针版 connect」：动态库里的成员函数指针指向导入
 * 跳转桩，和主程序里 moc 记录的真实函数地址对不上，Qt 会报
 * 「signal not found」（实测 Windows/MinGW 必现）。
 *
 * 因此跨模块的信号监听统一走这里：连接动作放在主程序内编译，模块只传
 * 一个 std::function 过来。目前只有「存档地图立体视图」实验开关需要。
 */
#ifndef WORLDMAPSIGNALBRIDGE_H
#define WORLDMAPSIGNALBRIDGE_H

#include <QObject>

#include <functional>

namespace WorldMapSignalBridge {

/**
 * @brief 监听「存档地图立体视图」实验开关变化
 * @param context 上下文对象（通常是地图页），销毁时自动断开
 * @param handler 开关变化回调
 * @return 连接句柄（一般不用保存，context 销毁会自动断开）
 */
QMetaObject::Connection connectMap3dChanged(QObject *context,
                                            std::function<void(bool)> handler);

} // namespace WorldMapSignalBridge

#endif // WORLDMAPSIGNALBRIDGE_H
