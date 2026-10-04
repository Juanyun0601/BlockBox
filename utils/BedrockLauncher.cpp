/**
 * @file   BedrockLauncher.cpp
 * @brief  基岩版启动器类实现
 * @author BlockBox Team
 */

#include "BedrockLauncher.h"

#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

#include "platform.h"
#include "utils/SettingsManager.h"

BedrockLauncher* BedrockLauncher::m_instance = nullptr;
QMutex BedrockLauncher::m_instanceMutex;

BedrockLauncher* BedrockLauncher::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new BedrockLauncher();
        }
    }
    return m_instance;
}

BedrockLauncher::BedrockLauncher(QObject *parent)
    : QObject(parent)
{
}

BedrockLauncher::~BedrockLauncher()
{
}

bool BedrockLauncher::isInstalled()
{
    return queryInstalledInfo();
}

BedrockLauncher::BedrockInfo BedrockLauncher::info() const
{
    return m_info;
}

QString BedrockLauncher::errorMessage() const
{
    return m_errorMessage;
}

bool BedrockLauncher::queryInstalledInfo(bool preferPreviewOverride, bool hasOverride)
{
#if defined(Q_OS_ANDROID)
    // Android: Bedrock Edition is available as a separate app
    // For now, mark as not installed - could use PackageManager to check
    m_errorMessage = tr("基岩版在 Android 上需要通过 Google Play 或其他商店安装");
    return false;
#elif !defined(PLATFORM_WINDOWS)
    m_errorMessage = tr("基岩版仅支持在 Windows 平台上运行");
    return false;
#endif

    m_info = BedrockInfo();
    m_errorMessage.clear();

    // 通过 PowerShell Get-AppxPackage 查询基岩版正式版 / 预览版
    QProcess ps;
    ps.setProcessChannelMode(QProcess::MergedChannels);
    const QString script =
        QStringLiteral(
            "$pkgs = Get-AppxPackage -Name 'Microsoft.MinecraftUWP','Microsoft.MinecraftWindowsBeta' -ErrorAction SilentlyContinue;"
            "if ($pkgs) { $pkgs | ForEach-Object {"
            "  [PSCustomObject]@{ Name = $_.Name; Version = $_.Version; PackageFamilyName = $_.PackageFamilyName; InstallLocation = $_.InstallLocation }"
            "} | ConvertTo-Json -Compress"
            "}");
    ps.start(QStringLiteral("powershell.exe"),
             QStringList() << QStringLiteral("-NoProfile") << QStringLiteral("-ExecutionPolicy")
                           << QStringLiteral("Bypass") << QStringLiteral("-Command") << script);

    if (!ps.waitForFinished(15000)) {
        m_errorMessage = tr("检测基岩版安装状态超时");
        return false;
    }

    const QString output = QString::fromUtf8(ps.readAllStandardOutput()).trimmed();
    if (output.isEmpty()) {
        m_errorMessage = tr("未检测到已安装的基岩版，请先在微软商店中安装 Minecraft");
        return false;
    }

    QJsonArray items;
    const QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8());
    if (doc.isArray()) {
        items = doc.array();
    } else if (doc.isObject()) {
        items.append(doc.object());
    }

    if (items.isEmpty()) {
        m_errorMessage = tr("未检测到已安装的基岩版，请先在微软商店中安装 Minecraft");
        return false;
    }

    // 默认正式版（Microsoft.MinecraftUWP）优先；启用「优先预览版」设置时反转为预览版优先。
    // 支持实例级覆盖：hasOverride 为 true 时使用调用方提供的值，否则沿用全局设置。
    const bool preferPreview = hasOverride
        ? preferPreviewOverride
        : SettingsManager::instance()
              ->getProperty(QStringLiteral("bedrock/preferPreview"), false)
              .toBool();
    QJsonObject chosen;
    for (const QJsonValue &v : items) {
        const QJsonObject obj = v.toObject();
        const QString name = obj.value("Name").toString();
        if (!preferPreview && name == QStringLiteral("Microsoft.MinecraftUWP")) {
            chosen = obj;
            break;
        }
        if (preferPreview && name == QStringLiteral("Microsoft.MinecraftWindowsBeta")) {
            chosen = obj;
            break;
        }
    }
    if (chosen.isEmpty())
        chosen = items.first().toObject();

    m_info.name = chosen.value("Name").toString();
    m_info.version = chosen.value("Version").toString();
    m_info.packageFamilyName = chosen.value("PackageFamilyName").toString();
    m_info.installLocation = chosen.value("InstallLocation").toString();

    if (m_info.name.isEmpty() || m_info.packageFamilyName.isEmpty()) {
        m_errorMessage = tr("基岩版安装信息解析失败");
        return false;
    }

    // 从 AppxManifest.xml 读取应用 ID（绝大多数情况下为 App）
    if (!m_info.installLocation.isEmpty()) {
        const QString manifestPath = m_info.installLocation + QStringLiteral("/AppxManifest.xml");
        QFile manifest(manifestPath);
        if (manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(manifest.readAll());
            manifest.close();
            const QRegularExpression appIdRe(QStringLiteral("<Application[^>]*\\bId=\"([^\"]+)\""));
            const QRegularExpressionMatch match = appIdRe.match(content);
            if (match.hasMatch())
                m_info.appId = match.captured(1);
        }
    }

    m_info.valid = true;
    return true;
}

bool BedrockLauncher::launchGame(bool preferPreviewOverride, bool hasOverride)
{
    if (!queryInstalledInfo(preferPreviewOverride, hasOverride))
        return false;

    // 通过 explorer shell:AppsFolder 协议启动 UWP 应用（标准做法）
    // 形如: shell:AppsFolder\Microsoft.MinecraftUWP_8wekyb3d8bbwe!App
    const QString aumid = m_info.packageFamilyName + QLatin1Char('!') + m_info.appId;
    const QString target = QStringLiteral("shell:AppsFolder\\") + aumid;

    if (!QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList() << target)) {
        m_errorMessage = tr("启动基岩版失败，无法启动 explorer.exe");
        return false;
    }

    m_errorMessage.clear();
    return true;
}
