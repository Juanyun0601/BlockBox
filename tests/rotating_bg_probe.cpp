/**
 * @file   rotating_bg_probe.cpp
 * @brief  「旋转」背景模式黑底问题探针
 *
 * 场景：BackgroundWidget 进入 Rotating 模式后，若既没有内置场景名、
 * 也没有可用的自定义图片（例如设置里选了「自定义图片」却没选图），
 * GL 天空盒控件会以未加载贴图的状态铺满整个窗口，表现为一片黑底。
 *
 * 探针依次构造若干组配置，真正显示窗口后用 PrintWindow 抓整窗像素，
 * 统计近黑像素比例，输出 PASS/FAIL。
 */
#include "BackgroundWidget.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <QDebug>

#include <windows.h>

namespace {

struct CaseConfig {
    QString label;
    QString scene;      // 内置场景名（空串 = 自定义图片）
    QString imagePath;  // 自定义图片路径
};

QImage captureWindow(HWND hwnd)
{
    RECT rc {};
    if (!GetWindowRect(hwnd, &rc))
        return QImage();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return QImage();

    BITMAPINFO bmi {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bmp = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(memDc, bmp);
    PrintWindow(hwnd, memDc, 2 /*PW_RENDERFULLCONTENT*/);
    SelectObject(memDc, old);

    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    if (bits)
        memcpy(img.bits(), bits, size_t(w) * size_t(h) * 4);

    DeleteObject(bmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return img;
}

struct Stats {
    double meanLuma = 0.0;
    double darkRatio = 0.0;  // 亮度 < 30 的采样点占比
};

// 两帧之间发生明显变化的采样点占比，用于判断全景是否还在转动
double frameDiffRatio(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull() || a.size() != b.size())
        return 1.0;
    const double scale = double(a.height()) / 620.0;
    const int step = qMax(2, int(6 * scale));
    int changed = 0;
    int total = 0;
    for (int y = 0; y < a.height(); y += step) {
        for (int x = 0; x < a.width(); x += step) {
            const QRgb ca = a.pixel(x, y);
            const QRgb cb = b.pixel(x, y);
            const int d = qAbs(qRed(ca) - qRed(cb)) + qAbs(qGreen(ca) - qGreen(cb))
                          + qAbs(qBlue(ca) - qBlue(cb));
            if (d > 12)
                ++changed;
            ++total;
        }
    }
    return total > 0 ? double(changed) / double(total) : 0.0;
}

Stats analyze(const QImage &img)
{
    Stats s;
    if (img.isNull())
        return s;

    // 跳过四条边（窗口阴影/圆角），只统计中间区域
    const int x0 = img.width() / 10;
    const int x1 = img.width() - x0;
    const int y0 = img.height() / 10;
    const int y1 = img.height() - y0;
    if (x1 <= x0 || y1 <= y0)
        return s;

    const int stepX = qMax(1, (x1 - x0) / 60);
    const int stepY = qMax(1, (y1 - y0) / 40);
    long long sum = 0;
    long long dark = 0;
    long long total = 0;
    for (int y = y0; y < y1; y += stepY) {
        for (int x = x0; x < x1; x += stepX) {
            const QRgb c = img.pixel(x, y);
            const int luma = (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
            sum += luma;
            if (luma < 30)
                ++dark;
            ++total;
        }
    }
    if (total > 0) {
        s.meanLuma = double(sum) / double(total);
        s.darkRatio = double(dark) / double(total);
    }
    return s;
}

// 处理事件一段时间，让界面真正跑起来
void pumpEvents(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString outDir = QStringLiteral("D:/BlockBox/build_shots/rotating_probe");
    QDir().mkpath(outDir);
    const QString shotDir = outDir + QStringLiteral("/shots");
    QDir().mkpath(shotDir);

    const QVector<CaseConfig> cases = {
        { QStringLiteral("内置场景 overworld"), QStringLiteral("overworld"), QString() },
        { QStringLiteral("内置实景场景 alps_field（单张等距图）"), QStringLiteral("alps_field"), QString() },
        { QStringLiteral("自定义图片未选图（场景名与图片都为空）"), QString(), QString() },
        { QStringLiteral("场景名无效（nosuchscene）"), QStringLiteral("nosuchscene"), QString() },
        { QStringLiteral("自定义图片路径不存在"), QString(), QStringLiteral("D:/no_such_dir/no_such.png") },
        { QStringLiteral("自定义图片有效"), QString(),
          QStringLiteral(":/Images/Panorama/cherry/panorama_0.jpg") },
    };

    int failed = 0;
    int caseCount = cases.size();
    QStringList report;

    for (int i = 0; i < cases.size(); ++i) {
        const CaseConfig &c = cases.at(i);

        // 每种配置都用全新的窗口，模拟启动器重新启动后的状态
        auto *window = new QWidget();
        window->setWindowTitle(QStringLiteral("rotating-bg-probe"));
        window->resize(900, 620);
        auto *bg = new BackgroundWidget(window);
        bg->setGeometry(window->rect());
        bg->lower();
        window->show();

        // 与 MainWindow::updateBackgroundWidget 相同的调用顺序
        bg->setMode(BackgroundWidget::Rotating);
        bg->setBlurRadius(0);
        bg->setPanoramaScene(c.scene);
        bg->setImage(c.imagePath);

        // 给 GL 天空盒留出加载与绘制的时间
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1500)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        const QImage shot = captureWindow(hwnd);
        const Stats st = analyze(shot);

        const QString fileName = QStringLiteral("%1/%2.png").arg(shotDir).arg(i);
        if (!shot.isNull())
            shot.save(fileName);

        const bool blank = (st.meanLuma < 25.0 && st.darkRatio > 0.9);
        if (blank)
            ++failed;

        report << QStringLiteral("[%1] %2 | meanLuma=%3 darkRatio=%4")
                      .arg(blank ? QStringLiteral("FAIL ") : QStringLiteral("PASS "),
                           c.label)
                      .arg(st.meanLuma, 0, 'f', 1)
                      .arg(st.darkRatio, 0, 'f', 3);

        window->hide();
        delete window;
        QCoreApplication::processEvents();
    }

    // 运行中切换：主世界 → 自定义图片（未选图），即用户在设置里改成「自定义图片」却没选图的情形
    {
        auto *window = new QWidget();
        window->setWindowTitle(QStringLiteral("rotating-bg-probe-switch"));
        window->resize(900, 620);
        auto *bg = new BackgroundWidget(window);
        bg->setGeometry(window->rect());
        bg->lower();
        window->show();

        bg->setMode(BackgroundWidget::Rotating);
        bg->setBlurRadius(0);
        bg->setPanoramaScene(QStringLiteral("overworld"));
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 800)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        // 用户把场景改成「自定义图片」，但没有指定图片
        bg->setPanoramaScene(QString());
        bg->setImage(QString());
        t.restart();
        while (t.elapsed() < 1200)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        const QImage shot = captureWindow(reinterpret_cast<HWND>(window->winId()));
        const Stats st = analyze(shot);
        if (!shot.isNull())
            shot.save(QStringLiteral("%1/switch.png").arg(shotDir));

        const bool blank = (st.meanLuma < 25.0 && st.darkRatio > 0.9);
        if (blank)
            ++failed;
        report << QStringLiteral("[%1] 运行中切到「自定义图片」但没选图 | meanLuma=%2 darkRatio=%3")
                      .arg(blank ? QStringLiteral("FAIL ") : QStringLiteral("PASS "),
                           QString::number(st.meanLuma, 'f', 1),
                           QString::number(st.darkRatio, 'f', 3));
        ++caseCount;

        window->hide();
        delete window;
        QCoreApplication::processEvents();
    }

    // 转速设置：0% 应完全静止，100% 以上应持续转动
    {
        auto *window = new QWidget();
        window->setWindowTitle(QStringLiteral("rotating-bg-probe-speed"));
        window->resize(900, 620);
        auto *bg = new BackgroundWidget(window);
        bg->setGeometry(window->rect());
        bg->lower();
        window->show();

        bg->setMode(BackgroundWidget::Rotating);
        bg->setBlurRadius(0);
        bg->setPanoramaScene(QStringLiteral("overworld"));
        bg->setRotatingSpeed(0);

        auto pump = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        };
        pump(1000);

        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        const QImage still1 = captureWindow(hwnd);
        pump(2500);
        const QImage still2 = captureWindow(hwnd);
        const double stillDiff = frameDiffRatio(still1, still2);
        if (!still2.isNull())
            still2.save(QStringLiteral("%1/speed0.png").arg(shotDir));

        const bool stillOk = stillDiff < 0.05;
        if (!stillOk)
            ++failed;
        report << QStringLiteral("[%1] 转速 0%（静止）| 两帧差异占比=%2")
                      .arg(stillOk ? QStringLiteral("PASS ") : QStringLiteral("FAIL "),
                           QString::number(stillDiff, 'f', 3));
        ++caseCount;

        bg->setRotatingSpeed(200);
        pump(600);
        const QImage spin1 = captureWindow(hwnd);
        pump(2500);
        const QImage spin2 = captureWindow(hwnd);
        const double spinDiff = frameDiffRatio(spin1, spin2);
        if (!spin2.isNull())
            spin2.save(QStringLiteral("%1/speed200.png").arg(shotDir));

        const bool spinOk = spinDiff > 0.15;
        if (!spinOk)
            ++failed;
        report << QStringLiteral("[%1] 转速 200%（转动）| 两帧差异占比=%2")
                      .arg(spinOk ? QStringLiteral("PASS ") : QStringLiteral("FAIL "),
                           QString::number(spinDiff, 'f', 3));
        ++caseCount;

        window->hide();
        delete window;
        QCoreApplication::processEvents();
    }

