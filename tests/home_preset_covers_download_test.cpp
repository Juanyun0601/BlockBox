// 临时自测：验证 HomePresetCovers 的下载流程（本机 http 服务，不联网、不改产品代码）
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

#include "utils/HomePresetCovers.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // 用测试模式隔离 AppData，别写到真实数据目录
    QStandardPaths::setTestModeEnabled(true);
    qInfo().noquote() << "coverDir =" << HomePresetCovers::coverDir();

    // 先清掉上次的缓存，保证这次真的走一遍下载
    QDir(HomePresetCovers::coverDir()).removeRecursively();

    HomePresetCovers *covers = HomePresetCovers::instance();

    QObject::connect(covers, &HomePresetCovers::downloadingChanged, [](bool on) {
        qInfo().noquote() << "downloadingChanged" << on;
    });
    QObject::connect(covers, &HomePresetCovers::progressChanged, [](int done, int total) {
        qInfo().noquote() << "progress" << done << "/" << total;
    });
    QObject::connect(covers, &HomePresetCovers::finished, [&app](const QStringList &files) {
        const bool ok = files.size() == HomePresetCovers::presetCount()
                        && HomePresetCovers::isDownloaded();
        qInfo().noquote() << "FINISHED files=" << files.size() << "isDownloaded="
                          << HomePresetCovers::isDownloaded();
        for (const QString &f : files)
            qInfo().noquote() << "   " << f;
        app.exit(ok ? 0 : 3);
    });
    QObject::connect(covers, &HomePresetCovers::failed, [&app](const QString &error) {
        qWarning().noquote() << "FAILED:" << error;
        app.exit(2);
    });

    // 延到事件循环里再触发：文件已全部下好时 download() 会同步跑完并立刻发 finished，
    // 若在 exec() 之前发，app.exit() 不会生效（只是测试写法问题，App 里是点按钮才下载）
    QTimer::singleShot(0, covers, [covers]() { covers->download(); });
    return app.exec();
}
