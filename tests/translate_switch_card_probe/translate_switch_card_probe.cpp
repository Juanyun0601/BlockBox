/**
 * @file   translate_switch_card_probe.cpp
 * @brief  共享翻译切换卡片组件（components/TranslateSwitchCard）行为与渲染探针
 *
 * 与 tests/content_translate_card_probe 的区别：那份探针手抄了一份卡片搭建代码，
 * 这份直接链接 components/TranslateSwitchCard.cpp，验证的是真正会被详情页 / 列表页
 * 使用的实现，不会因为镜像代码与实现漂移而失去意义。
 *
 * 覆盖：
 *   1. styles/style.qss 整体花括号配平 + 翻译卡片块内不出现硬编码颜色（保证两套主题都吃 token）；
 *   2. 卡片贴住父控件右下角（右边距 / 下边距 24），EN 默认选中且为主色；
 *   3. 点击「中」发 modeChangeRequested(true)，点击 EN 发 modeChangeRequested(false)；
 *   4. setBusy(true) 显示「AI 翻译中…」并禁用「中」，EN 仍可点击（用于中途取消）；
 *   5. setChineseMode(true) 后「中」为主色、EN 恢复卡片底色；
 *   6. 负控制：按钮 objectName 改名后不再有选中态主色；
 *   7. 列表页批量翻译结果解析（与 pages/ContentDownloadPage.cpp 同规则）：
 *      正常多行 / 带 ``` 包裹 / 带「名称：简介：」前缀 / 缺条目。
 *
 * PNG 输出：D:/BlockBox/_out/translate_switch_card_probe_*.png
 */
#include <QApplication>
#include <QColor>
#include <QFile>
#include <QLabel>
#include <QMap>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "components/TranslateSwitchCard.h"
#include "components/TranslatePayloadCodec.h"

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

