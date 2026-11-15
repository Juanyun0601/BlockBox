/**
 * @file   SchematicOrthoViewRenderer.h
 * @brief  投影三视图（正视图 / 侧视图 / 俯视图）渲染器
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 把一个投影沿三个轴向拍平成正交投影视图（工程三视图）：
 *   正视图 = 编辑器「前」视角（从北面 -Z 往南看，看到 north 面）
 *   侧视图 = 编辑器「左」视角（从西面 -X 往东看，看到 west 面）
 *   俯视图 = 编辑器「顶」视角（从上往下看，看到 up 面）
 *
 * 渲染方式与封面渲染器同族：纯 2D QPainter 软渲染，不引入 3D 引擎。
 * 但与封面不同，这里用 BlockShapeLoader 解析方块真实形状与朝向
 * （楼梯 / 台阶 / 活板门等按状态正确显示），贴图取自实例图集，
 * 每列从近到远做可见性扫描（满格不透明方块截断列），再从远到近绘制。
 *
 * 无实例 / 无形状 / 无贴图时逐级回退：真实形状 -> 整格立方体；
 * 模型面贴图 -> 方块基础贴图 -> 代表色块，保证任何投影都能出图。
 */
#pragma once

#include <QImage>
#include <QString>

/** 三视图渲染结果；error 非空表示失败（中文原因，可直接展示给用户） */
struct SchematicOrthoResult
{
    QImage front;   ///< 正视图
    QImage left;    ///< 侧视图（左视图）
    QImage top;     ///< 俯视图

    bool isValid() const { return !front.isNull() && !left.isNull() && !top.isNull(); }
    QString error;
};

class SchematicOrthoViewRenderer
{
public:
    /**
     * @brief 生成三张正交视图（同步，耗时与体积成正比；建议放工作线程调用）
     * @param filePath    投影文件路径（.litematic / .nbt 直接读，
     *                    .schematic / .schem 自动转成临时 .litematic）
     * @param instancePath 实例的 versions/<版本> 目录（贴图来源；空则整图用色块）
     * @param maxPixels   单张视图最长边的像素上限（等比缩放适配）
     */
    static SchematicOrthoResult render(const QString &filePath, const QString &instancePath,
                                       int maxPixels = 1600);
};
