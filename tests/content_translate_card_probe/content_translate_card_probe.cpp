/**
 * @file   content_translate_card_probe.cpp
 * @brief  在线资源详情页右下角悬浮翻译卡片（EN / 中）独立渲染与像素探针
 *
 * 被验证的实现（只读参考，探针不修改它们）：
 *   - styles/style.qss:
 *       QWidget#contentTranslateCard / QLabel#contentTranslateStatus /
 *       QPushButton#contentTranslateEnBtn, QPushButton#contentTranslateZhBtn
 *   - components/TranslateSwitchCard.cpp（详情页与列表页共用的悬浮翻译切换卡片）
 *       内边距 4/间距 2、按钮高 28、卡片圆角 18px，
 *       位置固定在右下角（右边距 24、下边距 24），EN 默认选中。
 *
 * 注意：本探针手抄了一份卡片搭建代码。直接验证真实组件的探针是
 * tests/translate_switch_card_probe（链接 components/TranslateSwitchCard.cpp），
 * 排查组件行为问题时优先用它。
 *
 * 探针做法（不链接 ThemeManager，避免 BackgroundManager/PluginManager 依赖）：
 *   1. 读 styles/style.qss 原文，先做全文件花括号配平检查（防止整张样式表被打断）；
 *   2. 只截取「在线资源详情页右下角悬浮翻译卡片」到「详情页翻译按钮」之间的样式块；
 *   3. 用 utils/ThemeManager.cpp 里的真实取值替换块内用到的 @TOKEN@（浅色分支），
 *      少数由主色派生的 token 按 ThemeManager 同样的算法就地算；
 *   4. 按真实层级/objectName 搭出卡片放在 900x600 画布右下角，grab() 出 PNG；
 *   5. 程序化像素断言 + 负控制（卡片 objectName 改名后选中态断言必须失败）。
 *
 * PNG 输出：D:/BlockBox/_out/content_translate_card_probe.png
 *          D:/BlockBox/_out/content_translate_card_probe_norule.png（负控制证据）
 */
#include <QApplication>
#include <QColor>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QWidget>

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

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

bool colorClose(const QColor &a, const QColor &b, int tol = 14)
{
    if (!a.isValid() || !b.isValid())
        return false;
    return qAbs(a.red() - b.red()) <= tol
        && qAbs(a.green() - b.green()) <= tol
        && qAbs(a.blue() - b.blue()) <= tol;
}

/// 从 ThemeManager.cpp 里取简单字面量 token（第一个出现的字符串即浅色分支）
QMap<QString, QString> parseTokens(const QString &themeManagerSource)
{
    QMap<QString, QString> tokens;
    static const QRegularExpression re(
        QStringLiteral("T\\[\"(@[A-Z0-9_]+@)\"\\]\\s*=\\s*([^;]+);"));
    QRegularExpressionMatchIterator it = re.globalMatch(themeManagerSource);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString token = m.captured(1);
        if (tokens.contains(token))
            continue;
        static const QRegularExpression strRe(QStringLiteral("\"([^\"]*)\""));
        const QRegularExpressionMatch sm = strRe.match(m.captured(2));
        if (sm.hasMatch())
            tokens.insert(token, sm.captured(1));
    }
    return tokens;
}

/// 解析后的卡片控件集合
struct CardParts {
    QWidget *card = nullptr;
    QLabel *status = nullptr;
    QPushButton *en = nullptr;
    QPushButton *zh = nullptr;
};

