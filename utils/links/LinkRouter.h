#ifndef LINKROUTER_H
#define LINKROUTER_H

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

#include "utils/content/ContentData.h"
#include "utils/mod/ModData.h"

class QTextBrowser;
class QWidget;

/**
 * @brief 「点了一个链接」的统一入口
 *
 * 详情页（帖子正文、资源描述、相关链接、房间简介…）里点到的链接都先过这里：
 *   · 方块盒子自己的链接：blockbox:// 一键加入、论坛帖子 / 板块 —— 留在软件里打开；
 *   · CurseForge / Modrinth 资源页链接 —— 反查成软件自己的资源详情页再打开；
 *   · 其它链接 —— 照旧交给系统浏览器。
 *
 * 资源链接反查要走一次网络（CurseForge 得把项目页的 slug 换成数字 id，
 * Modrinth 用 slug 取一次项目信息），拿不到就退回浏览器，不会把链接吞掉。
 *
 * 页面只需要调 open()，具体「打开哪个页面」由 MainWindow 注册的 Handlers 决定，
 * 页面本身不依赖主窗口。
 */
class LinkRouter : public QObject
{
    Q_OBJECT

public:
    /** 链接归类结果 */
    enum class Kind
    {
        External,      ///< 站外链接：交给系统浏览器
        ForumThread,   ///< 论坛帖子：thread.html?id=
        ForumBoard,    ///< 论坛板块：board.html?slug=
        CurseForge,    ///< CurseForge 资源页
        Modrinth,      ///< Modrinth 资源页
        BlockBoxJoin   ///< blockbox:// 一键加入
    };

    struct Target
    {
        Kind kind = Kind::External;
        QUrl url;                 ///< 原始链接（反查失败时用它兜底开浏览器）
        QString slug;             ///< 资源页最后一段（CF / Modrinth 的 slug）
        QString contentTypeKey;   ///< mod / modpack / resourcepack / shader / datapack / world
        int threadId = 0;
        QString boardSlug;
        QString boardName;
    };

    /** 由主窗口注册：把「打开某个页面」的具体动作交回主窗口 */
    struct Handlers
    {
        std::function<void(int threadId)> openForumThread;
        std::function<void(const QString &slug, const QString &boardName)> openForumBoard;
        std::function<void(const QString &url)> handleJoinLink;
        std::function<void(const ModInfo &info, ContentType type)> openContentDetail;
    };

    static LinkRouter *instance();

    /** 只看链接本身，不发请求：给调用方判断「这条链接值不值得自己接」 */
    static Target classify(const QUrl &url);

    /**
     * @brief 给正文渲染控件接上智能链接
     *
     * QTextBrowser 默认把外链直接甩给系统浏览器，接上以后点链接会先过 open()。
     * 论坛正文控件（ForumPostBrowser）有自己的折叠 / 图片逻辑，不要用它替换。
     */
    static void wireTextBrowser(QTextBrowser *browser);

    void setHandlers(const Handlers &handlers);

    /**
     * @brief 打开一条链接
     * @param url     要打开的链接
     * @param context 触发点击的控件（只用来决定左下角提示挂在哪个窗口上，可为空）
     */
    void open(const QUrl &url, QWidget *context = nullptr);

private:
    explicit LinkRouter(QObject *parent = nullptr);

    void openInBrowser(const QUrl &url);
    /** 左下角提示：拿不到合适的窗口就不弹，避免提示飘到屏幕左上角 */
    void notify(QWidget *context, const QString &text);
    void openContent(const ModInfo &info, const QString &contentTypeKey,
                     const QUrl &fallback, QWidget *context);
    void resolveCurseForge(const Target &target, QWidget *context);
    void resolveModrinth(const Target &target, QWidget *context);

    Handlers m_handlers;
};

#endif // LINKROUTER_H
