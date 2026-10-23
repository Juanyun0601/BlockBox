/**
 * 探针：以真实对话框启动投影方块编辑器，抓截图 + 实测加载/重建/拾取耗时 + 撤销/重做功能验证。
 * 用法：probe_pbed_shot.exe <litematic> <输出目录>
 */

#include "components/ProjectionBlockEditorDialog.h"
#include "components/VoxelViewWidget.h"
#include "utils/Schematic/SchematicDocument.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <cstdio>
#include <cstdlib>

namespace
{
QElapsedTimer g_sinceStart;
QString g_outDir;
QString g_file;

void savePng(const QImage &img, const QString &path)
{
    if (img.isNull())
    {
        std::printf("[shot] NULL image -> %s\n", qPrintable(path));
        return;
    }
    const bool ok = img.save(path, "PNG");
    std::printf("[shot] %s %dx%d %s\n", ok ? "OK" : "FAIL", img.width(), img.height(),
                qPrintable(path));
}

int cellAt(VoxelViewWidget *v, int x, int y, int z)
{
    const SchematicDocument *doc = v->document();
    if (!doc)
        return -99;
    return doc->paletteIndexAt(y * doc->layerSize() + z * doc->sizeX() + x);
}

void printChips(QWidget *w, const char *tag)
{
    QStringList parts;
    const QList<QLabel *> chips = w->findChildren<QLabel *>(QStringLiteral("projChip"));
    for (QLabel *l : chips)
    {
        QString txt = l->text();
        for (int i = 0; i < txt.size(); ++i)
            if (txt.at(i).unicode() > 127)
                txt[i] = QLatin1Char('.');
        parts << txt;
    }
    if (auto *stats = w->findChild<QLabel *>(QStringLiteral("projStatsLabel")))
        parts << QStringLiteral("[stats] ") + stats->text();
    std::printf("[chips] %-8s %s\n", tag, qPrintable(parts.join(QStringLiteral(" | "))));
}

bool hasUndoStep(QWidget *w)
{
    const QList<QPushButton *> btns = w->findChildren<QPushButton *>();
    for (QPushButton *b : btns)
        if (b->toolTip().startsWith(QStringLiteral("撤销：")))
            return b->isEnabled();
    return false;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    g_file = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QString();
    g_outDir = (argc > 2) ? QString::fromLocal8Bit(argv[2]) : QStringLiteral(".");
    QDir().mkpath(g_outDir);

    g_sinceStart.start();

    QTimer::singleShot(1800, &app, [&]() {
        QWidget *w = QApplication::activeModalWidget();
        if (!w)
        {
            std::printf("[shot] no active modal widget\n");
            QApplication::quit();
            return;
        }
        std::printf("[perf] 打开到首帧就绪 %lld ms\n", static_cast<long long>(g_sinceStart.elapsed()));
        std::printf("[shot] dialog %dx%d\n", w->width(), w->height());

        savePng(w->grab().toImage(), g_outDir + QStringLiteral("/dialog.png"));

        if (auto *side = w->findChild<QWidget *>(QStringLiteral("projSideCard")))
        {
            const QSize smh = side->minimumSizeHint();
            std::printf("[size] side w=%d minHint=%d hint=%d\n", side->width(), smh.width(),
                        side->sizeHint().width());
        }

        auto *view = w->findChild<VoxelViewWidget *>();
        if (!view)
        {
            std::printf("[shot] VoxelViewWidget not found\n");
            w->close();
            QTimer::singleShot(100, &app, []() { QApplication::quit(); });
            return;
        }

        std::printf("[perf] 视口 %dx%d 四边形=%d\n", view->width(), view->height(), view->quadCount());
        savePng(view->grabFramebuffer(), g_outDir + QStringLiteral("/viewport.png"));

        // ---- 1) 几何重建耗时（含剔除/AO/填充缓冲）----
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 5; ++i)
            view->rebuild();
        const qint64 rebuildMs = t.elapsed();
        std::printf("[perf] rebuild 平均 %.2f ms（5 次）四边形=%d\n",
                    double(rebuildMs) / 5.0, view->quadCount());
        savePng(view->grabFramebuffer(), g_outDir + QStringLiteral("/viewport_rebuild.png"));
        view->setDocument(view->document());
        std::printf("[dbg] setDocument 后 quads=%d\n", view->quadCount());
        savePng(view->grabFramebuffer(), g_outDir + QStringLiteral("/viewport_reset.png"));

        // ---- 2) 拾取（DDA）耗时：直接在视口内随机取点 ----
        t.restart();
        int hits = 0;
        const int picks = 300;
        for (int i = 0; i < picks; ++i)
        {
            const QPoint p((i * 37) % qMax(1, view->width()), (i * 53) % qMax(1, view->height()));
            int hx = 0, hy = 0, hz = 0;
            if (view->rayPick(p, hx, hy, hz))
                ++hits;
        }
        const qint64 pickUs = t.nsecsElapsed() / 1000;
        std::printf("[perf] rayPick 平均 %.1f us（%d 次，命中 %d）\n",
                    double(pickUs) / picks, picks, hits);

        // ---- 3) 悬停更新耗时：模拟鼠标划过多格 ----
        std::printf("[dbg] 悬停前 quads=%d\n", view->quadCount());
        t.restart();
        const int moves = 120;
        for (int i = 0; i < moves; ++i)
        {
            const QPointF local(qreal((i * 61) % qMax(1, view->width())),
                                qreal((i * 71) % qMax(1, view->height())));
            QMouseEvent ev(QEvent::MouseMove, local, view->mapToGlobal(local.toPoint()),
                           Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(view, &ev);
        }
        std::printf("[perf] 悬停跟随平均 %.2f ms/次（%d 次）\n",
                    double(t.nsecsElapsed() / 1000000.0) / moves, moves);
        std::printf("[dbg] 悬停后 quads=%d\n", view->quadCount());

        savePng(view->grabFramebuffer(), g_outDir + QStringLiteral("/viewport_hover.png"));

        if (auto *list = w->findChild<QListWidget *>(QStringLiteral("projPaletteList")))
            std::printf("[shot] palette items = %d\n", list->count());

        // ---- 4) 撤销 / 重做功能验证 ----
        printChips(w, "初始");
        const QList<int> snapshotBefore = view->document()->indices();
        const int layerSize = view->document()->layerSize();
        const int sizeX = view->document()->sizeX();
        const int sizeY = view->document()->sizeY();
        const int sizeZ = view->document()->sizeZ();
        if (auto *combo = w->findChild<QComboBox *>(QStringLiteral("projToolCombo")))
            combo->setCurrentIndex(1); // 放置工具
        std::printf("[dbg] 切工具后 quads=%d\n", view->quadCount());

        const bool okPlace = QMetaObject::invokeMethod(view, "placeRequested", Qt::DirectConnection,
                                  Q_ARG(int, 24), Q_ARG(int, 21), Q_ARG(int, 24));
        std::printf("[undo] place invoke=%d quads=%d\n", int(okPlace), view->quadCount());
        printChips(w, "放置后");

        QMetaObject::invokeMethod(w, "onUndo", Qt::DirectConnection);
        std::printf("[undo] 撤销后 quads=%d  A=%d B=%d C=%d\n", view->quadCount(), cellAt(view, 24, 21, 24),
                    cellAt(view, 26, 21, 26), cellAt(view, 28, 21, 28));
        printChips(w, "撤销后");

        QMetaObject::invokeMethod(w, "onRedo", Qt::DirectConnection);
        std::printf("[undo] 重做后 quads=%d  A=%d\n", view->quadCount(), cellAt(view, 24, 21, 24));
        printChips(w, "重做后");

        // 再放置两个方块后连续撤销三次
        QMetaObject::invokeMethod(view, "placeRequested", Qt::DirectConnection,
                                  Q_ARG(int, 26), Q_ARG(int, 21), Q_ARG(int, 26));
        std::printf("[undo] 放置B quads=%d A=%d B=%d\n", view->quadCount(), cellAt(view, 24, 21, 24),
                    cellAt(view, 26, 21, 26));
        QMetaObject::invokeMethod(view, "placeRequested", Qt::DirectConnection,
                                  Q_ARG(int, 28), Q_ARG(int, 21), Q_ARG(int, 28));
        std::printf("[undo] 放置C quads=%d B=%d C=%d\n", view->quadCount(), cellAt(view, 26, 21, 26),
                    cellAt(view, 28, 21, 28));
        QMetaObject::invokeMethod(w, "onUndo", Qt::DirectConnection);
        std::printf("[undo] 撤销1 quads=%d C=%d\n", view->quadCount(), cellAt(view, 28, 21, 28));
        QMetaObject::invokeMethod(w, "onUndo", Qt::DirectConnection);
        std::printf("[undo] 撤销2 quads=%d B=%d\n", view->quadCount(), cellAt(view, 26, 21, 26));
        QMetaObject::invokeMethod(w, "onUndo", Qt::DirectConnection);
        std::printf("[undo] 撤销3 quads=%d A=%d 初始=10592\n", view->quadCount(), cellAt(view, 24, 21, 24));
        view->setDocument(view->document());
        std::printf("[undo] 撤销3+setDocument quads=%d\n", view->quadCount());
        view->rebuild();
        std::printf("[undo] 撤销3+rebuild quads=%d\n", view->quadCount());
        std::printf("[undo] 连续三次撤销后 可撤销=%d\n", int(hasUndoStep(w)));
        printChips(w, "三次撤销");

        savePng(w->grab().toImage(), g_outDir + QStringLiteral("/dialog_after_undo.png"));

        {
            const QList<int> after = view->document()->indices();
            int diffs = 0;
            for (int i = 0; i < after.size() && diffs < 8; ++i)
            {
                if (after.at(i) != snapshotBefore.at(i))
                {
                    const int y = i / layerSize;
                    const int rem = i % layerSize;
                    const int z = rem / sizeX;
                    const int x = rem % sizeX;
                    std::printf("[diff] cell=%d (x=%d,y=%d,z=%d) %d -> %d\n", i, x, y, z,
                                snapshotBefore.at(i), after.at(i));
                    ++diffs;
                }
            }
            std::printf("[diff] total reported=%d of %d cells (vol %dx%dx%d)\n", diffs,
                        after.size(), sizeX, sizeY, sizeZ);
        }
        view->setHover(-1, -1, -1);
        savePng(view->grabFramebuffer(), g_outDir + QStringLiteral("/viewport_final.png"));
        std::printf("[undo] done\n");
        std::fflush(stdout);
        std::_Exit(0);
    });

    ProjectionBlockEditorDialog::editProjection(nullptr, g_file, QString());
    return 0;
}














