// 临时探针：验证「一键下载最新版本及前置」（MainWindow::onCardDownloadRequested）
// 在「搜索结果没有版本文件 → 异步补全详情 → 重入」这条路径上能真正完成下载。
// 用法：card_probe.exe <实例目录>   退出码 0=落盘成功 1=超时/失败 2=参数错误
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

// 先把 MainWindow 传递包含的 Qt 头文件全部展开，再放开访问限制，
// 这样只有 MainWindow 自身的 private 成员被打开，不会影响 Qt 头文件。
#include <QMainWindow>
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QList>
#include <QMap>
#include <QHash>
#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTextBrowser>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "utils/content/ContentData.h"
#include "utils/mod/ModData.h"

#define private public
#include "mainwindow.h"
#undef private

static QFile *g_log = nullptr;

static void logLine(const QString &text)
{
    if (!g_log) {
        g_log = new QFile(QStringLiteral("probe_log.txt"));
        g_log->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    }
    QByteArray line = text.toUtf8();
    line += '\n';
    g_log->write(line);
    g_log->flush();
}

static bool hasAnyJar(const QString &dir)
{
    QDir d(dir);
    if (!d.exists())
        return false;
    return !d.entryList(QStringList{QStringLiteral("*.jar")}, QDir::Files).isEmpty();
}

int main(int argc, char *argv[])
{
    logLine(QStringLiteral("[probe] 启动"));
    QApplication app(argc, argv);
    logLine(QStringLiteral("[probe] QApplication 就绪"));

    if (argc < 2) {
        logLine(QStringLiteral("[probe] 参数缺失"));
        return 2;
    }
    const QString instancePath = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1]));
    const QString modsDir = instancePath + QStringLiteral("/mods");
    QDir().mkpath(modsDir);

    logLine(QStringLiteral("[probe] 实例目录: %1").arg(instancePath));
    logLine(QStringLiteral("[probe] mods 已存在: %1, 原有 jar 数: %2")
                .arg(QFileInfo(modsDir).isDir())
                .arg(QDir(modsDir).entryList(QStringList{QStringLiteral("*.jar")}, QDir::Files).size()));

    logLine(QStringLiteral("[probe] 开始构造 MainWindow"));
    MainWindow w;
    logLine(QStringLiteral("[probe] MainWindow 构造完成"));
    w.show();
    logLine(QStringLiteral("[probe] MainWindow show 完成"));
    for (int i = 0; i < 60; ++i)
        QApplication::processEvents();

    w.updateCurrentInstance(instancePath);
    for (int i = 0; i < 60; ++i)
        QApplication::processEvents();
    logLine(QStringLiteral("[probe] 当前实例: path=%1 version=%2 loader=%3")
                .arg(w.m_currentInstancePath, w.m_currentInstanceVersion, w.m_currentInstanceLoader));

    ModInfo info;
    info.id = QStringLiteral("sodium");
    info.name = QStringLiteral("Sodium");
    info.englishName = QStringLiteral("Sodium");
    info.source = QStringLiteral("modrinth");
    info.modType = QStringLiteral("mod");

    logLine(QStringLiteral("[probe] 触发一键下载（versionFiles 为空，应走详情补全）"));
    w.onCardDownloadRequested(info, ContentType::Mod, 0, QString(), QString());
    logLine(QStringLiteral("[probe] onCardDownloadRequested 返回（异步下载已发起）"));

    QElapsedTimer timer;
    timer.start();
    bool ok = false;
    int lastReport = 0;
    while (timer.elapsed() < 150000) {
        for (int i = 0; i < 20; ++i)
            QApplication::processEvents();
        QThread::msleep(100);
        if (hasAnyJar(modsDir)) {
            ok = true;
            break;
        }
        if (timer.elapsed() - lastReport > 15000) {
            lastReport = timer.elapsed();
            logLine(QStringLiteral("[probe] 等待中 %1s").arg(lastReport / 1000));
        }
    }

    const QStringList jars = QDir(modsDir).entryList(QStringList{QStringLiteral("*.jar")}, QDir::Files);
    logLine(QStringLiteral("[probe] 结果: %1, 耗时 %2ms, jar=%3")
                .arg(ok ? QStringLiteral("成功落盘") : QStringLiteral("超时未落盘"))
                .arg(timer.elapsed())
                .arg(jars.join(QStringLiteral(", "))));

    return ok ? 0 : 1;
}
