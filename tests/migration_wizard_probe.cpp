/**
 * @file   migration_wizard_probe.cpp
 * @brief  迁移向导探针：假 APPDATA + 假游戏目录，截图四步并真实跑一次「只登记」迁移
 *
 * 运行前由 tests/run_migration_wizard_probe.ps1 备份 HKCU\Software\BlockBox，
 * 结束后恢复，避免把探针数据留在真实设置里。
 */
#include "components/MigrationWizardDialog.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QWidget>

#include <cstdio>

namespace {

void out(const QString &line)
{
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);
}

void settle(QWidget *w, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (w)
            w->repaint();
    }
}

bool grab(QWidget *w, const QString &path)
{
    if (!w)
        return false;
    const bool ok = w->grab().save(path);
    out(QStringLiteral("[%1] %2").arg(ok ? QStringLiteral("ok") : QStringLiteral("FAIL"), path));
    return ok;
}

QPushButton *findButton(QWidget *root, const QString &text)
{
    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text() == text && button->isVisible())
            return button;
    }
    return nullptr;
}

QPushButton *findButtonContains(QWidget *root, const QString &text)
{
    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text().contains(text) && button->isVisible())
            return button;
    }
    return nullptr;
}

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(data);
}

QString makeFakeWorld(const QString &root)
{
    QDir().mkpath(root);

    const auto makeVersion = [&](const QString &gameDir, const QString &name) {
        const QString dir = gameDir + QStringLiteral("/versions/") + name;
        QDir().mkpath(dir);
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), name);
        obj.insert(QStringLiteral("type"), QStringLiteral("release"));
        writeFile(dir + QStringLiteral("/") + name + QStringLiteral(".json"),
                  QJsonDocument(obj).toJson(QJsonDocument::Indented));
        writeFile(dir + QStringLiteral("/") + name + QStringLiteral(".jar"), QByteArray(64, 'x'));
    };

    const QString pclGame = root + QStringLiteral("/appdata/PCL/.minecraft");
    makeVersion(pclGame, QStringLiteral("1.21.1"));
    makeVersion(pclGame, QStringLiteral("1.20.1-forge"));
    writeFile(pclGame + QStringLiteral("/PCL.ini"), QByteArray("VersionArgumentIndie:1\n"));
    writeFile(pclGame + QStringLiteral("/mods/sodium.jar"), QByteArray(32, 'm'));
    writeFile(pclGame + QStringLiteral("/mods/iris.jar"), QByteArray(32, 'm'));
    writeFile(pclGame + QStringLiteral("/mods/optifine.jar"), QByteArray(32, 'm'));
    writeFile(pclGame + QStringLiteral("/saves/World1/level.dat"), QByteArray(32, 's'));
    writeFile(pclGame + QStringLiteral("/saves/World2/level.dat"), QByteArray(32, 's'));

    QJsonArray accounts;
    QJsonObject acc;
    acc.insert(QStringLiteral("Name"), QStringLiteral("Steve"));
    acc.insert(QStringLiteral("TokenType"), QStringLiteral("offline"));
    acc.insert(QStringLiteral("IsOffline"), true);
    acc.insert(QStringLiteral("AuthServer"), QString());
    accounts.append(acc);
    QJsonObject acc2;
    acc2.insert(QStringLiteral("Name"), QStringLiteral("Alex"));
    acc2.insert(QStringLiteral("TokenType"), QStringLiteral("offline"));
    acc2.insert(QStringLiteral("IsOffline"), true);
    accounts.append(acc2);
    writeFile(pclGame + QStringLiteral("/accounts.json"),
              QJsonDocument(accounts).toJson(QJsonDocument::Indented));

    const QString javaDir = root + QStringLiteral("/jdk17/bin");
    QDir().mkpath(javaDir);
    writeFile(javaDir + QStringLiteral("/java.exe"), QByteArray("fake"));

    QJsonObject javaEntry;
    javaEntry.insert(QStringLiteral("Folder"), javaDir);
    QJsonObject version;
    version.insert(QStringLiteral("Major"), 17);
    javaEntry.insert(QStringLiteral("Version"), version);
    QJsonArray javaList;
    javaList.append(javaEntry);
    QJsonObject config;
    config.insert(QStringLiteral("JavaList"), javaList);
    writeFile(root + QStringLiteral("/appdata/PCL/config.json"),
              QJsonDocument(config).toJson(QJsonDocument::Indented));

    // 官方启动器兜底目录（也能被 HMCL 认领），额外放一个版本
    const QString officialGame = root + QStringLiteral("/appdata/.minecraft");
    makeVersion(officialGame, QStringLiteral("1.19.2"));
    QJsonObject profiles;
    QJsonObject profile;
    profile.insert(QStringLiteral("name"), QStringLiteral("Latest release"));
    profile.insert(QStringLiteral("lastVersionId"), QStringLiteral("1.19.2"));
    profiles.insert(QStringLiteral("Latest release"), profile);
    writeFile(officialGame + QStringLiteral("/launcher_profiles.json"),
              QJsonDocument(profiles).toJson(QJsonDocument::Indented));

    return pclGame;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString outDir = QStringLiteral("D:/BlockBox/_out/migration_probe");
    QDir().mkpath(outDir);
    const QString fakeAppData = outDir + QStringLiteral("/appdata");
    const QString fakeGame = makeFakeWorld(outDir);
    qputenv("BLOCKBOX_APPDATA", fakeAppData.toLocal8Bit());
    qputenv("BLOCKBOX_SKIP_DISK_SCAN", "1");

    ThemeManager::instance()->setTheme(
        &app, qgetenv("MIGRATION_PROBE_DARK") == "1" ? ThemeManager::DarkTheme
                                                      : ThemeManager::LightTheme);
    ThemeManager::instance()->setThemeColor(QStringLiteral("#2E7D32"));
    settle(nullptr, 100);

    QWidget host;
    host.setWindowTitle(QStringLiteral("migration wizard probe"));
    host.resize(1280, 820);
    host.show();
    settle(&host, 250);

    MigrationWizardDialog *dialog = new MigrationWizardDialog(&host);
    dialog->resize(host.size());
    dialog->show();
    settle(dialog, 1000);
    grab(dialog, outDir + QStringLiteral("/01_scan.png"));

    // 等扫描完成（最多 45 秒）
    QPushButton *next = nullptr;
    for (int i = 0; i < 450; ++i) {
        settle(dialog, 100);
        next = findButton(dialog, QStringLiteral("下一步"));
        if (next && next->isEnabled())
            break;
    }
    out(QStringLiteral("scan done, next=%1")
            .arg(next && next->isEnabled() ? QStringLiteral("enabled")
                                           : QStringLiteral("disabled")));
    grab(dialog, outDir + QStringLiteral("/02_scan_done.png"));

    if (next)
        next->click();
    settle(dialog, 600);
    grab(dialog, outDir + QStringLiteral("/03_select.png"));

    QPushButton *expand = findButton(dialog, QStringLiteral("展开"));
    if (expand) {
        expand->click();
        settle(dialog, 400);
        grab(dialog, outDir + QStringLiteral("/04_select_expanded.png"));
    }

    next = findButton(dialog, QStringLiteral("下一步"));
    if (next && next->isEnabled())
        next->click();
    settle(dialog, 700);
    grab(dialog, outDir + QStringLiteral("/05_options.png"));

    // 切到复制模式看一眼估算和磁盘提示，再切回只登记
    const bool copyMode = (qgetenv("MIGRATION_PROBE_COPY") == "1");
    const QList<QRadioButton *> radios = dialog->findChildren<QRadioButton *>();
    QRadioButton *copyRadio = nullptr;
    QRadioButton *registerRadio = nullptr;
    for (QRadioButton *radio : radios) {
        if (radio->text().contains(QStringLiteral("复制一份")))
            copyRadio = radio;
        if (radio->text().contains(QStringLiteral("只登记")))
            registerRadio = radio;
    }
    if (copyRadio) {
        copyRadio->setChecked(true);
        settle(dialog, 1200);
        grab(dialog, outDir + QStringLiteral("/06_options_copy.png"));
    }
    // 真正跑一次默认的「只登记」：几秒完成，也不会额外复制几份文件；
    // MIGRATION_PROBE_COPY=1 时保留「复制一份」，用来验证复制队列和进度
    if (!copyMode && registerRadio) {
        registerRadio->setChecked(true);
        settle(dialog, 400);
    }

    QPushButton *start = findButtonContains(dialog, QStringLiteral("开始迁移"));
    if (start) {
        out(QStringLiteral("clicking start: %1").arg(start->text()));
        start->click();
        settle(dialog, 4000);
        grab(dialog, outDir + (copyMode ? QStringLiteral("/07_result_copy.png")
                                        : QStringLiteral("/07_result.png")));
    } else {
        out(QStringLiteral("start button not found"));
    }

    // 打印写入后的设置，便于人工核对
    SettingsManager *sm = SettingsManager::instance();
    out(QStringLiteral("accounts=%1 folders=%2 java=%3")
            .arg(sm->getAccounts().size())
            .arg(sm->getInstanceFolders().size())
            .arg(sm->getJavaInstallations().size()));
    for (const InstanceFolderInfo &folder : sm->getInstanceFolders())
        out(QStringLiteral("folder: %1 -> %2").arg(folder.name, folder.path));

    Q_UNUSED(fakeGame);
    dialog->close();
    delete dialog;
    app.quit();
    return 0;
}