/// 与 pages/ContentDetailPage.cpp::buildTranslateCard() 保持一致
CardParts buildCard(QWidget *parent, const QString &cardObjectName)
{
    CardParts parts;
    parts.card = new QWidget(parent);
    parts.card->setObjectName(cardObjectName);
    parts.card->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *layout = new QHBoxLayout(parts.card);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    parts.status = new QLabel(QStringLiteral("AI 翻译中…"), parts.card);
    parts.status->setObjectName(QStringLiteral("contentTranslateStatus"));
    parts.status->hide();
    layout->addWidget(parts.status);

    auto makeSegmentBtn = [&](const QString &text, const QString &objectName) {
        QPushButton *btn = new QPushButton(text, parts.card);
        btn->setObjectName(objectName);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        btn->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(btn);
        return btn;
    };

    parts.en = makeSegmentBtn(QStringLiteral("EN"), QStringLiteral("contentTranslateEnBtn"));
    parts.zh = makeSegmentBtn(QStringLiteral("中"), QStringLiteral("contentTranslateZhBtn"));
    parts.en->setChecked(true);

    auto *shadow = new QGraphicsDropShadowEffect(parts.card);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 46));
    parts.card->setGraphicsEffect(shadow);

    const int margin = 24;
    const QSize hint = parts.card->sizeHint();
    const int cardW = qMax(hint.width(), 96);
    const int cardH = qMax(hint.height(), 36);
    parts.card->setGeometry(qMax(0, parent->width() - cardW - margin),
                            qMax(0, parent->height() - cardH - margin),
                            cardW, cardH);
    parts.card->show();
    return parts;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    qInstallMessageHandler(msgHandler);

    const QString root = QStringLiteral("D:/BlockBox/C-BlockBox");
    const QString qssPath = root + QStringLiteral("/styles/style.qss");
    const QString themePath = root + QStringLiteral("/utils/ThemeManager.cpp");

    const QString qss = readAll(qssPath);
    expect(!qss.isEmpty(), QStringLiteral("读取 styles/style.qss 成功（%1 字节）").arg(qss.size()));

    // ── 1. 全文件花括号配平（样式表被打断会让其后所有规则失效） ──
    int depth = 0;
    int minDepth = 0;
    for (const QChar &ch : qss) {
        if (ch == QLatin1Char('{'))
            ++depth;
        else if (ch == QLatin1Char('}')) {
            --depth;
            minDepth = qMin(minDepth, depth);
        }
    }
    expect(depth == 0 && minDepth == 0,
           QStringLiteral("style.qss 花括号配平（结束深度 %1 / 最低深度 %2）").arg(depth).arg(minDepth));

    // ── 2. 截取翻译卡片样式块 ──
    const QString blockStart = QStringLiteral("/* 在线资源详情页右下角悬浮翻译卡片");
    const QString blockEnd = QStringLiteral("/* 详情页翻译按钮 */");
    const int startIdx = qss.indexOf(blockStart);
    const int endIdx = startIdx < 0 ? -1 : qss.indexOf(blockEnd, startIdx);
    expect(startIdx >= 0 && endIdx > startIdx,
           QStringLiteral("找到翻译卡片样式块（%1..%2）").arg(startIdx).arg(endIdx));
    if (startIdx < 0 || endIdx <= startIdx)
        return 1;
    QString block = qss.mid(startIdx, endIdx - startIdx);

    // ── 3. 替换块内 token（浅色主题取值） ──
    const QMap<QString, QString> parsed = parseTokens(readAll(themePath));
    QMap<QString, QString> tokens = parsed;
    const QString primary = tokens.value(QStringLiteral("@PRIMARY@"), QStringLiteral("#2E7D32"));
    // 由主色派生的 token：与 ThemeManager 同样的算法
    tokens[QStringLiteral("@PRIMARY@")] = primary;
    tokens[QStringLiteral("@PRIMARY_MEDIUM@")] = QColor(primary).lighter(120).name();
    tokens[QStringLiteral("@PRIMARY_BG@")] = QColor(primary).lighter(180).name();
    tokens[QStringLiteral("@TEXT_ACCENT@")] = QColor(primary).darker(140).name();
    tokens[QStringLiteral("@TEXT_PRIMARY@")] = QStringLiteral("#333333");
    tokens[QStringLiteral("@TEXT_BORDER@")] = QStringLiteral("#e8e8e8");
    tokens[QStringLiteral("@SECONDARY@")] = QStringLiteral("#256428");
    tokens[QStringLiteral("@BG_SECONDARY@")] = QStringLiteral("#f8f9fa");
    tokens[QStringLiteral("@BORDER_DEFAULT@")] = QStringLiteral("#e8eaed");

    const QRegularExpression tokenRe(QStringLiteral("@([A-Z0-9_]+)@"));
    QRegularExpressionMatchIterator it = tokenRe.globalMatch(block);
    QStringList usedTokens;
    int last = 0;
    QString resolved;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        resolved += block.mid(last, m.capturedStart() - last);
        const QString token = m.captured(0);
        usedTokens << token;
        resolved += tokens.value(token, QStringLiteral("#808080"));
        last = m.capturedEnd();
    }
    resolved += block.mid(last);
    info(QStringLiteral("块内使用 token：%1").arg(usedTokens.join(QStringLiteral(" "))));

    // ── 4. 应用样式表并检查解析告警 ──
    app.setStyleSheet(resolved);
    expect(g_parseWarnings == 0,
           QStringLiteral("卡片样式块被 Qt 完整解析（解析告警 %1 条）").arg(g_parseWarnings));
    for (const QString &w : g_parseWarningSamples)
        info(QStringLiteral("解析告警样例：%1").arg(w));

    // ── 5. 搭出画布与卡片 ──
    QWidget page;
    page.setObjectName(QStringLiteral("probeRoot"));
    page.setAttribute(Qt::WA_StyledBackground, true);
    page.setStyleSheet(QStringLiteral("QWidget#probeRoot { background-color: #f5f5f5; }"));
    page.resize(900, 600);

    CardParts parts = buildCard(&page, QStringLiteral("contentTranslateCard"));
    page.show();
    app.processEvents();

    const QRect cardRect = parts.card->geometry();
    const QColor cardBg(tokens.value(QStringLiteral("@BG_CARD@"), QStringLiteral("#ffffff")));
    const QColor primaryColor(primary);
    const QColor pageBg(QStringLiteral("#f5f5f5"));

    info(QStringLiteral("卡片几何 %1,%2 %3x%4；page %5x%6")
             .arg(cardRect.x()).arg(cardRect.y())
             .arg(cardRect.width()).arg(cardRect.height())
             .arg(page.width()).arg(page.height()));

    const QPixmap shot = page.grab();
    const QString outDir = QStringLiteral("D:/BlockBox/_out");
    shot.save(outDir + QStringLiteral("/content_translate_card_probe.png"));

    // ── 6. 像素断言 ──
    const int rightMargin = page.width() - (cardRect.x() + cardRect.width());
    const int bottomMargin = page.height() - (cardRect.y() + cardRect.height());
    expect(rightMargin == 24 && bottomMargin == 24,
           QStringLiteral("卡片停在右下角（右边距 %1 / 下边距 %2，期望 24/24）")
               .arg(rightMargin).arg(bottomMargin));

    const QColor padPixel = shot.toImage().pixelColor(cardRect.center().x(), cardRect.top() + 2);
    expect(colorClose(padPixel, cardBg),
           QStringLiteral("卡片内边距处为卡片底色 %1（实测 %2）")
               .arg(cardBg.name(), padPixel.name()));

    // 采样点取按钮内靠左的空白处：按钮中心会被文字笔画/抗锯齿覆盖，不能作为底色证据
    const QPoint enProbe = parts.en->mapTo(&page, QPoint(6, parts.en->height() / 2));
    const QPoint zhProbe = parts.zh->mapTo(&page, QPoint(6, parts.zh->height() / 2));
    const QColor enPixel = shot.toImage().pixelColor(enProbe);
    expect(colorClose(enPixel, primaryColor),
           QStringLiteral("EN 默认选中显示主色 %1（实测 %2）")
               .arg(primaryColor.name(), enPixel.name()));

    const QColor zhPixel = shot.toImage().pixelColor(zhProbe);
    expect(colorClose(zhPixel, cardBg),
           QStringLiteral("「中」未选中显示卡片底色 %1（实测 %2）")
               .arg(cardBg.name(), zhPixel.name()));

    const QColor cornerPixel = shot.toImage().pixelColor(cardRect.left() + 1, cardRect.top() + 1);
    expect(!colorClose(cornerPixel, cardBg, 6),
           QStringLiteral("卡片左上角已圆角（角落实测 %1 ≠ 底色 %2）")
               .arg(cornerPixel.name(), cardBg.name()));

    const QColor outsidePixel = shot.toImage().pixelColor(cardRect.left() - 8, cardRect.top() - 20);
    expect(colorClose(outsidePixel, pageBg),
           QStringLiteral("卡片之外仍是页面底色（实测 %1）").arg(outsidePixel.name()));

    // ── 7. 负控制：按钮 objectName 改名后主色断言必须失败 ──
    QWidget *norule = new QWidget(&page);
    norule->setObjectName(QStringLiteral("contentTranslateCard"));
    norule->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout *nl = new QHBoxLayout(norule);
    nl->setContentsMargins(4, 4, 4, 4);
    nl->setSpacing(2);
    QPushButton *enNoRule = new QPushButton(QStringLiteral("EN"), norule);
    enNoRule->setObjectName(QStringLiteral("contentTranslateEnBtnRenamed"));
    enNoRule->setCheckable(true);
    enNoRule->setChecked(true);
    enNoRule->setFixedHeight(28);
    nl->addWidget(enNoRule);
    norule->setGeometry(40, 40, 120, 36);
    norule->show();
    app.processEvents();
    const QPixmap noruleShot = page.grab();
    noruleShot.save(outDir + QStringLiteral("/content_translate_card_probe_norule.png"));
    const QColor norulePixel = noruleShot.toImage().pixelColor(
        enNoRule->mapTo(&page, QPoint(6, enNoRule->height() / 2)));
    expect(!colorClose(norulePixel, primaryColor),
           QStringLiteral("负控制：按钮 objectName 改名后无主色选中态（实测 %1）").arg(norulePixel.name()));

    std::fprintf(stderr, "\n[summary] pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
