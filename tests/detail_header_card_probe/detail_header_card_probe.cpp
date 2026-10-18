/**
 * @file   detail_header_card_probe.cpp
 * @brief  资源详情页顶部信息区卡片（QWidget#modDetailHeader）独立渲染探针
 *
 * 被验证的实现（只读参考，探针不修改它们）：
 *   - styles/style.qss:
 *       QWidget#modDetailHeader { background-color: @BG_GLASS@; border: 1px solid @BORDER@; border-radius: 12px; }
 *   - pages/{ContentDetailPage,BedrockContentDetailPage,ModDetailPage}.cpp::buildHeader()
 *       头部布局 setContentsMargins(20,20,20,20) / setSpacing(20)
 *       内部：QLabel#modDetailIcon(64x64) + 名称/英文名/简介 + QPushButton#contentFavoriteBtn(40x40)
 *
 * 探针做法（不链接 ThemeManager，避免 BackgroundManager/PluginManager 依赖）：
 *   1. 读取 styles/style.qss 原文；
 *   2. 解析 utils/ThemeManager.cpp 里 buildTokenTable() 的浅色/深色分支 + 公共段真心取值；
 *   3. 只替换 QSS 中真实出现过的 @TOKEN@（其余 token 走兜底值），整份应用到 QApplication；
 *   4. 用与真实代码一致的 objectName / 层级 / 内边距搭出头部，浅色+深色各 grab() 一张 PNG；
 *   5. 程序化像素断言 + 负控制（objectName 改名后断言必须失败）。
 *
 * PNG 输出：D:/BlockBox/_out/detail_header_card_{light,dark}.png
 *          D:/BlockBox/_out/detail_header_card_light_shadow.png   （阴影诊断，仅 INFO）
 *          D:/BlockBox/_out/detail_header_card_light_norule.png   （负控制证据）
 */
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMap>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

int g_fail = 0;
int g_pass = 0;
int g_parseWarnings = 0;
QStringList g_parseWarningSamples;

void msgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains(QLatin1String("Could not parse"))) {
        ++g_parseWarnings;
        if (g_parseWarningSamples.size() < 3)
            g_parseWarningSamples << msg;
    }
    std::fprintf(stderr, "[qt ] %s\n", msg.toLocal8Bit().constData());
}

void expect(bool ok, const QString &what)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", qUtf8Printable(what));
    if (ok)
        ++g_pass;
    else
        ++g_fail;
}

void info(const QString &what)
{
    std::fprintf(stderr, "[info] %s\n", qUtf8Printable(what));
}

// ───────────────────────────── 颜色小工具 ─────────────────────────────

QString hex(const QColor &c)
{
    return c.name(QColor::HexRgb);
}

QString rgbaText(const QColor &c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(QString::number(c.alphaF(), 'f', 2));
}

/// 解析 ThemeManager 风格的取值：#rrggbb / #rgb / rgb() / rgba()（alpha 支持 0-1 浮点、百分比、0-255 整数）
QColor parseColorValue(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.startsWith(QLatin1Char('#'))) {
        const QColor c(s);
        return c.isValid() ? c : QColor();
    }
    static const QRegularExpression re(
        QStringLiteral("^rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*"
                       "(?:,\\s*([0-9]*\\.?[0-9]+)\\s*(%?))?\\)$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return QColor();
    QColor c(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    if (!m.captured(4).isEmpty()) {
        const double v = m.captured(4).toDouble();
        int a = 255;
        if (m.captured(5) == QLatin1String("%"))
            a = qRound(255.0 * v / 100.0);
        else if (v <= 1.0)
            a = qRound(255.0 * v);
        else
            a = qRound(v);
        c.setAlpha(qBound(0, a, 255));
    }
    return c;
}

/// 半透明前景 src 叠在不透明背景 dst 上的结果（CSS alpha 混合）
QColor blendOver(const QColor &src, const QColor &dst)
{
    const double a = src.alphaF();
    auto mix = [a](int s, int d) { return qRound(s * a + d * (1.0 - a)); };
    return QColor(mix(src.red(), dst.red()),
                  mix(src.green(), dst.green()),
                  mix(src.blue(), dst.blue()));
}

bool near(const QColor &a, const QColor &b, int tol)
{
    return qAbs(a.red() - b.red()) <= tol
        && qAbs(a.green() - b.green()) <= tol
        && qAbs(a.blue() - b.blue()) <= tol;
}

int maxChannelDiff(const QColor &a, const QColor &b)
{
    return std::max({qAbs(a.red() - b.red()), qAbs(a.green() - b.green()), qAbs(a.blue() - b.blue())});
}

double relLuminance(const QColor &c)
{
    auto lin = [](int v) {
        const double s = v / 255.0;
        return (s <= 0.03928) ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.red()) + 0.7152 * lin(c.green()) + 0.0722 * lin(c.blue());
}

double contrastRatio(const QColor &a, const QColor &b)
{
    double la = relLuminance(a);
    double lb = relLuminance(b);
    if (la < lb)
        std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

// ─────────────────────── ThemeManager.cpp 取值解析 ───────────────────────

QString extractBlock(const QString &src, const QString &beginMarker, const QString &endMarker)
{
    const int b = src.indexOf(beginMarker);
    if (b < 0)
        return QString();
    const int e = src.indexOf(endMarker, b + beginMarker.size());
    if (e < 0)
        return QString();
    return src.mid(b, e - b);
}

QMap<QString, QString> parseLiteralAssignments(const QString &block)
{
    QMap<QString, QString> out;
    static const QRegularExpression re(
        QStringLiteral("T\\[\\s*\"(@[A-Z0-9_.]+@)\"\\s*\\]\\s*=\\s*\"([^\"]*)\"\\s*;"));
    QRegularExpressionMatchIterator it = re.globalMatch(block);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out.insert(m.captured(1), m.captured(2));
    }
    return out;
}

/// 抽出 `isLight ? "A" : (isDark ? "B" : "C")` / `isLight ? "A" : "B"` 形式的取值，并从文本中移除
QString applyAndStripTernaries(const QRegularExpression &re, QString work,
                               QMap<QString, QString> &light, QMap<QString, QString> &dark)
{
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(work);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += work.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        light.insert(m.captured(1), m.captured(2));
        dark.insert(m.captured(1), m.captured(3));
    }
    out += work.mid(last);
    return out;
}

bool parseTernaryPair(const QString &src, const QString &fn, QString *light, QString *dark)
{
    const int i = src.indexOf(fn + QLatin1String("("));
    if (i < 0)
        return false;
    const int j = src.indexOf(QLatin1String("return"), i);
    if (j < 0)
        return false;
    const int k = src.indexOf(QLatin1Char(';'), j);
    if (k < 0)
        return false;
    static const QRegularExpression re(QStringLiteral("\"(#[0-9a-fA-F]{3,8})\""));
    QStringList vals;
    QRegularExpressionMatchIterator it = re.globalMatch(src.mid(j, k - j));
    while (it.hasNext())
        vals << it.next().captured(1);
    if (vals.size() < 2)
        return false;
    *light = vals.at(0);
    *dark = vals.at(1);
    return true;
}

QString primaryDefault(const QString &src)
{
    static const QRegularExpression re(
        QStringLiteral("m_currentThemeColor\\(\"(#[0-9a-fA-F]{6})\"\\)"));
    const QRegularExpressionMatch m = re.match(src);
    return m.hasMatch() ? m.captured(1) : QStringLiteral("#2E7D32");
}

struct ThemeInfo {
    QString name;
    QString textHex;
    QString borderHex;
    QString primaryHex;
    QMap<QString, QString> tokens;
};

QString glueRgba(const QColor &c, const QString &alphaLiteral)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alphaLiteral);
}

