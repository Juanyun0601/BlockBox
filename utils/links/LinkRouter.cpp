#include "LinkRouter.h"

#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/SettingsManager.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModrinthAPI.h"

#include <QApplication>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QTextBrowser>
#include <QTimer>
#include <QUrlQuery>
#include <QWidget>

namespace {

constexpr int kSlowHintDelayMs = 700;

/** CurseForge 项目页的分类段 → 软件里的内容类型键 */
QString curseforgeTypeKey(const QString &type)
{
    const QString key = type.toLower();
    if (key == QLatin1String("mc-mods"))       return QStringLiteral("mod");
    if (key == QLatin1String("modpacks"))      return QStringLiteral("modpack");
    if (key == QLatin1String("texture-packs")) return QStringLiteral("resourcepack");
    if (key == QLatin1String("worlds"))        return QStringLiteral("world");
    if (key == QLatin1String("customization")) return QStringLiteral("shader");
    return QString();
}

/** 内容类型键 → ContentType（只认详情页能打开的那几种） */
bool contentTypeForKey(const QString &key, ContentType *type)
{
    if (!type)
        return false;
    if (key == QLatin1String("mod"))          { *type = ContentType::Mod;          return true; }
    if (key == QLatin1String("modpack"))      { *type = ContentType::Modpack;      return true; }
    if (key == QLatin1String("resourcepack")) { *type = ContentType::ResourcePack; return true; }
    if (key == QLatin1String("shader"))       { *type = ContentType::ShaderPack;   return true; }
    if (key == QLatin1String("datapack"))     { *type = ContentType::DataPack;     return true; }
    if (key == QLatin1String("world"))        { *type = ContentType::World;        return true; }
    return false;
}

/** 这条链接是不是本站论坛的（站点地址还没配好时只认站内相对路径） */
bool isForumSite(const QUrl &url)
{
    auto *service = ForumAccountService::instance();
    const QUrl base(service ? service->baseUrl() : QString());
    if (base.host().isEmpty())
        return url.host().isEmpty();
    if (url.host().isEmpty())
        return true;
    return url.host().compare(base.host(), Qt::CaseInsensitive) == 0;
}

QString pathFileName(const QUrl &url)
{
    return url.path().section(QLatin1Char('/'), -1);
}

/** 左下角提示挂在哪个窗口上：优先点击来源所在的窗口，再退到当前活动窗口 */
QWidget *toastHost(QWidget *context)
{
    if (context) {
        if (QWidget *window = context->window())
            return window;
    }
    return QApplication::activeWindow();
}

} // namespace

LinkRouter::LinkRouter(QObject *parent)
    : QObject(parent)
{
}

LinkRouter *LinkRouter::instance()
{
    static LinkRouter *router = nullptr;
    if (!router)
        router = new LinkRouter(qApp);
    return router;
}

