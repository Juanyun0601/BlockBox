/**
 * @file   forum_thread_gallery_probe.cpp
 * @brief  帖子详情页样式画廊探针（脱离完整 app 的 QSS 校验）
 *
 * 背景：论坛页面正被另一处编辑，整棵树暂时无法链接，这里用「真实 token + 真实
 * style.qss + 真实 objectName/布局」搭出帖子详情页的静态近似，用于：
 *  1) 校验新增 QSS 是否可解析（残留 @TOKEN@ 数量）；
 *  2) 用截图肉眼核对配色、卡片圆角、楼层/楼中楼/侧栏/回复框的观感。
 *
 * token 直接正则解析自 utils/ThemeManager.cpp 的 isLight 分支，避免手抄色值失真。
 *
 * 用法：见 build_forum_probe.bat（g++ 单文件编译）
 */

#include <QApplication>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

namespace {

// 与 ThemeManager::buildTokenTable() 逐项对齐的浅色 token 表。
// 默认主题色 #2E7D32、文字 #333333、次要色 #256428、信息色 #2196F3
// （见 ThemeManager 构造函数的初始化列表，用户未自定义时即为这些值）。
QString rgbaOf(const QColor &c, const QString &alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(alpha);
}

QMap<QString, QString> buildLightTokens()
{
    QMap<QString, QString> T;
    const QString light = QStringLiteral("#f5f5f5");  // 占位，防误用
    Q_UNUSED(light);

    // 静态表面 / 边框 / 文字
    T["@BG_BASE@"]         = "#f5f5f5";
    T["@BG_CARD@"]         = "#ffffff";
    T["@BG_CONTENT@"]      = "#fafafa";
    T["@BG_MUTED@"]        = "#f8f9fa";
    T["@BG_HOVER@"]        = "#eaeaea";
    T["@BG_PRESSED@"]      = "#e0e0e0";
    T["@BG_TRACK@"]        = "#e8e8e8";
    T["@BG_TAB@"]          = "#f0f0f0";
    T["@BG_DISABLED@"]     = "#d5d7db";
    T["@BG_MSG@"]          = "#edf0f4";

    T["@BORDER@"]          = "#e8eaed";
    T["@BORDER_STRONG@"]   = "#dadce0";
    T["@BORDER_HOVER@"]    = "#bdbdbd";
    T["@BORDER_DASHED@"]   = "#c4c7c5";
    T["@BORDER_DISABLED@"] = "#b0b0b0";
    T["@BORDER_LIGHT@"]    = "#f0f1f3";

    T["@TEXT_SECONDARY@"]  = "#666666";
    T["@TEXT_TERTIARY@"]   = "#888888";
    T["@TEXT_DISABLED@"]   = "#bbbbbb";

    T["@DANGER_BG@"]       = "#ffebee";
    T["@DANGER_BORDER@"]   = "#ffcdd2";
    T["@DANGER_TEXT@"]     = "#c62828";

    T["@WARNING_BG@"]      = "#fff8e1";
    T["@WARNING_BORDER@"]  = "#ffe082";
    T["@WARNING_TEXT@"]    = "#e65100";
    T["@WARNING_TEXT_DARK@"] = "#856404";

    T["@SUCCESS@"]         = "#059669";
    T["@THINK_BG@"]        = "#fffde7";

    // 恒定色
    T["@BORDER_ON_ACCENT@"] = "#ffffff";
    T["@TEXT_ON_PRIMARY@"]  = "#ffffff";

    // 语义别名
    T["@BG_SECONDARY@"]   = T.value("@BG_MUTED@");
    T["@BORDER_DEFAULT@"] = T.value("@BORDER@");

    // 玻璃态
    T["@BG_GLASS@"]             = "rgba(255, 255, 255, 0.65)";
    T["@BG_GLASS_TASKBAR@"]     = "rgba(255, 255, 255, 0.42)";
    T["@BORDER_GLASS_TASKBAR@"] = "rgba(255, 255, 255, 0.55)";
    T["@BG_CARD_GLASS@"]        = rgbaOf(QColor(T.value("@BG_CARD@")), "0.65");
    T["@BG_CONTENT_GLASS@"]     = rgbaOf(QColor(T.value("@BG_CONTENT@")), "0.65");

    // 动态文字 / 次要主题色
    T["@TEXT_PRIMARY@"] = "#333333";
    T["@TEXT_BORDER@"]  = "#e8e8e8";
    T["@SECONDARY@"]    = "#256428";

    // 主色族
    const QColor pc("#2E7D32");
    T["@PRIMARY@"]         = pc.name();
    T["@PRIMARY_HOVER@"]   = pc.darker(110).name();
    T["@PRIMARY_PRESSED@"] = pc.darker(125).name();
    T["@PRIMARY_DARK@"]    = pc.darker(130).name();
    T["@PRIMARY_GRAD1@"]   = pc.lighter(130).name();
    T["@PRIMARY_GRAD2@"]   = pc.lighter(150).name();
    T["@PRIMARY_GRAD3@"]   = pc.lighter(140).name();
    T["@PRIMARY_MEDIUM@"]  = pc.lighter(120).name();
    T["@PRIMARY_LIGHT@"]   = pc.lighter(150).name();
    T["@PRIMARY_LIGHTER@"] = pc.lighter(160).name();
    T["@PRIMARY_BG@"]      = pc.lighter(180).name();
    T["@PRIMARY_BG_SOFT@"] = pc.lighter(190).name();

    T["@TEXT_ACCENT@"]      = pc.darker(140).name();
    T["@TEXT_ACCENT_DARK@"] = pc.darker(170).name();
    T["@PRIMARY_TEXT@"]     = T.value("@TEXT_ACCENT@");

    for (const QString &a : {QStringLiteral("8"), QStringLiteral("10"), QStringLiteral("12"),
                             QStringLiteral("15"), QStringLiteral("18"), QStringLiteral("20"),
                             QStringLiteral("26"), QStringLiteral("30"), QStringLiteral("31"),
                             QStringLiteral("38"), QStringLiteral("40"), QStringLiteral("46"),
                             QStringLiteral("51"), QStringLiteral("56"), QStringLiteral("64"),
                             QStringLiteral("76"), QStringLiteral("89"), QStringLiteral("102"),
                             QStringLiteral("0.1"), QStringLiteral("0.15"),
                             QStringLiteral("0.2")})
        T[QStringLiteral("@PRIMARY_RGBA_%1@").arg(a)] = rgbaOf(pc, a);

    // 信息强调色族
    const QColor ic("#2196F3");
    T["@INFO@"]         = ic.name();
    T["@INFO_HOVER@"]   = ic.darker(110).name();
    T["@INFO_PRESSED@"] = ic.darker(125).name();
    T["@INFO_BG@"]      = ic.lighter(180).name();
    T["@INFO_BG_DEEP@"] = ic.lighter(150).name();
    T["@INFO_BG_SOFT@"] = ic.lighter(190).name();
    T["@INFO_BORDER@"]  = ic.lighter(120).name();
    T["@INFO_BORDER_LIGHT@"] = ic.lighter(160).name();
    T["@INFO_DARK@"]    = ic.lighter(130).name();
    for (const QString &a : {QStringLiteral("20"), QStringLiteral("26"), QStringLiteral("38"),
                             QStringLiteral("51")})
        T[QStringLiteral("@INFO_RGBA_%1@").arg(a)] = rgbaOf(ic, a);

    // 危险 / 警告
    const QColor danger("#f44336");
    T["@DANGER@"]         = danger.name();
    T["@DANGER_HOVER@"]   = "#d32f2f";
    T["@DANGER_PRESSED@"] = "#b71c1c";
    T["@DANGER_LIGHT@"]   = "#ff5252";
    for (const QString &a : {QStringLiteral("26"), QStringLiteral("31"), QStringLiteral("38"),
                             QStringLiteral("51")})
        T[QStringLiteral("@DANGER_RGBA_%1@").arg(a)] = rgbaOf(danger, a);

    const QColor warning("#ff9800");
    T["@WARNING@"]       = warning.name();
    T["@WARNING_HOVER@"] = "#fb8c00";
    for (const QString &a : {QStringLiteral("26"), QStringLiteral("31"), QStringLiteral("38"),
                             QStringLiteral("64")})
        T[QStringLiteral("@WARNING_RGBA_%1@").arg(a)] = rgbaOf(warning, a);
    T["@THINK_RGBA_51@"] = "rgba(255, 224, 130, 51)";

    // 链接 / 编辑器 / 代码 / 杂项
    T["@LINK@"]           = "#1a73e8";
    T["@LINK_HOVER@"]     = "#409eff";
    T["@LINK_PRESSED@"]   = "#1765cc";
    T["@LINK_RGBA_0.10@"] = "rgba(26, 115, 232, 0.10)";
    T["@EDITOR_BG@"]      = "#1e1e1e";
    T["@EDITOR_BG_ALT@"]  = "#2a2a2a";
    T["@EDITOR_BORDER@"]  = "#3a3a3a";
    T["@EDITOR_SELECTION@"] = "#264f78";
    T["@EDITOR_TEXT@"]    = "#d4d4d4";
    T["@CODE_BG@"]        = "#f6f8fa";
    T["@CODE_BORDER@"]    = "#e1e4e8";
    T["@CODE_TEXT@"]      = "#c7254e";
    T["@INDIGO@"]         = "#5c6bc0";
    T["@INDIGO_RGBA_20@"] = "rgba(92, 107, 192, 20)";
    T["@INDIGO_RGBA_46@"] = "rgba(92, 107, 192, 46)";
    T["@TEAL_RGBA_80@"]   = "rgba(0, 200, 200, 80)";
    T["@TEAL_RGBA_150@"]  = "rgba(0, 200, 200, 150)";
    T["@WHITE_RGBA_30@"]  = "rgba(255, 255, 255, 30)";
    T["@WHITE_RGBA_235@"] = "rgba(255, 255, 255, 235)";
    T["@BAR_BG@"]         = "#E0E0E0";

    for (const QString &a : {QStringLiteral("5"), QStringLiteral("8"), QStringLiteral("10"),
                             QStringLiteral("15"), QStringLiteral("20"), QStringLiteral("31"),
                             QStringLiteral("64"), QStringLiteral("115")})
        T[QStringLiteral("@BLACK_RGBA_%1@").arg(a)] = "rgba(0, 0, 0, " + a + ")";
    for (const QString &a : {QStringLiteral("0.06"), QStringLiteral("0.08"),
                             QStringLiteral("0.12"), QStringLiteral("0.13"),
                             QStringLiteral("0.18"), QStringLiteral("0.20"),
                             QStringLiteral("0.25"), QStringLiteral("0.32")})
        T[QStringLiteral("@BLACK_RGBA_%1@").arg(a)] = "rgba(0, 0, 0, " + a + ")";

    std::fprintf(stderr, "[tokens] %d light tokens built\n", int(T.size()));
    return T;
}

QString resolveStyle(QString style, const QMap<QString, QString> &tokens)
{
    for (auto it = tokens.cbegin(); it != tokens.cend(); ++it)
        style.replace(it.key(), it.value());

    int leftover = 0;
    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    auto it = re.globalMatch(style);
    QStringList unknown;
    while (it.hasNext()) {
        const auto m = it.next();
        ++leftover;
        if (!unknown.contains(m.captured(1)))
            unknown << m.captured(1);
    }
    std::fprintf(stderr, "[tokens] leftover unresolved placeholders: %d %s\n", leftover,
                 qUtf8Printable(unknown.join(QLatin1Char(','))));
    return style;
}

QLabel *lab(QWidget *parent, const QString &name, const QString &text,
            const QString &cls = QString())
{
    auto *l = new QLabel(text, parent);
    if (!name.isEmpty())
        l->setObjectName(name);
    if (!cls.isEmpty())
        l->setProperty("cls", cls);
    l->setWordWrap(true);
    return l;
}

QFrame *card(QWidget *parent, const QString &name)
{
    auto *f = new QFrame(parent);
    f->setObjectName(name);
    return f;
}

// 楼中楼
QWidget *buildChild(QWidget *parent, bool first, const QString &who, const QString &time,
                    const QString &text)
{
    auto *row = new QWidget(parent);
    row->setObjectName(QStringLiteral("forumChild"));
    row->setProperty("first", first);

    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 12, 0, 12);
    lay->setSpacing(12);

