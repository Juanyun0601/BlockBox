/**
 * @file   install_page_probe.cpp
 * @brief  安装新实例页视觉探针：渲染真实 InstallInstancePage + 真实 style.qss
 *
 * 流程：进入页面 → 拉取官方版本列表 → 瀑布流卡片 + minecraft.wiki 封面回填
 *       → 截图后切回列表视图再截一张。输出：
 *   D:/BlockBox/_out/install_page_masonry.png
 *   D:/BlockBox/_out/install_page_list.png
 */
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QRegularExpression>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "components/ContentViewSwitch.h"
#include "pages/InstallInstancePage.h"

namespace {

// 与 ThemeManager::buildTokenTable 同族的浅色 token 表（探针用精简版）
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
        {"@PRIMARY_RGBA_20@", "rgba(46, 125, 50, 0.2)"},
        {"@PRIMARY_RGBA_38@", "rgba(46, 125, 50, 0.38)"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@SUCCESS@", "#10B981"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#D32F2F"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    // 兜底：剩余 token 置灰，避免 QSS 解析告警
    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

int countMasonryCards(QWidget *page)
{
    int n = 0;
    const auto cards = page->findChildren<QWidget *>();
    for (QWidget *w : cards) {
        if (w->property("versionId").isValid())
            ++n;
    }
    return n;
}

void saveShot(QWidget &w, const char *tag, const QString &path)
{
    const bool ok = w.grab().save(path);
    std::fprintf(stderr, "[%s] %s %s\n", ok ? "ok" : "FAIL", tag, qUtf8Printable(path));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("install_page_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(resolveTokens(raw));

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    // 版本类型图标是否随 qrc 打进二进制
    const QStringList iconPaths = {
        QStringLiteral(":/Images/Block/Grass_Block.png"),
        QStringLiteral(":/Images/Block/Command_Block.png"),
        QStringLiteral(":/Images/Block/Egg_enchanted.png"),
        QStringLiteral(":/Images/Block/Stone.png"),
    };
    for (const QString &p : iconPaths)
        std::fprintf(stderr, "[icon] %s %s\n", qUtf8Printable(p),
                     QPixmap(p).isNull() ? "MISSING" : "ok");

    // 上一轮探针会把视图偏好存成列表，这里固定回瀑布流
    {
        QSettings settings;
        settings.remove(QStringLiteral("bb_view_install_instance"));
        settings.sync();
    }

    QWidget win;
    win.setWindowTitle(QStringLiteral("install page probe"));
    win.resize(1240, 820);
    auto *layout = new QVBoxLayout(&win);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *page = new InstallInstancePage(&win);
    layout->addWidget(page);

    win.show();
    app.processEvents();

    // 状态机：等版本列表 → 等封面与图片下载 → 截瀑布流 → 切列表 → 截列表
    auto *timer = new QTimer(&app);
    int tick = 0;
    int loadedTick = -1;
    bool shotEarly = false;
    bool shotMasonry = false;
    bool shotList = false;
    QObject::connect(timer, &QTimer::timeout, [&]() {
        ++tick;
        auto *list = page->findChild<QListWidget *>(QStringLiteral("versionListWidget"));
        const int count = list ? list->count() : 0;

        if (loadedTick < 0 && count > 0) {
            loadedTick = tick;
            std::fprintf(stderr, "[load] version list count=%d\n", count);
        }

        // 列表加载 1.5s 后截一张：热缓存时封面应已就位（冷缓存则是渐变占位）
        if (loadedTick > 0 && !shotEarly && tick >= loadedTick + 5) {
            saveShot(win, "early", QStringLiteral("D:/BlockBox/_out/install_page_early.png"));
            shotEarly = true;
        }

        // 列表加载后给 wiki 封面请求 + 图片下载留足时间
        if (loadedTick > 0 && !shotMasonry && tick >= loadedTick + 100) {
            const int cards = countMasonryCards(page);
            const int banners = page->findChildren<QLabel *>(
                                    QStringLiteral("contentCardBanner")).count();
            std::fprintf(stderr, "[masonry] cards=%d banners=%d\n", cards, banners);
            saveShot(win, "masonry", QStringLiteral("D:/BlockBox/_out/install_page_masonry.png"));
            shotMasonry = true;

            const auto switches = page->findChildren<ContentViewSwitch *>();
            std::fprintf(stderr, "[switch] found=%d\n", int(switches.size()));
            for (ContentViewSwitch *sw : switches) {
                sw->setViewMode(ContentViewSwitch::List);
                std::fprintf(stderr, "[switch] mode=%d\n", int(sw->viewMode()));
            }
            for (QStackedWidget *st : page->findChildren<QStackedWidget *>())
                std::fprintf(stderr, "[switch] stackIndex=%d\n", st->currentIndex());
            app.processEvents();
        }

        if (shotMasonry && !shotList && tick >= loadedTick + 110) {
            saveShot(win, "list", QStringLiteral("D:/BlockBox/_out/install_page_list.png"));
            shotList = true;
        }

        // 列表拍完后依次切快照 / 愚人节版 / 远古版筛选，各停留 6 tick 拍照
        if (shotList) {
            const int t = tick - loadedTick;
            const auto combos = page->findChildren<QComboBox *>();
            QComboBox *filterCombo = combos.size() >= 2 ? combos[1] : nullptr;
            if (t == 118 && filterCombo) {
                filterCombo->setCurrentIndex(1);
            } else if (t == 124 && filterCombo) {
                saveShot(win, "snapshot", QStringLiteral("D:/BlockBox/_out/install_page_snapshot.png"));
                filterCombo->setCurrentIndex(2);
            } else if (t == 130 && filterCombo) {
                saveShot(win, "april", QStringLiteral("D:/BlockBox/_out/install_page_april.png"));
                filterCombo->setCurrentIndex(3);
            } else if (t == 136) {
                saveShot(win, "old", QStringLiteral("D:/BlockBox/_out/install_page_old.png"));
                app.quit();
                return;
            }
        }

        if (tick >= 500) {   // 150s 兜底
            std::fprintf(stderr, "[timeout] loaded=%d count=%d\n", loadedTick > 0, count);
            saveShot(win, "timeout", QStringLiteral("D:/BlockBox/_out/install_page_timeout.png"));
            app.quit();
        }
    });
    timer->start(300);

    return app.exec();
}
