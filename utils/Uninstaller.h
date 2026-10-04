/**
 * @file   Uninstaller.h
 * @brief  方块盒子自卸载工具声明
 * @author BlockBox Team
 * @date   2026-09-12
 */
#ifndef UNINSTALLER_H
#define UNINSTALLER_H

#include <QString>

class QWidget;

/**
 * @brief 自卸载流程：确认弹窗、系统残留清理与退出后的程序目录删除
 *
 * 方块盒子为便携式安装：程序、启动器数据与游戏数据都位于程序目录内，
 * 运行中的进程无法删除自身，因此退出后由临时脚本继续完成清理：
 * - 注册表设置项（HKCU\Software\BlockBox）
 * - 系统公共数据目录（%LOCALAPPDATA%\BlockBox、%APPDATA%\BlockBox、临时文件）
 * - 程序目录（可选择保留 .minecraft / BedrockData 游戏数据）
 */
class Uninstaller
{
public:
    /// 显示卸载确认对话框，用户确认后清理残留、启动后台清理脚本并退出
    static void runUninstallFlow(QWidget *parent);

private:
    Uninstaller() = delete;

    /// 删除设置中登记过的桌面 .blockbox 快捷启动文件
    static void removeDesktopQuickLaunchFiles();

    /// 还原对系统 hosts 文件的修改（未修改过则不做任何事，尽力而为）
    static void restoreHostsIfModified();

    /// 写出并启动退出后的清理脚本；成功返回 true，失败时给出 errorMsg
    static bool startCleanupProcess(bool deleteGameData, QString *errorMsg);
};

#endif // UNINSTALLER_H