LinkRouter::Target LinkRouter::classify(const QUrl &url)
{
    Target target;
    target.url = url;

    const QString scheme = url.scheme().toLower();

    // 一键加入：软件自己接住，别再绕一圈交给系统去重新拉起一个进程
    if (scheme == QLatin1String("blockbox"))
    {
        target.kind = Kind::BlockBoxJoin;
        return target;
    }

    // 站内论坛链接：帖子 / 板块
    if (isForumSite(url))
    {
        const QString file = pathFileName(url);
        if (file == QLatin1String("thread.html"))
        {
            bool ok = false;
            const int id = QUrlQuery(url).queryItemValue(QStringLiteral("id")).toInt(&ok);
            if (ok && id > 0)
            {
                target.kind = Kind::ForumThread;
                target.threadId = id;
                return target;
            }
        }
        else if (file == QLatin1String("board.html"))
        {
            const QString slug = QUrlQuery(url).queryItemValue(QStringLiteral("slug"));
            if (!slug.isEmpty())
            {
                target.kind = Kind::ForumBoard;
                target.boardSlug = slug;
                // 板块名先拿 slug 占位，真正打开时列表页会按 slug 取到名字
                target.boardName = slug;
                return target;
            }
        }
    }

    const QString host = url.host().toLower();
    const QString path = url.path();

    // CurseForge 项目页：curseforge.com/minecraft/{类型}/{slug}
    if (host.endsWith(QLatin1String("curseforge.com")))
    {
        static const QRegularExpression cfRe(
            QStringLiteral("^/minecraft/(mc-mods|modpacks|texture-packs|worlds|customization)/([^/]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = cfRe.match(path);
        if (match.hasMatch())
        {
            const QString key = curseforgeTypeKey(match.captured(1));
            if (!key.isEmpty())
            {
                target.kind = Kind::CurseForge;
                target.contentTypeKey = key;
                target.slug = match.captured(2);
                return target;
            }
        }
    }

    // Modrinth 项目页：modrinth.com/{类型}/{slug}，也认 modrinth.com/project/{slug}
    if (host.endsWith(QLatin1String("modrinth.com")))
    {
        static const QRegularExpression mrRe(
            QStringLiteral("^/(mod|modpack|resourcepack|shader|datapack|project)/([^/]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = mrRe.match(path);
        if (match.hasMatch())
        {
            const QString type = match.captured(1).toLower();
            target.kind = Kind::Modrinth;
            // project 形式看不出类型，等反查到项目信息后再按 project_type 打开
            target.contentTypeKey = (type == QLatin1String("project")) ? QString() : type;
            target.slug = match.captured(2);
            return target;
        }
    }

    return target;
}

void LinkRouter::wireTextBrowser(QTextBrowser *browser)
{
    if (!browser)
        return;

    browser->setOpenLinks(false);
    browser->setOpenExternalLinks(false);
    QObject::connect(browser, &QTextBrowser::anchorClicked, browser, [browser](const QUrl &url) {
        LinkRouter::instance()->open(url, browser);
    });
}

void LinkRouter::setHandlers(const Handlers &handlers)
{
    m_handlers = handlers;
}

void LinkRouter::open(const QUrl &url, QWidget *context)
{
    if (!url.isValid())
        return;

    // 正文里可能写的是站内相对路径（thread.html?id=…），先按论坛地址补全
    QUrl resolved = url;
    if (resolved.scheme().isEmpty())
    {
        auto *service = ForumAccountService::instance();
        const QString base = service ? service->baseUrl() : QString();
        if (!base.isEmpty())
            resolved = QUrl(base).resolved(url);
    }
    if (!resolved.isValid())
        return;

    const QString scheme = resolved.scheme().toLower();
    // 描述正文里偶尔会有 #锚点、相对图片这类点到也没法打开的链接，直接忽略
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")
        && scheme != QLatin1String("mailto") && scheme != QLatin1String("file")
        && scheme != QLatin1String("blockbox"))
    {
        return;
    }

    const Target target = classify(resolved);
    switch (target.kind)
    {
    case Kind::ForumThread:
        if (m_handlers.openForumThread)
        {
            m_handlers.openForumThread(target.threadId);
            return;
        }
        break;
    case Kind::ForumBoard:
        if (m_handlers.openForumBoard)
        {
            m_handlers.openForumBoard(target.boardSlug, target.boardName);
            return;
        }
        break;
    case Kind::BlockBoxJoin:
        if (m_handlers.handleJoinLink)
        {
            m_handlers.handleJoinLink(resolved.toString());
            return;
        }
        break;
    case Kind::CurseForge:
        if (m_handlers.openContentDetail)
        {
            resolveCurseForge(target, context);
            return;
        }
        break;
    case Kind::Modrinth:
        if (m_handlers.openContentDetail)
        {
            resolveModrinth(target, context);
            return;
        }
        break;
    case Kind::External:
        break;
    }

    openInBrowser(resolved);
}

void LinkRouter::openInBrowser(const QUrl &url)
{
    if (url.isValid())
        QDesktopServices::openUrl(url);
}

void LinkRouter::notify(QWidget *context, const QString &text)
{
    if (QWidget *host = toastHost(context))
        NotificationManager::showInfo(host, text);
}

void LinkRouter::openContent(const ModInfo &info, const QString &contentTypeKey,
                             const QUrl &fallback, QWidget *context)
{
    ContentType type = ContentType::Mod;
    if (!contentTypeForKey(contentTypeKey, &type) || !m_handlers.openContentDetail)
    {
        openInBrowser(fallback);
        return;
    }
    m_handlers.openContentDetail(info, type);
    Q_UNUSED(context);
}

void LinkRouter::resolveCurseForge(const Target &target, QWidget *context)
{
    auto *api = new CurseForgeAPI(this);
    const QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        api->setApiKey(apiKey);

    const QVariant useMcim = SettingsManager::instance()->property("use_mcim");
    api->setBaseUrl((useMcim.isValid() ? useMcim.toBool() : true)
                        ? QStringLiteral("https://mod.mcimirror.top/curseforge")
                        : QStringLiteral("https://api.curseforge.com"));

    // 反查可能要一两秒，慢的话先说一句，别让用户以为点空了
    QSharedPointer<bool> settled(new bool(false));
    QTimer::singleShot(kSlowHintDelayMs, this, [this, context, settled]() {
        if (*settled)
            return;
        notify(context, tr("正在方块盒子里打开这个资源…"));
    });

    connect(api, &CurseForgeAPI::modResolvedBySlug, this,
            [this, api, target, context, settled](const ModInfo &info) {
                *settled = true;
                api->deleteLater();
                openContent(info, target.contentTypeKey, target.url, context);
            });
    connect(api, &CurseForgeAPI::modBySlugFailed, this,
            [this, api, target, context, settled](const QString &, const QString &) {
                *settled = true;
                api->deleteLater();
                notify(context, tr("没在方块盒子里找到这个 CurseForge 资源，已用浏览器打开"));
                openInBrowser(target.url);
            });

    api->fetchModBySlug(target.slug);
}

void LinkRouter::resolveModrinth(const Target &target, QWidget *context)
{
    auto *api = new ModrinthAPI(this);
    api->setProjectType(target.contentTypeKey);

    const QVariant useMcim = SettingsManager::instance()->property("use_mcim");
    api->setBaseUrl((useMcim.isValid() ? useMcim.toBool() : true)
                        ? QStringLiteral("https://mod.mcimirror.top/modrinth")
                        : QStringLiteral("https://api.modrinth.com"));

    QSharedPointer<bool> settled(new bool(false));
    QTimer::singleShot(kSlowHintDelayMs, this, [this, context, settled]() {
        if (*settled)
            return;
        notify(context, tr("正在方块盒子里打开这个资源…"));
    });

    connect(api, &ModrinthAPI::modDetailReceived, this,
            [this, api, target, context, settled](const ModInfo &detail) {
                *settled = true;
                api->deleteLater();
                ModInfo info = detail;
                if (info.source.isEmpty())
                    info.source = QStringLiteral("modrinth");
                // 链接看不出类型时（modrinth.com/project/…），按接口返回的项目类型打开
                const QString key = target.contentTypeKey.isEmpty() ? info.contentKind
                                                                    : target.contentTypeKey;
                openContent(info, key, target.url, context);
            });
    connect(api, &ModrinthAPI::modDetailFailed, this,
            [this, api, target, context, settled](const QString &) {
                *settled = true;
                api->deleteLater();
                notify(context, tr("没在方块盒子里找到这个 Modrinth 资源，已用浏览器打开"));
                openInBrowser(target.url);
            });

    api->fetchModDetail(target.slug);
}
