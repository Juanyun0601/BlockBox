/**
 * @file   SystemInfo.h
 * @brief  系统信息检测 — 获取当前操作系统名称/版本/架构等
 * @author BlockBox Team
 *
 * 主要用于：
 *   1. 设置页「系统信息」标签页展示当前系统名；
 *   2. 自动选择合适的安装类型（如 Windows → mcappx 直链，其它 → mcapks 网盘）。
 */

#pragma once

#include <QString>

/// 系统信息汇总
struct SystemInfoData
{
    QString osName;        ///< 简短系统名（Windows / macOS / Linux ...）
    QString osFullName;    ///< 完整名称（如 Windows 11 家庭中文版）
    QString kernelVersion; ///< 内核版本（如 10.0.22631）
    QString build;         ///< 构建号（如 22631）
    QString architecture;  ///< CPU 架构（x86_64 / arm64 ...）
    QString edition;       ///< Windows 版本类型（家庭版/专业版/企业版...，非 Windows 为空）
};

class SystemInfo
{
public:
    /// 收集完整系统信息
    static SystemInfoData collect();

    /// 简短系统名（Windows / macOS / Linux / 其它）
    static QString osName();

    /// 完整系统名称
    static QString osFullName();

    /// CPU 架构
    static QString architecture();

    static bool isWindows();
    static bool isLinux();
    static bool isMac();
    static bool isAndroid();

private:
    /// Windows 版本类型（从注册表读取，如 家庭中文版/专业版）
    static QString windowsEdition();
    /// 仅 Windows：读取注册表字符串值
    static QString readRegistryString(const QString &path, const QString &key);
};