/// 未在源码中直接取到的 token 的兜底值（保证 QSS 里不残留 @TOKEN@，否则 Qt 会中止解析并丢弃后续规则）
QString fallbackValue(const QString &token, const ThemeInfo &ti, bool isLight)
{
    const QString inner = token.mid(1, token.size() - 2);   // 去掉首尾 @
    const QColor pc(ti.primaryHex);

    if (inner.startsWith(QLatin1String("PRIMARY_RGBA_"))) {
        const QString a = inner.mid(QLatin1String("PRIMARY_RGBA_").size());
        return glueRgba(pc, a);
    }
    if (inner.startsWith(QLatin1String("BLACK_RGBA_"))) {
        const QString a = inner.mid(QLatin1String("BLACK_RGBA_").size());
        if (a.contains(QLatin1Char('.')) && !isLight)
            return QStringLiteral("rgba(255, 255, 255, %1)").arg(a);
        return QStringLiteral("rgba(0, 0, 0, %1)").arg(a);
    }
    if (inner.startsWith(QLatin1String("INFO_RGBA_")))
        return glueRgba(QColor(QStringLiteral("#2196F3")), inner.mid(QLatin1String("INFO_RGBA_").size()));
    if (inner.startsWith(QLatin1String("DANGER_RGBA_")))
        return glueRgba(QColor(QStringLiteral("#f44336")), inner.mid(QLatin1String("DANGER_RGBA_").size()));
    if (inner.startsWith(QLatin1String("WARNING_RGBA_")))
        return glueRgba(QColor(QStringLiteral("#ff9800")), inner.mid(QLatin1String("WARNING_RGBA_").size()));
    if (inner.startsWith(QLatin1String("WHITE_RGBA_")))
        return glueRgba(QColor(Qt::white), inner.mid(QLatin1String("WHITE_RGBA_").size()));
    if (inner.startsWith(QLatin1String("TEAL_RGBA_")))
        return glueRgba(QColor(0, 200, 200), inner.mid(QLatin1String("TEAL_RGBA_").size()));
    if (inner.startsWith(QLatin1String("INDIGO_RGBA_")))
        return glueRgba(QColor(QStringLiteral("#5c6bc0")), inner.mid(QLatin1String("INDIGO_RGBA_").size()));

    if (inner.contains(QLatin1String("TEXT_PRIMARY")))
        return ti.textHex;
    if (inner.contains(QLatin1String("TEXT")))
        return ti.textHex;
    if (inner.contains(QLatin1String("PRIMARY_DARK")))
        return pc.darker(130).name();
    if (inner.contains(QLatin1String("PRIMARY_HOVER")))
        return pc.darker(110).name();
    if (inner.contains(QLatin1String("PRIMARY_PRESSED")))
        return pc.darker(125).name();
    if (inner.contains(QLatin1String("PRIMARY")))
        return inner.contains(QLatin1String("BG")) ? pc.lighter(180).name() : pc.name();
    if (inner.contains(QLatin1String("BORDER")))
        return ti.borderHex;
    if (inner.startsWith(QLatin1String("BG_")))
        return ti.tokens.value(QStringLiteral("@BG_BASE@"), isLight ? QStringLiteral("#f5f5f5")
                                                                   : QStringLiteral("#1a1a1a"));
    if (inner.contains(QLatin1String("SUCCESS")))
        return isLight ? QStringLiteral("#059669") : QStringLiteral("#10B981");
    if (inner.contains(QLatin1String("WARNING")))
        return QStringLiteral("#ff9800");
    if (inner.contains(QLatin1String("DANGER")))
        return QStringLiteral("#f44336");
    if (inner.contains(QLatin1String("INFO")))
        return QStringLiteral("#2196F3");
    if (inner.contains(QLatin1String("LINK")))
        return QStringLiteral("#1a73e8");
    if (inner.contains(QLatin1String("EDITOR")))
        return QStringLiteral("#1e1e1e");
    if (inner.contains(QLatin1String("CODE")))
        return QStringLiteral("#f6f8fa");
    if (inner.contains(QLatin1String("BAR_BG")))
        return isLight ? QStringLiteral("#E0E0E0") : QStringLiteral("#2E2E2E");
    if (inner.contains(QLatin1String("SECONDARY")))
        return ti.tokens.value(QStringLiteral("@TEXT_SECONDARY@"),
                               isLight ? QStringLiteral("#666666") : QStringLiteral("#b0b0b0"));
    return isLight ? QStringLiteral("#ececec") : QStringLiteral("#3a3a3a");
}