    // 模糊设置：模糊会重算全景素材，必须仍然正常出图
    {
        auto *window = new QWidget();
        window->setWindowTitle(QStringLiteral("rotating-bg-probe-blur"));
        window->resize(900, 620);
        auto *bg = new BackgroundWidget(window);
        bg->setGeometry(window->rect());
        bg->lower();
        window->show();
        bg->setMode(BackgroundWidget::Rotating);
        bg->setBlurRadius(12);
        bg->setPanoramaScene(QStringLiteral("overworld"));
        pumpEvents(1200);

        const QImage shot = captureWindow(reinterpret_cast<HWND>(window->winId()));
        const Stats st = analyze(shot);
        if (!shot.isNull())
            shot.save(QStringLiteral("%1/blur12.png").arg(shotDir));
        const bool blank = (st.meanLuma < 25.0 && st.darkRatio > 0.9);
        if (blank)
            ++failed;
        report << QStringLiteral("[%1] 模糊 12 像素 | meanLuma=%2 darkRatio=%3")
                      .arg(blank ? QStringLiteral("FAIL ") : QStringLiteral("PASS "),
                           QString::number(st.meanLuma, 'f', 1),
                           QString::number(st.darkRatio, 'f', 3));
        ++caseCount;

        window->hide();
        delete window;
        QCoreApplication::processEvents();
    }

