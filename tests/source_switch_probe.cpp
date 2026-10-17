/**
 * @file   source_switch_probe.cpp
 * @brief  SourceSwitchBar（跨源合并卡片详情页来源切换）视觉探针
 *
 * 加载真实 style.qss（token 解析后），渲染真实 SourceSwitchBar：
 *  - 两个文本段（Modrinth / CurseForge）均可见且无裁切
 *  - setActiveSource("modrinth") 后 Modrinth 段为主色药丸、CurseForge 段透出轨道
 *  - 点击 CurseForge 段发出 sourceSelected("curseforge")；点击当前激活段不发信号
 *  - 切换激活段后药丸位置随选中段移动
 *  - 捕获 "Could not parse" 样式表解析告警
 * 截图输出：D:/BlockBox/_out/source_switch_probe_{modrinth,curseforge}.png
 */
#include <QApplication>
#include <QFile>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "components/SourceSwitchBar.h"

namespace {

int g_parseWarnings = 0;
int g_fail = 0;

void msgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains(QLatin1String("Could not parse")))
        ++g_parseWarnings;
    std::fprintf(stderr, "[qt ] %s\n", msg.toLocal8Bit().constData());
}

void expect(bool ok, const QString &what)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", qUtf8Printable(what));
    if (!ok)
        ++g_fail;
}

// 与 ThemeManager::buildTokenTable 同族的 token 表（探针用精简版，仿 view_switch_probe）
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
        {"@PRIMARY_RGBA_20@", "rgba(46, 125, 50, 0.2)"},
        {"@PRIMARY_RGBA_38@", "rgba(46, 125, 50, 0.38)"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@SUCCESS@", "#10B981"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#D32F2F"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    // 兜底：剩余 token 置灰，避免 QSS 解析告警
    static const QRegularExpression re(QStringLiteral("@([A-Za-z0-9_.\\-]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

QColor sampleAt(const QImage &img, int x, int y)
{
    const qreal dpr = img.devicePixelRatio();
    return img.pixelColor(qRound(x * dpr), qRound(y * dpr));
}

bool near(const QColor &a, const QColor &b, int tol = 8)
{
    return qAbs(a.red() - b.red()) <= tol
        && qAbs(a.green() - b.green()) <= tol
        && qAbs(a.blue() - b.blue()) <= tol;
}

} // namespace

int main(int argc, char **argv)
{
    qInstallMessageHandler(msgHandler);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("source_switch_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();

    qApp->setStyleSheet(resolveTokens(raw)
                        + QStringLiteral("\nQWidget#probeCard { background-color: #ffffff; }"));

    QWidget card;
    card.setObjectName(QStringLiteral("probeCard"));
    QVBoxLayout lay(&card);
    lay.setContentsMargins(16, 16, 16, 16);
    SourceSwitchBar *bar = new SourceSwitchBar(&card);
    bar->setActiveSource(QStringLiteral("modrinth"));
    lay.addWidget(bar, 0, Qt::AlignLeft);
    card.resize(320, 96);
    card.show();
    app.processEvents();

    QPushButton *mrBtn = nullptr;
    QPushButton *cfBtn = nullptr;
    for (QPushButton *b : bar->findChildren<QPushButton *>()) {
        if (b->text() == QLatin1String("Modrinth"))
            mrBtn = b;
        if (b->text() == QLatin1String("CurseForge"))
            cfBtn = b;
    }
    expect(mrBtn && cfBtn, QStringLiteral("找到 Modrinth / CurseForge 两个分段按钮"));
    if (!mrBtn || !cfBtn)
        return 1;

    expect(mrBtn->property("active").toBool() && !cfBtn->property("active").toBool(),
           QStringLiteral("初始激活段 = Modrinth"));

    const QRect mrR = mrBtn->geometry();
    const QRect cfR = cfBtn->geometry();
    expect(bar->rect().contains(mrR) && bar->rect().contains(cfR),
           QStringLiteral("按钮均在控件矩形内（无裁切）"));

    const QColor kTrackLight(0xF8, 0xF9, 0xFA);   // @BG_MUTED@ 亮色
    const QColor kPrimary(0x2E, 0x7D, 0x32);       // @PRIMARY@

    QImage img = bar->grab().toImage();
    expect(near(sampleAt(img, mrR.x() + 3, mrR.center().y()), kPrimary),
           QStringLiteral("Modrinth 激活药丸 = PRIMARY"));
    expect(near(sampleAt(img, cfR.x() + 3, cfR.center().y()), kTrackLight),
           QStringLiteral("CurseForge 未选中 = 透出轨道"));
    bar->grab().save(QStringLiteral("D:/BlockBox/_out/source_switch_probe_modrinth.png"));

    // ── 点击分段：激活段不发信号，未激活段发出 sourceSelected ─────────────
    QString selected;
    QObject::connect(bar, &SourceSwitchBar::sourceSelected,
                     [&selected](const QString &s) { selected = s; });

    // 点击当前激活段（Modrinth）：不应发信号
    mrBtn->click();
    app.processEvents();
    expect(selected.isEmpty(), QStringLiteral("点击激活段（Modrinth）不发信号"));

    // 点击未激活段（CurseForge）：发出信号
    cfBtn->click();
    expect(selected == QLatin1String("curseforge"),
           QStringLiteral("点击 CurseForge 发出 sourceSelected(\"curseforge\")（实际: %1）")
               .arg(selected));

    // 模拟页面切换后的激活态回填（真实流程由 setModInfo 驱动）
    bar->setActiveSource(QStringLiteral("curseforge"));
    app.processEvents();
    img = bar->grab().toImage();
    expect(!mrBtn->property("active").toBool() && cfBtn->property("active").toBool(),
           QStringLiteral("切换后激活段属性移至 CurseForge"));
    expect(near(sampleAt(img, cfR.x() + 3, cfR.center().y()), kPrimary),
           QStringLiteral("CurseForge 激活药丸 = PRIMARY"));
    expect(near(sampleAt(img, mrR.x() + 3, mrR.center().y()), kTrackLight),
           QStringLiteral("Modrinth 恢复未选中"));
    bar->grab().save(QStringLiteral("D:/BlockBox/_out/source_switch_probe_curseforge.png"));

    // 再次点击激活段：不发信号
    selected.clear();
    mrBtn->click();
    app.processEvents();
    expect(selected == QLatin1String("modrinth"),
           QStringLiteral("点击未激活的 Modrinth 段发出 sourceSelected"));
    selected.clear();
    cfBtn->click();
    app.processEvents();
    expect(selected.isEmpty(), QStringLiteral("点击已激活段不再发信号"));

    std::fprintf(stderr, "[done] failures=%d parseWarnings=%d\n",
                 g_fail, g_parseWarnings);
    return g_fail == 0 ? 0 : 1;
}