bool buildThemeInfos(const QString &tmSrc, ThemeInfo *light, ThemeInfo *dark)
{
    const QString lightBlock = extractBlock(tmSrc, QStringLiteral("if (isLight)"),
                                            QStringLiteral("} else if (isDark)"));
    const QString darkBlock = extractBlock(tmSrc, QStringLiteral("} else if (isDark)"),
                                           QStringLiteral("} else {"));
    if (lightBlock.isEmpty() || darkBlock.isEmpty()) {
        std::fprintf(stderr, "[err ] 无法从 ThemeManager.cpp 中切出浅色/深色 token 分支\n");
        return false;
    }

    light->name = QStringLiteral("light");
    dark->name = QStringLiteral("dark");
    light->tokens = parseLiteralAssignments(lightBlock);
    dark->tokens = parseLiteralAssignments(darkBlock);

    // 公共段：三元表达式 + 常量字面量（浅色/深色分开取真实值）
    const QString commonBlock = extractBlock(tmSrc, QStringLiteral("T[\"@BORDER_ON_ACCENT@\"]"),
                                             QStringLiteral("return T;"));
    if (commonBlock.isEmpty()) {
        std::fprintf(stderr, "[err ] 无法定位 ThemeManager.cpp 的公共 token 段\n");
        return false;
    }
    static const QRegularExpression nested(
        QStringLiteral("T\\[\\s*\"(@[A-Z0-9_.]+@)\"\\s*\\]\\s*=\\s*isLight\\s*\\?\\s*\"([^\"]*)\"\\s*:"
                       "\\s*\\(\\s*isDark\\s*\\?\\s*\"([^\"]*)\"\\s*:\\s*\"[^\"]*\"\\s*\\)\\s*;"));
    static const QRegularExpression simple(
        QStringLiteral("T\\[\\s*\"(@[A-Z0-9_.]+@)\"\\s*\\]\\s*=\\s*isLight\\s*\\?\\s*\"([^\"]*)\"\\s*:"
                       "\\s*\"([^\"]*)\"\\s*;"));
    QString work = applyAndStripTernaries(nested, commonBlock, light->tokens, dark->tokens);
    work = applyAndStripTernaries(simple, work, light->tokens, dark->tokens);
    const QMap<QString, QString> commonLiterals = parseLiteralAssignments(work);
    for (auto it = commonLiterals.constBegin(); it != commonLiterals.constEnd(); ++it) {
        light->tokens.insert(it.key(), it.value());
        dark->tokens.insert(it.key(), it.value());
    }

    // 计算型 token（源码里不是字面量，按源码公式取真实值）
    QString textL, textD, borderL, borderD, secL, secD;
    if (!parseTernaryPair(tmSrc, QStringLiteral("defaultTextColor"), &textL, &textD)
        || !parseTernaryPair(tmSrc, QStringLiteral("defaultTextBorderColor"), &borderL, &borderD)
        || !parseTernaryPair(tmSrc, QStringLiteral("defaultSecondaryColor"), &secL, &secD)) {
        std::fprintf(stderr, "[err ] 无法解析 ThemeManager.cpp 的 default*Color() 取值\n");
        return false;
    }
    const QString primary = primaryDefault(tmSrc);

    ThemeInfo *infos[2] = {light, dark};
    const bool lights[2] = {true, false};
    const QString texts[2] = {textL, textD};
    const QString borders[2] = {borderL, borderD};
    const QString secs[2] = {secL, secD};
    for (int i = 0; i < 2; ++i) {
        ThemeInfo *ti = infos[i];
        const bool isLight = lights[i];
        const QColor pc(primary);

        ti->textHex = texts[i];
        ti->borderHex = ti->tokens.value(QStringLiteral("@BORDER@"), borders[i]);
        ti->primaryHex = primary;

        ti->tokens[QStringLiteral("@TEXT_PRIMARY@")] = texts[i];
        ti->tokens[QStringLiteral("@TEXT_BORDER@")] = borders[i];
        ti->tokens[QStringLiteral("@SECONDARY@")] = secs[i];
        ti->tokens[QStringLiteral("@PRIMARY@")] = pc.name();
        ti->tokens[QStringLiteral("@PRIMARY_HOVER@")] = pc.darker(110).name();
        ti->tokens[QStringLiteral("@PRIMARY_PRESSED@")] = pc.darker(125).name();
        ti->tokens[QStringLiteral("@PRIMARY_DARK@")] = pc.darker(130).name();
        ti->tokens[QStringLiteral("@PRIMARY_MEDIUM@")] = pc.lighter(120).name();
        ti->tokens[QStringLiteral("@PRIMARY_LIGHT@")] = pc.lighter(150).name();
        ti->tokens[QStringLiteral("@PRIMARY_LIGHTER@")] = pc.lighter(160).name();
        ti->tokens[QStringLiteral("@PRIMARY_GRAD1@")] = pc.lighter(130).name();
        ti->tokens[QStringLiteral("@PRIMARY_GRAD2@")] = pc.lighter(150).name();
        ti->tokens[QStringLiteral("@PRIMARY_GRAD3@")] = pc.lighter(140).name();
        ti->tokens[QStringLiteral("@PRIMARY_BG@")] = isLight ? pc.lighter(180).name()
                                                              : QStringLiteral("#2d4d2d");
        ti->tokens[QStringLiteral("@PRIMARY_BG_SOFT@")] = pc.lighter(190).name();
        ti->tokens[QStringLiteral("@TEXT_ACCENT@")] = isLight ? pc.darker(140).name()
                                                              : pc.lighter(120).name();
        ti->tokens[QStringLiteral("@TEXT_ACCENT_DARK@")] = isLight ? pc.darker(170).name()
                                                                   : pc.lighter(150).name();
        ti->tokens[QStringLiteral("@PRIMARY_TEXT@")] = ti->tokens.value(QStringLiteral("@TEXT_ACCENT@"));
        ti->tokens[QStringLiteral("@TEXT_ON_PRIMARY@")] = QStringLiteral("#ffffff");
        ti->tokens[QStringLiteral("@BORDER_ON_ACCENT@")] = QStringLiteral("#ffffff");
        ti->tokens[QStringLiteral("@BG_SECONDARY@")] = ti->tokens.value(QStringLiteral("@BG_MUTED@"));
        ti->tokens[QStringLiteral("@BORDER_DEFAULT@")] = ti->tokens.value(QStringLiteral("@BORDER@"));
        const QColor card(ti->tokens.value(QStringLiteral("@BG_CARD@")));
        const QColor content(ti->tokens.value(QStringLiteral("@BG_CONTENT@")));
        ti->tokens[QStringLiteral("@BG_CARD_GLASS@")] = glueRgba(card, QStringLiteral("0.65"));
        ti->tokens[QStringLiteral("@BG_CONTENT_GLASS@")] = glueRgba(content, QStringLiteral("0.65"));
    }
    return true;
}

