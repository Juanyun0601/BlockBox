#include "ClipboardMonitor.h"

#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QUrl>

ClipboardMonitor::ClipboardMonitor(QObject *parent)
    : QObject(parent)
    , m_clipboard(QApplication::clipboard())
{
    connect(m_clipboard, &QClipboard::dataChanged,
            this, &ClipboardMonitor::onClipboardChanged);
}

ClipboardMonitor::~ClipboardMonitor()
{
}

void ClipboardMonitor::start()
{
    // 连接已在构造函数中建立，无需额外操作
}

void ClipboardMonitor::stop()
{
    disconnect(m_clipboard, &QClipboard::dataChanged,
               this, &ClipboardMonitor::onClipboardChanged);
}

void ClipboardMonitor::addRejected(const QString &url)
{
    m_rejectedLinks.insert(url);
}

void ClipboardMonitor::onClipboardChanged()
{
    const QString text = m_clipboard->text().trimmed();
    if (text.isEmpty())
        return;

    ClipboardLinkInfo info = parseUrl(text);
    if (info.source.isEmpty())
        return;

    // 已拒绝的链接不再弹窗
    if (m_rejectedLinks.contains(text))
        return;

    emit linkDetected(info);
}

ClipboardLinkInfo ClipboardMonitor::parseUrl(const QString &url) const
{
    ClipboardLinkInfo info;

    // CurseForge: curseforge.com/minecraft/{type}/{slug}
    // type: mc-mods, modpacks, texture-packs, worlds, customization
    static const QRegularExpression cfRegex(
        R"(curseforge\.com/minecraft/(mc-mods|modpacks|texture-packs|worlds|customization)/([^/\s?#]+))",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch cfMatch = cfRegex.match(url);
    if (cfMatch.hasMatch())
    {
        info.source = "curseforge";
        info.slug = cfMatch.captured(2);
        info.contentType = curseforgeTypeToContentType(cfMatch.captured(1));
        info.displayName = QStringLiteral("CurseForge");
        return info;
    }

    // Modrinth: modrinth.com/{type}/{slug}
    // type: mod, modpack, datapack, resourcepack, shader
    static const QRegularExpression mrRegex(
        R"(modrinth\.com/(mod|modpack|datapack|resourcepack|shader)/([^/\s?#]+))",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch mrMatch = mrRegex.match(url);
    if (mrMatch.hasMatch())
    {
        info.source = "modrinth";
        info.slug = mrMatch.captured(2);
        info.contentType = modrinthTypeToContentType(mrMatch.captured(1));
        info.displayName = QStringLiteral("Modrinth");
        return info;
    }

    return info;
}

ContentType ClipboardMonitor::curseforgeTypeToContentType(const QString &cfType) const
{
    if (cfType == "mc-mods")
        return ContentType::Mod;
    if (cfType == "modpacks")
        return ContentType::Modpack;
    if (cfType == "texture-packs")
        return ContentType::ResourcePack;
    if (cfType == "worlds")
        return ContentType::World;
    if (cfType == "customization")
        return ContentType::ShaderPack;
    return ContentType::Mod;
}

ContentType ClipboardMonitor::modrinthTypeToContentType(const QString &mrType) const
{
    if (mrType == "mod")
        return ContentType::Mod;
    if (mrType == "modpack")
        return ContentType::Modpack;
    if (mrType == "datapack")
        return ContentType::DataPack;
    if (mrType == "resourcepack")
        return ContentType::ResourcePack;
    if (mrType == "shader")
        return ContentType::ShaderPack;
    return ContentType::Mod;
}