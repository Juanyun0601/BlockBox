// 方块盒子预编译头（PCH）
//
// 为什么要有这个文件：
//   整个工程几百个翻译单元几乎都要拉进同一批 Qt 头文件（QWidget、QString、
//   QJsonObject……），每次改一个文件都要把这些头重新解析一遍，占了单文件
//   编译时间的三成以上。声明成预编译头后，这批头只解析一次，之后每个文件
//   直接复用解析结果，单文件编译明显变快。
//
// 实测（Windows / mingw g++ 13.1 / -O2）：代表文件从约 16s 降到约 13s；
// 头文件用得越重的文件收益越大（mainwindow.cpp 从 19s 降到 9s 左右）。
//
// 注意：
// 1) 这里故意按“整个 Qt 模块”整体包含，而不是逐个挑头文件。实测逐个挑
//    反而更慢——没被挑中的头还是要在每个文件里重新解析一遍。所以除非
//    确定某个模块完全用不到，否则不要缩减下面的列表。
// 2) 内容必须保持稳定：本文件一改，所有目标文件都会重编。
// 3) 用 __cplusplus 包住：qmake 会同时生成 C 和 C++ 两份预编译头，而工程里
//    的 third_party/cubiomes 是纯 C 源码，不能看到 C++ 头。
// 4) 只在桌面端（Windows/macOS/Linux）启用，Android/OHOS 保持原样，
//    开关见 BlockBox.pro 里的 precompile_header 配置。

#ifdef __cplusplus

#include <QtCore/QtCore>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>
#include <QtNetwork/QtNetwork>
#include <QtConcurrent/QtConcurrent>
#include <QtSvg/QSvgRenderer>
#include <QtOpenGLWidgets/QOpenGLWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#endif // __cplusplus
