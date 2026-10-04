/**
 * @file   BedrockLauncher.h
 * @brief  基岩版启动器类定义
 * @author BlockBox Team
 *
 * 基岩版（Minecraft Windows / 预览版）在 Windows 上是微软商店的 UWP 应用，
 * 不存在像 Java 版那样的独立 jar 启动入口。本类负责：
 *   1. 检测系统中是否已安装基岩版（Microsoft.MinecraftUWP 或 Microsoft.MinecraftWindowsBeta）
 *   2. 获取已安装版本的版本号 / 安装路径 / 包系列名
 *   3. 通过 shell:AppsFolder 协议直接启动已安装的基岩版
 */

#pragma once

#include <QObject>
#include <QString>
#include <QMutex>

class BedrockLauncher : public QObject
{
    Q_OBJECT

public:
    struct BedrockInfo {
        QString name;              // 包名（Microsoft.MinecraftUWP / Microsoft.MinecraftWindowsBeta）
        QString version;           // 版本号
        QString packageFamilyName; // 包系列名（用于 shell:AppsFolder 启动）
        QString appId = QStringLiteral("App"); // 应用 ID（来自 AppxManifest）
        QString installLocation;   // 安装路径
        bool valid = false;
    };

    static BedrockLauncher* instance();

    /** 检测系统中是否已安装基岩版（正式版优先，其次预览版） */
    bool isInstalled();

    /** 获取已安装基岩版信息（isInstalled 通过后可直接读取缓存） */
    BedrockInfo info() const;

    /** 检测失败 / 未安装时的错误说明 */
    QString errorMessage() const;

    /**
     * @brief 启动已安装的基岩版
     * @param preferPreviewOverride 优先启动预览版（请传 true）
     * @param hasOverride           是否使用实例级覆盖值；false 时回退到全局设置
     * @return true=已成功启动进程；false=未安装 / 启动失败（通过 errorMessage 获取原因）
     */
    bool launchGame(bool preferPreviewOverride = false, bool hasOverride = false);

private:
    explicit BedrockLauncher(QObject *parent = nullptr);
    ~BedrockLauncher() override;

    /** 通过 PowerShell Get-AppxPackage 查询并缓存基岩版信息 */
    bool queryInstalledInfo(bool preferPreviewOverride = false, bool hasOverride = false);

    static BedrockLauncher* m_instance;
    static QMutex m_instanceMutex;

    BedrockInfo m_info;
    QString m_errorMessage;
};
