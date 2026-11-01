/**
 * @file   settings_width_probe.cpp
 * @brief  设置页宽度回归探针：逐子页打印 minimumSizeHint 宽度，定位横向滚动来源
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），构建真实 MainWindow 与设置页：
 *   1. 以最小窗口尺寸 1200x800 启动，走真实导航路径进入 设置>常规设置
 *      （侧栏第 2 项 → 子导航气泡打开，主内容区左侧留出气泡宽度）；
 *   2. 打印外层 QScrollArea 视口宽度、水平滚动条最大值/可见性，
 *      以及 QStackedWidget 内每个子页的 minimumSizeHint 宽度；
 *   3. 对每个子页截图，并列出最宽的 settingRow 行及撑宽子控件；
 *   4. 放大到 1600x900 再测一遍，用于对比“固定宽度”现象。
 *
 * 结论判定：任一子页 minimumSizeHint().width() 超过视口可用宽度时，
 * 外层 QScrollArea 会出现水平滚动条，设置页主栏表现为“固定宽度”。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>

#include "pages/SettingsPage.h"

#include <algorithm>
#include <cstdio>

namespace {

void stderrMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    const char *tag = "[info ]";
    if (type == QtWarningMsg) tag = "[warn ]";
    else if (type == QtCriticalMsg || type == QtFatalMsg) tag = "[crit ]";
    fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    fflush(stderr);
}

QString widgetText(QWidget *w)
{
    if (!w) return QString();
    QString t = w->property("text").toString();
    if (t.isEmpty()) t = w->property("placeholderText").toString();
    if (t.isEmpty()) {
        if (auto *le = qobject_cast<QLineEdit*>(w)) t = le->placeholderText();
    }
    if (t.size() > 40) t = t.left(40) + QStringLiteral("…");
    return t;
}

QScrollArea *settingsScrollArea(QWidget *root)
{
    QScrollArea *result = nullptr;
    for (QScrollArea *sa : root->findChildren<QScrollArea *>()) {
        if (qobject_cast<SettingsPage *>(sa->widget())) { result = sa; break; }
    }
    return result;
}

void dumpScene(QWidget *w, SettingsPage *sp, const char *tag)
{
    QScrollArea *scroll = settingsScrollArea(sp->window());
    if (!scroll) { qInfo() << "PROBE-FAIL: settings QScrollArea not found"; return; }
    QWidget *pageWidget = scroll->widget();
    QStackedWidget *stack = sp->findChild<QStackedWidget *>(QStringLiteral("settingsContentStack"));

    qInfo().noquote() << QString("== [%1] window=%2x%3  viewport=%4x%5  pageWidget=%6x%7  hbar max=%8 visible=%9  vbar max=%10")
                         .arg(QString::fromLatin1(tag))
                         .arg(w->width()).arg(w->height())
                         .arg(scroll->viewport()->width()).arg(scroll->viewport()->height())
                         .arg(pageWidget->width()).arg(pageWidget->height())
                         .arg(scroll->horizontalScrollBar()->maximum())
                         .arg(scroll->horizontalScrollBar()->isVisible() ? "yes" : "no")
                         .arg(scroll->verticalScrollBar()->maximum());
    qInfo() << "   settingsPage minSizeHint:" << sp->minimumSizeHint()
            << " size:" << sp->size();
    qInfo() << "   stack minSizeHint:" << (stack ? stack->minimumSizeHint() : QSize())
            << " current:" << (stack ? stack->currentIndex() : -1);

    if (!stack) return;

    struct RowInfo { int width; QString page; QWidget *row; };
    QList<RowInfo> allRows;

    for (int i = 0; i < stack->count(); ++i) {
        QWidget *pg = stack->widget(i);
        if (!pg) continue;
        const int mw = pg->minimumSizeHint().width();
        qInfo().noquote() << QString("[page %1] %2  msh-w=%3  objectName=%4")
                             .arg(i).arg(pg->metaObject()->className()).arg(mw)
                             .arg(pg->objectName());
        for (QFrame *f : pg->findChildren<QFrame *>()) {
            if (f->objectName() == QLatin1String("settingRow"))
                allRows.append({ f->minimumSizeHint().width(), pg->objectName(), f });
        }
    }

    std::sort(allRows.begin(), allRows.end(),
              [](const RowInfo &a, const RowInfo &b) { return a.width > b.width; });

    qInfo() << "== TOP 10 widest settingRow rows ==";
    for (int i = 0; i < qMin(10, allRows.size()); ++i) {
        const RowInfo &ri = allRows[i];
        qInfo().noquote() << QString("[row #%1] %2px  page=%3")
                             .arg(i).arg(ri.width).arg(ri.page);

        QList<QWidget *> kids = ri.row->findChildren<QWidget *>();
        struct KidInfo { int w; QWidget *obj; };
        QList<KidInfo> kids2;
        for (QWidget *k : kids) {
            const int kw = qMax(k->minimumSizeHint().width(), k->minimumWidth());
            if (kw <= 0) continue;
            kids2.append({ kw, k });
        }
        std::sort(kids2.begin(), kids2.end(),
                  [](const KidInfo &a, const KidInfo &b) { return a.w > b.w; });
        for (int j = 0; j < qMin(5, kids2.size()); ++j) {
            QWidget *k = kids2[j].obj;
            qInfo().noquote() << QString("    child %1px  %2  objectName=%3  text=%4")
                                 .arg(kids2[j].w)
                                 .arg(k->metaObject()->className())
                                 .arg(k->objectName())
                                 .arg(widgetText(k));
        }
    }
}

void shootPages(MainWindow *w, SettingsPage *sp)
{
    QStackedWidget *stack = sp->findChild<QStackedWidget *>(QStringLiteral("settingsContentStack"));
    if (!stack) return;
    for (int i = 0; i < stack->count(); ++i) {
        QWidget *pg = stack->widget(i);
        if (!pg) continue;
        stack->setCurrentIndex(i);
        QApplication::processEvents();
        const QString name = QStringLiteral("settings_1200_page%1_%2.png")
                                 .arg(i).arg(pg->objectName());
        const bool ok = w->grab().save(name);
        qInfo().noquote() << "[shot]" << name << (ok ? "ok" : "FAILED");
    }
    stack->setCurrentIndex(0);
    QApplication::processEvents();
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    MainWindow w;
    w.resize(1200, 800);   // 主窗口最小尺寸
    w.show();
    QApplication::processEvents();

    // 1.2s 后走真实导航：设置页会在 onParentNavClicked(2) 内按需创建
    QTimer::singleShot(1200, &app, [&app, &w]() {
        QMetaObject::invokeMethod(&w, "onParentNavClicked", Qt::DirectConnection,
                                  Q_ARG(int, 2));
        QApplication::processEvents();
    });

    // 3.2s 后测量（等子导航气泡 / 页面动画稳定）
    QTimer::singleShot(3200, &app, [&app, &w]() {
        SettingsPage *settingsPage = w.findChild<SettingsPage *>();
        if (!settingsPage) {
            qCritical() << "PROBE-FAIL: SettingsPage not found";
            app.exit(1);
            return;
        }

        dumpScene(&w, settingsPage, "1200x800");

        // 截图：逐个设置子页（含悬浮子导航气泡），便于人工确认排版
        shootPages(&w, settingsPage);

        // 放大窗口对比：若最小宽度依赖，放大后应自动铺满
        w.resize(1600, 900);
        QApplication::processEvents();
        QTimer::singleShot(600, &app, [&app, &w, settingsPage]() {
            dumpScene(&w, settingsPage, "1600x900");
            qInfo() << "PROBE-DONE";
            app.quit();
        });
    });

    return app.exec();
}
