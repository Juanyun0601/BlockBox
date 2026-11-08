/**
 * @file   java_download_probe.cpp
 * @brief  Java 下载页离屏探针：亮/暗主题 + 空闲/下载中状态截图
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 用法（在 C-BlockBox 下）：
 *   rcc -name resources_data .tmp_cc/probe_style.qrc -o build-win-release/probe_out/qrc_resources_data.cpp
 *   pwsh .syntax_check/build_probe_win_release.ps1 -Name java_download_probe `
 *        -Overrides pages/JavaDownloadPage.cpp,build-win-release/probe_out/qrc_resources_data.cpp
 *   build-win-release/probe_out/java_download_probe.exe
 */

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "pages/JavaDownloadPage.h"
#include "utils/ThemeManager.h"

namespace {

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void shoot(ThemeManager::ThemeType theme, const QString &themeName)
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    ThemeManager::setTheme(app, theme);

    QWidget host;
    host.setAutoFillBackground(true);
    QPalette pal = host.palette();
    pal.setColor(QPalette::Window,
                 theme == ThemeManager::DarkTheme ? QColor(QStringLiteral("#1a1a1a"))
                                                  : QColor(QStringLiteral("#f5f5f5")));
    host.setPalette(pal);
    host.resize(1180, 880);

    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *page = new JavaDownloadPage(&host);
    layout->addWidget(page);

    host.show();
    QApplication::processEvents();
    waitMs(2600); // 等版本列表（网络）回来，看真实状态
    QApplication::processEvents();

    QString path = QStringLiteral("probe_out/java_download_%1.png").arg(themeName);
    std::printf("[%s] %s\n", host.grab().save(path) ? "PASS" : "FAIL", qUtf8Printable(path));

    // 等一下，别把「正在获取版本」的中间态截成下载中
    for (int i = 0; i < 40 && JavaDownloader::instance()->status() != JavaDownloadStatus::Idle; ++i)
        waitMs(200);
    QApplication::processEvents();

    // 预览下载中状态：直接摆弄视图控件（不改业务状态，也不真的下载）
    auto *bar = page->findChild<QProgressBar *>(QStringLiteral("javaDlProgress"));
    auto *pill = page->findChild<QLabel *>(QStringLiteral("javaDlStatus"));
    auto *detail = page->findChild<QLabel *>(QStringLiteral("javaDlProgressText"));
    if (bar && pill && detail) {
        bar->parentWidget()->setVisible(true);
        bar->setValue(42);
        detail->setText(QStringLiteral("正在下载... 42%"));
        pill->setText(QStringLiteral("●  ") + QStringLiteral("正在下载 Java..."));
        pill->setProperty("state", QStringLiteral("busy"));
        pill->style()->unpolish(pill);
        pill->style()->polish(pill);
        pill->setVisible(true);
        for (auto *combo : page->findChildren<QComboBox *>())
            combo->setEnabled(true);
        page->findChild<QPushButton *>(QStringLiteral("javaDlBrowseBtn"))->setEnabled(true);
        page->findChild<QPushButton *>(QStringLiteral("javaDlCancelBtn"))->setVisible(true);
    }
    QApplication::processEvents();

    path = QStringLiteral("probe_out/java_download_%1_busy.png").arg(themeName);
    std::printf("[%s] %s\n", host.grab().save(path) ? "PASS" : "FAIL", qUtf8Printable(path));

    // 主窗口最小尺寸（1200x800，扣掉顶栏后约 744 高）下检查是否出现滚动条、按钮是否还在
    host.resize(1200, 744);
    QApplication::processEvents();
    path = QStringLiteral("probe_out/java_download_%1_min.png").arg(themeName);
    std::printf("[%s] %s\n", host.grab().save(path) ? "PASS" : "FAIL", qUtf8Printable(path));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QApplication::setApplicationName(QStringLiteral("Settings"));

    shoot(ThemeManager::LightTheme, QStringLiteral("light"));
    shoot(ThemeManager::DarkTheme, QStringLiteral("dark"));
    return 0;
}
