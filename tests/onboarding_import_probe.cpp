/**
 * @file   onboarding_import_probe.cpp
 * @brief  新手引导「从启动器导入」页交互探针：截图 + 真实执行一次导入，并打印导入前后的设置
 *
 * 注意：会写入真实的 BlockBox 设置（Windows 是 HKCU\Software\BlockBox\BlockBox），
 * 跑之前请先备份、跑完恢复（见 tests/run_onboarding_import_probe.ps1）。
 */
#include "pages/OnboardingWizard.h"
#include "utils/SettingsManager.h"
#include "utils/LauncherImporter.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QMetaObject>
#include <QTimer>
#include <QWidget>

#include <cstdio>

namespace {

QStringList g_report;

void out(const QString &line)
{
    g_report << line;
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);
}

void settle(QWidget *w, int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (w) w->repaint();
    }
}

bool grab(QWidget *w, const QString &path)
{
    if (!w) return false;
    const bool ok = w->grab().save(path);
    out(QStringLiteral("[%1] %2").arg(ok ? QStringLiteral("ok") : QStringLiteral("FAIL"), path));
    return ok;
}

void invoke(OnboardingWizard *wiz, const char *slot)
{
    QMetaObject::invokeMethod(wiz, slot, Qt::DirectConnection);
}

void dumpSettings(const QString &tag)
{
    SettingsManager *sm = SettingsManager::instance();
    const QList<AccountInfo> accounts = sm->getAccounts();
    const QList<InstanceFolderInfo> folders = sm->getInstanceFolders();
    const QList<QPair<QString, QString>> javas = sm->getJavaInstallations();
    const QList<AuthServerInfo> authServers = sm->getAuthServers();

    out(QStringLiteral("---- settings %1").arg(tag));
    out(QStringLiteral("  accounts (%1):").arg(accounts.size()));
    for (const AccountInfo &a : accounts)
        out(QStringLiteral("    - %1 [%2] default=%3").arg(a.username, a.type).arg(a.isDefault));
    out(QStringLiteral("  folders (%1):").arg(folders.size()));
    for (const InstanceFolderInfo &f : folders)
        out(QStringLiteral("    - %1 -> %2").arg(f.name, f.path));
    out(QStringLiteral("  java (%1):").arg(javas.size()));
    for (const auto &j : javas)
        out(QStringLiteral("    - %1 (%2)").arg(j.first, j.second));
    out(QStringLiteral("  authServers (%1):").arg(authServers.size()));
    for (const AuthServerInfo &s : authServers)
        out(QStringLiteral("    - %1 -> %2").arg(s.name, s.url));
    out(QStringLiteral("  download: source=%1 threads=%2")
            .arg(static_cast<int>(sm->getDownloadSource()))
            .arg(sm->getDownloadThreadCount()));
    out(QStringLiteral("  memory: min=%1 max=%2 auto=%3")
            .arg(sm->getMinMemory())
            .arg(sm->getMaxMemory())
            .arg(sm->isAutoMemoryEnabled()));
}

void dumpCardTexts(OnboardingWizard *wiz, const QString &tag)
{
    out(QStringLiteral("---- card texts %1").arg(tag));
    const QList<QLabel *> labels = wiz->findChildren<QLabel *>(QStringLiteral("obwLauncherStatusText"));
    for (int i = 0; i < labels.size(); ++i)
        out(QStringLiteral("  [%1] %2").arg(i).arg(labels.at(i)->text()));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString outDir = QStringLiteral("D:/BlockBox/_out/onboarding_probe");
    QDir().mkpath(outDir);

    ThemeManager::instance()->setTheme(&app, ThemeManager::LightTheme);
    ThemeManager::instance()->setThemeColor(QStringLiteral("#2E7D32"));
    settle(nullptr, 80);

    dumpSettings(QStringLiteral("before"));

    QWidget host;
    host.setWindowTitle(QStringLiteral("onboarding import probe host"));
    // 默认按桌面窗口截图；设 ONBOARDING_PROBE_WIDTH / _HEIGHT 可模拟横屏手机的宽度
    const int hostW = qEnvironmentVariableIntValue("ONBOARDING_PROBE_WIDTH");
    const int hostH = qEnvironmentVariableIntValue("ONBOARDING_PROBE_HEIGHT");
    host.resize(hostW > 0 ? hostW : 1280, hostH > 0 ? hostH : 800);
    host.show();
    settle(&host, 200);

    OnboardingWizard *wiz = new OnboardingWizard(&host);
    // 新手引导已是主窗口里的独立整页：探针里手动铺满宿主窗口
    wiz->setGeometry(host.rect());
    wiz->show();
    settle(wiz, 250);

    invoke(wiz, "onNextClicked");            // 语言 → 引导方式
    settle(wiz, 200);
    QMetaObject::invokeMethod(wiz, "onMethodPicked", Qt::DirectConnection,
                              Q_ARG(int, 1)); // MethodImport
    settle(wiz, 120);
    invoke(wiz, "onNextClicked");            // → 从启动器导入
    settle(wiz, 2000);                       // 等异步检测完成
    grab(wiz, outDir + QStringLiteral("/08_import_light.png"));
    dumpCardTexts(wiz, QStringLiteral("after detect"));

    // 真实点一次「导入」：按当前平台支持的启动器列表逐个导入
    // （桌面是 PCL / HMCL / MultiMC / BakaXL，安卓是 FCL / ZL2 / Pojav / 其他目录）
    const QStringList keys = LauncherImporter::supportedKeys();
    for (int i = 0; i < keys.size(); ++i) {
        QMetaObject::invokeMethod(wiz, "onLauncherImport", Qt::DirectConnection,
                                  Q_ARG(QString, keys.at(i)));
        settle(wiz, 600);
        dumpCardTexts(wiz, QStringLiteral("after import %1").arg(keys.at(i)));
        grab(wiz, outDir + QStringLiteral("/09_import_%1.png").arg(keys.at(i)));
    }

    dumpSettings(QStringLiteral("after"));

    const QString reportPath = QCoreApplication::applicationDirPath() + QStringLiteral("/import_report.txt");
    QFile file(reportPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(g_report.join(QLatin1Char('\n')).toUtf8());

    wiz->close();
    delete wiz;
    app.quit();
    return 0;
}
