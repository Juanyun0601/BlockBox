/**
 * @file   QuickLaunchManager.h
 * @brief  桌面快捷启动（.blockbox 文件）管理器
 * @author BlockBox Team
 * @date   2026-09-06
 */
#ifndef QUICKLAUNCHMANAGER_H
#define QUICKLAUNCHMANAGER_H

#include <QString>

/**
 * @brief 桌面快捷启动管理器
 *
 * 在桌面生成 "<实例名>.blockbox" 快捷启动文件，并通过 HKCU 注册表注册
 * ".blockbox" 扩展名与方块盒子的关联（无需管理员权限）。双击该文件即可
 * 打开方块盒子并直接启动对应实例。
 *
 * 文件结构（图标与数据合一，从前往后）：
 *   [多尺寸 ICO 图标数据][载荷标记\n][JSON 载荷]
 * 资源管理器通过 ProgID 的 DefaultIcon = "%1" 直接把文件自身的 ICO 部分
 * 渲染为文件图标，因此桌面文件显示的正是实例设置中配置的那个图标；
 * ICO 目录之外的载荷字节会被图标解析器忽略，两种数据互不干扰。
 */
class QuickLaunchManager
{
public:
    /** 在桌面创建（或刷新）指定实例的 .blockbox 快捷启动文件 */
    static bool createDesktopShortcut(const QString &instancePath, QString *errorMessage = nullptr);
    /** 解析 .blockbox 快捷启动文件，取出其中的实例路径 */
    static bool parseQuickLaunchFile(const QString &filePath, QString *instancePath,
                                     QString *errorMessage = nullptr);
    /** 实例名称/图标变更后刷新已存在的快捷启动文件（文件被用户删除/移动则不再重建） */
    static void refreshDesktopShortcut(const QString &instancePath);
    /** 注册 .blockbox 扩展名与方块盒子的文件关联（HKCU，无需管理员权限） */
    static bool registerFileAssociation(QString *errorMessage = nullptr);
};

#endif // QUICKLAUNCHMANAGER_H
