/**
 * @file   install_flow_probe.cpp
 * @brief  安装流程探针：真实 LoaderDetailPage + 真实 VersionDownloader，模拟点击“安装”
 *
 * 目的：验证「新实例无法安装」的修复——点安装后，
 *   1) 版本文件落在 <游戏根>/versions/<实例名>/<实例名>.json 与 <版本id>.jar；
 *   2) LoaderDetailPage 发出的 installRequested 携带的正是该实例目录。
 *
 * 用法：install_flow_probe.exe [版本id] [游戏根目录] [实例名]
 * 退出码：0=通过 1=安装失败 2=超时 3=产物缺失 4=控件缺失 5=路径未落位
 */
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "pages/LoaderDetailPage.h"
#include "utils/SettingsManager.h"
#include "utils/VersionDownloader.h"

namespace {

void logLine(const QString &text)
{
    std::fprintf(stderr, "[%s] %s\n", qUtf8Printable(QTime::currentTime().toString("hh:mm:ss")),
                 qUtf8Printable(text));
    std::fflush(stderr);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("install_flow_probe"));

    const QString versionId = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("1.21.1");
    const QString rootPath = QDir::fromNativeSeparators(
        argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("D:/BlockBox/_out/flow_probe_root"));
    const QString instName = argc > 3 ? QString::fromLocal8Bit(argv[3])
                                      : QStringLiteral("正式版-%1").arg(versionId);
    const QString expectDir = QDir::cleanPath(rootPath + "/versions/" + instName);

    logLine(QStringLiteral("版本=%1 游戏根=%2 实例名=%3").arg(versionId, rootPath, instName));

    // 准备一个“全新”的游戏根，避免复用旧产物掩盖问题
    if (QDir(rootPath).exists())
        QDir(rootPath).removeRecursively();
    if (!QDir().mkpath(rootPath + "/versions")) {
        logLine(QStringLiteral("无法创建游戏根目录"));
        return 4;
    }
    SettingsManager::instance()->addInstanceFolder(QStringLiteral("探针实例文件夹"), rootPath, true);
    SettingsManager::instance()->setDefaultInstanceFolder(rootPath);

    QWidget win;
    win.resize(1240, 820);
    auto *layout = new QVBoxLayout(&win);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *page = new LoaderDetailPage(&win);
    layout->addWidget(page);
    page->setVersionType(QStringLiteral("正式版"));
    page->setVersionInfo(versionId);
    win.show();
    app.processEvents();

    auto *nameEdit = page->findChild<QLineEdit *>(QStringLiteral("loaderInstanceNameEdit"));
    auto *installBtn = page->findChild<QPushButton *>(QStringLiteral("installButton"));
    auto *pathCombo = page->findChild<QComboBox *>(QStringLiteral("loaderPathCombo"));
    if (!nameEdit || !installBtn || !pathCombo) {
        logLine(QStringLiteral("控件缺失: nameEdit=%1 installBtn=%2 pathCombo=%3")
                    .arg(nameEdit != nullptr).arg(installBtn != nullptr).arg(pathCombo != nullptr));
        return 4;
    }

    int idx = pathCombo->findData(rootPath);
    if (idx < 0) {
        for (int i = 0; i < pathCombo->count(); ++i) {
            logLine(QStringLiteral("combo[%1]=%2 data=%3")
                        .arg(i).arg(pathCombo->itemText(i), pathCombo->itemData(i).toString()));
        }
        logLine(QStringLiteral("下拉框里没有探针游戏根，按用户手动选择的方式追加一项"));
        pathCombo->addItem(QStringLiteral("探针实例文件夹"), rootPath);
        idx = pathCombo->count() - 1;
    }
    pathCombo->setCurrentIndex(idx);
    nameEdit->setText(instName);
    app.processEvents();
    logLine(QStringLiteral("已选中游戏根=%1，实例名输入框=%2")
                .arg(pathCombo->currentData().toString(), nameEdit->text()));

    bool finished = false;
    int exitCode = 2;

    auto reportArtifacts = [&](const QString &dir) {
        const QString jsonPath = dir + "/" + QFileInfo(dir).fileName() + ".json";
        const QString jarPath = dir + "/" + versionId + ".jar";
        logLine(QStringLiteral("实例目录产物：json=%1(%2 字节) jar=%3(%4 字节)")
                    .arg(jsonPath)
                    .arg(QFileInfo(jsonPath).size())
                    .arg(jarPath)
                    .arg(QFileInfo(jarPath).size()));
        return QFile::exists(jsonPath) && QFileInfo(jsonPath).size() > 0
            && QFile::exists(jarPath) && QFileInfo(jarPath).size() > 0;
    };

    QObject::connect(page, &LoaderDetailPage::installRequested, &app,
                     [&](const QString &instancePath, const QString &name, const QString &mcVersion,
                         const QMap<QString, SelectedLoader> &loaders) {
        const QString reported = QDir::cleanPath(QDir::fromNativeSeparators(instancePath));
        logLine(QStringLiteral("SIG installRequested path=%1 name=%2 mc=%3 loaders=%4")
                    .arg(reported, name, mcVersion).arg(loaders.size()));
        finished = true;
        if (reported != expectDir) {
            logLine(QStringLiteral("FAIL 安装完成后的实例目录不是 <游戏根>/versions/<实例名>，期望 %1")
                        .arg(expectDir));
            exitCode = 5;
        } else if (!reportArtifacts(reported)) {
            logLine(QStringLiteral("FAIL 实例目录缺少版本 json 或 jar"));
            exitCode = 3;
        } else {
            logLine(QStringLiteral("PASS 新实例安装落位正确"));
            exitCode = 0;
        }
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });

    QObject::connect(VersionDownloader::instance(), &VersionDownloader::downloadFailed, &app,
                     [&](const QString &error) {
        logLine(QStringLiteral("SIG downloadFailed: %1").arg(error));
        finished = true;
        exitCode = 1;
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });

    QObject::connect(VersionDownloader::instance(), &VersionDownloader::downloadCompleted, &app,
                     [&](const QString &vid, const QString &path) {
        logLine(QStringLiteral("SIG downloadCompleted vid=%1 path=%2").arg(vid, path));
    });

    QObject::connect(VersionDownloader::instance(), &VersionDownloader::statusChanged, &app,
                     [&](const QString &status) { logLine(QStringLiteral("STATUS %1").arg(status)); });

    QObject::connect(VersionDownloader::instance(), &VersionDownloader::downloadProgressUpdated,
                     &app, [&](qint64 received, qint64 total) {
        });
    logLine(QStringLiteral("10 秒后开始点击安装按钮"));
    QTimer::singleShot(10000, installBtn, [installBtn]() { installBtn->click(); });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&]() {
        if (!finished) {
            logLine(QStringLiteral("TIMEOUT 600 秒仍未拿到 installRequested"));
            exitCode = 2;
        }
        app.quit();
    });
    watchdog.start(600000);
    page->grab().save(QStringLiteral("D:/BlockBox/_out/install_flow_probe_page.png"));
    app.exec();
    logLine(QStringLiteral("结束 exit=%1").arg(exitCode));
    return exitCode;
}
