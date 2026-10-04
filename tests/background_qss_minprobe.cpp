/**
 * @file   background_qss_minprobe.cpp
 * @brief  最小化 QSS 语义探针（不依赖主程序，复现"设置页主栏自定义背景无效"）
 *
 * 复刻主程序的关键结构（contentWrapper → BackgroundWidget → QStackedWidget →
 * QScrollArea → SettingsPage → #settingsRightFrame → #settingsCard）与
 * style.qss 的关键规则，然后追加 BackgroundManager::backgroundStyleSheet()
 * 的真实输出，逐像素判断右栏是否透出背景，并捕获 "Could not parse
 * application stylesheet" 警告。
 *
 * 颜色语义：BackgroundWidget 画品红 #FF20E0；QSS 窗口底色画绿色 #00C853。
 *   右栏空隙像素 = 品红    ⇒ 透明链路完整生效
 *   右栏空隙像素 = 绿色    ⇒ 右栏透明但上层遮挡（控件不透明）
 *   右栏空隙像素 = #fafafa ⇒ 右栏不透明（问题复现）
 */

#include "utils/BackgroundManager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageLogContext>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <cstring>

namespace {

int g_parseWarnings = 0;

void msgHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains("Could not parse"))
        ++g_parseWarnings;
    fprintf(stderr, "[qt ] %s\n", msg.toLocal8Bit().constData());
}

// 背景层：无论什么模式都画品红（与 QSS 注入色不同，便于区分绘制来源）
class ProbeBgWidget : public QWidget {
public:
    explicit ProbeBgWidget(QWidget *parent = nullptr) : QWidget(parent) { }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xFF, 0x20, 0xE0));
    }
};

// 与 ThemeManager 浅色主题一致的令牌子表（覆盖背景样式串中用到的全部令牌）
QString resolveTokens(const QString &in)
{
    static const struct { const char *tok; const char *val; } T[] = {
        { "@BG_CARD@",        "#ffffff" },
        { "@BORDER@",         "#e8eaed" },
        { "@BG_HOVER@",       "#eaeaea" },
        { "@BG_PRESSED@",     "#e0e0e0" },
        { "@PRIMARY@",        "#2e7d32" },
        { "@PRIMARY_BG@",     "#e8f5e9" },
        { "@BORDER_LIGHT@",   "#f0f1f3" },
        { "@BG_CONTENT@",     "#fafafa" },
        { "@BG_BASE@",        "#f5f5f5" },
        { "@TEXT_PRIMARY@",   "#333333" },
    };
    QString out = in;
    for (const auto &t : T)
        out.replace(QString::fromLatin1(t.tok), QString::fromLatin1(t.val));
    return out;
}

const char *kBaseQss =
    "QMainWindow { background-color: #00c853; }\n"          // 窗口底色=绿色
    "QFrame { background-color: transparent; }\n"
    "QStackedWidget { background-color: transparent; border: none; }\n"
    "QScrollArea { background-color: transparent; border: none; }\n"
    "QScrollArea > QWidget { background-color: transparent; }\n"
    "QScrollArea > QWidget > QWidget { background-color: transparent; }\n"
    "QFrame#settingsRightFrame { background-color: #fafafa; border-top-right-radius: 16px; }\n"
    "QStackedWidget#settingsContentStack { background-color: transparent; border: none; }\n"
    "QWidget#settingsInterfaceContent { background-color: transparent; border: none; }\n"
    "QFrame#settingsCard { background-color: #ffffff; border: 1px solid #e8eaed; border-radius: 16px; }\n";

// ── 真实 style.qss 加载与规则拆分（用于二分定位肇事规则）─────────────────
QString neutralResolve(const QString &in)
{
    // 用真实值替换背景相关关键令牌，其余令牌替换为中性色，保证规则可解析
    QString out = resolveTokens(in);
    // 必须清掉所有 @...@（含未注册的小写令牌如 @radius-sm@），否则
    // 解析失败的规则会让二分把"整表被丢弃"误判成"遮挡复现"
    static const QRegularExpression re("@[A-Za-z0-9_.\\-]+@");
    out.replace(re, "#ececec");
    return out;
}

// 拆成顶层规则：去掉注释后按 '{'...'}' 配对切分（含选择器部分）
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
            if (ch == '"' ) inStr = !inStr;
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

