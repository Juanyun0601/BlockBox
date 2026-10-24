/**
 * @file   probe.cpp
 * @brief  详情页版本卡片「下载」图标可读性探针（真实控件 + 真实样式表，离屏渲染）
 *
 * 复现问题：pages/ContentDetailPage.cpp 的卡片下载按钮底色是主题色（@PRIMARY@），
 * 图标却用同一个主题色绘制，等于同色叠同色 —— 按钮位置只剩一块纯色方块。
 *
 * 断言：
 *   1) 资源包详情页存在下载按钮
 *   2) 图标像素确实被画出来了（与底色有明显差异）
 *   3) 图标与底色的对比度足够（白色图标 ≈ 高对比）
 *
 * 构建：见 build.sh（AFTER=1 跑当前源码；BEFORE=1 用对象目录里的旧 ContentDetailPage.o）
 * 运行：QT_QPA_PLATFORM=offscreen probe.exe
 */
#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>

#include <cmath>
#include <cstdio>

#include "pages/ContentDetailPage.h"
#include "utils/ThemeManager.h"
#include "utils/mod/ModData.h"

namespace {

int g_fail = 0;

void check(bool ok, const QString &what)
{
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.toLocal8Bit().constData());
    if (!ok) ++g_fail;
}

double relativeLuminance(const QColor &c)
{
    auto chan = [](double v) {
        v /= 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * chan(c.red()) + 0.7152 * chan(c.green()) + 0.0722 * chan(c.blue());
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    const double hi = qMax(la, lb), lo = qMin(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

ModVersionFile makeFile(const QString &version)
{
    ModVersionFile file;
    file.version = version;
    file.gameVersions = QStringList{QStringLiteral("1.21.4")};
    file.releaseType = QStringLiteral("release");
    file.fileName = version + QStringLiteral(".zip");
    file.downloadUrl = QStringLiteral("https://example.invalid/") + file.fileName;
    return file;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 真实主题 + 真实样式表（@PRIMARY@ 等占位符由 ThemeManager 替换）
    ThemeManager::setTheme(&app, "dark");
    const QColor primary(ThemeManager::instance()->currentThemeColor());

    ModInfo info;
    info.id = QStringLiteral("probe_resourcepack_icon");
    info.name = QStringLiteral("Probe 资源包");
    info.source = QStringLiteral("modrinth");
    info.gameVersions = QStringList{QStringLiteral("1.21.4")};
    info.latestVersion = QStringLiteral("1.0.0");
    info.versionFiles << makeFile(QStringLiteral("1.0.0"))
                      << makeFile(QStringLiteral("0.9.0"));

    ContentDetailPage page(ContentType::ResourcePack);
    page.setModInfo(info);
    page.resize(1100, 760);
    page.show();
    app.processEvents();

    QList<QPushButton *> dlButtons;
    const QList<QWidget *> widgets = page.findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (w->objectName() == QLatin1String("modDetailDownloadBtn"))
            dlButtons.append(qobject_cast<QPushButton *>(w));
    }
    check(!dlButtons.isEmpty(),
          QStringLiteral("资源包详情页存在下载按钮（%1 个）").arg(dlButtons.size()));
    if (dlButtons.isEmpty())
        return 1;

    QPushButton *btn = dlButtons.first();
    const QPixmap shot = btn->grab();
    const QImage img = shot.toImage().convertToFormat(QImage::Format_ARGB32);

    // 取按钮中心区域里出现次数最多的颜色当底色，差异明显的像素算作图标笔画
    const QColor bg = QColor(img.pixel(img.width() / 2, 2));
    long iconPixels = 0;
    double r = 0, g = 0, b = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = img.pixel(x, y);
            if (qAlpha(c) < 8)
                continue;
            const QColor cc(c);
            const int diff = qAbs(cc.red() - bg.red()) + qAbs(cc.green() - bg.green())
                           + qAbs(cc.blue() - bg.blue());
            if (diff > 90) {
                ++iconPixels;
                r += cc.red();
                g += cc.green();
                b += cc.blue();
            }
        }
    }
    const QColor iconColor(iconPixels ? int(r / iconPixels) : 0,
                           iconPixels ? int(g / iconPixels) : 0,
                           iconPixels ? int(b / iconPixels) : 0);
    const double contrast = iconPixels ? contrastRatio(iconColor, bg) : 1.0;

    std::printf("  按钮尺寸 %dx%d 底色 %s 图标色 %s 图标像素 %ld 对比度 %.2f\n",
                img.width(), img.height(), qPrintable(bg.name()),
                qPrintable(iconColor.name()), iconPixels, contrast);

    check(iconPixels > 20, QStringLiteral("下载图标被画出来了（图标像素 %1）").arg(iconPixels));
    check(contrast >= 2.5, QStringLiteral("图标与按钮底色对比度 ≥ 2.5（实际 %1）").arg(contrast, 0, 'f', 2));

    // 保存按钮放大图，便于人工核对观感
    const int scale = 8;
    QImage big(img.width() * scale, img.height() * scale, QImage::Format_ARGB32);
    big.fill(QColor("#101010"));
    for (int y = 0; y < big.height(); ++y) {
        for (int x = 0; x < big.width(); ++x) {
            big.setPixel(x, y, img.pixel(x / scale, y / scale));
        }
    }
    big.save(QStringLiteral("D:/BlockBox/C-BlockBox/tests/detail_download_icon_probe/button.png"));
    std::printf("  primary=%s  截图: tests/detail_download_icon_probe/button.png\n",
                qPrintable(primary.name()));

    if (g_fail == 0)
        std::printf("\nALL CHECKS PASSED\n");
    else
        std::printf("\n%d CHECK(S) FAILED\n", g_fail);
    std::printf("fail=%d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