    // 窗口禁用后恢复（居中模态弹窗）、隐藏后重新显示、失去/重获激活：背景都必须还在转
    {
        auto *window = new QWidget();
        window->setWindowTitle(QStringLiteral("rotating-bg-probe-recover"));
        window->resize(900, 620);
        auto *bg = new BackgroundWidget(window);
        bg->setGeometry(window->rect());
        bg->lower();
        window->show();
        bg->setMode(BackgroundWidget::Rotating);
        bg->setBlurRadius(0);
        bg->setPanoramaScene(QStringLiteral("overworld"));
        bg->setRotatingSpeed(100);
        pumpEvents(1000);

        const HWND hwnd = reinterpret_cast<HWND>(window->winId());

        // 居中模态弹窗：宿主窗口先被禁用，关闭后恢复
        window->setEnabled(false);
        pumpEvents(400);
        window->setEnabled(true);
        pumpEvents(700);
        const QImage afterEnable = captureWindow(hwnd);
        const Stats enableStats = analyze(afterEnable);
        if (!afterEnable.isNull())
            afterEnable.save(QStringLiteral("%1/recover_enable.png").arg(shotDir));
        const bool enableBlank = (enableStats.meanLuma < 25.0 && enableStats.darkRatio > 0.9);
        if (enableBlank)
            ++failed;
        report << QStringLiteral("[%1] 禁用后恢复（模态弹窗）| meanLuma=%2 darkRatio=%3")
                      .arg(enableBlank ? QStringLiteral("FAIL ") : QStringLiteral("PASS "),
                           QString::number(enableStats.meanLuma, 'f', 1),
                           QString::number(enableStats.darkRatio, 'f', 3));
        ++caseCount;

        // 隐藏再显示（切换页面/最小化还原）
        window->hide();
        pumpEvents(300);
        window->show();
        pumpEvents(900);
        const QImage afterShow1 = captureWindow(hwnd);
        const Stats showStats = analyze(afterShow1);
        pumpEvents(1500);
        const QImage afterShow2 = captureWindow(hwnd);
        const double showDiff = frameDiffRatio(afterShow1, afterShow2);
        if (!afterShow2.isNull())
            afterShow2.save(QStringLiteral("%1/recover_show.png").arg(shotDir));
        const bool showOk = !(showStats.meanLuma < 25.0 && showStats.darkRatio > 0.9) && showDiff > 0.1;
        if (!showOk)
            ++failed;
        report << QStringLiteral("[%1] 隐藏后重新显示且仍在转动 | meanLuma=%2 darkRatio=%3 帧差=%4")
                      .arg(showOk ? QStringLiteral("PASS ") : QStringLiteral("FAIL "),
                           QString::number(showStats.meanLuma, 'f', 1),
                           QString::number(showStats.darkRatio, 'f', 3),
                           QString::number(showDiff, 'f', 3));
        ++caseCount;

        window->hide();
        delete window;
        QCoreApplication::processEvents();
    }

    for (const QString &line : report)
        QTextStream(stdout) << line << "\n";
    QTextStream(stdout) << QStringLiteral("%1/%2 cases passed\n")
                               .arg(caseCount - failed)
                               .arg(caseCount);
    qInfo().noquote() << "报告：" << QStringLiteral("%1/rotate_report.txt").arg(outDir);

    // 写报告文件，便于后续比对
    QFile f(QStringLiteral("%1/rotate_report.txt").arg(outDir));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        QTextStream(&f) << report.join('\n') << "\n";

    return failed == 0 ? 0 : 1;
}
