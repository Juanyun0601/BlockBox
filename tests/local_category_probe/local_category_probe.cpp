/**
 * @file   local_category_probe.cpp
 * @brief  本地资源「管理分类」对话框视觉探针：
 *         加载真实 style.qss（token 解析）+ 真实 LocalCategoryDialog /
 *         LocalCategoryManager，渲染空列表 / 有分类 / 选中态并截图。
 *
 * 输出：D:/BlockBox/_out/local_category_probe_*.png
 */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "components/LocalCategoryDialog.h"
#include "utils/LocalCategoryManager.h"

#include <cstdio>

namespace {

// 与 ThemeManager::buildTokenTable 同步的浅色 token 表（探针用精简版）
QString resolveTokens(QString style)
{
    struct KV { const char *k; const char *v; };
    const QVector<KV> t = {
        {"@BG_BASE@", "#f5f5f5"}, {"@BG_CARD@", "#ffffff"},
        {"@BG_CONTENT@", "#fafafa"}, {"@BG_MUTED@", "#f8f9fa"},
        {"@BG_HOVER@", "#eaeaea"}, {"@BG_PRESSED@", "#e0e0e0"},
        {"@BG_TRACK@", "#e8e8e8"}, {"@BG_TAB@", "#f0f0f0"},
        {"@BG_DISABLED@", "#d5d7db"}, {"@BG_MSG@", "#edf0f4"},
        {"@BG_GLASS@", "rgba(255, 255, 255, 0.72)"},
        {"@BG_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.42)"},
        {"@BORDER_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.55)"},
        {"@BORDER@", "#e8eaed"}, {"@BORDER_STRONG@", "#dadce0"},
        {"@BORDER_HOVER@", "#bdbdbd"}, {"@BORDER_DASHED@", "#c4c7c5"},
        {"@BORDER_DISABLED@", "#b0b0b0"}, {"@BORDER_LIGHT@", "#f0f1f3"},
        {"@TEXT_SECONDARY@", "#666666"}, {"@TEXT_TERTIARY@", "#888888"},
        {"@TEXT_DISABLED@", "#bbbbbb"}, {"@TEXT_PRIMARY@", "#333333"},
        {"@TEXT_BORDER@", "#e8e8e8"},
        {"@PRIMARY_BG@", "#e8f5e9"}, {"@TEXT_ACCENT@", "#1b5e20"},
        {"@BLACK_RGBA_0.06@", "rgba(0, 0, 0, 0.06)"},
        {"@BLACK_RGBA_0.08@", "rgba(0, 0, 0, 0.08)"},
        {"@BLACK_RGBA_0.10@", "rgba(0, 0, 0, 0.10)"},
        {"@BLACK_RGBA_0.13@", "rgba(0, 0, 0, 0.13)"},
        {"@BLACK_RGBA_0.25@", "rgba(0, 0, 0, 0.25)"},
        {"@PRIMARY@", "#2E7D32"},
        {"@PRIMARY_HOVER@", "#256428"},
        {"@PRIMARY_PRESSED@", "#1B5E20"},
        {"@PRIMARY_DARK@", "#1B5E20"},
        {"@PRIMARY_RGBA_12@", "rgba(46, 125, 50, 0.12)"},
        {"@PRIMARY_RGBA_18@", "rgba(46, 125, 50, 0.18)"},
        {"@PRIMARY_RGBA_20@", "rgba(46, 125, 50, 0.2)"},
        {"@PRIMARY_RGBA_38@", "rgba(46, 125, 50, 0.38)"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@SUCCESS@", "#10B981"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#f44336"},
        {"@DANGER_HOVER@", "#d32f2f"}, {"@DANGER_PRESSED@", "#b71c1c"},
        {"@DANGER_BG@", "#ffebee"}, {"@DANGER_BORDER@", "#ffcdd2"},
        {"@DANGER_TEXT@", "#c62828"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

void pump(QApplication &app, int n = 8)
{
    for (int i = 0; i < n; ++i)
        app.processEvents(QEventLoop::AllEvents, 30);
}

QString outPath(const QString &name)
{
    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));
    return QStringLiteral("D:/BlockBox/_out/") + name;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("local_category_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly))
    {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(resolveTokens(raw));

    // 临时实例目录：避免污染真实用户数据
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::fprintf(stderr, "temp dir failed\n");
        return 2;
    }

    LocalCategoryManager mgr;
    mgr.setInstancePath(tmp.path());
    mgr.setResourceType(QStringLiteral("mods"));

    // ── 场景1：空列表 ──────────────────────────────────────────────
    {
        LocalCategoryDialog dlg(&mgr);
        dlg.show();
        pump(app);
        if (QWidget *fw = QApplication::focusWidget())
            fw->clearFocus();
        pump(app);
        const bool ok = dlg.grab().save(outPath(QStringLiteral("local_category_probe_empty.png")));
        std::fprintf(stderr, "[shot] empty: %s\n", ok ? "ok" : "FAIL");
        std::fprintf(stderr, "[size] empty dialog: %dx%d\n", dlg.width(), dlg.height());
        dlg.close();
    }

    // ── 场景2：有分类（含 0 项与多项）+ 选中第一项 ──────────────────
    {
        const QString idA = mgr.addCategory(QStringLiteral("性能优化"));
        const QString idB = mgr.addCategory(QStringLiteral("建筑装饰"));
        const QString idC = mgr.addCategory(QStringLiteral("辅助工具"));
        mgr.assignCategory(QStringLiteral("sodium.jar"), idA);
        mgr.assignCategory(QStringLiteral("lithium.jar"), idA);
        mgr.assignCategory(QStringLiteral("iris.jar"), idA);
        mgr.assignCategory(QStringLiteral("jei.jar"), idA);
        mgr.assignCategory(QStringLiteral("chisel.jar"), idB);

        LocalCategoryDialog dlg(&mgr);
        dlg.show();
        pump(app);

        if (QListWidget *list = dlg.findChild<QListWidget *>())
        {
            if (list->count() > 0)
            {
                list->setCurrentRow(0);
                pump(app);
            }
        }
        if (QWidget *fw = QApplication::focusWidget())
            fw->clearFocus();
        pump(app);

        const bool ok = dlg.grab().save(outPath(QStringLiteral("local_category_probe_filled.png")));
        std::fprintf(stderr, "[shot] filled+selected: %s\n", ok ? "ok" : "FAIL");
        std::fprintf(stderr, "[size] filled dialog: %dx%d\n", dlg.width(), dlg.height());

        if (QListWidget *list = dlg.findChild<QListWidget *>())
        {
            const QListWidgetItem *it = list->currentItem();
            std::fprintf(stderr, "[info] selected item text: %s\n",
                         it ? qUtf8Printable(it->text()) : "(none)");
            for (QPushButton *btn : dlg.findChildren<QPushButton *>())
            {
                std::fprintf(stderr, "[btn ] \"%s\" objectName=%s enabled=%d size=%dx%d\n",
                             qUtf8Printable(btn->text()),
                             qUtf8Printable(btn->objectName()),
                             int(btn->isEnabled()), btn->width(), btn->height());
            }
        }
        dlg.close();
    }

    std::fprintf(stderr, "\n======== local_category_probe done ========\n");
    return 0;
}
