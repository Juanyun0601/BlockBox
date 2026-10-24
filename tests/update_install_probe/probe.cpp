// 更新安装探针：直接跑 UpdateChecker 里真正的「准备 + 启动安装」逻辑，
// 验证拖入新版本文件后是否真的能把程序目录换成新版本。
//
// 做法：把 UpdateChecker.cpp 整体 include 进来，这样匿名命名空间里的
// prepareWindowsUpdate / launchWindowsUpdate / locatePayloadRoot 都能直接调用，
// 链接时排除原来的 UpdateChecker.o，避免重复定义。
//
// 用法：把本探针产物命名为 BlockBoxWindows.exe 放进「假的程序目录」，
//       然后 BlockBoxWindows.exe <更新包路径>
#include "utils/UpdateChecker.cpp"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QTextStream out(stdout);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        out << "usage: BlockBoxWindows.exe <package>\n";
        return 1;
    }

    const QString package = args.at(1);
    out << "package       : " << package << "\n";
    out << "appDir        : " << QCoreApplication::applicationDirPath() << "\n";
    out << "exeName       : " << QFileInfo(QCoreApplication::applicationFilePath()).fileName()
        << "\n";

    bool isSetup = false;
    QString payloadDir;
    QString error;
    if (!prepareWindowsUpdate(package, &isSetup, &payloadDir, &error)) {
        out << "PREPARE-FAILED: " << error << "\n";
        out.flush();
        return 2;
    }
    out << "PREPARE-OK setup=" << (isSetup ? 1 : 0) << " payload=" << payloadDir << "\n";

    if (!launchWindowsUpdate(package, isSetup, payloadDir, &error)) {
        out << "LAUNCH-FAILED: " << error << "\n";
        out.flush();
        return 3;
    }

    out << "LAUNCH-OK: helper started, exiting now\n";
    out.flush();
    return 0;
}