/// 只替换 QSS 中真实出现过的 token（正则与 ThemeManager::resolveTokens 一致：大写 + 数字 + _ . ）
QString resolveQss(const QString &raw, const ThemeInfo &ti, bool isLight,
                   int *fromSource, QStringList *fallbackTokens)
{
    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(raw);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += raw.mid(last, m.capturedStart() - last);
        const QString token = m.captured(0);
        const QString fromTable = ti.tokens.value(token);
        if (!fromTable.isEmpty()) {
            out += fromTable;
            ++(*fromSource);
        } else {
            out += fallbackValue(token, ti, isLight);
            fallbackTokens->append(token);
        }
        last = m.capturedEnd();
    }
    out += raw.mid(last);
    return out;
}

// ───────────────────────────── 渲染探针本体 ─────────────────────────────

struct CardPass {
    QWidget *host = nullptr;
    QWidget *header = nullptr;
    QLabel *icon = nullptr;
    QPushButton *fav = nullptr;
    QImage img;
    QRect card;
};

CardPass *buildAndGrab(const QString &headerObjectName, bool isLight, bool withShadow,
                       const QString &pngPath)
{
    CardPass *p = new CardPass;

    p->host = new QWidget;
    p->host->setObjectName(QStringLiteral("probePageHost"));
    p->host->setAttribute(Qt::WA_StyledBackground, true);
    p->host->resize(900, 260);

    QVBoxLayout *hostLayout = new QVBoxLayout(p->host);
    hostLayout->setContentsMargins(24, 24, 24, 24);
    hostLayout->setSpacing(0);

    // —— 与真实 buildHeader() 完全一致：objectName / 层级 / 内边距 / 间距 / 尺寸 ——
    p->header = new QWidget(p->host);
    p->header->setObjectName(headerObjectName);
    if (withShadow) {
        // ModDetailPage::addSectionShadow(m_headerWidget) 的真实参数
        QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(p->header);
        shadow->setBlurRadius(16);
        shadow->setOffset(0, 2);
        shadow->setColor(QColor(0, 0, 0, 20));
        p->header->setGraphicsEffect(shadow);
    }
    QHBoxLayout *headerLayout = new QHBoxLayout(p->header);
    headerLayout->setContentsMargins(20, 20, 20, 20);
    headerLayout->setSpacing(20);

    p->icon = new QLabel(p->header);
    p->icon->setObjectName(QStringLiteral("modDetailIcon"));
    p->icon->setFixedSize(64, 64);
    p->icon->setAlignment(Qt::AlignCenter);
    QPixmap placeholder(64, 64);
    placeholder.fill(isLight ? QColor(QStringLiteral("#e8e8e8")) : QColor(QStringLiteral("#2d2d2d")));
    p->icon->setPixmap(placeholder);
    headerLayout->addWidget(p->icon);

    QWidget *nameWidget = new QWidget(p->header);
    QVBoxLayout *nameLayout = new QVBoxLayout(nameWidget);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(4);

    QLabel *chinese = new QLabel(QString::fromUtf8("极限优化"), nameWidget);
    chinese->setObjectName(QStringLiteral("modDetailChineseName"));
    nameLayout->addWidget(chinese);

    QLabel *english = new QLabel(QStringLiteral("Extreme Optimization"), nameWidget);
    english->setObjectName(QStringLiteral("modDetailEnglishName"));
    nameLayout->addWidget(english);

    QLabel *desc = new QLabel(QString::fromUtf8("渲染探针用简介文本，用于占位验证头部卡片的排版与内边距。"), nameWidget);
    desc->setObjectName(QStringLiteral("modDetailDesc"));
    desc->setWordWrap(true);
    nameLayout->addWidget(desc);

    headerLayout->addWidget(nameWidget, 1);

    p->fav = new QPushButton(p->header);
    p->fav->setObjectName(QStringLiteral("contentFavoriteBtn"));
    p->fav->setCursor(Qt::PointingHandCursor);
    p->fav->setFixedSize(40, 40);
    p->fav->setIconSize(QSize(20, 20));
    // 与 ContentDetailPage/BedrockContentDetailPage 里一致的行内白底样式
    p->fav->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e0e0e0;"
        "  border-radius: 10px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #fff8e1;"
        "  border-color: #ffc107;"
        "}"));
    headerLayout->addWidget(p->fav);

    hostLayout->addWidget(p->header);
    hostLayout->addStretch(1);

    p->host->show();
    QCoreApplication::processEvents();
    p->host->layout()->activate();
    QCoreApplication::processEvents();

    p->card = p->header->geometry();
    p->img = p->host->grab().toImage();
    p->img.setDevicePixelRatio(1.0);
    if (!pngPath.isEmpty()) {
        QDir().mkpath(QFileInfo(pngPath).absolutePath());
        p->img.save(pngPath);
    }
    return p;
}

