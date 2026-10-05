/**
 * @file   settings_width_probe.cpp
 * @brief  设置页宽度回归探针：逐子页打印 minimumSizeHint 宽度，定位横向滚动来源
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），构建真实 MainWindow 与设置页，
 * 对 QStackedWidget 内每个子页测量最小宽度；并对最宽的 settingRow 行
 * 打印其内部最宽子控件，精确定位撑宽元凶。
 *
 * 结论判定：任一子页 minimumSizeHint().width() 超过阈值（视口可用宽度）
 * 时，外层 QScrollArea 会出现水平滚动条，设置页主栏表现为"固定宽度"。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
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

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    MainWindow w;
    w.resize(1280, 800);
    w.show();
    QApplication::processEvents();

    QTimer::singleShot(1800, &app, [&app, &w]() {
        SettingsPage *settingsPage = w.findChild<SettingsPage *>();
        if (!settingsPage) {
            qCritical() << "PROBE-FAIL: SettingsPage not found";
            app.exit(1);
            return;
        }

        // SettingsPage -> QScrollArea（parent 链：SettingsPage 的父是滚动区 viewport，
        // viewport 的父是 QScrollArea 本身）
        QScrollArea *scroll = nullptr;
        QWidget *p = settingsPage->parentWidget();
        while (p) {
            if (auto *sa = qobject_cast<QScrollArea *>(p)) { scroll = sa; break; }
            p = p->parentWidget();
        }
        if (!scroll) {
            qCritical() << "PROBE-FAIL: settings QScrollArea not found";
            app.exit(1);
            return;
        }

        QWidget *pageWidget = scroll->widget();
        qInfo() << "== outer settings scroll widget msh:" << pageWidget->minimumSizeHint();
        qInfo() << "== viewport size:" << scroll->viewport()->size();
        qInfo() << "== hbar maximum:" << scroll->horizontalScrollBar()->maximum();

        // 十个子页逐一测最小宽度
        const QStringList pageNames = {
            "settingsGeneralContent", "settingsInterfaceContent", "settingsGameContent",
            "settingsBedrockGameContent", "settingsInstanceContent", "settingsJavaContent",
            "settingsAdvancedContent", "settingsKeyBindContent", "settingsSystemInfoContent",
            "settingsAiAssistantContent"
        };

        struct RowInfo { int width; QString page; QWidget *row; };
        QList<RowInfo> allRows;

        for (const QString &name : pageNames) {
            QWidget *pg = pageWidget->findChild<QWidget *>(name);
            if (!pg) continue;
            const int mw = pg->minimumSizeHint().width();
            qInfo() << "[page]" << name << "msh-w =" << mw;
            for (QFrame *f : pg->findChildren<QFrame *>()) {
                if (f->objectName() == QLatin1String("settingRow")) {
                    allRows.append({ f->minimumSizeHint().width(), name, f });
                }
            }
        }

        std::sort(allRows.begin(), allRows.end(),
                  [](const RowInfo &a, const RowInfo &b) { return a.width > b.width; });

        qInfo() << "== TOP 12 widest settingRow rows ==";
        for (int i = 0; i < qMin(12, allRows.size()); ++i) {
            const RowInfo &ri = allRows[i];
            qInfo() << QString("[row #%1] %2px  page=%3")
                       .arg(i).arg(ri.width).arg(ri.page);

            // 行内最宽的 5 个直接子控件
            QList<QWidget *> kids = ri.row->findChildren<QWidget *>();
            struct KidInfo { int w; QWidget *obj; };
            QList<KidInfo> kids2;
            for (QWidget *k : kids) {
                if (k->minimumSizeHint().width() <= 0 && k->minimumWidth() <= 0) continue;
                kids2.append({ qMax(k->minimumSizeHint().width(), k->minimumWidth()), k });
            }
            std::sort(kids2.begin(), kids2.end(),
                      [](const KidInfo &a, const KidInfo &b) { return a.w > b.w; });
            for (int j = 0; j < qMin(5, kids2.size()); ++j) {
                QWidget *k = kids2[j].obj;
                qInfo() << QString("    child %1px  %2  objectName=%3  text=%4")
                           .arg(kids2[j].w)
                           .arg(k->metaObject()->className())
                           .arg(k->objectName())
                           .arg(widgetText(k));
            }
        }

        // 钻取：界面设置页 700px 的构成——先看顶层卡片，再钻最宽卡片内部
        QWidget *iface = pageWidget->findChild<QWidget *>("settingsInterfaceContent");
        if (iface) {
            qInfo() << "== interface page drill-down (total msh-w ="
                    << iface->minimumSizeHint().width() << ") ==";
            QVBoxLayout *lay = qobject_cast<QVBoxLayout *>(iface->layout());
            if (lay) {
                for (int i = 0; i < lay->count(); ++i) {
                    QLayoutItem *it = lay->itemAt(i);
                    QWidget *w2 = it->widget();
                    if (!w2) continue;
                    const int iw = w2->minimumSizeHint().width();
                    if (iw < 300) continue;  // 只看可能撑宽页面的卡片
                    qInfo() << QString("  [card %1] %2px  %3  objectName=%4")
                               .arg(i).arg(iw)
                               .arg(w2->metaObject()->className())
                               .arg(w2->objectName());
                    // 打印卡片自身布局的每个 item 的 minimumSize
                    QLayout *cardLay = w2->layout();
                    if (cardLay) {
                        for (int j = 0; j < cardLay->count(); ++j) {
                            QLayoutItem *cit = cardLay->itemAt(j);
                            qInfo() << QString("      item %1  min=%2  widget=%3")
                                       .arg(j).arg(cit->minimumSize().width())
                                       .arg(cit->widget() ? cit->widget()->objectName()
                                                          : QString("(layout)"));
                            if (cit->widget()) {
                                QLayout *inner = cit->widget()->layout();
                                if (inner) {
                                    for (int k = 0; k < inner->count(); ++k) {
                                        QLayoutItem *iit = inner->itemAt(k);
                                        qInfo() << QString("          inner %1  min=%2  widget=%3 (%4)")
                                                   .arg(k).arg(iit->minimumSize().width())
                                                   .arg(iit->widget() ? iit->widget()->metaObject()->className()
                                                                      : QString("(layout)"))
                                                   .arg(iit->widget() ? iit->widget()->objectName()
                                                                      : QString());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        qInfo() << "PROBE-DONE";
        app.quit();
    });

    return app.exec();
}