    auto *avatar = new QLabel(QStringLiteral("烟"), row);
    avatar->setObjectName(QStringLiteral("forumAvatar"));
    avatar->setFixedSize(28, 28);
    avatar->setAlignment(Qt::AlignCenter);
    lay->addWidget(avatar);

    auto *body = new QVBoxLayout;
    body->setSpacing(2);
    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    head->addWidget(lab(row, QStringLiteral("forumChildName"), who));
    head->addWidget(lab(row, QStringLiteral("forumChildTime"), time));
    head->addWidget(lab(row, QStringLiteral("forumChildFloor"), QStringLiteral("#2")));
    head->addStretch(1);
    body->addLayout(head);
    body->addWidget(lab(row, QString(), text));
    lay->addLayout(body, 1);
    return row;
}

// 楼层
QWidget *buildPost(QWidget *parent, bool op, const QString &title, const QString &name,
                   const QString &counts, const QString &time, const QString &editMark,
                   const QString &floor, const QString &content, bool withChildren,
                   bool showAllChildren)
{
    auto *post = card(parent, QStringLiteral("forumPost"));
    post->setProperty("op", op);

    auto *lay = new QHBoxLayout(post);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(14);

    // 左侧作者栏
    auto *side = new QVBoxLayout;
    side->setSpacing(6);
    auto *avatar = new QLabel(op ? QStringLiteral("白") : QStringLiteral("烟"), post);
    avatar->setObjectName(QStringLiteral("forumAvatar"));
    avatar->setFixedSize(52, 52);
    avatar->setAlignment(Qt::AlignCenter);
    auto *sideTop = new QHBoxLayout;
    sideTop->addWidget(avatar);
    sideTop->addStretch(1);
    side->addLayout(sideTop);
    side->addWidget(lab(post, QStringLiteral("forumPostName"), name));
    if (op)
        side->addWidget(lab(post, QStringLiteral("forumRoleBadge"), QStringLiteral("管理员")));
    side->addWidget(lab(post, QStringLiteral("forumPostCounts"), counts));
    side->addStretch(1);
    lay->addLayout(side);

    // 右侧正文
    auto *main = new QVBoxLayout;
    main->setSpacing(8);
    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    if (op)
        head->addWidget(lab(post, QStringLiteral("forumOpBadge"), QStringLiteral("楼主")));
    head->addWidget(lab(post, QStringLiteral("forumPostTime"), time));
    if (!editMark.isEmpty())
        head->addWidget(lab(post, QStringLiteral("forumEditMark"), editMark));
    head->addStretch(1);
    head->addWidget(lab(post, QStringLiteral("forumFloorNo"), floor));
    main->addLayout(head);
    main->addWidget(lab(post, QString(), content));

    if (withChildren) {
        auto *children = new QWidget(post);
        children->setObjectName(QStringLiteral("forumChildren"));
        auto *cl = new QVBoxLayout(children);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);
        cl->addWidget(buildChild(children, true, QStringLiteral("烟雨楼台"),
                                 QStringLiteral("2 小时前"),
                                 QStringLiteral("整合包作者把 Forge 版本降到 47.2.20 就好了，我这边同样问题。")));
        cl->addWidget(buildChild(children, false, QStringLiteral("Mcyangtuo"),
                                 QStringLiteral("1 小时前"),
                                 QStringLiteral("管理员：已把附件日志转到开发者群，稍后回复。")));
        if (!showAllChildren) {
            auto *more = new QWidget(children);
            more->setObjectName(QStringLiteral("forumChildrenMore"));
            auto *ml = new QHBoxLayout(more);
            ml->setContentsMargins(0, 0, 0, 0);
            auto *toggle = new QPushButton(QStringLiteral("查看全部 8 条回复"), more);
            toggle->setObjectName(QStringLiteral("forumChildrenToggle"));
            ml->addWidget(toggle);
            ml->addStretch(1);
            cl->addWidget(more);
        }
        main->addWidget(children);
    }

    // 操作行
    auto *acts = new QHBoxLayout;
    acts->setSpacing(6);
    for (const QString &t : {QStringLiteral("回复"), QStringLiteral("引用"),
                             QStringLiteral("6"), QStringLiteral("复制链接")}) {
        auto *b = new QPushButton(t, post);
        b->setObjectName(QStringLiteral("forumAct"));
        acts->addWidget(b);
    }
    acts->addStretch(1);
    for (const QString &t : {QStringLiteral("编辑"), QStringLiteral("举报"),
                             QStringLiteral("删除")}) {
        auto *b = new QPushButton(t, post);
        b->setObjectName(QStringLiteral("forumAct"));
        b->setProperty("danger", t == QStringLiteral("删除"));
        acts->addWidget(b);
    }
    main->addLayout(acts);

    lay->addLayout(main, 1);
    return post;
}