QColor sampleAt(const QImage &img, int x, int y)
{
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height())
        return QColor();
    return img.pixelColor(x, y);
}

QString ptText(int x, int y)
{
    return QStringLiteral("(%1,%2)").arg(x).arg(y);
}

// ───────────────────────────── 断言 ─────────────────────────────

void assertTheme(const QString &tag, CardPass *p, const QColor &pageBg, const QColor &glass,
                 const QColor &border, const QColor &fill)
{
    const QRect c = p->card;
    const int l = c.left(), t = c.top(), r = c.right(), b = c.bottom();
    const int cx = c.center().x(), cy = c.center().y();

    info(QStringLiteral("%1 卡片矩形(l,t,w,h) = (%2,%3,%4,%5) 页面底色=%6 玻璃色=%7(@BG_GLASS@) 期望内部混合色=0.65*玻璃+0.35*页面=%8 边框=%9")
             .arg(tag).arg(l).arg(t).arg(c.width()).arg(c.height())
             .arg(hex(pageBg), rgbaText(glass), hex(fill), hex(border)));

    // ① 卡片内部空白像素 == 0.65*BG_GLASS + 0.35*页面底色（±2）
    const QPoint interior[4] = {QPoint(cx, t + 8), QPoint(cx, b - 8),
                                QPoint(l + 8, cy), QPoint(r - 8, cy)};
    QStringList interiorMeasured;
    bool interiorOk = true;
    for (const QPoint &pt : interior) {
        const QColor got = sampleAt(p->img, pt.x(), pt.y());
        interiorMeasured << QStringLiteral("%1=%2(Δ%3)").arg(ptText(pt.x(), pt.y()), hex(got))
                                .arg(maxChannelDiff(got, fill));
        if (!near(got, fill, 2))
            interiorOk = false;
    }
    expect(interiorOk,
           QStringLiteral("%1 ① 卡片内部空白像素 == 0.65*@BG_GLASS@+0.35*页面底色(%2, ±2)  实测: %3")
               .arg(tag, hex(fill), interiorMeasured.join(QStringLiteral(", "))));

    // ② 卡片内部 != 页面底色（卡片本身可见）
    bool visibleOk = true;
    QStringList visibleMeasured;
    for (const QPoint &pt : interior) {
        const QColor got = sampleAt(p->img, pt.x(), pt.y());
        const int d = maxChannelDiff(got, pageBg);
        visibleMeasured << QStringLiteral("%1=Δ%2").arg(ptText(pt.x(), pt.y())).arg(d);
        if (d < 3)
            visibleOk = false;
    }
    info(QStringLiteral("%1 ② 卡片面 vs 页面底色色差: %2 ; 对比度(卡片面/页面)=%3 边框/页面=%4")
             .arg(tag, visibleMeasured.join(QStringLiteral(", ")))
             .arg(QString::number(contrastRatio(fill, pageBg), 'f', 3),
                  QString::number(contrastRatio(border, pageBg), 'f', 3)));
    expect(visibleOk,
           QStringLiteral("%1 ② 卡片内部 != 页面底色(%2)（卡片可见）").arg(tag, hex(pageBg)));

    // ③ 四角外侧 == 页面底色，且 != 卡片面（12px 圆角生效）
    const QPoint corners[4] = {QPoint(l + 1, t + 1), QPoint(r - 1, t + 1),
                               QPoint(l + 1, b - 1), QPoint(r - 1, b - 1)};
    const char *cornerName[4] = {"左上", "右上", "左下", "右下"};
    for (int i = 0; i < 4; ++i) {
        const QColor got = sampleAt(p->img, corners[i].x(), corners[i].y());
        expect(near(got, pageBg, 3) && !near(got, fill, 2),
               QStringLiteral("%1 ③ %2角外侧 %3 == 页面底色 %4 且 != 卡片面 %5  实测=%6")
                   .arg(tag, QString::fromUtf8(cornerName[i]), ptText(corners[i].x(), corners[i].y()),
                        hex(pageBg), hex(fill), hex(got)));
    }

    // ④ 上边框像素 == @BORDER@（±6，含抗锯齿容差）
    bool borderOk = true;
    QStringList borderMeasured;
    const int borderXs[3] = {cx - 100, cx, cx + 100};
    for (int i = 0; i < 3; ++i) {
        const QColor got = sampleAt(p->img, borderXs[i], t);
        borderMeasured << QStringLiteral("%1=%2").arg(ptText(borderXs[i], t), hex(got));
        if (!near(got, border, 6))
            borderOk = false;
    }
    expect(borderOk,
           QStringLiteral("%1 ④ 卡片上边框行 y=%2 == @BORDER@(%3, ±6)  实测: %4")
               .arg(tag).arg(t).arg(hex(border), borderMeasured.join(QStringLiteral(", "))));
    info(QStringLiteral("%1 ④b 下边框行 y=%2 = %3（参考，期望同为 @BORDER@）")
             .arg(tag).arg(b).arg(hex(sampleAt(p->img, cx, b))));

    // ⑤ 底边内侧第一行 == 卡片面（旧的 1px 底部灰色分隔线已不存在）
    bool bottomRowOk = true;
    QString bottomRowBad;
    for (int x = l + 14; x <= r - 14; ++x) {
        const QColor got = sampleAt(p->img, x, b - 1);
        if (!near(got, fill, 2)) {
            bottomRowOk = false;
            bottomRowBad = QStringLiteral("首个不符 %1=%2").arg(ptText(x, b - 1), hex(got));
            break;
        }
    }
    expect(bottomRowOk,
           QStringLiteral("%1 ⑤ 卡片底边内侧第一行 y=%2 整行 == 卡片面(%3)  实测: %4")
               .arg(tag).arg(b - 1).arg(hex(fill),
                    bottomRowOk ? QStringLiteral("整行一致") : bottomRowBad));

    // ⑥ 底部 20px 内边距区域没有内容（整行都是卡片面）——x 避开 12px 圆角弧线区域
    bool bandOk = true;
    QString bandBad;
    const int x0 = l + 14, x1 = r - 14;
    for (int y = b - 19; y <= b - 1 && bandOk; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const QColor got = sampleAt(p->img, x, y);
            if (!near(got, fill, 2)) {
                bandOk = false;
                bandBad = QStringLiteral("首个不符 %1=%2").arg(ptText(x, y), hex(got));
                break;
            }
        }
    }
    expect(bandOk,
           QStringLiteral("%1 ⑥ 底部 20px 内边距区域(y=%2..%3, x=%4..%5)整行均为卡片面(%6) 实测: %7")
               .arg(tag).arg(b - 19).arg(b - 1).arg(x0).arg(x1).arg(hex(fill),
                    bandOk ? QStringLiteral("无内容贴底") : bandBad));

    info(QStringLiteral("%1 图标 QLabel 实际尺寸 = %2x%3（代码 setFixedSize(64,64)，QSS 里 min/max-width=72px）")
             .arg(tag).arg(p->icon->width()).arg(p->icon->height()));
    info(QStringLiteral("%1 收藏按钮实际尺寸 = %2x%3，位置=%4")
             .arg(tag).arg(p->fav->width()).arg(p->fav->height())
             .arg(QStringLiteral("(%1,%2)").arg(p->fav->geometry().x()).arg(p->fav->geometry().y())));
}

