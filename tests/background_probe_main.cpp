/**
 * @file   background_probe_main.cpp
 * @brief  自定义背景回归探针（不参与主程序构建，单独编译链接）
 *
 * 背景：设置页主栏被反馈"自定义背景无效"。本探针链接主程序全部目标文件
 * （仅替换 main.cpp），用真实 MainWindow 渲染出像素，做自动化判定：
 *
 *   1. 依次切换 背景模式 = 经典 / 纯色(品红) / 图片(纯黄) ，
 *      分别抓取 首页 与 设置页(界面设置子页) 的整窗图像；
 *   2. 在主栏区域统计背景色像素：透明链路生效 ⇒ 有大量背景色像素，
 *      若被不透明容器（如 #settingsRightFrame）遮挡 ⇒ 计数为 0；
 *   3. 对控件链逐层 grab() 采样（alpha + 主色），精确定位遮挡层；
 *   4. 结束后恢复用户的 background 设置项，截图存档到 build/probe_out/。
 *
 * 用法（构建目录下）：
 *   g++ <主程序编译参数> -o probe_main.o ../../tests/background_probe_main.cpp
 *   链接 debug/object_script 过滤掉 main.o 后的全体目标文件
 *   运行 BlockBoxProbe.exe（PATH 需含 Qt bin）
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QThread>
#include <QTimer>
#include <QStackedWidget>
#include <QScrollArea>
#include <QSettings>
#include <QDir>

#include "components/BackgroundWidget.h"
#include "components/SideBar.h"
#include "utils/BackgroundManager.h"
#include "utils/ThemeManager.h"

#include <cstdio>

namespace {

int g_parseWarnings = 0;
int g_unregisteredTokenWarnings = 0;

// Qt 日志默认在 Windows GUI 程序里走 OutputDebugString，控制台拿不到；
// 统一转成 fprintf(stderr)，保证探针结论能被脚本捕获
void stderrMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    if (msg.contains(QStringLiteral("Could not parse")))
        ++g_parseWarnings;
    if (msg.contains(QStringLiteral("QSS_UNREGISTERED_TOKEN")))
        ++g_unregisteredTokenWarnings;
    const char *tag = "[info ]";
    if (type == QtWarningMsg) tag = "[warn ]";
    else if (type == QtCriticalMsg || type == QtFatalMsg) tag = "[crit ]";
    fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    fflush(stderr);
}

// ── 样式表解析失败二分定位 ───────────────────────────────────────────────
// 去注释后按 '{'...'}' 拆规则；前缀二分找出第一条触发
// "Could not parse application stylesheet" 的规则
QStringList splitRules(const QString &qss)
{
    QString noComment;
    noComment.reserve(qss.size());
    bool inBlock = false, inStr = false;
    for (int i = 0; i < qss.size(); ++i) {
        const QChar ch = qss.at(i);
        const QChar next = (i + 1 < qss.size()) ? qss.at(i + 1) : QChar();
        if (!inStr && ch == '/' && next == '*') { inBlock = true; ++i; continue; }
        if (inBlock && ch == '*' && next == '/') { inBlock = false; ++i; continue; }
        if (!inBlock) {
            if (ch == '"') inStr = !inStr;
            noComment += ch;
        }
    }
    QStringList rules;
    int depth = 0, ruleStart = 0;
    for (int i = 0; i < noComment.size(); ++i) {
        const QChar ch = noComment.at(i);
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                const QString rule = noComment.mid(ruleStart, i - ruleStart + 1).trimmed();
                if (!rule.isEmpty())
                    rules << rule;
                ruleStart = i + 1;
            }
            if (depth < 0) depth = 0;
        }
    }
    return rules;
}

void findParseCulprit(const QString &sheet, const char *label)
{
    const QStringList rules = splitRules(sheet);
    qInfo().noquote() << QString("  [bisect:%1] rules=%2").arg(QString::fromLatin1(label))
        .arg(rules.size());
    auto triggers = [&rules](int n) {
        const int before = g_parseWarnings;
        QString s;
        for (int i = 0; i < n && i < rules.size(); ++i)
            s += "\n" + rules[i];
        qApp->setStyleSheet(s);
        return g_parseWarnings > before;
    };
    if (!triggers(rules.size())) {
        qInfo().noquote() << QString("  [bisect:%1] full sheet parses OK")
            .arg(QString::fromLatin1(label));
        return;
    }
    int lo = 1, hi = rules.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (triggers(mid)) hi = mid; else lo = mid + 1;
    }
    qInfo().noquote() << QString("  [culprit:%1] rule#%2: %3")
        .arg(QString::fromLatin1(label), QString::number(lo - 1),
             rules[lo - 1].simplified().left(220));
    qApp->setStyleSheet(sheet);   // 还原完整样式，避免污染后续截图
}


// 沿窗口局部坐标 point 逐层向上打印控件链（class/objectName/geometry），定位不透明层
void dumpChildChain(MainWindow &w, const QPoint &winPos, const char *label)
{
    QWidget *c = w.childAt(winPos);
    fprintf(stderr, "  [chain] %s @(%d,%d):\n", label, winPos.x(), winPos.y());
    while (c) {
        const QRect rg = QRect(c->mapTo(&w, QPoint(0, 0)), c->size());
        fprintf(stderr, "    %-22s obj=%-28s geom=%dx%d%+d%+d winRect=%d,%d %dx%d\n",
                c->metaObject()->className(), c->objectName().toLocal8Bit().constData(),
                c->width(), c->height(), c->x(), c->y(), rg.x(), rg.y(), rg.width(), rg.height());
        c = c->parentWidget();
        if (c == &w) {
            fprintf(stderr, "    MainWindow\n");
            break;
        }
    }
}


// ── 用户原有背景设置的保存/恢复 ──────────────────────────────────────────
struct SavedBg { int mode; QString solid; QString image; int blur; };

SavedBg loadSavedBg()
{
    QSettings s("BlockBox", "Settings");
    SavedBg v;
    v.mode  = s.value("background/mode", 0).toInt();
    v.solid = s.value("background/solidColor", "#f5f5f5").toString();
    v.image = s.value("background/imagePath", "").toString();
    v.blur  = s.value("background/blurRadius", 0).toInt();
    return v;
}

void restoreSavedBg(const SavedBg &v)
{
    QSettings s("BlockBox", "Settings");
    s.setValue("background/mode", v.mode);
    s.setValue("background/solidColor", v.solid);
    s.setValue("background/imagePath", v.image);
    s.setValue("background/blurRadius", v.blur);
}

// ── 事件循环等待（动画/样式重刷稳定） ────────────────────────────────────
void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
        QThread::msleep(15);
    }
}

// ── 像素统计 ─────────────────────────────────────────────────────────────
// regionLogical 为窗口逻辑坐标；统计区域内与 target 颜色近似(±tol)的像素数
long long countNear(const QImage &img, const QRectF &regionLogical,
                    const QColor &target, int tol = 8)
{
    const qreal dpr = img.devicePixelRatio();
    QRect r(qRound(regionLogical.x() * dpr), qRound(regionLogical.y() * dpr),
            qRound(regionLogical.width() * dpr), qRound(regionLogical.height() * dpr));
    r = r.intersected(img.rect());
    long long n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            const QRgb p = img.pixel(x, y);
            if (qAbs(qRed(p) - target.red()) <= tol &&
                qAbs(qGreen(p) - target.green()) <= tol &&
                qAbs(qBlue(p) - target.blue()) <= tol) {
                ++n;
            }
        }
    }
    return n;
}

// grab 单个控件并汇报：alpha 透明率 + 中心区域主色
void reportLayer(const QString &label, QWidget *w)
{
    if (!w) {
        qInfo().noquote() << QString("  [layer] %-28s : (null)").arg(label);
        return;
    }
    const QImage img = w->grab().toImage();
    const int cx = img.width() / 2, cy = img.height() / 2;
    const int half = qMax(2, qMin(img.width(), img.height()) / 8);
    long long total = 0, transparent = 0;
    long long rSum = 0, gSum = 0, bSum = 0, opaque = 0;
    for (int y = cy - half; y <= cy + half && y < img.height(); ++y) {
        for (int x = cx - half; x <= cx + half && x < img.width(); ++x) {
            const QRgb p = img.pixel(x, y);
            ++total;
            if (qAlpha(p) < 16) {
                ++transparent;
            } else {
                rSum += qRed(p); gSum += qGreen(p); bSum += qBlue(p);
                ++opaque;
            }
        }
    }
    if (total == 0) {
        qInfo().noquote() << QString("  [layer] %-28s : empty image").arg(label);
        return;
    }
    const double tRatio = double(transparent) / double(total);
    if (tRatio > 0.6) {
        qInfo().noquote() << QString("  [layer] %-28s : TRANSPARENT (%1% alpha<16)")
            .arg(label, QString::number(tRatio * 100, 'f', 0));
    } else if (opaque > 0) {
        const QColor avg(rSum / opaque, gSum / opaque, bSum / opaque);
        qInfo().noquote() << QString("  [layer] %-28s : OPAQUE  avg=%1  (transparent %2%)")
            .arg(label, avg.name(), QString::number(tRatio * 100, 'f', 0));
    }
}

// 主栏区域（contentWrapper 内偏右 60%，避开悬浮子导航气泡）
QRectF mainColumnRegion(MainWindow &w, QWidget *contentWrapper)
{
    const QRect g = contentWrapper->geometry();
    const QPoint tl = contentWrapper->parentWidget()->mapTo(&w, g.topLeft());
    const qreal x0 = tl.x() + g.width() * 0.40;
    const qreal wdt = g.width() * 0.56;
    const qreal y0 = tl.y() + g.height() * 0.12;
    const qreal hgt = g.height() * 0.76;
    return QRectF(x0, y0, wdt, hgt);
}

// 任意控件在窗口坐标下的区域（用于区域像素统计）
QRectF widgetRegion(MainWindow &w, QWidget *widget)
{
    if (!widget) return QRectF();
    const QPoint tl = widget->mapTo(&w, QPoint(0, 0));
    return QRectF(tl.x(), tl.y(), widget->width(), widget->height());
}

void saveShot(const QImage &img, const QString &name)
{
    const QString dir = qApp->applicationDirPath() + "/probe_out";
    QDir().mkpath(dir);
    const QString path = dir + "/" + name;
    img.save(path);
    qInfo().noquote() << QString("  [shot] %1").arg(path);
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    const SavedBg saved = loadSavedBg();

    // 主程序启动路径等价：先应用主题（内部会拼入背景覆盖样式）
    ThemeManager::instance()->applyThemeColor();

    MainWindow w;
    w.show();
    settle(1200);

    BackgroundManager *bg = BackgroundManager::instance();

    // 测试用背景色：品红（纯色模式）/ 纯黄（图片模式）
    const QColor kProbeSolid("#FF20E0");
    const QColor kProbeImage("#FFD400");

    // 生成纯色测试图（右下角一小块蓝，便于后续区分旋转/拉伸，不影响本探针判定）
    const QString testImg = qApp->applicationDirPath() + "/probe_bg_test.png";
    {
        QPixmap pm(640, 420);
        pm.fill(kProbeImage);
        QPainter p(&pm);
        p.fillRect(pm.width() - 60, pm.height() - 60, 60, 60, QColor(0, 90, 220));
        p.end();
        pm.save(testImg);
    }

    int failures = 0;
    auto expect = [&](const char *what, bool ok, const QString &detail) {
        qInfo().noquote() << QString("  [%1] %2  %3")
            .arg(ok ? "PASS" : "FAIL", QString::fromLatin1(what), detail);
        if (!ok) ++failures;
    };

    // ── 场景 A：首页 ─────────────────────────────────────────────────────
    qInfo() << "== A. HomePage ==";
    QWidget *contentWrapper = nullptr;
    if (auto *bgw = w.findChild<BackgroundWidget *>())
        contentWrapper = bgw->parentWidget();
    const QRectF homeRegion = contentWrapper ? mainColumnRegion(w, contentWrapper)
                                             : QRectF(60, 60, 900, 600);

    bg->setMode(BackgroundManager::Classic);
    settle(500);
    const QImage homeClassic = w.grab().toImage();
    saveShot(homeClassic, "home_classic.png");

    bg->setSolidColor(kProbeSolid.name());
    bg->setMode(BackgroundManager::SolidColor);
    settle(700);
    const QImage homeSolid = w.grab().toImage();
    saveShot(homeSolid, "home_solid.png");
    {
        const long long n = countNear(homeSolid, homeRegion, kProbeSolid);
        expect("home solid shows through", n > 1000,
               QString("solid-pixels=%1").arg(n));
    }
    // 左侧主导航栏自带玻璃态底色，不参与透明化
    {
        const QRectF sb = widgetRegion(w, w.findChild<SideBar *>());
        const long long n = countNear(homeSolid, sb, kProbeSolid);
        expect("left sidebar does NOT show custom background",
               !sb.isEmpty() && n < 500,
               QString("solid-pixels=%1 region-empty=%2").arg(n).arg(sb.isEmpty()));
    }
    // 顶栏与内容区之间不能留出透出背景的横缝（topBarSeparator 曾把约束写在宽度轴上）
    if (contentWrapper) {
        const QPoint tl = contentWrapper->mapTo(&w, QPoint(0, 0));
        const QRectF gap(tl.x(), tl.y() - 3, contentWrapper->width(), 3);
        const long long n = countNear(homeSolid, gap, kProbeSolid);
        expect("no background-colored gap under the top bar", n < 50,
               QString("solid-pixels=%1").arg(n));
    }

    // ── 场景 A2：实例选择页（顶部工具条保持底色，列表仍透出背景） ──────────
    qInfo() << "== A2. InstanceSelectPage ==";
    QMetaObject::invokeMethod(&w, "onInstanceSelectClicked", Qt::QueuedConnection);
    settle(1500);
    {
        const QImage instSolid = w.grab().toImage();
        saveShot(instSolid, "instance_select_solid.png");

        const QRectF topBand = widgetRegion(w, w.findChild<QWidget *>("instancePageTopBar"));
        const long long topN = countNear(instSolid, topBand, kProbeSolid);
        expect("instance select top toolbar does NOT show custom background",
               !topBand.isEmpty() && topN < 500,
               QString("solid-pixels=%1 region-empty=%2").arg(topN).arg(topBand.isEmpty()));

        const QRectF listReg = widgetRegion(w, w.findChild<QScrollArea *>("instanceScrollArea"));
        const long long listN = countNear(instSolid, listReg, kProbeSolid);
        expect("instance select list still shows custom background",
               !listReg.isEmpty() && listN > 300,
               QString("solid-pixels=%1").arg(listN));
    }
    // 实例选择页会隐藏主导航栏（setSideBarVisible(false)），
    // 走真实的返回路径恢复，后续场景才不会拿到隐藏状态
    QMetaObject::invokeMethod(&w, "onBackToMain", Qt::QueuedConnection);
    settle(900);

    // ── 场景 B：设置页（界面设置子页） ───────────────────────────────────
    qInfo() << "== B. SettingsPage / Interface tab ==";
    QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Q_ARG(int, 2));
    settle(1500);
    QMetaObject::invokeMethod(&w, "onSettingsNavItemClicked", Q_ARG(int, 1));
    settle(600);

    const QRectF settingsRegion = contentWrapper ? mainColumnRegion(w, contentWrapper)
                                                 : homeRegion;

    bg->setMode(BackgroundManager::Classic);
    settle(500);
    const QImage settingsClassic = w.grab().toImage();
    saveShot(settingsClassic, "settings_classic.png");

    bg->setMode(BackgroundManager::SolidColor);
    settle(700);
    const QImage settingsSolid = w.grab().toImage();
    saveShot(settingsSolid, "settings_solid.png");
    {
        const long long n = countNear(settingsSolid, settingsRegion, kProbeSolid);
        expect("settings main column solid shows through", n > 1000,
               QString("solid-pixels=%1").arg(n));
    }

    bg->setBlurRadius(0);
    bg->setMode(BackgroundManager::Image);
    bg->setImagePath(testImg);
    settle(900);
    const QImage settingsImage = w.grab().toImage();
    saveShot(settingsImage, "settings_image.png");
    {
        const long long n = countNear(settingsImage, settingsRegion, kProbeImage);
        expect("settings main column image shows through", n > 1000,
               QString("image-pixels=%1").arg(n));
    }

    // ── 场景 C：逐层诊断（纯色模式仍生效时抓控件链） ─────────────────────
    qInfo() << "== C. Layer diagnosis (SolidColor active) ==";

    // 用 childAt 精确定位主栏/导航两处的控件链
    if (contentWrapper) {
        const QRectF reg = mainColumnRegion(w, contentWrapper);
        dumpChildChain(w, QPoint(int(reg.center().x()), int(reg.center().y())), "main column");
        dumpChildChain(w, QPoint(contentWrapper->x() + 150, contentWrapper->y() + 300), "left of column");
    }

    bg->setMode(BackgroundManager::SolidColor);
    settle(600);

    qInfo() << "  widget chain under #contentWrapper:";
    reportLayer("BackgroundWidget", w.findChild<BackgroundWidget *>());
    if (contentWrapper) {
        reportLayer("contentWrapper", contentWrapper);
        QStackedWidget *stack = contentWrapper->findChild<QStackedWidget *>();
        reportLayer("QStackedWidget(main)", stack);
        if (stack) {
            QWidget *page = stack->widget(static_cast<int>(PageIndex::SettingsPage));
            reportLayer("page(scrollArea)", page);
            if (auto *sa = qobject_cast<QScrollArea *>(page)) {
                reportLayer("viewport", sa->viewport());
                QWidget *sp = sa->widget();
                reportLayer("SettingsPage", sp);
                if (sp) {
                    reportLayer("settingsRightFrame",
                                sp->findChild<QFrame *>("settingsRightFrame"));
                    reportLayer("settingsContentStack",
                                sp->findChild<QStackedWidget *>("settingsContentStack"));
                    reportLayer("settingsInterfaceContent",
                                sp->findChild<QWidget *>("settingsInterfaceContent"));
                }
            }
        }
    }

    // ── 场景 D：多页面诊断（经典 vs 图片 逐像素差异定位过度透明化） ──────
    // 差异大 ⇒ 该页面在自定义背景下被透明化（卡片/侧边栏失去自身背景）
    qInfo() << "== D. Multi-page diagnosis (Classic vs Image diff) ==";
    {
        struct PageNav { const char *name; int sidebarIndex; bool needChildNav; int childIndex; };
        const QList<PageNav> pages = {
            { "resources_home",    1, false, -1 },
            { "install_instance",  1, true,   0 },
            { "plugin_list",       5, false, -1 },
        };
        for (const PageNav &pg : pages) {
            QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Q_ARG(int, pg.sidebarIndex));
            settle(1400);
            if (pg.needChildNav) {
                QMetaObject::invokeMethod(&w, "onChildNavClicked",
                                          Q_ARG(int, pg.sidebarIndex), Q_ARG(int, pg.childIndex));
                settle(1400);
            }
            bg->setMode(BackgroundManager::Classic);
            settle(500);
            const QImage classic = w.grab().toImage();
            saveShot(classic, QString("%1_classic.png").arg(pg.name));

            bg->setMode(BackgroundManager::Image);
            settle(900);
            const QImage image = w.grab().toImage();
            saveShot(image, QString("%1_image.png").arg(pg.name));

            // 整窗差异像素数（容差 8）
            long long diff = 0;
            const QImage a = classic.convertToFormat(QImage::Format_RGB32);
            const QImage b = image.convertToFormat(QImage::Format_RGB32);
            for (int y = 0; y < a.height(); y += 2) {
                for (int x = 0; x < a.width(); x += 2) {
                    const QRgb pa = a.pixel(x, y), pb = b.pixel(x, y);
                    if (qAbs(qRed(pa) - qRed(pb)) > 8 || qAbs(qGreen(pa) - qGreen(pb)) > 8
                        || qAbs(qBlue(pa) - qBlue(pb)) > 8)
                        ++diff;
                }
            }
            // 侧边栏区域（x 0..76）差异
            long long sideDiff = 0;
            for (int y = 0; y < a.height(); y += 2) {
                for (int x = 0; x < 76 && x < a.width(); x += 2) {
                    const QRgb pa = a.pixel(x, y), pb = b.pixel(x, y);
                    if (qAbs(qRed(pa) - qRed(pb)) > 8 || qAbs(qGreen(pa) - qGreen(pb)) > 8
                        || qAbs(qBlue(pa) - qBlue(pb)) > 8)
                        ++sideDiff;
                }
            }
            qInfo().noquote() << QString("  [diff] %1: window-diff=%2 sidebar-diff=%3")
                .arg(QString::fromLatin1(pg.name), QString::number(diff),
                     QString::number(sideDiff));
        }

        // 回到设置页（界面设置）与首页，保持后续诊断上下文一致
        QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Q_ARG(int, 0));
        settle(1000);
    }

    // ── 场景 E：深色主题诊断（深色 + 自定义背景） ─────────────────────────
    qInfo() << "== E. Dark theme diagnosis ==";
    {
        QSettings themeSettings("BlockBox", "Settings");
        const QString savedTheme = themeSettings.value("theme", "light").toString();

        ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
        settle(900);

        // 深色拼装样式若解析失败，二分定位肇事规则
        findParseCulprit(qApp->styleSheet(), "dark-load");

        bg->setSolidColor(kProbeSolid.name());
        bg->setMode(BackgroundManager::SolidColor);
        settle(700);

        // 首页
        QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Q_ARG(int, 0));
        settle(1200);
        saveShot(w.grab().toImage(), "dark_home_solid.png");

        // 设置页（界面设置）
        QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Q_ARG(int, 2));
        settle(1400);
        QMetaObject::invokeMethod(&w, "onSettingsNavItemClicked", Q_ARG(int, 1));
        settle(600);
        const QImage darkSettingsSolid = w.grab().toImage();
        saveShot(darkSettingsSolid, "dark_settings_solid.png");
        expect("dark settings main column solid shows through",
               countNear(darkSettingsSolid, settingsRegion, kProbeSolid) > 1000,
               QString("solid-pixels=%1")
                   .arg(countNear(darkSettingsSolid, settingsRegion, kProbeSolid)));

        // 资源>安装新实例
        QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Q_ARG(int, 1));
        settle(1400);
        saveShot(w.grab().toImage(), "dark_install_solid.png");

        // 设置页 + 流光（流光底色为浅色渐变，检查深色下的表现）
        QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Q_ARG(int, 2));
        settle(1400);
        bg->setMode(BackgroundManager::FlowLight);
        settle(900);
        findParseCulprit(qApp->styleSheet(), "dark-flow");
        saveShot(w.grab().toImage(), "dark_settings_flow.png");

        // 还原浅色主题
        bg->setMode(BackgroundManager::Classic);
        if (savedTheme == QLatin1String("dark"))
            ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
        else if (savedTheme == QLatin1String("custom"))
            ThemeManager::instance()->loadTheme(ThemeManager::CustomTheme);
        else
            ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
        settle(600);
    }

    // ── 样式表完整性：出现 "Could not parse" 或未注册占位符就判失败 ────────
    // Qt 在第一条无法解析的规则处中止，其后所有规则（含追加在末尾的背景覆盖）
    // 全部失效 —— 未注册占位符是最常见的成因，这里必须是硬性断言。
    // 走 ThemeManager::reloadPluginStyles() 而不是直接 qApp->setStyleSheet：
    // 只有它会重新执行 resolveTokens，占位符告警才计得到。
    {
        const int warnBeforeFinal = g_parseWarnings;
        const int tokenBeforeFinal = g_unregisteredTokenWarnings;
        ThemeManager::instance()->reloadPluginStyles();
        QCoreApplication::processEvents();
        expect("application stylesheet parses cleanly (no QSS syntax error)",
               g_parseWarnings == warnBeforeFinal,
               QString("new-warnings=%1").arg(g_parseWarnings - warnBeforeFinal));
        expect("all style.qss placeholders are registered in buildTokenTable()",
               g_unregisteredTokenWarnings == tokenBeforeFinal,
               QString("dropped-rules=%1").arg(g_unregisteredTokenWarnings - tokenBeforeFinal));
    }

    // ── 收尾：恢复用户设置 ───────────────────────────────────────────────
    restoreSavedBg(saved);
    QFile::remove(testImg);

    qInfo().noquote() << QString("PROBE RESULT: %1").arg(failures == 0 ? "PASS" : "FAIL");
    QTimer::singleShot(120, &app, &QCoreApplication::quit);
    return app.exec() == 0 ? (failures == 0 ? 0 : 1) : 2;
}
