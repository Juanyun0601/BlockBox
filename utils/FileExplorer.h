/**
 * @file   FileExplorer.h
 * @brief  文件资源管理器统一入口 — 按设置在系统 / 软件自带 / MT管理器 之间分流
 * @author BlockBox Team
 * @date   2026-09-26
 */
#pragma once

#include <QString>

class QWidget;

/**
 * @brief 打开目录的统一入口
 *
 * 后端：
 *  - System    ：系统默认（打开目录用系统资源管理器）
 *  - MtManager ：MT 管理器（仅安卓，未安装或启动失败时回退到系统）
 */
class FileExplorer
{
public:
    enum class Backend {
        System,     ///< 系统文件资源管理器
        MtManager   ///< MT 管理器（仅安卓）
    };

    /**
     * @brief 按当前设置打开目录
     * @param parent 父控件（当前后端未用到，保留参数兼容旧调用）
     * @param path   目录路径；若为文件则打开其所在目录；为空则忽略
     */
    static void open(QWidget *parent, const QString &path);

    /** @brief 当前后端 */
    static Backend backend();

    /** @brief 写入设置 */
    static void setBackend(Backend backend);

    /** @brief 后端的持久化键值：system / builtin / mt */
    static QString keyOf(Backend backend);

    /** @brief 由持久化键值还原后端，未知值按 System 处理 */
    static Backend fromKey(const QString &key);

    /** @brief MT 管理器包名（安卓） */
    static QString mtManagerPackage();
};
