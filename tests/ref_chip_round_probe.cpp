/**
 * @file   ref_chip_round_probe.cpp
 * @brief  AI 助手页引用标签（QFrame#aiChatRefChip）圆角端到端探针
 *
 * 加载真实 style.qss（token 解析后），渲染真实 objectName 的引用标签，
 * 截图到 _out/ref_chip_round_*.png，并用角点/中心像素对比判断圆角是否生效：
 *  - 角点像素 == 标签底色  => 直角（圆角未生效，Qt 对超尺寸 border-radius 不钳制）
 *  - 角点像素 != 标签底色  => 圆角生效
 *
 * 用法：qmake ref_chip_round_probe.pro && mingw32-make && ./ref_chip_round_probe.exe
 */

#include <QApplication>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QRegularExpression>
#include <QTimer>
#include <QWidget>

#include <cstdio>

namespace {

// 与 ThemeManager::buildTokenTable 同族的精简浅色 token 表；
// 未登记 token 统一置灰，避免 QSS 解析告警
QString resolveTokens(QString style)
{
    struct KV { const char *k; const char *v; };
    const KV t[] = {
        {"@PRIMARY_BG@", "#e8f5e9"}, {"@TEXT_ACCENT@", "#1b5e20"},
        {"@TEXT_SECONDARY@", "#666666"}, {"@BG_BASE@", "#f5f5f5"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

QFrame *buildChip(QWidget *parent)
{
    // 与 AiChatPage::addReferenceChip 相同的 objectName / 布局 / 字号
    auto *chip = new QFrame(parent);
    chip->setObjectName(QStringLiteral("aiChatRefChip"));
    auto *lay = new QHBoxLayout(chip);
    lay->setContentsMargins(8, 2, 4, 2);
    lay->setSpacing(4);
    auto *label = new QLabel(QStringLiteral("[1] [模组] Sodium"), chip);
    label->setObjectName(QStringLiteral("aiChatRefChipLabel"));
    lay->addWidget(label);
    auto *closeBtn = new QLabel(QStringLiteral("×"), chip);
    closeBtn->setObjectName(QStringLiteral("aiChatRefChipLabel"));
    closeBtn->setFixedSize(18, 18);
    closeBtn->setAlignment(Qt::AlignCenter);
    lay->addWidget(closeBtn);
    return chip;
}

// 角点取样：圆角时角点与标签外部像素一致（露出窗口背景）；直角时角点是描边色
bool cornerRounded(const QImage &img, int x0, int y0)
{
    const QColor corner = img.pixelColor(x0, y0);
    const QColor outside = img.pixelColor(x0 - 3, y0 - 3);
    std::fprintf(stderr, "  corner=%s outside=%s\n",
                 qUtf8Printable(corner.name()),
                 qUtf8Printable(outside.name()));
    return corner == outside;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString style = resolveTokens(QString::fromUtf8(f.readAll()));
    qApp->setStyleSheet(style);

    QWidget win;
    win.setStyleSheet(QStringLiteral("background: #f5f5f5;"));
    win.setFixedSize(260, 90);
    auto *root = new QHBoxLayout(&win);
    root->setContentsMargins(20, 20, 20, 20);
    auto *chip = buildChip(&win);
    root->addWidget(chip);
    win.show();
    app.processEvents();

    chip->resize(chip->sizeHint());
    app.processEvents();

    const QImage full = win.grab().toImage();
    const QString out = QStringLiteral("D:/BlockBox/_out/ref_chip_round.png");
    const bool ok = full.save(out);
    std::fprintf(stderr, "[%s] %s (%dx%d)\n", ok ? "ok" : "FAIL",
                 qUtf8Printable(out), full.width(), full.height());

    // 标签在 win 内的偏移 = 布局边距 20（chip 顶边贴内容区）
    const int cx = 20, cy = 20;
    const bool rounded = cornerRounded(full, cx, cy);
    std::fprintf(stderr, "[verdict] chip corner is %s\n",
                 rounded ? "ROUNDED (radius effective)" : "SQUARE (radius NOT applied)");

    QTimer::singleShot(50, &app, &QCoreApplication::quit);
    return app.exec();
}