/* ── 列表页整页翻译的响应解析：直接链接真实实现（components/TranslatePayloadCodec） ── */
QMap<int, QPair<QString, QString>> parseListTranslation(const QString &text)
{
    return TranslatePayloadCodec::parse(text);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    qInstallMessageHandler(msgHandler);

    const QString root = QStringLiteral("D:/BlockBox/C-BlockBox");
    const QString outDir = QStringLiteral("D:/BlockBox/_out");

    // ── 1. 样式表结构检查 ──
    const QString qss = readAll(root + QStringLiteral("/styles/style.qss"));
    expect(!qss.isEmpty(), QStringLiteral("读取 styles/style.qss 成功（%1 字节）").arg(qss.size()));

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

    const QString blockStart = QStringLiteral("/* 在线资源详情页右下角悬浮翻译卡片");
    const QString blockEnd = QStringLiteral("/* 详情页翻译按钮 */");
    const int startIdx = qss.indexOf(blockStart);
    const int endIdx = startIdx < 0 ? -1 : qss.indexOf(blockEnd, startIdx);
    expect(startIdx >= 0 && endIdx > startIdx,
           QStringLiteral("找到翻译卡片样式块（%1..%2）").arg(startIdx).arg(endIdx));
    if (startIdx < 0 || endIdx <= startIdx)
        return 1;
    const QString block = qss.mid(startIdx, endIdx - startIdx);

    // 块内只允许 @TOKEN@，不允许硬编码 #RRGGBB（否则换主题不跟随）
    static const QRegularExpression hexRe(QStringLiteral("#[0-9a-fA-F]{3,8}\\b"));
    const QStringList hardcoded = [&]() {
        QStringList found;
        QRegularExpressionMatchIterator it = hexRe.globalMatch(block);
        while (it.hasNext())
            found << it.next().captured(0);
        return found;
    }();
    expect(hardcoded.isEmpty(),
           QStringLiteral("卡片样式块不含硬编码颜色（发现 %1 处：%2）")
               .arg(hardcoded.size()).arg(hardcoded.join(QStringLiteral(" "))));

    const QMap<QString, QString> parsedTokens = parseTokens(
        readAll(root + QStringLiteral("/utils/ThemeManager.cpp")));
    QMap<QString, QString> tokens = parsedTokens;
    const QString primary = tokens.value(QStringLiteral("@PRIMARY@"), QStringLiteral("#2E7D32"));
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
    QString resolved;
    int last = 0;
    QStringList usedTokens;
    QRegularExpressionMatchIterator it = tokenRe.globalMatch(block);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        resolved += block.mid(last, m.capturedStart() - last);
        usedTokens << m.captured(0);
        resolved += tokens.value(m.captured(0), QStringLiteral("#808080"));
        last = m.capturedEnd();
    }
    resolved += block.mid(last);
    info(QStringLiteral("块内使用 token：%1").arg(usedTokens.join(QStringLiteral(" "))));

    app.setStyleSheet(resolved);
    expect(g_parseWarnings == 0,
           QStringLiteral("卡片样式块被 Qt 完整解析（解析告警 %1 条）").arg(g_parseWarnings));
    for (const QString &w : g_parseWarningSamples)
        info(QStringLiteral("解析告警样例：%1").arg(w));

    // ── 2. 真实组件：搭一个列表页大小的宿主 ──
    QWidget page;
    page.setObjectName(QStringLiteral("probeRoot"));
    page.setAttribute(Qt::WA_StyledBackground, true);
    page.setStyleSheet(QStringLiteral("QWidget#probeRoot { background-color: #f5f5f5; }"));
    page.resize(1000, 640);

    TranslateSwitchCard *card = new TranslateSwitchCard(&page);
    int requestedTrue = 0;
    int requestedFalse = 0;
    QObject::connect(card, &TranslateSwitchCard::modeChangeRequested, &page,
                     [&](bool chinese) {
        if (chinese)
            ++requestedTrue;
        else
            ++requestedFalse;
    });

    page.show();
    app.processEvents();

    QPushButton *enBtn = card->findChild<QPushButton *>(QStringLiteral("contentTranslateEnBtn"));
    QPushButton *zhBtn = card->findChild<QPushButton *>(QStringLiteral("contentTranslateZhBtn"));
    QLabel *status = card->findChild<QLabel *>(QStringLiteral("contentTranslateStatus"));
    expect(enBtn && zhBtn, QStringLiteral("组件内找到 EN / 中 两个按钮"));
    if (!enBtn || !zhBtn)
        return 1;

    const QColor cardBg(tokens.value(QStringLiteral("@BG_CARD@"), QStringLiteral("#ffffff")));
    const QColor primaryColor(primary);
    const QColor pageBg(QStringLiteral("#f5f5f5"));

    // ── 3. 位置与默认态 ──
    QRect cardRect = card->geometry();
    info(QStringLiteral("卡片几何 %1,%2 %3x%4；page %5x%6")
             .arg(cardRect.x()).arg(cardRect.y())
             .arg(cardRect.width()).arg(cardRect.height())
             .arg(page.width()).arg(page.height()));
    expect(page.width() - (cardRect.x() + cardRect.width()) == 24
               && page.height() - (cardRect.y() + cardRect.height()) == 24,
           QStringLiteral("卡片停在右下角（右边距 %1 / 下边距 %2）")
               .arg(page.width() - (cardRect.x() + cardRect.width()))
               .arg(page.height() - (cardRect.y() + cardRect.height())));
    expect(card->isChineseMode() == false, QStringLiteral("默认 EN 选中"));

    QPixmap shot = page.grab();
    shot.save(outDir + QStringLiteral("/translate_switch_card_probe_en.png"));

    // 按钮内靠左空白处取样：中心像素会被文字笔画与抗锯齿污染
    const QPoint enProbe = enBtn->mapTo(&page, QPoint(6, enBtn->height() / 2));
    const QPoint zhProbe = zhBtn->mapTo(&page, QPoint(6, zhBtn->height() / 2));
    expect(colorClose(shot.toImage().pixelColor(enProbe), primaryColor),
           QStringLiteral("EN 选中为主色 %1（实测 %2）")
               .arg(primaryColor.name(), shot.toImage().pixelColor(enProbe).name()));
    expect(colorClose(shot.toImage().pixelColor(zhProbe), cardBg),
           QStringLiteral("「中」未选中为卡片底色 %1（实测 %2）")
               .arg(cardBg.name(), shot.toImage().pixelColor(zhProbe).name()));
    expect(colorClose(shot.toImage().pixelColor(cardRect.center().x(), cardRect.top() + 2), cardBg),
           QStringLiteral("卡片内边距处为卡片底色"));

    // ── 4. 交互：点「中」→ 请求翻译；busy 态 ──
    zhBtn->click();
    app.processEvents();
    expect(requestedTrue == 1, QStringLiteral("点「中」发出 modeChangeRequested(true)"));
    expect(zhBtn->isChecked() && !enBtn->isChecked(), QStringLiteral("点「中」后选中态切到「中」"));

    card->setBusy(true);
    app.processEvents();
    expect(card->isBusy() && status && status->isVisible(),
           QStringLiteral("busy 时显示「AI 翻译中…」"));
    expect(!zhBtn->isEnabled(), QStringLiteral("busy 时「中」被禁用"));
    expect(enBtn->isEnabled(), QStringLiteral("busy 时 EN 仍可点（用于中途取消）"));

    shot = page.grab();
    shot.save(outDir + QStringLiteral("/translate_switch_card_probe_busy.png"));

    // ── 5. 落定到中文态 ──
    card->setBusy(false);
    card->setChineseMode(true);
    app.processEvents();
    const QPixmap zhShot = page.grab();
    zhShot.save(outDir + QStringLiteral("/translate_switch_card_probe_zh.png"));

    enBtn->setChecked(false);
    zhBtn->setChecked(true);
    app.processEvents();
    const QPoint zhProbe2 = zhBtn->mapTo(&page, QPoint(6, zhBtn->height() / 2));
    const QPoint enProbe2 = enBtn->mapTo(&page, QPoint(6, enBtn->height() / 2));
    expect(colorClose(page.grab().toImage().pixelColor(zhProbe2), primaryColor),
           QStringLiteral("中文态「中」为主色（实测 %1）")
               .arg(page.grab().toImage().pixelColor(zhProbe2).name()));
    expect(colorClose(page.grab().toImage().pixelColor(enProbe2), cardBg),
           QStringLiteral("中文态 EN 恢复卡片底色（实测 %1）")
               .arg(page.grab().toImage().pixelColor(enProbe2).name()));

    // ── 6. 点 EN 回退 ──
    enBtn->click();
    app.processEvents();
    expect(requestedFalse == 1, QStringLiteral("点 EN 发出 modeChangeRequested(false)"));
    expect(!card->isChineseMode(), QStringLiteral("点 EN 后回到英文态"));

    // 按钮 setChecked 由组件内部完成，这里再确认一次真实点击路径
    zhBtn->click();
    app.processEvents();
    expect(requestedTrue == 2, QStringLiteral("再次点「中」会再次发出请求"));

    // ── 7. 跟随父控件尺寸变化 ──
    page.resize(760, 520);
    app.processEvents();
    cardRect = card->geometry();
    expect(page.width() - (cardRect.x() + cardRect.width()) == 24
               && page.height() - (cardRect.y() + cardRect.height()) == 24,
           QStringLiteral("父控件缩放后仍贴住右下角（右边距 %1 / 下边距 %2）")
               .arg(page.width() - (cardRect.x() + cardRect.width()))
               .arg(page.height() - (cardRect.y() + cardRect.height())));

    // ── 8. 负控制：objectName 改名后没有选中态主色 ──
    QWidget *norule = new QWidget(&page);
    norule->setObjectName(QStringLiteral("contentTranslateCardRenamed"));
    norule->setAttribute(Qt::WA_StyledBackground, true);
    QPushButton *renamed = new QPushButton(QStringLiteral("EN"), norule);
    renamed->setObjectName(QStringLiteral("contentTranslateEnBtnRenamed"));
    renamed->setCheckable(true);
    renamed->setChecked(true);
    renamed->setFixedHeight(28);
    renamed->move(4, 4);
    norule->setGeometry(40, 40, 120, 36);
    norule->show();
    app.processEvents();
    const QColor norulePixel = page.grab().toImage().pixelColor(
        renamed->mapTo(&page, QPoint(6, renamed->height() / 2)));
    expect(!colorClose(norulePixel, primaryColor),
           QStringLiteral("负控制：按钮改名后无主色选中态（实测 %1）").arg(norulePixel.name()));

    // ── 9. 列表批量翻译结果解析 ──
    const QString clean = QStringLiteral(
        "[#0#] Complementary Shaders ||| A shader pack with high quality lighting.\n"
        "[#1#] Fresh Animations ||| Makes mob animations smoother.");
    QMap<int, QPair<QString, QString>> r = parseListTranslation(clean);
    expect(r.size() == 2 && r.value(0).first == QStringLiteral("Complementary Shaders")
               && r.value(0).second.startsWith(QStringLiteral("A shader pack")),
           QStringLiteral("解析：正常多行（%1 条）").arg(r.size()));

    const QString fenced = QStringLiteral("```\n[#0#] Iris Shaders ||| Modern shader loader.\n```");
    r = parseListTranslation(fenced);
    expect(r.size() == 1 && r.value(0).first == QStringLiteral("Iris Shaders")
               && r.value(0).second == QStringLiteral("Modern shader loader."),
           QStringLiteral("解析：去掉 ``` 代码块包裹"));

    const QString prefixed = QStringLiteral("[#0#] 名称：互补光影 简介：高质量光照的光影包");
    r = parseListTranslation(prefixed);
    expect(r.size() == 1 && r.value(0).first == QStringLiteral("互补光影")
               && r.value(0).second == QStringLiteral("高质量光照的光影包"),
           QStringLiteral("解析：容忍「名称：」「简介：」前缀（名称=%1 简介=%2）")
               .arg(r.value(0).first, r.value(0).second));

    const QString missing = QStringLiteral("[#0#] A ||| a\n[#2#] C ||| c");
    r = parseListTranslation(missing);
    expect(r.size() == 2 && r.contains(0) && r.contains(2) && !r.contains(1),
           QStringLiteral("解析：缺条目时按序号落位，不串行"));

    const QString junk = QStringLiteral("抱歉，我无法翻译这些内容。");
    r = parseListTranslation(junk);
    expect(r.isEmpty(), QStringLiteral("解析：模型没按格式返回时结果为空（页面会提示格式异常）"));

    std::fprintf(stderr, "\n[summary] pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