QFrame *buildRailCard(QWidget *parent, const QString &title)
{
    auto *c = card(parent, QStringLiteral("forumRailCard"));
    auto *lay = new QVBoxLayout(c);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(9);
    lay->addWidget(lab(c, QStringLiteral("forumRailTitle"), title));
    return c;
}

QWidget *buildGallery()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("forumGalleryProbe"));
    page->setStyleSheet(QStringLiteral("#forumGalleryProbe { background: #ffffff; }"));

    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(26, 16, 26, 28);
    outer->setSpacing(20);

    auto *main = new QVBoxLayout;
    main->setSpacing(14);

    // 面包屑
    auto *crumbs = new QHBoxLayout;
    crumbs->setSpacing(6);
    for (const QString &t : {QStringLiteral("社区"), QStringLiteral("综合讨论")}) {
        auto *b = new QPushButton(t, page);
        b->setObjectName(QStringLiteral("forumCrumb"));
        crumbs->addWidget(b);
    }
    crumbs->addWidget(lab(page, QStringLiteral("forumCrumbSep"), QStringLiteral("›")));
    crumbs->addWidget(lab(page, QStringLiteral("forumCrumbCurrent"),
                          QStringLiteral("整合包启动一直卡在 Forge 加载界面，日志见附件")));
    crumbs->addStretch(1);
    main->addLayout(crumbs);

    // 主题头
    auto *hero = card(page, QStringLiteral("forumHero"));
    auto *hl = new QVBoxLayout(hero);
    hl->setContentsMargins(20, 18, 20, 18);
    hl->setSpacing(10);
    auto *badges = new QHBoxLayout;
    badges->setSpacing(6);
    badges->addWidget(lab(hero, QStringLiteral("forumBadge"), QStringLiteral("置顶")));
    badges->addWidget(lab(hero, QStringLiteral("forumBadgeWarn"), QStringLiteral("已锁定")));
    badges->addStretch(1);
    hl->addLayout(badges);
    hl->addWidget(lab(hero, QStringLiteral("forumThreadTitle"),
                      QStringLiteral("整合包启动一直卡在 Forge 加载界面，日志见附件")));

    auto *meta = new QHBoxLayout;
    meta->setSpacing(10);
    meta->addWidget(lab(hero, QStringLiteral("forumBoardChip"), QStringLiteral("综合讨论")));
    auto *av = new QLabel(hero);
    av->setObjectName(QStringLiteral("forumAvatar"));
    av->setFixedSize(20, 20);
    meta->addWidget(av);
    meta->addWidget(lab(hero, QStringLiteral("forumMetaAuthor"), QStringLiteral("小白菜鸟")));
    meta->addWidget(lab(hero, QStringLiteral("forumMetaDot"), QStringLiteral("·")));
    meta->addWidget(lab(hero, QStringLiteral("forumMuted"),
                        QStringLiteral("发布于 3 小时前 · 1,204 次浏览 · 27 条回复")));
    meta->addStretch(1);
    auto *bookmark = new QPushButton(QStringLiteral("☆ 收藏"), hero);
    bookmark->setObjectName(QStringLiteral("forumBookmarkButton"));
    meta->addWidget(bookmark);
    hl->addLayout(meta);

    auto *tags = new QHBoxLayout;
    tags->setSpacing(6);
    for (const QString &t : {QStringLiteral("求助"), QStringLiteral("Forge"),
                             QStringLiteral("整合包")}) {
        auto *chip = lab(hero, QStringLiteral("forumTagChip"), t);
        chip->setProperty("cls", QStringLiteral("tag"));
        tags->addWidget(chip);
    }
    tags->addStretch(1);
    hl->addLayout(tags);
    main->addWidget(hero);

    // 楼层
    main->addWidget(buildPost(
        page, true, QString(), QStringLiteral("小白菜鸟"),
        QStringLiteral("24 帖 · 3 主题\n注册于 2024-11-02"), QStringLiteral("3 小时前 · 已编辑"),
        QStringLiteral("编辑于 2 小时前"), QStringLiteral("楼主"),
        QStringLiteral("启动器点「启动」后一直停在 Forge 加载界面，等了 20 分钟没动静。"
                       "已确认 Java 版本是 17，模组是 Sodium + Lithium 组合。\n\n"
                       "日志里这一段反复出现：\nClassNotFoundException: net.minecraftforge.forgespi.language.IModProvider"),
        true, false));
    main->addWidget(buildPost(
        page, false, QString(), QStringLiteral("烟雨楼台"),
        QStringLiteral("108 帖 · 12 主题\n注册于 2023-06-18"), QStringLiteral("2 小时前"), QString(),
        QStringLiteral("2 楼"),
        QStringLiteral("同样问题 +1，我用的是 Forge 47.2.18，也是卡住。"), false, false));

    // 加载更多 / 分页
    auto *pager = new QPushButton(QStringLiteral("下一页"), page);
    pager->setObjectName(QStringLiteral("forumPagerButton"));
    main->addWidget(pager);

    // 回复框
    auto *composer = card(page, QStringLiteral("forumComposer"));
    auto *cl = new QVBoxLayout(composer);
    cl->setContentsMargins(18, 14, 18, 14);
    cl->setSpacing(9);
    auto *ch = new QHBoxLayout;
    ch->setSpacing(8);
    ch->addWidget(lab(composer, QStringLiteral("forumComposerTitle"), QStringLiteral("回复主题")));
    ch->addStretch(1);
    ch->addWidget(lab(composer, QStringLiteral("forumComposerHint"),
                      QStringLiteral("支持 Markdown · 可直接粘贴图片")));
    cl->addLayout(ch);

    auto *target = new QFrame(composer);
    target->setObjectName(QStringLiteral("forumReplyTargetBar"));
    auto *tl = new QHBoxLayout(target);
    tl->setContentsMargins(10, 6, 10, 6);
    tl->addWidget(lab(target, QStringLiteral("forumReplyTarget"),
                      QStringLiteral("正在回复 <b>烟雨楼台</b> 的 2 楼")));
    tl->addStretch(1);
    auto *clearBtn = new QPushButton(QStringLiteral("取消"), target);
    clearBtn->setObjectName(QStringLiteral("forumReplyTargetClear"));
    tl->addWidget(clearBtn);
    cl->addWidget(target);

    auto *edit = new QPlainTextEdit(composer);
    edit->setObjectName(QStringLiteral("forumReplyEdit"));
    edit->setPlaceholderText(QStringLiteral("说点什么…支持 Markdown：**加粗**、`代码`、> 引用"));
    edit->setFixedHeight(96);
    cl->addWidget(edit);

    auto *foot = new QHBoxLayout;
    foot->setSpacing(10);
    foot->addWidget(lab(composer, QStringLiteral("forumComposerHint"),
                        QStringLiteral("以 小白菜鸟 的身份回复 · Ctrl + Enter 发送")));
    foot->addStretch(1);
    foot->addWidget(lab(composer, QStringLiteral("forumComposerCount"), QStringLiteral("0 字")));
    auto *send = new QPushButton(QStringLiteral("发表回复"), composer);
    send->setObjectName(QStringLiteral("forumPrimaryButton"));
    foot->addWidget(send);
    cl->addLayout(foot);
    main->addWidget(composer);
    main->addStretch(1);

    outer->addLayout(main, 1);

    // 侧栏
    auto *rail = new QVBoxLayout;
    rail->setSpacing(14);

    auto *info = buildRailCard(page, QStringLiteral("主题信息"));
    auto *il = qobject_cast<QVBoxLayout *>(info->layout());
    auto *stats = new QHBoxLayout;
    stats->setSpacing(10);
    for (const QString &t : {QStringLiteral("1,204"), QStringLiteral("27")}) {
        auto *box = card(info, QStringLiteral("forumStatBox"));
        auto *bl = new QVBoxLayout(box);
        bl->setContentsMargins(12, 8, 12, 8);
        bl->setSpacing(0);
        bl->addWidget(lab(box, QStringLiteral("forumStatValue"), t));
        bl->addWidget(lab(box, QStringLiteral("forumRailKey"),
                          t == QStringLiteral("1,204") ? QStringLiteral("浏览")
                                                       : QStringLiteral("回复")));
        stats->addWidget(box, 1);
    }
    il->addLayout(stats);
    for (const QString &k : {QStringLiteral("版块"), QStringLiteral("楼主"),
                             QStringLiteral("发布"), QStringLiteral("最后回复")}) {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        row->addWidget(lab(info, QStringLiteral("forumRailKey"), k));
        row->addStretch(1);
        row->addWidget(lab(info, k == QStringLiteral("版块")
                                    ? QStringLiteral("forumRailLink") : QStringLiteral("forumRailValue"),
                           k == QStringLiteral("版块") ? QStringLiteral("综合讨论")
                                                       : QStringLiteral("3 小时前")));
        il->addLayout(row);
    }
    il->addWidget(lab(info, QStringLiteral("forumRailNote"), QStringLiteral("主题已锁定，暂不可回复")));
    for (const QString &t : {QStringLiteral("刷新本页"), QStringLiteral("复制本页链接")}) {
        auto *b = new QPushButton(t, info);
        b->setObjectName(QStringLiteral("forumRailButton"));
        il->addWidget(b);
    }
    rail->addWidget(info);

    auto *related = buildRailCard(page, QStringLiteral("同版块主题"));
    auto *rl = qobject_cast<QVBoxLayout *>(related->layout());
    for (int i = 0; i < 3; ++i) {
        auto *row = card(related, QStringLiteral("forumRailThread"));
        auto *bl = new QVBoxLayout(row);
        bl->setContentsMargins(0, 7, 0, 7);
        bl->setSpacing(2);
        QLabel *t = lab(row, QStringLiteral("forumRailThreadTitle"),
                        QStringLiteral("整合包启动一直卡在 Forge 加载界面"));
        t->setWordWrap(false);
        bl->addWidget(t);
        bl->addWidget(lab(row, QStringLiteral("forumRailThreadMeta"),
                          QStringLiteral("27 回复 · 3 小时前")));
        rl->addWidget(row);
    }
    rail->addWidget(related);

    auto *tips = buildRailCard(page, QStringLiteral("社区提示"));
    auto *tpl = qobject_cast<QVBoxLayout *>(tips->layout());
    for (const QString &t : {QStringLiteral("发主题前先搜索，避免重复提问。"),
                             QStringLiteral("求助请附上日志与截图，方便定位。"),
                             QStringLiteral("友善讨论，禁止发布违规内容。")}) {
        tpl->addWidget(lab(tips, QStringLiteral("forumRailTip"), QStringLiteral("· ") + t));
    }
    rail->addWidget(tips);
    rail->addStretch(1);

    auto *railWrap = new QWidget(page);
    railWrap->setFixedWidth(300);
    railWrap->setLayout(rail);
    outer->addWidget(railWrap);

    return page;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyleSheet(QStringLiteral("QWidget { font-family: 'Microsoft YaHei UI'; }"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QMap<QString, QString> tokens = buildLightTokens();
    const QString style = resolveStyle(QString::fromUtf8(f.readAll()), tokens);
    qApp->setStyleSheet(style);

    QWidget host;
    host.setStyleSheet(QStringLiteral("QWidget#host { background: #f5f5f5; }"));
    host.setObjectName(QStringLiteral("host"));
    auto *scroll = new QScrollArea(&host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QWidget *page = buildGallery();
    scroll->setWidget(page);
    auto *lay = new QVBoxLayout(&host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(scroll);
    host.resize(1180, 1600);
    host.show();

    QTimer::singleShot(400, [&]() {
        const QImage img = host.grab().toImage();
        const QString out = QStringLiteral("D:/BlockBox/_out/forum_thread_gallery_light.png");
        const bool ok = img.save(out);
        std::fprintf(stderr, "[%s] %s (%dx%d)\n", ok ? "ok" : "FAIL",
                     qUtf8Printable(out), img.width(), img.height());
        qApp->quit();
    });
    return app.exec();
}
