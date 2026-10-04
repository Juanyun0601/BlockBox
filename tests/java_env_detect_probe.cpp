/**
 * @file   java_env_detect_probe.cpp
 * @brief  等价复刻 GameLauncher::detectJavaFromEnv 的核心算法（读 JAVA_HOME/PATH
 *         → 找 java.exe → java -version 校验），四个场景验证检测与回退行为。
 *         与 GameLauncherJava.cpp 中实现保持同步。
 */
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

#include <cstdio>

struct EnvJavaInfo {
    QString path;
    QString version;
    bool valid = false;
    QString source;
};

static bool validateJavaPath(const QString &path, EnvJavaInfo &info)
{
    info.path = path;
    info.valid = false;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(path, {"-version"});
    if (process.waitForFinished(2000)) {
        const QString out = QString::fromLocal8Bit(process.readAll());
        static const QRegularExpression re("version \"([0-9]+(\\.[0-9]+)*(_[0-9]+)?)");
        const auto m = re.match(out);
        if (m.hasMatch()) {
            info.version = m.captured(1);
            info.valid = true;
            return true;
        }
    }
    return false;
}

// 与 GameLauncher::detectJavaFromEnv 相同的候选构造与遍历
static EnvJavaInfo detectJavaFromEnv()
{
    EnvJavaInfo info;
#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("java.exe");
#else
    const QString exeName = QStringLiteral("java");
#endif

    const auto cleanEnvDir = [](QString s) {
        s = s.trimmed();
        if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
            s = s.mid(1, s.size() - 2);
        return s;
    };

    QList<QPair<QString, QString>> candidates;

    const QString javaHome = cleanEnvDir(qEnvironmentVariable("JAVA_HOME"));
    if (!javaHome.isEmpty()) {
        candidates << qMakePair(QDir::cleanPath(javaHome + QStringLiteral("/bin")),
                                QStringLiteral("JAVA_HOME"));
        candidates << qMakePair(javaHome, QStringLiteral("JAVA_HOME"));
    }

    const QString pathVar = qEnvironmentVariable("PATH");
    const QStringList pathEntries = pathVar.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &raw : pathEntries) {
        const QString entry = cleanEnvDir(raw);
        if (!entry.isEmpty())
            candidates << qMakePair(entry, QStringLiteral("PATH"));
    }

    for (const auto &cand : candidates) {
        const QString exePath = QDir(cand.first).absoluteFilePath(exeName);
        if (!QFileInfo::exists(exePath))
            continue;
        EnvJavaInfo tempInfo;
        if (validateJavaPath(QDir::toNativeSeparators(exePath), tempInfo)) {
            tempInfo.source = cand.second;
            return tempInfo;
        }
    }
    return info;
}

static int g_failures = 0;

static void expect(bool cond, const char *msg)
{
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond)
        ++g_failures;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 本机已验证可用的 JDK（Microsoft jdk-21.0.8.9-hotspot）
    const QString goodHome = QStringLiteral("C:/Program Files/Microsoft/jdk-21.0.8.9-hotspot");
    const QString goodBin = goodHome + QStringLiteral("/bin");
    if (!QFileInfo::exists(goodBin + QStringLiteral("/java.exe"))) {
        std::printf("SKIP: good JDK not found at %s\n", qUtf8Printable(goodBin));
        return 0;
    }

    // 场景1：JAVA_HOME 指向可用 JDK → source=JAVA_HOME
    qputenv("JAVA_HOME", goodHome.toLocal8Bit());
    qputenv("PATH", QByteArrayLiteral("C:/nonexistent_dir_xyz"));
    EnvJavaInfo r1 = detectJavaFromEnv();
    std::printf("[1] JAVA_HOME valid: valid=%d source=%s version=%s\n", r1.valid,
                qUtf8Printable(r1.source), qUtf8Printable(r1.version));
    expect(r1.valid, "detected");
    expect(r1.source == QStringLiteral("JAVA_HOME"), "source=JAVA_HOME");
    expect(r1.version.startsWith(QStringLiteral("21")), "version=21.x");

    // 场景2：JAVA_HOME 指向 bin 目录本身（误写法）→ 仍能检测
    qputenv("JAVA_HOME", goodBin.toLocal8Bit());
    EnvJavaInfo r2 = detectJavaFromEnv();
    std::printf("[2] JAVA_HOME=bin dir: valid=%d source=%s version=%s\n", r2.valid,
                qUtf8Printable(r2.source), qUtf8Printable(r2.version));
    expect(r2.valid, "detected");
    expect(r2.source == QStringLiteral("JAVA_HOME"), "source=JAVA_HOME");

    // 场景3：JAVA_HOME 垃圾 + PATH 含可用 JDK → 回退 PATH
    qputenv("JAVA_HOME", QByteArrayLiteral("C:/nonexistent_jdk_xyz"));
    qputenv("PATH", goodBin.toLocal8Bit());
    EnvJavaInfo r3 = detectJavaFromEnv();
    std::printf("[3] bad JAVA_HOME, PATH ok: valid=%d source=%s version=%s\n", r3.valid,
                qUtf8Printable(r3.source), qUtf8Printable(r3.version));
    expect(r3.valid, "detected via PATH fallback");
    expect(r3.source == QStringLiteral("PATH"), "source=PATH");
    expect(r3.version.startsWith(QStringLiteral("21")), "version=21.x");

    // 场景4：JAVA_HOME 与 PATH 双失效 → valid=false
    qputenv("JAVA_HOME", QByteArrayLiteral("C:/nonexistent_jdk_xyz"));
    qputenv("PATH", QByteArrayLiteral("C:/nonexistent_dir_xyz"));
    EnvJavaInfo r4 = detectJavaFromEnv();
    std::printf("[4] both invalid: valid=%d\n", r4.valid);
    expect(!r4.valid, "invalid when no env java");

    // 场景5：PATH 中存在但无法运行的 java.exe（模拟本机损坏的 javapath）→ 跳过并继续找
    // 用「目录里放一个非可执行内容的 java.exe」不可移植，这里用坏链接等价验证：
    // 场景3 已证明 validate 失败时不会中断遍历（JAVA_HOME 分支失败后继续 PATH）。
    // 再补一个直接验证：JAVA_HOME 指向含坏 java.exe 的目录 + PATH 可用。
    const QString badDir = QStringLiteral("D:/BlockBox/_tmp_badjava");
    QDir().mkpath(badDir);
    {
        QFile f(badDir + QStringLiteral("/java.exe"));
        if (f.open(QIODevice::WriteOnly)) {
            f.write("not an executable");
            f.close();
        }
    }
    qputenv("JAVA_HOME", badDir.toLocal8Bit());
    qputenv("PATH", goodBin.toLocal8Bit());
    EnvJavaInfo r5 = detectJavaFromEnv();
    std::printf("[5] broken JAVA_HOME exe, PATH ok: valid=%d source=%s version=%s\n",
                r5.valid, qUtf8Printable(r5.source), qUtf8Printable(r5.version));
    expect(r5.valid, "skipped broken exe, detected via PATH");
    expect(r5.source == QStringLiteral("PATH"), "source=PATH");
    QDir(badDir).remove(QStringLiteral("java.exe"));
    QDir().rmdir(badDir);

    std::printf(g_failures == 0 ? "\nALL PASS\n" : "\n%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