/// 负控制：objectName 换掉后，① 的断言必须失败
void assertNegativeControl(const QString &tag, CardPass *p, const QColor &pageBg, const QColor &fill)
{
    const QRect c = p->card;
    const int cx = c.center().x();
    const QColor got = sampleAt(p->img, cx, c.top() + 8);
    const bool stillFill = near(got, fill, 2);
    expect(!stillFill,
           QStringLiteral("NEGATIVE-CONTROL %1 ① objectName=modDetailHeaderX 后卡片内部 %2 不再等于卡片面 %3")
               .arg(tag, ptText(cx, c.top() + 8), hex(fill)));
    if (!stillFill)
        std::fprintf(stderr, "[    ] NEGATIVE-CONTROL %s PASS（负控制生效：该像素实测=%s，页面底色=%s）\n",
                     qUtf8Printable(tag), qUtf8Printable(hex(got)), qUtf8Printable(hex(pageBg)));
    expect(near(got, pageBg, 2),
           QStringLiteral("NEGATIVE-CONTROL %1 无规则命中时卡片内部透出页面底色 %2（实测=%3）")
               .arg(tag, hex(pageBg), hex(got)));
}

} // namespace

int main(int argc, char **argv)
{
    qInstallMessageHandler(msgHandler);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("detail_header_card_probe"));

    // 工程根目录定位（构建目录在 tests/detail_header_card_probe/build[/release] 下）
    QString root = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../../../.."));
    if (!QFile::exists(root + QStringLiteral("/styles/style.qss")))
        root = QStringLiteral("D:/BlockBox/C-BlockBox");
    info(QStringLiteral("工程根目录 = %1").arg(root));

    QFile f(root + QStringLiteral("/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "[err ] 打不开 styles/style.qss\n");
        return 2;
    }
    const QString rawQss = QString::fromUtf8(f.readAll());
    f.close();

    QFile tm(root + QStringLiteral("/utils/ThemeManager.cpp"));
    if (!tm.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "[err ] 打不开 utils/ThemeManager.cpp\n");
        return 2;
    }
    const QString tmSrc = QString::fromUtf8(tm.readAll());
    tm.close();

    ThemeInfo light, dark;
    if (!buildThemeInfos(tmSrc, &light, &dark))
        return 2;

    QMap<QString, QString> qssByTheme;
    const ThemeInfo *infos[2] = {&light, &dark};
    const bool lights[2] = {true, false};
    for (int i = 0; i < 2; ++i) {
        const ThemeInfo *ti = infos[i];
        int fromSource = 0;
        QStringList fallbacks;
        QString resolved = resolveQss(rawQss, *ti, lights[i], &fromSource, &fallbacks);
        fallbacks.removeDuplicates();
        info(QStringLiteral("[%1] token: 取自 ThemeManager.cpp 源码 %2 处；兜底 %3 种%4")
                 .arg(ti->name).arg(fromSource).arg(fallbacks.size())
                 .arg(fallbacks.isEmpty() ? QString()
                                          : QStringLiteral("（%1）")
                                                .arg(fallbacks.mid(0, 8).join(QStringLiteral(" ")))));
        info(QStringLiteral("[%1] 关键 token: @BG_GLASS@=%2 @BORDER@=%3 @BG_BASE@=%4 @BG_CARD@=%5 @TEXT_PRIMARY@=%6 @PRIMARY@=%7")
                 .arg(ti->name,
                      ti->tokens.value(QStringLiteral("@BG_GLASS@")),
                      ti->tokens.value(QStringLiteral("@BORDER@")),
                      ti->tokens.value(QStringLiteral("@BG_BASE@")),
                      ti->tokens.value(QStringLiteral("@BG_CARD@")),
                      ti->tokens.value(QStringLiteral("@TEXT_PRIMARY@")),
                      ti->tokens.value(QStringLiteral("@PRIMARY@"))));
        static const QRegularExpression leftover(QStringLiteral("@([A-Z0-9_.]+)@"));
        expect(!leftover.match(resolved).hasMatch(),
               QStringLiteral("[%1] QSS 中无残留未解析 token").arg(ti->name));
        qssByTheme.insert(ti->name, resolved);
    }

    expect(light.tokens.contains(QStringLiteral("@BG_GLASS@"))
               && light.tokens.value(QStringLiteral("@BG_GLASS@")).contains(QLatin1String("rgba")),
           QStringLiteral("从 ThemeManager.cpp 取到真实的 @BG_GLASS@ = %1")
               .arg(light.tokens.value(QStringLiteral("@BG_GLASS@"))));
    expect(dark.tokens.value(QStringLiteral("@BG_GLASS@")) != light.tokens.value(QStringLiteral("@BG_GLASS@")),
           QStringLiteral("深色 @BG_GLASS@ = %1 与浅色不同")
               .arg(dark.tokens.value(QStringLiteral("@BG_GLASS@"))));

    struct Result {
        QColor pageBg, glass, border, fill;
        CardPass *pass = nullptr;
    };
    Result res[2];

    for (int i = 0; i < 2; ++i) {
        const ThemeInfo *ti = infos[i];
        const QString tag = lights[i] ? QStringLiteral("[LIGHT]") : QStringLiteral("[DARK]");
        const QColor pageBg(ti->tokens.value(QStringLiteral("@BG_BASE@")));
        const QColor glass = parseColorValue(ti->tokens.value(QStringLiteral("@BG_GLASS@")));
        const QColor border(ti->tokens.value(QStringLiteral("@BORDER@")));
        expect(pageBg.isValid() && glass.isValid() && border.isValid(),
               QStringLiteral("%1 页面底色/@BG_GLASS@/@BORDER@ 解析成功（%2 / %3 / %4 → alpha=%5）")
                   .arg(tag, hex(pageBg), ti->tokens.value(QStringLiteral("@BG_GLASS@")), hex(border))
                   .arg(QString::number(glass.alphaF(), 'f', 2)));

        const QColor fill = blendOver(glass, pageBg);
        QString css = qssByTheme.value(ti->name);
        css += QStringLiteral("\nQWidget#probePageHost { background-color: %1; }\n").arg(hex(pageBg));
        qApp->setStyleSheet(css);
        QCoreApplication::processEvents();

        const QString png = QStringLiteral("D:/BlockBox/_out/detail_header_card_%1.png").arg(ti->name);
        CardPass *p = buildAndGrab(QStringLiteral("modDetailHeader"), lights[i], false, png);
        expect(p->img.size() == p->host->size(),
               QStringLiteral("%1 grab() 尺寸 %2x%3 == 窗口 %4x%5（1:1 像素采样）")
                   .arg(tag).arg(p->img.width()).arg(p->img.height())
                   .arg(p->host->width()).arg(p->host->height()));
        info(QStringLiteral("%1 截图: %2").arg(tag, png));

        assertTheme(tag, p, pageBg, glass, border, fill);

        // ── 负控制（同一主题）──
        const QString negPng = lights[i]
            ? QStringLiteral("D:/BlockBox/_out/detail_header_card_light_norule.png")
            : QString();
        CardPass *neg = buildAndGrab(QStringLiteral("modDetailHeaderX"), lights[i], false, negPng);
        assertNegativeControl(tag, neg, pageBg, fill);

        res[i].pageBg = pageBg;
        res[i].glass = glass;
        res[i].border = border;
        res[i].fill = fill;
        res[i].pass = p;

        delete neg;   // 负控制只需结论，删掉避免影响后续主题
    }

    // ── 阴影诊断（仅 INFO，不参与断言）：ModDetailPage 给头部挂 QGraphicsDropShadowEffect ──
    {
        const int i = 0;   // 浅色
        const QString tag = QStringLiteral("[LIGHT-SHADOW]");
        const Result &r = res[i];
        QString css = qssByTheme.value(infos[i]->name);
        css += QStringLiteral("\nQWidget#probePageHost { background-color: %1; }\n").arg(hex(r.pageBg));
        qApp->setStyleSheet(css);
        QCoreApplication::processEvents();

        const QString png = QStringLiteral("D:/BlockBox/_out/detail_header_card_light_shadow.png");
        CardPass *sh = buildAndGrab(QStringLiteral("modDetailHeader"), true, true, png);
        const QRect c = sh->card;
        const int cx = c.center().x();
        info(QStringLiteral("%1 截图: %2").arg(tag, png));

        struct Probe { int x, y; const char *what; };
        const Probe probes[4] = {
            {cx, c.top() + 8, "卡片内部(上内边距)"},
            {cx, c.top() - 6, "卡片上方 6px(阴影带)"},
            {c.left() - 6, c.center().y(), "卡片左侧 6px(阴影带)"},
            {cx, c.bottom() + 6, "卡片下方 6px(阴影带)"},
        };
        for (const Probe &pr : probes) {
            const QColor plain = sampleAt(res[i].pass->img, pr.x, pr.y);
            const QColor shadowed = sampleAt(sh->img, pr.x, pr.y);
            info(QStringLiteral("%1 %2 %3: 无阴影=%4 有阴影=%5 Δ=%6")
                     .arg(tag, QString::fromUtf8(pr.what), ptText(pr.x, pr.y),
                          hex(plain), hex(shadowed))
                     .arg(maxChannelDiff(plain, shadowed)));
        }
        const QColor cornerPlain = sampleAt(res[i].pass->img, c.left() + 1, c.top() + 1);
        const QColor cornerShadow = sampleAt(sh->img, c.left() + 1, c.top() + 1);
        info(QStringLiteral("%1 圆角外侧(%2): 无阴影=%3 有阴影=%4 Δ=%5")
                 .arg(tag, ptText(c.left() + 1, c.top() + 1), hex(cornerPlain), hex(cornerShadow))
                 .arg(maxChannelDiff(cornerPlain, cornerShadow)));
        const QColor innerShadow = sampleAt(sh->img, cx, c.top() + 8);
        info(QStringLiteral("%1 卡片面: 无阴影=%2 有阴影=%3 期望混合色=%4")
                 .arg(tag, hex(r.fill), hex(innerShadow), hex(r.fill)));
        delete sh;
    }

    std::fprintf(stderr, "\n================ 汇总 ================\n");
    std::fprintf(stderr, "PASS=%d FAIL=%d  Qt 样式表解析告警(Could not parse)=%d\n",
                 g_pass, g_fail, g_parseWarnings);
    for (const QString &w : g_parseWarningSamples)
        std::fprintf(stderr, "  [warn] %s\n", qUtf8Printable(w));
    std::fprintf(stderr, "浅色: 页面底色=%s 期望卡片面=%s 边框=%s\n",
                 qUtf8Printable(hex(res[0].pageBg)), qUtf8Printable(hex(res[0].fill)),
                 qUtf8Printable(hex(res[0].border)));
    std::fprintf(stderr, "深色: 页面底色=%s 期望卡片面=%s 边框=%s\n",
                 qUtf8Printable(hex(res[1].pageBg)), qUtf8Printable(hex(res[1].fill)),
                 qUtf8Printable(hex(res[1].border)));
    std::fprintf(stderr, "PNG: D:/BlockBox/_out/detail_header_card_light.png\n"
                         "     D:/BlockBox/_out/detail_header_card_dark.png\n"
                         "     D:/BlockBox/_out/detail_header_card_light_shadow.png\n"
                         "     D:/BlockBox/_out/detail_header_card_light_norule.png\n");

    delete res[0].pass;
    delete res[1].pass;
    return g_fail == 0 ? 0 : 1;
}