QColor sampleAt(QImage img, int x, int y)
{
    const qreal dpr = img.devicePixelRatio();
    return img.pixelColor(int(x * dpr), int(y * dpr));
}

void report(const char *what, const QColor &c)
{
    fprintf(stderr, "[pixel] %-42s = %s\n", what, qUtf8Printable(c.name()));
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(msgHandler);
    QApplication app(argc, argv);

    // BackgroundManager 的 setter 会立即持久化到 QSettings，
    // 结束时按原值恢复，避免探针污染用户真实的背景配置
    QSettings bgSettings("BlockBox", "Settings");
    const int savedMode = bgSettings.value("background/mode", 0).toInt();
    const QString savedSolid = bgSettings.value("background/solidColor", "#f5f5f5").toString();
    const QString savedImage = bgSettings.value("background/imagePath", "").toString();
    const int savedBlur = bgSettings.value("background/blurRadius", 0).toInt();
    auto restoreBgSettings = [&]() {
        bgSettings.setValue("background/mode", savedMode);
        bgSettings.setValue("background/solidColor", savedSolid);
        bgSettings.setValue("background/imagePath", savedImage);
        bgSettings.setValue("background/blurRadius", savedBlur);
    };

    const QString mode = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("basic");
    const QString stylePath = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                       : QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss");

    // ── 复刻控件层级 ─────────────────────────────────────────────────────
    QMainWindow win;
    win.setFixedSize(800, 600);
    QWidget *central = new QWidget;
    QVBoxLayout *cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    QWidget *contentWrapper = new QWidget;
    contentWrapper->setObjectName("contentWrapper");
    QHBoxLayout *wl = new QHBoxLayout(contentWrapper);
    wl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(contentWrapper);

    ProbeBgWidget *bg = new ProbeBgWidget(contentWrapper);
    bg->lower();

    QStackedWidget *stack = new QStackedWidget(contentWrapper);
    wl->addWidget(stack);

    QScrollArea *sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    stack->addWidget(sa);

    QWidget *settingsPage = new QWidget;             // SettingsPage 替身
    sa->setWidget(settingsPage);
    QVBoxLayout *pl = new QVBoxLayout(settingsPage);
    pl->setContentsMargins(0, 0, 0, 0);

    QFrame *rightFrame = new QFrame;
    rightFrame->setObjectName("settingsRightFrame");
    pl->addWidget(rightFrame);
    QVBoxLayout *rl = new QVBoxLayout(rightFrame);
    rl->setContentsMargins(24, 8, 24, 24);

    QFrame *card = new QFrame;
    card->setObjectName("settingsCard");
    card->setFixedHeight(200);
    rl->addWidget(card);
    rl->addStretch();

    // 用真实 BackgroundManager 的样式输出（纯色模式）
    BackgroundManager *bgm = BackgroundManager::instance();
    bgm->setMode(BackgroundManager::SolidColor);
    bgm->setSolidColor("#00c853");                    // QSS 底色=绿（与品红区分）
    const QString override1 = resolveTokens(bgm->backgroundStyleSheet());

    win.setCentralWidget(central);
    win.show();

    auto grab = [&win]() { win.grab(); QCoreApplication::processEvents(); return win.grab().toImage(); };

    // 采样点：卡片外右栏空隙（下方 stretch 区）
    auto gapColor = [&grab]() {
        return sampleAt(grab(), 400, 450);
    };
    auto classify = [](const QColor &c) -> const char * {
        if (c == QColor(0xFF, 0x20, 0xE0)) return "MAGENTA(bg-widget)";
        if (c == QColor(0x00, 0xc8, 0x53)) return "GREEN(transparent)";
        if (c == QColor(0xfa, 0xfa, 0xfa)) return "FAFAFA(rightFrame-opaque)";
        if (c == QColor(0xff, 0xff, 0xff)) return "WHITE(opaque)";
        return "OTHER";
    };
    const QColor kGreen(0x00, 0xc8, 0x53);
    const QColor kMagenta(0xFF, 0x20, 0xE0);

    if (mode == QLatin1String("basic")) {
        // ── 步骤 1：仅基础样式 ───────────────────────────────────────────
        qApp->setStyleSheet(QString::fromLatin1(kBaseQss));
        report("step1 base-only, gap below card  ", sampleAt(grab(), 400, 450));
        report("step1 base-only, gap right side  ", sampleAt(grab(), 785, 100));
        report("step1 base-only, inside card     ", sampleAt(grab(), 400, 100));

        // ── 步骤 2：追加真实 BackgroundManager 覆盖样式 ──────────────────
        const int warnBefore = g_parseWarnings;
        qApp->setStyleSheet(QString::fromLatin1(kBaseQss) + "\n" + override1);
        report("step2 + bgManager override, below", sampleAt(grab(), 400, 450));
        report("step2 + bgManager override, right", sampleAt(grab(), 785, 100));
        report("step2 + bgManager override, card ", sampleAt(grab(), 400, 100));
        fprintf(stderr, "[info] parse warnings after step2: %d (delta %d)\n",
                g_parseWarnings, g_parseWarnings - warnBefore);

        // ── 步骤 3：手动追加等价透明规则（对照组）────────────────────────
        qApp->setStyleSheet(QString::fromLatin1(kBaseQss) +
                            "\nQWidget#contentWrapper QWidget { background: transparent; }");
        report("step3 manual rule, gap below     ", sampleAt(grab(), 400, 450));
        report("step3 manual rule, gap right     ", sampleAt(grab(), 785, 100));
        report("step3 manual rule, inside card   ", sampleAt(grab(), 400, 100));

        restoreBgSettings();
        QTimer::singleShot(100, &app, &QCoreApplication::quit);
        return app.exec();
    }

    // ── full / bisect 模式：加载真实 style.qss ───────────────────────────
    QFile f(stylePath);
    if (!f.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "[err ] cannot open %s\n", qUtf8Printable(stylePath));
        restoreBgSettings();
        return 2;
    }
    const QStringList rules = splitRules(neutralResolve(QString::fromUtf8(f.readAll())));
    fprintf(stderr, "[info] style.qss rules: %d\n", rules.size());

    QList<int> all;
    for (int i = 0; i < rules.size(); ++i)
        all << i;

    int warns = g_parseWarnings;
    const QColor fullColor = [&]() {
        QString sheet = QString::fromLatin1(kBaseQss);
        for (int i : all) sheet += "\n" + rules[i];
        sheet += "\n" + override1;
        qApp->setStyleSheet(sheet);
        return gapColor();
    }();
    fprintf(stderr, "[info] FULL style: gap=%s (parse warns %d)\n",
            qUtf8Printable(fullColor.name()), g_parseWarnings - warns);

    if (fullColor == kGreen || fullColor == kMagenta) {
        fprintf(stderr, "[verdict] FULL style + override still TRANSPARENT —— 真实 style.qss 不复现\n");
        restoreBgSettings();
        QTimer::singleShot(100, &app, &QCoreApplication::quit);
        return app.exec();
    }

    // 二分定位肇事规则（假设：单条高特异性规则压过透明覆盖）
    QList<QList<int>> queue;
    if (!all.isEmpty()) queue << all;
    QList<int> culprits;
    while (!queue.isEmpty()) {
        const QList<int> cur = queue.takeFirst();
        if (cur.isEmpty())
            continue;
        auto testSet = [&](const QList<int> &idx) {
            QString sheet = QString::fromLatin1(kBaseQss);
            for (int i : idx) sheet += "\n" + rules[i];
            sheet += "\n" + override1;
            const int w0 = g_parseWarnings;
            qApp->setStyleSheet(sheet);
            if (g_parseWarnings != w0)
                return false;          // 该集合解析失败 ⇒ 整表被丢弃，结论无效
            const QColor c = gapColor();
            return c != kGreen && c != kMagenta;        // 不透明 ⇒ 复现
        };
        if (cur.size() == 1) {
            culprits << cur.first();
            fprintf(stderr, "[culprit] rule#%d : %s\n", cur.first(),
                    qUtf8Printable(rules[cur.first()].simplified().left(160)));
            continue;
        }
        const int mid = cur.size() / 2;
        const QList<int> left = cur.mid(0, mid);
        const QList<int> right = cur.mid(mid);
        if (testSet(left))  queue << left;
        if (testSet(right)) queue << right;
    }
    fprintf(stderr, "[verdict] culprits found: %d\n", culprits.size());

    restoreBgSettings();
    QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
